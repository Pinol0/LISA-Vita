/*
 * Perf fix (MKXP_VITA_CORE0_MAIN): the game's main thread gets user core 0 to itself; every thread
 * created later without an explicit core (SDL audio, OpenAL mixer, mkxp-z audio streams, Ruby's
 * timer thread, Fiber threads) runs on user cores 1-2 (-Wl,--wrap=sceKernelCreateThread).
 * d50: in battle the slow frames lost ~35 ms (median) waiting, with ~26 preemptions and ~34 core
 * changes of the main thread per frame. A thread that asks for specific cores keeps them.
 * The first 64 creations go to qa.log (name, priority, requested and applied mask).
 */
#include <cstdio>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include "vita_paths.h"

extern "C" {
SceUID __real_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
                                    SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option);

static int vitaAffinityLogged = 0;

SceUID __wrap_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
                                    SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option)
{
    const int others = SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2;
    const int mask = (cpuAffinityMask == 0 || cpuAffinityMask == SCE_KERNEL_CPU_MASK_USER_ALL) ? others : cpuAffinityMask;
    const SceUID id = __real_sceKernelCreateThread(name, entry, initPriority, stackSize, attr, mask, option);
    if (vitaAffinityLogged < 64) {
        ++vitaAffinityLogged;
        FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
        if (f) {
            std::fprintf(f, "THREAD_CREATE name=%s prio=0x%x stack=%u mask_req=0x%x mask=0x%x uid=0x%x\n", name ? name : "?",
                         (unsigned)initPriority, (unsigned)stackSize, (unsigned)cpuAffinityMask, (unsigned)mask, (unsigned)id);
            std::fclose(f);
        }
    }
    return id;
}

/* main.cpp, first thing in main(). */
void vitaPinMainThread(void)
{
    const int r = sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) {
        std::fprintf(f, "MAIN_THREAD core0 result=0x%x\n", (unsigned)r);
        std::fclose(f);
    }
}
}
