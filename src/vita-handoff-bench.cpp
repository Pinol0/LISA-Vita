/*
 * Diagnostic (MKXP_VITA_HANDOFF_BENCH): what one thread-to-thread handoff costs on this console.
 * d73/d74 profiler: a Ruby Fiber switch (coroutine/pthread: mutex + condition variable handoff
 * between two pthreads, Context.c) took 150-680 us on average, also with the Fiber threads on core 0
 * (MKXP_VITA_FIBER_CORE0). Once, after kStartFrame Graphics.update calls (game and audio running),
 * the main thread ping-pongs kRounds times with a helper pthread in four ways and writes one
 * HANDOFF_BENCH line to qa.log (one-way switch: average / median / max, microseconds):
 *   cond_c0   pthread mutex + cond, Context.c's pattern, helper on core 0 (as a Fiber thread now)
 *   cond_c12  the same with the helper on cores 1-2 (Fiber threads before d74)
 *   sema_c0   two kernel semaphores (sceKernelSignalSema / sceKernelWaitSema), helper on core 0
 *   sema_c12  the same, helper on cores 1-2
 * plus the cost of creating + starting + joining a pthread (create_join, as one Fiber lifetime).
 * Takes well under a second; the game is paused meanwhile.
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "vita_paths.h"

namespace
{
const int kRounds = 400;
const unsigned kStartFrame = 1800;

inline uint64_t now() { return sceKernelGetProcessTimeWide(); }

struct Bench
{
    int mode;            /* 0 = cond, 1 = sema */
    int helperMask;
    pthread_mutex_t mtx;
    pthread_cond_t cv[2];
    int turn;            /* 0 = main, 1 = helper (cond mode) */
    SceUID sema[2];
    uint64_t t;          /* time of the last hand-over request */
    uint32_t lat[2 * kRounds];
    int n;
};

void record(Bench &b)
{
    if (b.n < 2 * kRounds)
        b.lat[b.n++] = (uint32_t)(now() - b.t);
}

void *helper(void *arg)
{
    Bench &b = *static_cast<Bench *>(arg);
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), b.helperMask);
    for (int i = 0; i < kRounds; ++i) {
        if (b.mode == 0) {
            pthread_mutex_lock(&b.mtx);
            while (b.turn != 1)
                pthread_cond_wait(&b.cv[1], &b.mtx);
            record(b);
            b.turn = 0;
            b.t = now();
            pthread_cond_signal(&b.cv[0]);
            pthread_mutex_unlock(&b.mtx);
        } else {
            sceKernelWaitSema(b.sema[1], 1, nullptr);
            record(b);
            b.t = now();
            sceKernelSignalSema(b.sema[0], 1);
        }
    }
    return nullptr;
}

/* "avg/median/max" in us of the recorded one-way handoffs. */
void summary(Bench &b, char *out, size_t cap)
{
    if (!b.n) {
        std::snprintf(out, cap, "n/a");
        return;
    }
    uint64_t sum = 0;
    for (int i = 0; i < b.n; ++i)
        sum += b.lat[i];
    std::sort(b.lat, b.lat + b.n);
    std::snprintf(out, cap, "%u/%u/%u", (unsigned)(sum / b.n), b.lat[b.n / 2], b.lat[b.n - 1]);
}

void run(Bench &b, int mode, int helperMask, char *out, size_t cap)
{
    std::memset(&b, 0, sizeof(b));
    b.mode = mode;
    b.helperMask = helperMask;
    pthread_mutex_init(&b.mtx, nullptr);
    pthread_cond_init(&b.cv[0], nullptr);
    pthread_cond_init(&b.cv[1], nullptr);
    b.sema[0] = sceKernelCreateSema("vita_hb0", 0, 0, 1, nullptr);
    b.sema[1] = sceKernelCreateSema("vita_hb1", 0, 0, 1, nullptr);
    pthread_t th;
    if (pthread_create(&th, nullptr, helper, &b) != 0) {
        std::snprintf(out, cap, "create_failed");
        return;
    }
    sceKernelDelayThread(2000);   /* helper reaches its first wait */
    for (int i = 0; i < kRounds; ++i) {
        if (mode == 0) {
            pthread_mutex_lock(&b.mtx);
            b.turn = 1;
            b.t = now();
            pthread_cond_signal(&b.cv[1]);
            while (b.turn != 0)
                pthread_cond_wait(&b.cv[0], &b.mtx);
            record(b);
            pthread_mutex_unlock(&b.mtx);
        } else {
            b.t = now();
            sceKernelSignalSema(b.sema[1], 1);
            sceKernelWaitSema(b.sema[0], 1, nullptr);
            record(b);
        }
    }
    pthread_join(th, nullptr);
    summary(b, out, cap);
    pthread_cond_destroy(&b.cv[0]);
    pthread_cond_destroy(&b.cv[1]);
    pthread_mutex_destroy(&b.mtx);
    sceKernelDeleteSema(b.sema[0]);
    sceKernelDeleteSema(b.sema[1]);
}

void *empty(void *) { return nullptr; }

Bench gBench;
unsigned gFrames = 0;
} // namespace

/* main.cpp, once per Graphics.update (main thread). */
extern "C" void vitaHandoffBenchTick(void)
{
    if (++gFrames != kStartFrame)
        return;
    const int c0 = SCE_KERNEL_CPU_MASK_USER_0, c12 = SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2;
    char condC0[48], condC12[48], semaC0[48], semaC12[48];
    run(gBench, 0, c0, condC0, sizeof(condC0));
    run(gBench, 0, c12, condC12, sizeof(condC12));
    run(gBench, 1, c0, semaC0, sizeof(semaC0));
    run(gBench, 1, c12, semaC12, sizeof(semaC12));
    uint64_t cj = 0;
    const int kThreads = 20;
    for (int i = 0; i < kThreads; ++i) {
        const uint64_t t0 = now();
        pthread_t th;
        if (pthread_create(&th, nullptr, empty, nullptr) == 0)
            pthread_join(th, nullptr);
        cj += now() - t0;
    }
    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) {
        std::fprintf(f, "HANDOFF_BENCH us avg/median/max cond_c0=%s cond_c12=%s sema_c0=%s sema_c12=%s create_join=%u\n",
                     condC0, condC12, semaC0, semaC12, (unsigned)(cj / kThreads));
        std::fclose(f);
    }
}

/* pthread_join time (-Wl,--wrap=pthread_join): a Fiber that ends is joined by the one it resumes,
 * inside the switch the profiler measures (Context.c coroutine_transfer). PERF pt_join=n/avg_us. */
namespace
{
uint64_t gJoinUs = 0;
unsigned gJoinN = 0;
}
extern "C" int __real_pthread_join(pthread_t th, void **ret);
extern "C" int __wrap_pthread_join(pthread_t th, void **ret)
{
    const uint64_t t0 = now();
    const int r = __real_pthread_join(th, ret);
    gJoinUs += now() - t0;
    ++gJoinN;
    return r;
}
extern "C" void vitaJoinStats(unsigned *n, unsigned *us)
{
    *n = gJoinN;
    *us = (unsigned)gJoinUs;
    gJoinN = 0;
    gJoinUs = 0;
}
