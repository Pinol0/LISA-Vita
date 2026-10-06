/*
 * Vita-only freeze probe (MKXP_VITA_FREEZE_PROBE). Diagnostic only: observes, never fixes.
 *
 * Writers (main thread, and whichever Fiber thread currently runs Ruby) only do an atomic
 * fetch-add on the ring head plus plain stores guarded by a per-entry sequence number. The
 * watchdog thread reads without locks: an entry whose sequence changed while it was copied is
 * dropped. Nothing here allocates, prints or does I/O on the writer side. The watchdog uses only
 * sceKernel and sceIo calls and its own integer formatter (no stdio, no malloc, no newlib locks),
 * and never calls OpenGL, vitaGL or Ruby.
 */
#include "vita_freeze_probe.h"
#include "vita_paths.h"

#include <stddef.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/io/fcntl.h>

#include <ruby.h>

#include "vita_build_tag.h"

#define FZ_RING_SIZE 256u              /* power of two */
#define FZ_RING_DUMP 128u
#define FZ_HIST_SIZE 64u               /* power of two */
#define FZ_PREVIEW 96
#define FZ_WATCH_PERIOD_US 250000u
#define FZ_STALL_US 2000000u
#define FZ_STILL_US 5000000u           /* first dump at 2 s, one more line at 2 + 3 s */
#define FZ_MAX_EPISODES 4
#define FZ_LOG_PATH VITA_GAME_ROOT "freeze.log"

/* ------------------------------------------------------------------------------------------ */
/* Shared state                                                                               */

struct FzEntry
{
    uint32_t seq;          /* 0 = being written, else ring index + 1 */
    uint32_t frame;
    uint32_t phase;
    uint32_t tid;
    uint64_t ts;
    uintptr_t a[4];
};

struct FzCmd
{
    uint32_t seq;
    uint32_t frame;
    uint64_t ts;
    uint32_t map_id;
    uint32_t event_id;
    uint32_t depth;
    uint32_t index;
    int32_t code;
    uint32_t script_hash;  /* 355 only */
};

struct FzPreview           /* seqlock: odd while being written */
{
    uint32_t seq;
    uint32_t hash;
    uint32_t len;
    uint32_t extra;        /* 655 lines (script) or character id (move route) */
    uint32_t frame;
    char text[FZ_PREVIEW];
};

struct FzCo                /* seqlock */
{
    uint32_t seq;
    uintptr_t src;
    uintptr_t dst;
    uint32_t n;
    uint32_t tid;
    uintptr_t ret;
    uint64_t ts;
};

static FzEntry fzRing[FZ_RING_SIZE];
static uint32_t fzHead;

static FzCmd fzHist[FZ_HIST_SIZE];
static uint32_t fzHistHead;

static uint32_t fzFrame;               /* completed Graphics.update calls */
static uint32_t fzPhase;
static uint64_t fzLastMarkTs;
static uint64_t fzLastFrameTs;
static uint32_t fzMainTid;

/* Interpreter state, updated right before each event command runs */
static uint32_t fzIpSeq;
static uint32_t fzIpMap, fzIpEvent, fzIpDepth, fzIpIndex;
static int32_t fzIpCode;
static uint32_t fzCmdCount;

static FzPreview fzScript;             /* last Script (355) command */
static FzPreview fzMoveScript;         /* last move route script (code 45) */

static FzCo fzCoEnter, fzCoReturn, fzCoExitEnter;
static uint32_t fzCoTransfers, fzCoReturns, fzCoExitEnters, fzCoExitReturns;
static uint32_t fzCoInits, fzCoDestroyEnters, fzCoDestroyExits;
static uint32_t fzInterpResumes, fzInterpReturns;

static char fzFailText[4096];          /* written once by the main thread on script failure */
static uint32_t fzFailSeq;
static int32_t fzFailIndex = -1;

static inline uint64_t fzNow() { return sceKernelGetProcessTimeWide(); }

#define FZ_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define FZ_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static uint32_t fzFnv(const char *s, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; ++i) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------------------------------ */
/* Writer side                                                                                */

extern "C" void vitaFreezeMark(uint32_t phase, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
    const uint64_t now = fzNow();
    const uint32_t idx = __atomic_fetch_add(&fzHead, 1u, __ATOMIC_RELAXED);
    FzEntry *e = &fzRing[idx & (FZ_RING_SIZE - 1)];

    __atomic_store_n(&e->seq, 0u, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    e->frame = __atomic_load_n(&fzFrame, __ATOMIC_RELAXED);
    e->phase = phase;
    e->tid = (uint32_t)sceKernelGetThreadId();
    e->ts = now;
    e->a[0] = a0;
    e->a[1] = a1;
    e->a[2] = a2;
    e->a[3] = a3;
    FZ_STORE(e->seq, idx + 1u);

    __atomic_store_n(&fzPhase, phase, __ATOMIC_RELAXED);
    __atomic_store_n(&fzLastMarkTs, now, __ATOMIC_RELAXED);
}

extern "C" void vitaFreezeFrameDone(void)
{
    vitaFreezeMark(VITA_FZ_FRAME_END, 0, 0, 0, 0);
    __atomic_fetch_add(&fzFrame, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&fzLastFrameTs, fzNow(), __ATOMIC_RELEASE);
    vitaFreezeMark(VITA_FZ_FRAME_BEGIN, 0, 0, 0, 0);
}

static void fzPreviewSet(FzPreview &p, const char *s, size_t n, uint32_t hash, uint32_t extra)
{
    const uint32_t seq = __atomic_load_n(&p.seq, __ATOMIC_RELAXED);
    FZ_STORE(p.seq, seq + 1u);                 /* odd: writing */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    const size_t c = n < (size_t)(FZ_PREVIEW - 1) ? n : (size_t)(FZ_PREVIEW - 1);
    for (size_t i = 0; i < c; ++i) {
        const char ch = s[i];
        p.text[i] = (ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : ch;
    }
    p.text[c] = 0;
    p.hash = hash;
    p.len = (uint32_t)n;
    p.extra = extra;
    p.frame = __atomic_load_n(&fzFrame, __ATOMIC_RELAXED);
    FZ_STORE(p.seq, seq + 2u);                 /* even: stable */
}

static void fzCoSet(FzCo &c, uintptr_t src, uintptr_t dst, uint32_t n, uint32_t tid, uintptr_t ret)
{
    const uint32_t seq = __atomic_load_n(&c.seq, __ATOMIC_RELAXED);
    FZ_STORE(c.seq, seq + 1u);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    c.src = src;
    c.dst = dst;
    c.n = n;
    c.tid = tid;
    c.ret = ret;
    c.ts = fzNow();
    FZ_STORE(c.seq, seq + 2u);
}

extern "C" void vitaFreezeScriptFailed(long index, const char *name, const char *cls, const char *msg,
                                       const char *backtrace)
{
    /* Error path only. Lines: name, class, message, then one backtrace entry per line. */
    FZ_STORE(fzFailSeq, 1u);
    size_t o = 0;
    const char *parts[4] = { name, cls, msg, backtrace };
    const char *labels[4] = { "script_name=", "exception_class=", "message=", "backtrace:\n" };
    for (int k = 0; k < 4; ++k) {
        for (const char *l = labels[k]; *l && o < sizeof(fzFailText) - 2; ++l)
            fzFailText[o++] = *l;
        const char *p = parts[k] ? parts[k] : "";
        for (; *p && o < sizeof(fzFailText) - 2; ++p)
            fzFailText[o++] = (k < 3 && *p == '\n') ? ' ' : *p;   /* backtrace keeps its newlines */
        if (o < sizeof(fzFailText) - 2 && (o == 0 || fzFailText[o - 1] != '\n'))
            fzFailText[o++] = '\n';
    }
    fzFailText[o] = 0;
    fzFailIndex = (int32_t)index;
    FZ_STORE(fzFailSeq, 2u);
    vitaFreezeMark(VITA_FZ_SCRIPT_FAILED, (uintptr_t)index, 0, 0, 0);
}

/* ------------------------------------------------------------------------------------------ */
/* Coroutine backend: linker --wrap around the entry points cont.o calls in Context.o         */

extern "C" {
void *__real_coroutine_transfer(void *current, void *target);
void *__real_coroutine_transfer_exit(void *current, void *target);
void __real_coroutine_initialize(void *context, void *start, void *stack, size_t size);
void __real_coroutine_destroy(void *context);

#ifdef MKXP_VITA_RUBY_PROF
/* Profiler (d73): latency of a Fiber switch (request on one thread -> the other thread running) and
 * of a new Fiber's thread start (request -> pte_threadStart). Transfers are serialized by the GVL:
 * one request in flight at a time. */
static volatile uint64_t fzXferT = 0;
static uint64_t fzXferUs = 0, fzStartUs = 0;
static unsigned fzXferN = 0, fzStartN = 0;
void vitaFiberTimingThreadStart(void)   /* vita-pthread-parms.cpp, on the new thread */
{
    const uint64_t t = fzXferT;
    if (t) {
        fzStartUs += sceKernelGetProcessTimeWide() - t;
        ++fzStartN;
    }
}
void vitaFiberTimingStats(unsigned *xferN, unsigned *xferUs, unsigned *startN, unsigned *startUs)
{
    *xferN = fzXferN; *xferUs = (unsigned)fzXferUs; *startN = fzStartN; *startUs = (unsigned)fzStartUs;
    fzXferN = fzStartN = 0;
    fzXferUs = fzStartUs = 0;
}
#define FZ_XFER_REQUEST() (fzXferT = sceKernelGetProcessTimeWide())
#define FZ_XFER_RESUMED() do { const uint64_t t_ = fzXferT; if (t_) { fzXferUs += sceKernelGetProcessTimeWide() - t_; ++fzXferN; } } while (0)
#else
#define FZ_XFER_REQUEST() ((void)0)
#define FZ_XFER_RESUMED() ((void)0)
#endif

void *__wrap_coroutine_transfer(void *current, void *target)
{
    const uint32_t n = __atomic_add_fetch(&fzCoTransfers, 1u, __ATOMIC_RELAXED);
    const uint32_t tid = (uint32_t)sceKernelGetThreadId();
    fzCoSet(fzCoEnter, (uintptr_t)current, (uintptr_t)target, n, tid, 0);
    vitaFreezeMark(VITA_FZ_CO_TRANSFER_ENTER, (uintptr_t)current, (uintptr_t)target, n, tid);

    FZ_XFER_REQUEST();
    void *r = __real_coroutine_transfer(current, target);
    FZ_XFER_RESUMED();

    /* Runs on current's thread once something transfers back to it. */
    __atomic_add_fetch(&fzCoReturns, 1u, __ATOMIC_RELAXED);
    fzCoSet(fzCoReturn, (uintptr_t)current, (uintptr_t)target, n, (uint32_t)sceKernelGetThreadId(), (uintptr_t)r);
    vitaFreezeMark(VITA_FZ_CO_TRANSFER_EXIT, (uintptr_t)current, (uintptr_t)target, n, (uintptr_t)r);
    return r;
}

void *__wrap_coroutine_transfer_exit(void *current, void *target)
{
    const uint32_t n = __atomic_add_fetch(&fzCoExitEnters, 1u, __ATOMIC_RELAXED);
    const uint32_t tid = (uint32_t)sceKernelGetThreadId();
    fzCoSet(fzCoExitEnter, (uintptr_t)current, (uintptr_t)target, n, tid, 0);
    vitaFreezeMark(VITA_FZ_CO_TRANSFER_EXIT_ENTER, (uintptr_t)current, (uintptr_t)target, n, tid);

    /* Normally ends this thread (pthread_exit); returns only on the fallback path. */
    FZ_XFER_REQUEST();
    void *r = __real_coroutine_transfer_exit(current, target);

    __atomic_add_fetch(&fzCoExitReturns, 1u, __ATOMIC_RELAXED);
    vitaFreezeMark(VITA_FZ_CO_TRANSFER_EXIT_RETURN, (uintptr_t)current, (uintptr_t)target, n, (uintptr_t)r);
    return r;
}

void __wrap_coroutine_initialize(void *context, void *start, void *stack, size_t size)
{
    __atomic_add_fetch(&fzCoInits, 1u, __ATOMIC_RELAXED);
    vitaFreezeMark(VITA_FZ_CO_INIT, (uintptr_t)context, (uintptr_t)stack, (uintptr_t)size, 0);
    __real_coroutine_initialize(context, start, stack, size);
}

void __wrap_coroutine_destroy(void *context)
{
    __atomic_add_fetch(&fzCoDestroyEnters, 1u, __ATOMIC_RELAXED);
    vitaFreezeMark(VITA_FZ_CO_DESTROY_ENTER, (uintptr_t)context, 0, 0, 0);
    __real_coroutine_destroy(context);
    __atomic_add_fetch(&fzCoDestroyExits, 1u, __ATOMIC_RELAXED);
    vitaFreezeMark(VITA_FZ_CO_DESTROY_EXIT, (uintptr_t)context, 0, 0, 0);
}

/* libruby (fiberexit-v2) keeps these counters; a plain struct copy, no locks. */
struct coroutine_vita_stats
{
    unsigned int created;
    unsigned int exited;
    unsigned int joined_exited;
    unsigned int joined_suspended;
    unsigned int released_exited;
};
void coroutine_vita_get_stats(struct coroutine_vita_stats *stats);
}

/* ------------------------------------------------------------------------------------------ */
/* Ruby side: module VitaProbe (called by the prepended hooks)                                */

static inline uintptr_t fzNum(VALUE v)
{
    return FIXNUM_P(v) ? (uintptr_t)FIX2LONG(v) : 0;
}

static VALUE fz_rb_m(VALUE self, VALUE phase)
{
    (void)self;
    const uint32_t ph = (uint32_t)fzNum(phase);
    if (ph == VITA_FZ_INTERP_UPDATE_EXIT)
        __atomic_add_fetch(&fzInterpReturns, 1u, __ATOMIC_RELAXED);
    vitaFreezeMark(ph, 0, 0, 0, 0);
    return Qnil;
}

static VALUE fz_rb_m2(VALUE self, VALUE phase, VALUE a0, VALUE a1)
{
    (void)self;
    const uint32_t ph = (uint32_t)fzNum(phase);
    if (ph == VITA_FZ_INTERP_UPDATE_ENTER)
        __atomic_add_fetch(&fzInterpResumes, 1u, __ATOMIC_RELAXED);
    vitaFreezeMark(ph, fzNum(a0), fzNum(a1), 0, 0);
    return Qnil;
}

static VALUE fz_rb_cmd(VALUE self, VALUE map, VALUE ev, VALUE depth, VALUE index, VALUE code)
{
    (void)self;
    const uint32_t m = (uint32_t)fzNum(map), e = (uint32_t)fzNum(ev);
    const uint32_t d = (uint32_t)fzNum(depth), i = (uint32_t)fzNum(index);
    const int32_t c = FIXNUM_P(code) ? (int32_t)FIX2LONG(code) : -1;

    const uint32_t seq = __atomic_load_n(&fzIpSeq, __ATOMIC_RELAXED);
    FZ_STORE(fzIpSeq, seq + 1u);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    fzIpMap = m; fzIpEvent = e; fzIpDepth = d; fzIpIndex = i; fzIpCode = c;
    FZ_STORE(fzIpSeq, seq + 2u);
    __atomic_add_fetch(&fzCmdCount, 1u, __ATOMIC_RELAXED);

    const uint32_t h = __atomic_fetch_add(&fzHistHead, 1u, __ATOMIC_RELAXED);
    FzCmd *r = &fzHist[h & (FZ_HIST_SIZE - 1)];
    __atomic_store_n(&r->seq, 0u, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r->frame = __atomic_load_n(&fzFrame, __ATOMIC_RELAXED);
    r->ts = fzNow();
    r->map_id = m; r->event_id = e; r->depth = d; r->index = i; r->code = c;
    r->script_hash = 0;
    FZ_STORE(r->seq, h + 1u);

    vitaFreezeMark(VITA_FZ_INTERP_CMD, m, e, ((uintptr_t)(d & 0xffff) << 16) | (i & 0xffff), (uintptr_t)c);
    return Qnil;
}

/* Script (355) about to be evaluated: hash/len/preview of its first line, no Ruby allocation. */
static VALUE fz_rb_script(VALUE self, VALUE str, VALUE extraLines)
{
    (void)self;
    uint32_t hash = 0, len = 0;
    if (RB_TYPE_P(str, T_STRING)) {
        const char *p = RSTRING_PTR(str);
        len = (uint32_t)RSTRING_LEN(str);
        hash = fzFnv(p, len, 2166136261u);
        fzPreviewSet(fzScript, p, len, hash, (uint32_t)fzNum(extraLines));
    }
    const uint32_t h = __atomic_load_n(&fzHistHead, __ATOMIC_RELAXED);
    if (h) fzHist[(h - 1) & (FZ_HIST_SIZE - 1)].script_hash = hash;  /* same command's entry */
    vitaFreezeMark(VITA_FZ_RUBY_SCRIPT_ENTER, hash, len, fzNum(extraLines), 0);
    return Qnil;
}

static VALUE fz_rb_mscript(VALUE self, VALUE str, VALUE charId)
{
    (void)self;
    uint32_t hash = 0, len = 0;
    if (RB_TYPE_P(str, T_STRING)) {
        const char *p = RSTRING_PTR(str);
        len = (uint32_t)RSTRING_LEN(str);
        hash = fzFnv(p, len, 2166136261u);
        fzPreviewSet(fzMoveScript, p, len, hash, (uint32_t)fzNum(charId));
    }
    vitaFreezeMark(VITA_FZ_MOVE_SCRIPT_ENTER, hash, len, fzNum(charId), 0);
    return Qnil;
}

static VALUE fz_rb_cache(VALUE self, VALUE folder, VALUE file)
{
    (void)self;
    uint32_t h = 2166136261u;
    if (RB_TYPE_P(folder, T_STRING)) h = fzFnv(RSTRING_PTR(folder), RSTRING_LEN(folder), h);
    if (RB_TYPE_P(file, T_STRING)) h = fzFnv(RSTRING_PTR(file), RSTRING_LEN(file), h);
    vitaFreezeMark(VITA_FZ_CACHE_LOAD_ENTER, h, 0, 0, 0);
    return Qnil;
}

static VALUE fz_rb_install_hooks(VALUE self);

extern "C" void vitaFreezeRubyInit(void)
{
    VALUE mod = rb_define_module("VitaProbe");
    rb_define_singleton_method(mod, "m", RUBY_METHOD_FUNC(fz_rb_m), 1);
    rb_define_singleton_method(mod, "m2", RUBY_METHOD_FUNC(fz_rb_m2), 3);
    rb_define_singleton_method(mod, "cmd", RUBY_METHOD_FUNC(fz_rb_cmd), 5);
    rb_define_singleton_method(mod, "script", RUBY_METHOD_FUNC(fz_rb_script), 2);
    rb_define_singleton_method(mod, "mscript", RUBY_METHOD_FUNC(fz_rb_mscript), 2);
    rb_define_singleton_method(mod, "cache", RUBY_METHOD_FUNC(fz_rb_cache), 2);
    rb_define_global_function("vita_freeze_install_hooks", RUBY_METHOD_FUNC(fz_rb_install_hooks), 0);
}

/* Phase numbers must match enum VitaFreezePhase. Every hook calls super unchanged. */
static const char fzRubyHooks[] =
    "module VitaProbeHooks\n"
    "  module SceneMap\n"
    "    def update; VitaProbe.m(30); super; ensure; VitaProbe.m(31); end\n"
    "  end\n"
    "  module GameMap\n"
    "    def update(*a); VitaProbe.m(32); super; ensure; VitaProbe.m(33); end\n"
    "    def update_events; VitaProbe.m(36); super; ensure; VitaProbe.m(37); end\n"
    "    def setup(map_id); VitaProbe.m2(49, map_id, 0); super; ensure; VitaProbe.m(50); end\n"
    "  end\n"
    "  module GamePlayer\n"
    "    def update; VitaProbe.m(34); super; ensure; VitaProbe.m(35); end\n"
    "    def perform_transfer; VitaProbe.m2(51, @new_map_id, 0); super; ensure; VitaProbe.m(52); end\n"
    "  end\n"
    "  module SpritesetMap\n"
    "    def update; VitaProbe.m(38); super; ensure; VitaProbe.m(39); end\n"
    "    def dispose; VitaProbe.m(55); super; ensure; VitaProbe.m(56); end\n"
    "    def refresh_characters; VitaProbe.m(57); super; ensure; VitaProbe.m(58); end\n"
    "  end\n"
    "  module Interpreter\n"
    "    def update; VitaProbe.m2(40, @event_id, @depth); super; ensure; VitaProbe.m(41); end\n"
    "    def execute_command\n"
    "      c = @list[@index]\n"
    "      VitaProbe.cmd(@map_id, @event_id, @depth, @index, c ? c.code : nil)\n"
    "      super\n"
    "    end\n"
    "    def command_355\n"
    "      n = 0\n"
    "      i = @index + 1\n"
    "      while @list[i] && @list[i].code == 655\n"
    "        n += 1\n"
    "        i += 1\n"
    "      end\n"
    "      VitaProbe.script(@list[@index].parameters[0], n)\n"
    "      super\n"
    "    ensure\n"
    "      VitaProbe.m(44)\n"
    "    end\n"
    "  end\n"
    "  module Character\n"
    "    def process_move_command(command)\n"
    "      return super unless command.code == 45\n"
    "      VitaProbe.mscript(command.parameters[0], @id)\n"
    "      begin\n"
    "        super\n"
    "      ensure\n"
    "        VitaProbe.m(46)\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "  module CacheHook\n"
    "    def load_bitmap(folder_name, filename, hue = 0)\n"
    "      VitaProbe.cache(folder_name.to_s, filename.to_s)\n"
    "      super\n"
    "    ensure\n"
    "      VitaProbe.m(48)\n"
    "    end\n"
    "  end\n"
    "end\n"
    "Scene_Map.prepend(VitaProbeHooks::SceneMap)\n"
    "Game_Map.prepend(VitaProbeHooks::GameMap)\n"
    "Game_Player.prepend(VitaProbeHooks::GamePlayer)\n"
    "Spriteset_Map.prepend(VitaProbeHooks::SpritesetMap)\n"
    "Game_Interpreter.prepend(VitaProbeHooks::Interpreter)\n"
    "Game_Character.prepend(VitaProbeHooks::Character)\n"
    "Cache.singleton_class.prepend(VitaProbeHooks::CacheHook)\n"
    "VitaProbe.m(74)\n";

/* Once only: prepend order matters, and a second prepend of the same modules is not wanted. */
static int fzHooksInstalled;

extern "C" int vitaFreezeRubyInstallHooks(void)
{
    if (fzHooksInstalled)
        return -1;
    fzHooksInstalled = 1;
    int state = 0;
    rb_eval_string_protect(fzRubyHooks, &state);
    if (state)
        rb_set_errinfo(Qnil);
    return state;
}

/* Called by rgss_main right before it yields to the game (main.cpp engine stub): every alias and
 * class_eval of rgss_main has run by then, so no later alias can capture a probe wrapper. */
static VALUE fz_rb_install_hooks(VALUE self)
{
    (void)self;
    const int state = vitaFreezeRubyInstallHooks();
    sceClibPrintf("VITA_TRACE freeze probe ruby hooks state=%d\n", state);
    return INT2FIX(state);
}

/* ------------------------------------------------------------------------------------------ */
/* Watchdog                                                                                   */

static const char *const fzPhaseNames[VITA_FZ_PHASE_COUNT] = {
    /*  0 */ "NONE", "FRAME_BEGIN", "FRAME_END", "GRAPHICS_UPDATE_ENTER", "GRAPHICS_UPDATE_EXIT",
    /*  5 */ "VITA_RENDER_ENTER", "VITA_RENDER_EXIT", "PREPARE_ENTER", "PREPARE_EXIT",
    /*  9 */ "SCENE_FBO_SETUP_ENTER", "SCENE_FBO_SETUP_EXIT", "COMPOSITE_ENTER", "COMPOSITE_EXIT",
    /* 13 */ "PREBLIT_DELAY_ENTER", "PREBLIT_DELAY_EXIT", "FINAL_BLIT_ENTER", "FINAL_BLIT_EXIT",
    /* 17 */ "SWAP_ENTER", "SWAP_EXIT", "PERF_WRITE_ENTER", "PERF_WRITE_EXIT",
    /* 21 */ "INPUT_UPDATE_ENTER", "INPUT_UPDATE_EXIT", 0, 0, 0, 0, 0, 0, 0,
    /* 30 */ "SCENE_MAP_UPDATE_ENTER", "SCENE_MAP_UPDATE_EXIT", "GAME_MAP_UPDATE_ENTER",
    /* 33 */ "GAME_MAP_UPDATE_EXIT", "GAME_PLAYER_UPDATE_ENTER", "GAME_PLAYER_UPDATE_EXIT",
    /* 36 */ "MAP_EVENTS_UPDATE_ENTER", "MAP_EVENTS_UPDATE_EXIT", "SPRITESET_MAP_UPDATE_ENTER",
    /* 39 */ "SPRITESET_MAP_UPDATE_EXIT", "INTERP_UPDATE_ENTER", "INTERP_UPDATE_EXIT", "INTERP_CMD",
    /* 43 */ "RUBY_SCRIPT_ENTER", "RUBY_SCRIPT_EXIT", "MOVE_SCRIPT_ENTER", "MOVE_SCRIPT_EXIT",
    /* 47 */ "CACHE_LOAD_ENTER", "CACHE_LOAD_EXIT", "MAP_SETUP_ENTER", "MAP_SETUP_EXIT",
    /* 51 */ "PLAYER_TRANSFER_ENTER", "PLAYER_TRANSFER_EXIT", "SPRITESET_CREATE_ENTER",
    /* 54 */ "SPRITESET_CREATE_EXIT", "SPRITESET_DISPOSE_ENTER", "SPRITESET_DISPOSE_EXIT",
    /* 57 */ "SPRITESET_REFRESH_ENTER", "SPRITESET_REFRESH_EXIT", 0,
    /* 60 */ "CO_TRANSFER_ENTER", "CO_TRANSFER_EXIT", "CO_TRANSFER_EXIT_ENTER",
    /* 63 */ "CO_TRANSFER_EXIT_RETURN", "CO_INIT", "CO_DESTROY_ENTER", "CO_DESTROY_EXIT", 0, 0, 0,
    /* 70 */ "SCRIPT_EVAL_ENTER", "SCRIPT_EVAL_EXIT", "SCRIPT_FAILED", "MAIN_IDLE",
    /* 74 */ "RUBY_PROBE_INSTALLED",
};

static const char *fzName(uint32_t ph)
{
    return (ph < VITA_FZ_PHASE_COUNT && fzPhaseNames[ph]) ? fzPhaseNames[ph] : "?";
}
#ifdef MKXP_VITA_BLOCK_PROF
/* vita-block-prof.cpp: the phase of the longest wait. */
extern "C" const char *vitaFreezePhaseNow(void) { return fzName(FZ_LOAD(fzPhase)); }
#endif

/* Output buffer and a tiny formatter: no stdio, no malloc. */
static char fzOut[40960];
static size_t fzOutLen;

static void fzS(const char *s)
{
    while (*s && fzOutLen < sizeof(fzOut) - 1)
        fzOut[fzOutLen++] = *s++;
}

static void fzU(uint64_t v)
{
    char t[24];
    int n = 0;
    do { t[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < 24);
    while (n && fzOutLen < sizeof(fzOut) - 1)
        fzOut[fzOutLen++] = t[--n];
}

static void fzI(int64_t v)
{
    if (v < 0) { fzS("-"); fzU((uint64_t)(-v)); }
    else fzU((uint64_t)v);
}

static void fzX(uintptr_t v)
{
    static const char hex[] = "0123456789abcdef";
    fzS("0x");
    for (int s = (int)(sizeof(uintptr_t) * 8) - 4; s >= 0; s -= 4)
        if (fzOutLen < sizeof(fzOut) - 1)
            fzOut[fzOutLen++] = hex[(v >> s) & 0xf];
}

static void fzKV(const char *k, uint64_t v) { fzS(" "); fzS(k); fzS("="); fzU(v); }
static void fzKX(const char *k, uintptr_t v) { fzS(" "); fzS(k); fzS("="); fzX(v); }

static void fzFlush()
{
    if (fzOutLen == 0)
        return;
    SceUID fd = sceIoOpen(FZ_LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        size_t off = 0;
        while (off < fzOutLen) {
            const int w = sceIoWrite(fd, fzOut + off, fzOutLen - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        sceIoClose(fd);
    }
    fzOutLen = 0;
}

static bool fzCoRead(const FzCo &c, FzCo &out)
{
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t s1 = FZ_LOAD(c.seq);
        if (s1 & 1u) continue;
        out = c;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (FZ_LOAD(c.seq) == s1) return true;
    }
    return false;
}

static void fzCoLine(const char *label, const FzCo &c)
{
    FzCo v;
    fzS(label);
    if (!fzCoRead(c, v)) { fzS(" (torn)\n"); return; }
    fzKX("src", v.src); fzKX("dst", v.dst); fzKV("n", v.n); fzKX("tid", v.tid);
    fzKX("ret", v.ret); fzKV("ts_us", v.ts); fzS("\n");
}

static void fzPreviewLine(const char *label, const FzPreview &p, const char *extraName)
{
    fzS(label);
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t s1 = FZ_LOAD(p.seq);
        if (s1 & 1u) continue;
        char text[FZ_PREVIEW];
        memcpy(text, p.text, sizeof(text));
        const uint32_t hash = p.hash, len = p.len, extra = p.extra, frame = p.frame;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (FZ_LOAD(p.seq) != s1) continue;
        text[FZ_PREVIEW - 1] = 0;
        if (s1 == 0) { fzS(" (none)\n"); return; }
        fzKX("hash", hash); fzKV("len", len); fzKV(extraName, extra); fzKV("frame", frame);
        fzS(" text=\""); fzS(text); fzS("\"\n");
        return;
    }
    fzS(" (busy)\n");
}

static void fzHeader(const char *what, uint64_t now, uint64_t stallUs)
{
    fzS("\n===== "); fzS(what); fzS(" =====\n");
    fzS("BUILD " MKXP_VITA_BUILD_TAG "\n");
    fzS("timestamp_us="); fzU(now);
    fzKV("stall_ms", stallUs / 1000);
    fzKV("frame_counter", FZ_LOAD(fzFrame));
    fzS(" current_phase="); fzS(fzName(FZ_LOAD(fzPhase)));
    fzKV("last_frame_ts_us", FZ_LOAD(fzLastFrameTs));
    fzKV("last_marker_ts_us", FZ_LOAD(fzLastMarkTs));
    fzKX("main_tid", fzMainTid);
    fzS("\n");
}

static void fzDump(uint64_t now, uint64_t stallUs)
{
    fzHeader("FREEZE DETECTED", now, stallUs);

    /* Ruby / event interpreter */
    {
        uint32_t m = 0, e = 0, d = 0, i = 0; int32_t c = 0; bool ok = false;
        for (int tries = 0; tries < 4 && !ok; ++tries) {
            const uint32_t s1 = FZ_LOAD(fzIpSeq);
            if (s1 & 1u) continue;
            m = fzIpMap; e = fzIpEvent; d = fzIpDepth; i = fzIpIndex; c = fzIpCode;
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            ok = FZ_LOAD(fzIpSeq) == s1;
        }
        fzS("INTERPRETER"); fzKV("map_id", m); fzKV("event_id", e); fzKV("depth", d);
        fzKV("index", i); fzS(" code="); fzI(c); fzKV("commands_total", FZ_LOAD(fzCmdCount));
        fzKV("resumes", FZ_LOAD(fzInterpResumes)); fzKV("resume_returns", FZ_LOAD(fzInterpReturns));
        if (!ok) fzS(" (torn)");
        fzS("\n");
    }
    fzPreviewLine("LAST_SCRIPT_355", fzScript, "extra_655_lines");
    fzPreviewLine("LAST_MOVE_SCRIPT", fzMoveScript, "char_id");

    if (FZ_LOAD(fzFailSeq) == 2u) {
        fzS("SCRIPT_FAILED index="); fzI(fzFailIndex); fzS("\n"); fzS(fzFailText); fzS("SCRIPT_FAILED_END\n");
    }

    /* Coroutine backend */
    fzS("COROUTINE"); fzKV("transfers", FZ_LOAD(fzCoTransfers)); fzKV("transfer_returns", FZ_LOAD(fzCoReturns));
    fzKV("transfer_exit_enters", FZ_LOAD(fzCoExitEnters)); fzKV("transfer_exit_returns", FZ_LOAD(fzCoExitReturns));
    fzKV("inits", FZ_LOAD(fzCoInits)); fzKV("destroy_enters", FZ_LOAD(fzCoDestroyEnters));
    fzKV("destroy_exits", FZ_LOAD(fzCoDestroyExits));
    {
        struct coroutine_vita_stats st;
        coroutine_vita_get_stats(&st);
        fzKV("lib_created", st.created); fzKV("lib_exited", st.exited);
        fzKV("lib_joined_exited", st.joined_exited); fzKV("lib_joined_suspended", st.joined_suspended);
        fzKV("lib_released_exited", st.released_exited);
    }
    fzS("\n");
    fzCoLine("CO_LAST_TRANSFER_ENTER ", fzCoEnter);
    fzCoLine("CO_LAST_TRANSFER_RETURN", fzCoReturn);
    fzCoLine("CO_LAST_EXIT_ENTER     ", fzCoExitEnter);
    fzFlush();

    /* Event command history, oldest first */
    const uint32_t hh = FZ_LOAD(fzHistHead);
    const uint32_t hn = hh < FZ_HIST_SIZE ? hh : FZ_HIST_SIZE;
    fzS("CMD_HISTORY count="); fzU(hn); fzS(" (oldest first)\n");
    for (uint32_t k = hh - hn; k != hh; ++k) {
        const FzCmd *r = &fzHist[k & (FZ_HIST_SIZE - 1)];
        const uint32_t s1 = FZ_LOAD(r->seq);
        if (s1 != k + 1u) { fzS("  (lost)\n"); continue; }
        FzCmd v = *r;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (FZ_LOAD(r->seq) != s1) { fzS("  (lost)\n"); continue; }
        fzS("  CMD"); fzKV("ts_us", v.ts); fzKV("frame", v.frame); fzKV("map", v.map_id);
        fzKV("ev", v.event_id); fzKV("depth", v.depth); fzKV("index", v.index);
        fzS(" code="); fzI(v.code);
        if (v.script_hash) fzKX("script_hash", v.script_hash);
        fzS("\n");
    }
    fzFlush();

    /* General ring, oldest first */
    const uint32_t head = FZ_LOAD(fzHead);
    const uint32_t n = head < FZ_RING_DUMP ? head : FZ_RING_DUMP;
    fzS("RING count="); fzU(n); fzKV("head", head); fzS(" (oldest first)\n");
    for (uint32_t k = head - n; k != head; ++k) {
        const FzEntry *e = &fzRing[k & (FZ_RING_SIZE - 1)];
        const uint32_t s1 = FZ_LOAD(e->seq);
        if (s1 != k + 1u) { fzS("  (lost)\n"); continue; }
        FzEntry v = *e;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (FZ_LOAD(e->seq) != s1) { fzS("  (lost)\n"); continue; }
        fzS("  "); fzU(v.ts); fzS(" f="); fzU(v.frame); fzS(" "); fzS(fzName(v.phase));
        fzKX("tid", v.tid);
        fzKX("a0", v.a[0]); fzKX("a1", v.a[1]); fzKX("a2", v.a[2]); fzKX("a3", v.a[3]);
        fzS("\n");
        if (fzOutLen > sizeof(fzOut) - 512) fzFlush();
    }
    fzS("===== END =====\n");
    fzFlush();
}

static int fzWatchdog(SceSize args, void *argp)
{
    (void)args; (void)argp;

    fzS("\n===== FREEZE_PROBE_ARMED =====\nBUILD " MKXP_VITA_BUILD_TAG "\n");
    fzS("timestamp_us="); fzU(fzNow()); fzS("\n");
    fzFlush();

    uint32_t lastFrame = FZ_LOAD(fzFrame);
    uint64_t lastChange = fzNow();
    bool inStall = false, stillWritten = false;
    int episodes = 0;

    for (;;) {
        sceKernelDelayThread(FZ_WATCH_PERIOD_US);

        const uint32_t f = FZ_LOAD(fzFrame);
        const uint64_t now = fzNow();

        if (f != lastFrame) {
            if (inStall) {
                fzS("STALL_RECOVERED timestamp_us="); fzU(now);
                fzKV("stall_ms", (now - lastChange) / 1000);
                fzKV("frame_counter", f); fzS("\n");
                fzFlush();
                inStall = false;
            }
            lastFrame = f;
            lastChange = now;
            continue;
        }

        /* Arm only after the first presented frame (startup script loading is not a stall). */
        if (f == 0)
            continue;

        const uint64_t stall = now - lastChange;
        if (!inStall && stall >= FZ_STALL_US && episodes < FZ_MAX_EPISODES) {
            fzDump(now, stall);
            inStall = true;
            stillWritten = false;
            ++episodes;
        } else if (inStall && !stillWritten && stall >= FZ_STILL_US) {
            fzS("STALL_STILL_PRESENT timestamp_us="); fzU(now);
            fzKV("stall_ms", stall / 1000);
            fzS(" phase="); fzS(fzName(FZ_LOAD(fzPhase)));
            fzKV("frame_counter", f); fzS("\n");
            fzFlush();
            stillWritten = true;
        }
    }
    return 0;
}

extern "C" void vitaFreezeProbeStart(void)
{
    fzMainTid = (uint32_t)sceKernelGetThreadId();
    fzLastFrameTs = fzNow();

    /* Higher priority than the main thread so it still runs if main spins; sleeps 250 ms. */
    SceUID th = sceKernelCreateThread("vita_freeze_wd", fzWatchdog, 100, 0x4000, 0,
                                      SCE_KERNEL_CPU_MASK_USER_ALL, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
}
