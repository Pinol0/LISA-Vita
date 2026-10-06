// Host test for src/vita-heap-ledger.cpp: table consistency under random alloc/free/realloc against an
// exact reference, and the per-caller dump (top callers, growers). Build:
//   g++ -std=gnu++17 -O1 -I. t_heap_ledger.cpp -o t && ./t
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <cstring>
typedef struct { int d[8]; } SceKernelLwMutexWork;
typedef int SceUID;
typedef struct { unsigned size; void *stack; int stackSize; } SceKernelThreadInfo;
extern "C" int sceKernelCreateLwMutex(SceKernelLwMutexWork *, const char *, int, int, void *) { return 0; }
static int gLocked = 0, gMaxLocked = 0;
extern "C" int sceKernelLockLwMutex(SceKernelLwMutexWork *, int, void *) { if (++gLocked > gMaxLocked) gMaxLocked = gLocked; return 0; }
extern "C" int sceKernelUnlockLwMutex(SceKernelLwMutexWork *, int) { --gLocked; return 0; }
extern "C" SceUID sceKernelGetThreadId() { return 0x40010003; }
extern "C" int sceKernelGetThreadInfo(SceUID, SceKernelThreadInfo *) { return -1; }
extern "C" unsigned long long sceKernelGetProcessTimeWide() { return 0; }
#define VITA_LEDGER_TEXT_RANGE(lo, hi) do { (lo) = 0; (hi) = 0; } while (0)
struct _reent;
extern "C" void *__wrap__malloc_r(struct _reent *r, size_t n);
extern "C" void __wrap__free_r(struct _reent *r, void *p);
extern "C" void *__real__malloc_r(struct _reent *, size_t n) { return std::malloc(n); }
extern "C" void __real__free_r(struct _reent *, void *p) { std::free(p); }
// like newlib: realloc and calloc are built on the (wrapped) _malloc_r / _free_r
extern "C" void *__real__realloc_r(struct _reent *r, void *p, size_t n) {
    void *q = __wrap__malloc_r(r, n); if (q && p) { std::memcpy(q, p, 1); } __wrap__free_r(r, p); return q; }
extern "C" void *__real__calloc_r(struct _reent *r, size_t a, size_t b) { void *p = __wrap__malloc_r(r, a * b); if (p) std::memset(p, 0, a * b); return p; }
extern "C" void *__real__memalign_r(struct _reent *, size_t a, size_t n) { void *p = nullptr; return posix_memalign(&p, a, n) ? nullptr : p; }
#include "../../../src/vita-heap-ledger.cpp"

static int fails = 0;
#define CHECK(c, m) do { bool ok_ = (c); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", m); if (!ok_) ++fails; } while (0)
__attribute__((noinline)) void *allocA(size_t n) { return __wrap__malloc_r(nullptr, n); }
__attribute__((noinline)) void *allocB(size_t n) { return __wrap__calloc_r(nullptr, 1, n); }

int main()
{
    vitaHeapLedgerInit();
    gMaxLocked = 0;
    std::mt19937 rng(42);
    std::map<void *, size_t> ref;
    uint64_t refBytes = 0;
    for (int it = 0; it < 400000; ++it) {
        int op = rng() % 10;
        if (op < 5 || ref.empty()) {
            size_t n = 1 + rng() % 5000;
            void *p = (rng() & 1) ? allocA(n) : allocB(n);
            ref[p] = n; refBytes += n;
        } else if (op < 9) {
            auto itr = ref.begin(); std::advance(itr, rng() % ref.size());
            refBytes -= itr->second; __wrap__free_r(nullptr, itr->first); ref.erase(itr);
        } else {
            auto itr = ref.begin(); std::advance(itr, rng() % ref.size());
            size_t n = 1 + rng() % 8000; void *q = __wrap__realloc_r(nullptr, itr->first, n);
            refBytes -= itr->second; ref.erase(itr); ref[q] = n; refBytes += n;
        }
        if ((it & 0xffff) == 0 || it == 399999) {
            unsigned b, kb, u; vitaHeapLedgerTotals(&b, &kb, &u);
            if (b != ref.size() || kb != refBytes / 1024) { std::printf("  mismatch at %d: %u vs %zu\n", it, b, ref.size()); ++fails; }
        }
    }
    unsigned b, kb, u; vitaHeapLedgerTotals(&b, &kb, &u);
    CHECK(b == ref.size() && kb == refBytes / 1024 && u == 0, "table matches the reference after 400k random ops (backward-shift delete)");
    // every tracked pointer findable, every freed one gone
    size_t found = 0;
    for (auto &kv : ref) { unsigned i = hashOf((uintptr_t)kv.first); while (gSlots[i].ptr && gSlots[i].ptr != (uintptr_t)kv.first) i = (i + 1) & (kSlots - 1); found += gSlots[i].ptr == (uintptr_t)kv.first && gSlots[i].size == kv.second; }
    CHECK(found == ref.size(), "every live block is findable with its size");
    std::remove("heap_callers.log");
    vitaHeapLedgerDump("first");
    for (int i = 0; i < 200; ++i) ref[allocA(10000)] = 10000;   // allocA grows by ~2 MB
    vitaHeapLedgerDump("second");
    FILE *f = std::fopen("heap_callers.log", "r"); std::string all; char line[512];
    while (f && std::fgets(line, sizeof line, f)) all += line;
    if (f) std::fclose(f);
    CHECK(all.find("HEAP_LEDGER2 #1") != std::string::npos && all.find("grower delta_bytes=") != std::string::npos && all.find("grower_caller delta_bytes=") != std::string::npos, "second dump lists growers (tuple and caller)");
    size_t g = all.find("grower delta_bytes=");
    CHECK(g != std::string::npos && all.find("delta_bytes=2000000 ", g) == g + 7, "the top grower is the caller that allocated 200 x 10000 bytes");
    CHECK(gLocked == 0 && gMaxLocked == 1, "nested calls (realloc/calloc -> malloc/free) never lock twice; lock released");
    for (auto &kv : ref) __wrap__free_r(nullptr, kv.first);
    vitaHeapLedgerTotals(&b, &kb, &u);
    CHECK(b == 0 && kb == 0, "all freed: table empty");
    std::printf("fails=%d\n", fails);
    return fails;
}
