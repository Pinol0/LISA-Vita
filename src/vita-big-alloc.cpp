/*
 * Fix (MKXP_VITA_BIG_ALLOC): C++ allocations of MKXP_VITA_BIG_ALLOC_MIN bytes or more come from a
 * dedicated sceClibMspace on its own memblock instead of the newlib heap.
 *
 * Why (d27/d28 soaks): the newlib heap fragmented until no large block fitted any more. Heap in use
 * stayed flat (~90 MiB) while the arena grew to its limit (d28: 143/144 MiB, 46 MiB free but in
 * pieces, 3 KiB at the top) -> std::bad_alloc. The large blocks - Bitmap CPU copies (up to 22 MiB in
 * battle), PNG/text cache entries, audio PCM - were interleaved with thousands of small Ruby
 * allocations, so the holes they left could not take another large block.
 *
 * All those large blocks are C++ (std::vector), so the replaceable global operator new/delete route
 * them by size: the pool only ever holds large blocks of similar sizes. Ruby, vitaGL, OpenAL and SDL
 * use malloc and stay on the newlib heap. operator delete finds the owner from the address range.
 * When the pool is full (or before vitaBigAllocInit, i.e. static constructors) the newlib heap is
 * used: counted as a fallback, never an error.
 *
 * The memory is taken from the user memory left after the newlib heap and before vglInit (which gives
 * vitaGL everything left but a threshold): MKXP_VITA_HEAP_MB is lowered by the same amount so the
 * total budget and vitaGL's pools do not change.
 */
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <pthread.h>

#ifndef MKXP_VITA_BIG_ALLOC_MIN
#define MKXP_VITA_BIG_ALLOC_MIN (128 * 1024)
#endif

namespace {
SceClibMspace gPool = nullptr;
uintptr_t gPoolBase = 0, gPoolEnd = 0;
pthread_mutex_t gPoolLock = PTHREAD_MUTEX_INITIALIZER;
unsigned gFallbacks = 0;       /* large requests served by the newlib heap (pool full / not ready) */
unsigned gPoolBlocks = 0;      /* live blocks in the pool */

inline bool inPool(const void *p)
{
    const uintptr_t a = (uintptr_t)p;
    return a >= gPoolBase && a < gPoolEnd;
}

void *bigAlloc(std::size_t n)
{
    if (n == 0)
        n = 1;
    if (n >= (std::size_t)MKXP_VITA_BIG_ALLOC_MIN && gPool) {
        pthread_mutex_lock(&gPoolLock);
        void *p = sceClibMspaceMalloc(gPool, n);
        if (p)
            ++gPoolBlocks;
        else
            ++gFallbacks;
        pthread_mutex_unlock(&gPoolLock);
        if (p)
            return p;
    } else if (n >= (std::size_t)MKXP_VITA_BIG_ALLOC_MIN) {
        ++gFallbacks;   /* before init: static constructors */
    }
    return std::malloc(n);
}

void bigFree(void *p)
{
    if (!p)
        return;
    if (inPool(p)) {
        pthread_mutex_lock(&gPoolLock);
        sceClibMspaceFree(gPool, p);
        --gPoolBlocks;
        pthread_mutex_unlock(&gPoolLock);
        return;
    }
    std::free(p);
}
} // namespace

/* main.cpp, before vglInit. Returns false (and everything stays on the newlib heap) on failure. */
bool vitaBigAllocInit(unsigned int megabytes)
{
    const SceSize size = (SceSize)megabytes * 1024 * 1024;
    SceUID blk = sceKernelAllocMemBlock("mkxp_big_alloc", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, nullptr);
    if (blk < 0)
        return false;
    void *base = nullptr;
    if (sceKernelGetMemBlockBase(blk, &base) < 0 || !base)
        return false;
    SceClibMspace pool = sceClibMspaceCreate(base, size);
    if (!pool)
        return false;
    gPoolBase = (uintptr_t)base;
    gPoolEnd = gPoolBase + size;
    gPool = pool;   /* published last: bigAlloc checks gPool */
    return true;
}

/* PERF accounting (vita_diag.cpp). */
extern "C" void vitaBigAllocStats(unsigned *usedKb, unsigned *freeKb, unsigned *blocks, unsigned *fallbacks)
{
    *usedKb = *freeKb = 0;
    if (gPool) {
        SceClibMspaceStats st;
        pthread_mutex_lock(&gPoolLock);
        sceClibMspaceMallocStats(gPool, &st);
        pthread_mutex_unlock(&gPoolLock);
        *usedKb = (unsigned)(st.current_in_use / 1024);
        *freeKb = (unsigned)((st.capacity - st.current_in_use) / 1024);
    }
    *blocks = gPoolBlocks;
    *fallbacks = gFallbacks;
}

/*
 * Ruby's Fiber stacks (MKXP_VITA_BIG_ALLOC_MMAP, -Wl,--wrap=mmap,--wrap=munmap): Ruby allocates each
 * fiber pool (count x 324 KiB stacks) with mmap, which vita-compat implements as memalign(4096) on the
 * newlib heap. d30 soak: 98,080 fibers, i.e. as many 324 KiB blocks churned through the fragmented
 * heap, until "FiberError: can't alloc machine stack to fiber (1 x 331776 bytes)". Same semantics as
 * vita-compat (anonymous mappings only, page aligned, whole-block munmap as cont.c does), from the pool.
 */
#ifdef MKXP_VITA_BIG_ALLOC_MMAP
extern "C" {
void *__real_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
int __real_munmap(void *addr, size_t length);

void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
    if (fd == -1 && length > 0 && gPool) {
        pthread_mutex_lock(&gPoolLock);
        void *p = sceClibMspaceMemalign(gPool, 4096, length);
        if (p)
            ++gPoolBlocks;
        else
            ++gFallbacks;
        pthread_mutex_unlock(&gPoolLock);
        if (p)
            return p;
    }
    return __real_mmap(addr, length, prot, flags, fd, offset);
}

int __wrap_munmap(void *addr, size_t length)
{
    if (addr && inPool(addr)) {
        bigFree(addr);
        return 0;
    }
    return __real_munmap(addr, length);
}
}
#endif

/* Heap ledger (vita-heap-ledger.cpp, diagnostic): the next wrapped malloc records this caller instead
 * of operator new itself, so small C++ objects are attributed to the code that creates them. */
extern "C" __attribute__((weak)) volatile uintptr_t vitaHeapLedgerNewCaller;

#ifdef MKXP_VITA_OOM_LOG
/*
 * Diagnostic (MKXP_VITA_OOM_LOG): before operator new throws std::bad_alloc (which no caller catches:
 * terminate -> abort, d63/d66), one qa.log line with the request and the state of both allocators.
 * Only a static buffer and sceIo: nothing here allocates. The first 8 failures, then every 16th
 * (d92: the 8 lines ran out 20 minutes before the failure that mattered), at most 64 lines;
 * n= counts them all.
 */
#include <algorithm>
#include <cstdio>
#include <malloc.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include "vita_paths.h"
extern "C" void vitaBigAllocStats(unsigned *usedKb, unsigned *freeKb, unsigned *blocks, unsigned *fallbacks);
static void vitaOomLog(std::size_t n, uintptr_t caller)
{
    static unsigned total = 0, logged = 0;
    static char buf[340];
    ++total;
    if (logged >= 64 || (total > 8 && total % 16))
        return;
    ++logged;
    const struct mallinfo mi = mallinfo();
    unsigned bu = 0, bf = 0, bb = 0, bfb = 0;
    vitaBigAllocStats(&bu, &bf, &bb, &bfb);
    const int len = std::snprintf(buf, sizeof(buf),
        "OOM n=%u operator_new size=%u caller=%p anchor=%p thread=0x%x heap_used_kb=%u heap_arena_kb=%u heap_free_kb=%u "
        "big_used_kb=%u big_free_kb=%u big_blocks=%u big_fallbacks=%u\n",
        total, (unsigned)n, (void *)caller, (void *)&vitaBigAllocStats, (unsigned)sceKernelGetThreadId(),
        (unsigned)(mi.uordblks / 1024), (unsigned)(mi.arena / 1024), (unsigned)(mi.fordblks / 1024), bu, bf, bb, bfb);
    const SceUID fd = sceIoOpen(VITA_GAME_ROOT "qa.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        if (len > 0)
            sceIoWrite(fd, buf, (SceSize)std::min<int>(len, (int)sizeof(buf) - 1));
        sceIoClose(fd);
    }
}
#endif

/* Replaceable global allocation functions (C++17). Failure -> std::bad_alloc, as the defaults. */
void *operator new(std::size_t n)
{
    if (&vitaHeapLedgerNewCaller && n < (std::size_t)MKXP_VITA_BIG_ALLOC_MIN)   /* only the malloc path */
        vitaHeapLedgerNewCaller = (uintptr_t)__builtin_return_address(0);
    void *p = bigAlloc(n);
    if (!p) {
#ifdef MKXP_VITA_OOM_LOG
        vitaOomLog(n, (uintptr_t)__builtin_return_address(0));
#endif
        throw std::bad_alloc();
    }
    return p;
}
void *operator new[](std::size_t n)
{
    return operator new(n);
}
void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
    return bigAlloc(n);
}
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept
{
    return bigAlloc(n);
}
void operator delete(void *p) noexcept { bigFree(p); }
void operator delete[](void *p) noexcept { bigFree(p); }
void operator delete(void *p, std::size_t) noexcept { bigFree(p); }
void operator delete[](void *p, std::size_t) noexcept { bigFree(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { bigFree(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { bigFree(p); }
