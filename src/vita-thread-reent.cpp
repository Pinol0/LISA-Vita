/*
 * Fix (MKXP_VITA_THREAD_REENT_FIX): vitasdk newlib keeps per-thread C library state (struct _reent)
 * in a fixed table of 256 slots (reent_list, __getreent_for_thread). A slot is taken the first time a
 * thread uses the C library and is given back only by vitasdk_delete_thread_reent(), which
 * pthread-embedded calls (pte_osThreadDelete / pte_osThreadExitAndDelete) but SDL's Vita thread
 * backend does not: SDL_SYS_WaitThread() ends with sceKernelWaitThreadEnd + sceKernelDeleteThread.
 *
 * mkxp-z starts an SDL thread for every BGM/BGS stream start, ME watch and fade, so every music change
 * leaked a slot. d29 soak: after ~50 min (step 196) the table was full, __getreent_for_thread
 * returned NULL and pthread_create (a Ruby Fiber) crashed in pte_osThreadCreate (DFAR 0xfffffffc).
 *
 * The linker wraps sceKernelDeleteThread (-Wl,--wrap=sceKernelDeleteThread): before any thread is
 * deleted, its slot is released. The thread has ended but still exists, so its TLS (where newlib
 * keeps the slot pointer) is still readable; for pthread threads the slot is already released and
 * vitasdk_delete_thread_reent does nothing (TLS pointer already cleared).
 */
#include <psp2/kernel/threadmgr.h>
#include <vitasdk/utils.h>   /* vitasdk_delete_thread_reent (public vitasdk API) */

extern "C" {
int __real_sceKernelDeleteThread(SceUID thid);
void *sceKernelGetThreadTLSAddr(SceUID thid, int key);

unsigned int vitaThreadReentReleased = 0;             /* PERF: slots given back by this wrapper */

int __wrap_sceKernelDeleteThread(SceUID thid)
{
    /* newlib's TLS key for the reent pointer (0x89, see __getreent_for_thread). */
    void **tls = (void **)sceKernelGetThreadTLSAddr(thid, 0x89);
    if (tls && *tls) {
        if (vitasdk_delete_thread_reent(thid))
            ++vitaThreadReentReleased;
    }
    return __real_sceKernelDeleteThread(thid);
}
}
