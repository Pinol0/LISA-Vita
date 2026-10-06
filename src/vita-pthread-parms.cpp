/*
 * Fix (MKXP_VITA_PTHREAD_PARMS_FIX): vitasdk's pthread-embedded pthread_create() mallocs a 12-byte
 * ThreadParms block (thread, start routine, argument) and passes it to pte_threadStart(), which reads
 * the three fields on entry and never frees it: one leak per thread. Every Ruby Fiber is a pthread
 * (coroutine/pthread), so with LISA's parallel events (up to one Fiber per frame) this was, with the
 * pthread_attr_t leak fixed in libruby v7, the ~28 bytes per frame that filled the newlib heap in 3 h
 * (d63: 56 -> 93 MiB, std::bad_alloc; d64 heap ledger: pthread_create+0x2c, one block per Fiber).
 *
 * The linker wraps pte_threadStart (-Wl,--wrap=pte_threadStart; its only user is pthread_create): the
 * fields are copied into this frame, which lives as long as the thread, and the block is freed.
 * pte_threadStart never touches the block after entry (it keeps the fields in its own frame).
 */
#include <cstdlib>
#ifdef MKXP_VITA_FIBER_CORE0
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#endif

extern "C" {
struct VitaPteThreadParms
{
    void *tid;
    void *(*start)(void *);
    void *arg;
};

int __real_pte_threadStart(void *vthreadParms);
unsigned int vitaPthreadParmsFreed = 0;   /* PERF */
__attribute__((weak)) void vitaFiberTimingThreadStart(void);   /* vita_freeze_probe.cpp (RUBY_PROF) */
#ifdef MKXP_VITA_FIBER_CORE0
/*
 * Perf fix (MKXP_VITA_FIBER_CORE0): a Ruby Fiber's thread (start routine coroutine_trampoline,
 * libruby coroutine/pthread) runs on user core 0 with the main thread. A Fiber is a coroutine of the
 * thread that resumes it: only one of them runs at a time, the other waits for the switch.
 * MKXP_VITA_CORE0_MAIN keeps core 0 for the main thread and sends every other thread to cores 1-2,
 * where the audio threads (stream decoding, mixing) run: d73 profiler, a Fiber switch took ~600 us
 * on average (2-5 per frame on the busy maps: 1.4-4.5 ms/frame) while core 0 sat idle.
 */
void *coroutine_trampoline(void *context);   /* libruby, coroutine/pthread/Context.c */
unsigned int vitaFiberCore0 = 0;   /* PERF: Fiber threads moved to core 0 */
#endif

int __wrap_pte_threadStart(void *vthreadParms)
{
    if (vitaFiberTimingThreadStart)
        vitaFiberTimingThreadStart();
    VitaPteThreadParms parms = *static_cast<VitaPteThreadParms *>(vthreadParms);
    std::free(vthreadParms);
#ifdef MKXP_VITA_FIBER_CORE0
    if (parms.start == coroutine_trampoline &&
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0) >= 0)
        __atomic_add_fetch(&vitaFiberCore0, 1, __ATOMIC_RELAXED);
#endif
    __atomic_add_fetch(&vitaPthreadParmsFreed, 1, __ATOMIC_RELAXED);
    const int r = __real_pte_threadStart(&parms);
    asm volatile("" : : "r"(&parms) : "memory");   /* parms must outlive the call: no tail call */
    return r;
}
}
