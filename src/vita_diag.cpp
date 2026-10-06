/*
 * Vita diagnostics storage/output (see mkxp-z/src/vita_diag.h). Diagnostic builds only.
 *
 * Timeline (MKXP_VITA_TIMELINE): phase BOOT is armed at process start and closed 30 frames after
 * the first Scene_Map#start; phase BATTLE is armed by Game_Interpreter#command_301 and closed 60
 * frames after Scene_Battle#start. Entries are kept in RAM (no allocation per event) and written
 * once per phase, from the main thread after the swap, to ux0:data/ruby_vita_test/*_timeline.log.
 * Timestamps are absolute (sceKernelGetProcessTimeWide, us since process start).
 *
 * PERF counters (MKXP_VITA_PERF_BITMAP): count/us/bytes per op for the current 120-frame PERF
 * window, appended to the PERF line by sharedstate_test.cpp.
 */
#include "vita_diag.h"
#include "vita_paths.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <ruby.h>
#ifdef MKXP_VITA_RUBY_PROF
#include <ruby/debug.h>
#endif

#include <malloc.h>
#include <vitaGL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static const char *const kOpNames[VD_OP_COUNT] = {
    "script_eval", "ShaderSet", "glCompileShader", "glLinkProgram", "glTexImage2D", "glTexSubImage2D",
    "TileAtlasVX::build", "png_decode", "bitmap_upload", "stretch_blt", "radial_blur", "fill_rect",
    "gradient_fill_rect", "clear", "clear_rect", "blt", "draw_text", "text_size", "TTF_Init",
    "TTF_OpenFont", "TTF_Render", "font_cache_hit", "frame.prepare", "frame.composite", "frame.swap",
    "Window#refresh", "WindowVX_base_redraw",
};

int vitaDiagOpDepth = 0;

extern "C" uint64_t vitaDiagNow(void) { return sceKernelGetProcessTimeWide(); }

/* ------------------------------------------------------------------ PERF counters */
#ifdef MKXP_VITA_PERF_BITMAP
struct PerfAcc { unsigned count; uint64_t us; uint64_t bytes; };
static PerfAcc gPerf[VD_OP_COUNT];
static PerfAcc gFramePerfPrev[VD_OP_COUNT];   /* slow-frame log (RUBY_PROF): gPerf at the previous frame end */
int vitaDiagBattleScene = 0;
extern "C" int vitaDiagFboLive;     /* gl-fun-vita.cpp */
extern "C" int vitaDiagFboGenFail;
extern "C" int vitaDiagFboRtLive;
extern "C" int vitaDiagFboRtMax;
#ifdef MKXP_VITA_DEFERRED_GL_DELETE
int vitaDeferredGLPending();   /* gl-fun-vita.cpp */
#endif
extern "C" void vitaDiagBitmapMem(int *live, uint64_t *cpuBytes, uint64_t *texBytes);   /* bitmap-vita-minimal.cpp */
extern "C" size_t vitaDiagPngCacheBytes();                              /* bitmap-vita-minimal.cpp */
extern "C" __attribute__((weak)) void vitaHeapLedgerTotals(unsigned *blocks, unsigned *kb, unsigned *untracked);
extern "C" __attribute__((weak)) void vitaHeapLedgerDump(const char *reason);   /* vita-heap-ledger.cpp */
extern "C" __attribute__((weak)) unsigned int vitaPthreadParmsFreed;   /* vita-pthread-parms.cpp */
extern "C" __attribute__((weak)) int vitaBlockFrameEnd(char *b, int cap, int slow);   /* vita-block-prof.cpp */
extern "C" __attribute__((weak)) int vitaBlockPerfAppend(char *b, int cap);
extern "C" __attribute__((weak)) void vitaFsIndexStats(unsigned *found, unsigned *missing, unsigned *unknown, unsigned *dirs);   /* vita-fs-index.cpp */
extern "C" __attribute__((weak)) void vitaPrefetchStatsC(unsigned *q, unsigned *t, unsigned *w, unsigned *d, unsigned *kb);   /* vita-prefetch.cpp */
extern "C" __attribute__((weak)) void vitaPredecodeStatsC(unsigned *q, unsigned *t, unsigned *w, unsigned *d, unsigned *h, unsigned *ikb, unsigned *skb);   /* vita-predecode.cpp */
extern "C" __attribute__((weak)) unsigned int vitaSemNocancelWaits;   /* vita-sem-nocancel.cpp */
extern "C" __attribute__((weak)) unsigned int vitaFiberCore0;   /* vita-pthread-parms.cpp (FIBER_CORE0) */
extern "C" __attribute__((weak)) void vitaJoinStats(unsigned *n, unsigned *us);   /* vita-handoff-bench.cpp */
extern "C" __attribute__((weak)) void vitaTexPagingStats(unsigned *evicted, unsigned *restored, unsigned *fails, unsigned *outKb);   /* bitmap-vita-minimal.cpp */
extern "C" __attribute__((weak)) void vitaAtlasParkStats(unsigned *reused, unsigned *fresh, unsigned *parked);   /* sharedstate_test.cpp */
extern "C" __attribute__((weak)) unsigned int vitaThreadReentReleased;   /* vita-thread-reent.cpp */
/* libruby pthread coroutine backend (Context.c): Fiber threads created / ended and joined. */
struct coroutine_vita_stats { unsigned int created, exited, joined_exited, joined_suspended, released_exited; };
extern "C" __attribute__((weak)) void coroutine_vita_get_stats(struct coroutine_vita_stats *stats);
/* libruby v8 (Fiber thread pool): threads started, Fibers started on a reused thread, idle threads. */
extern "C" __attribute__((weak)) void coroutine_vita_pool_stats(unsigned *spawned, unsigned *reused, unsigned *idle);
extern "C" __attribute__((weak)) void vitaBigAllocStats(unsigned *usedKb, unsigned *freeKb, unsigned *blocks,
                                                        unsigned *fallbacks);   /* vita-big-alloc.cpp */
extern "C" __attribute__((weak)) size_t vitaDiagTextCacheBytes();       /* vita-font.cpp (TEXT_CACHE) */
extern "C" __attribute__((weak)) void vitaDiagImgCacheStats(unsigned *h, unsigned *m, unsigned *s, unsigned *f);   /* IMG_DISK_CACHE */
extern "C" __attribute__((weak)) void vitaVglLogFlush(void);   /* gl-fun-vita.cpp (VGL_LOG_BUFFER) */
extern "C" __attribute__((weak)) unsigned int vitaLogicUpdates;   /* main.cpp (MKXP_VITA_FPS30) */
/* vitaGL-vita-d26 (weak: absent from older libs): per-frame peak of the circular vertex pool; pool sizes. */
extern "C" __attribute__((weak)) uint32_t vgl_diag_circular_peak;
extern "C" __attribute__((weak)) size_t vgl_mem_get_total_space(int type);
#ifdef MKXP_VITA_CPU_444
#include <psp2/power.h>
#endif
#ifdef MKXP_VITA_FD_DIAG
#include <psp2/io/stat.h>
extern "C" int *__vita_fdmap[256];   /* newlib (libc.a): DescriptorTranslation *[MAX_OPEN_FILES] */
#endif
#ifdef MKXP_VITA_GL_LEDGER
extern "C" void vitaGlLedgerTotals(int *ntex, uint64_t *texBytes, int *nbuf, uint64_t *bufBytes);   /* gl-fun-vita.cpp */
extern "C" void vitaGlLedgerMaybeDump(unsigned int window);
#endif

#ifdef MKXP_VITA_RUBY_PROF
static int profAppend(char *buf, int cap);
#ifdef MKXP_VITA_ALLOC_PROF
static int allocAppend(char *buf, int cap);
#endif
static void slowFramesFlush();
static bool slowFramesNearlyFull();
#endif
/* Window#refresh per class (detail = class name) for the current PERF window. */
struct RefreshAcc { char cls[32]; unsigned count; uint64_t us; };
static RefreshAcc gRefresh[24];
static int gNRefresh = 0;

static void perfRefreshAdd(const char *cls, uint64_t dur)
{
    if (!cls) cls = "?";
    int i = 0;
    for (; i < gNRefresh; ++i)
        if (!std::strcmp(gRefresh[i].cls, cls)) break;
    if (i == gNRefresh) {
        if (gNRefresh == (int)(sizeof(gRefresh) / sizeof(gRefresh[0]))) return;
        std::snprintf(gRefresh[i].cls, sizeof(gRefresh[i].cls), "%s", cls);
        gRefresh[i].count = 0; gRefresh[i].us = 0;
        ++gNRefresh;
    }
    gRefresh[i].count++;
    gRefresh[i].us += dur;
}

/* Appends " bmp_<op>=count/ms/KiB" for the ops that happened in this window, then resets. */
extern "C" int vitaDiagPerfAppend(char *buf, int cap)
{
    int len = 0;
    for (int i = 0; i < VD_OP_COUNT && len < cap; ++i) {
        if (i == VD_FRAME_PREPARE || i == VD_FRAME_COMPOSITE || i == VD_FRAME_SWAP || !gPerf[i].count)
            continue;
        len += std::snprintf(buf + len, cap - len, " bmp_%s=%u/%.2f/%llu", kOpNames[i], gPerf[i].count,
                             gPerf[i].us / 1000.0, (unsigned long long)(gPerf[i].bytes / 1024));
    }
    if (len < cap)
        len += std::snprintf(buf + len, cap - len, " fbo_live=%d fbo_gen_fail=%d fbo_rt_live=%d fbo_rt_max=%d",
                             vitaDiagFboLive, vitaDiagFboGenFail, vitaDiagFboRtLive, vitaDiagFboRtMax);
#ifdef MKXP_VITA_DEFERRED_GL_DELETE
    if (len < cap)
        len += std::snprintf(buf + len, cap - len, " gl_del_pending=%d", vitaDeferredGLPending());
#endif
    /* Memory: newlib heap (malloc'd bytes in use / sbrk arena, peak of both since boot), vitaGL
     * free pools, live Bitmaps with their CPU pixel copies and texture sizes. */
    if (len < cap) {
        const struct mallinfo mi = mallinfo();
        static size_t peakUsed = 0, peakArena = 0;
        peakUsed = std::max(peakUsed, (size_t)mi.uordblks);
        peakArena = std::max(peakArena, (size_t)mi.arena);
        int bmpLive = 0;
        uint64_t bmpCpu = 0, bmpTex = 0;
        vitaDiagBitmapMem(&bmpLive, &bmpCpu, &bmpTex);
        len += std::snprintf(buf + len, cap - len,
                             " heap_used_kb=%u heap_arena_kb=%u heap_peak_used_kb=%u heap_peak_arena_kb=%u"
                             " vgl_free_ram_kb=%u vgl_free_vram_kb=%u vgl_free_phycont_kb=%u"
                             " bmp_live=%d bmp_cpu_kb=%llu bmp_tex_kb=%llu",
                             (unsigned)(mi.uordblks / 1024), (unsigned)(mi.arena / 1024),
                             (unsigned)(peakUsed / 1024), (unsigned)(peakArena / 1024),
                             (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024),
                             (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024),
                             bmpLive, (unsigned long long)(bmpCpu / 1024), (unsigned long long)(bmpTex / 1024));
    }
    if (len < cap) {
        /* Fragmentation: free bytes inside the arena, and free at its top (releasable). */
        const struct mallinfo fm = mallinfo();
        len += std::snprintf(buf + len, cap - len, " heap_free_kb=%u heap_top_free_kb=%u",
                             (unsigned)(fm.fordblks / 1024), (unsigned)(fm.keepcost / 1024));
    }
    if (len < cap && vitaBigAllocStats) {
        /* Large-block pool (MKXP_VITA_BIG_ALLOC): in use, free, live blocks, requests that fell back
         * to the newlib heap (pool full). */
        unsigned bu = 0, bf = 0, bb = 0, bfb = 0;
        vitaBigAllocStats(&bu, &bf, &bb, &bfb);
        len += std::snprintf(buf + len, cap - len, " big_used_kb=%u big_free_kb=%u big_blocks=%u big_fallbacks=%u",
                             bu, bf, bb, bfb);
    }
    if (len < cap && vitaHeapLedgerTotals) {
        /* newlib heap ledger: tracked live blocks/bytes; a per-caller dump every 25 windows. */
        static unsigned win = 0;
        unsigned hb = 0, hkb = 0, hu = 0;
        vitaHeapLedgerTotals(&hb, &hkb, &hu);
        len += std::snprintf(buf + len, cap - len, " hl_blocks=%u hl_kb=%u hl_untracked=%u", hb, hkb, hu);
        if (++win % 25 == 0 && vitaHeapLedgerDump) {
            char r[24];
            std::snprintf(r, sizeof(r), "perf_win_%u", win);
            vitaHeapLedgerDump(r);
        }
    }
    if (len < cap && coroutine_vita_get_stats) {
        /* Threads: Fiber threads alive (created - joined), newlib slots released by the delete wrapper. */
        struct coroutine_vita_stats cs;
        coroutine_vita_get_stats(&cs);
        len += std::snprintf(buf + len, cap - len, " fiber_created=%u fiber_live=%u reent_released=%u",
                             cs.created, cs.created - cs.joined_exited - cs.joined_suspended,
                             &vitaThreadReentReleased ? vitaThreadReentReleased : 0u);
    }
    if (len < cap && &vitaPthreadParmsFreed)
        len += std::snprintf(buf + len, cap - len, " pt_parms_freed=%u", vitaPthreadParmsFreed);
    if (len < cap && &vitaFiberCore0)
        len += std::snprintf(buf + len, cap - len, " fiber_core0=%u", vitaFiberCore0);
    if (len < cap && vitaBlockPerfAppend)
        len += vitaBlockPerfAppend(buf + len, cap - len);
    if (len < cap && vitaFsIndexStats) {
        unsigned fo = 0, mi = 0, un = 0, di = 0;
        vitaFsIndexStats(&fo, &mi, &un, &di);
        len += std::snprintf(buf + len, cap - len, " fs_idx=%u/%u/%u/%u", fo, mi, un, di);
    }
    if (len < cap && vitaPrefetchStatsC) {
        unsigned q = 0, t = 0, w = 0, d = 0, kb = 0;
        vitaPrefetchStatsC(&q, &t, &w, &d, &kb);
        len += std::snprintf(buf + len, cap - len, " pf=%u/%u/%u/%u/%u", q, t, w, d, kb);
    }
    if (len < cap && vitaPredecodeStatsC) {
        unsigned q = 0, t = 0, w = 0, d = 0, h = 0, ikb = 0, skb = 0;
        vitaPredecodeStatsC(&q, &t, &w, &d, &h, &ikb, &skb);
        len += std::snprintf(buf + len, cap - len, " pd=%u/%u/%u/%u/%u/%u/%u", q, t, w, d, h, ikb, skb);
    }
    if (len < cap && &vitaSemNocancelWaits)
        len += std::snprintf(buf + len, cap - len, " sem_nc=%u", vitaSemNocancelWaits);
    if (len < cap && coroutine_vita_pool_stats) {
        unsigned sp = 0, re = 0, idle = 0;
        coroutine_vita_pool_stats(&sp, &re, &idle);
        len += std::snprintf(buf + len, cap - len, " fiber_pool=%u/%u/%u", sp, re, idle);
    }
    if (len < cap && vitaJoinStats) {
        unsigned jn = 0, jus = 0;
        vitaJoinStats(&jn, &jus);
        len += std::snprintf(buf + len, cap - len, " pt_join=%u/%.0f", jn, jn ? (double)jus / jn : 0.0);
    }
    if (len < cap && vitaTexPagingStats) {
        unsigned ev = 0, rs = 0, fl = 0, ok = 0;
        vitaTexPagingStats(&ev, &rs, &fl, &ok);
        len += std::snprintf(buf + len, cap - len, " pg_evicted=%u pg_restored=%u pg_fail=%u pg_out_kb=%u", ev, rs, fl, ok);
    }
    if (len < cap && vitaAtlasParkStats) {
        unsigned ru = 0, fr = 0, pk = 0;
        vitaAtlasParkStats(&ru, &fr, &pk);
        len += std::snprintf(buf + len, cap - len, " atlas_reused=%u atlas_new=%u atlas_parked=%u", ru, fr, pk);
    }
    if (len < cap && &vitaLogicUpdates) {
        /* Logic steps (Graphics.update calls) in this window: with MKXP_VITA_FPS30, 2 per image. */
        static unsigned prevUpd = 0;
        len += std::snprintf(buf + len, cap - len, " upd=%u", vitaLogicUpdates - prevUpd);
        prevUpd = vitaLogicUpdates;
    }
    if (len < cap && vitaDiagImgCacheStats) {
        unsigned dh = 0, dm = 0, ds = 0, df = 0;
        vitaDiagImgCacheStats(&dh, &dm, &ds, &df);
        len += std::snprintf(buf + len, cap - len, " dcache_hit=%u dcache_miss=%u dcache_store=%u dcache_fail=%u", dh, dm, ds, df);
    }
    if (len < cap)
        len += std::snprintf(buf + len, cap - len, " png_cache_kb=%u text_cache_kb=%u",
                             (unsigned)(vitaDiagPngCacheBytes() / 1024),
                             vitaDiagTextCacheBytes ? (unsigned)(vitaDiagTextCacheBytes() / 1024) : 0u);
    if (len < cap && &vgl_diag_circular_peak)
        len += std::snprintf(buf + len, cap - len, " vgl_circ_peak_kb=%u", (unsigned)(vgl_diag_circular_peak / 1024));
    if (len < cap && vgl_mem_get_total_space) {
        static bool once = false;
        if (!once) {
            once = true;
            len += std::snprintf(buf + len, cap - len, " vgl_total_vram_kb=%u vgl_total_ram_kb=%u vgl_total_phycont_kb=%u vgl_total_budget_kb=%u",
                                 (unsigned)(vgl_mem_get_total_space(VGL_MEM_VRAM) / 1024), (unsigned)(vgl_mem_get_total_space(VGL_MEM_RAM) / 1024),
                                 (unsigned)(vgl_mem_get_total_space(VGL_MEM_PHYCONT) / 1024), (unsigned)(vgl_mem_get_total_space(VGL_MEM_BUDGET) / 1024));
        }
    }
    /* Ruby GC (main thread, GVL held: PERF is flushed from Graphics.update): heap pages
     * (16 KiB each in 3.1), live slots, bytes malloc'd since the last GC, GC runs. */
    if (len < cap) {
        static ID idPages, idLive, idMallocInc, idOldMallocInc, idCount;
        if (!idPages) {
            idPages = rb_intern("heap_allocated_pages");
            idLive = rb_intern("heap_live_slots");
            idMallocInc = rb_intern("malloc_increase_bytes");
            idOldMallocInc = rb_intern("oldmalloc_increase_bytes");
            idCount = rb_intern("count");
        }
        len += std::snprintf(buf + len, cap - len,
                             " gc_pages=%u gc_live_slots=%u gc_malloc_inc_kb=%u gc_oldmalloc_inc_kb=%u gc_count=%u",
                             (unsigned)rb_gc_stat(ID2SYM(idPages)), (unsigned)rb_gc_stat(ID2SYM(idLive)),
                             (unsigned)(rb_gc_stat(ID2SYM(idMallocInc)) / 1024),
                             (unsigned)(rb_gc_stat(ID2SYM(idOldMallocInc)) / 1024),
                             (unsigned)rb_gc_stat(ID2SYM(idCount)));
    }
#ifdef MKXP_VITA_FD_DIAG
    /* newlib descriptor table (256 static slots, libc __vita_fdmap -> __vita_fdmap_pool entries whose
     * first two words are the SceUID and the descriptor type): d35 soak, every Ruby File.open failed
     * silently after ~6 h. Slots in use, split by type value; fd_table.log once per threshold. */
    if (len < cap) {
        static const unsigned kThresholds[] = { 64, 128, 200, 240 };
        static unsigned nextThreshold = 0;
        unsigned used = 0, byType[6] = { 0 };
        for (int fd = 0; fd < 256; ++fd) {
            const int *d = __vita_fdmap[fd];
            if (!d) continue;
            ++used;
            byType[(unsigned)d[1] < 5 ? d[1] : 5]++;
        }
        len += std::snprintf(buf + len, cap - len, " fd_used=%u fd_t0=%u fd_t1=%u fd_t2=%u fd_t3=%u fd_t4=%u fd_tx=%u",
                             used, byType[0], byType[1], byType[2], byType[3], byType[4], byType[5]);
        if (nextThreshold < 4 && used >= kThresholds[nextThreshold]) {
            ++nextThreshold;
            FILE *f = std::fopen(VITA_GAME_ROOT "fd_table.log", "a");
            if (f) {
                std::fprintf(f, "===== FD_TABLE used=%u t_s=%u =====\n", used,
                             (unsigned)(sceKernelGetProcessTimeWide() / 1000000));
                for (int fd = 0; fd < 256; ++fd) {
                    const int *d = __vita_fdmap[fd];
                    if (!d) continue;
                    SceIoStat st;
                    std::memset(&st, 0, sizeof(st));
                    const int r = d[1] == 0 ? sceIoGetstatByFd(d[0], &st) : -1;
                    std::fprintf(f, "fd=%d uid=0x%08x type=%d ref=%d stat=%d mode=0%o size=%lld\n", fd,
                                 (unsigned)d[0], d[1], d[2], r, (unsigned)st.st_mode, (long long)st.st_size);
                }
                std::fclose(f);
            }
        }
    }
#endif
#ifdef MKXP_VITA_GL_LEDGER
    /* GL objects alive through the mkxp gl table (gl-fun-vita.cpp); gl_ledger.log on changes. */
    if (len < cap) {
        static unsigned window = 0;
        int ntex = 0, nbuf = 0;
        uint64_t texBytes = 0, bufBytes = 0;
        vitaGlLedgerTotals(&ntex, &texBytes, &nbuf, &bufBytes);
        len += std::snprintf(buf + len, cap - len, " gl_tex_n=%d gl_tex_kb=%llu gl_buf_n=%d gl_buf_kb=%llu", ntex,
                             (unsigned long long)(texBytes / 1024), nbuf, (unsigned long long)(bufBytes / 1024));
        vitaGlLedgerMaybeDump(window++);
    }
#endif
#ifdef MKXP_VITA_RUBY_PROF
    if (len < cap)
        len += profAppend(buf + len, cap - len);
#else
    /* Without the Ruby profiler (d56 comparison build): still the current map, once per window. */
    if (len < cap) {
        const VALUE gm = rb_gv_get("$game_map");
        static ID idMapId = 0;
        if (!idMapId) idMapId = rb_intern("map_id");
        int mapId = 0;
        if (!NIL_P(gm) && rb_respond_to(gm, idMapId)) {
            const VALUE id = rb_funcall(gm, idMapId, 0);
            if (FIXNUM_P(id)) mapId = FIX2INT(id);
        }
        len += std::snprintf(buf + len, cap - len, " map=%d", mapId);
    }
#endif
#ifdef MKXP_VITA_ALLOC_PROF
    if (len < cap)
        len += allocAppend(buf + len, cap - len);
#endif
#ifdef MKXP_VITA_CPU_444
    if (len < cap)
        len += std::snprintf(buf + len, cap - len, " cpu_mhz=%d bus_mhz=%d gpu_mhz=%d", scePowerGetArmClockFrequency(),
                             scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency());
#endif
    /* " refresh[Class]=count/total_ms" */
    for (int i = 0; i < gNRefresh && len < cap; ++i)
        len += std::snprintf(buf + len, cap - len, " refresh[%s]=%u/%.2f", gRefresh[i].cls,
                             gRefresh[i].count, gRefresh[i].us / 1000.0);
    gNRefresh = 0;
    std::memset(gPerf, 0, sizeof(gPerf));
#ifdef MKXP_VITA_LOG_BATCH
    /* d85: one card write per log every MKXP_VITA_LOG_BATCH windows (or a nearly full buffer):
     * the writes every window (3 files, 10-60 ms each on the card) were a stall of their own. */
    static unsigned vitaBatchWin = 0;
    bool vitaBatchDue = ++vitaBatchWin % MKXP_VITA_LOG_BATCH == 0;
#ifdef MKXP_VITA_RUBY_PROF
    vitaBatchDue = vitaBatchDue || slowFramesNearlyFull();
#endif
#else
    const bool vitaBatchDue = true;
#endif
#ifdef MKXP_VITA_RUBY_PROF
    std::memset(gFramePerfPrev, 0, sizeof(gFramePerfPrev));
    if (vitaBatchDue) slowFramesFlush();
#endif
    if (vitaBatchDue && vitaVglLogFlush) vitaVglLogFlush();   /* MKXP_VITA_VGL_LOG_BUFFER (gl-fun-vita.cpp) */
    return len;
}
#endif

/* ------------------------------------------------------------------ timeline */
#ifdef MKXP_VITA_TIMELINE
enum { PH_IDLE = 0, PH_BOOT = 1, PH_BATTLE = 2 };
struct Entry { uint64_t t0, dur, bytes; char tag[28]; char detail[76]; };
struct Agg { char tag[28]; unsigned count; uint64_t us, max, bytes; };

static const int kMaxEntries = 6000;
static Entry gEntries[kMaxEntries];
static int gNEntries = 0, gDropped = 0;
static Agg gAgg[160];
static int gNAgg = 0;
static int gPhase = PH_BOOT;          /* armed at process start */
static uint64_t gPhaseStart = 0;
static int gFramesInPhase = 0, gFramesAfterScene = -1, gBattleCount = 0;

static void aggAdd(const char *tag, uint64_t dur, uint64_t bytes)
{
    int i = 0;
    for (; i < gNAgg; ++i)
        if (!std::strcmp(gAgg[i].tag, tag)) break;
    if (i == gNAgg) {
        if (gNAgg == (int)(sizeof(gAgg) / sizeof(gAgg[0]))) return;
        std::memset(&gAgg[i], 0, sizeof(Agg));
        std::snprintf(gAgg[i].tag, sizeof(gAgg[i].tag), "%s", tag);
        ++gNAgg;
    }
    gAgg[i].count++;
    gAgg[i].us += dur;
    gAgg[i].max = std::max(gAgg[i].max, dur);
    gAgg[i].bytes += bytes;
}

static void addEntry(const char *tag, uint64_t t0, uint64_t dur, uint64_t bytes, const char *detail)
{
    if (gNEntries >= kMaxEntries) { ++gDropped; return; }
    Entry &e = gEntries[gNEntries++];
    e.t0 = t0; e.dur = dur; e.bytes = bytes;
    std::snprintf(e.tag, sizeof(e.tag), "%s", tag);
    std::snprintf(e.detail, sizeof(e.detail), "%s", detail ? detail : "");
}

static void resetPhase(int phase)
{
    gPhase = phase;
    gNEntries = gDropped = gNAgg = 0;
    gFramesInPhase = 0;
    gFramesAfterScene = -1;
    gPhaseStart = vitaDiagNow();
}

static void flushPhase()
{
    const char *path = gPhase == PH_BOOT ? VITA_GAME_ROOT "boot_timeline.log"
                                         : VITA_GAME_ROOT "battle_timeline.log";
    std::sort(gEntries, gEntries + gNEntries, [](const Entry &a, const Entry &b) { return a.t0 < b.t0; });
    std::sort(gAgg, gAgg + gNAgg, [](const Agg &a, const Agg &b) { return a.us > b.us; });
    FILE *f = std::fopen(path, "a");
    if (f) {
        const uint64_t base = gPhase == PH_BOOT ? 0 : gPhaseStart;
        std::fprintf(f, "\n===== %s TIMELINE%s%d =====\n", gPhase == PH_BOOT ? "BOOT" : "BATTLE ENTRY",
                     gPhase == PH_BOOT ? "" : " #", gPhase == PH_BOOT ? 0 : gBattleCount);
        std::fprintf(f, "time base: %s; entries=%d dropped=%d; t = start, dur = duration\n",
                     gPhase == PH_BOOT ? "process start" : "Game_Interpreter#command_301", gNEntries, gDropped);
        for (int i = 0; i < gNEntries; ++i) {
            const Entry &e = gEntries[i];
            /* Signed: a span that began before the phase was armed (e.g. command_301) is negative. */
            const double t = (double)(int64_t)(e.t0 - base) / 1e6;
            std::fprintf(f, "%10.3f s  %10.3f ms  %-26s %s", t, e.dur / 1000.0, e.tag, e.detail);
            if (e.bytes) std::fprintf(f, "  bytes=%llu", (unsigned long long)e.bytes);
            std::fputc('\n', f);
        }
        std::fprintf(f, "----- aggregate (sorted by total time) -----\n");
        for (int i = 0; i < gNAgg; ++i)
            std::fprintf(f, "%-26s count=%-6u total=%10.3f ms  max=%9.3f ms  bytes=%llu\n", gAgg[i].tag,
                         gAgg[i].count, gAgg[i].us / 1000.0, gAgg[i].max / 1000.0, (unsigned long long)gAgg[i].bytes);
        std::fprintf(f, "===== END =====\n");
        std::fclose(f);
    }
    resetPhase(PH_IDLE);
}

static bool alwaysRecorded(int op)
{
    return op == VD_SCRIPT_EVAL || op == VD_SHADERSET || op == VD_GL_COMPILE || op == VD_GL_LINK ||
           op == VD_ATLAS_BUILD || op == VD_TTF_INIT || op == VD_TTF_OPEN;
}
#endif

extern "C" void vitaDiagSpan(int op, uint64_t t0, uint64_t bytes, const char *detail)
{
    const uint64_t dur = vitaDiagNow() - t0;
#ifdef MKXP_VITA_PERF_BITMAP
    gPerf[op].count++;
    gPerf[op].us += dur;
    gPerf[op].bytes += bytes;
    if (op == VD_WIN_REFRESH)
        perfRefreshAdd(detail, dur);
#endif
#ifdef MKXP_VITA_TIMELINE
    if (gPhase == PH_IDLE)
        return;
    aggAdd(kOpNames[op], dur, bytes);
    const bool frameOp = op == VD_FRAME_PREPARE || op == VD_FRAME_COMPOSITE || op == VD_FRAME_SWAP;
    /* Frame spans: the first 40 frames of the phase and any frame region >= 100 ms. Other ops:
     * >= 1 ms while the buffer is below 5000 entries, then only >= 100 ms (the tail matters). */
    const bool keep = frameOp ? (gFramesInPhase < 40 || dur >= 100000)
                              : (alwaysRecorded(op) || dur >= 100000 || (dur >= 1000 && gNEntries < 5000));
    if (keep) {
        char d[76];
        if (frameOp) { std::snprintf(d, sizeof(d), "frame %d", gFramesInPhase); detail = d; }
        addEntry(kOpNames[op], t0, dur, bytes, detail);
    }
#else
    (void)detail;
#endif
}

extern "C" void vitaDiagCount(int op)
{
#ifdef MKXP_VITA_PERF_BITMAP
    gPerf[op].count++;
#endif
#ifdef MKXP_VITA_TIMELINE
    if (gPhase != PH_IDLE)
        aggAdd(kOpNames[op], 0, 0);
#endif
}

extern "C" void vitaDiagMark(const char *tag, const char *detail)
{
#ifdef MKXP_VITA_TIMELINE
    if (gPhase != PH_IDLE)
        addEntry(tag, vitaDiagNow(), 0, 0, detail);
#else
    (void)tag; (void)detail;
#endif
}

static void namedSpan(const char *tag, uint64_t t0, const char *detail, uint64_t minUs)
{
#ifdef MKXP_VITA_TIMELINE
    if (gPhase == PH_IDLE)
        return;
    const uint64_t dur = vitaDiagNow() - t0;
    aggAdd(tag, dur, 0);
    if (dur >= minUs)
        addEntry(tag, t0, dur, 0, detail);
    if ((gPhase == PH_BOOT && !std::strcmp(tag, "Scene_Map#start")) ||
        (gPhase == PH_BATTLE && !std::strcmp(tag, "Scene_Battle#start")))
        if (gFramesAfterScene < 0)
            gFramesAfterScene = 0;
#else
    (void)tag; (void)t0; (void)detail; (void)minUs;
#endif
}

extern "C" void vitaDiagNamedSpan(const char *tag, uint64_t t0, const char *detail)
{
    namedSpan(tag, t0, detail, 0);
}

extern "C" void vitaDiagFrameDone(void)
{
#ifdef MKXP_VITA_TIMELINE
    if (gPhase == PH_IDLE)
        return;
    ++gFramesInPhase;
    if (gFramesAfterScene >= 0 && ++gFramesAfterScene >= (gPhase == PH_BOOT ? 30 : 60))
        flushPhase();
#endif
}

/* ------------------------------------------------------------------ Ruby functions */
static VALUE rbDiagNow(VALUE self) { (void)self; return ULL2NUM(vitaDiagNow()); }

/* vita_diag_span(tag, t0[, detail[, min_us]]): named timeline span (aggregate always, entry if >= min_us). */
static VALUE rbDiagSpan(int argc, VALUE *argv, VALUE self)
{
    (void)self;
    if (argc < 2) return Qnil;
    VALUE tag = rb_obj_as_string(argv[0]);
    VALUE detail = argc > 2 && !NIL_P(argv[2]) ? rb_obj_as_string(argv[2]) : Qnil;
    const uint64_t minUs = argc > 3 ? NUM2ULL(argv[3]) : 0;
    namedSpan(StringValueCStr(tag), NUM2ULL(argv[1]), NIL_P(detail) ? nullptr : StringValueCStr(detail), minUs);
    return Qnil;
}

/* vita_diag_op(op_index, t0[, detail]): a VitaDiagOp span from Ruby (e.g. VD_WIN_REFRESH). */
static VALUE rbDiagOp(int argc, VALUE *argv, VALUE self)
{
    (void)self;
    if (argc < 2) return Qnil;
    const int op = NUM2INT(argv[0]);
    if (op < 0 || op >= VD_OP_COUNT) return Qnil;
    VALUE detail = argc > 2 && !NIL_P(argv[2]) ? rb_obj_as_string(argv[2]) : Qnil;
    vitaDiagSpan(op, NUM2ULL(argv[1]), 0, NIL_P(detail) ? nullptr : StringValueCStr(detail));
    return Qnil;
}

static VALUE rbDiagMark(int argc, VALUE *argv, VALUE self)
{
    (void)self;
    if (argc < 1) return Qnil;
    VALUE tag = rb_obj_as_string(argv[0]);
    VALUE detail = argc > 1 && !NIL_P(argv[1]) ? rb_obj_as_string(argv[1]) : Qnil;
    vitaDiagMark(StringValueCStr(tag), NIL_P(detail) ? nullptr : StringValueCStr(detail));
    return Qnil;
}

static VALUE rbDiagArmBattle(VALUE self)
{
    (void)self;
#ifdef MKXP_VITA_TIMELINE
    if (gPhase == PH_BOOT)
        flushPhase();
    if (gPhase == PH_IDLE) {
        ++gBattleCount;
        resetPhase(PH_BATTLE);
        vitaDiagMark("command_301", "battle entry armed");
    }
#endif
    return Qnil;
}

static VALUE rbDiagScene(VALUE self, VALUE battle)
{
    (void)self; (void)battle;
#ifdef MKXP_VITA_PERF_BITMAP
    vitaDiagBattleScene = RTEST(battle) ? 1 : 0;
#endif
    return Qnil;
}

#if defined(MKXP_VITA_RUBY_PROF) && defined(MKXP_VITA_PERF_BITMAP)
/*
 * Light profiler (MKXP_VITA_RUBY_PROF): where the Ruby time of a frame goes (ruby_ms on the PERF
 * line was ~11 ms/frame on every map in the d35 soak, only ~0.4 ms of it in bitmap operations).
 * Totals per PERF window, filled by prepended timing hooks (main.cpp): script phases, time per
 * map event (Game_Event#update, by event id), current map and its event count, GC time.
 */
static const char *const kProfNames[] = { "input", "map", "map_events", "map_interp", "player",
                                          "spriteset_map", "windows", "spriteset_battle",
                                          "spr_character", "spr_reflect", "spr_mirror", "spr_shadow", "spr_icon",
                                          "load_header", "window_new", "scene_start",
                                          "sf_party", "sf_playtime", "draw_character", "cache_character", "contents_clear",
                                          "event_update" };
static const int kProfCount = (int)(sizeof(kProfNames) / sizeof(kProfNames[0]));
static uint64_t gProfUs[kProfCount];
static uint64_t gFrameProfPrev[kProfCount], gFrameGcPrev = 0;   /* slow-frame log: values at the previous frame end */
static unsigned gProfCalls[kProfCount];
static uint64_t gProfEvUs[1000];
static int gProfMap = 0, gProfEvents = 0;
/* d73: common events by id, interpreter commands by code (time spent waiting in Fiber.yield
 * excluded), window classes (outermost update), Fiber switch / thread start latency. */
static uint64_t gProfCeUs[1000], gProfCmdUs[1000], gYieldUs = 0;
static unsigned gProfCeN[1000], gProfCmdN[1000];
struct ProfWin { VALUE klass; uint64_t us; unsigned n; };
static ProfWin gProfWin[64];
static int gProfWinN = 0;
extern "C" __attribute__((weak)) void vitaFiberTimingStats(unsigned *xferN, unsigned *xferUs, unsigned *startN, unsigned *startUs);
extern "C" __attribute__((weak)) int vitaGcTuned;   /* main.cpp (GC_TUNE_L = 1, GC_MID = 2); absent = 0 */

/* GC time from the internal GC_ENTER/GC_EXIT events (every mark/sweep step, lazy sweep included):
 * GC.total_time stays 0 on the Vita (gc.c measures it with CLOCK_PROCESS_CPUTIME_ID). The hook
 * runs inside the GC: it only reads the clock. */
static uint64_t gGcUs = 0, gGcT0 = 0;
static unsigned gGcStarts = 0;
/* Marking pause: GC_START -> GC_END_MARK (one stop for a minor GC; the sweep runs lazily after). */
static uint64_t gGcMarkT0 = 0, gGcMarkUs = 0, gGcMarkMaxUs = 0;
static void profGcHook(VALUE tpval, void *data)
{
    (void)data;
    const rb_event_flag_t ev = rb_tracearg_event_flag(rb_tracearg_from_tracepoint(tpval));
    if (ev == RUBY_INTERNAL_EVENT_GC_ENTER)
        gGcT0 = vitaDiagNow();
    else if (ev == RUBY_INTERNAL_EVENT_GC_EXIT && gGcT0)
        gGcUs += vitaDiagNow() - gGcT0, gGcT0 = 0;
    else if (ev == RUBY_INTERNAL_EVENT_GC_START)
        ++gGcStarts, gGcMarkT0 = vitaDiagNow();
    else if (ev == RUBY_INTERNAL_EVENT_GC_END_MARK && gGcMarkT0) {
        const uint64_t d = vitaDiagNow() - gGcMarkT0;
        gGcMarkUs += d;
        if (d > gGcMarkMaxUs) gGcMarkMaxUs = d;
        gGcMarkT0 = 0;
    }
}

/* Profiler clock: microseconds modulo 2^30, always a Fixnum on 32-bit Ruby. vita_diag_now became a
 * Bignum (one allocation per call) after ~18 min of uptime; d37/d39 hooks called it per sprite and
 * per event, which inflated the allocations and GC they were measuring. */
static const uint64_t kProfMask = (1u << 30) - 1;
static VALUE rbProfNow(VALUE self) { (void)self; return LONG2FIX((long)(vitaDiagNow() & kProfMask)); }
static inline uint64_t profSince(VALUE t0) { return (vitaDiagNow() - (uint64_t)FIX2LONG(t0)) & kProfMask; }

/* Active profiler phases (innermost last): the allocation profile charges each new object to the
 * innermost phase (MKXP_VITA_ALLOC_PROF). Phase kProfCount = "other" (no phase active). */
static int gPhaseStack[32];
static int gPhaseDepth = 0;
static inline int curPhase() { return gPhaseDepth ? gPhaseStack[gPhaseDepth - 1] : kProfCount; }
static inline void phasePop(int i)
{
    for (int d = gPhaseDepth - 1; d >= 0; --d)
        if (gPhaseStack[d] == i) { gPhaseDepth = d; return; }
}
static VALUE rbProfEnter(VALUE self, VALUE idx)
{
    (void)self;
    const int i = NUM2INT(idx);
    if (gPhaseDepth < 32 && i >= 0 && i < kProfCount) gPhaseStack[gPhaseDepth++] = i;
    return LONG2FIX((long)(vitaDiagNow() & kProfMask));
}

static VALUE rbProf(VALUE self, VALUE idx, VALUE t0)
{
    (void)self;
    const int i = NUM2INT(idx);
    if (i >= 0 && i < kProfCount && FIXNUM_P(t0)) gProfUs[i] += profSince(t0), gProfCalls[i]++;
    phasePop(i);
    return Qnil;
}

static VALUE rbProfEv(VALUE self, VALUE id, VALUE t0)
{
    (void)self;
    const int i = NUM2INT(id);
    if (FIXNUM_P(t0)) gProfEvUs[i >= 0 && i < 1000 ? i : 0] += profSince(t0), gProfUs[21] += profSince(t0), gProfCalls[21]++;
    phasePop(21);
    return Qnil;
}

static VALUE rbProfCe(VALUE self, VALUE id, VALUE t0)
{
    (void)self;
    const int i = FIXNUM_P(id) ? (int)FIX2LONG(id) : 0;
    if (FIXNUM_P(t0)) gProfCeUs[i >= 0 && i < 1000 ? i : 0] += profSince(t0), gProfCeN[i >= 0 && i < 1000 ? i : 0]++;
    return Qnil;
}
static VALUE rbProfYielded(VALUE self, VALUE t0)
{
    (void)self;
    if (FIXNUM_P(t0)) gYieldUs += profSince(t0);
    return Qnil;
}
static VALUE rbProfYmark(VALUE self) { (void)self; return LONG2FIX((long)(gYieldUs & kProfMask)); }
static VALUE rbProfCmd(VALUE self, VALUE code, VALUE t0, VALUE y0)
{
    (void)self;
    if (!FIXNUM_P(t0) || !FIXNUM_P(y0)) return Qnil;
    const uint64_t el = profSince(t0), y = (gYieldUs - (uint64_t)FIX2LONG(y0)) & kProfMask;
    const int c = FIXNUM_P(code) ? (int)FIX2LONG(code) : 0;
    const int k = c >= 0 && c < 1000 ? c : 0;
    gProfCmdUs[k] += el > y ? el - y : 0;
    gProfCmdN[k]++;
    return Qnil;
}
static VALUE rbProfWin(VALUE self, VALUE klass, VALUE t0)
{
    (void)self;
    if (!FIXNUM_P(t0)) return Qnil;
    int i = 0;
    while (i < gProfWinN && gProfWin[i].klass != klass) ++i;
    if (i == gProfWinN) {
        if (gProfWinN == 64) return Qnil;
        gProfWin[gProfWinN++] = ProfWin{ klass, 0, 0 };
    }
    gProfWin[i].us += profSince(t0);
    gProfWin[i].n++;
    return Qnil;
}

#ifdef MKXP_VITA_PROF_SUB
#include "vita-prof-sub.inc"   /* needs vitaDiagNow, kProfMask, profSince; host test tools/hosttests/prof-sub */
#endif

#ifdef MKXP_VITA_ALLOC_PROF
/*
 * Allocation profile (MKXP_VITA_ALLOC_PROF, diagnostic): every Ruby object allocation (internal
 * NEWOBJ event) counted by type and charged to the innermost profiler phase; one allocation in 64
 * also sampled with the Ruby method that made it (rb_profile_frames, no allocation in the hook; the
 * labels are resolved once per PERF window). d51: ~6000 objects/frame on the busy maps (Float math
 * on 32-bit Ruby) drive the 30-40 ms minor GCs: which methods allocate them?
 */
enum { AT_FLOAT, AT_ARRAY, AT_STRING, AT_HASH, AT_OBJECT, AT_DATA, AT_PROC, AT_OTHER, AT_COUNT };
static const char *const kAllocTypeNames[AT_COUNT] = { "float", "array", "string", "hash", "object", "data", "proc", "other" };
static unsigned gAllocType[AT_COUNT];
static unsigned gAllocPhase[kProfCount + 1], gAllocPhaseFloat[kProfCount + 1];
struct AllocSite { VALUE frame; unsigned count, floats; };
static AllocSite gAllocSites[512];
static unsigned gAllocSeq = 0;
static bool gAllocPaused = false;

static void allocHook(VALUE tpval, void *data)
{
    (void)data;
    if (gAllocPaused) return;
    const VALUE obj = rb_tracearg_object(rb_tracearg_from_tracepoint(tpval));
    int t;
    switch (BUILTIN_TYPE(obj)) {
    case T_FLOAT: t = AT_FLOAT; break;
    case T_ARRAY: t = AT_ARRAY; break;
    case T_STRING: t = AT_STRING; break;
    case T_HASH: t = AT_HASH; break;
    case T_OBJECT: t = AT_OBJECT; break;
    case T_DATA: t = AT_DATA; break;
    case T_IMEMO: t = AT_PROC; break;
    default: t = AT_OTHER; break;
    }
    ++gAllocType[t];
    const int ph = curPhase();
    ++gAllocPhase[ph];
    if (t == AT_FLOAT) ++gAllocPhaseFloat[ph];
    if ((++gAllocSeq & 63) == 0) {
        VALUE frame;
        if (rb_profile_frames(0, 1, &frame, nullptr) == 1) {
            unsigned h = (unsigned)((frame >> 3) * 2654435761u) & 511;
            for (int probe = 0; probe < 16; ++probe, h = (h + 1) & 511) {
                AllocSite &st = gAllocSites[h];
                if (!st.frame) st.frame = frame;
                if (st.frame == frame) { ++st.count; if (t == AT_FLOAT) ++st.floats; break; }
            }
        }
    }
}

static int allocAppend(char *buf, int cap)
{
    gAllocPaused = true;   /* the labels below allocate */
    int len = std::snprintf(buf, cap, " alloc_t=");
    for (int i = 0; i < AT_COUNT && len < cap; ++i)
        len += std::snprintf(buf + len, cap - len, "%s%s:%u", i ? "," : "", kAllocTypeNames[i], gAllocType[i]);
    if (len < cap) len += std::snprintf(buf + len, cap - len, " alloc_phase=");
    for (int k = 0, first = 1; k < 8 && len < cap; ++k) {
        int best = -1;
        for (int i = 0; i <= kProfCount; ++i)
            if (gAllocPhase[i] && (best < 0 || gAllocPhase[i] > gAllocPhase[best])) best = i;
        if (best < 0) break;
        len += std::snprintf(buf + len, cap - len, "%s%s:%u/%uf", first ? "" : ",", best < kProfCount ? kProfNames[best] : "other",
                             gAllocPhase[best], gAllocPhaseFloat[best]);
        first = 0;
        gAllocPhase[best] = 0;
    }
    if (len < cap) len += std::snprintf(buf + len, cap - len, " alloc_top=");
    for (int k = 0; k < 12 && len < cap - 120; ++k) {
        int best = -1;
        for (int i = 0; i < 512; ++i)
            if (gAllocSites[i].count && (best < 0 || gAllocSites[i].count > gAllocSites[best].count)) best = i;
        if (best < 0) break;
        const VALUE label = rb_profile_frame_full_label(gAllocSites[best].frame);
        char name[80];
        std::snprintf(name, sizeof(name), "%s", RB_TYPE_P(label, T_STRING) ? RSTRING_PTR(label) : "?");
        for (char *c = name; *c; ++c) if (*c == ' ' || *c == ',') *c = '_';
        len += std::snprintf(buf + len, cap - len, "%s%s:%u/%uf", k ? "," : "", name, gAllocSites[best].count * 64,
                             gAllocSites[best].floats * 64);
        gAllocSites[best].count = 0;
    }
    std::memset(gAllocType, 0, sizeof(gAllocType));
    std::memset(gAllocPhase, 0, sizeof(gAllocPhase));
    std::memset(gAllocPhaseFloat, 0, sizeof(gAllocPhaseFloat));
    std::memset(gAllocSites, 0, sizeof(gAllocSites));
    gAllocPaused = false;
    return len;
}
#endif

static VALUE rbProfMap(VALUE self, VALUE map, VALUE events)
{
    (void)self;
    gProfMap = NUM2INT(map);
    gProfEvents = NUM2INT(events);
    return Qnil;
}

/* " map=N events=N gc_ms=T p_<phase>=T ... ev_top=id:T,id:T,..." (T = ms in this window). */
static int profAppend(char *buf, int cap)
{
    /* Current map from $game_map (Game_Map#setup is not called when a save is loaded: map=0 in d40). */
    {
        const VALUE gm = rb_gv_get("$game_map");
        static ID idMapId = 0;
        if (!idMapId) idMapId = rb_intern("map_id");
        if (!NIL_P(gm) && rb_respond_to(gm, idMapId)) {
            const VALUE id = rb_funcall(gm, idMapId, 0);
            if (FIXNUM_P(id)) gProfMap = FIX2INT(id);
        }
    }
    int len = std::snprintf(buf, cap, " map=%d events=%d", gProfMap, gProfEvents);
    {
        /* Off-screen character sprites skipped in this window (MKXP_VITA_OFFSCREEN_SPRITES), -1 = n/a. */
        const VALUE n = rb_gv_get("$vita_skip_n");
        const VALUE on = rb_gv_get("$vita_offscreen_skip");
        if (len < cap) len += std::snprintf(buf + len, cap - len, " spr_skip=%ld offscreen_skip=%d",
                                            FIXNUM_P(n) ? FIX2LONG(n) : -1L, RTEST(on) ? 1 : 0);
        if (FIXNUM_P(n)) rb_gv_set("$vita_skip_n", INT2FIX(0));
        const VALUE sf = rb_gv_get("$vita_sfast_n");
        if (len < cap) len += std::snprintf(buf + len, cap - len, " spr_fast=%ld", FIXNUM_P(sf) ? FIX2LONG(sf) : -1L);
        if (FIXNUM_P(sf)) rb_gv_set("$vita_sfast_n", INT2FIX(0));
        const VALUE mt = rb_gv_get("$vita_mog_trimmed");
        if (len < cap && FIXNUM_P(mt)) len += std::snprintf(buf + len, cap - len, " mog_trim=%ld", FIX2LONG(mt));
        if (FIXNUM_P(mt)) rb_gv_set("$vita_mog_trimmed", INT2FIX(0));
        const VALUE cs = rb_gv_get("$vita_combo_skip");
        if (len < cap && FIXNUM_P(cs)) len += std::snprintf(buf + len, cap - len, " combo_skip=%ld", FIX2LONG(cs));
        if (FIXNUM_P(cs)) rb_gv_set("$vita_combo_skip", INT2FIX(0));
        const VALUE mf = rb_gv_get("$vita_mr_full"), ms = rb_gv_get("$vita_mr_skip");
        if (len < cap) len += std::snprintf(buf + len, cap - len, " map_refresh=%ld/%ld", FIXNUM_P(mf) ? FIX2LONG(mf) : -1L, FIXNUM_P(ms) ? FIX2LONG(ms) : -1L);
        if (FIXNUM_P(mf)) rb_gv_set("$vita_mr_full", INT2FIX(0));
        if (FIXNUM_P(ms)) rb_gv_set("$vita_mr_skip", INT2FIX(0));
        const VALUE ef = rb_gv_get("$vita_ev_fast_n");
        if (len < cap) len += std::snprintf(buf + len, cap - len, " ev_fast=%ld", FIXNUM_P(ef) ? FIX2LONG(ef) : -1L);
        if (FIXNUM_P(ef)) rb_gv_set("$vita_ev_fast_n", INT2FIX(0));
    }
    if (len < cap) len += std::snprintf(buf + len, cap - len, " gc_ms=%.2f gc_starts=%u gc_mark_ms=%.2f gc_mark_max_ms=%.2f",
                                        gGcUs / 1000.0, gGcStarts, gGcMarkUs / 1000.0, gGcMarkMaxUs / 1000.0);
    gGcUs = 0;
    gFrameGcPrev = 0;
    gGcStarts = 0;
    gGcMarkUs = 0;
    gGcMarkMaxUs = 0;
    {
        /* Minor/major GCs and objects allocated in this window (GC.stat deltas). */
        static ID idMinor, idMajor, idAlloc;
        static size_t pMinor, pMajor, pAlloc;
        if (!idMinor) {
            idMinor = rb_intern("minor_gc_count");
            idMajor = rb_intern("major_gc_count");
            idAlloc = rb_intern("total_allocated_objects");
        }
        const size_t mi = rb_gc_stat(ID2SYM(idMinor)), ma = rb_gc_stat(ID2SYM(idMajor)), al = rb_gc_stat(ID2SYM(idAlloc));
        if (len < cap) len += std::snprintf(buf + len, cap - len, " gc_minor=%u gc_major=%u alloc_k=%u gc_tune=%d",
                                            (unsigned)(mi - pMinor), (unsigned)(ma - pMajor), (unsigned)((al - pAlloc) / 1000), &vitaGcTuned ? vitaGcTuned : 0);
        pMinor = mi; pMajor = ma; pAlloc = al;
    }
    for (int i = 0; i < kProfCount && len < cap; ++i)
        len += std::snprintf(buf + len, cap - len, " p_%s=%.2f/%u", kProfNames[i], gProfUs[i] / 1000.0, gProfCalls[i]);
    for (int k = 0; k < 5 && len < cap; ++k) {
        int best = -1;
        for (int i = 0; i < 1000; ++i)
            if (gProfEvUs[i] && (best < 0 || gProfEvUs[i] > gProfEvUs[best])) best = i;
        if (best < 0) break;
        len += std::snprintf(buf + len, cap - len, "%s%d:%.2f", k ? "," : " ev_top=", best, gProfEvUs[best] / 1000.0);
        gProfEvUs[best] = 0;
    }
    for (int k = 0; k < 6 && len < cap; ++k) {   /* common events: id:ms/updates */
        int best = -1;
        for (int i = 0; i < 1000; ++i)
            if (gProfCeUs[i] && (best < 0 || gProfCeUs[i] > gProfCeUs[best])) best = i;
        if (best < 0) break;
        len += std::snprintf(buf + len, cap - len, "%s%d:%.2f/%u", k ? "," : " ce_top=", best, gProfCeUs[best] / 1000.0, gProfCeN[best]);
        gProfCeUs[best] = 0;
    }
    for (int k = 0; k < 10 && len < cap; ++k) {   /* interpreter commands: code:ms/calls */
        int best = -1;
        for (int i = 0; i < 1000; ++i)
            if (gProfCmdUs[i] && (best < 0 || gProfCmdUs[i] > gProfCmdUs[best])) best = i;
        if (best < 0) break;
        len += std::snprintf(buf + len, cap - len, "%s%d:%.2f/%u", k ? "," : " cmd_top=", best, gProfCmdUs[best] / 1000.0, gProfCmdN[best]);
        gProfCmdUs[best] = 0;
    }
    for (int k = 0; k < 6 && len < cap; ++k) {   /* window classes: Class:ms/updates */
        int best = -1;
        for (int i = 0; i < gProfWinN; ++i)
            if (gProfWin[i].us && (best < 0 || gProfWin[i].us > gProfWin[best].us)) best = i;
        if (best < 0) break;
        len += std::snprintf(buf + len, cap - len, "%s%s:%.2f/%u", k ? "," : " win_top=", rb_class2name(gProfWin[best].klass),
                             gProfWin[best].us / 1000.0, gProfWin[best].n);
        gProfWin[best].us = 0;
    }
#ifdef MKXP_VITA_PROF_SUB
    if (len < cap) len += subAppend(buf + len, cap - len);
#endif
    if (len < cap && vitaFiberTimingStats) {
        unsigned xn = 0, xus = 0, sn = 0, sus = 0;
        vitaFiberTimingStats(&xn, &xus, &sn, &sus);
        len += std::snprintf(buf + len, cap - len, " fiber_xfer=%u/%.0f fiber_start=%u/%.0f", xn, xn ? (double)xus / xn : 0.0,
                             sn, sn ? (double)sus / sn : 0.0);
    }
    std::memset(gProfUs, 0, sizeof(gProfUs));
    std::memset(gProfCalls, 0, sizeof(gProfCalls));
    std::memset(gFrameProfPrev, 0, sizeof(gFrameProfPrev));
    std::memset(gProfEvUs, 0, sizeof(gProfEvUs));
    std::memset(gProfCeUs, 0, sizeof(gProfCeUs));
    std::memset(gProfCeN, 0, sizeof(gProfCeN));
    std::memset(gProfCmdUs, 0, sizeof(gProfCmdUs));
    std::memset(gProfCmdN, 0, sizeof(gProfCmdN));
    for (int i = 0; i < gProfWinN; ++i) gProfWin[i].us = 0, gProfWin[i].n = 0;
    return len;
}
/*
 * Slow-frame log (MKXP_VITA_RUBY_PROF + FRAME_HIST): every frame >= 50 ms gets one line in
 * slow_frames.log with what happened in that frame only: Ruby time, GC time, profiler phases and
 * bitmap operations over 1 ms. d45: 1-3% of the frames over 100 ms, two 5-7 s stalls (save screen).
 * Lines are kept in RAM and written once per PERF window.
 */
#ifdef MKXP_VITA_LOG_BATCH
static char gSlowBuf[98304];   /* d85: written every LOG_BATCH windows */
#else
static char gSlowBuf[12288];
#endif
/* Slow-frame threshold (MKXP_VITA_SLOW_FRAME_MS, default 50 ms); profiler phases listed from 1 ms,
 * or 0.5 ms with a threshold under 50 ms. */
#ifndef MKXP_VITA_SLOW_FRAME_MS
#define MKXP_VITA_SLOW_FRAME_MS 50
#endif
static const uint64_t kSlowFrameUs = (uint64_t)MKXP_VITA_SLOW_FRAME_MS * 1000;
static const uint64_t kSlowPartUs = MKXP_VITA_SLOW_FRAME_MS < 50 ? 500 : 1000;
static int gSlowLen = 0;
static unsigned gFrameNo = 0;

/* Main-thread CPU accounting (sceKernelGetThreadInfo): CPU time actually run vs wall time, and how
 * often the thread was preempted or moved to another core. d48: on the walking-step frames
 * prepareDraw and composite jumped from < 1 ms to ~14 ms each with no upload: working or waiting? */
struct ThrSample { uint64_t run; unsigned preempt, moves; };
static ThrSample thrNow()
{
    SceKernelThreadInfo ti;
    std::memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    ThrSample t = { 0, 0, 0 };
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) == 0) {
        t.run = ti.runClocks;
        t.preempt = ti.threadPreemptCount + ti.intrPreemptCount;
        t.moves = (unsigned)ti.changeCpuCount;
    }
    return t;
}
static ThrSample gThrFrame0, gThrPhase0;
static uint64_t gPhaseWall0 = 0, gPrepCpu = 0, gPrepWall = 0, gCompCpu = 0, gCompWall = 0;

/* sharedstate_test.cpp: around prepareDraw (which 0) and composite (which 1). */
extern "C" void vitaDiagPhaseMark(int which, int begin)
{
    if (begin) {
        gThrPhase0 = thrNow();
        gPhaseWall0 = vitaDiagNow();
        return;
    }
    const ThrSample t = thrNow();
    const uint64_t cpu = t.run - gThrPhase0.run, wall = vitaDiagNow() - gPhaseWall0;
    if (which == 0) { gPrepCpu = cpu; gPrepWall = wall; } else { gCompCpu = cpu; gCompWall = wall; }
}

extern "C" void vitaDiagFrameEnd(unsigned long long frameUs, unsigned long long rubyUs)
{
    ++gFrameNo;
    const ThrSample thr = thrNow();
    static ID idMinor = 0, idMajor = 0, idLive = 0;
    static size_t gFrameMinorPrev = 0, gFrameMajorPrev = 0;
    if (!idMinor) {
        idMinor = rb_intern("minor_gc_count");
        idMajor = rb_intern("major_gc_count");
        idLive = rb_intern("heap_live_slots");
    }
    const size_t gcMinor = rb_gc_stat(ID2SYM(idMinor)), gcMajor = rb_gc_stat(ID2SYM(idMajor));
    const bool vitaSlow = frameUs >= kSlowFrameUs && gSlowLen < (int)sizeof(gSlowBuf) - 900;
    if (vitaSlow) {
        char *b = gSlowBuf + gSlowLen;
        const int cap = (int)sizeof(gSlowBuf) - gSlowLen;
        int n = std::snprintf(b, cap, "SLOW_FRAME n=%u t_s=%u ms=%.1f ruby_ms=%.1f gc_ms=%.1f gc_minor=%u gc_major=%u live=%u scene=%s map=%d", gFrameNo,
                              (unsigned)(vitaDiagNow() / 1000000), frameUs / 1000.0, rubyUs / 1000.0,
                              (gGcUs - gFrameGcPrev) / 1000.0, (unsigned)(gcMinor - gFrameMinorPrev), (unsigned)(gcMajor - gFrameMajorPrev),
                              (unsigned)rb_gc_stat(ID2SYM(idLive)), vitaDiagBattleScene ? "BATTLE" : "MAP", gProfMap);
        if (n < cap - 120)
            n += std::snprintf(b + n, cap - n, " cpu_ms=%.1f preempt=%u cpu_moves=%u prep=%.1f/%.1f comp=%.1f/%.1f",
                               (thr.run - gThrFrame0.run) / 1000.0, thr.preempt - gThrFrame0.preempt, thr.moves - gThrFrame0.moves,
                               gPrepCpu / 1000.0, gPrepWall / 1000.0, gCompCpu / 1000.0, gCompWall / 1000.0);
        for (int i = 0; i < kProfCount && n < cap - 40; ++i) {
            const uint64_t d = gProfUs[i] - gFrameProfPrev[i];
            if (d >= kSlowPartUs) n += std::snprintf(b + n, cap - n, " p_%s=%.1f", kProfNames[i], d / 1000.0);
        }
        for (int i = 0; i < VD_OP_COUNT && n < cap - 40; ++i) {
            const uint64_t d = gPerf[i].us - gFramePerfPrev[i].us;
            if (d >= 1000) n += std::snprintf(b + n, cap - n, " %s=%u/%.1f", kOpNames[i], gPerf[i].count - gFramePerfPrev[i].count, d / 1000.0);
        }
        if (vitaBlockFrameEnd && n < cap - 200) n += vitaBlockFrameEnd(b + n, cap - 200 - n, 1);
#ifdef MKXP_VITA_PROF_SUB
        if (n < cap - 200) n += subFrameAppend(b + n, cap - 200 - n, true);
#endif
        if (n < cap - 1) { b[n++] = '\n'; gSlowLen += n; }
    } else if (vitaBlockFrameEnd) {
        vitaBlockFrameEnd(nullptr, 0, 0);
    }
#ifdef MKXP_VITA_PROF_SUB
    if (!vitaSlow) subFrameAppend(nullptr, 0, false);
#endif
    std::memcpy(gFrameProfPrev, gProfUs, sizeof(gProfUs));
    gFrameGcPrev = gGcUs;
    gFrameMinorPrev = gcMinor;
    gFrameMajorPrev = gcMajor;
    gThrFrame0 = thr;
    std::memcpy(gFramePerfPrev, gPerf, sizeof(gPerf));
}

static bool slowFramesNearlyFull() { return gSlowLen > (int)sizeof(gSlowBuf) * 3 / 4; }
static void slowFramesFlush()
{
    if (!gSlowLen) return;
    FILE *f = std::fopen(VITA_GAME_ROOT "slow_frames.log", "a");
    if (f) { std::fwrite(gSlowBuf, 1, (size_t)gSlowLen, f); std::fclose(f); }
    gSlowLen = 0;
}
#endif

extern "C" void vitaDiagRubyInit(void)
{
#if defined(MKXP_VITA_RUBY_PROF) && defined(MKXP_VITA_PERF_BITMAP)
    rb_define_global_function("vita_prof_now", RUBY_METHOD_FUNC(rbProfNow), 0);
    rb_define_global_function("vita_prof_enter", RUBY_METHOD_FUNC(rbProfEnter), 1);
#ifdef MKXP_VITA_ALLOC_PROF
    {
        VALUE tpa = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ, allocHook, nullptr);
        rb_gc_register_mark_object(tpa);
        rb_tracepoint_enable(tpa);
    }
#endif
    rb_define_global_function("vita_prof", RUBY_METHOD_FUNC(rbProf), 2);
    rb_define_global_function("vita_prof_ev", RUBY_METHOD_FUNC(rbProfEv), 2);
    rb_define_global_function("vita_prof_ce", RUBY_METHOD_FUNC(rbProfCe), 2);
    rb_define_global_function("vita_prof_yielded", RUBY_METHOD_FUNC(rbProfYielded), 1);
    rb_define_global_function("vita_prof_ymark", RUBY_METHOD_FUNC(rbProfYmark), 0);
    rb_define_global_function("vita_prof_cmd", RUBY_METHOD_FUNC(rbProfCmd), 3);
    rb_define_global_function("vita_prof_win", RUBY_METHOD_FUNC(rbProfWin), 2);
    rb_define_global_function("vita_prof_map", RUBY_METHOD_FUNC(rbProfMap), 2);
#ifdef MKXP_VITA_PROF_SUB
    rb_define_global_function("vita_prof_sub_slot", RUBY_METHOD_FUNC(rbProfSubSlot), 1);
    rb_define_global_function("vita_prof_sub_enter", RUBY_METHOD_FUNC(rbProfSubEnter), 0);
    rb_define_global_function("vita_prof_sub", RUBY_METHOD_FUNC(rbProfSub), 2);
#endif
    {
        VALUE tp = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_GC_START | RUBY_INTERNAL_EVENT_GC_END_MARK |
                                            RUBY_INTERNAL_EVENT_GC_ENTER | RUBY_INTERNAL_EVENT_GC_EXIT,
                                     profGcHook, nullptr);
        rb_gc_register_mark_object(tp);
        rb_tracepoint_enable(tp);
    }
#endif
    rb_define_global_function("vita_diag_now", RUBY_METHOD_FUNC(rbDiagNow), 0);
    rb_define_global_function("vita_diag_span", RUBY_METHOD_FUNC(rbDiagSpan), -1);
    rb_define_global_function("vita_diag_mark", RUBY_METHOD_FUNC(rbDiagMark), -1);
    rb_define_global_function("vita_diag_op", RUBY_METHOD_FUNC(rbDiagOp), -1);
    rb_define_global_function("vita_diag_arm_battle", RUBY_METHOD_FUNC(rbDiagArmBattle), 0);
    rb_define_const(rb_cObject, "VITA_DIAG_OP_WIN_REFRESH", INT2NUM(VD_WIN_REFRESH));
    rb_define_global_function("vita_diag_scene", RUBY_METHOD_FUNC(rbDiagScene), 1);
}
