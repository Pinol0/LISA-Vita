/*
 * Diagnostic only (MKXP_VITA_HEAP_LEDGER): per-caller accounting of the newlib heap.
 *
 * d30/d31 soaks: newlib heap in use grew ~1 MiB per route loop (67 -> 80 MiB over 12 loops) with a
 * flat Ruby object count, until NoMemoryError. d63: ~28 bytes per frame for 3 hours (heap 56 -> 93
 * MiB, Ruby's own malloc count flat) until std::bad_alloc. This finds who holds it.
 *
 * d64: the wrapped layer is newlib's reentrant allocator (-Wl,--wrap=_malloc_r,_free_r,_realloc_r,
 * _calloc_r,_memalign_r): malloc/free/realloc/calloc/memalign/aligned_alloc all end there, and so
 * do newlib's own internal allocations (stdio, per-thread state), which the d32 malloc-level wrappers
 * did not see. A call made from inside a wrapped call (calloc -> _malloc_r, realloc -> malloc/free)
 * is passed through unrecorded: the outer call records the result.
 *
 * Every live block is recorded (pointer, size, caller tuple) in a static open-addressing table. The
 * caller tuple is the return address of the allocation call plus the next two return addresses
 * found on the stack (words that point just after a Thumb BL/BLX in the app's code, inside the
 * thread's stack), so allocations made through generic helpers (operator new, xmalloc, vglMalloc)
 * are told apart by who called the helper. heap_callers.log gets the top tuples by live bytes and the
 * biggest growers since the previous dump, by tuple and by direct caller.
 *
 * The wrappers call nothing that allocates; the dump is formatted into a static buffer under the lock
 * and written after releasing it (stdio allocates). Before vitaHeapLedgerInit (static constructors,
 * single-threaded) blocks are recorded without the lock. Callers are runtime addresses: the dump
 * header has an anchor (address of vitaHeapLedgerDump) to translate them with the ELF.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#include "vita_paths.h"

struct _reent;
extern "C" {
void *__real__malloc_r(struct _reent *r, size_t n);
void __real__free_r(struct _reent *r, void *p);
void *__real__realloc_r(struct _reent *r, void *p, size_t n);
void *__real__calloc_r(struct _reent *r, size_t a, size_t b);
void *__real__memalign_r(struct _reent *r, size_t align, size_t n);
}

#ifndef VITA_LEDGER_TEXT_RANGE
extern "C" void _init(void);
extern "C" void _fini(void);
/* The app's code: _init is first in .text, _fini last (crt). */
#define VITA_LEDGER_TEXT_RANGE(lo, hi) do { (lo) = (uintptr_t)&_init; (hi) = (uintptr_t)&_fini; } while (0)
#endif

namespace {
const unsigned kBits = 18;                      /* 262,144 slots x 12 bytes = 3 MiB (static) */
const unsigned kSlots = 1u << kBits;
struct Slot { uintptr_t ptr; uint32_t size; uint32_t tuple; };
Slot gSlots[kSlots];
unsigned gLive = 0, gUntracked = 0;
uint64_t gLiveBytes = 0;

/* Interned caller tuples; index 0 = "tuple table full". */
const unsigned kTupBits = 14;
const unsigned kTup = 1u << kTupBits;
struct Tuple { uint32_t c[3]; };
Tuple gTup[kTup];
uint16_t gTupHash[kTup * 2];                    /* open addressing over tuple indices (0 = empty) */
unsigned gTupCount = 1;

SceKernelLwMutexWork gLock;
bool gLockReady = false;
SceUID gOwner = 0;
int gDepth = 0;

/* Takes the ledger for an outer call; false for a call nested in one (same thread). */
inline bool enter()
{
    if (!gLockReady)
        return gDepth++ == 0;
    const SceUID me = sceKernelGetThreadId();
    if (gOwner == me) {   /* only this thread can have stored its own id */
        ++gDepth;
        return false;
    }
    sceKernelLockLwMutex(&gLock, 1, nullptr);
    gOwner = me;
    gDepth = 1;
    return true;
}
inline void leave()
{
    if (--gDepth == 0 && gLockReady) {
        gOwner = 0;
        sceKernelUnlockLwMutex(&gLock, 1);
    }
}

inline unsigned hashOf(uintptr_t p) { return (uint32_t)((p >> 3) * 2654435761u) >> (32 - kBits); }

uint32_t internTuple(const uint32_t c[3])
{
    uint32_t h = (c[0] * 2654435761u) ^ (c[1] * 2246822519u) ^ (c[2] * 3266489917u);
    unsigned i = h >> (32 - (kTupBits + 1));
    for (unsigned probes = 0; probes < kTup * 2; ++probes, i = (i + 1) & (kTup * 2 - 1)) {
        const unsigned t = gTupHash[i];
        if (!t) {
            if (gTupCount >= kTup)
                return 0;
            gTup[gTupCount] = Tuple{ { c[0], c[1], c[2] } };
            gTupHash[i] = (uint16_t)gTupCount;
            return gTupCount++;
        }
        if (gTup[t].c[0] == c[0] && gTup[t].c[1] == c[1] && gTup[t].c[2] == c[2])
            return t;
    }
    return 0;
}

/* Per-thread stack bounds (direct-mapped cache, refreshed when sp is outside the cached range). */
struct StackBounds { SceUID thid; uintptr_t lo, hi; };
StackBounds gStacks[64];

bool stackBounds(uintptr_t sp, uintptr_t &lo, uintptr_t &hi)
{
    const SceUID me = sceKernelGetThreadId();
    StackBounds &s = gStacks[((uint32_t)me * 2654435761u) >> 26];
    if (s.thid != me || sp < s.lo || sp >= s.hi) {
        SceKernelThreadInfo info;
        std::memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetThreadInfo(me, &info) < 0 || !info.stack || info.stackSize <= 0)
            return false;
        s.thid = me;
        s.lo = (uintptr_t)info.stack;
        s.hi = s.lo + (uintptr_t)info.stackSize;
        if (sp < s.lo || sp >= s.hi)
            return false;
    }
    lo = s.lo;
    hi = s.hi;
    return true;
}

/* A Thumb return address: odd, in the app's code, right after a BL / BLX imm (32-bit) or BLX reg. */
inline bool isReturnAddress(uintptr_t v, uintptr_t textLo, uintptr_t textHi)
{
    if (!(v & 1) || v < textLo + 5 || v >= textHi)
        return false;
    const uintptr_t a = v - 1;
    const uint16_t hw1 = *(const uint16_t *)(a - 4), hw2 = *(const uint16_t *)(a - 2);
    if ((hw1 & 0xF800) == 0xF000 && ((hw2 & 0xD000) == 0xD000 || (hw2 & 0xD001) == 0xC000))
        return true;
    return (hw2 & 0xFF87) == 0x4780;
}

__attribute__((noinline)) void callerTuple(uintptr_t c0, uint32_t out[3])
{
    out[0] = (uint32_t)c0;
    out[1] = out[2] = 0;
    uintptr_t textLo, textHi;
    VITA_LEDGER_TEXT_RANGE(textLo, textHi);
    volatile uintptr_t here = 0;
    const uintptr_t sp = (uintptr_t)&here;
    uintptr_t lo, hi;
    if (textHi <= textLo || !stackBounds(sp, lo, hi))
        return;
    const uintptr_t *w = (const uintptr_t *)((sp + 3) & ~(uintptr_t)3);
    const uintptr_t *end = (const uintptr_t *)std::min<uintptr_t>(hi, (uintptr_t)w + 96 * sizeof(uintptr_t));
    unsigned k = 1;
    bool seenC0 = false;
    for (; w < end && k < 3; ++w) {
        const uintptr_t v = *w;
        if (!isReturnAddress(v, textLo, textHi))
            continue;
        if (v == c0 && !seenC0) {   /* the wrapper's own saved LR */
            seenC0 = true;
            continue;
        }
        out[k++] = (uint32_t)v;
    }
}

void insert(void *p, size_t size, uint32_t tuple)
{
    if (!p)
        return;
    if (gLive >= kSlots - kSlots / 8) {
        ++gUntracked;
        return;
    }
    unsigned i = hashOf((uintptr_t)p);
    while (gSlots[i].ptr)
        i = (i + 1) & (kSlots - 1);
    gSlots[i].ptr = (uintptr_t)p;
    gSlots[i].size = (uint32_t)size;
    gSlots[i].tuple = tuple;
    ++gLive;
    gLiveBytes += size;
}

/* Linear probing with backward-shift deletion (no tombstones). */
void erase(void *p)
{
    if (!p)
        return;
    unsigned i = hashOf((uintptr_t)p);
    while (gSlots[i].ptr && gSlots[i].ptr != (uintptr_t)p)
        i = (i + 1) & (kSlots - 1);
    if (!gSlots[i].ptr)
        return;   /* not tracked (table was full) */
    gLiveBytes -= gSlots[i].size;
    --gLive;
    unsigned j = i;
    for (;;) {
        gSlots[i].ptr = 0;
        for (;;) {
            j = (j + 1) & (kSlots - 1);
            if (!gSlots[j].ptr)
                return;
            const unsigned k = hashOf(gSlots[j].ptr);
            /* can slot j move to i? only if its home k is not cyclically in (i, j] */
            if (i <= j ? (k <= i || k > j) : (k <= i && k > j))
                break;
        }
        gSlots[i] = gSlots[j];
        i = j;
    }
}

uint32_t tupleFor(uintptr_t c0)
{
    uint32_t c[3];
    callerTuple(c0, c);
    return internTuple(c);
}

/* Dump aggregation: per tuple (direct index) and per direct caller (hash), with previous values. */
uint32_t gTupCnt[kTup];
uint64_t gTupBytes[kTup], gTupPrev[kTup];
struct Agg { uint32_t caller; uint32_t count; uint64_t bytes; };
const unsigned kAgg = 8192;
Agg gAgg[kAgg], gPrev[kAgg];
char gOut[24576];
int gDumps = 0;
}

extern "C" {
/* Set by operator new (vita-big-alloc.cpp) just before its malloc: attribute to its caller. */
volatile uintptr_t vitaHeapLedgerNewCaller = 0;

void *__wrap__malloc_r(struct _reent *r, size_t n)
{
    const uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    if (!enter()) {
        void *p = __real__malloc_r(r, n);
        leave();
        return p;
    }
    void *p = __real__malloc_r(r, n);
    uintptr_t caller = ra;
    if (vitaHeapLedgerNewCaller) {
        caller = vitaHeapLedgerNewCaller;
        vitaHeapLedgerNewCaller = 0;
    }
    if (p)
        insert(p, n, tupleFor(caller));
    leave();
    return p;
}
void __wrap__free_r(struct _reent *r, void *p)
{
    if (!p)
        return;
    const bool outer = enter();
    if (outer)
        erase(p);
    __real__free_r(r, p);
    leave();
}
void *__wrap__realloc_r(struct _reent *r, void *p, size_t n)
{
    const uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    const bool outer = enter();
    void *q = __real__realloc_r(r, p, n);
    if (outer && (q || n == 0)) {
        erase(p);
        if (q)
            insert(q, n, tupleFor(ra));
    }
    leave();
    return q;
}
void *__wrap__calloc_r(struct _reent *r, size_t a, size_t b)
{
    const uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    const bool outer = enter();
    void *p = __real__calloc_r(r, a, b);
    if (outer && p)
        insert(p, a * b, tupleFor(ra));
    leave();
    return p;
}
void *__wrap__memalign_r(struct _reent *r, size_t align, size_t n)
{
    const uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    const bool outer = enter();
    void *p = __real__memalign_r(r, align, n);
    if (outer && p)
        insert(p, n, tupleFor(ra));
    leave();
    return p;
}

/* main.cpp, first thing in main(): from here on the wrappers lock. */
void vitaHeapLedgerInit()
{
    if (sceKernelCreateLwMutex(&gLock, "heap_ledger", 0, 0, nullptr) >= 0)
        gLockReady = true;
}

/* PERF line (vita_diag.cpp): tracked live blocks / bytes, untracked allocations. */
void vitaHeapLedgerTotals(unsigned *blocks, unsigned *kb, unsigned *untracked)
{
    *blocks = gLive;
    *kb = (unsigned)(gLiveBytes / 1024);
    *untracked = gUntracked;
}

static unsigned aggFind(const Agg *t, uint32_t c)
{
    unsigned h = (c * 2654435761u) >> (32 - 13);
    while (t[h].count && t[h].caller != c)
        h = (h + 1) & (kAgg - 1);
    return h;
}

void vitaHeapLedgerDump(const char *reason)
{
    int len = 0;
    static uint16_t idx[kTup];
    static uint16_t cidx[kAgg];
    static long long cdelta[kAgg];
    if (!enter()) {   /* never nested in an allocation */
        leave();
        return;
    }
    std::memset(gTupCnt, 0, sizeof(gTupCnt));
    std::memset(gTupBytes, 0, sizeof(gTupBytes));
    std::memset(gAgg, 0, sizeof(gAgg));
    unsigned nc = 0;
    for (unsigned i = 0; i < kSlots; ++i) {
        if (!gSlots[i].ptr)
            continue;
        const uint32_t t = gSlots[i].tuple;
        gTupCnt[t]++;
        gTupBytes[t] += gSlots[i].size;
        const uint32_t c0 = t ? gTup[t].c[0] : 0;
        const unsigned h = aggFind(gAgg, c0);
        if (!gAgg[h].count) {
            if (nc >= kAgg - kAgg / 8)
                continue;
            gAgg[h].caller = c0;
            cidx[nc++] = (uint16_t)h;
        }
        gAgg[h].count++;
        gAgg[h].bytes += gSlots[i].size;
    }
    unsigned n = 0;
    for (unsigned t = 0; t < gTupCount; ++t)
        if (gTupCnt[t] || gTupPrev[t])
            idx[n++] = (uint16_t)t;
    len += std::snprintf(gOut + len, sizeof(gOut) - len,
                         "===== HEAP_LEDGER2 #%d t_s=%u reason=%s anchor=%p live_blocks=%u live_kb=%u untracked=%u tuples=%u callers=%u =====\n",
                         gDumps, (unsigned)(sceKernelGetProcessTimeWide() / 1000000), reason ? reason : "",
                         (void *)&vitaHeapLedgerDump, gLive, (unsigned)(gLiveBytes / 1024), gUntracked, gTupCount, nc);
    std::sort(idx, idx + n, [](uint16_t x, uint16_t y) { return gTupBytes[x] > gTupBytes[y]; });
    for (unsigned k = 0; k < n && k < 30 && len < (int)sizeof(gOut) - 200; ++k) {
        const unsigned t = idx[k];
        len += std::snprintf(gOut + len, sizeof(gOut) - len, "  top kb=%llu blocks=%u delta_kb=%lld tuple=%p,%p,%p\n",
                             (unsigned long long)(gTupBytes[t] / 1024), gTupCnt[t],
                             gDumps ? ((long long)gTupBytes[t] - (long long)gTupPrev[t]) / 1024 : 0,
                             (void *)(uintptr_t)gTup[t].c[0], (void *)(uintptr_t)gTup[t].c[1], (void *)(uintptr_t)gTup[t].c[2]);
    }
    if (gDumps) {
        /* the biggest growers since the previous dump, even if not in the top 30 by size */
        std::sort(idx, idx + n, [](uint16_t x, uint16_t y) {
            return (long long)gTupBytes[x] - (long long)gTupPrev[x] > (long long)gTupBytes[y] - (long long)gTupPrev[y];
        });
        for (unsigned k = 0; k < n && k < 20 && len < (int)sizeof(gOut) - 200; ++k) {
            const unsigned t = idx[k];
            const long long d = (long long)gTupBytes[t] - (long long)gTupPrev[t];
            if (d < 1024)
                break;
            len += std::snprintf(gOut + len, sizeof(gOut) - len, "  grower delta_bytes=%lld kb=%llu blocks=%u tuple=%p,%p,%p\n",
                                 d, (unsigned long long)(gTupBytes[t] / 1024), gTupCnt[t],
                                 (void *)(uintptr_t)gTup[t].c[0], (void *)(uintptr_t)gTup[t].c[1], (void *)(uintptr_t)gTup[t].c[2]);
        }
        for (unsigned k = 0; k < nc; ++k) {
            const Agg &a = gAgg[cidx[k]];
            const unsigned ph = aggFind(gPrev, a.caller);
            cdelta[cidx[k]] = (long long)a.bytes - (gPrev[ph].count ? (long long)gPrev[ph].bytes : 0);
        }
        std::sort(cidx, cidx + nc, [](uint16_t x, uint16_t y) { return cdelta[x] > cdelta[y]; });
        for (unsigned k = 0; k < nc && k < 12 && len < (int)sizeof(gOut) - 200; ++k) {
            const Agg &a = gAgg[cidx[k]];
            if (cdelta[cidx[k]] < 1024)
                break;
            len += std::snprintf(gOut + len, sizeof(gOut) - len, "  grower_caller delta_bytes=%lld kb=%llu blocks=%u caller=%p\n",
                                 cdelta[cidx[k]], (unsigned long long)(a.bytes / 1024), a.count, (void *)(uintptr_t)a.caller);
        }
    }
    std::memcpy(gTupPrev, gTupBytes, sizeof(gTupBytes));
    std::memcpy(gPrev, gAgg, sizeof(gAgg));
    ++gDumps;
    leave();

    FILE *f = std::fopen(VITA_GAME_ROOT "heap_callers.log", "a");
    if (f) {
        std::fwrite(gOut, 1, (size_t)len, f);
        std::fclose(f);
    }
}
}
