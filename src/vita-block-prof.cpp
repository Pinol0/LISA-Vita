/*
 * Diagnostic (MKXP_VITA_BLOCK_PROF): where the main thread waits. d78: returning to the map after a
 * save, one 2.2 s frame used only 0.9 s of main-thread CPU with 26 preemptions: ~1.3 s blocked.
 * The linker wraps the blocking kernel calls (-Wl,--wrap=..., every caller: newlib, pthread-embedded,
 * OpenAL, vitaGL, mkxp): on the main thread each call is timed. Other threads pass straight through
 * (one TPIDRURO read; TPIDRURO holds the per-thread TLS pointer; checked against the thread id).
 *   slow_frames.log, per slow frame: blk=total_ms, blk_<kind>=calls/ms (>= 1 ms) and the longest
 *     single wait: blk_max=<kind>:<ms>:<detail>@<freeze-probe phase>
 *   PERF window: blk_top=<kind>:ms/calls (top 6), blk_max=... (longest of the window)
 * detail: file path for sceIo* (fd table filled by sceIoOpen), object name for semaphores, mutexes
 * and threads (asked only for waits >= 2 ms), the requested delay for sceKernelDelayThread.
 * Note: during a Fiber switch the main thread waits on a semaphore while the Fiber runs, so
 * blk_sema also contains Ruby work done in Fibers.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

extern "C" __attribute__((weak)) const char *vitaFreezePhaseNow(void);   /* vita_freeze_probe.cpp */

namespace
{
enum Kind { K_SEMA, K_MUTEX, K_LWMUTEX, K_DELAY, K_JOIN, K_IO_OPEN, K_IO_READ, K_IO_WRITE, K_IO_CLOSE, K_IO_SEEK,
            K_IO_STAT, K_IO_STAT_MISS, K_IO_DIR, K_IO_OTHER, K_GXM_FINISH, K_GXM_DQ, K_GXM_NOTIFY, K_VBLANK, K_COUNT };
const char *const kNames[K_COUNT] = { "sema", "mutex", "lwmutex", "delay", "join", "io_open", "io_read", "io_write",
                                      "io_close", "io_seek", "io_stat", "io_stat_miss", "io_dir", "io_other", "gxm_finish",
                                      "gxm_dqfinish", "gxm_notify", "vblank" };
struct Acc { uint64_t us; unsigned n; };
struct Max { uint32_t us; int kind; char detail[64]; char phase[28]; };
Acc gWin[K_COUNT], gFrame[K_COUNT];
Max gWinMax, gFrameMax;
uintptr_t gMainTp = 0;
SceUID gMainTid = -1;

/* fd -> path of the last sceIoOpen (main thread and others: paths of every open file). */
struct FdPath { SceUID fd; char path[56]; };
FdPath gFds[48];

inline uintptr_t tp()
{
    uintptr_t v;
    asm volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(v));
    return v;
}
inline bool onMain() { return gMainTp && tp() == gMainTp && sceKernelGetThreadId() == gMainTid; }
inline uint64_t now() { return sceKernelGetProcessTimeWide(); }

void copyStr(char *dst, size_t cap, const char *s)
{
    if (!s) s = "";
    const size_t n = std::strlen(s);
    const char *from = n >= cap ? s + n - (cap - 1) : s;   /* keep the end of long paths */
    std::strncpy(dst, from, cap - 1);
    dst[cap - 1] = 0;
}
const char *fdPath(SceUID fd)
{
    for (auto &e : gFds)
        if (e.fd == fd && fd > 0) return e.path;
    return "?";
}
void fdSet(SceUID fd, const char *path)
{
    if (fd < 0) return;
    for (auto &e : gFds)
        if (e.fd == fd || e.fd == 0) { e.fd = fd; copyStr(e.path, sizeof(e.path), path); return; }
    gFds[(unsigned)fd % 48].fd = fd;
    copyStr(gFds[(unsigned)fd % 48].path, sizeof(gFds[0].path), path);
}
void fdClear(SceUID fd)
{
    for (auto &e : gFds)
        if (e.fd == fd) e.fd = 0;
}

void record(int k, uint64_t t0, const char *detail)
{
    const uint32_t d = (uint32_t)(now() - t0);
    gWin[k].us += d; gWin[k].n++;
    gFrame[k].us += d; gFrame[k].n++;
    for (Max *m : { &gFrameMax, &gWinMax })
        if (d > m->us) {
            m->us = d;
            m->kind = k;
            copyStr(m->detail, sizeof(m->detail), detail);
            copyStr(m->phase, sizeof(m->phase), vitaFreezePhaseNow ? vitaFreezePhaseNow() : "");
        }
}
template <typename Info, typename F> const char *objName(F getInfo, SceUID id, uint64_t t0, char *buf)
{
    if (now() - t0 < 2000) return "";
    Info info;
    std::memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (getInfo(id, &info) < 0) return "";
    copyStr(buf, 32, info.name);
    return buf;
}
} // namespace

extern "C" {
int __real_sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout);
int __real_sceKernelLockMutex(SceUID mutexid, int lockCount, unsigned int *timeout);
int __real_sceKernelLockLwMutex(SceKernelLwMutexWork *pWork, int lockCount, unsigned int *pTimeout);
int __real_sceKernelDelayThread(SceUInt delay);
int __real_sceKernelDelayThreadCB(SceUInt delay);
int __real_sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout);
int __real_sceKernelWaitThreadEndCB(SceUID thid, int *stat, SceUInt *timeout);
SceUID __real_sceIoOpen(const char *file, int flags, SceMode mode);
SceSSize __real_sceIoRead(SceUID fd, void *buf, SceSize nbyte);
SceSSize __real_sceIoWrite(SceUID fd, const void *buf, SceSize nbyte);
int __real_sceIoClose(SceUID fd);
SceOff __real_sceIoLseek(SceUID fd, SceOff offset, int whence);
long __real_sceIoLseek32(SceUID fd, long offset, int whence);
int __real_sceIoGetstat(const char *file, SceIoStat *stat);
SceUID __real_sceIoDopen(const char *dirname);
int __real_sceIoDread(SceUID fd, SceIoDirent *dir);
int __real_sceIoRemove(const char *file);
int __real_sceIoRename(const char *oldname, const char *newname);
int __real_sceIoSyncByFd(SceUID fd, int flag);
int __real_sceIoPread(SceUID fd, void *data, SceSize size, SceOff offset);
void __real_sceGxmFinish(SceGxmContext *context);
int __real_sceGxmDisplayQueueFinish();
int __real_sceGxmNotificationWait(const SceGxmNotification *notification);
int __real_sceDisplayWaitVblankStart(void);
int __real_sceDisplayWaitVblankStartMulti(unsigned int vcount);

#define TIMED(kind, call, detail)                       \
    do {                                                \
        if (!onMain()) return call;                     \
        const uint64_t t0 = now();                      \
        auto r = call;                                  \
        record(kind, t0, detail);                       \
        return r;                                       \
    } while (0)

int __wrap_sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout)
{
    if (!onMain()) return __real_sceKernelWaitSema(semaid, signal, timeout);
    const uint64_t t0 = now();
    const int r = __real_sceKernelWaitSema(semaid, signal, timeout);
    char b[32];
    record(K_SEMA, t0, objName<SceKernelSemaInfo>(sceKernelGetSemaInfo, semaid, t0, b));
    return r;
}
int __wrap_sceKernelLockMutex(SceUID mutexid, int lockCount, unsigned int *timeout)
{
    if (!onMain()) return __real_sceKernelLockMutex(mutexid, lockCount, timeout);
    const uint64_t t0 = now();
    const int r = __real_sceKernelLockMutex(mutexid, lockCount, timeout);
    char b[32];
    record(K_MUTEX, t0, objName<SceKernelMutexInfo>(sceKernelGetMutexInfo, mutexid, t0, b));
    return r;
}
int __wrap_sceKernelLockLwMutex(SceKernelLwMutexWork *pWork, int lockCount, unsigned int *pTimeout)
{
    TIMED(K_LWMUTEX, __real_sceKernelLockLwMutex(pWork, lockCount, pTimeout), "");
}
static const char *delayStr(SceUInt d, char *b) { std::snprintf(b, 24, "%u us", (unsigned)d); return b; }
int __wrap_sceKernelDelayThread(SceUInt delay)
{
    char b[24];
    TIMED(K_DELAY, __real_sceKernelDelayThread(delay), delayStr(delay, b));
}
int __wrap_sceKernelDelayThreadCB(SceUInt delay)
{
    char b[24];
    TIMED(K_DELAY, __real_sceKernelDelayThreadCB(delay), delayStr(delay, b));
}
static int waitEnd(bool cb, SceUID thid, int *stat, SceUInt *timeout)
{
    if (!onMain()) return cb ? __real_sceKernelWaitThreadEndCB(thid, stat, timeout) : __real_sceKernelWaitThreadEnd(thid, stat, timeout);
    char name[32] = "";
    SceKernelThreadInfo info;   /* name first: the thread may be deleted right after it ends */
    std::memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelGetThreadInfo(thid, &info) >= 0) copyStr(name, sizeof(name), info.name);
    const uint64_t t0 = now();
    const int r = cb ? __real_sceKernelWaitThreadEndCB(thid, stat, timeout) : __real_sceKernelWaitThreadEnd(thid, stat, timeout);
    record(K_JOIN, t0, name);
    return r;
}
int __wrap_sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout) { return waitEnd(false, thid, stat, timeout); }
int __wrap_sceKernelWaitThreadEndCB(SceUID thid, int *stat, SceUInt *timeout) { return waitEnd(true, thid, stat, timeout); }

SceUID __wrap_sceIoOpen(const char *file, int flags, SceMode mode)
{
    if (!onMain()) {
        const SceUID fd = __real_sceIoOpen(file, flags, mode);
        fdSet(fd, file);
        return fd;
    }
    const uint64_t t0 = now();
    const SceUID fd = __real_sceIoOpen(file, flags, mode);
    fdSet(fd, file);
    record(K_IO_OPEN, t0, file);
    return fd;
}
SceSSize __wrap_sceIoRead(SceUID fd, void *buf, SceSize nbyte) { TIMED(K_IO_READ, __real_sceIoRead(fd, buf, nbyte), fdPath(fd)); }
SceSSize __wrap_sceIoWrite(SceUID fd, const void *buf, SceSize nbyte) { TIMED(K_IO_WRITE, __real_sceIoWrite(fd, buf, nbyte), fdPath(fd)); }
int __wrap_sceIoClose(SceUID fd)
{
    if (!onMain()) {
        const int r = __real_sceIoClose(fd);
        fdClear(fd);
        return r;
    }
    char path[56];
    copyStr(path, sizeof(path), fdPath(fd));
    const uint64_t t0 = now();
    const int r = __real_sceIoClose(fd);
    fdClear(fd);
    record(K_IO_CLOSE, t0, path);
    return r;
}
SceOff __wrap_sceIoLseek(SceUID fd, SceOff offset, int whence) { TIMED(K_IO_SEEK, __real_sceIoLseek(fd, offset, whence), fdPath(fd)); }
long __wrap_sceIoLseek32(SceUID fd, long offset, int whence) { TIMED(K_IO_SEEK, __real_sceIoLseek32(fd, offset, whence), fdPath(fd)); }
int __wrap_sceIoGetstat(const char *file, SceIoStat *stat)
{
    if (!onMain()) return __real_sceIoGetstat(file, stat);
    const uint64_t t0 = now();
    const int r = __real_sceIoGetstat(file, stat);
    record(r < 0 ? K_IO_STAT_MISS : K_IO_STAT, t0, file);   /* d81: misses (no such file) apart */
    return r;
}
SceUID __wrap_sceIoDopen(const char *dirname) { TIMED(K_IO_DIR, __real_sceIoDopen(dirname), dirname); }
int __wrap_sceIoDread(SceUID fd, SceIoDirent *dir) { TIMED(K_IO_DIR, __real_sceIoDread(fd, dir), ""); }
int __wrap_sceIoRemove(const char *file) { TIMED(K_IO_OTHER, __real_sceIoRemove(file), file); }
int __wrap_sceIoRename(const char *oldname, const char *newname) { TIMED(K_IO_OTHER, __real_sceIoRename(oldname, newname), newname); }
int __wrap_sceIoSyncByFd(SceUID fd, int flag) { TIMED(K_IO_OTHER, __real_sceIoSyncByFd(fd, flag), fdPath(fd)); }
int __wrap_sceIoPread(SceUID fd, void *data, SceSize size, SceOff offset) { TIMED(K_IO_READ, __real_sceIoPread(fd, data, size, offset), fdPath(fd)); }
void __wrap_sceGxmFinish(SceGxmContext *context)
{
    if (!onMain()) { __real_sceGxmFinish(context); return; }
    const uint64_t t0 = now();
    __real_sceGxmFinish(context);
    record(K_GXM_FINISH, t0, "");
}
int __wrap_sceGxmDisplayQueueFinish() { TIMED(K_GXM_DQ, __real_sceGxmDisplayQueueFinish(), ""); }
int __wrap_sceGxmNotificationWait(const SceGxmNotification *notification) { TIMED(K_GXM_NOTIFY, __real_sceGxmNotificationWait(notification), ""); }
int __wrap_sceDisplayWaitVblankStart(void) { TIMED(K_VBLANK, __real_sceDisplayWaitVblankStart(), ""); }
int __wrap_sceDisplayWaitVblankStartMulti(unsigned int vcount) { TIMED(K_VBLANK, __real_sceDisplayWaitVblankStartMulti(vcount), ""); }

/* vita_diag.cpp vitaDiagFrameEnd (main thread, every frame): appends to a slow frame's line when
 * slow != 0, then starts the next frame. The first call registers the main thread. */
int vitaBlockFrameEnd(char *b, int cap, int slow)
{
    if (!gMainTp) {
        gMainTp = tp();
        gMainTid = sceKernelGetThreadId();
    }
    int n = 0;
    if (slow && cap > 0) {
        uint64_t tot = 0;
        for (int k = 0; k < K_COUNT; ++k) tot += gFrame[k].us;
        n += std::snprintf(b + n, cap - n, " blk=%.1f", tot / 1000.0);
        for (int k = 0; k < K_COUNT && n < cap - 40; ++k)
            if (gFrame[k].us >= 1000) n += std::snprintf(b + n, cap - n, " blk_%s=%u/%.1f", kNames[k], gFrame[k].n, gFrame[k].us / 1000.0);
        if (gFrameMax.us >= 1000 && n < cap - 120)
            n += std::snprintf(b + n, cap - n, " blk_max=%s:%.1f:%s@%s", kNames[gFrameMax.kind], gFrameMax.us / 1000.0,
                               gFrameMax.detail, gFrameMax.phase);
        if (n > cap) n = cap;
    }
    std::memset(gFrame, 0, sizeof(gFrame));
    gFrameMax.us = 0;
    return n;
}

/* vita_diag.cpp PERF line, once per window. */
int vitaBlockPerfAppend(char *b, int cap)
{
    int n = 0;
    bool used[K_COUNT] = {};
    for (int i = 0; i < 6 && n < cap - 40; ++i) {
        int best = -1;
        for (int k = 0; k < K_COUNT; ++k)
            if (!used[k] && gWin[k].us && (best < 0 || gWin[k].us > gWin[best].us)) best = k;
        if (best < 0) break;
        used[best] = true;
        n += std::snprintf(b + n, cap - n, "%s%s:%.1f/%u", i ? "," : " blk_top=", kNames[best], gWin[best].us / 1000.0, gWin[best].n);
    }
    if (gWinMax.us && n < cap - 120)
        n += std::snprintf(b + n, cap - n, " blk_max=%s:%.1f:%s@%s", kNames[gWinMax.kind], gWinMax.us / 1000.0, gWinMax.detail,
                           gWinMax.phase);
    if (n > cap) n = cap;
    std::memset(gWin, 0, sizeof(gWin));
    gWinMax.us = 0;
    return n;
}
}
