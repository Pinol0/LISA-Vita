// Host test for src/vita-pthread-parms.cpp: a fake pte_threadStart reads the three ThreadParms fields
// on entry, like the vitasdk one, then runs the start routine. The source is compiled with std::free
// replaced by testFree, which scribbles over the block and counts it: the wrapper must free every
// block exactly once and keep the fields valid (a use of the freed block reads the scribble).
//   ./run.sh   (also runs the two mutations: no copy, no free)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
static std::set<void *> gFreed;
static int gDoubleFree = 0;
static void testFree(void *p) { if (!gFreed.insert(p).second) ++gDoubleFree; std::memset(p, 0xDD, 12); }
#include SRC
static int ran = 0;
static void *startRoutine(void *arg) { ran += *(int *)arg; return nullptr; }
extern "C" int __real_pte_threadStart(void *v)
{
    VitaPteThreadParms *p = (VitaPteThreadParms *)v;
    if (p->start != startRoutine) return -1;   // read the freed (scribbled) block
    p->start(p->arg);
    return 7;
}
int main()
{
    int one = 1, fails = 0;
    for (int i = 0; i < 1000; ++i) {
        VitaPteThreadParms *p = (VitaPteThreadParms *)std::malloc(sizeof(VitaPteThreadParms));
        p->tid = p; p->start = startRoutine; p->arg = &one;
        if (__wrap_pte_threadStart(p) != 7) ++fails;
    }
    if (ran != 1000 || gFreed.size() != 1000 || gDoubleFree || vitaPthreadParmsFreed != 1000) ++fails;
    std::printf("%s  1000 starts: routine ran %d times, return value kept, %zu blocks freed once each\n",
                fails ? "FAIL" : "PASS", ran, gFreed.size());
    return fails ? 1 : 0;
}
