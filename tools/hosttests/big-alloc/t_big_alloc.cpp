// Host test for src/vita-big-alloc.cpp: routing of operator new/delete by size and by address.
// The Vita APIs are replaced by a fake mspace (bump allocator with a free list is not needed: the
// test only checks routing, ownership, fallback when full, and that nothing is freed by the wrong
// allocator). Build:
//   g++ -std=gnu++17 -O1 -DMKXP_VITA_BIG_ALLOC_MMAP -I. t_big_alloc.cpp -o t && ./t
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <string>

// ---- fake psp2 API -------------------------------------------------------------------------
typedef int SceUID;
typedef unsigned int SceSize;
typedef void *SceClibMspace;
struct SceClibMspaceStats { SceSize capacity, unk, peak_in_use, current_in_use; };
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 0
static unsigned char *gBlock;
static size_t gBlockSize, gUsed;
static std::map<void *, size_t> gLive;   // pool blocks
static int gForeignFree = 0;
extern "C" SceUID sceKernelAllocMemBlock(const char *, int, SceSize size, void *)
{ gBlock = (unsigned char *)std::malloc(size); gBlockSize = size; return 1; }
extern "C" int sceKernelGetMemBlockBase(SceUID, void **base) { *base = gBlock; return 0; }
extern "C" SceClibMspace sceClibMspaceCreate(void *, SceSize) { return (void *)1; }
extern "C" void *sceClibMspaceMalloc(SceClibMspace, SceSize n)
{
    if (gUsed + n > gBlockSize) return nullptr;
    void *p = gBlock + gUsed; gUsed += (n + 15) & ~15u; gLive[p] = n; return p;
}
extern "C" void sceClibMspaceFree(SceClibMspace, void *p) { if (!gLive.erase(p)) ++gForeignFree; }
extern "C" void *sceClibMspaceMemalign(SceClibMspace m, SceSize align, SceSize n)
{
    size_t off = ((size_t)(gBlock + gUsed) + align - 1) & ~(size_t)(align - 1);
    gUsed = off - (size_t)gBlock;
    return sceClibMspaceMalloc(m, n);
}
static int gRealMmap = 0, gRealMunmap = 0;
extern "C" void *__real_mmap(void *, size_t n, int, int, int, long) { ++gRealMmap; return std::malloc(n); }
extern "C" int __real_munmap(void *p, size_t) { ++gRealMunmap; std::free(p); return 0; }
extern "C" void sceClibMspaceMallocStats(SceClibMspace, SceClibMspaceStats *st)
{ size_t u = 0; for (auto &kv : gLive) u += kv.second; st->capacity = gBlockSize; st->current_in_use = u; }
#define SCE_PSP2_FAKE
// ---------------------------------------------------------------------------------------------
// Pull the implementation in with the Vita headers stubbed out.
#define _PSP2_KERNEL_CLIB_H_
#define _PSP2_KERNEL_SYSMEM_H_
#include "../../../src/vita-big-alloc.cpp"

static int fails = 0;
#define CHECK(c, m) do { bool ok_ = (c); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", m); if (!ok_) ++fails; } while (0)
static bool inBlock(const void *p) { return p >= (void *)gBlock && p < (void *)(gBlock + gBlockSize); }

int main()
{
    // Before init: large blocks come from malloc and are freed with free.
    unsigned char *volatile early = new unsigned char[512 * 1024];   // volatile: not elided
    early[0] = 1;
    CHECK(!gBlock, "before init no pool exists");
    delete[] early;

    CHECK(vitaBigAllocInit(4), "init 4 MiB pool");
    unsigned u, f, b, fb;
    vitaBigAllocStats(&u, &f, &b, &fb);
    CHECK(fb == 1, "the pre-init large allocation counted as fallback");

    int *small = new int(7);
    CHECK(!inBlock(small), "small allocation stays on the normal heap");
    delete small;

    std::vector<unsigned char> big(1024 * 1024, 1);
    CHECK(inBlock(big.data()), "1 MiB vector goes to the pool");
    std::vector<unsigned char> big2(1024 * 1024, 2);
    big.swap(big2);                                   // like BitmapPrivate::pixels.swap(px)
    CHECK(inBlock(big.data()) && inBlock(big2.data()) && big[0] == 2 && big2[0] == 1, "swap keeps both in the pool");
    big2.clear(); big2.shrink_to_fit();
    CHECK(gLive.size() == 1, "freed pool block leaves the pool");

    std::vector<unsigned char> growing;
    for (int i = 0; i < 300000; ++i) growing.push_back((unsigned char)i);   // crosses the threshold
    CHECK(inBlock(growing.data()), "vector that grows past the threshold ends in the pool");

    // Ruby fiber stacks: anonymous mmap from the pool, page aligned, freed by munmap of the base.
    void *st = __wrap_mmap(nullptr, 331776, 3, 0x22, -1, 0);
    CHECK(inBlock(st) && ((size_t)st & 4095) == 0 && gRealMmap == 0, "anonymous mmap (fiber stack) from the pool, page aligned");
    CHECK(__wrap_munmap(st, 331776) == 0 && gRealMunmap == 0 && gLive.count(st) == 0, "munmap of the base frees it in the pool");
    void *fm = __wrap_mmap(nullptr, 4096, 3, 1, 5, 0);   // file mapping: not ours
    CHECK(gRealMmap == 1 && !inBlock(fm), "file-backed mmap goes to the original implementation");
    __wrap_munmap(fm, 4096);
    CHECK(gRealMunmap == 1, "munmap outside the pool goes to the original implementation");

    std::string s(300 * 1024, 'x');
    CHECK(inBlock(s.data()), "large std::string also routed (operator new)");

    // Last (the fake pool never reuses memory): fill it, the next large requests fall back.
    unsigned fb0;
    vitaBigAllocStats(&u, &f, &b, &fb0);
    std::vector<std::vector<unsigned char>> fill;
    size_t fellBack = 0;
    for (int i = 0; i < 8; ++i) { fill.emplace_back(512 * 1024); if (!inBlock(fill.back().data())) ++fellBack; }
    vitaBigAllocStats(&u, &f, &b, &fb);
    CHECK(fellBack > 0 && fb == fb0 + fellBack, "pool full: large requests fall back and are counted");
    fill.clear();
    CHECK(gForeignFree == 0, "no pointer was freed by the wrong allocator");

    std::printf("fails=%d\n", fails);
    return fails;
}
