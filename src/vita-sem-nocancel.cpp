/*
 * Perf fix (MKXP_VITA_SEM_NOCANCEL): blocking waits without the 500 us cancellation poll.
 * vitasdk's pthread-embedded implements a cancellable wait (pte_osSemaphoreCancellablePend,
 * vita_osal.o) as a loop of sceKernelPollEventFlag(cancel flag) + sceKernelWaitSema(timeout 500 us):
 * a thread blocked in pthread_cond_wait (-> sem_timedwait(semBlockQueue, NULL)) or sem_wait wakes up
 * ~2000 times per second only to check for pthread_cancel. Every Ruby Fiber is a pthread waiting in
 * pthread_cond_wait while suspended (libruby coroutine/pthread, Context.c), all on core 0 with the
 * main thread (MKXP_VITA_FIBER_CORE0), plus the idle threads of the Fiber pool (libruby v8) and the
 * main thread itself during every Fiber switch (d77: ~5 switches per frame at 600-900 us on the
 * busy maps).
 * No thread can be cancelled in this program: pthread_cancel is not linked (weak, undefined; checked
 * below at run time: if it ever gets linked, every call goes to the original cancellable functions).
 * Then the cancellable wait and sem_wait_nocancel (pthread-embedded's own non-cancellable variant,
 * sem_wait.o: same value accounting, one sceKernelWaitSema without timeout) behave the same except
 * for the periodic wake-ups. The linker wraps sem_wait and sem_timedwait (-Wl,--wrap=...): the calls
 * from pthread_cond_wait / signal / broadcast / destroy and OpenAL's alsem_wait come here.
 * sem_timedwait with a real timeout (pthread_cond_timedwait: Ruby's native_cond_timedwait) is left
 * to the original, which needs the 500 us slices for its deadline.
 */
#include <pthread.h>
#include <semaphore.h>

#pragma weak pthread_cancel

extern "C" {
int sem_wait_nocancel(sem_t *sem);   /* pthread-embedded, sem_wait.o (not in semaphore.h) */
int __real_sem_wait(sem_t *sem);
int __real_sem_timedwait(sem_t *sem, const struct timespec *abstime);

unsigned int vitaSemNocancelWaits = 0;   /* PERF sem_nc: waits redirected (cumulative) */

static inline bool canCancel() { return &pthread_cancel != nullptr; }

int __wrap_sem_wait(sem_t *sem)
{
    if (canCancel())
        return __real_sem_wait(sem);
    __atomic_add_fetch(&vitaSemNocancelWaits, 1u, __ATOMIC_RELAXED);
    return sem_wait_nocancel(sem);
}

int __wrap_sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    if (abstime || canCancel())
        return __real_sem_timedwait(sem, abstime);
    __atomic_add_fetch(&vitaSemNocancelWaits, 1u, __ATOMIC_RELAXED);
    return sem_wait_nocancel(sem);
}
}
