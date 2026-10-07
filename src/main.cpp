#include <psp2/kernel/processmgr.h>
#ifdef MKXP_VITA_INPUT_V3
/* Before ruby.h: rgss-input.h uses <cstring>, which ruby.h's memcpy macros would break. */
#include "rgss-input.h"
#endif
#include "vita_paths.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <cstring>
#include <ruby.h>
#include <zlib.h>
#include <vector>
#ifdef MKXP_VITA_FREEZE_PROBE
#include <string>
#endif

#include <vitaGL.h>
#include <psp2/kernel/clib.h>
#include "vita_freeze_probe.h"
/* Diagnostic only (MKXP_VITA_TIMELINE / MKXP_VITA_PERF_BITMAP): no code when OFF. */
#include "vita_diag.h"
#ifdef MKXP_VITA_DIAG
extern "C" void vitaDiagRubyInit(void);
#endif
#ifdef MKXP_VITA_DEBUG_BOOT_MAP_ID
#define VITA_DIAG_STR2(x) #x
#define VITA_DIAG_STR(x) VITA_DIAG_STR2(x)
#endif

/* TEST VITA: autopress di :C per attraversare il titolo senza input. */
#define VITA_TEST_AUTOPRESS

#include "gl-fun.h"
#include "config.h"
#include "glstate.h"
#include "sharedstate.h"
void tableBindingInit();
void etcBindingInit();
void spriteBindingInit();
void bitmapBindingInitVitaMinimal();
void windowVXBindingInit();
#ifdef MKXP_VITA_AUDIO
void audioBindingInit();   /* upstream binding/audio-binding.cpp */
#endif
void viewportBindingInit();
void tilemapVXBindingInit();
void planeBindingInit();
void vitaSetGLState(GLState *state);
void vitaRenderFrame();
#ifdef MKXP_VITA_TEX_PAGING
extern "C" void vitaTexPagingTick();
#endif
#ifdef MKXP_VITA_BIG_ALLOC
bool vitaBigAllocInit(unsigned int megabytes);   /* vita-big-alloc.cpp */
#endif
#ifdef MKXP_VITA_RENDER_EXC
#include "exception.h"
void raiseRbExc(Exception *exc);   /* binding-shim.cpp */
#endif

#ifdef MKXP_VITA_HEAP_MB
/*
 * Memory budget (MKXP_VITA_HEAP_MB): size of the newlib heap (default 128 MiB when undefined). vitaGL
 * takes the user memory left at vglInit (minus a 16 MiB threshold) as its RAM pool, so this moves
 * memory between the two. d26 soak: vitaGL RAM pool never below 28 MiB free, newlib heap exhausted
 * (arena 127 MiB, bad_alloc in the PNG decoder).
 */
extern "C" {
int _newlib_heap_size_user = MKXP_VITA_HEAP_MB * 1024 * 1024;
}
#endif
extern const char module_rpg3[];
extern "C" void rb_call_builtin_inits(void);
#ifdef MKXP_VITA_QUIET_BOOT
#include <cstdarg>
#include <cstdio>
#include <png.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#endif
extern "C" {
#include "../common/debugScreen.h"
#ifdef MKXP_VITA_QUIET_BOOT
/*
 * Public builds (MKXP_VITA_QUIET_BOOT): a loading screen instead of the boot messages. The screen is
 * app0:boot/loading.png (960x544, packaged if boot/loading.png exists) or "LISA: The Painful /
 * Loading..." on black. Every boot message is kept in memory and written once to qa.log (BOOT_LOG)
 * when the game scripts start; a failure (a message with FAILED / not found / Could not) brings up
 * the text screen with the whole log, as before, while the boot screen is still the one shown (once
 * the game draws, errors are the error screen's job).
 */
static char gVitaBootLog[16384];
static size_t gVitaBootLen = 0;
static bool gVitaBootVerbose = false, gVitaBootText = false, gVitaBootLogged = false;
static void *gVitaBootFb = nullptr;

static void vitaBootLogToQa(const char *why)
{
    if (gVitaBootLogged)
        return;
    gVitaBootLogged = true;
    if (FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a")) {
        fprintf(f, "BOOT_LOG %s\n%.*sBOOT_LOG end\n", why, (int)gVitaBootLen, gVitaBootLog);
        fclose(f);
    }
}

static bool vitaBootScreenShown()
{
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    return gVitaBootFb && sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0 && fb.base == gVitaBootFb;
}

static void vitaBootCenter(const char *text, int y)
{
    PsvDebugScreenFont *font = psvDebugScreenGetFont();
    int x = (SCREEN_WIDTH - (int)strlen(text) * font->size_w) / 2;
    psvDebugScreenSetCoordsXY(&x, &y);
    psvDebugScreenPuts(text);
}

/* The loading screen (main(), before vglInit). */
static void vitaBootScreenStart()
{
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_file(&img, "app0:boot/loading.png")) {
        img.format = PNG_FORMAT_RGBA;
        const SceUID blk = img.width == SCREEN_WIDTH && img.height == SCREEN_HEIGHT
            ? sceKernelAllocMemBlock("boot_screen", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 2 * 1024 * 1024, NULL) : -1;
        void *base = nullptr;
        if (blk >= 0 && sceKernelGetMemBlockBase(blk, &base) >= 0 &&
            png_image_finish_read(&img, NULL, base, SCREEN_WIDTH * 4, NULL)) {
            SceDisplayFrameBuf fb;
            memset(&fb, 0, sizeof(fb));
            fb.size = sizeof(fb);
            fb.base = base;
            fb.pitch = SCREEN_WIDTH;
            fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
            fb.width = SCREEN_WIDTH;
            fb.height = SCREEN_HEIGHT;
            if (sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0) {
                gVitaBootFb = base;
                return;
            }
        }
        png_image_free(&img);
        if (blk >= 0)
            sceKernelFreeMemBlock(blk);
    }
    psvDebugScreenInit();
    gVitaBootText = true;
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0)
        gVitaBootFb = fb.base;
    PsvDebugScreenFont *small = psvDebugScreenGetFont();
    PsvDebugScreenFont *big = psvDebugScreenScaleFont2x(small);
    if (big)
        psvDebugScreenSetFont(big);
    vitaBootCenter("LISA: The Painful", SCREEN_HEIGHT / 2 - 40);
    psvDebugScreenSetFont(small);
    vitaBootCenter("Loading...", SCREEN_HEIGHT / 2 + 10);
}

static int vitaBootPrintf(const char *format, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    if (gVitaBootVerbose)
        return psvDebugScreenPuts(line);
    const size_t len = strlen(line);
    if (gVitaBootLen + len < sizeof(gVitaBootLog)) {
        memcpy(gVitaBootLog + gVitaBootLen, line, len);
        gVitaBootLen += len;
    }
    if (strstr(line, "FAILED") || strstr(line, "not found") || strstr(line, "Could not")) {
        vitaBootLogToQa("failed");
        if (vitaBootScreenShown()) {
            gVitaBootVerbose = true;
            if (!gVitaBootText)
                psvDebugScreenInit();   /* the text screen replaces the loading image */
            psvDebugScreenPuts("\e[H\e[2J");
            psvDebugScreenPuts("LISA: The Painful - boot log\n\n");
            gVitaBootLog[gVitaBootLen] = 0;
            psvDebugScreenPuts(gVitaBootLog);
        }
    } else if (strstr(line, "Running LISA scripts")) {
        vitaBootLogToQa("ok");
    }
    return n;
}
#define psvDebugScreenPrintf vitaBootPrintf
#endif

static VALUE vita_debug_print(VALUE self, VALUE message)
{
    VALUE str = rb_obj_as_string(message);
    psvDebugScreenPrintf("%s\n", StringValueCStr(str));
    return Qnil;
}

/* TRACE VITA: unico canale su tty0 (sceClibPrintf) per i punti di trace. */
static VALUE vita_trace_print(VALUE self, VALUE message)
{
    VALUE str = rb_obj_as_string(message);
    sceClibPrintf("VITA_TRACE %s\n", StringValueCStr(str));
    return Qnil;
}

}

/*
 * TEST VITA: stampa classe, messaggio e backtrace dell'eccezione
 * Ruby corrente su tty0 (sceClibPrintf, visibile in vita3k.log).
 */
static void vitaLogRubyException(const char *where)
{
    VALUE exc = rb_errinfo();

    if (NIL_P(exc))
        return;

    VALUE cls = rb_class_name(rb_obj_class(exc));
    VALUE msg = rb_funcall(exc, rb_intern("message"), 0);

    sceClibPrintf("VITA_EXC [%s] %s: %s\n",
                  where,
                  StringValueCStr(cls),
                  StringValueCStr(msg));

    VALUE bt = rb_funcall(exc, rb_intern("backtrace"), 0);

    if (RB_TYPE_P(bt, T_ARRAY))
    {
        long n = RARRAY_LEN(bt);

        for (long i = 0; i < n && i < 30; ++i)
        {
            VALUE line = rb_obj_as_string(rb_ary_entry(bt, i));
            sceClibPrintf("VITA_EXC   at %s\n", StringValueCStr(line));
        }
    }
}

#ifdef MKXP_VITA_CPU_444
static void vitaApplyCpuClock(const char *why);
#endif
#ifdef MKXP_VITA_FPS30
#include <vitaGL.h>   /* eglSwapInterval */
static bool vitaFps30 = false;   /* d59: the user prefers 60; L+R+SELECT switches */
extern "C" { unsigned int vitaLogicUpdates = 0; }   /* PERF upd= (vita_diag.cpp) */
#endif
#ifdef MKXP_VITA_GC_MID
extern "C" void ruby_gc_set_params(void);   /* gc.c (not in the public headers) */
extern "C" { int vitaGcTuned = 0; }          /* PERF gc_tune= (vita_diag.cpp): 1 = GC_TUNE_L set, 2 = GC_MID */
#endif
extern "C" {
static VALUE vita_graphics_update(VALUE self)
{
    (void)self;

    VITA_FREEZE_MARK(GRAPHICS_UPDATE_ENTER);
#ifdef MKXP_VITA_FPS30
    /*
     * MKXP_VITA_FPS30: steady 30 fps with the game logic still at 60 steps per second. RGSS games
     * advance one logic step per Graphics.update and are written for 60; showing every other step
     * with a 2-vblank swap interval gives one image every 33.3 ms, and each image has the budget of
     * two steps (d58: busy maps 50-55 fps with drops, which the user found worse than a steady 30).
     * Every Graphics.update still runs (frame_count, input, transitions, fades count steps as before);
     * only the rendering of the odd steps is skipped: the next image shows the current state.
     * L+R+SELECT switches 30 <-> 60 (qa.log FPS_MODE).
     */
    {
        static bool vitaFpsPrev = false, vitaFpsInit = false;
        if (!vitaFpsInit) {
            vitaFpsInit = true;
            eglSwapInterval(0, vitaFps30 ? 2 : 1);
        }
#ifdef MKXP_VITA_NO_DEBUG_KEYS
        const bool on = false;   /* public build: no test key combos (MKXP_VITA_NO_DEBUG_KEYS) */
#else
        SceCtrlData vitaFpsPad;
        const unsigned kCombo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
        const bool on = sceCtrlPeekBufferPositive(0, &vitaFpsPad, 1) > 0 && (vitaFpsPad.buttons & kCombo) == kCombo;
#endif
        if (on && !vitaFpsPrev) {
            vitaFps30 = !vitaFps30;
            eglSwapInterval(0, vitaFps30 ? 2 : 1);
            FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
            if (f) { fprintf(f, "FPS_MODE %d\n", vitaFps30 ? 30 : 60); fclose(f); }
        }
        vitaFpsPrev = on;
        ++vitaLogicUpdates;
        if (vitaFps30 && (vitaLogicUpdates & 1)) {
            VITA_FREEZE_MARK(GRAPHICS_UPDATE_EXIT);
            return Qnil;   /* logic step without an image */
        }
    }
#endif
#if defined(MKXP_VITA_OFFSCREEN_SPRITES) && !defined(MKXP_VITA_NO_DEBUG_KEYS)
    {
        /* L+R+START toggles the off-screen sprite skip (edge-triggered). Test key: not in public
         * builds (MKXP_VITA_NO_DEBUG_KEYS), where L and R are game buttons. */
        static bool vitaOffPrev = false;
        SceCtrlData vitaOffPad;
        const unsigned kCombo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START;
        const bool on = sceCtrlPeekBufferPositive(0, &vitaOffPad, 1) > 0 && (vitaOffPad.buttons & kCombo) == kCombo;
        if (on && !vitaOffPrev) {
            const VALUE cur = rb_gv_get("$vita_offscreen_skip");
            rb_gv_set("$vita_offscreen_skip", RTEST(cur) ? Qfalse : Qtrue);
            rb_gv_set("$vita_event_fast", RTEST(cur) ? Qfalse : Qtrue);   /* MKXP_VITA_EVENT_FAST (nil if absent) */
            rb_gv_set("$vita_sprite_fast", RTEST(cur) ? Qfalse : Qtrue);  /* MKXP_VITA_SPRITE_FAST (nil if absent) */
            rb_gv_set("$vita_region_incr", RTEST(cur) ? Qfalse : Qtrue);  /* MKXP_VITA_REGION_INCR (nil if absent) */
            rb_gv_set("$vita_sprite_native", RTEST(cur) ? Qfalse : Qtrue); /* MKXP_VITA_SPRITE_NATIVE */
            rb_gv_set("$vita_map_refresh_fast", RTEST(cur) ? Qfalse : Qtrue); /* MKXP_VITA_MAP_REFRESH_FAST */
            FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
            if (f) { fprintf(f, "OFFSCREEN_SKIP %s\n", RTEST(cur) ? "OFF" : "ON"); fclose(f); }
        }
        vitaOffPrev = on;
    }
#endif
#ifdef MKXP_VITA_CPU_444
    {
        static unsigned vitaClockFrames = 0;
        if (++vitaClockFrames % 300 == 0)
            vitaApplyCpuClock("reapply");
    }
#endif
#ifdef MKXP_VITA_TEX_PAGING
    vitaTexPagingTick();   /* bitmap-vita.cpp: release idle file-backed textures under GPU memory pressure */
#endif
#ifdef MKXP_VITA_RENDER_EXC
    /*
     * Fix (MKXP_VITA_RENDER_EXC): an mkxp Exception thrown while rendering (e.g. "disposed viewport"
     * from SpritePrivate::prepare -> Viewport::aboutToAccess) escaped as a C++ exception ->
     * std::terminate -> abort (d22/d23). Upstream Graphics.update is an RB_METHOD_GUARD: the
     * exception becomes a Ruby exception raised by Graphics.update.
     */
    Exception *vitaRenderExc = nullptr;
    try {
        vitaRenderFrame();
    } catch (const Exception &e) {
        vitaRenderExc = new Exception(e);
    }
    VITA_FREEZE_MARK(GRAPHICS_UPDATE_EXIT);
#ifdef MKXP_VITA_FREEZE_PROBE
    vitaFreezeFrameDone();
#endif
    if (vitaRenderExc)
        raiseRbExc(vitaRenderExc);
#else
    vitaRenderFrame();
    VITA_FREEZE_MARK(GRAPHICS_UPDATE_EXIT);
#ifdef MKXP_VITA_FREEZE_PROBE
    vitaFreezeFrameDone();
#endif
#endif

    return Qnil;
}
}

#ifdef MKXP_VITA_SCREEN_FX
/* sharedstate-vita.cpp (MKXP_VITA_SCREEN_FX). */
extern "C" void vitaFxSetBrightness(int value);
extern "C" void vitaFxFreeze(void);
extern "C" void vitaFxTransition(float prog);
static VALUE vita_fx_brightness(VALUE self, VALUE v) { (void)self; vitaFxSetBrightness(NUM2INT(v)); return Qnil; }
static VALUE vita_fx_freeze(VALUE self) { (void)self; vitaFxFreeze(); return Qnil; }
static VALUE vita_fx_transition(VALUE self, VALUE prog) { (void)self; vitaFxTransition((float)NUM2DBL(prog)); return Qnil; }
extern "C" bool vitaFxIsFrozen(void);
static VALUE vita_fx_frozen_p(VALUE self) { (void)self; return vitaFxIsFrozen() ? Qtrue : Qfalse; }
#endif

static unsigned int vitaInputCurrent = 0;
static unsigned int vitaInputPrevious = 0;
static int vitaInputHold[32] = {0};

#ifdef MKXP_VITA_INPUT_V2
/*
 * Fix (MKXP_VITA_INPUT_V2): RGSS keys as Symbol *or* Integer (event conditional branches store the
 * button as an Integer: 2/4/6/8 = directions, 11 A, 12 B, 13 C, 14 X, 15 Y, 16 Z, 17 L, 18 R), and
 * a mask of Vita buttons per key. LISA layout (combo keys W/A/S/D = R/X/Y/Z on PC):
 *   Triangle = R        Square = X        Cross = C + Y        Circle = B + Z
 *   L trigger = L + A (dash)              R trigger = R
 */
static unsigned int vitaButtonForRubyKey(VALUE key)
{
    int code = -1;
    if (FIXNUM_P(key)) {
        code = FIX2INT(key);
    } else if (SYMBOL_P(key)) {
        static const struct { const char *name; int code; } names[] = {
            { "DOWN", 2 }, { "LEFT", 4 }, { "RIGHT", 6 }, { "UP", 8 }, { "A", 11 }, { "B", 12 },
            { "C", 13 }, { "X", 14 }, { "Y", 15 }, { "Z", 16 }, { "L", 17 }, { "R", 18 }, { "SHIFT", 21 },
        };
        const ID id = SYM2ID(key);
        for (const auto &n : names)
            if (id == rb_intern(n.name)) { code = n.code; break; }
    }
    switch (code) {
    case 2:  return SCE_CTRL_DOWN;
    case 4:  return SCE_CTRL_LEFT;
    case 6:  return SCE_CTRL_RIGHT;
    case 8:  return SCE_CTRL_UP;
    case 11: return SCE_CTRL_LTRIGGER;                 /* A: dash */
    case 12: return SCE_CTRL_CIRCLE;                   /* B: cancel / menu */
    case 13: return SCE_CTRL_CROSS;                    /* C: confirm */
    case 14: return SCE_CTRL_SQUARE;                   /* X (A key) */
    case 15: return SCE_CTRL_CROSS;                    /* Y (S key) */
    case 16: return SCE_CTRL_CIRCLE;                   /* Z (D key) */
    case 17: return SCE_CTRL_LTRIGGER;                 /* L */
    case 18: return SCE_CTRL_TRIANGLE | SCE_CTRL_RTRIGGER; /* R (W key) */
    case 21: return SCE_CTRL_LTRIGGER;                 /* SHIFT = A */
    default: return 0;
    }
}
#else
static unsigned int vitaButtonForRubyKey(VALUE key)
{
    if (!SYMBOL_P(key))
        return 0;

    ID id = SYM2ID(key);

    if (id == rb_intern("UP"))
        return SCE_CTRL_UP;

    if (id == rb_intern("DOWN"))
        return SCE_CTRL_DOWN;

    if (id == rb_intern("LEFT"))
        return SCE_CTRL_LEFT;

    if (id == rb_intern("RIGHT"))
        return SCE_CTRL_RIGHT;

    /*
     * RGSS:
     * C = conferma
     * B = annulla
     */
    if (id == rb_intern("C"))
        return SCE_CTRL_CROSS;

    if (id == rb_intern("B"))
        return SCE_CTRL_CIRCLE;

    /*
     * Mapping iniziale degli altri pulsanti RGSS.
     */
    if (id == rb_intern("A"))
        return SCE_CTRL_SQUARE;

    if (id == rb_intern("X"))
        return SCE_CTRL_TRIANGLE;

    if (id == rb_intern("L"))
        return SCE_CTRL_LTRIGGER;

    if (id == rb_intern("R"))
        return SCE_CTRL_RTRIGGER;

    return 0;
}
#endif

static int vitaButtonIndex(unsigned int button)
{
    if (button == 0)
        return -1;

    for (int i = 0; i < 32; ++i)
    {
        if (button == (1u << i))
            return i;
    }

    return -1;
}

#ifdef MKXP_VITA_INPUT_V3
/*
 * RGSS3 Input (MKXP_VITA_INPUT_V3): mkxp-z semantics in rgss-input.h (per-button states, single
 * repeating button 23/6 frames, dir4 priority + dead combos, dir8 combos); this file only reads the
 * pad and holds the Vita binding table.
 */
static RgssInput::Input vitaRgssInput(60);
#ifdef MKXP_VITA_ANALOG_INPUT
#include "vita-analog.h"
static VitaAnalog::State vitaAnalogState;
#endif

/* Vita -> RGSS bindings (LISA layout: combo keys W/A/S/D = R/X/Y/Z on PC). Directions first: the
 * table order decides which newly pressed button becomes the repeating one (as upstream). */
static void vitaInputInitBindings()
{
    using namespace RgssInput;
    vitaRgssInput.setBindings({
        { SCE_CTRL_DOWN, Down }, { SCE_CTRL_LEFT, Left }, { SCE_CTRL_RIGHT, Right }, { SCE_CTRL_UP, Up },
        { SCE_CTRL_CROSS, C }, { SCE_CTRL_CROSS, Y },          /* confirm / S */
        { SCE_CTRL_CIRCLE, B }, { SCE_CTRL_CIRCLE, Z },        /* cancel / D */
        { SCE_CTRL_SQUARE, X },                                /* A key */
        { SCE_CTRL_TRIANGLE, R }, { SCE_CTRL_RTRIGGER, R },    /* W key */
        { SCE_CTRL_LTRIGGER, L }, { SCE_CTRL_LTRIGGER, A }, { SCE_CTRL_LTRIGGER, Shift },  /* dash */
#ifdef MKXP_VITA_ANALOG_INPUT
        /* right stick (vita-analog.h): the combo keys only, never C / B */
        { VitaAnalog::kRightUp, R }, { VitaAnalog::kRightLeft, X },
        { VitaAnalog::kRightDown, Y }, { VitaAnalog::kRightRight, Z },
#endif
    });
}

static int vitaRgssCode(VALUE key)
{
    if (FIXNUM_P(key))
        return FIX2INT(key);
    if (SYMBOL_P(key))
        return RgssInput::codeForName(rb_id2name(SYM2ID(key)));
    return RgssInput::None;
}

static VALUE vita_input_update(VALUE self)
{
    (void)self;
    VITA_FREEZE_MARK(INPUT_UPDATE_ENTER);
    static bool bindingsReady = false;
    if (!bindingsReady) {
        vitaInputInitBindings();
        bindingsReady = true;
    }
    SceCtrlData pad;
    std::memset(&pad, 0, sizeof(pad));
#ifdef MKXP_VITA_ANALOG_INPUT
    /* Feature (MKXP_VITA_ANALOG_INPUT): left stick = D-pad, right stick = combo keys (vita-analog.h).
     * The pad is sampled in SCE_CTRL_MODE_ANALOG since boot. */
    unsigned int buttons = 0;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0)
        buttons = pad.buttons | VitaAnalog::update(vitaAnalogState, pad.lx, pad.ly, pad.rx, pad.ry);
#else
    const unsigned int buttons = sceCtrlPeekBufferPositive(0, &pad, 1) > 0 ? pad.buttons : 0;
#endif
    vitaRgssInput.update(buttons);
    VITA_FREEZE_MARK(INPUT_UPDATE_EXIT);
    return Qnil;
}

static VALUE vita_input_press(VALUE self, VALUE key)
{
    (void)self;
    return vitaRgssInput.isPressed(vitaRgssCode(key)) ? Qtrue : Qfalse;
}

static VALUE vita_input_trigger(VALUE self, VALUE key)
{
    (void)self;
    return vitaRgssInput.isTriggered(vitaRgssCode(key)) ? Qtrue : Qfalse;
}

static VALUE vita_input_repeat(VALUE self, VALUE key)
{
    (void)self;
    return vitaRgssInput.isRepeated(vitaRgssCode(key)) ? Qtrue : Qfalse;
}

static VALUE vita_input_dir4(VALUE self)
{
    (void)self;
    return INT2NUM(vitaRgssInput.dir4());
}

static VALUE vita_input_dir8(VALUE self)
{
    (void)self;
    return INT2NUM(vitaRgssInput.dir8());
}
#else
static VALUE vita_input_update(VALUE self)
{
    (void)self;

    VITA_FREEZE_MARK(INPUT_UPDATE_ENTER);
    vitaInputPrevious = vitaInputCurrent;

    SceCtrlData pad;
    std::memset(&pad, 0, sizeof(pad));

    int result =
        sceCtrlPeekBufferPositive(
            0,
            &pad,
            1
        );

    if (result > 0)
        vitaInputCurrent = pad.buttons;
    else
        vitaInputCurrent = 0;

#ifndef MKXP_VITA_INPUT_V2
/* (INPUT_V2 drops these per-frame debug-screen prints: CPU cost while a key is held.) */
if (vitaInputCurrent & SCE_CTRL_DOWN)
    psvDebugScreenPrintf("INPUT: DOWN\n");

if (vitaInputCurrent & SCE_CTRL_UP)
    psvDebugScreenPrintf("INPUT: UP\n");

if (vitaInputCurrent & SCE_CTRL_CROSS)
    psvDebugScreenPrintf("INPUT: CROSS\n");
#endif

    /*
     * Conta per quanti frame ogni pulsante
     * viene tenuto premuto.
     */
    for (int i = 0; i < 32; ++i)
    {
        unsigned int mask =
            1u << i;

        if (vitaInputCurrent & mask)
        {
            if (vitaInputPrevious & mask)
                ++vitaInputHold[i];
            else
                vitaInputHold[i] = 1;
        }
        else
        {
            vitaInputHold[i] = 0;
        }
    }

    VITA_FREEZE_MARK(INPUT_UPDATE_EXIT);
    return Qnil;
}

static VALUE vita_input_press(VALUE self, VALUE key)
{
    (void)self;

    unsigned int button =
        vitaButtonForRubyKey(key);

    if (button == 0)
        return Qfalse;

    return
        (vitaInputCurrent & button)
        ? Qtrue
        : Qfalse;
}

static VALUE vita_input_trigger(VALUE self, VALUE key)
{
    (void)self;

    unsigned int button =
        vitaButtonForRubyKey(key);

    if (button == 0)
        return Qfalse;

#ifdef MKXP_VITA_INPUT_V2
    /* Any button of the key's mask pressed this frame. */
    return (vitaInputCurrent & ~vitaInputPrevious & button) ? Qtrue : Qfalse;
#else
    bool now =
        (vitaInputCurrent & button) != 0;

    bool before =
        (vitaInputPrevious & button) != 0;

    return
        (now && !before)
        ? Qtrue
        : Qfalse;
#endif
}

static VALUE vita_input_repeat(VALUE self, VALUE key)
{
    (void)self;

    unsigned int button =
        vitaButtonForRubyKey(key);

#ifdef MKXP_VITA_INPUT_V2
    /* Masks with several buttons: repeat on the button held the longest. */
    int index = -1;
    for (int i = 0; i < 32; ++i)
        if ((button & (1u << i)) && (index < 0 || vitaInputHold[i] > vitaInputHold[index]))
            index = i;
#else
    int index =
        vitaButtonIndex(button);
#endif

    if (index < 0)
        return Qfalse;

    int frames =
        vitaInputHold[index];

    /*
     * Comportamento simile a RGSS:
     *
     * - primo frame: true
     * - poi pausa
     * - dopo 24 frame ripete ogni 6 frame
     */
    if (frames == 1)
        return Qtrue;

    if (frames >= 24 &&
        ((frames - 24) % 6) == 0)
    {
        return Qtrue;
    }

    return Qfalse;
}

static VALUE vita_input_dir4(VALUE self)
{
    (void)self;

    if (vitaInputCurrent & SCE_CTRL_DOWN)
        return INT2NUM(2);

    if (vitaInputCurrent & SCE_CTRL_LEFT)
        return INT2NUM(4);

    if (vitaInputCurrent & SCE_CTRL_RIGHT)
        return INT2NUM(6);

    if (vitaInputCurrent & SCE_CTRL_UP)
        return INT2NUM(8);

    return INT2NUM(0);
}

static VALUE vita_input_dir8(VALUE self)
{
    /*
     * Per ora ci basta lo stesso comportamento di dir4.
     * Lo miglioreremo quando serviranno le diagonali.
     */
    return vita_input_dir4(self);
}
#endif /* MKXP_VITA_INPUT_V3 */

#ifdef MKXP_VITA_DEBUG_SOAK
/* Soak (debug): keep the console awake during unattended runs (no automatic standby / screen off). */
static VALUE vita_power_tick_rb(VALUE)
{
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    return Qnil;
}
#endif
#ifdef MKXP_VITA_HEAP_LEDGER
/* Diagnostic only: newlib heap per-caller ledger (vita-heap-ledger.cpp). */
extern "C" void vitaHeapLedgerInit();
extern "C" void vitaHeapLedgerDump(const char *reason);
static VALUE vita_heap_ledger_dump_rb(VALUE, VALUE reason)
{
    vitaHeapLedgerDump(StringValueCStr(reason));
    return Qnil;
}
#endif
#ifdef MKXP_VITA_GL_LEDGER
/* Diagnostic only: vita_gl_ledger_dump(reason) -> gl_ledger.log (gl-fun-vita.cpp). */
extern "C" void vitaGlLedgerDump(const char *reason);
static VALUE vita_gl_ledger_dump_rb(VALUE, VALUE reason)
{
    vitaGlLedgerDump(StringValueCStr(reason));
    return Qnil;
}
#endif

#ifdef MKXP_VITA_RUBY_PIPE_LOG
/*
 * Called by libruby v5 (thread_pthread.c, vita_comm_pipe_repair) when a Ruby communication pipe
 * (a TCP loopback socketpair in vitasdk newlib) was found broken, typically after suspend/resume,
 * and replaced. One line per repair in ruby_pipe.log. Timer thread / signal path: plain stdio.
 */
extern "C" void rb_vita_comm_pipe_event(const char *what, int fd, int err, int new_rd, int new_wr)
{
    FILE *f = fopen(VITA_GAME_ROOT "ruby_pipe.log", "a");
    if (!f)
        return;
    fprintf(f, "COMM_PIPE_REPAIR t_us=%llu op=%s fd=%d errno=%d new=%d,%d\n",
            (unsigned long long)sceKernelGetProcessTimeWide(), what, fd, err, new_rd, new_wr);
    fclose(f);
}
#endif

#ifdef MKXP_VITA_CPU_444
#include <psp2/power.h>
/*
 * Perf fix (MKXP_VITA_CPU_444): the ARM cores at 444 MHz instead of the 333 MHz an application gets
 * by default (the frame time is dominated by Ruby on the CPU: d35/d36 PERF). Applied at boot and
 * re-applied if the system lowered it (after standby/resume), checked every 300 frames.
 */
static void vitaApplyCpuClock(const char *why)
{
    if (scePowerGetArmClockFrequency() >= 444)
        return;
    const int r = scePowerSetArmClockFrequency(444);
    FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) {
        fprintf(f, "CPU_CLOCK %s set=0x%x arm=%d bus=%d gpu=%d\n", why, (unsigned)r, scePowerGetArmClockFrequency(),
                scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency());
        fclose(f);
    }
}
#endif

#ifdef MKXP_VITA_CORE0_MAIN
extern "C" void vitaPinMainThread(void);   /* vita-thread-affinity.cpp */
#endif
#ifdef MKXP_VITA_MOG_CAP
#include <vitaGL.h>   /* vglMemFree */
/* vita_gpu_free_kb: free memory left in the vitaGL pools textures use (VRAM, RAM, PHYCONT). */
static VALUE vita_gpu_free_kb_rb(VALUE)
{
    const size_t f = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM) + vglMemFree(VGL_MEM_PHYCONT);
    return LONG2NUM((long)(f / 1024));
}
#endif
#ifdef MKXP_VITA_SCENE_FLUSH
extern "C" void vitaSceneGpuFlush(void);   /* gl-fun-vita.cpp */
static VALUE vita_scene_gpu_flush_rb(VALUE) { vitaSceneGpuFlush(); return Qnil; }
#endif
#if defined(MKXP_VITA_SAVE_PROF) || defined(MKXP_VITA_MAP_PROF)
/* vita_thread_cpu_ms: CPU time run by the calling thread (sceKernelGetThreadInfo.runClocks, us). */
static VALUE vita_thread_cpu_ms_rb(VALUE)
{
    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) != 0)
        return DBL2NUM(0.0);
    return DBL2NUM((double)ti.runClocks / 1000.0);
}
#endif
#ifdef MKXP_VITA_SPRITE_NATIVE
extern "C" void vitaSpriteNativeInit(void);   /* vita-sprite-native.cpp */
#endif
#ifdef MKXP_VITA_EVENT_NATIVE
extern "C" void vitaEventNativeInit(void);   /* vita-event-native.cpp */
#ifdef MKXP_VITA_ZLIB
extern "C" void Init_zlib(void);   /* libruby-static.a: ext/zlib */
#endif
#ifdef MKXP_VITA_PERF_LITE
/*
 * Release monitoring (MKXP_VITA_PERF_LITE): appended to each PERF line (sharedstate-vita.cpp, once
 * per window, main thread inside Graphics.update): current map, in battle, Ruby GCs of the window
 * (minor/major) and the heap (live slots, pages). A few C calls every 2 s, no Ruby code.
 */
#ifdef MKXP_VITA_CPU_PAGING
#include <malloc.h>
extern "C" void vitaBigAllocStats(unsigned *usedKb, unsigned *freeKb, unsigned *blocks, unsigned *fallbacks);   /* vita-big-alloc.cpp */
extern "C" void vitaCpuPagingStats(unsigned *dropped, unsigned *droppedKb);   /* bitmap-vita.cpp */
#endif
extern "C" int vitaPerfLiteAppend(char *buf, int cap)
{
    static ID idMinor, idMajor, idLive, idPages, idMapId, idInBattle;
    static size_t pMinor = 0, pMajor = 0;
    if (!idMinor) {
        idMinor = rb_intern("minor_gc_count"); idMajor = rb_intern("major_gc_count");
        idLive = rb_intern("heap_live_slots"); idPages = rb_intern("heap_allocated_pages");
        idMapId = rb_intern("@map_id"); idInBattle = rb_intern("@in_battle");
    }
    const VALUE gm = rb_gv_get("$game_map"), gp = rb_gv_get("$game_party");
    const VALUE mid = NIL_P(gm) || SPECIAL_CONST_P(gm) ? Qnil : rb_ivar_get(gm, idMapId);
    const bool battle = !NIL_P(gp) && !SPECIAL_CONST_P(gp) && RTEST(rb_ivar_get(gp, idInBattle));
    const size_t mi = rb_gc_stat(ID2SYM(idMinor)), ma = rb_gc_stat(ID2SYM(idMajor));
    const int n = std::snprintf(buf, cap, " map=%ld battle=%d gc_minor=%u gc_major=%u live=%u pages=%u",
                                FIXNUM_P(mid) ? FIX2LONG(mid) : 0L, battle ? 1 : 0, (unsigned)(mi - pMinor), (unsigned)(ma - pMajor),
                                (unsigned)rb_gc_stat(ID2SYM(idLive)), (unsigned)rb_gc_stat(ID2SYM(idPages)));
    pMinor = mi; pMajor = ma;
#ifdef MKXP_VITA_CPU_PAGING
    /* MKXP_VITA_CPU_PAGING: big pool free, newlib heap in use / free inside the arena, clean CPU copies given back (count/KiB). */
    if (n >= 0 && n < cap) {
        unsigned bu = 0, bf = 0, bb = 0, bfb = 0, cd = 0, ck = 0;
        vitaBigAllocStats(&bu, &bf, &bb, &bfb);
        vitaCpuPagingStats(&cd, &ck);
        const struct mallinfo m = mallinfo();
        const int k = std::snprintf(buf + n, cap - n, " big_free_kb=%u heap_kb=%u/%u cpu_drop=%u/%u", bf,
                                    (unsigned)(m.uordblks / 1024), (unsigned)(m.fordblks / 1024), cd, ck);
        if (k > 0)
            return n + k < cap ? n + k : cap - 1;
    }
#endif
    return n < 0 ? 0 : (n < cap ? n : cap - 1);
}
#endif
#ifdef MKXP_VITA_ANIM_PREFETCH
extern "C" void vitaAnimPrefetchInit(void);   /* bitmap-vita.cpp */
#endif
#endif
#ifdef MKXP_VITA_OBJ_HIST
/*
 * vita_heap_census (MKXP_VITA_OBJ_HIST): every heap slot by internal type, including what
 * ObjectSpace.each_object(Object) does not see (d67: live slots grew ~2 per frame while the visible
 * classes stayed flat): IMEMO by subtype, T_DATA by typed-data name (or class), hidden (class-less)
 * strings/arrays/hashes/objects, zombies (freed objects waiting for their finalizer). Counting only:
 * nothing is allocated during the heap walk.
 */
#include <algorithm>
extern "C" void rb_objspace_each_objects(int (*callback)(void *, void *, size_t, void *), void *data);
namespace {
struct VitaCensus
{
    unsigned type[32];
    unsigned imemo[16];
    unsigned hidden[32];
    struct Key { const void *key; bool typed; unsigned n; } data[48];
    unsigned ndata, dataOther;
    /* d75: method id of each callinfo / callcache (callcache: with a sample receiver class) */
    struct Site { VALUE mid; VALUE klass; unsigned n; } ci[64], cc[64];
    unsigned nci, ncc, ciOther, ccOther;
};
void vitaCensusSite(VitaCensus::Site *t, unsigned &n, unsigned &other, VALUE mid, VALUE klass)
{
    unsigned i = 0;   /* grouped by method id; klass = the first receiver class seen (a sample) */
    while (i < n && t[i].mid != mid)
        ++i;
    if (i == n) {
        if (n == 64) { ++other; return; }
        t[n++] = VitaCensus::Site{ mid, klass, 0 };
    }
    t[i].n++;
}
VitaCensus gVitaCensus;

int vitaCensusPage(void *vstart, void *vend, size_t stride, void *)
{
    VitaCensus &c = gVitaCensus;
    for (char *p = (char *)vstart; p < (char *)vend; p += stride) {
        const VALUE v = (VALUE)p;
        const VALUE flags = RBASIC(v)->flags;
        if (!flags)
            continue;   /* free slot */
        const int t = (int)(flags & RUBY_T_MASK);
        c.type[t]++;
        if (t == RUBY_T_IMEMO) {
            const unsigned it = (unsigned)((flags >> RUBY_FL_USHIFT) & 0x0f);
            c.imemo[it]++;
            const VALUE *f = (const VALUE *)v;
            if (it == 11) {                        /* imemo_callinfo: flags, kwarg, mid, flag, argc */
                vitaCensusSite(c.ci, c.nci, c.ciOther, f[2], 0);
            } else if (it == 12) {                 /* imemo_callcache: flags, klass, cme_, call_ */
                const VALUE cme = f[2];
                VALUE mid = 0;
                if (cme && !SPECIAL_CONST_P(cme)) {
                    const VALUE mf = RBASIC(cme)->flags;
                    if ((mf & RUBY_T_MASK) == RUBY_T_IMEMO && ((mf >> RUBY_FL_USHIFT) & 0x0f) == 6)   /* imemo_ment */
                        mid = ((const VALUE *)cme)[3];   /* called_id */
                }
                vitaCensusSite(c.cc, c.ncc, c.ccOther, mid, f[1]);
            }
        }
        else if (t != RUBY_T_ZOMBIE && t != RUBY_T_MOVED && t != RUBY_T_NODE && !RBASIC(v)->klass)
            c.hidden[t]++;
        if (t == RUBY_T_DATA) {
            const bool typed = RTYPEDDATA_P(v);
            const void *key = typed ? (const void *)RTYPEDDATA_TYPE(v)->wrap_struct_name : (const void *)RBASIC(v)->klass;
            unsigned i = 0;
            while (i < c.ndata && (c.data[i].key != key || c.data[i].typed != typed))
                ++i;
            if (i == c.ndata) {
                if (c.ndata == sizeof(c.data) / sizeof(c.data[0])) {
                    c.dataOther++;
                    continue;
                }
                c.data[c.ndata++] = { key, typed, 0 };
            }
            c.data[i].n++;
        }
    }
    return 0;
}
}

static VALUE vita_heap_census_rb(VALUE)
{
    static const char *const kTypes[32] = {
        "none", "object", "class", "module", "float", "string", "regexp", "array", "hash", "struct", "bignum",
        "file", "data", "match", "complex", "rational", "0x10", "nil", "true", "false", "symbol", "fixnum",
        "undef", "0x17", "0x18", "0x19", "imemo", "node", "iclass", "zombie", "moved", "0x1f" };
    static const char *const kImemo[16] = {
        "env", "cref", "svar", "throw_data", "ifunc", "memo", "ment", "iseq", "tmpbuf", "ast",
        "parser_strterm", "callinfo", "callcache", "constcache", "i14", "i15" };
    std::memset(&gVitaCensus, 0, sizeof(gVitaCensus));
    rb_objspace_each_objects(vitaCensusPage, nullptr);
    const VitaCensus &c = gVitaCensus;
    std::string out = "types";
    char b[160];
    for (int t = 0; t < 32; ++t)
        if (c.type[t]) { snprintf(b, sizeof(b), " %s=%u", kTypes[t], c.type[t]); out += b; }
    out += "\n  imemo";
    for (int i = 0; i < 16; ++i)
        if (c.imemo[i]) { snprintf(b, sizeof(b), " %s=%u", kImemo[i], c.imemo[i]); out += b; }
    out += "\n  hidden";
    for (int t = 0; t < 32; ++t)
        if (c.hidden[t]) { snprintf(b, sizeof(b), " %s=%u", kTypes[t], c.hidden[t]); out += b; }
    out += "\n  data";
    unsigned order[48];
    for (unsigned i = 0; i < c.ndata; ++i)
        order[i] = i;
    std::sort(order, order + c.ndata, [&](unsigned x, unsigned y) { return c.data[x].n > c.data[y].n; });
    for (unsigned k = 0; k < c.ndata && k < 24; ++k) {
        const VitaCensus::Key &d = c.data[order[k]];
        const char *name = d.typed ? (const char *)d.key : (d.key ? rb_class2name((VALUE)d.key) : "(hidden)");
        snprintf(b, sizeof(b), " %s%s=%u", d.typed ? "" : "untyped:", name ? name : "?", d.n);
        out += b;
    }
    if (c.dataOther) { snprintf(b, sizeof(b), " other=%u", c.dataOther); out += b; }
    for (int pass = 0; pass < 2; ++pass) {
        const VitaCensus::Site *t = pass ? c.cc : c.ci;
        const unsigned n = pass ? c.ncc : c.nci;
        unsigned ord[64];
        for (unsigned i = 0; i < n; ++i)
            ord[i] = i;
        std::sort(ord, ord + n, [&](unsigned x, unsigned y) { return t[x].n > t[y].n; });
        out += pass ? "\n  callcache" : "\n  callinfo";
        for (unsigned k = 0; k < n && k < 8; ++k) {
            const VitaCensus::Site &e = t[ord[k]];
            const char *name = e.mid ? rb_id2name((ID)e.mid) : nullptr;
            std::string kn = "-";
            if (e.klass && !SPECIAL_CONST_P(e.klass)) {
                const int kt = (int)(RBASIC(e.klass)->flags & RUBY_T_MASK);
                if (kt == RUBY_T_CLASS || kt == RUBY_T_MODULE) {
                    kn = rb_class2name(e.klass);
                    if (RBASIC(e.klass)->flags & RUBY_FL_SINGLETON) kn += "(singleton)";
                } else if (kt == RUBY_T_ICLASS) {
                    kn = "iclass";
                }
            }
            snprintf(b, sizeof(b), " %s%s%s=%u", name ? name : "?", pass ? "@" : "", pass ? kn.c_str() : "", e.n);
            out += b;
        }
        const unsigned other = pass ? c.ccOther : c.ciOther;
        if (other) { snprintf(b, sizeof(b), " other=%u", other); out += b; }
    }
    return rb_str_new(out.data(), (long)out.size());
}
#endif
#ifdef MKXP_VITA_ERROR_SCREEN
/*
 * Fix (MKXP_VITA_ERROR_SCREEN): how the game ends, as in RGSS3.
 *  - The game scripts return (title "Shutdown", SceneManager.exit) or call exit (SystemExit): the
 *    app closes. Before, it stayed on the last frame forever.
 *  - An exception escapes the scripts: RGSS3 shows it in a message box and closes. Here a screen
 *    over the game shows class, message and backtrace (also appended to qa.log) until X or O is
 *    pressed, then the app closes. Before, the game stopped silently on the last frame.
 */
static VALUE vitaFailedExc = Qnil;      /* exception that ended the scripts (registered with the GC) */
static bool vitaScriptsRan = false;     /* the game scripts were evaluated (else: keep the old idle) */
extern "C" __attribute__((weak)) void vitaVglLogFlush(void);   /* gl-fun-vita.cpp (VGL_LOG_BUFFER) */

static void vitaExitApp(int code)
{
    if (vitaVglLogFlush)
        vitaVglLogFlush();
    fflush(nullptr);
    sceKernelExitProcess(code);
}

/* Shown with the game's own Graphics/Bitmap/Input. If that fails too, the X/O wait is done here. */
static void vitaShowErrorScreen(VALUE exc)
{
    rb_gv_set("$vita_error", exc);
    int state = 0;
    VALUE shown = rb_eval_string_protect(
        "begin\n"
        "  e = $vita_error\n"
        "  msg = e.message.to_s\n"
        "  bt = (e.backtrace || []).first(12)\n"
        "  File.open('" VITA_GAME_ROOT "qa.log', 'a') do |f|\n"
        "    f.puts \"RUBY_ERROR #{e.class}: #{msg.gsub(\"\\n\", ' ')}\"\n"
        "    bt.each { |l| f.puts \"  @ #{l}\" }\n"
        "  end rescue nil\n"
        "  Graphics.transition(0) rescue nil\n"
        "  Graphics.brightness = 255 rescue nil\n"
        "  vp = Viewport.new(0, 0, Graphics.width, Graphics.height)\n"
        "  vp.z = 1_000_000\n"
        "  b = Bitmap.new(Graphics.width, Graphics.height)\n"
        "  b.fill_rect(b.rect, Color.new(0, 0, 0, 235))\n"
        "  b.font.size = 16\n"
        "  b.font.bold = false\n"
        "  b.font.italic = false\n"
        "  b.font.shadow = false\n"
        "  b.font.outline = false\n"
        "  y = 8\n"
        "  put = lambda do |text, color|\n"
        "    b.font.color = color\n"
        "    rest = text.to_s\n"
        "    rest = ' ' if rest.empty?\n"
        "    until rest.empty? || y > b.height - 20\n"
        "      n = rest.size\n"
        "      n -= 1 while n > 1 && b.text_size(rest[0, n]).width > b.width - 16\n"
        "      b.draw_text(8, y, b.width - 16, 18, rest[0, n])\n"
        "      rest = rest[n..-1].to_s\n"
        "      y += 18\n"
        "    end\n"
        "  end\n"
        "  white = Color.new(255, 255, 255)\n"
        "  gray = Color.new(176, 176, 176)\n"
        "  put.call('The game stopped because of a script error.', Color.new(255, 128, 128))\n"
        "  put.call('', white)\n"
        "  put.call(\"#{e.class}\", white)\n"
        "  msg.split(\"\\n\").first(4).each { |l| put.call(l, white) }\n"
        "  put.call('', white)\n"
        "  bt.each { |l| put.call(l, gray) }\n"
        "  y = b.height - 26\n"
        "  put.call('Saved in qa.log. Press X or O to quit.', Color.new(255, 255, 160))\n"
        "  s = Sprite.new(vp)\n"
        "  s.bitmap = b\n"
        "  frames = 0\n"
        "  loop do\n"
        "    Graphics.update\n"
        "    Input.update\n"
        "    frames += 1\n"
        "    break if frames > 60 && (Input.trigger?(:C) || Input.trigger?(:B))\n"
        "  end\n"
        "  true\n"
        "rescue Exception => x\n"
        "  File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"ERROR_SCREEN_FAILED #{x.class}: #{x.message}\" } rescue nil\n"
        "  false\n"
        "end\n",
        &state);
    if (state || shown != Qtrue) {
        rb_set_errinfo(Qnil);
        /* Fallback: the last frame stays on screen; wait for X or O (released first). */
        SceCtrlData pad;
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
        bool released = false;
        for (;;) {
            memset(&pad, 0, sizeof(pad));
            sceCtrlPeekBufferPositive(0, &pad, 1);
            const bool down = (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) != 0;
            if (!down)
                released = true;
            else if (released)
                break;
            sceKernelDelayThread(16000);
        }
    }
}
#endif
int main()
{
#ifdef MKXP_VITA_CORE0_MAIN
    vitaPinMainThread();
#endif
#ifdef MKXP_VITA_CPU_444
    vitaApplyCpuClock("boot");
#endif
#ifdef MKXP_VITA_HEAP_LEDGER
    vitaHeapLedgerInit();   /* diagnostic: from here on the malloc wrappers lock */
#endif
    sceClibPrintf("VITA_TRACE main start\n");
#ifdef MKXP_VITA_FREEZE_PROBE
    vitaFreezeProbeStart();
#endif
    VITA_DIAG_MARK("main_enter", nullptr);

#ifdef MKXP_VITA_QUIET_BOOT
    vitaBootScreenStart();
    psvDebugScreenPrintf("LISA-Vita boot\n\n");
#else
    psvDebugScreenInit();

    psvDebugScreenPrintf("mkxp-z Vita minimal\n");
    psvDebugScreenPrintf("===================\n\n");
#endif

    /*
     * Renderer mkxp-z / vitaGL
     */
    psvDebugScreenPrintf("Initializing vitaGL...\n");

#ifdef MKXP_VITA_BIG_ALLOC
    /* Large C++ blocks pool (vita-big-alloc.cpp): must be created before vglInit, which gives vitaGL
     * the user memory left. */
    {
        const bool vitaBigOk = vitaBigAllocInit(MKXP_VITA_BIG_ALLOC_MB);
        FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
        if (f) {
            fprintf(f, "BIG_ALLOC_INIT %s size_mb=%d\n", vitaBigOk ? "OK" : "FAILED", MKXP_VITA_BIG_ALLOC_MB);
            fclose(f);
        }
    }
#endif
#ifdef MKXP_VITA_NO_MSAA
    /*
     * Fix (MKXP_VITA_NO_MSAA): vglInit() = vglInitExtended(pool, 960, 544, 0x1000000, SCE_GXM_MULTISAMPLE_4X).
     * With 4x MSAA vitaGL gives every framebuffer a 4-sample depth buffer on its first clear: 32 MiB for
     * the 1024x2048 tile atlas, 4.4 MiB per 544x416 window. When the pools are full these land in the
     * newlib heap (d22: BIGALLOC_BY fn=Clear 33554432, pool=4) and exhaust it (bad_alloc in the PNG
     * loader, missing tiles). mkxp-z renders 2D without MSAA upstream: same call, no multisampling.
     */
    GLboolean vglResult = vglInitExtended(8 * 1024 * 1024, 960, 544, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);
#else
    GLboolean vglResult = vglInit(8 * 1024 * 1024);
#endif

    psvDebugScreenPrintf(
        "vglInit returned %d\n",
        vglResult
    );

    psvDebugScreenPrintf("Initializing GLFunctions...\n");
    initGLFunctions();
    psvDebugScreenPrintf("GLFunctions OK\n");

    /*
     * GLState: same configuration as the rendering shell
     * (src/shell/sharedstate-vita.cpp).
     */
    Config conf;
    conf.defScreenW = 960;
    conf.defScreenH = 544;
    conf.maxTextureSize = 0;

    GLState vitaGLState(conf);

    vitaSetGLState(&vitaGLState);

    psvDebugScreenPrintf("GLState OK\n");

    /*
     * SharedState minimale.
     * Deve esistere PRIMA che Ruby possa fare Sprite.new.
     */
    SharedState vitaSharedState;

    psvDebugScreenPrintf("SharedState OK\n");

sceCtrlSetSamplingMode(
    SCE_CTRL_MODE_ANALOG
);

    /*
     * Da qui parte Ruby / LISA
     */
    RUBY_INIT_STACK;

    int result = ruby_setup();
#ifdef MKXP_VITA_AUDIT_FIXES
    /* RGSS3 exception classes (binding-shim.cpp), defined before any script as upstream does. */
    {
        VALUE vitaRgssErrorClass();
        vitaRgssErrorClass();
    }
#endif

    sceClibPrintf("VITA_TRACE after ruby_setup result=%d\n", result);
    VITA_DIAG_MARK("ruby_setup_done", nullptr);
#ifdef MKXP_VITA_SPRITE_NATIVE
    vitaSpriteNativeInit();
#endif
#ifdef MKXP_VITA_EVENT_NATIVE
    vitaEventNativeInit();
#endif
#ifdef MKXP_VITA_ZLIB
    /*
     * Fix (MKXP_VITA_ZLIB): RGSS3 has the Zlib module; libruby-static.a contains the extension
     * (zlib.o) but nothing initialized it, so Zlib was an undefined constant here. d88: LISA's
     * Steam achievements script (Steam::Stats.store: Zlib::Deflate + Marshal into steamstat.dat)
     * failed after the first achievement and retried every second, 1210 "Couldn't store pending
     * achievements!" lines in stdout.log (a card write each).
     */
    Init_zlib();
    rb_provide("zlib.so");
#endif
#ifdef MKXP_VITA_ANIM_PREFETCH
    vitaAnimPrefetchInit();
#endif
#ifdef MKXP_VITA_GC_MID
    /*
     * Perf fix (MKXP_VITA_GC_MID): Ruby GC parameters between the defaults and the d40 "L" set
     * (d40: defaults ~10 GCs per 120 frames, ~47 ms each; L set 2.5 GCs, ~109 ms each). Embedded
     * Ruby never reads RUBY_GC_* by itself (only ruby_options() calls ruby_gc_set_params()).
     * L held at boot keeps the Ruby defaults (A/B). qa.log: GC_MID.
     */
    /* d86: free slots after a GC configurable (MKXP_VITA_GC_FREE_SLOTS): more room = fewer minor GCs
     * (d85: one every ~5 s on the maps, ~18 ms each = a dropped frame); +20 bytes of heap per slot. */
#define VITA_GC_STR2(x) #x
#define VITA_GC_STR(x) VITA_GC_STR2(x)
#ifdef MKXP_VITA_GC_FREE_SLOTS
#define VITA_GC_FREE_SLOTS VITA_GC_STR(MKXP_VITA_GC_FREE_SLOTS)
#else
#define VITA_GC_FREE_SLOTS "60000"
#endif
    /* d87: d86 (free_slots 300000) made Ruby fall back to ~300 ms major GCs every ~10 s (the minimum
     * of free slots could not be met after a minor GC without growing the heap). Instead the heap
     * starts larger (MKXP_VITA_GC_INIT_SLOTS) and is kept (MKXP_VITA_GC_MAX_RATIO: free share above
     * which Ruby releases pages), with the default 60000 free slots. */
#ifdef MKXP_VITA_GC_INIT_SLOTS
#define VITA_GC_INIT_SLOTS VITA_GC_STR(MKXP_VITA_GC_INIT_SLOTS)
#else
#define VITA_GC_INIT_SLOTS "300000"
#endif
    {
        SceCtrlData vitaGcPad;
#ifdef MKXP_VITA_NO_DEBUG_KEYS
        const bool vitaGcDefaults = false;   /* public build: no A/B key at boot */
        (void)vitaGcPad;
#else
        const bool vitaGcDefaults = sceCtrlPeekBufferPositive(0, &vitaGcPad, 1) > 0 && (vitaGcPad.buttons & SCE_CTRL_LTRIGGER);
#endif
        if (!vitaGcDefaults) {
            setenv("RUBY_GC_HEAP_INIT_SLOTS", VITA_GC_INIT_SLOTS, 1);
#ifdef MKXP_VITA_GC_MAX_RATIO
            setenv("RUBY_GC_HEAP_FREE_SLOTS_MAX_RATIO", VITA_GC_STR(MKXP_VITA_GC_MAX_RATIO), 1);
#endif
            setenv("RUBY_GC_HEAP_FREE_SLOTS", VITA_GC_FREE_SLOTS, 1);
            setenv("RUBY_GC_HEAP_FREE_SLOTS_MIN_RATIO", "0.25", 1);
            setenv("RUBY_GC_MALLOC_LIMIT", "33554432", 1);
            ruby_gc_set_params();
            vitaGcTuned = 2;
        }
        FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
        if (f) {
            fprintf(f, "GC_MID %s\n", vitaGcDefaults ? "OFF (L held: Ruby defaults)" : "ON: init_slots=" VITA_GC_INIT_SLOTS " free_slots=" VITA_GC_FREE_SLOTS " min_ratio=0.25 malloc_limit=32MiB"
#ifdef MKXP_VITA_GC_MAX_RATIO
                    " max_ratio=" VITA_GC_STR(MKXP_VITA_GC_MAX_RATIO)
#endif
);
            fclose(f);
        }
    }
#endif

rb_define_global_function(
    "vita_dbg",
    RUBY_METHOD_FUNC(vita_debug_print),
    1
);

rb_define_global_function(
    "vita_trace",
    RUBY_METHOD_FUNC(vita_trace_print),
    1
);


    if (result != 0) {
        psvDebugScreenPrintf("ruby_setup FAILED: %d\n", result);

        VITA_FREEZE_MARK(MAIN_IDLE);
        while (1)
            sceKernelDelayThread(1000000);
    }

    psvDebugScreenPrintf("Ruby OK\n");
psvDebugScreenPrintf("Initializing Ruby builtins...\n");
rb_call_builtin_inits();
psvDebugScreenPrintf("Ruby builtins OK\n");
psvDebugScreenPrintf("Initializing mkxp Table...\n");
tableBindingInit();
psvDebugScreenPrintf("Table binding OK\n");
psvDebugScreenPrintf("Initializing Color/Tone/Rect...\n");
etcBindingInit();
psvDebugScreenPrintf("ETC bindings OK\n");

psvDebugScreenPrintf("Initializing Bitmap binding...\n");
bitmapBindingInitVitaMinimal();
psvDebugScreenPrintf("Bitmap binding OK\n");

psvDebugScreenPrintf("Initializing Sprite binding...\n");
spriteBindingInit();
psvDebugScreenPrintf("Sprite binding OK\n");

psvDebugScreenPrintf("Initializing Viewport binding...\n");
viewportBindingInit();
psvDebugScreenPrintf("Viewport binding OK\n");

psvDebugScreenPrintf("Initializing Tilemap binding...\n");
tilemapVXBindingInit();
psvDebugScreenPrintf("Tilemap binding OK\n");

psvDebugScreenPrintf("Initializing Plane binding...\n");
planeBindingInit();
psvDebugScreenPrintf("Plane binding OK\n");

sceClibPrintf("VITA_TRACE bindings initialized\n");

psvDebugScreenPrintf("Initializing WindowVX binding...\n");
windowVXBindingInit();
#ifdef MKXP_VITA_AUDIO
audioBindingInit();
#endif
psvDebugScreenPrintf("WindowVX binding OK\n");

int engineStubState = 0;

rb_eval_string_protect(
    "module Graphics\n"
    "  @width = 544\n"
    "  @height = 416\n"
    "  def self.resize_screen(w, h)\n"
    "    @width = w\n"
    "    @height = h\n"
    "  end\n"
    "  def self.width; @width; end\n"
    "  def self.height; @height; end\n"
    "  @frame_count = 0\n"
    "  @frame_rate = 60\n"
    "  @brightness = 255\n"
    "  class << self\n"
    "    attr_accessor :frame_count, :frame_rate, :brightness\n"
    "  end\n"
    "  def self.update\n"
    "    @frame_count += 1\n"
    "    vita_present\n"
    "  end\n"
    "  def self.wait(duration)\n"
    "    duration.to_i.times { update }\n"
    "  end\n"
    "  def self.frame_reset; end\n"
#ifdef MKXP_VITA_SCREEN_FX
    /* Fix (MKXP_VITA_SCREEN_FX): real brightness / fades / freeze / transition / snap (sharedstate-vita.cpp). */
    "  def self.brightness=(v)\n"
    "    @brightness = [[v.to_i, 0].max, 255].min\n"
    "    vita_fx_brightness(@brightness)\n"
    "  end\n"
    "  def self.fadeout(duration)\n"
    "    d = duration.to_i\n"
    "    b0 = @brightness\n"
    "    d.times { |i| self.brightness = b0 - b0 * (i + 1) / d; update }\n"
    "    self.brightness = 0\n"
    "  end\n"
    "  def self.fadein(duration)\n"
    "    d = duration.to_i\n"
    "    b0 = @brightness\n"
    "    d.times { |i| self.brightness = b0 + (255 - b0) * (i + 1) / d; update }\n"
    "    self.brightness = 255\n"
    "  end\n"
    "  def self.snap_to_bitmap\n"
    "    vita_graphics_snap(Bitmap.new(@width, @height))\n"
    "  end\n"
    "  def self.freeze\n"
    "    vita_fx_freeze\n"
    "  end\n"
    /* As mkxp-z Graphics::transition: no-op unless frozen; brightness back to 255 (game over fadeout). */
#ifdef MKXP_VITA_GFX_V2
    /* As mkxp-z Graphics::transition: prog = i / duration (i = 0..duration-1), optional transition
     * bitmap (TransShader, vague clamped to 1..256); a missing file raises like upstream. */
    "  def self.transition(duration = 10, filename = nil, vague = 40)\n"
    "    return unless vita_fx_frozen?\n"
    "    self.brightness = 255\n"
    "    d = duration.to_i\n"
    "    tm = nil\n"
    "    if filename && !filename.to_s.empty?\n"
    "      tm = Bitmap.new(filename.to_s)\n"
    "      vita_fx_trans_map(tm, vague.to_i)\n"
    "    end\n"
    "    begin\n"
    "      d.times { |i| vita_fx_transition(i.to_f / d); vita_present }\n"
    "    ensure\n"
    "      if tm\n"
    "        vita_fx_trans_map(nil, 0)\n"
    "        tm.dispose\n"
    "      end\n"
    "      vita_fx_transition(-1.0)\n"
    "    end\n"
    "  end\n"
    /* Only the 544x416 RGSS3 screen exists in this renderer: say so instead of silently lying. */
    "  def self.resize_screen(w, h)\n"
    "    return if w.to_i == 544 && h.to_i == 416\n"
    "    $stdout.puts \"[vita] Graphics.resize_screen(#{w}, #{h}) unsupported: only 544x416\"\n"
    "  end\n"
    "  def self.play_movie(filename)\n"
    "    $stdout.puts \"[vita] Graphics.play_movie(#{filename}) unsupported: movie skipped\"\n"
    "  end\n"
#else
    "  def self.transition(duration = 10, filename = nil, vague = 40)\n"
    "    return unless vita_fx_frozen?\n"
    "    self.brightness = 255\n"
    "    d = duration.to_i\n"
    "    d.times { |i| vita_fx_transition((i + 1).to_f / d); vita_present }\n"
    "    vita_fx_transition(-1.0)\n"
    "  end\n"
#endif
#else
    "  def self.fadeout(duration)\n"
    "    duration.to_i.times { update }\n"
    "    @brightness = 0\n"
    "  end\n"
    "  def self.fadein(duration)\n"
    "    duration.to_i.times { update }\n"
    "    @brightness = 255\n"
    "  end\n"
    "  def self.snap_to_bitmap\n"
    "    Bitmap.new(@width, @height)\n"
    "  end\n"
    "  def self.freeze; end\n"
    "  def self.transition(*args); end\n"
#endif
#if defined(MKXP_VITA_RGSS_COMPAT) && !defined(MKXP_VITA_GFX_V2)
    /* No movie playback on the Vita yet: RGSS play_movie must at least not raise NoMethodError. */
    "  def self.play_movie(filename); end\n"
#endif
    "end\n"

#ifdef MKXP_VITA_GFX_V2
/* RGSS3 global functions: msgbox / msgbox_p have no dialog on the Vita: they go to stdout.log
 * (redirected); save_data is the real Marshal writer (relative to the game directory). */
"def msgbox(*args)\n"
"  $stdout.puts \"[msgbox] #{args.join}\"\n"
"  nil\n"
"end\n"
"def msgbox_p(*args)\n"
"  $stdout.puts \"[msgbox_p] #{args.map(&:inspect).join(\"\\n\")}\"\n"
"  args.size <= 1 ? args[0] : args\n"
"end\n"
"def save_data(obj, filename)\n"
"  File.open(filename, 'wb') { |f| Marshal.dump(obj, f) }\n"
"end\n"
#endif
"class Font\n"
"  class << self\n"
"    attr_accessor :default_name\n"
"    attr_accessor :default_size\n"
"    attr_accessor :default_bold\n"
"    attr_accessor :default_italic\n"
"    attr_accessor :default_shadow\n"
"    attr_accessor :default_outline\n"
"    attr_accessor :default_color\n"
"    attr_accessor :default_out_color\n"
"  end\n"
"  attr_accessor :name\n"
"  attr_accessor :size\n"
"  attr_accessor :bold\n"
"  attr_accessor :italic\n"
"  attr_accessor :shadow\n"
"  attr_accessor :outline\n"
/* RGSS3 Font: Font.new([name[, size]]); color/out_color are this Font's own Color objects:
 * the setters copy the values (Color#set), dup gives new Color objects (as mkxp-z). */
"  attr_reader :color\n"
"  attr_reader :out_color\n"
"  def initialize(name = nil, size = nil)\n"
"    @name = name.nil? ? Font.default_name : name\n"
"    @size = (size.nil? || size == 0) ? (Font.default_size || 24) : size\n"
"    @bold = Font.default_bold || false\n"
"    @italic = Font.default_italic || false\n"
"    @shadow = Font.default_shadow || false\n"
"    @outline = Font.default_outline.nil? ? true : Font.default_outline\n"
"    @color = Font.default_color ? Font.default_color.dup : Color.new(255, 255, 255, 255)\n"
"    @out_color = Font.default_out_color ? Font.default_out_color.dup : Color.new(0, 0, 0, 128)\n"
"  end\n"
"  def initialize_copy(other)\n"
"    super\n"
"    @color = other.color.dup\n"
"    @out_color = other.out_color.dup\n"
"  end\n"
"  def color=(c)\n"
"    @color.set(c)\n"
"  end\n"
"  def out_color=(c)\n"
"    @out_color.set(c)\n"
"  end\n"
#ifdef MKXP_VITA_FONT_V2
/* RGSS3: Font.exist? asks the real TTF resolver (the one draw_text uses). */
"  def self.exist?(name)\n"
"    vita_font_exist(name.to_s)\n"
"  end\n"
"end\n"
/* RGSS3 / mkxp-z class defaults (games may override them, e.g. Yanfly Core). */
"Font.default_name = ['VL Gothic']\n"
"Font.default_size = 24\n"
"Font.default_bold = false\n"
"Font.default_italic = false\n"
"Font.default_shadow = false\n"
"Font.default_outline = true\n"
"Font.default_color = Color.new(255, 255, 255, 255)\n"
"Font.default_out_color = Color.new(0, 0, 0, 128)\n"
#else
"  def self.exist?(name)\n"
"    true\n"
"  end\n"
"end\n"
#endif

    "class Object\n"
    "  def clone\n"
    "    dup\n"
    "  end\n"
    "end\n"


"class Bitmap\n"
"  attr_reader :font\n"
/* Bitmap#font= copies the Font's state into this Bitmap's own Font (mkxp-z Bitmap::setFont). */
"  def font=(f)\n"
"    @font.name = f.name\n"
"    @font.size = f.size\n"
"    @font.bold = f.bold\n"
"    @font.italic = f.italic\n"
"    @font.shadow = f.shadow\n"
"    @font.outline = f.outline\n"
"    @font.color = f.color\n"
"    @font.out_color = f.out_color\n"
"  end\n"

#ifndef MKXP_VITA_RGSS_COMPAT
"  def hue_change(hue)\n"
"    self\n"
"  end\n"
#endif

#ifndef MKXP_VITA_SCREEN_FX
"  def blur\n"
"    self\n"
"  end\n"
#endif

#ifndef MKXP_VITA_BITMAP_WINDOW_OPS
/* No-ops unless MKXP_VITA_BITMAP_WINDOW_OPS provides the real ones (bitmap-binding-vita.cpp). */
"  def clear\n"
"    self\n"
"  end\n"

"  def clear_rect(*args)\n"
"    self\n"
"  end\n"

"  def fill_rect(*args)\n"
"    self\n"
"  end\n"

"  def gradient_fill_rect(*args)\n"
"    self\n"
"  end\n"

"  def blt(*args)\n"
"    self\n"
"  end\n"
#endif


/* text_size is native now (bitmap-binding-vita.cpp), with the draw_text metrics. */

#ifndef MKXP_VITA_RGSS_COMPAT
"  def get_pixel(x, y)\n"
"    Color.new(255, 255, 255, 255)\n"
"  end\n"
#endif
"end\n"

#ifndef MKXP_VITA_FS_V2
    "class Dir\n"
    "  def self.glob(pattern)\n"
    "    []\n"
    "  end\n"
    "end\n"
#endif

#ifndef MKXP_VITA_AUDIO
"module Audio\n"
"  def self.bgm_play(*args); end\n"
"  def self.bgm_stop; end\n"
"  def self.bgm_fade(*args); end\n"
"  def self.bgm_pos; 0; end\n"

"  def self.bgs_play(*args); end\n"
"  def self.bgs_stop; end\n"
"  def self.bgs_fade(*args); end\n"
"  def self.bgs_pos; 0; end\n"

"  def self.me_play(*args); end\n"
"  def self.me_stop; end\n"
"  def self.me_fade(*args); end\n"

"  def self.se_play(*args); end\n"
"  def self.se_stop; end\n"
"end\n"
#endif

"module Input\n"
"  DOWN  = :DOWN\n"
"  LEFT  = :LEFT\n"
"  RIGHT = :RIGHT\n"
"  UP    = :UP\n"
"  A     = :A\n"
"  B     = :B\n"
"  C     = :C\n"
"  X     = :X\n"
"  Y     = :Y\n"
"  Z     = :Z\n"
"  L     = :L\n"
"  R     = :R\n"
"  SHIFT = :SHIFT\n"
"  CTRL  = :CTRL\n"
"  ALT   = :ALT\n"
"  F5    = :F5\n"
"  F6    = :F6\n"
"  F7    = :F7\n"
"  F8    = :F8\n"
"  F9    = :F9\n"
"\n"
"  def self.update\n"
"  end\n"
"\n"
"  def self.press?(key)\n"
"    false\n"
"  end\n"
"\n"
"  def self.trigger?(key)\n"
"    false\n"
"  end\n"
"\n"
"  def self.repeat?(key)\n"
"    false\n"
"  end\n"
"\n"
"  def self.dir4\n"
"    0\n"
"  end\n"
"\n"
"  def self.dir8\n"
"    0\n"
"  end\n"
"end\n"


    "def load_data(filename)\n"
    "  path = '" VITA_GAME_ROOT "' + filename\n"
    "  Marshal.load(File.binread(path))\n"
    "end\n"

"def rgss_main\n"
"  File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'RGSS_MAIN_ENTER' }\n"
"  vita_dbg('DBG: rgss_main ENTER')\n"
"\n"
"  SceneManager.define_singleton_method(:run) do\n"
"    vita_dbg('DBG: SceneManager.run ENTER')\n"
"\n"
"    vita_dbg('DBG: before DataManager.init')\n"
"    DataManager.init\n"
"    vita_dbg('DBG: after DataManager.init')\n"
"\n"
"    vita_dbg('DBG: before use_midi?')\n"
"    if SceneManager.use_midi?\n"
"      vita_dbg('DBG: MIDI enabled')\n"
"      Audio.setup_midi\n"
"      vita_dbg('DBG: after Audio.setup_midi')\n"
"    else\n"
"      vita_dbg('DBG: MIDI disabled')\n"
"    end\n"
"\n"
"    vita_dbg('DBG: before first_scene_class')\n"
"    klass = SceneManager.first_scene_class\n"
"    vita_dbg('DBG: after first_scene_class')\n"
"\n"
"    vita_dbg('DBG: before scene.new')\n"
"    SceneManager.instance_variable_set(:@scene, klass.new)\n"
"    vita_dbg('DBG: after scene.new')\n"
"\n"
"    while SceneManager.instance_variable_get(:@scene)\n"
"      File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'GAME_LOOP_ITER' }\n"
"      scene = SceneManager.instance_variable_get(:@scene)\n"
"      vita_dbg('DBG: before scene.main')\n"
"      scene.main\n"
"      vita_dbg('DBG: after scene.main')\n"
"    end\n"
"\n"
"    vita_dbg('DBG: SceneManager.run EXIT')\n"
"  end\n"
"\n"

"  Scene_Title.class_eval do\n"
"    alias vita_original_start start\n"
"\n"
"    def start\n"
"      vita_dbg('TITLE: start ENTER')\n"
"      vita_dbg('TITLE: before original start')\n"
"      vita_original_start\n"
"      vita_dbg('TITLE: after original start')\n"
"    end\n"
"  end\n"
"\n"

"  Scene_Title.send(:define_method, :start) do\n"
"    vita_trace('Scene_Title start')\n"
"    File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'SCENE_TITLE_START' }\n"
"    vita_dbg('TITLE: start ENTER')\n"
"\n"
"    vita_dbg('TITLE: before create_main_viewport')\n"
"    create_main_viewport\n"
"    vita_dbg('TITLE: after create_main_viewport')\n"
"\n"
"    vita_dbg('TITLE: before SceneManager.clear')\n"
"    SceneManager.clear\n"
"    vita_dbg('TITLE: after SceneManager.clear')\n"
"\n"
"    vita_dbg('TITLE: before Graphics.freeze')\n"
"    Graphics.freeze\n"
"    vita_dbg('TITLE: after Graphics.freeze')\n"
"\n"

"    vita_dbg('TITLE: before create_background')\n"
"    vita_dbg('TITLE1 NAME: ' + $data_system.title1_name.to_s)\n"
"    vita_dbg('TITLE2 NAME: ' + $data_system.title2_name.to_s)\n"
"    create_background\n"

"    vita_dbg('TITLE: after create_background')\n"
"\n"

"    vita_dbg('TITLE: before create_foreground')\n"
"    create_foreground\n"
"    vita_dbg('TITLE: after create_foreground')\n"
"\n"
"    vita_dbg('TITLE: before create_command_window')\n"
"    create_command_window\n"
"    vita_dbg('TITLE: after create_command_window')\n"
"\n"
"    vita_dbg('TITLE: before play_title_music')\n"
"    play_title_music\n"
"    vita_dbg('TITLE: after play_title_music')\n"
"  end\n"
"\n"


"  Scene_Base.send(:define_method, :main) do\n"
"    vita_dbg('DBG: Scene_Base main ENTER')\n"
"\n"
"    vita_dbg('DBG: before start')\n"
"    start\n"
"    vita_dbg('DBG: after start')\n"
"\n"
"    vita_dbg('DBG: before post_start')\n"
"    post_start\n"
"    vita_dbg('DBG: after post_start')\n"
"\n"
"    vita_dbg('DBG: before first update')\n"
"    File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'SCENE_MAP_FIRST_UPDATE' }\n"
"    update\n"
"    vita_dbg('DBG: FIRST UPDATE OK')\n"
"    File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'SCENE_MAP_FIRST_RENDER' }\n"
"\n"
"    update until scene_changing?\n"
"\n"
"    vita_dbg('DBG: leaving update loop')\n"
"    pre_terminate\n"
"    terminate\n"
"  end\n"
"\n"

"  Scene_Title.class_eval do\n"
"    alias vita_trace_command_new_game command_new_game\n"
"    def command_new_game\n"
"      File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'NEW_GAME_ENTER' }\n"
"      vita_trace('command_new_game')\n"
"      vita_trace_command_new_game\n"
"    end\n"
"  end\n"
"  Scene_Map.class_eval do\n"
"    alias vita_trace_scene_map_start start\n"
"    def start\n"
"      vita_trace('Scene_Map start')\n"
"      File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'SCENE_MAP_CREATE' }\n"
"      File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'SCENE_MAP_START' }\n"
"      vita_trace_scene_map_start\n"
"    end\n"
"  end\n"
"  Spriteset_Map.class_eval do\n"
"    alias vita_trace_spriteset_map_initialize initialize\n"
"    def initialize(*args)\n"
"      vita_trace_spriteset_map_initialize(*args)\n"
"      vita_trace('after Spriteset_Map initialize')\n"
"    end\n"
"  end\n"
"\n"
#ifdef MKXP_VITA_DEBUG_BITMAP_WINDOW
/* Diagnostic only: log Bitmap window operations for the battle and the scene right after it. */
"  Scene_Battle.prepend(Module.new { def start; vita_bitmap_log_arm(1200); super; end; def terminate; super; vita_bitmap_log_arm(600); end })\n"
#endif
#ifdef MKXP_VITA_DEBUG_TEXT_LOG
/* Diagnostic only: log the first 100 draw_text calls of each battle (bitmap-binding-vita.cpp). */
"  Scene_Battle.prepend(Module.new { def start; vita_text_log_arm(100); super; end })\n"
#endif
#ifdef MKXP_VITA_DEBUG_BOOT_CHECKPOINT
/* Debug only: boot from / record the pre-battle checkpoint (MKXP_VITA_DEBUG_BOOT_CHECKPOINT). */
"  # DEBUG ONLY: evaluated at top level when rgss_main runs (module definitions are not allowed in a method).\n"
"  TOPLEVEL_BINDING.eval(<<~'VITA_DEBUG_BOOT', 'vita_debug_boot', 1)\n"
"  # DEBUG ONLY (MKXP_VITA_DEBUG_BOOT_CHECKPOINT): boot straight into a checkpoint recorded just\n"
"  # before the first battle (map 41, event 2 -> troop 3). Uses the game's own save serializer.\n"
"  module VitaDebugBoot\n"
"    PATH = '" VITA_GAME_ROOT "debug_checkpoint_battle1.rvdata2'\n"
"    MAP_ID = 41\n"
"    DONE_SWITCH = 27\n"
"    def self.log(msg)\n"
"      File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"DEBUG_CHECKPOINT #{msg}\" }\n"
"      vita_trace(\"DEBUG_CHECKPOINT #{msg}\")\n"
"    end\n"
"    def self.save\n"
"      tmp = PATH + '.tmp'\n"
"      File.open(tmp, 'wb') do |f|\n"
"        $game_system.on_before_save\n"
"        Marshal.dump(DataManager.make_save_header, f)\n"
"        Marshal.dump(DataManager.make_save_contents, f)\n"
"      end\n"
"      File.rename(tmp, PATH)\n"
"      log(\"SAVED map=#{$game_map.map_id} x=#{$game_player.x} y=#{$game_player.y} dir=#{$game_player.direction} frame=#{Graphics.frame_count} sw4=#{$game_switches[4]} sw7=#{$game_switches[7]}\")\n"
"    rescue Exception => e\n"
"      log(\"SAVE_FAILED #{e.class}: #{e.message.tr(\"\\n\", \" \")}\")\n"
"    end\n"
"    def self.load\n"
"      File.open(PATH, 'rb') do |f|\n"
"        Marshal.load(f)\n"
"        DataManager.extract_save_contents(Marshal.load(f))\n"
"      end\n"
"      DataManager.reload_map_if_updated\n"
"      $game_system.on_after_load\n"
"      log(\"LOADED map=#{$game_map.map_id} x=#{$game_player.x} y=#{$game_player.y} dir=#{$game_player.direction}\")\n"
"      true\n"
"    rescue Exception => e\n"
"      log(\"LOAD_FAILED #{e.class}: #{e.message.tr(\"\\n\", \" \")}\")\n"
"      false\n"
"    end\n"
"  end\n"
"  class << SceneManager\n"
"    alias vita_debug_boot_first_scene_class first_scene_class\n"
"    def first_scene_class\n"
"      if File.exist?(VitaDebugBoot::PATH)\n"
"        $vita_debug_checkpoint_done = true\n"
"        return Scene_Map if VitaDebugBoot.load\n"
"      else\n"
"        VitaDebugBoot.log(\"NONE: normal boot, will record on map #{VitaDebugBoot::MAP_ID}\")\n"
"      end\n"
"      vita_debug_boot_first_scene_class\n"
"    end\n"
"  end\n"
"  # Record once: first Scene_Map frame on map 41 with no event running and the dog not dead yet.\n"
"  module VitaDebugBootRecord\n"
"    def update\n"
"      if !$vita_debug_checkpoint_done && $game_map.map_id == VitaDebugBoot::MAP_ID &&\n"
"         !$game_switches[VitaDebugBoot::DONE_SWITCH] && !$game_map.interpreter.running? &&\n"
"         !$game_message.busy?\n"
"        $vita_debug_checkpoint_done = true\n"
"        VitaDebugBoot.save\n"
"      end\n"
"      super\n"
"    end\n"
"  end\n"
"  Scene_Map.prepend(VitaDebugBootRecord)\n"
"  VITA_DEBUG_BOOT\n"
#endif
#ifdef MKXP_VITA_DEBUG_SOAK
/* Debug only: resource-churn soak test while R is held at boot (MKXP_VITA_DEBUG_SOAK). Source: soak.rb. */
"  TOPLEVEL_BINDING.eval(<<~'VITA_SOAK', 'vita_soak', 1)\n"
"  VITA_SOAK_ROOT = '" VITA_GAME_ROOT "'\n"
"  # DEBUG ONLY (MKXP_VITA_DEBUG_SOAK): resource-churn soak test, active only when R is held at boot.\n"
"  # New game (no save, no checkpoint), then forever: map transfer x3, main menu, battle, over 18 maps\n"
"  # of the first areas (tilesets, parallaxes, BGMs) and 8 troops, picked from the map data (code 201\n"
"  # transfer targets reachable from map 41, code 301 troops). Messages/battles advance by pulsing C.\n"
"  # Menu and battle are opened exactly as the game does (Scene_Map#call_menu, Game_Interpreter#command_301).\n"
"  # Saving, quitting, game over and title are neutralised and logged: soak never writes save files.\n"
"  module VitaSoak\n"
"    ROUTE = [[41, 0, 10], [42, 38, 22], [29, 5, 10], [45, 7, 10], [48, 27, 1], [50, 5, 13],\n"
"             [55, 1, 27], [57, 6, 9], [103, 27, 17], [80, 4, 10], [58, 7, 10], [59, 4, 10],\n"
"             [305, 25, 10], [77, 52, 15], [60, 1, 23], [11, 96, 16], [280, 3, 9], [13, 9, 10]]\n"
"    TROOPS = [3, 4, 5, 12, 1, 22, 13, 15]\n"
"    # Turbo timings (user request: answers in 15-20 min of hardware time, not 1-2 h): one loop of the\n"
"    # 18 maps in ~2.5 min instead of ~9. Same work per step (map load, battle, menu), just closer.\n"
"    STEP_FRAMES = 120      # 2 s on each map\n"
"    MENU_FRAMES = 60\n"
"    BATTLE_FRAMES = 300    # then the enemies are knocked out (normal victory)\n"
"    PULSE = 20\n"
"    LOG_PATH = VITA_SOAK_ROOT + 'soak.log'\n"
"    @active = false\n"
"    @step = 0\n"
"    @frames = 0\n"
"    @map_i = 0\n"
"    @troop_i = 0\n"
"    class << self\n"
"      attr_accessor :active, :frames\n"
"      attr_reader :step\n"
"    end\n"
"  \n"
"    # A failed write is reported on $stdout (stdout.log, opened at boot): d35 soak, every File.open\n"
"    # failed silently after ~6 h and the run looked stuck.\n"
"    def self.log(msg)\n"
"      line = \"f=#{Graphics.frame_count} step=#{@step} #{msg}\"\n"
"      begin\n"
"        File.open(LOG_PATH, 'a') { |f| f.puts line }\n"
"      rescue Exception => e\n"
"        @log_failures = (@log_failures || 0) + 1\n"
"        if @log_failures <= 3 || (@log_failures % 100).zero?\n"
"          $stdout.puts \"SOAK_LOG_FAILED ##{@log_failures} #{e.class}: #{e.message} | #{line}\"\n"
"          $stdout.flush rescue nil\n"
"        end\n"
"      end\n"
"      vita_trace(\"SOAK #{msg}\") rescue nil\n"
"    end\n"
"  \n"
"    def self.ledger(tag)\n"
"      vita_gl_ledger_dump(\"soak step=#{@step} #{tag}\") if respond_to?(:vita_gl_ledger_dump, true)\n"
"    end\n"
"  \n"
"    def self.start_new_game\n"
"      DataManager.setup_new_game\n"
"      m = ROUTE[0]\n"
"      $game_map.setup(m[0])\n"
"      $game_player.moveto(m[1], m[2])\n"
"      $game_player.refresh\n"
"      $game_map.autoplay\n"
"      log(\"START map=#{m[0]}\")\n"
"    end\n"
"  \n"
"    def self.unblock_interpreter\n"
"      return unless $game_map.interpreter.running?\n"
"      $game_map.interpreter.clear\n"
"      $game_message.clear\n"
"      log('INTERPRETER_CLEARED')\n"
"    end\n"
"  \n"
"    # Called from Scene_Map#update once per frame. An exception here is logged, never fatal.\n"
"    def self.map_tick(scene)\n"
"      map_tick_inner(scene)\n"
"    rescue Exception => e\n"
"      @frames = 0\n"
"      log(\"SOAK_ERROR #{e.class}: #{e.message.tr(\"\\n\", ' ')} @ #{(e.backtrace || []).first(3).join(' | ')}\")\n"
"    end\n"
"  \n"
"    def self.map_tick_inner(scene)\n"
"      @frames += 1\n"
"      log(\"ALIVE map=#{$game_map.map_id} scene=#{scene.class}\") if (Graphics.frame_count % 3600).zero?\n"
"      return if @frames < STEP_FRAMES\n"
"      if $game_player.transfer?\n"
"        # Watchdog: a reserved transfer should complete within a few frames of the step.\n"
"        log(\"TRANSFER_PENDING #{@frames} frames map=#{$game_map.map_id}\") if @frames == STEP_FRAMES + 600\n"
"        return\n"
"      end\n"
"      @frames = 0\n"
"      @step += 1\n"
"      ledger(\"before action=#{@step % 5}\")\n"
"      case @step % 5\n"
"      when 2\n"
"        log(\"MENU from map=#{$game_map.map_id}\")\n"
"        scene.send(:call_menu)   # as the game: SceneManager.call + Window_MenuCommand::init_command_position\n"
"      when 4\n"
"        troop = TROOPS[@troop_i % TROOPS.size]\n"
"        @troop_i += 1\n"
"        return log(\"BATTLE_SKIPPED troop=#{troop} (no such troop)\") unless $data_troops[troop]\n"
"        unblock_interpreter\n"
"        $game_party.members.each(&:recover_all)\n"
"        log(\"BATTLE troop=#{troop} map=#{$game_map.map_id}\")\n"
"        BattleManager.setup(troop, true, true)\n"
"        $game_player.make_encounter_count\n"
"        SceneManager.call(Scene_Battle)\n"
"      else\n"
"        @map_i += 1\n"
"        m = ROUTE[@map_i % ROUTE.size]\n"
"        loop_census if (@map_i % ROUTE.size).zero?\n"
"        unblock_interpreter\n"
"        log(\"TRANSFER map=#{m[0]} x=#{m[1]} y=#{m[2]}\")\n"
"        $game_player.reserve_transfer(m[0], m[1], m[2], 2)\n"
"      end\n"
"    end\n"
"  \n"
"    def self.io_name(io)\n"
"      io.respond_to?(:path) && io.path ? io.path : \"fd#{io.fileno}\"\n"
"    rescue Exception\n"
"      '?'\n"
"    end\n"
"  \n"
"    # Once per route loop: Ruby object counts by type and the newlib heap ledger (per-caller live bytes).\n"
"    def self.loop_census\n"
"      c = ObjectSpace.count_objects\n"
"      log(\"LOOP #{@map_i / ROUTE.size} objects total=#{c[:TOTAL] - c[:FREE]} string=#{c[:T_STRING]} array=#{c[:T_ARRAY]} \" \\\n"
"          \"hash=#{c[:T_HASH]} object=#{c[:T_OBJECT]} data=#{c[:T_DATA]} struct=#{c[:T_STRUCT]} gc=#{GC.count}\")\n"
"      # Live objects per class (one pass over the heap), top 20 with the change since the previous loop.\n"
"      per = Hash.new(0)\n"
"      ObjectSpace.each_object { |o| per[(o.class rescue BasicObject)] += 1 }\n"
"      prev = @class_prev || {}\n"
"      top = per.sort_by { |k, v| -v }.first(20)\n"
"      log(\"LOOP #{@map_i / ROUTE.size} classes \" + top.map { |k, v| \"#{k}=#{v}(#{v - prev.fetch(k, v)})\" }.join(' '))\n"
"      growers = per.map { |k, v| [k, v - prev.fetch(k, 0)] }.select { |k, d| d > 0 && prev.key?(k) }.sort_by { |k, d| -d }.first(10)\n"
"      log(\"LOOP #{@map_i / ROUTE.size} growers \" + growers.map { |k, d| \"#{k}+#{d}\" }.join(' ')) unless growers.empty?\n"
"      @class_prev = per\n"
"      # Open Ruby IO objects (file descriptors held by the game scripts), by path.\n"
"      ios = ObjectSpace.each_object(IO).reject { |io| io.closed? rescue true }\n"
"      paths = Hash.new(0)\n"
"      ios.each { |io| paths[io_name(io)] += 1 }\n"
"      log(\"LOOP #{@map_i / ROUTE.size} open_io=#{ios.size} \" + paths.sort_by { |k, v| -v }.first(8).map { |k, v| \"#{k}=#{v}\" }.join(' '))\n"
"      vita_heap_ledger_dump(\"loop #{@map_i / ROUTE.size}\") if respond_to?(:vita_heap_ledger_dump, true)\n"
"    rescue Exception => e\n"
"      log(\"LOOP_CENSUS_ERROR #{e.class}: #{e.message}\")\n"
"    end\n"
"  \n"
"    # C is pulsed in battles and while a message is shown. A message window can stay open after the\n"
"    # interpreter was cleared by a forced transfer ($game_message no longer busy): pulse it too.\n"
"    def self.message_window_open?\n"
"      mw = SceneManager.scene && SceneManager.scene.instance_variable_get(:@message_window)\n"
"      mw && !mw.disposed? && mw.openness > 0\n"
"    rescue Exception\n"
"      false\n"
"    end\n"
"  \n"
"    def self.pulse?\n"
"      @active && (Graphics.frame_count % PULSE).zero? &&\n"
"        ($game_message.busy? || SceneManager.scene_is?(Scene_Battle) || message_window_open?)\n"
"    end\n"
"  \n"
"    # Unattended runs: tell the system the console is in use (no automatic standby / screen off).\n"
"    def self.keep_awake\n"
"      vita_power_tick if @active && (Graphics.frame_count % 60).zero? && respond_to?(:vita_power_tick, true)\n"
"    end\n"
"  \n"
"    # LISA battles wait for the Yanfly Input Combo keys (L R X Y Z) after \"execute\": in battle, one\n"
"    # of them is pulsed every 10 frames (offset from the C pulse), cycling through the five.\n"
"    COMBO_KEYS = { L: 17, R: 18, X: 14, Y: 15, Z: 16 }\n"
"    def self.combo_pulse?(key)\n"
"      return false unless @active && SceneManager.scene_is?(Scene_Battle)\n"
"      f = Graphics.frame_count\n"
"      return false unless f % 10 == 5\n"
"      sym = COMBO_KEYS.keys[(f / 10) % COMBO_KEYS.size]\n"
"      key == sym || key == COMBO_KEYS[sym]\n"
"    end\n"
"  end\n"
"  \n"
"  class << SceneManager\n"
"    alias vita_soak_first_scene_class first_scene_class\n"
"    def first_scene_class\n"
"      Input.update\n"
"      if Input.press?(:R)\n"
"        VitaSoak.active = true\n"
"        $vita_debug_checkpoint_done = true   # never record the debug checkpoint during a soak\n"
"        VitaSoak.log('SOAK_ENABLED (R held at boot)')\n"
"        VitaSoak.start_new_game\n"
"        return Scene_Map\n"
"      end\n"
"      vita_soak_first_scene_class\n"
"    end\n"
"  \n"
"    alias vita_soak_exit exit\n"
"    def exit\n"
"      return vita_soak_exit unless VitaSoak.active\n"
"      VitaSoak.log('EXIT_BLOCKED')\n"
"      goto(Scene_Map)\n"
"    end\n"
"  end\n"
"  \n"
"  class << Input\n"
"    alias vita_soak_trigger trigger?\n"
"    def trigger?(key)\n"
"      return true if (key == :C || key == 13) && VitaSoak.pulse?\n"
"      return true if VitaSoak.combo_pulse?(key)\n"
"      vita_soak_trigger(key)\n"
"    end\n"
"  end\n"
"  \n"
"  class << DataManager\n"
"    alias vita_soak_save_game save_game\n"
"    def save_game(index)\n"
"      return vita_soak_save_game(index) unless VitaSoak.active\n"
"      VitaSoak.log(\"SAVE_BLOCKED slot=#{index}\")\n"
"      false\n"
"    end\n"
"  end\n"
"  \n"
"  module VitaSoakMap\n"
"    def update\n"
"      VitaSoak.keep_awake\n"
"      VitaSoak.map_tick(self) if VitaSoak.active\n"
"      super\n"
"    end\n"
"  end\n"
"  Scene_Map.prepend(VitaSoakMap)\n"
"  \n"
"  module VitaSoakMenu\n"
"    def update\n"
"      VitaSoak.keep_awake\n"
"      super\n"
"      return unless VitaSoak.active\n"
"      VitaSoak.frames += 1\n"
"      if VitaSoak.frames >= VitaSoak::MENU_FRAMES\n"
"        VitaSoak.frames = 0\n"
"        VitaSoak.log('MENU_RETURN')\n"
"        return_scene\n"
"      end\n"
"    end\n"
"  end\n"
"  Scene_Menu.prepend(VitaSoakMenu)\n"
"  \n"
"  module VitaSoakBattle\n"
"    # Elapsed time from Graphics.frame_count: LISA's battle waits (combo window, effects) loop on\n"
"    # update_basic, so counting Scene_Battle#update calls under-measured (d20b: 10000-frame battles).\n"
"    def start\n"
"      @vita_soak_t0 = Graphics.frame_count\n"
"      @vita_soak_won = false\n"
"      @vita_soak_waiting_logged = false\n"
"      VitaSoak.frames = 0 if VitaSoak.active\n"
"      super\n"
"    end\n"
"  \n"
"    # The timeout ends the battle the way a player does: by winning. In a safe state (out of the\n"
"    # turn, no message, combo window closed) every enemy is knocked out with the game's own die, then\n"
"    # the pulsed inputs drive the next turn to the normal victory (d22-d24: a forced escape via\n"
"    # process_abort left a LISA sprite on the disposed battle viewport -> \"disposed viewport\", the\n"
"    # same RGSSError mkxp-z raises upstream). Nothing is forced after that; a stuck battle is logged.\n"
"    def vita_soak_safe?\n"
"      return false if BattleManager.in_turn? || BattleManager.aborting? || $game_message.busy?\n"
"      combo = instance_variable_get(:@input_combo_skill_window)\n"
"      return false if combo && !combo.disposed? && combo.visible\n"
"      true\n"
"    end\n"
"  \n"
"    def update\n"
"      VitaSoak.keep_awake\n"
"      super\n"
"      return unless VitaSoak.active\n"
"      elapsed = Graphics.frame_count - @vita_soak_t0\n"
"      if !@vita_soak_won && elapsed >= VitaSoak::BATTLE_FRAMES && vita_soak_safe?\n"
"        @vita_soak_won = true\n"
"        $game_troop.members.each { |e| e.die if e.alive? }\n"
"        VitaSoak.log(\"BATTLE_KO_ENEMIES (timeout, #{elapsed} frames, safe state)\")\n"
"      elsif elapsed >= VitaSoak::BATTLE_FRAMES * 3 && !@vita_soak_waiting_logged\n"
"        @vita_soak_waiting_logged = true\n"
"        VitaSoak.log(\"BATTLE_STILL_RUNNING (#{elapsed} frames, won=#{!!@vita_soak_won})\")\n"
"      end\n"
"    end\n"
"  \n"
"    def terminate\n"
"      super\n"
"      return unless VitaSoak.active\n"
"      VitaSoak.frames = 0\n"
"      $game_party.members.each(&:recover_all)\n"
"      VitaSoak.log('BATTLE_END')\n"
"      VitaSoak.ledger('after battle')\n"
"    end\n"
"  end\n"
"  Scene_Battle.prepend(VitaSoakBattle)\n"
"  \n"
"  [Scene_Save, Scene_Load].each do |klass|\n"
"    klass.prepend(Module.new do\n"
"      def start\n"
"        super\n"
"        return unless VitaSoak.active\n"
"        VitaSoak.log(\"#{self.class}_BLOCKED\")\n"
"        return_scene\n"
"      end\n"
"    end)\n"
"  end\n"
"  \n"
"  module VitaSoakGameover\n"
"    def start\n"
"      return super unless VitaSoak.active\n"
"      super\n"
"      VitaSoak.log('GAMEOVER_SKIPPED')\n"
"      $game_party.members.each(&:recover_all)\n"
"      SceneManager.goto(Scene_Map)\n"
"    end\n"
"  end\n"
"  Scene_Gameover.prepend(VitaSoakGameover)\n"
"  \n"
"  module VitaSoakTitle\n"
"    def start\n"
"      return super unless VitaSoak.active\n"
"      super\n"
"      VitaSoak.log('TITLE_SKIPPED: new game again')\n"
"      VitaSoak.start_new_game\n"
"      SceneManager.goto(Scene_Map)\n"
"    end\n"
"  end\n"
"  Scene_Title.prepend(VitaSoakTitle)\n"
"  VITA_SOAK\n"
#endif
#ifdef MKXP_VITA_SAVE_PATH
"  # Fix (MKXP_VITA_SAVE_PATH): save files in " VITA_GAME_ROOT " (the working directory app0:\n"
"  # is read-only) and no Dir.glob (stubbed): check the slots directly.\n"
"  DataManager.singleton_class.send(:define_method, :make_filename) { |index| sprintf('" VITA_GAME_ROOT "Save%02d.rvdata2', index + 1) }\n"
"  DataManager.singleton_class.send(:define_method, :save_file_exists?) { (0...savefile_max).any? { |i| File.exist?(make_filename(i)) } }\n"
#endif
/* Debug boot-map: an alias, so it must come before every prepend-based hook below (else recursion). */
#ifdef MKXP_VITA_DEBUG_BOOT_MAP_ID
"  $vita_debug_boot_map = [" VITA_DIAG_STR(MKXP_VITA_DEBUG_BOOT_MAP_ID) ", " VITA_DIAG_STR(MKXP_VITA_DEBUG_BOOT_MAP_X) ", " VITA_DIAG_STR(MKXP_VITA_DEBUG_BOOT_MAP_Y) "]\n"
"  # DEBUG ONLY (MKXP_VITA_DEBUG_BOOT_MAP): new game placed straight on one map (tileset repro).\n"
"  class << SceneManager\n"
"    alias vita_debug_map_first_scene_class first_scene_class\n"
"    def first_scene_class\n"
"      if defined?(VitaDebugBoot)\n"
"        # With the checkpoint in the same build: the map boot only while L is held at startup.\n"
"        Input.update\n"
"        unless Input.press?(Input::L)\n"
"          File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts 'DEBUG_BOOT_MAP skipped (L not held): checkpoint path' }\n"
"          return vita_debug_map_first_scene_class\n"
"        end\n"
"        # Never let the prologue re-record (overwrite) the existing checkpoint on map 41.\n"
"        $vita_debug_checkpoint_done = true\n"
"      end\n"
"      DataManager.setup_new_game\n"
"      $game_map.setup($vita_debug_boot_map[0])\n"
"      $game_player.moveto($vita_debug_boot_map[1], $vita_debug_boot_map[2])\n"
"      $game_player.refresh\n"
"      $game_map.autoplay\n"
"      File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"DEBUG_BOOT_MAP map=#{$vita_debug_boot_map.inspect}\" }\n"
"      Scene_Map\n"
"    end\n"
"  end\n"
#endif
#ifdef MKXP_VITA_FREEZE_PROBE
/* Freeze probe hooks: after every alias/class_eval above, right before the game starts. */
"  vita_freeze_install_hooks\n"
#endif
#ifdef MKXP_VITA_DIAG
/* Diagnostic only: vita_diag.cpp timing hooks (see mkxp-z/src/vita_diag.h). */
"  # DIAGNOSTIC ONLY (MKXP_VITA_TIMELINE / MKXP_VITA_PERF_BITMAP): timing wrappers, installed after\n"
"  # every alias/class_eval of the game scripts (same place as the freeze probe hooks).\n"
"  TOPLEVEL_BINDING.eval(<<~'VITA_DIAG_HOOKS', 'vita_diag_hooks', 1)\n"
"  module VitaDiagHooks\n"
"    def self.wrap(owner, meth, tag, min_us = 0, &det)\n"
"      return unless owner.method_defined?(meth) || owner.private_method_defined?(meth)\n"
"      priv = owner.private_method_defined?(meth)\n"
"      # def + zsuper (d76): define_method { |*a, &b| super(*a, &b) } left one callinfo + callcache per\n"
"      # call on the Vita (d75 census: update_all_windows, update_events, update_interpreter, ...).\n"
"      m = Module.new\n"
"      m.const_set(:VITA_TAG, tag)\n"
"      m.const_set(:VITA_MIN_US, min_us)\n"
"      m.const_set(:VITA_DET, det)\n"
"      m.module_eval(<<~'VITA_M'.sub('__M__', meth.to_s))\n"
"        def __M__(*a, &b)\n"
"          t0 = vita_diag_now\n"
"          begin\n"
"            super\n"
"          ensure\n"
"            d = VITA_DET ? (VITA_DET.call(self, a) rescue nil) : nil\n"
"            vita_diag_span(VITA_TAG, t0, d, VITA_MIN_US)\n"
"          end\n"
"        end\n"
"      VITA_M\n"
"      m.send(:ruby2_keywords, meth)\n"
"      m.send(:private, meth) if priv\n"
"      owner.prepend(m)\n"
"    rescue Exception => e\n"
"      vita_diag_mark('HOOK_FAILED', \"#{tag}: #{e.class}: #{e.message}\")\n"
"    end\n"
"    def self.cls(name)\n"
"      Object.const_defined?(name) ? Object.const_get(name) : nil\n"
"    end\n"
"    def self.inst(cname, meth, tag = nil, min_us = 0, &d)\n"
"      c = cls(cname)\n"
"      wrap(c, meth, tag || \"#{cname}##{meth}\", min_us, &d) if c\n"
"    end\n"
"    def self.sing(cname, meth, tag = nil, min_us = 0, &d)\n"
"      c = cls(cname)\n"
"      wrap(c.singleton_class, meth, tag || \"#{cname}.#{meth}\", min_us, &d) if c\n"
"    end\n"
"  end\n"
"  # Window refreshes (count + time per PERF window; >= 1 ms also in the timeline): every\n"
"  # Window_Base class with its own #refresh, outermost call only.\n"
"  begin\n"
"    ObjectSpace.each_object(Class).select { |c| c <= Window_Base && c.instance_methods(false).include?(:refresh) }.each do |c|\n"
"      # def + zsuper (d76), as VitaDiagHooks.wrap.\n"
"      c.prepend(Module.new do\n"
"        def refresh(*a, &b)\n"
"          return super if $vita_diag_in_refresh\n"
"          $vita_diag_in_refresh = true\n"
"          t0 = vita_diag_now\n"
"          begin\n"
"            super\n"
"          ensure\n"
"            $vita_diag_in_refresh = false\n"
"            vita_diag_op(VITA_DIAG_OP_WIN_REFRESH, t0, self.class.name)\n"
"          end\n"
"        end\n"
"        ruby2_keywords(:refresh)\n"
"      end)\n"
"    end\n"
"  rescue Exception => e\n"
"    vita_diag_mark('HOOK_FAILED', \"refresh: #{e.class}: #{e.message}\")\n"
"  end\n"
"  Scene_Battle.prepend(Module.new { def start; vita_diag_scene(true); super; end; def terminate; super; vita_diag_scene(false); end })\n"
#ifdef MKXP_VITA_TIMELINE
"  # Timeline (MKXP_VITA_TIMELINE): boot and battle entry milestones.\n"
"  H = VitaDiagHooks\n"
"  H.sing(:DataManager, :init)\n"
"  H.sing(:DataManager, :load_database)\n"
"  H.sing(:DataManager, :load_normal_database)\n"
"  H.sing(:DataManager, :create_game_objects)\n"
"  H.sing(:DataManager, :setup_new_game)\n"
"  H.sing(:DataManager, :extract_save_contents)\n"
"  H.sing(:DataManager, :reload_map_if_updated)\n"
"  H.sing(:VitaDebugBoot, :load, 'VitaDebugBoot.load (checkpoint Marshal)')\n"
"  H.sing(:SceneManager, :first_scene_class)\n"
"  H.sing(:SceneManager, :call) { |s, a| a[0].to_s }\n"
"  H.sing(:SceneManager, :goto) { |s, a| a[0].to_s }\n"
"  H.sing(:SceneManager, :snap_for_background)\n"
"  H.sing(:BattleManager, :setup) { |s, a| \"troop=#{a[0]}\" }\n"
"  H.sing(:BattleManager, :play_battle_bgm)\n"
"  H.sing(:Graphics, :transition, nil, 0) { |s, a| \"duration=#{a[0].inspect}\" }\n"
"  H.sing(:Graphics, :freeze)\n"
"  H.sing(:Cache, :load_bitmap, 'Cache.load_bitmap', 1000) { |s, a| \"#{a[0]}#{a[1]}\" }\n"
"  H.inst(:Game_Map, :setup) { |s, a| \"map=#{a[0]} tileset=#{(s.instance_variable_get(:@map).tileset_id rescue '?')}\" }\n"
"  H.inst(:Spriteset_Map, :initialize)\n"
"  H.inst(:Spriteset_Map, :create_tilemap)\n"
"  H.inst(:Spriteset_Map, :load_tileset)\n"
"  H.inst(:Spriteset_Map, :create_characters)\n"
"  H.inst(:Spriteset_Map, :create_parallax)\n"
"  H.inst(:Spriteset_Map, :dispose)\n"
"  H.inst(:Scene_Base, :main) { |s, a| s.class.name }\n"
"  H.inst(:Scene_Map, :start)\n"
"  H.inst(:Scene_Map, :post_start)\n"
"  H.inst(:Scene_Map, :create_spriteset)\n"
"  H.inst(:Scene_Map, :create_all_windows)\n"
"  H.inst(:Scene_Map, :terminate)\n"
"  H.inst(:Scene_Map, :pre_battle_scene)\n"
"  H.inst(:Scene_Map, :perform_battle_transition)\n"
"  H.inst(:Scene_Battle, :start)\n"
"  H.inst(:Scene_Battle, :post_start)\n"
"  H.inst(:Scene_Battle, :create_spriteset)\n"
"  H.inst(:Scene_Battle, :create_all_windows)\n"
"  H.inst(:Scene_Battle, :battle_start)\n"
"  H.inst(:Spriteset_Battle, :initialize)\n"
"  H.inst(:Spriteset_Battle, :create_battleback1)\n"
"  H.inst(:Spriteset_Battle, :create_battleback2)\n"
"  H.inst(:Spriteset_Battle, :create_enemies)\n"
"  H.inst(:Spriteset_Battle, :create_actors)\n"
"  H.inst(:Spriteset_Battle, :battleback1_bitmap)\n"
"  H.inst(:Spriteset_Battle, :battleback2_bitmap)\n"
"  H.inst(:Spriteset_Battle, :create_blurry_background_bitmap)\n"
"  H.inst(:Window_Base, :initialize, 'Window_Base#initialize') { |s, a| s.class.name }\n"
"  H.inst(:Game_Interpreter, :command_301) { |s, a| \"params=#{(s.instance_variable_get(:@params).inspect rescue '?')}\" }\n"
#ifdef MKXP_VITA_RUBY_PROF
"  # Light profiler (MKXP_VITA_RUBY_PROF): Ruby time per script phase and per map event.\n"
"  GC.measure_total_time = true\n"
"  [[:Input, :update, 0, true], [:Game_Map, :update, 1], [:Game_Map, :update_events, 2],\n"
"   [:Game_Map, :update_interpreter, 3], [:Game_Player, :update, 4], [:Spriteset_Map, :update, 5],\n"
"   [:Scene_Base, :update_all_windows, 6], [:Spriteset_Battle, :update, 7],\n"
"   [:DataManager, :load_header, 13, true], [:Window_Base, :initialize, 14], [:Scene_Base, :start, 15],\n"
"   [:Window_SaveFile, :draw_party_characters, 16], [:Window_SaveFile, :draw_playtime, 17],\n"
"   [:Window_Base, :draw_character, 18], [:Cache, :character, 19, true], [:Bitmap, :clear, 20]].each do |cname, meth, idx, sing|\n"
"    c = H.cls(cname)\n"
"    next unless c\n"
"    owner = sing ? c.singleton_class : c\n"
"    next unless owner.method_defined?(meth) || owner.private_method_defined?(meth)\n"
"    priv = owner.private_method_defined?(meth)\n"
"    # def + zsuper (d76): define_method { |*a, &b| super(*a, &b) } left one callinfo + callcache per\n"
"    # call on the Vita (d75 census: update_all_windows, update_events, update_interpreter, ...).\n"
"    m = Module.new\n"
"    m.const_set(:VITA_IDX, idx)\n"
"    m.module_eval(<<~'VITA_M'.sub('__M__', meth.to_s))\n"
"      def __M__(*a, &b)\n"
"        t0 = vita_prof_enter(VITA_IDX)\n"
"        begin\n"
"          super\n"
"        ensure\n"
"          vita_prof(VITA_IDX, t0)\n"
"        end\n"
"      end\n"
"    VITA_M\n"
"    m.send(:ruby2_keywords, meth)\n"
"    m.send(:private, meth) if priv\n"
"    owner.prepend(m)\n"
"  end\n"
"  # Character sprites by class (Galv's Character Effects adds reflect/mirror/shadow/icon sprites):\n"
"  # outermost update only, so a subclass calling super is not counted twice.\n"
"  [[:Sprite_Character, 8], [:Sprite_Reflect, 9], [:Sprite_Mirror, 10], [:Sprite_Shadow, 11], [:Sprite_Icon, 12]].each do |cname, idx|\n"
"    c = H.cls(cname)\n"
"    next unless c && c.method_defined?(:update)\n"
"    # def (not define_method |*a|): no Array per call; vita_prof_now is a Fixnum (no Bignum).\n"
"    m = Module.new\n"
"    m.module_eval(\"def update; return super if $vita_prof_spr; $vita_prof_spr = true; t0 = vita_prof_enter(#{idx}); begin; super; ensure; $vita_prof_spr = false; vita_prof(#{idx}, t0); end; end\")\n"
"    c.prepend(m)\n"
"  end\n"
"  Game_Event.prepend(Module.new { def update; t0 = vita_prof_enter(21); super; ensure; vita_prof_ev(@id, t0); end })\n"
"  # d73: common events by id, interpreter commands by code (minus the time waiting in Fiber.yield),\n"
"  # window classes (outermost update, instrumented the first time Scene_Base sees each class).\n"
"  if defined?(Game_CommonEvent) && Game_CommonEvent.method_defined?(:update)\n"
"    # VX Ace's Game_CommonEvent keeps @event (RPG::CommonEvent), no @common_event_id (d77: all were id 0).\n"
"    Game_CommonEvent.prepend(Module.new { def update; t0 = vita_prof_now; super; ensure; vita_prof_ce(@event ? @event.id : 0, t0); end })\n"
"  end\n"
"  class << Fiber\n"
"    prepend(Module.new { def yield(*a); t = vita_prof_now; begin; super; ensure; vita_prof_yielded(t); end; end })\n"
"  end\n"
"  if Game_Interpreter.method_defined?(:execute_command)\n"
"    Game_Interpreter.prepend(Module.new do\n"
"      def execute_command\n"
"        c = (@list && (cmd = @list[@index])) ? cmd.code : 0\n"
"        y0 = vita_prof_ymark\n"
"        t0 = vita_prof_now\n"
"        super\n"
"      ensure\n"
"        vita_prof_cmd(c, t0, y0) if t0\n"
"      end\n"
"    end)\n"
"  end\n"
"  module VitaProfWin\n"
"    @done = {}\n"
"    def self.instrument(k)\n"
"      return if @done[k]\n"
"      @done[k] = true\n"
"      k.prepend(Module.new do\n"
"        def update\n"
"          return super if $vita_prof_win\n"
"          $vita_prof_win = true\n"
"          t0 = vita_prof_now\n"
"          begin\n"
"            super\n"
"          ensure\n"
"            $vita_prof_win = false\n"
"            vita_prof_win(self.class, t0)\n"
"          end\n"
"        end\n"
"      end)\n"
"    end\n"
"  end\n"
"  Scene_Base.prepend(Module.new do\n"
"    def update_all_windows\n"
"      instance_variables.each { |v| w = instance_variable_get(v); VitaProfWin.instrument(w.class) if w.is_a?(Window) }\n"
"      super\n"
"    end\n"
"  end)\n"
"  Game_Map.prepend(Module.new { def setup(map_id); r = super; vita_prof_map(map_id, @events.size); r; end })\n"
#ifdef MKXP_VITA_PROF_SUB
"  # d77 (MKXP_VITA_PROF_SUB): per-method update profiler (vita_diag.cpp, PERF sub_top). DEEP classes\n"
"  # (one or a few instances per frame): every method whose name contains \"update\" (inherited ones\n"
"  # too, predicates excluded); TOP classes (many instances): #update only. One prepended module per\n"
"  # class, def + zsuper as the other hooks (d76), visibility and ruby2_keywords kept.\n"
"  module VitaProfSub\n"
"    DEEP = %i[Spriteset_Battle Spriteset_Map Spriteset_Weather Sprite_Battler Sprite_Timer Game_Map Game_Player\n"
"              Game_Followers Game_Follower Game_Troop Game_Screen Scene_Battle Scene_Map]\n"
"    TOP = %i[Sprite_Picture Sprite_Popup Sprite_Character Game_Event Game_Picture]\n"
"    # d78: Sprite_Battler also its animation / effect / MOG motion steps (d77 battles on 282: 4.8 ms/frame\n"
"    # of Sprite_Battler#update own time outside the update_* methods).\n"
"    EXTRA = { Sprite_Battler: /setup|start|animation|motion|effect|execute|popup|refresh|create|make|dispose/ }\n"
"    CORE = [Kernel, Object, BasicObject, Sprite, Plane, Window, Viewport, Bitmap]\n"
"    def self.install(c, names)\n"
"      src = +''\n"
"      privs = []\n"
"      names.each do |n|\n"
"        slot = vita_prof_sub_slot(\"#{c.name}##{n}\")\n"
"        break unless slot\n"
"        privs << n if c.private_method_defined?(n)\n"
"        src << \"def #{n}(*a, &b); t0 = vita_prof_sub_enter; begin; super; ensure; vita_prof_sub(#{slot}, t0); end; end\\n\"\n"
"        src << \"ruby2_keywords(:#{n})\\n\"\n"
"      end\n"
"      return if src.empty?\n"
"      m = Module.new\n"
"      m.module_eval(src)\n"
"      privs.each { |n| m.send(:private, n) }\n"
"      c.prepend(m)\n"
"    rescue Exception => e\n"
"      vita_diag_mark('HOOK_FAILED', \"prof_sub #{c}: #{e.class}: #{e.message}\")\n"
"    end\n"
"    def self.update_methods(c)\n"
"      extra = EXTRA[c.name.to_sym]\n"
"      (c.instance_methods + c.private_instance_methods).uniq.select do |n|\n"
"        s = n.to_s\n"
"        (s.include?('update') || (extra && s.match?(extra))) && s.match?(/\\A[a-z_][A-Za-z0-9_]*\\z/) &&\n"
"          !CORE.include?(c.instance_method(n).owner)\n"
"      end.sort\n"
"    end\n"
"    def self.run(deep, top)\n"
"      deep.each { |cn| c = Object.const_defined?(cn) ? Object.const_get(cn) : nil; install(c, update_methods(c)) if c.is_a?(Class) }\n"
"      top.each { |cn| c = Object.const_defined?(cn) ? Object.const_get(cn) : nil; install(c, [:update]) if c.is_a?(Class) && c.method_defined?(:update) }\n"
"    end\n"
"  end\n"
"  VitaProfSub.run(VitaProfSub::DEEP, VitaProfSub::TOP)\n"
#endif
#endif
"  # Outermost: arm the BATTLE phase before command_301 runs (its span ends after the battle).\n"
"  Game_Interpreter.prepend(Module.new { def command_301; vita_diag_arm_battle; super; end })\n"
"  vita_diag_mark('ruby_hooks_installed', nil)\n"
#endif
"  VITA_DIAG_HOOKS\n"
#endif
#ifdef MKXP_VITA_SLOW_LOAD_LOG
"  # LIGHT DIAGNOSTIC (MKXP_VITA_SLOW_LOAD_LOG): outermost Cache.load_bitmap calls >= 300 ms.\n"
"  Cache.singleton_class.prepend(Module.new do\n"
"    def load_bitmap(folder_name, filename, hue = 0)\n"
"      return super if $vita_slow_load_in\n"
"      $vita_slow_load_in = true\n"
"      # Same file loaded >= 8 times in one frame (d36: Graphics/System/Window, 2-3 s stalls): who calls it.\n"
"      key = \"#{folder_name}#{filename}\"\n"
"      if $vita_rl_frame == Graphics.frame_count && $vita_rl_key == key\n"
"        $vita_rl_n += 1\n"
"        if $vita_rl_n == 8\n"
"          File.open('" VITA_GAME_ROOT "slow_load.log', 'a') { |f| f.puts \"REPEATED_LOAD frame=#{Graphics.frame_count} path=#{key} scene=#{SceneManager.scene.class} map=#{($game_map.map_id rescue '?')} caller=#{caller(1, 12).join(' | ')}\" } rescue nil\n"
"        end\n"
"      else\n"
"        $vita_rl_frame = Graphics.frame_count\n"
"        $vita_rl_key = key\n"
"        $vita_rl_n = 1\n"
"      end\n"
"      t0 = Time.now\n"
"      begin\n"
"        super\n"
"      ensure\n"
"        $vita_slow_load_in = false\n"
"        ms = ((Time.now - t0) * 1000).round\n"
"        if ms >= 300\n"
"          File.open('" VITA_GAME_ROOT "slow_load.log', 'a') { |f| f.puts \"SLOW_LOAD frame=#{Graphics.frame_count} ms=#{ms} path=#{folder_name}#{filename} hue=#{hue} scene=#{SceneManager.scene.class}\" } rescue nil\n"
"        end\n"
"      end\n"
"    end\n"
"  end)\n"
#endif
#ifdef MKXP_VITA_SCENE_BREADCRUMB
"  # DIAGNOSTIC ONLY (MKXP_VITA_SCENE_BREADCRUMB): one line per scene change / save step / flash to\n"
"  # breadcrumb.log (open+close each time), to locate native crashes that leave no Ruby trace.\n"
"  TOPLEVEL_BINDING.eval(<<~'VITA_CRUMB', 'vita_crumb', 1)\n"
"  module VitaCrumb\n"
#if defined(MKXP_VITA_CRUMB_BUFFER)
"    # Perf fix (MKXP_VITA_CRUMB_BUFFER): lines kept in RAM and appended to the file with one write\n"
"    # (open + write + close) at most once per 60 frames, at 256 lines, at exit and before the error\n"
"    # screen. d79 block profiler: with a write per line (CRUMB_PERSIST, sync) a save made ~67 writes,\n"
"    # 5-60 ms each on the memory card, 0.3-1.0 s of the ~1-2 s stalls around the save screen.\n"
"    # A hard crash loses at most the last second of lines.\n"
"    PATH = '" VITA_GAME_ROOT "breadcrumb.log'\n"
"    @buf = []\n"
"    @t = 0\n"
"    def self.log(s)\n"
"      @buf << \"f=#{Graphics.frame_count} #{s}\\n\"\n"
"      flush if @buf.size >= 256\n"
"    end\n"
"    def self.tick\n"
"      flush if !@buf.empty? && Graphics.frame_count - @t >= 60\n"
"    end\n"
"    def self.flush\n"
"      @t = Graphics.frame_count\n"
"      return if @buf.empty?\n"
"      data = @buf.join\n"
"      @buf.clear\n"
"      File.open(PATH, 'a') { |f| f.write(data) }\n"
"    rescue Exception\n"
"    end\n"
#elif defined(MKXP_VITA_CRUMB_PERSIST)
"    # Perf fix (MKXP_VITA_CRUMB_PERSIST): the file stays open with sync (each line written at once,\n"
"    # so it is on the card if the game crashes) instead of open/append/close per line (~15 ms on\n"
"    # the memory card: d60, most of the remaining 0.3 s of the save screen). Reopened after an error\n"
"    # (e.g. a descriptor lost across standby).\n"
"    def self.log(s)\n"
"      @f ||= File.open('" VITA_GAME_ROOT "breadcrumb.log', 'a').tap { |f| f.sync = true }\n"
"      @f.puts \"f=#{Graphics.frame_count} #{s}\"\n"
"    rescue Exception\n"
"      (@f.close rescue nil) if @f\n"
"      @f = nil\n"
"    end\n"
#else
"    def self.log(s)\n"
"      File.open('" VITA_GAME_ROOT "breadcrumb.log', 'a') { |f| f.puts \"f=#{Graphics.frame_count} #{s}\" }\n"
"    rescue Exception\n"
"    end\n"
#endif
"    def self.wrap(owner, meth, tag)\n"
"      return unless owner.method_defined?(meth) || owner.private_method_defined?(meth)\n"
"      priv = owner.private_method_defined?(meth)\n"
"      # def + zsuper (d76): define_method { |*a, &b| super(*a, &b) } left one callinfo + callcache per\n"
"      # call on the Vita (d75 census: update_all_windows, update_events, update_interpreter, ...).\n"
"      m = Module.new\n"
"      m.const_set(:VITA_TAG, tag)\n"
"      m.module_eval(<<~'VITA_M'.sub('__M__', meth.to_s))\n"
"        def __M__(*a, &b)\n"
"          VitaCrumb.log(\"#{VITA_TAG} BEGIN #{a.map { |x| x.is_a?(Class) ? x.name : x.class.name }.join(',')}\")\n"
"          r = super\n"
"          VitaCrumb.log(\"#{VITA_TAG} END\")\n"
"          r\n"
"        end\n"
"      VITA_M\n"
"      m.send(:ruby2_keywords, meth)\n"
"      m.send(:private, meth) if priv\n"
"      owner.prepend(m)\n"
"    end\n"
"  end\n"
#ifdef MKXP_VITA_CRUMB_BUFFER
"  Scene_Base.prepend(Module.new { def update_basic; super; VitaCrumb.tick; end })\n"
"  at_exit { VitaCrumb.flush }\n"
#endif
"  VitaCrumb.log('BOOT')\n"
"  VitaCrumb.wrap(SceneManager.singleton_class, :call, 'SceneManager.call')\n"
"  VitaCrumb.wrap(SceneManager.singleton_class, :goto, 'SceneManager.goto')\n"
"  VitaCrumb.wrap(SceneManager.singleton_class, :return, 'SceneManager.return')\n"
"  VitaCrumb.wrap(SceneManager.singleton_class, :snap_for_background, 'snap_for_background')\n"
"  VitaCrumb.wrap(Scene_Base, :start, 'Scene#start')\n"
"  VitaCrumb.wrap(Scene_Base, :post_start, 'Scene#post_start')\n"
"  VitaCrumb.wrap(Scene_Base, :terminate, 'Scene#terminate')\n"
"  VitaCrumb.wrap(Scene_MenuBase, :create_background, 'create_background') if defined?(Scene_MenuBase)\n"
"  VitaCrumb.wrap(Scene_File, :create_savefile_windows, 'create_savefile_windows') if defined?(Scene_File)\n"
"  VitaCrumb.wrap(Scene_File, :create_help_window, 'create_help_window') if defined?(Scene_File)\n"
"  VitaCrumb.wrap(DataManager.singleton_class, :save_game, 'save_game')\n"
"  VitaCrumb.wrap(DataManager.singleton_class, :load_game, 'load_game')\n"
"  VitaCrumb.wrap(DataManager.singleton_class, :load_header, 'load_header')\n"
"  VitaCrumb.wrap(Game_Interpreter, :command_224, 'cmd224 flash')\n"
"  VitaCrumb.wrap(Game_Interpreter, :command_352, 'cmd352 save')\n"
"  VitaCrumb.wrap(Graphics.singleton_class, :freeze, 'Graphics.freeze')\n"
"  VitaCrumb.wrap(Graphics.singleton_class, :transition, 'Graphics.transition')\n"
"  VitaCrumb.wrap(Graphics.singleton_class, :snap_to_bitmap, 'Graphics.snap_to_bitmap')\n"
"  VitaCrumb.wrap(Bitmap, :blur, 'Bitmap#blur')\n"
"  VITA_CRUMB\n"
#endif
#ifdef MKXP_VITA_DISPLAY_MEMO
"  TOPLEVEL_BINDING.eval(<<~'VITA_DISPLAY_MEMO', 'vita_display_memo', 1)\n"
"  # Perf fix (MKXP_VITA_DISPLAY_MEMO, d86): Game_Map#display_x / display_y computed again only when\n"
"  # @display_x / @display_y change. LISA's \"Event Jitter Fix\" defines them as\n"
"  # (@display_x * 32).floor.to_f / 32: 3 Floats per call on 32-bit Ruby, called by every\n"
"  # adjust_x/adjust_y (screen_x, near_the_screen?, ...): d85, ~120 of the ~190 Floats allocated per\n"
"  # frame on the maps, which drive the minor GCs (~18 ms each, one every ~5 s). Installed only if\n"
"  # both methods are, by their bytecode, a pure function of that one instance variable (reads only\n"
"  # it, numeric literals, + - * /, floor, to_f; no arguments): then the same value (the same Float\n"
"  # object) is returned for an eql? @display_x. The cache lives here, not in Game_Map (saved in the\n"
"  # save files), with the map object it belongs to. qa.log DISPLAY_MEMO.\n"
"  module VitaDisplayMemo\n"
"    OPS = %i[opt_mult opt_div opt_plus opt_minus putobject_INT2FIX_0_ putobject_INT2FIX_1_ leave nop]\n"
"    SENDS = %i[floor to_f]\n"
"    def self.pure?(um, ivar)\n"
"      return false unless um && um.parameters.empty?\n"
"      iseq = RubyVM::InstructionSequence.of(um)\n"
"      return false unless iseq\n"
"      read = false\n"
"      iseq.to_a.last.each do |ins|\n"
"        next unless ins.is_a?(Array)\n"
"        case ins[0]\n"
"        when :getinstancevariable then return false unless ins[1] == ivar; read = true\n"
"        when :putobject then return false unless ins[1].is_a?(Integer) || ins[1].is_a?(Float)\n"
"        when :opt_send_without_block then return false unless SENDS.include?(ins[1][:mid])\n"
"        else return false unless OPS.include?(ins[0])\n"
"        end\n"
"      end\n"
"      read\n"
"    end\n"
"    def self.own(m)\n"
"      um = (Game_Map.instance_method(m) rescue nil)\n"
"      um = um.super_method while um && !um.owner.equal?(Game_Map)\n"
"      um\n"
"    end\n"
"    @map = nil; @kx = nil; @vx = nil; @ky = nil; @vy = nil; @hx = false; @hy = false\n"
"    class << self\n"
"      attr_accessor :map, :kx, :vx, :ky, :vy, :hx, :hy\n"
"    end\n"
"    module Hooks\n"
"      def display_x\n"
"        m = VitaDisplayMemo\n"
"        k = @display_x\n"
"        if m.map.equal?(self)\n"
"          return m.vx if m.hx && k.eql?(m.kx)\n"
"        else\n"
"          m.map = self; m.hx = false; m.hy = false\n"
"        end\n"
"        v = super\n"
"        m.kx = k; m.vx = v; m.hx = true\n"
"        v\n"
"      end\n"
"      def display_y\n"
"        m = VitaDisplayMemo\n"
"        k = @display_y\n"
"        if m.map.equal?(self)\n"
"          return m.vy if m.hy && k.eql?(m.ky)\n"
"        else\n"
"          m.map = self; m.hx = false; m.hy = false\n"
"        end\n"
"        v = super\n"
"        m.ky = k; m.vy = v; m.hy = true\n"
"        v\n"
"      end\n"
"    end\n"
"    def self.install\n"
"      ux = own(:display_x); uy = own(:display_y)\n"
"      return 'NOT INSTALLED (display_x/display_y not a pure function of @display_x/@display_y)' unless pure?(ux, :@display_x) && pure?(uy, :@display_y)\n"
"      Game_Map.prepend(Hooks)\n"
"      'INSTALLED'\n"
"    end\n"
"  end\n"
"  r = (VitaDisplayMemo.install rescue \"NOT INSTALLED (#{$!.class}: #{$!.message})\")\n"
"  File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"DISPLAY_MEMO #{r}\" } rescue nil\n"
"  VITA_DISPLAY_MEMO\n"
#endif
#ifdef MKXP_VITA_INTERP_HYBRID
"  TOPLEVEL_BINDING.eval(<<~'VITA_INTERP_HYBRID', 'vita_interp_hybrid', 1)\n"
"  # Perf fix (MKXP_VITA_INTERP_HYBRID): event commands that can never suspend run without a Fiber.\n"
"  # VX Ace runs every interpreter list in a Fiber (run: wait_for_message, the commands, Fiber.yield,\n"
"  # @fiber = nil); a parallel process that finishes its list gets a new Fiber the frame after next\n"
"  # (d81: LISA's common events 2 \"Get off Bike\" and 3 \"Rope\" every other frame, plus the parallel\n"
"  # map events; on the Vita each Fiber is a thread and every switch a thread handoff).\n"
"  # Here create_fiber leaves a marker; update runs the commands from @index directly while they are\n"
"  # in SAFE (040_Game_Interpreter: no Fiber.yield / wait / run / eval reachable; 111 and 122 except\n"
"  # their script forms); at the first other command (or a busy message window, where run would wait\n"
"  # at once) it creates the real Fiber { run }, which carries on from the same @index. After the\n"
"  # last command the marker TAIL stands for the suspended Fiber.yield: the next update clears\n"
"  # @fiber, as the Fiber would. Installed only if run / update / create_fiber / wait_for_message /\n"
"  # execute_command and the SAFE commands are still those of Game_Interpreter (qa.log\n"
"  # INTERP_HYBRID); a SAFE command redefined by a later script is left to the Fiber.\n"
"  # Host test: tools/hosttests/interp-hybrid/ (LISA's Game_Interpreter, random lists, frame traces).\n"
"  module VitaInterpHybrid\n"
"    LAZY = Object.new.freeze\n"
"    TAIL = Object.new.freeze\n"
"    SAFE_CODES = [0, 108, 111, 112, 113, 115, 118, 119, 121, 122, 123, 124, 125, 126, 127, 128, 129,\n"
"                  132, 133, 134, 135, 136, 137, 138, 202, 203, 206, 211, 214, 216, 231, 233, 235,\n"
"                  241, 242, 243, 244, 245, 246, 249, 250, 251, 281, 282, 283, 284, 285, 311, 312,\n"
"                  313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 331, 332, 333, 334,\n"
"                  335, 336, 337, 401, 402, 403, 404, 408, 411, 412, 413, 601, 602, 603, 604]\n"
"    SAFE = []\n"
"    # The Game_Interpreter definition of m (under any prepended module), or nil.\n"
"    def self.own(m)\n"
"      gi = Game_Interpreter\n"
"      return nil unless gi.method_defined?(m) || gi.private_method_defined?(m)\n"
"      um = gi.instance_method(m)\n"
"      um = um.super_method while um && !um.owner.equal?(gi)\n"
"      um\n"
"    end\n"
"    # $vita_interp_snap: Game_Interpreter's methods right after its own script section (main.cpp,\n"
"    # script loop). A method still equal to that definition was not redefined by a later script.\n"
"    def self.original?(m)\n"
"      snap = $vita_interp_snap\n"
"      um = own(m)\n"
"      snap.key?(m) ? (um && um == snap[m]) : um.nil?\n"
"    end\n"
"    def self.install\n"
"      return 'NOT INSTALLED (no Game_Interpreter snapshot)' unless $vita_interp_snap.is_a?(Hash)\n"
"      bad = %i[run update create_fiber wait_for_message running? clear setup marshal_load execute_command].reject { |m| original?(m) }\n"
"      return \"NOT INSTALLED (redefined: #{bad.join(',')})\" unless bad.empty?\n"
"      skipped = []\n"
"      SAFE_CODES.each { |code| original?(:\"command_#{code}\") ? (SAFE[code] = true) : (skipped << code) }\n"
"      Game_Interpreter.prepend(Hooks)\n"
"      skipped.empty? ? 'INSTALLED' : \"INSTALLED (left to the Fiber: #{skipped.join(',')})\"\n"
"    end\n"
"    module Hooks\n"
"      def create_fiber\n"
"        @fiber = LAZY if @list\n"
"      end\n"
"      def update\n"
"        f = @fiber\n"
"        if f.equal?(LAZY)\n"
"          vita_run_direct\n"
"        elsif f.equal?(TAIL)\n"
"          @fiber = nil\n"
"        else\n"
"          super\n"
"        end\n"
"      end\n"
"      # run without the Fiber while the next command cannot suspend; same loop as run.\n"
"      def vita_run_direct\n"
"        return vita_to_fiber if $game_message.busy?\n"
"        while (cmd = @list[@index])\n"
"          code = cmd.code\n"
"          unless SAFE[code] && !(code == 111 && cmd.parameters[0] == 12) && !(code == 122 && cmd.parameters[3] == 4)\n"
"            return vita_to_fiber\n"
"          end\n"
"          execute_command\n"
"          @index += 1\n"
"        end\n"
"        @fiber = TAIL\n"
"      end\n"
"      def vita_to_fiber\n"
"        @fiber = Fiber.new { run }\n"
"        @fiber.resume\n"
"      end\n"
"    end\n"
"  end\n"
"  r = (VitaInterpHybrid.install rescue \"NOT INSTALLED (#{$!.class}: #{$!.message})\")\n"
"  File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"INTERP_HYBRID #{r}\" } rescue nil\n"
"  VITA_INTERP_HYBRID\n"
#endif
#ifdef MKXP_VITA_ANIM_PREFETCH
"  TOPLEVEL_BINDING.eval(<<~'VITA_ANIM_PREFETCH', 'vita_anim_prefetch', 1)\n"
"  # Perf fix (MKXP_VITA_ANIM_PREFETCH): at BattleManager.setup (before the battle transition) the\n"
"  # animation images this battle can show are queued for the background read-ahead\n"
"  # (vita-prefetch.cpp): the party's skills, attack skill and weapon animations, the enemies'\n"
"  # action skills. Images already in Cache are skipped; at most 24 files. Scene_Battle#terminate\n"
"  # drops what was not used. Only file reads move to another core; decoding and the Bitmap stay\n"
"  # on the main thread as before.\n"
"  module VitaAnimPrefetch\n"
"    # Animation ids this battle can show: attacks (actors' weapons and attack skill, enemies'\n"
"    # weapons via Yanfly) first, then the enemies' action skills, then the actors' skills.\n"
"    def self.ids\n"
"      atk = []; foe = []; own = []\n"
"      $game_party.battle_members.each do |a|\n"
"        atk << a.atk_animation_id1 << a.atk_animation_id2\n"
"        atk << ($data_skills[a.attack_skill_id].animation_id rescue 0)\n"
"        a.skills.each { |s| own << s.animation_id }\n"
"      end\n"
"      $game_troop.members.each do |e|\n"
"        atk << e.atk_animation_id1 << e.atk_animation_id2 if e.respond_to?(:atk_animation_id2)   # Yanfly: enemy weapons\n"
"        e.enemy.actions.each { |ac| s = $data_skills[ac.skill_id]; foe << s.animation_id if s }\n"
"      end\n"
"      (atk + foe + own).uniq.select { |i| i.is_a?(Integer) && i > 0 }\n"
"    end\n"
"    def self.start\n"
"      anims = ids.map { |i| $data_animations[i] }.compact\n"
"      names = anims.flat_map { |an| [an.animation1_name, an.animation2_name] }.reject { |n| n.nil? || n.empty? }.uniq\n"
"      n = 0\n"
"      names.each do |nm|\n"
"        key = \"Graphics/Animations/#{nm}\"\n"
"        next if (Cache.include?(key) rescue false)\n"
"        ok = defined?(vita_predecode_image) ? vita_predecode_image(key) : vita_prefetch_image(key)\n"
"        n += 1 if ok\n"
"        break if n >= 24\n"
"      end\n"
"      if defined?(vita_predecode_sound)\n"
"        # MKXP_VITA_ANIM_PREDECODE: the SEs of the same animations (RPG::SE#play -> \"Audio/SE/\" + name)\n"
"        ses = anims.flat_map { |an| an.timings.map { |t| t.se && t.se.name } }.reject { |s| s.nil? || s.empty? }.uniq\n"
"        ses.first(32).each { |nm| vita_predecode_sound(\"Audio/SE/#{nm}\") }\n"
"      end\n"
"      n\n"
"    end\n"
"  end\n"
"  BattleManager.singleton_class.prepend(Module.new do\n"
"    def setup(*a)\n"
"      r = super\n"
"      (VitaAnimPrefetch.start rescue nil)\n"
"      r\n"
"    end\n"
"  end)\n"
"  Scene_Battle.prepend(Module.new { def terminate; super; vita_prefetch_clear; vita_predecode_clear if defined?(vita_predecode_clear); end })\n"
"  VITA_ANIM_PREFETCH\n"
#endif
#ifdef MKXP_VITA_GALV_SHADOW_KEEP
"  TOPLEVEL_BINDING.eval(<<~'VITA_GALV_SHADOW', 'vita_galv_shadow', 1)\n"
"  # Perf fix (MKXP_VITA_GALV_SHADOW_KEEP, d90): Galv's Character Effects - Spriteset_Map#refresh_effects\n"
"  # without recreating identical shadow sprites. d88: maps 268/269 (\"Fancy Cave 2/3\") at 5-7 fps:\n"
"  # their parallel event 3 runs char_effects(1,true) at every restart (every other frame), and\n"
"  # char_effects always calls refresh_effects = dispose every effect sprite + create them again:\n"
"  # ~40 Sprite_Shadow (events x 3 light sources + player) 30 times a second.\n"
"  # When only shadows are on (reflect/mirror/icon off, no reflect/mirror/icon sprite), the shadow\n"
"  # flicker option is off (wave_amp is only set at creation), and the sprites create_effects would\n"
"  # make are, in order, the same (character, light source) pairs as the current ones, with none\n"
"  # disposed and none showing an animation or a balloon, the current sprites are kept: everything a\n"
"  # Sprite_Shadow shows is recomputed from the character and the light at each update (bitmap and\n"
"  # src_rect, position, opacity and angle, mirror, blend, visibility; color and z fixed); the\n"
"  # drawing order of new sprites (end of their z group, in creation order) is reproduced by\n"
"  # re-inserting each kept sprite with a z change and back. Otherwise the original runs.\n"
"  # Host test with Galv's script and LISA's Sprite_Character: tools/hosttests/galv-shadow/.\n"
"  module VitaGalvShadow\n"
"    def self.plan\n"
"      m = $game_map\n"
"      list = []\n"
"      return list if m.light_source.empty?\n"
"      m.light_source.count.times do |s|\n"
"        m.events.values.each { |e| list << [e, s] if e.shadow }\n"
"        $game_player.followers.each { |f| list << [f, s] if f.shadow }\n"
"        list << [$game_player, s] if $game_player.shadow\n"
"        m.vehicles.each { |v| list << [v, s] if v.shadow }\n"
"      end\n"
"      list\n"
"    end\n"
"    module Hooks\n"
"      def refresh_effects\n"
"        m = $game_map\n"
"        fx = m.char_effects\n"
"        if @shadow_sprites && @reflect_sprites && @mirror_sprites && @icon_sprites &&\n"
"           fx[1] && !fx[0] && !fx[2] && !fx[3] && !(m.shadow_options && m.shadow_options[2]) &&\n"
"           @reflect_sprites.empty? && @mirror_sprites.empty? && @icon_sprites.empty?\n"
"          want = VitaGalvShadow.plan\n"
"          cur = @shadow_sprites\n"
"          if want.size == cur.size && !cur.empty? &&\n"
"             cur.each_with_index.all? { |sp, i| !sp.disposed? && sp.character.equal?(want[i][0]) && sp.instance_variable_get(:@source) == want[i][1] && !sp.animation? && !sp.instance_variable_get(:@balloon_sprite) }\n"
"            # As new sprites: angle back to a new Sprite's 0, end of the z group in creation order, and\n"
"            # what lasts of the update Sprite_Character#initialize runs at creation: get_angle (it\n"
"            # leaves the angle unchanged while the shadow is invisible); everything else that update\n"
"            # writes (bitmap, src_rect, position, opacity, mirror, blend, visibility) is written again\n"
"            # by the spriteset's update later in the frame. d90b ran the whole update here: ~20 extra\n"
"            # sprite updates per frame on maps 268/269.\n"
"            cur.each { |sp| sp.angle = 0; z = sp.z; sp.z = z + 1; sp.z = z; sp.get_angle }\n"
"            return\n"
"          end\n"
"        end\n"
"        super\n"
"      end\n"
"    end\n"
"    def self.install\n"
"      return 'NOT INSTALLED (no Galv Character Effects)' unless defined?(Sprite_Shadow) && Sprite_Shadow.method_defined?(:get_angle) && Spriteset_Map.method_defined?(:refresh_effects) &&\n"
"                                                                Spriteset_Map.method_defined?(:create_effects) && Game_Map.method_defined?(:light_source)\n"
"      Spriteset_Map.prepend(Hooks)\n"
"      'INSTALLED'\n"
"    end\n"
"  end\n"
"  r = (VitaGalvShadow.install rescue \"NOT INSTALLED (#{$!.class}: #{$!.message})\")\n"
"  File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"GALV_SHADOW_KEEP #{r}\" } rescue nil\n"
"  VITA_GALV_SHADOW\n"
#endif
#ifdef MKXP_VITA_SAVE_WRITE_ONCE
"  TOPLEVEL_BINDING.eval(<<~'VITA_SAVE_ONCE', 'vita_save_once', 1)\n"
"  # Perf fix (MKXP_VITA_SAVE_WRITE_ONCE): Marshal.dump(obj, file) builds the dump in memory and writes\n"
"  # it with one IO#write (same bytes; binmode as Marshal does for a port). Marshal flushes its buffer\n"
"  # to the port every few KiB: d79, a save (DataManager: two Marshal.dump into the file) made ~70\n"
"  # writes, ~0.5 s on the memory card. If the dump raises, nothing is written (the original left a\n"
"  # partial file; DataManager.save_game deletes it in both cases).\n"
"  Marshal.singleton_class.prepend(Module.new do\n"
"    def dump(obj, *rest)\n"
"      port = rest[0]\n"
"      return super unless port.is_a?(File) && rest.size <= 2\n"
"      data = rest.size == 2 ? super(obj, rest[1]) : super(obj)\n"
"      port.binmode if port.respond_to?(:binmode)\n"
"      port.write(data)\n"
"      port\n"
"    end\n"
"  end)\n"
"  VITA_SAVE_ONCE\n"
#endif
#ifdef MKXP_VITA_OFFSCREEN_SPRITES
"  TOPLEVEL_BINDING.eval(<<~'VITA_OFFSCREEN', 'vita_offscreen', 1)\n"
"  # PERF FIX (MKXP_VITA_OFFSCREEN_SPRITES): character sprites far off screen are not updated.\n"
"  # d39/d40: Sprite_Character#update ~0.16 ms per sprite per frame (Float math allocates on 32-bit\n"
"  # Ruby), 100-180 sprites on the busy maps. Events (game logic) are untouched: only the sprite of a\n"
"  # character >= 3 tiles (+ its cell size) outside the screen skips its update, and only when no\n"
"  # animation/balloon is running or requested (an event waiting for one must not stall), on maps\n"
"  # that do not loop. The last full update happens once the character is already out, so the\n"
"  # sprite is left off screen. Toggle at runtime: L+R+START ($vita_offscreen_skip, qa.log).\n"
"  $vita_offscreen_skip = true\n"
"  $vita_off_ok = false\n"
"  $vita_skip_n = 0\n"
"  Spriteset_Map.prepend(Module.new do\n"
"    def update\n"
"      m = $game_map\n"
"      if $vita_offscreen_skip && !m.loop_horizontal? && !m.loop_vertical?\n"
"        $vita_off_x0 = m.display_x.to_i\n"
"        $vita_off_y0 = m.display_y.to_i\n"
"        $vita_off_w = m.screen_tile_x\n"
"        $vita_off_h = m.screen_tile_y\n"
"        $vita_off_ok = true\n"
"      end\n"
"      super\n"
"    ensure\n"
"      $vita_off_ok = false\n"
"    end\n"
"  end)\n"
"  # MKXP_VITA_SPRITE_FAST_NATIVE: the same update in C (vita-sprite-fast-native.cpp).\n"
"  if defined?(VitaOffscreenNative)\n"
"    # Character readers that are, in every character class, Game_CharacterBase's own attr method\n"
"    # for that name (owner Game_CharacterBase: no override in a subclass or prepended module; no\n"
"    # bytecode: attr_reader/attr_accessor; original_name = the name: not an alias of another attr):\n"
"    # it returns @name, so the C code reads that instance variable directly. (UnboundMethod#==\n"
"    # between a subclass's and the base's method is false on Ruby 3.1, hence the explicit checks.)\n"
"    vita_cb_classes = ObjectSpace.each_object(Class).select { |k| k <= Game_CharacterBase }\n"
"    vita_direct = %w[x y real_x real_y pattern direction opacity blend_type bush_depth transparent priority_type\n"
"                     animation_id balloon_id].select do |m|\n"
"      vita_cb_classes.all? do |k|\n"
"        um = (k.instance_method(m) rescue nil)\n"
"        um && um.owner == Game_CharacterBase && RubyVM::InstructionSequence.of(um).nil? && um.original_name == m.to_sym\n"
"      end\n"
"    end\n"
"    vita_sprite_fast_native_bind(Sprite_Character, Game_CharacterBase, vita_direct)\n"
"    File.open('" VITA_GAME_ROOT "qa.log', 'a') { |f| f.puts \"SPRITE_FAST_NATIVE direct=#{vita_direct.join(',')}\" } rescue nil\n"
"    Sprite_Character.prepend(VitaOffscreenNative)\n"
"  else\n"
"  Sprite_Character.prepend(Module.new do\n"
"    def vita_offscreen?\n"
"      c = @character\n"
"      return false unless c && instance_of?(Sprite_Character)\n"
"      return false if @balloon_sprite || animation? || c.animation_id > 0 || c.balloon_id > 0\n"
"      mx = (@cw || 32) / 64 + 3\n"
"      my = (@ch || 32) / 32 + 3\n"
"      tx = c.x - $vita_off_x0\n"
"      ty = c.y - $vita_off_y0\n"
"      tx < -mx || tx > $vita_off_w + mx || ty < -my || ty > $vita_off_h + my\n"
"    end\n"
"    def update\n"
"      if $vita_off_ok && vita_offscreen?\n"
"        if @vita_off_ready\n"
"          $vita_skip_n += 1\n"
"          @vita_off_skipped = true   # sprite left stale: no SPRITE_FAST snapshot this frame\n"
"          return\n"
"        end\n"
"        @vita_off_skipped = false\n"
"        super\n"
"        @vita_off_ready = true\n"
"      else\n"
"        @vita_off_skipped = false\n"
"        @vita_off_ready = false\n"
"        super\n"
"      end\n"
"    end\n"
"  end)\n"
"  end\n"
"  VITA_OFFSCREEN\n"
#endif
#ifdef MKXP_VITA_EVENT_FAST
"  TOPLEVEL_BINDING.eval(<<~'VITA_EVENT_FAST', 'vita_event_fast', 1)\n"
"  VITA_EVENT_FAST_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_EVENT_FAST): exact fast path of Game_Event#update for still events.\n"
"  # d41: 151 events on map 11 cost 12.9 ms/frame, 138 of them fixed decorations. For an event that\n"
"  # is not moving/jumping, not route-forced, move_type 0, not autorun/parallel, no step anime and\n"
"  # resting on its original pattern with anime_count 0, the whole update chain of the stock VX Ace\n"
"  # scripts (update_animation, update_stop, update_self_movement, check_event_trigger_auto,\n"
"  # interpreter) only does `@stop_count += 1 unless @locked`. Installed only if those methods are\n"
"  # still the stock ones (owner check); host test against LISA's scripts: tools/hosttests/event-fast/.\n"
"  # Toggled together with the off-screen sprite skip (L+R+START).\n"
"  $vita_event_fast = true\n"
"  $vita_ev_fast_n = 0\n"
"  ok = [[Game_Event, :update, Game_Event], [Game_Event, :update_stop, Game_Event],\n"
"        [Game_Event, :check_event_trigger_auto, Game_Event], [Game_Event, :update_self_movement, Game_Event],\n"
"        [Game_Character, :update, Game_CharacterBase], [Game_Character, :update_stop, Game_Character],\n"
"        [Game_CharacterBase, :update_stop, Game_CharacterBase], [Game_CharacterBase, :update_animation, Game_CharacterBase],\n"
"        [Game_CharacterBase, :update_anime_count, Game_CharacterBase]].all? do |c, m, owner|\n"
"    # Skip modules prepended in front of the method (our profiler/probe hooks: RGSS3 scripts are\n"
"    # Ruby 1.9 code and cannot prepend). d43: the profiler's Game_Event#update module failed the check.\n"
"    um = (c.instance_method(m) rescue nil)\n"
"    um = um.super_method while um && !um.owner.is_a?(Class)\n"
"    um && um.owner == owner\n"
"  end\n"
"  File.open(VITA_EVENT_FAST_LOG, 'a') { |f| f.puts \"EVENT_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED (scripts override the update chain)'}\" } rescue nil\n"
#ifdef MKXP_VITA_EVENT_NATIVE
"  # FIX (MKXP_VITA_EVENT_NATIVE): the same check in C (vita-event-native.cpp), installed only if\n"
"  # Integer#==, #!= and #+ are the core ones (the C side compares two Fixnums directly) and a self-test\n"
"  # on random states gives the same writes as the Ruby condition below. qa.log EVENT_NATIVE ...\n"
"  vita_ev_native = false\n"
"  if ok && defined?(VitaEventNative)\n"
"    core = Integer.instance_method(:==).owner == Integer && Integer.instance_method(:!=).owner == BasicObject &&\n"
"           Integer.instance_method(:+).owner == Integer\n"
"    ref = lambda do |e|\n"
"      if $vita_event_fast && e.instance_variable_get(:@interpreter).nil? && e.instance_variable_get(:@trigger) != 3 &&\n"
"         !e.instance_variable_get(:@move_route_forcing) && e.instance_variable_get(:@move_type) == 0 &&\n"
"         e.instance_variable_get(:@jump_count) == 0 && e.instance_variable_get(:@real_x) == e.instance_variable_get(:@x) &&\n"
"         e.instance_variable_get(:@real_y) == e.instance_variable_get(:@y) && !e.instance_variable_get(:@step_anime) &&\n"
"         e.instance_variable_get(:@pattern) == e.instance_variable_get(:@original_pattern) &&\n"
"         e.instance_variable_get(:@anime_count) == 0\n"
"        e.instance_variable_set(:@stop_count, e.instance_variable_get(:@stop_count) + 1) unless e.instance_variable_get(:@locked)\n"
"        $vita_ev_fast_n += 1\n"
"        true\n"
"      else\n"
"        false\n"
"      end\n"
"    end\n"
"    rng = Random.new(20261005)\n"
"    pick = ->(a) { a[rng.rand(a.size)] }\n"
"    bad = 0\n"
"    n0 = $vita_ev_fast_n\n"
"    2000.times do |k|\n"
"      a = Game_Event.allocate\n"
"      x = rng.rand(100); y = rng.rand(30)\n"
"      {:@interpreter => pick.([nil, nil, nil, :run]), :@trigger => pick.([0, 1, 2, 3, 4, nil]),\n"
"       :@move_route_forcing => pick.([false, false, true, nil]), :@move_type => pick.([0, 0, 0, 1, 3]),\n"
"       :@jump_count => pick.([0, 0, 0, 2]), :@x => x, :@y => y,\n"
"       :@real_x => pick.([x, x, x.to_f, x - 0.25]), :@real_y => pick.([y, y.to_f, y + 0.5]),\n"
"       :@step_anime => pick.([false, false, true]), :@pattern => pick.([1, 1, 0, 2]),\n"
"       :@original_pattern => pick.([1, 1, 2]), :@anime_count => pick.([0, 0, 0.0, 3, 1.5]),\n"
"       :@stop_count => pick.([0, 7, 2**40]), :@locked => pick.([false, false, true, nil])}.each { |iv, v| a.instance_variable_set(iv, v) }\n"
"      b = a.clone\n"
"      $vita_event_fast = (k % 50 != 0)\n"
"      r1 = ref.call(a)\n"
"      r2 = vita_event_native_check(b)\n"
"      bad += 1 unless r1 == r2 && a.instance_variables.all? { |iv| a.instance_variable_get(iv) == b.instance_variable_get(iv) }\n"
"    end\n"
"    $vita_event_fast = true\n"
"    fast_ok = $vita_ev_fast_n - n0\n"
"    $vita_ev_fast_n = 0\n"
"    vita_ev_native = core && bad == 0\n"
"    File.open(VITA_EVENT_FAST_LOG, 'a') { |f| f.puts \"EVENT_NATIVE #{vita_ev_native ? 'INSTALLED' : 'NOT INSTALLED'} selftest_mismatch=#{bad} fast_cases=#{fast_ok} core_integer=#{core}\" } rescue nil\n"
"    Game_Event.prepend(VitaEventNative) if vita_ev_native\n"
"  end\n"
"  if ok && !vita_ev_native\n"
#else
"  if ok\n"
#endif
"    Game_Event.prepend(Module.new do\n"
"      def update\n"
"        if $vita_event_fast && @interpreter.nil? && @trigger != 3 && !@move_route_forcing && @move_type == 0 &&\n"
"           @jump_count == 0 && @real_x == @x && @real_y == @y && !@step_anime && @pattern == @original_pattern &&\n"
"           @anime_count == 0\n"
"          @stop_count += 1 unless @locked\n"
"          $vita_ev_fast_n += 1\n"
"          return\n"
"        end\n"
"        super\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_EVENT_FAST\n"
#endif
#ifdef MKXP_VITA_SPRITE_FAST
"  TOPLEVEL_BINDING.eval(<<~'VITA_SPRITE_FAST', 'vita_sprite_fast', 1)\n"
"  VITA_SPRITE_FAST_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_SPRITE_FAST): exact fast path of Sprite_Character#update for an unchanged\n"
"  # sprite. d44: on-screen character sprites cost 10-14 ms/frame on the busy maps, almost all still\n"
"  # decorations. With no animation/balloon running or requested and the same character, graphic,\n"
"  # pattern, direction, opacity, blend type, bush depth, transparency, real position, not jumping,\n"
"  # same priority and the same raw map display position as at the last full update, the stock\n"
"  # chain (update_bitmap/src_rect/position/other/balloon, setup_new_effect, Sprite_Base#update)\n"
"  # writes the same values again: only Sprite#update (C: flash, wave) is still called. Installed\n"
"  # only if those methods are the stock ones (owner check through our prepended modules).\n"
"  # Host test with LISA's scripts: tools/hosttests/sprite-fast/. Toggled with L+R+START.\n"
"  $vita_sprite_fast = true\n"
"  $vita_sfast_n = 0\n"
#ifdef MKXP_VITA_SPRITE_SCROLL
"  $vita_sscroll_n = 0\n"
#endif
"  $vita_sf_on = false\n"
"  owner_of = lambda do |c, m|\n"
"    um = (c.instance_method(m) rescue nil)\n"
"    um = um.super_method while um && !um.owner.is_a?(Class)\n"
"    um && um.owner\n"
"  end\n"
"  ok = [[Sprite_Character, %i[update update_bitmap graphic_changed? update_src_rect update_position update_other update_balloon setup_new_effect]],\n"
"        [Sprite_Base, %i[update update_animation animation?]],\n"
"        [Game_CharacterBase, %i[screen_x screen_y screen_z shift_y jump_height jumping? object_character?]],\n"
"        [Game_Map, %i[adjust_x adjust_y display_x display_y]]].all? do |c, ms|\n"
"    ms.all? { |m| owner_of.call(c, m) == c }\n"
"  end\n"
"  ok &&= Sprite.instance_method(:update).owner == Sprite\n"
"  File.open(VITA_SPRITE_FAST_LOG, 'a') { |f| f.puts \"SPRITE_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED (scripts override the update chain)'}\" } rescue nil\n"
"  if ok\n"
"    Sprite.send(:alias_method, :vita_csprite_update, :update)\n"
"    Spriteset_Map.prepend(Module.new do\n"
"      def update\n"
"        m = $game_map\n"
"        $vita_sf_dx = m.instance_variable_get(:@display_x)\n"
"        $vita_sf_dy = m.instance_variable_get(:@display_y)\n"
"        $vita_sf_on = $vita_sprite_fast\n"
"        super\n"
"      ensure\n"
"        $vita_sf_on = false\n"
"      end\n"
"    end)\n"
"    # MKXP_VITA_SPRITE_FAST_NATIVE: the same update in C (vita-sprite-fast-native.cpp).\n"
"    if defined?(VitaSpriteFastNative)\n"
"      Sprite_Character.prepend(VitaSpriteFastNative)\n"
"    else\n"
"    Sprite_Character.prepend(Module.new do\n"
"      def update\n"
"        c = @character\n"
"        if $vita_sf_on && @vsf_ok && c && @vsf_c.equal?(c) && !@balloon_sprite && !animation? &&\n"
#ifdef MKXP_VITA_SPRITE_SCROLL
"           c.animation_id == 0 && c.balloon_id == 0 && !graphic_changed? && !c.jumping? &&\n"
"           @vsf_rx == c.real_x && @vsf_ry == c.real_y &&\n"
"           @vsf_pat == c.pattern && @vsf_dir == c.direction && @vsf_op == c.opacity &&\n"
"           @vsf_bt == c.blend_type && @vsf_bd == c.bush_depth && @vsf_tr == c.transparent &&\n"
"           @vsf_pt == c.priority_type\n"
"          if @vsf_dx == $vita_sf_dx && @vsf_dy == $vita_sf_dy\n"
"            $vita_sfast_n += 1\n"
"            vita_csprite_update\n"
"            return\n"
"          end\n"
"          # Perf fix (MKXP_VITA_SPRITE_SCROLL, d86): only the map display moved (the screen scrolls\n"
"          # while the player walks: d85, every visible sprite took the full update, ~110 us each,\n"
"          # 4-5 ms per frame). With every other input unchanged the stock chain rewrites the same\n"
"          # bitmap, src_rect, opacity, blend, bush depth and visibility; only x/y/z follow the\n"
"          # display: Sprite#update (C) + update_position, then the new display goes in the snapshot.\n"
"          $vita_sscroll_n += 1\n"
"          vita_csprite_update\n"
"          update_position\n"
"          @vsf_dx = $vita_sf_dx; @vsf_dy = $vita_sf_dy\n"
"          return\n"
"        end\n"
#else
"           c.animation_id == 0 && c.balloon_id == 0 && !graphic_changed? && !c.jumping? &&\n"
"           @vsf_dx == $vita_sf_dx && @vsf_dy == $vita_sf_dy && @vsf_rx == c.real_x && @vsf_ry == c.real_y &&\n"
"           @vsf_pat == c.pattern && @vsf_dir == c.direction && @vsf_op == c.opacity &&\n"
"           @vsf_bt == c.blend_type && @vsf_bd == c.bush_depth && @vsf_tr == c.transparent &&\n"
"           @vsf_pt == c.priority_type\n"
"          $vita_sfast_n += 1\n"
"          vita_csprite_update\n"
"          return\n"
"        end\n"
#endif

"        super\n"
"        # No snapshot while jumping: the first landed frame changes y with the same inputs (host test).\n"
"        if @vita_off_skipped || !$vita_sf_on || !c || !instance_of?(Sprite_Character) || c.jumping?\n"
"          @vsf_ok = false\n"
"        else\n"
"          @vsf_ok = true\n"
"          @vsf_c = c\n"
"          @vsf_dx = $vita_sf_dx; @vsf_dy = $vita_sf_dy\n"
"          @vsf_rx = c.real_x; @vsf_ry = c.real_y\n"
"          @vsf_pat = c.pattern; @vsf_dir = c.direction; @vsf_op = c.opacity\n"
"          @vsf_bt = c.blend_type; @vsf_bd = c.bush_depth; @vsf_tr = c.transparent\n"
"          @vsf_pt = c.priority_type\n"
"        end\n"
"      end\n"
"    end)\n"
"    end\n"
"  end\n"
"  VITA_SPRITE_FAST\n"
#endif
#ifdef MKXP_VITA_REGION_INCR
"  TOPLEVEL_BINDING.eval(<<~'VITA_REGION_INCR', 'vita_region_incr', 1)\n"
"  VITA_REGION_INCR_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_REGION_INCR): Galv's Region Effects recreates every region-effect sprite\n"
"  # (up to 20) at each step on an effect region (refresh_region_effects = dispose all + create all):\n"
"  # d46, 60-90 ms every 16 frames while walking. Here only the new effect gets a new sprite and the\n"
"  # expired ones are disposed; a kept sprite is moved to the end of its z group (z changed and\n"
"  # restored: mkxp-z re-inserts it after the equal-z elements, as a new sprite is inserted), in\n"
"  # effectlist order, so the drawing order is the same as with the recreated sprites. A sprite with\n"
"  # an animation or balloon running is recreated as before. Host test: tools/hosttests/region-incr/.\n"
"  # Toggled with L+R+START.\n"
"  $vita_region_incr = true\n"
"  ok = defined?(Spriteset_Map) && Spriteset_Map.method_defined?(:refresh_region_effects) &&\n"
"       Sprite_Character.method_defined?(:character)\n"
"  File.open(VITA_REGION_INCR_LOG, 'a') { |f| f.puts \"REGION_INCR #{ok ? 'INSTALLED' : 'NOT INSTALLED (no Galv Region Effects)'}\" } rescue nil\n"
"  if ok\n"
"    Spriteset_Map.prepend(Module.new do\n"
"      def refresh_region_effects\n"
"        old = @region_sprites\n"
"        return super unless $vita_region_incr && old.is_a?(Array) && $game_map.effectlist && $game_map.r_events\n"
"        pool = {}\n"
"        old.each do |s|\n"
"          next if s.nil? || s.disposed? || s.animation? || s.instance_variable_get(:@balloon_sprite)\n"
"          (pool[s.character] ||= []) << s\n"
"        end\n"
"        list = []\n"
"        $game_map.effectlist.each_with_index do |id, i|\n"
"          ev = $game_map.r_events[id]\n"
"          s = (pool[ev] && pool[ev].shift)\n"
"          if s\n"
"            z = s.z\n"
"            s.z = z + 1   # re-insert at the end of the z group, as a newly created sprite\n"
"            s.z = z\n"
"            list[i] = s\n"
"          else\n"
"            list[i] = Sprite_Character.new(@viewport1, ev)\n"
"          end\n"
"        end\n"
"        kept = {}\n"
"        list.each { |s| kept[s.object_id] = true if s }\n"
"        old.each { |s| s.dispose if s && !kept[s.object_id] && !s.disposed? }\n"
"        @region_sprites = list\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_REGION_INCR\n"
#endif
#ifdef MKXP_VITA_SPRITE_NATIVE
"  TOPLEVEL_BINDING.eval(<<~'VITA_SPC', 'vita_spc', 1)\n"
"  VITA_SPC_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_SPRITE_NATIVE): Sprite_Character#update_position in C (vita-sprite-native.cpp)\n"
"  # for the character classes whose screen_x/screen_y/screen_z gave exactly the same values as the\n"
"  # C code in a self-test here (Integer/Float positions, scrolled displays, jumps, priorities,\n"
"  # graphic names). Falls back to the Ruby method with an animation running (move_animation),\n"
"  # on looping maps (adjust_x/adjust_y wrap) and for any value the C code does not handle.\n"
"  begin\n"
"    owner_of = lambda do |c, m|\n"
"      um = (c.instance_method(m) rescue nil)\n"
"      um = um.super_method while um && !um.owner.is_a?(Class)\n"
"      um && um.owner\n"
"    end\n"
"    ok = owner_of.call(Sprite_Character, :update_position) == Sprite_Character &&\n"
"         owner_of.call(Sprite_Character, :move_animation) == Sprite_Character &&\n"
"         %i[adjust_x adjust_y display_x display_y].all? { |m| owner_of.call(Game_Map, m) == Game_Map }\n"
"    classes = {}\n"
"    if ok\n"
"      saved_map = $game_map\n"
"      rng = Random.new(1234)\n"
"      # One test map for all cases (d53: a new RPG::Map per case allocated 2400 Tables, ~82 MB of\n"
"      # C++ heap that Ruby's GC does not see -> bad_alloc in Table::resize -> abort at boot).\n"
"      gm = Game_Map.allocate\n"
"      gm.instance_variable_set(:@map, RPG::Map.new(17, 13))\n"
"      [Game_Player, Game_Follower, Game_Event, Game_Vehicle].each do |k|\n"
"        next unless %i[screen_x screen_y screen_z].all? { |m| owner_of.call(k, m) == Game_CharacterBase }\n"
"        good = true\n"
"        600.times do |i|\n"

"          dxv = [rng.rand(120), rng.rand(1200) / 32.0, rng.rand(12000) / 1000.0, -rng.rand(40) / 8.0].sample(random: rng)\n"
"          dyv = [rng.rand(30), rng.rand(300) / 32.0, rng.rand(3000) / 1000.0].sample(random: rng)\n"
"          gm.instance_variable_set(:@display_x, dxv)\n"
"          gm.instance_variable_set(:@display_y, dyv)\n"
"          $game_map = gm\n"
"          c = k.allocate\n"
"          x = rng.rand(130); y = rng.rand(33)\n"
"          c.instance_variable_set(:@real_x, [x, x.to_f, x - rng.rand(8) / 8.0, x + rng.rand(1000) / 997.0].sample(random: rng))\n"
"          c.instance_variable_set(:@real_y, [y, y.to_f, y - rng.rand(8) / 8.0].sample(random: rng))\n"
"          peak = [0, 5, 10, 13].sample(random: rng)\n"
"          c.instance_variable_set(:@jump_peak, peak)\n"
"          c.instance_variable_set(:@jump_count, peak > 0 ? rng.rand(peak * 2 + 1) : 0)\n"
"          c.instance_variable_set(:@priority_type, rng.rand(3))\n"
"          c.instance_variable_set(:@character_name, ['', '!Door', '$Big', 'People1', '!$Obj'].sample(random: rng))\n"
"          c.instance_variable_set(:@tile_id, [0, 0, 5].sample(random: rng))\n"
"          ruby = [c.screen_x, c.screen_y, c.screen_z]\n"
"          nat = vita_spc_eval(c)\n"
"          unless nat && nat[0] == ruby[0] && nat[1] == ruby[1] && nat[2] == ruby[2] &&\n"
"                 nat[0].to_i == ruby[0].to_i && nat[1].to_i == ruby[1].to_i\n"
"            good = false\n"
"            File.open(VITA_SPC_LOG, 'a') { |f| f.puts \"SPRITE_NATIVE_MISMATCH #{k} ruby=#{ruby.inspect} c=#{nat.inspect}\" } rescue nil\n"
"            break\n"
"          end\n"
"        end\n"
"        classes[k] = true if good\n"
"      end\n"
"      $game_map = saved_map\n"
"    end\n"
"    File.open(VITA_SPC_LOG, 'a') { |f| f.puts \"SPRITE_NATIVE #{ok && !classes.empty? ? 'INSTALLED' : 'NOT INSTALLED'} classes=#{classes.keys.join(',')}\" } rescue nil\n"
"    if ok && !classes.empty?\n"
"      VITA_SPC_CLASSES = classes\n"
"      $vita_spc_noloop = false\n"
"      Spriteset_Map.prepend(Module.new do\n"
"        def update\n"
"          m = $game_map\n"
"          $vita_spc_noloop = $vita_sprite_native != false && !m.loop_horizontal? && !m.loop_vertical?\n"
"          super\n"
"        ensure\n"
"          $vita_spc_noloop = false\n"
"        end\n"
"      end)\n"
"      Sprite_Character.prepend(Module.new do\n"
"        def update_position\n"
"          return super if @animation || !$vita_spc_noloop || !VITA_SPC_CLASSES[@character.class]\n"
"          super unless vita_spc_pos(self, @character)\n"
"        end\n"
"      end)\n"
"    end\n"
"  rescue Exception => e\n"
"    $game_map = saved_map if defined?(saved_map)\n"
"    File.open(VITA_SPC_LOG, 'a') { |f| f.puts \"SPRITE_NATIVE ERROR #{e.class}: #{e.message}\" } rescue nil\n"
"  end\n"
"  VITA_SPC\n"
#endif
#ifdef MKXP_VITA_MAP_REFRESH_FAST
"  TOPLEVEL_BINDING.eval(<<~'VITA_MR', 'vita_map_refresh', 1)\n"
"  VITA_MR_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_MAP_REFRESH_FAST): exact shortcut of Game_Map#refresh. Writing any switch or\n"
"  # variable (even the same value) sets need_refresh, and LISA's parallel events write every frame:\n"
"  # d52, every event recomputed its page every frame (pages.reverse.find + conditions_met?). The\n"
"  # page of an event depends only on the switches, variables, self switches, items and party members\n"
"  # named in its pages' conditions; if those values are the same as at the last full refresh (same\n"
"  # map, same events, same Galv region events), every event with a page would get the same page\n"
"  # again, so only those are skipped. Everything else runs as in the full refresh: events without a\n"
"  # page or erased (setup_page(nil) each time), common events, refresh_tile_events, need_refresh.\n"
"  # Installed only if the refresh chain is the stock one (+ Galv Region Effects). Host test:\n"
"  # tools/hosttests/map-refresh/. Toggled with L+R+START.\n"
"  $vita_map_refresh_fast = true\n"
"  $vita_mr_full = 0\n"
"  $vita_mr_skip = 0\n"
"  owner_of = lambda do |c, m|\n"
"    um = (c.instance_method(m) rescue nil)\n"
"    um = um.super_method while um && !um.owner.is_a?(Class)\n"
"    um && um.owner\n"
"  end\n"
"  names = (Game_Map.instance_methods(false) + Game_Map.private_instance_methods(false)).grep(/refresh/).sort\n"
"  galv = names.include?(:galv_region_effects_gm_refresh)\n"
"  expected = [:need_refresh, :need_refresh=, :refresh, :refresh_tile_events]\n"
"  expected = (expected + [:galv_region_effects_gm_refresh]).sort if galv\n"
"  ok = names == expected.sort &&\n"
"       [[Game_Event, :refresh], [Game_Event, :find_proper_page], [Game_Event, :conditions_met?], [Game_Event, :setup_page],\n"
"        [Game_CommonEvent, :refresh]].all? { |c, m| owner_of.call(c, m) == c } &&\n"
"       (Game_Event.instance_methods(false) + Game_Event.private_instance_methods(false)).grep(/refresh|conditions_met|proper_page/).sort ==\n"
"         [:conditions_met?, :find_proper_page, :refresh]\n"
"  File.open(VITA_MR_LOG, 'a') { |f| f.puts \"MAP_REFRESH_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED'} galv=#{galv} names=#{names.inspect}\" } rescue nil\n"
"  if ok\n"
"    module VitaMapRefresh\n"
"      # Ids named in the conditions of every page of these events.\n"
"      def self.deps(events)\n"
"        sw = {}; vr = {}; ss = {}; it = {}; ac = {}\n"
"        events.each do |e|\n"
"          ev = e.instance_variable_get(:@event)\n"
"          mid = e.instance_variable_get(:@map_id)\n"
"          next unless ev\n"
"          ev.pages.each do |pg|\n"
"            c = pg.condition\n"
"            sw[c.switch1_id] = true if c.switch1_valid\n"
"            sw[c.switch2_id] = true if c.switch2_valid\n"
"            vr[c.variable_id] = true if c.variable_valid\n"
"            ss[[mid, ev.id, c.self_switch_ch]] = true if c.self_switch_valid\n"
"            it[c.item_id] = true if c.item_valid\n"
"            ac[c.actor_id] = true if c.actor_valid\n"
"          end\n"
"        end\n"
"        [sw.keys, vr.keys, ss.keys, it.keys, ac.keys]\n"
"      end\n"
"      # Actors read without $game_actors[] (which creates the actor): one not created yet cannot be a member.\n"
"      def self.values(d)\n"
"        sw, vr, ss, it, ac = d\n"
"        [sw.map { |i| $game_switches[i] }, vr.map { |i| $game_variables[i] }, ss.map { |k| $game_self_switches[k] },\n"
"         it.map { |i| $game_party.has_item?($data_items[i]) }, ac.map { |i| a = $game_actors.instance_variable_get(:@data)[i]; a ? $game_party.members.include?(a) : false }]\n"
"      end\n"
"    end\n"
"    Game_Map.prepend(Module.new do\n"
"      def refresh\n"
"        r = instance_variable_get(:@r_events)\n"
"        snap = @vita_mr_snap\n"
"        if $vita_map_refresh_fast && snap && snap[0] == @map_id && snap[1].equal?(@events) && snap[2] == @events.size &&\n"
"           snap[3] == (r ? r.keys : nil) && VitaMapRefresh.values(snap[4]) == snap[5]\n"
"          r.each_value { |e| e.refresh if e.instance_variable_get(:@erased) || !e.instance_variable_get(:@page) } if r\n"
"          @events.each_value { |e| e.refresh if e.instance_variable_get(:@erased) || !e.instance_variable_get(:@page) }\n"
"          @common_events.each { |e| e.refresh }\n"
"          refresh_tile_events\n"
"          @need_refresh = false\n"
"          $vita_mr_skip += 1\n"
"          return\n"
"        end\n"
"        res = super\n"
"        evs = @events.values\n"
"        evs += r.values if r\n"
"        d = VitaMapRefresh.deps(evs)\n"
"        @vita_mr_snap = [@map_id, @events, @events.size, (r ? r.keys : nil), d, VitaMapRefresh.values(d)]\n"
"        $vita_mr_full += 1\n"
"        res\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_MR\n"
#endif
#ifdef MKXP_VITA_SAVE_FAST
"  TOPLEVEL_BINDING.eval(<<~'VITA_SAVE_FAST', 'vita_save_fast', 1)\n"
"  VITA_SAVE_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_SAVE_FAST): the save/load screen opened in 2-3 s (d45-d48): 16 slots, each\n"
"  # Window_SaveFile#refresh reads the same header twice (draw_party_characters, draw_playtime) and\n"
"  # every empty slot raises Errno::ENOENT twice (load_header's `rescue nil`). Here load_header\n"
"  # returns nil at once for a file that does not exist (same result as the rescued exception), and\n"
"  # a second read of the same slot in the same frame is rebuilt from the first one with Marshal\n"
"  # (new objects each call, as from the file). The memo is dropped on every save and delete.\n"
"  # Installed only if DataManager's header methods are the stock ones. Host test:\n"
"  # tools/hosttests/save-fast/.\n"
"  $vita_save_fast = true\n"
"  dm = DataManager.singleton_class\n"
"  # our own aliases (vita_*: soak) are not game scripts\n"
"  names = (dm.instance_methods(false) + dm.private_instance_methods(false)).grep(/header|save_file|save_game/).reject { |n| n.to_s.start_with?('vita_') }.sort\n"
"  ok = names == [:delete_save_file, :load_header, :load_header_without_rescue, :make_save_header, :save_file_exists?, :save_game, :save_game_without_rescue].sort\n"
"  File.open(VITA_SAVE_LOG, 'a') { |f| f.puts \"SAVE_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED'} names=#{names.inspect}\" } rescue nil\n"
"  if ok\n"
"    module VitaSaveFast\n"
"      @memo = {}\n"
"      @frame = -1\n"
"      class << self; attr_accessor :memo, :frame; end\n"
"    end\n"
"    dm.prepend(Module.new do\n"
"      def load_header(index)\n"
"        return super unless $vita_save_fast\n"
"        fn = make_filename(index)\n"
"        return nil unless File.exist?(fn)   # File.open would raise ENOENT -> rescue nil\n"
"        if VitaSaveFast.frame != Graphics.frame_count\n"
"          VitaSaveFast.memo = {}\n"
"          VitaSaveFast.frame = Graphics.frame_count\n"
"        end\n"
"        if (bytes = VitaSaveFast.memo[index])\n"
"          return (Marshal.load(bytes) rescue nil)\n"
"        end\n"
"        h = super\n"
"        VitaSaveFast.memo[index] = Marshal.dump(h) rescue nil\n"
"        h\n"
"      end\n"
"      def save_game_without_rescue(index)\n"
"        VitaSaveFast.memo = {}\n"
"        super\n"
"      ensure\n"
"        VitaSaveFast.memo = {}\n"
"      end\n"
"      def delete_save_file(index)\n"
"        VitaSaveFast.memo = {}\n"
"        super\n"
"      ensure\n"
"        VitaSaveFast.memo = {}\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_SAVE_FAST\n"
#endif
#ifdef MKXP_VITA_SAVE_PROF
"  TOPLEVEL_BINDING.eval(<<~'VITA_SAVE_PROF', 'vita_save_prof', 1)\n"
"  VITA_SAVEPROF_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # LIGHT DIAGNOSTIC (MKXP_VITA_SAVE_PROF): what the save/load screen costs when it opens. Active\n"
"  # only during Scene_File#start; one SAVE_SCREEN line in qa.log (wall ms per call site, CPU ms).\n"
"  $vita_sfp = nil\n"
"  [[File.singleton_class, :open, 'File.open'], [Marshal.singleton_class, :load, 'Marshal.load'],\n"
"   [DataManager.singleton_class, :load_header, 'load_header'], [Window_Base, :initialize, 'Window_Base#initialize'],\n"
"   [Window_SaveFile, :refresh, 'refresh'], [Window_SaveFile, :draw_party_characters, 'party'],\n"
"   [Window_SaveFile, :draw_playtime, 'playtime'], [Window_Base, :draw_character, 'draw_character'],\n"
"   [Cache.singleton_class, :character, 'Cache.character']].each do |owner, meth, tag|\n"
"    next unless owner.method_defined?(meth) || owner.private_method_defined?(meth)\n"
"    priv = owner.private_method_defined?(meth)\n"
"    # def + zsuper (d76): define_method { |*a, &b| super(*a, &b) } left one callinfo + callcache per\n"
"    # call on the Vita (d75 census: update_all_windows, update_events, update_interpreter, ...).\n"
"    m = Module.new\n"
"    m.const_set(:VITA_TAG, tag)\n"
"    m.module_eval(<<~'VITA_M'.sub('__M__', meth.to_s))\n"
"      def __M__(*a, &b)\n"
"        h = $vita_sfp\n"
"        return super unless h\n"
"        t0 = Time.now\n"
"        begin\n"
"          super\n"
"        ensure\n"
"          h[VITA_TAG] += Time.now - t0\n"
"          h[VITA_TAG + '#n'] += 1\n"
"        end\n"
"      end\n"
"    VITA_M\n"
"    m.send(:ruby2_keywords, meth)\n"
"    m.send(:private, meth) if priv\n"
"    owner.prepend(m)\n"
"  end\n"
"  Scene_File.prepend(Module.new do\n"
"    def start\n"
"      h = Hash.new(0)\n"
"      $vita_sfp = h\n"
"      t0 = Time.now\n"
"      c0 = vita_thread_cpu_ms\n"
"      begin\n"
"        super\n"
"      ensure\n"
"        $vita_sfp = nil\n"
"        line = \"SAVE_SCREEN #{self.class} wall_ms=#{((Time.now - t0) * 1000).round} cpu_ms=#{(vita_thread_cpu_ms - c0).round} \" +\n"
"               h.keys.reject { |k| k.end_with?('#n') }.map { |k| \"#{k}=#{(h[k] * 1000).round}ms/#{h[k + '#n']}\" }.join(' ')\n"
"        File.open(VITA_SAVEPROF_LOG, 'a') { |f| f.puts line } rescue nil\n"
"      end\n"
"    end\n"
"  end)\n"
"  VITA_SAVE_PROF\n"
#endif
#ifdef MKXP_VITA_COMBO_FAST
"  TOPLEVEL_BINDING.eval(<<~'VITA_COMBO', 'vita_combo_fast', 1)\n"
"  VITA_COMBO_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # PERF FIX (MKXP_VITA_COMBO_FAST): LISA's input-combo list (Window_ComboSkillList, Yanfly Input\n"
"  # Combo Skills) is redrawn from scratch at every hp=/mp=/tp= of the actor while it is shown\n"
"  # (d60: 174 redraws x ~19 ms in a few battles, one dropped frame each). What it draws depends only\n"
"  # on: battler, skill, the combo skills (id, name, icon), whether each is usable now, the contents\n"
"  # bitmap and its size, the windowskin, line_height and the Font defaults. When all of these are\n"
"  # the same as at the last real redraw, the redraw would produce the same pixels: skipped. Only\n"
"  # this window draws into its contents. Installed only if the class has the expected methods.\n"
"  $vita_combo_fast = true\n"
"  $vita_combo_skip = 0\n"
"  ok = defined?(Window_ComboSkillList) &&\n"
"       (Window_ComboSkillList.instance_methods(false) + Window_ComboSkillList.private_instance_methods(false)).sort ==\n"
"         [:draw_background_colour, :draw_combo_skills, :draw_combo_title, :draw_horz_line, :initialize, :refresh, :refresh_check, :reveal, :text_setting].sort\n"
"  File.open(VITA_COMBO_LOG, 'a') { |f| f.puts \"COMBO_FAST #{ok ? 'INSTALLED' : 'NOT INSTALLED'}\" } rescue nil\n"
"  if ok\n"
"    Window_ComboSkillList.prepend(Module.new do\n"
"      def vita_combo_snapshot\n"
"        col = ->(c) { c ? [c.red, c.green, c.blue, c.alpha] : nil }\n"
"        usable = [:L, :R, :X, :Y, :Z].map do |b|\n"
"          id = @skill && @skill.combo_skill[b]\n"
"          sk = id && $data_skills[id]\n"
"          sk ? [id, sk.name.dup, sk.icon_index, (@battler ? @battler.usable?(sk) : nil)] : nil\n"
"        end\n"
"        [@battler.object_id, @skill.object_id, (@combo_skills || []).map { |k| k.id }, usable,\n"
"         contents.object_id, contents.width, contents.height, windowskin.object_id, line_height,\n"
"         (Font.default_name.dup rescue nil), Font.default_size, Font.default_bold, Font.default_italic,\n"
"         Font.default_shadow, Font.default_outline, col.call(Font.default_color), col.call(Font.default_out_color)]\n"
"      end\n"
"      def refresh\n"
"        return super unless $vita_combo_fast\n"
"        snap = vita_combo_snapshot\n"
"        if @vita_combo_snap == snap && !contents.disposed?\n"
"          $vita_combo_skip += 1\n"
"          return\n"
"        end\n"
"        super\n"
"        @vita_combo_snap = vita_combo_snapshot\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_COMBO\n"
#endif
#ifdef MKXP_VITA_SCENE_FLUSH
"  TOPLEVEL_BINDING.eval(<<~'VITA_SCENE_FLUSH', 'vita_scene_flush', 1)\n"
"  # FIX (MKXP_VITA_SCENE_FLUSH): give the previous scene's GPU memory back before the new scene\n"
"  # creates its sprites and tile atlases (gl-fun-vita.cpp vitaSceneGpuFlush).\n"
"  Scene_Base.prepend(Module.new do\n"
"    def main\n"
"      vita_scene_gpu_flush\n"
"      super\n"
"    end\n"
"  end)\n"
"  VITA_SCENE_FLUSH\n"
#endif
#ifdef MKXP_VITA_MOG_CAP
"  TOPLEVEL_BINDING.eval(<<~'VITA_MOG', 'vita_mog_cap', 1)\n"
"  VITA_MOG_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # FIX (MKXP_VITA_MOG_CAP): MOG Anti Animation Lag keeps every animation bitmap that finished\n"
"  # playing (a garbage list) until the scene changes, so it can be reused without reloading. In long\n"
"  # battles that filled the vitaGL pools (d61: 4.4 MiB sheets, 4-7 MiB free in every pool) and the\n"
"  # next textures spilled into the newlib heap. Here MOG keeps its behaviour while there is room;\n"
"  # when an animation ends and less than VITA_MOG_MIN_KB is free in the texture pools, the bitmaps of\n"
"  # that list that no animation uses now (Sprite_Base's own reference count is 0) are disposed and\n"
"  # dropped from the list. Cache reloads a disposed bitmap on its next use. Pixels on screen are the\n"
"  # same; only a repeated animation may be loaded again. Host test: tools/hosttests/mog-cap/.\n"
"  VITA_MOG_MIN_KB = 24 * 1024\n"
"  $vita_mog_cap = true\n"
"  $vita_mog_trimmed = 0\n"
"  ok = defined?(Game_Temp) && Game_Temp.method_defined?(:animation_garbage) &&\n"
"       Sprite_Base.class_variable_defined?(:@@_reference_count) &&\n"
"       (Sprite_Base.method_defined?(:execute_animation_garbage) || Sprite_Base.private_method_defined?(:execute_animation_garbage))\n"
"  File.open(VITA_MOG_LOG, 'a') { |f| f.puts \"MOG_CAP #{ok ? 'INSTALLED' : 'NOT INSTALLED (no MOG Anti Animation Lag)'}\" } rescue nil\n"
"  if ok\n"
"    module VitaMogCap\n"
"      @logged = 0\n"
"      def self.trim\n"
"        return unless $vita_mog_cap && $game_temp\n"
"        g = $game_temp.animation_garbage\n"
"        return if g.nil? || g.empty?\n"
"        free = vita_gpu_free_kb\n"
"        return if free >= VITA_MOG_MIN_KB\n"
"        refs = Sprite_Base.class_variable_get(:@@_reference_count)\n"
"        n = 0\n"
"        g.delete_if do |b|\n"
"          next true if b.disposed?\n"
"          next false if (refs[b] || 0) > 0     # used again by a running animation: keep\n"
"          b.dispose\n"
"          n += 1\n"
"          true\n"
"        end\n"
"        $vita_mog_trimmed += n\n"
"        if n > 0 && @logged < 30\n"
"          @logged += 1\n"
"          File.open(VITA_MOG_LOG, 'a') { |f| f.puts \"MOG_CAP trimmed=#{n} free_kb_before=#{free} after=#{vita_gpu_free_kb}\" } rescue nil\n"
"        end\n"
"      end\n"
"    end\n"
"    Sprite_Base.prepend(Module.new do\n"
"      def dispose_animation\n"
"        super\n"
"      ensure\n"
"        VitaMogCap.trim\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_MOG\n"
#endif
#ifdef MKXP_VITA_MAP_PROF
"  TOPLEVEL_BINDING.eval(<<~'VITA_MAP_PROF', 'vita_map_prof', 1)\n"
"  VITA_MAPPROF_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # DIAGNOSTIC (MKXP_VITA_MAP_PROF): where a slow map change spends its time (d65: map 74 usually\n"
"  # ~440 ms, three times 2.2-4.4 s with the CPU idle for more than half of it). Roots: Scene_Map\n"
"  # #perform_transfer and #start (back from a menu or the save screen). While a root runs, the listed\n"
"  # methods add wall and CPU ms (sceKernelGetThreadInfo runClocks of the main thread) and call\n"
"  # counts; a root of 100 ms or more writes one MAP_LOAD line to qa.log (nested methods are included\n"
"  # in their callers). A Scene_Map#update of 500 ms or more outside a root writes MAP_SLOW_UPDATE.\n"
"  $vita_mpp = nil\n"
"  $vita_mpp_lines = 0\n"
"  [[Game_Player, :perform_transfer, 'player_transfer'], [Game_Map, :setup, 'map_setup'],\n"
"   [Game_Map, :setup_events, 'setup_events'], [Game_Map, :refresh, 'map_refresh'],\n"
"   [Game_Map, :autoplay, 'autoplay'], [Game_Event, :initialize, 'Game_Event.new'],\n"
"   [Object, :load_data, 'load_data'], [Spriteset_Map, :initialize, 'Spriteset_Map.new'],\n"
"   [Spriteset_Map, :dispose, 'Spriteset_Map#dispose'], [Spriteset_Map, :create_tilemap, 'create_tilemap'],\n"
"   [Spriteset_Map, :load_tileset, 'load_tileset'], [Spriteset_Map, :create_characters, 'create_characters'],\n"
"   [Spriteset_Map, :create_parallax, 'create_parallax'], [Spriteset_Map, :update, 'spriteset_update'],\n"
"   [Scene_Map, :create_spriteset, 'create_spriteset'], [Scene_Map, :dispose_spriteset, 'dispose_spriteset'],\n"
"   [Scene_Map, :create_all_windows, 'create_all_windows'], [Scene_Map, :pre_transfer, 'pre_transfer'],\n"
"   [Scene_Map, :post_transfer, 'post_transfer'], [Cache.singleton_class, :load_bitmap, 'Cache.load_bitmap'],\n"
"   [Bitmap, :initialize, 'Bitmap.new'], [Audio.singleton_class, :bgm_play, 'Audio.bgm_play'],\n"
"   [Audio.singleton_class, :bgs_play, 'Audio.bgs_play'], [Audio.singleton_class, :me_play, 'Audio.me_play'],\n"
"   [Audio.singleton_class, :se_play, 'Audio.se_play'], [Audio.singleton_class, :bgm_stop, 'Audio.bgm_stop'],\n"
"   [Audio.singleton_class, :bgs_stop, 'Audio.bgs_stop'], [Audio.singleton_class, :bgm_fade, 'Audio.bgm_fade'],\n"
"   [Audio.singleton_class, :bgs_fade, 'Audio.bgs_fade'], [Graphics.singleton_class, :update, 'Graphics.update'],\n"
"   [Graphics.singleton_class, :freeze, 'Graphics.freeze'], [Graphics.singleton_class, :transition, 'Graphics.transition'],\n"
"   [Graphics.singleton_class, :fadeout, 'Graphics.fadeout'], [Graphics.singleton_class, :fadein, 'Graphics.fadein'],\n"
"   [Game_Interpreter, :setup, 'Interpreter#setup'], [Window_MapName, :refresh, 'MapName#refresh']].each do |owner, meth, tag|\n"
"    next unless owner.method_defined?(meth) || owner.private_method_defined?(meth)\n"
"    priv = owner.private_method_defined?(meth)\n"
"    # def + zsuper (d76): define_method { |*a, &b| super(*a, &b) } left one callinfo + callcache per\n"
"    # call on the Vita (d75 census: update_all_windows, update_events, update_interpreter, ...).\n"
"    m = Module.new\n"
"    m.const_set(:VITA_TAG, tag)\n"
"    m.module_eval(<<~'VITA_M'.sub('__M__', meth.to_s))\n"
"      def __M__(*a, &b)\n"
"        h = $vita_mpp\n"
"        return super unless h\n"
"        t0 = Time.now\n"
"        c0 = vita_thread_cpu_ms\n"
"        begin\n"
"          super\n"
"        ensure\n"
"          h[VITA_TAG] += Time.now - t0\n"
"          h[VITA_TAG + '#c'] += vita_thread_cpu_ms - c0\n"
"          h[VITA_TAG + '#n'] += 1\n"
"        end\n"
"      end\n"
"    VITA_M\n"
"    m.send(:ruby2_keywords, meth)\n"
"    m.send(:private, meth) if priv\n"
"    owner.prepend(m)\n"
"  end\n"
"  module VitaMapProf\n"
"    def self.run(root)\n"
"      return yield if $vita_mpp\n"
"      h = Hash.new(0)\n"
"      $vita_mpp = h\n"
"      m0 = ($game_map.map_id rescue 0)\n"
"      g0 = GC.count\n"
"      t0 = Time.now\n"
"      c0 = vita_thread_cpu_ms\n"
"      begin\n"
"        yield\n"
"      ensure\n"
"        $vita_mpp = nil\n"
"        wall = ((Time.now - t0) * 1000).round\n"
"        if wall >= 100 && $vita_mpp_lines < 500\n"
"          $vita_mpp_lines += 1\n"
"          parts = h.keys.reject { |k| k.end_with?('#c', '#n') }.sort_by { |k| -h[k] }.map do |k|\n"
"            \"#{k}=#{(h[k] * 1000).round}/#{h[k + '#c'].round}/#{h[k + '#n']}\"\n"
"          end\n"
"          line = \"MAP_LOAD root=#{root} map=#{m0}->#{($game_map.map_id rescue 0)} frame=#{Graphics.frame_count} \" \\\n"
"                 \"wall_ms=#{wall} cpu_ms=#{(vita_thread_cpu_ms - c0).round} gc=#{GC.count - g0} \" \\\n"
"                 \"(wall/cpu/n) \" + parts.join(' ')\n"
"          File.open(VITA_MAPPROF_LOG, 'a') { |f| f.puts line } rescue nil\n"
"        end\n"
"      end\n"
"    end\n"
"  end\n"
"  Scene_Map.prepend(Module.new do\n"
"    def perform_transfer\n"
"      VitaMapProf.run('perform_transfer') { super }\n"
"    end\n"
"    def start\n"
"      VitaMapProf.run('start') { super }\n"
"    end\n"
"    def update\n"
"      return super if $vita_mpp\n"
"      t0 = Time.now\n"
"      super\n"
"      wall = ((Time.now - t0) * 1000).round\n"
"      if wall >= 500 && $vita_mpp_lines < 500\n"
"        $vita_mpp_lines += 1\n"
"        File.open(VITA_MAPPROF_LOG, 'a') { |f| f.puts \"MAP_SLOW_UPDATE map=#{($game_map.map_id rescue 0)} frame=#{Graphics.frame_count} wall_ms=#{wall}\" } rescue nil\n"
"      end\n"
"    end\n"
"  end)\n"
"  VITA_MAP_PROF\n"
#endif
#ifdef MKXP_VITA_OBJ_HIST
"  TOPLEVEL_BINDING.eval(<<~'VITA_OBJ_HIST', 'vita_obj_hist', 1)\n"
"  VITA_OBJHIST_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  # DIAGNOSTIC (MKXP_VITA_OBJ_HIST): which objects pile up. d66: Ruby objects alive after full GCs\n"
"  # grew 288k -> 778k (heap pages 366 -> 951) in steps of 40-180k within a map, then a 4.4 MiB image\n"
"  # found no room (std::bad_alloc). Every 600 Graphics.update calls: if live slots exceed the last\n"
"  # report by STEP, a full GC runs; if they still do, one OBJ_HIST line goes to qa.log: top classes\n"
"  # by count, the classes of Array first elements and Hash first keys, and the instance variables of\n"
"  # one instance of each of the top non-core classes. A report takes ~1-2 s (one hitch per step).\n"
"  module VitaObjHist\n"
"    STEP = 60_000\n"
"    CORE = [String, Array, Hash, Float, Integer, Symbol, Proc, Range, Time, Object, Class, Module,\n"
"            Encoding, Regexp, MatchData, Rational, Complex, Method, UnboundMethod]\n"
"    @base = nil\n"
"    @n = 0\n"
"    @tick = 0\n"
"    def self.tick\n"
"      @tick += 1\n"
"      return if @tick % 600 != 0 || @n >= 25\n"
"      live = GC.stat(:heap_live_slots)\n"
"      if @base.nil?\n"
"        @base = live\n"
"        return\n"
"      end\n"
"      return if live < @base + STEP\n"
"      GC.start\n"
"      live = GC.stat(:heap_live_slots)\n"
"      return if live < @base + STEP\n"
"      report(live)\n"
"    rescue Exception => e\n"
"      File.open(VITA_OBJHIST_LOG, 'a') { |f| f.puts \"OBJ_HIST_FAILED #{e.class}: #{e.message}\" } rescue nil\n"
"      @n = 99\n"
"    end\n"
"    def self.desc(v)\n"
"      sz = (v.is_a?(Array) || v.is_a?(Hash) || v.is_a?(String)) ? \"[#{v.size}]\" : ''\n"
"      \"#{v.class}#{sz}\"\n"
"    end\n"
"    def self.report(live)\n"
"      t0 = Time.now\n"
"      h = Hash.new(0)\n"
"      af = Hash.new(0)\n"
"      hk = Hash.new(0)\n"
"      ObjectSpace.each_object(Object) do |o|\n"
"        c = o.class\n"
"        h[c] += 1\n"
"        if c == Array\n"
"          af[o.empty? ? :empty : o[0].class] += 1\n"
"        elsif c == Hash\n"
"          hk[o.empty? ? :empty : o.first[0].class] += 1\n"
"        end\n"
"      end\n"
"      top = h.sort_by { |_, v| -v }.first(30)\n"
"      samples = []\n"
"      top.each do |k, _|\n"
"        break if samples.size >= 6\n"
"        next if CORE.include?(k)\n"
"        o = ObjectSpace.each_object(k).first\n"
"        next unless o\n"
"        ivs = o.instance_variables.first(10).map { |iv| \"#{iv}:#{desc(o.instance_variable_get(iv))}\" }\n"
"        samples << \"#{k}{#{ivs.join(',')}}\"\n"
"      end\n"
"      fmt = ->(hh, n) { hh.sort_by { |_, v| -v }.first(n).map { |k, v| \"#{k}=#{v}\" }.join(' ') }\n"
"      line = \"OBJ_HIST n=#{@n} live=#{live} base=#{@base} pages=#{GC.stat(:heap_allocated_pages)} \" \\\n"
"             \"map=#{($game_map.map_id rescue 0)} frame=#{Graphics.frame_count} scene=#{(SceneManager.scene.class rescue nil)} \" \\\n"
"             \"ms=#{((Time.now - t0) * 1000).round}\\n  classes #{fmt.call(h, 30)}\\n  array_first #{fmt.call(af, 10)}\\n\" \\\n"
"             \"  hash_key #{fmt.call(hk, 8)}\\n  samples #{samples.join(' ')}\\n  \" \\\n"
"             \"#{(vita_heap_census rescue 'census unavailable')}\\n  gc final_slots=#{GC.stat(:heap_final_slots)} \" \\\n"
"             \"freed=#{GC.stat(:total_freed_objects)} allocated=#{GC.stat(:total_allocated_objects)} old=#{GC.stat(:old_objects)} \" \\\n"
"             \"old_limit=#{GC.stat(:old_objects_limit)} major=#{GC.stat(:major_gc_count)}\"\n"
"      File.open(VITA_OBJHIST_LOG, 'a') { |f| f.puts line } rescue nil\n"
"      @base = live\n"
"      @n += 1\n"
"    end\n"
"  end\n"
"  class << Graphics\n"
"    prepend(Module.new do\n"
"      def update(*a)\n"
"        r = super\n"
"        VitaObjHist.tick\n"
"        r\n"
"      end\n"
"    end)\n"
"  end\n"
"  VITA_OBJ_HIST\n"
#endif
#ifdef MKXP_VITA_PATCHES
"  TOPLEVEL_BINDING.eval(<<~'VITA_PATCHES', 'vita_patches', 1)\n"
"  # Feature (MKXP_VITA_PATCHES): game patches shipped in the package, app0:patches/ (listed in\n"
"  # index.txt, loaded in that order after the game's scripts, before the game starts). They carry\n"
"  # only the port's own code and drawings; the player's game files are not changed. Each one is\n"
"  # isolated: an error is logged (PATCH_FAILED) and the game goes on without it.\n"
"  VITA_PATCH_DIR = 'app0:patches/'\n"
"  VITA_PATCH_LOG = '" VITA_GAME_ROOT "qa.log'\n"
"  vita_patch_log = ->(line) { File.open(VITA_PATCH_LOG, 'a') { |f| f.puts line } rescue nil }\n"
"  vita_patch_files =\n"
"    begin\n"
"      File.read(VITA_PATCH_DIR + 'index.txt').split(/\\r?\\n/).map(&:strip).reject { |l| l.empty? || l.start_with?('#') }\n"
"    rescue Exception => e\n"
"      vita_patch_log.call(\"PATCHES none (#{e.class})\")\n"
"      []\n"
"    end\n"
"  vita_patch_files.each do |name|\n"
"    begin\n"
"      TOPLEVEL_BINDING.eval(File.read(VITA_PATCH_DIR + name, encoding: 'UTF-8'), \"patches/#{name}\", 1)\n"
"      vita_patch_log.call(\"PATCH_LOADED #{name}\")\n"
"    rescue Exception => e\n"
"      vita_patch_log.call(\"PATCH_FAILED #{name}: #{e.class}: #{e.message}\")\n"
"    end\n"
"  end\n"
"  VITA_PATCHES\n"
#endif
"  vita_dbg('DBG: before yield')\n"
"  yield\n"
"  vita_dbg('DBG: after yield')\n"
"end\n",
    &engineStubState
);

if (engineStubState) {
    VALUE exc = rb_errinfo();
    VALUE msg = rb_funcall(exc, rb_intern("message"), 0);

    psvDebugScreenPrintf(
        "Graphics/Font stubs FAILED: %s\n",
        StringValueCStr(msg)
    );

    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf("Graphics/Font stubs OK\n");
}

VALUE graphicsModule =
    rb_const_get(
        rb_cObject,
        rb_intern("Graphics")
    );

rb_define_singleton_method(
    graphicsModule,
    "vita_present",
    RUBY_METHOD_FUNC(vita_graphics_update),
    0
);

VALUE inputModule =
    rb_const_get(
        rb_cObject,
        rb_intern("Input")
    );

rb_define_singleton_method(
    inputModule,
    "update",
    RUBY_METHOD_FUNC(vita_input_update),
    0
);

rb_define_singleton_method(
    inputModule,
    "press?",
    RUBY_METHOD_FUNC(vita_input_press),
    1
);

rb_define_singleton_method(
    inputModule,
    "trigger?",
    RUBY_METHOD_FUNC(vita_input_trigger),
    1
);

rb_define_singleton_method(
    inputModule,
    "repeat?",
    RUBY_METHOD_FUNC(vita_input_repeat),
    1
);

rb_define_singleton_method(
    inputModule,
    "dir4",
    RUBY_METHOD_FUNC(vita_input_dir4),
    0
);

rb_define_singleton_method(
    inputModule,
    "dir8",
    RUBY_METHOD_FUNC(vita_input_dir8),
    0
);

#ifdef MKXP_VITA_FREEZE_PROBE
vitaFreezeRubyInit();
#endif
#ifdef MKXP_VITA_DIAG
vitaDiagRubyInit();
#endif
#ifdef MKXP_VITA_SCREEN_FX
rb_define_global_function("vita_fx_brightness", RUBY_METHOD_FUNC(vita_fx_brightness), 1);
rb_define_global_function("vita_fx_freeze", RUBY_METHOD_FUNC(vita_fx_freeze), 0);
rb_define_global_function("vita_fx_transition", RUBY_METHOD_FUNC(vita_fx_transition), 1);
rb_define_global_function("vita_fx_frozen?", RUBY_METHOD_FUNC(vita_fx_frozen_p), 0);
#endif
#ifdef MKXP_VITA_DEBUG_SOAK
rb_define_global_function("vita_power_tick", RUBY_METHOD_FUNC(vita_power_tick_rb), 0);
#endif
#if defined(MKXP_VITA_SAVE_PROF) || defined(MKXP_VITA_MAP_PROF)
rb_define_global_function("vita_thread_cpu_ms", RUBY_METHOD_FUNC(vita_thread_cpu_ms_rb), 0);
#endif
#ifdef MKXP_VITA_SCENE_FLUSH
rb_define_global_function("vita_scene_gpu_flush", RUBY_METHOD_FUNC(vita_scene_gpu_flush_rb), 0);
#endif
#ifdef MKXP_VITA_MOG_CAP
rb_define_global_function("vita_gpu_free_kb", RUBY_METHOD_FUNC(vita_gpu_free_kb_rb), 0);
#endif
#ifdef MKXP_VITA_OBJ_HIST
rb_define_global_function("vita_heap_census", RUBY_METHOD_FUNC(vita_heap_census_rb), 0);
#endif
#ifdef MKXP_VITA_HEAP_LEDGER
rb_define_global_function("vita_heap_ledger_dump", RUBY_METHOD_FUNC(vita_heap_ledger_dump_rb), 1);
#endif
#ifdef MKXP_VITA_GL_LEDGER
rb_define_global_function("vita_gl_ledger_dump", RUBY_METHOD_FUNC(vita_gl_ledger_dump_rb), 1);
#endif
#ifdef MKXP_VITA_STDOUT_TO_FILE
/*
 * Fix: STDOUT is not a valid file on the Vita (Errno::EBADF on write), so any Kernel#puts/print/p
 * in the game scripts raised, e.g. SteamAPI#store's rescue branch after an achievement unlock.
 * Route $stdout/$stderr to a log file instead (the STDOUT constant is left untouched).
 */
{
    int vitaStdoutState = 0;
    rb_eval_string_protect(
        "$stdout = File.open('" VITA_GAME_ROOT "stdout.log', 'a')\n"
        "$stdout.sync = true\n"
        "$stderr = $stdout\n"
        "$stdout.puts 'STDOUT_REDIRECT boot'\n",
        &vitaStdoutState
    );
    if (vitaStdoutState)
        rb_set_errinfo(Qnil);
}
#endif
#ifdef MKXP_VITA_FS_V2
#define VITA_FS_SELFTEST 1
#endif
#ifdef MKXP_VITA_CHDIR_GAME_ROOT
/*
 * Fix: the working directory is app0: (read-only package). Relative file access in the scripts
 * (e.g. SteamAPI's steamstat.dat next to __FILE__ = ".") must resolve inside the game root.
 */
{
    int vitaChdirState = 0;
    rb_eval_string_protect(
        "begin\n"
        "  Dir.chdir('" VITA_GAME_ROOT "')\n"
        "  $stdout.puts \"CHDIR ok #{Dir.pwd}\"\n"
        "rescue Exception => e\n"
        "  $stdout.puts \"CHDIR failed #{e.class}: #{e.message}\"\n"
        "end\n",
        &vitaChdirState
    );
    if (vitaChdirState)
        rb_set_errinfo(Qnil);
}
#endif
#ifdef VITA_FS_SELFTEST
/* Diagnostic (MKXP_VITA_FS_V2): how the Vita filesystem answers the file operations the RGSS
 * scripts use (relative, ./, glob, mtime, rename over an existing file, delete) -> stdout.log. */
{
    int vitaFsState = 0;
    rb_eval_string_protect(
"begin\n"
"  root = Dir.pwd\n"
"  res = []\n"
"  t = lambda do |name, &blk|\n"
"    begin\n"
"      r = blk.call\n"
"      res << \"#{name}=#{r.inspect}\"\n"
"    rescue Exception => e\n"
"      res << \"#{name}=EXC(#{e.class}: #{e.message[0, 60]})\"\n"
"    end\n"
"  end\n"
"  t.call('pwd') { root }\n"
"  t.call('write_abs') { File.open('" VITA_GAME_ROOT "' + 'vita_fs_selftest_abs.tmp', 'wb') { |f| f.write('a') }; true }\n"
"  t.call('write_rel') { File.open('vita_fs_selftest_rel.tmp', 'wb') { |f| f.write('b') }; true }\n"
"  t.call('write_dot') { File.open('./vita_fs_selftest_dot.tmp', 'wb') { |f| f.write('c') }; true }\n"
"  t.call('read_rel') { File.binread('vita_fs_selftest_rel.tmp') }\n"
"  t.call('exist_rel') { File.exist?('vita_fs_selftest_rel.tmp') }\n"
"  t.call('exist_dot') { File.exist?('./vita_fs_selftest_dot.tmp') }\n"
"  t.call('glob') { Dir.glob('vita_fs_selftest*.tmp').sort }\n"
"  t.call('glob_save') { Dir.glob('Save*.rvdata2').sort }\n"
"  t.call('mtime') { File.mtime('vita_fs_selftest_rel.tmp').class }\n"
"  t.call('rename_over') { File.rename('vita_fs_selftest_rel.tmp', 'vita_fs_selftest_abs.tmp'); File.binread('vita_fs_selftest_abs.tmp') }\n"
"  t.call('delete') { Dir.glob('vita_fs_selftest*.tmp').each { |f| File.delete(f) }; Dir.glob('vita_fs_selftest*.tmp') }\n"
"  t.call('expand_dot') { File.expand_path('./x') }\n"
"  $stdout.puts \"FS_SELFTEST \" + res.join(' ')\n"
"rescue Exception => e\n"
"  $stdout.puts \"FS_SELFTEST failed #{e.class}: #{e.message}\"\n"
"end\n"
        , &vitaFsState);
    if (vitaFsState)
        rb_set_errinfo(Qnil);
}
#endif

#if defined(VITA_TEST_AUTOPRESS) && !defined(MKXP_VITA_AUDIT_FIXES)
/*
 * MKXP_VITA_AUDIT_FIXES removes this early test hook: $vita_autopress is never filled, but the
 * wrapper ran on every Input.trigger? call (an extra Ruby method and an Array#find per call).
 *
 * TEST VITA: preme automaticamente dei tasti a frame prefissati.
 * $vita_autopress = [[frame, :TASTO], ...]; ogni voce scatta una
 * sola volta, quando Graphics.frame_count >= frame.
 */
{
    int autopressState = 0;

    rb_eval_string_protect(
        "$vita_autopress = []\n"
        "module Input\n"
        "  class << self\n"
        "    alias vita_c_trigger? trigger?\n"
        "    def trigger?(key)\n"
        "      if $vita_autopress\n"
        "        hit = $vita_autopress.find { |f, k| k == key && Graphics.frame_count >= f }\n"
        "        if hit\n"
        "          $vita_autopress.delete(hit)\n"
        "          vita_dbg(\"AUTOPRESS #{key} at frame #{Graphics.frame_count}\")\n"
        "          return true\n"
        "        end\n"
        "      end\n"
        "      vita_c_trigger?(key)\n"
        "    end\n"
        "  end\n"
        "end\n",
        &autopressState
    );

    if (autopressState)
    {
        vitaLogRubyException("autopress");
        rb_set_errinfo(Qnil);
    }
}
#endif

psvDebugScreenPrintf("Sprite class OK\n");

rb_gv_set("$MKXP", Qtrue);

psvDebugScreenPrintf("Loading module_rpg3...\n");

int rgssState = 0;
rb_eval_string_protect(module_rpg3, &rgssState);

if (rgssState) {
    psvDebugScreenPrintf("module_rpg3 FAILED\n");
    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf("module_rpg3 OK\n");
psvDebugScreenPrintf("Loading LISA Actors.rvdata2...\n");

int lisaState = 0;

#ifdef MKXP_VITA_DATA_PATHS
/* Fix (MKXP_VITA_DATA_PATHS): the game's own layout, Data/Actors.rvdata2 and Data/Scripts.rvdata2 (as
 * Game.ini's Scripts=Data\Scripts.rvdata2); the copies in the game root that the early test setup
 * used are still accepted. Missing files: a message pointing to the installation guide. */
VALUE lisaActor = rb_eval_string_protect(
    "f = File.exist?('" VITA_GAME_ROOT "Data/Actors.rvdata2') ? '" VITA_GAME_ROOT "Data/Actors.rvdata2' : '" VITA_GAME_ROOT "Actors.rvdata2'; "
    "data = Marshal.load(File.binread(f)); "
    "data[1].name.to_s",
    &lisaState
);
#else
VALUE lisaActor = rb_eval_string_protect(
    "data = Marshal.load("
    "File.binread('" VITA_GAME_ROOT "Actors.rvdata2')"
    "); "
    "data[1].name.to_s",
    &lisaState
);
#endif

if (lisaState) {
    VALUE exc = rb_errinfo();
    VALUE msg = rb_funcall(exc, rb_intern("message"), 0);

    psvDebugScreenPrintf(
        "LISA Actors FAILED: %s\n",
        StringValueCStr(msg)
    );
#ifdef MKXP_VITA_DATA_PATHS
    psvDebugScreenPrintf(
        "\nGame files not found in " VITA_GAME_ROOT "\n"
        "Copy the folders Data, Graphics, Audio and Fonts of your copy of\n"
        "LISA: The Painful there (extract Game.rgss3a first).\n"
        "See docs/INSTALLATION.md of the LISA-Vita project.\n");
#endif

    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf(
        "LISA Actor #1: %s\n",
        StringValueCStr(lisaActor)
    );
psvDebugScreenPrintf("\nLoading LISA Scripts.rvdata2...\n");

int scriptsState = 0;

#ifdef MKXP_VITA_DATA_PATHS
VALUE scriptsInfo = rb_eval_string_protect(
    "f = File.exist?('" VITA_GAME_ROOT "Data/Scripts.rvdata2') ? '" VITA_GAME_ROOT "Data/Scripts.rvdata2' : '" VITA_GAME_ROOT "Scripts.rvdata2'; "
    "$scripts = Marshal.load(File.binread(f)); "
    "[$scripts.is_a?(Array), $scripts.length]",
    &scriptsState
);
#else
VALUE scriptsInfo = rb_eval_string_protect(
    "$scripts = Marshal.load("
    "File.binread('" VITA_GAME_ROOT "Scripts.rvdata2')"
    "); "
    "[$scripts.is_a?(Array), $scripts.length]",
    &scriptsState
);
#endif

if (scriptsState) {
    VALUE exc = rb_errinfo();
    VALUE msg = rb_funcall(exc, rb_intern("message"), 0);

    psvDebugScreenPrintf(
        "Scripts FAILED: %s\n",
        StringValueCStr(msg)
    );
#ifdef MKXP_VITA_DATA_PATHS
    psvDebugScreenPrintf(
        "\nCould not read " VITA_GAME_ROOT "Data/Scripts.rvdata2\n"
        "See docs/INSTALLATION.md of the LISA-Vita project.\n");
#endif

    rb_set_errinfo(Qnil);
} else {
    VALUE isArray = rb_ary_entry(scriptsInfo, 0);
    VALUE count   = rb_ary_entry(scriptsInfo, 1);

    psvDebugScreenPrintf(
        "Scripts array: %s\n",
        RTEST(isArray) ? "YES" : "NO"
    );

    psvDebugScreenPrintf(
        "Script count: %ld\n",
        NUM2LONG(count)

);

      int firstState = 0;

    VALUE firstInfo = rb_eval_string_protect(
        "entry = nil; "
        "$scripts.each do |s| "
        "  if s.is_a?(Array) && s[1].to_s.length > 0 && s[2].is_a?(String) && s[2].bytesize > 8; "
        "    entry = s; "
        "    break; "
        "  end; "
        "end; "
        "[entry[1].to_s, entry[2]]",
        &firstState
    );

    if (firstState) {
        VALUE exc = rb_errinfo();
        VALUE msg = rb_funcall(exc, rb_intern("message"), 0);

        psvDebugScreenPrintf(
            "First script FAILED: %s\n",
            StringValueCStr(msg)
        );

        rb_set_errinfo(Qnil);
    } else {
        VALUE scriptName = rb_ary_entry(firstInfo, 0);
VALUE compressedData = rb_ary_entry(firstInfo, 1);

long compressedSize = RSTRING_LEN(compressedData);

        psvDebugScreenPrintf(
            "First script: %s\n",
            StringValueCStr(scriptName)
        );

        psvDebugScreenPrintf(
    "Compressed size: %ld bytes\n",
    compressedSize
);

std::vector<unsigned char> decoded(4096);

int zresult;
unsigned long decodedSize;

while (true) {
    decodedSize = decoded.size();

    zresult = uncompress(
        decoded.data(),
        &decodedSize,
        reinterpret_cast<const unsigned char *>(
            RSTRING_PTR(compressedData)
        ),
        compressedSize
    );

    if (zresult != Z_BUF_ERROR)
        break;

    decoded.resize(decoded.size() * 2);
}

if (zresult == Z_OK) {
    decoded.resize(decodedSize + 1);
    decoded[decodedSize] = '\0';

    psvDebugScreenPrintf(
        "Decompressed: %lu bytes\n",
        decodedSize
    );

    psvDebugScreenPrintf("\nEvaluating Vocab...\n");

    int evalState = 0;

    rb_eval_string_protect(
        reinterpret_cast<const char *>(decoded.data()),
        &evalState
    );

    if (evalState) {
        VALUE exc = rb_errinfo();
        VALUE msg = rb_funcall(exc, rb_intern("message"), 0);

        psvDebugScreenPrintf(
            "Vocab eval FAILED: %s\n",
            StringValueCStr(msg)
        );

        rb_set_errinfo(Qnil);
    } else {
        psvDebugScreenPrintf("Vocab eval OK!\n");
    }

    psvDebugScreenPrintf("\nRunning LISA scripts in order...\n");

    VALUE allScripts = rb_gv_get("$scripts");
    long allScriptCount = RARRAY_LEN(allScripts);

    for (long i = 0; i < allScriptCount; ++i) {
        VALUE script = rb_ary_entry(allScripts, i);

        if (!RB_TYPE_P(script, RUBY_T_ARRAY))
            continue;

        VALUE scriptName = rb_ary_entry(script, 1);
        VALUE compressedScript = rb_ary_entry(script, 2);

        if (!RB_TYPE_P(compressedScript, RUBY_T_STRING))
            continue;

        long compressedLen = RSTRING_LEN(compressedScript);

        std::vector<unsigned char> source(4096);

        int scriptZResult;
        unsigned long sourceSize;

        while (true) {
            sourceSize = source.size();

            scriptZResult = uncompress(
                source.data(),
                &sourceSize,
                reinterpret_cast<const unsigned char *>(
                    RSTRING_PTR(compressedScript)
                ),
                compressedLen
            );

            if (scriptZResult != Z_BUF_ERROR)
                break;

            source.resize(source.size() * 2);
        }

        if (scriptZResult != Z_OK) {
            psvDebugScreenPrintf(
                "[%ld] ZLIB FAILED: %d\n",
                i,
                scriptZResult
            );
            break;
        }

        source.resize(sourceSize + 1);
        source[sourceSize] = '\0';

        int scriptState = 0;

        VITA_FREEZE_MARK1(SCRIPT_EVAL_ENTER, i);
#ifdef MKXP_VITA_DIAG
        /* The last script (Main) runs the game: its span closes only at exit. */
        char vitaDiagScript[72];
        snprintf(vitaDiagScript, sizeof(vitaDiagScript), "[%ld] %.60s", i,
                 RB_TYPE_P(scriptName, RUBY_T_STRING) ? StringValueCStr(scriptName) : "?");
        VITA_DIAG_MARK("script_eval_begin", vitaDiagScript);
        const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
        rb_eval_string_protect(
            reinterpret_cast<const char *>(source.data()),
            &scriptState
        );
#ifdef MKXP_VITA_DIAG
        vitaDiagSpan(VD_SCRIPT_EVAL, vitaDiagT0, 0, vitaDiagScript);
#endif
        VITA_FREEZE_MARK1(SCRIPT_EVAL_EXIT, i);
#ifdef MKXP_VITA_INTERP_HYBRID
        /* VitaInterpHybrid (post-script hooks): Game_Interpreter's methods as its own section defines them. */
        if (!scriptState && RB_TYPE_P(scriptName, RUBY_T_STRING) && std::strcmp(StringValueCStr(scriptName), "Game_Interpreter") == 0) {
            int snapState = 0;
            rb_eval_string_protect("$vita_interp_snap = (Game_Interpreter.instance_methods(false) + Game_Interpreter.private_instance_methods(false))"
                                   ".to_h { |m| [m, Game_Interpreter.instance_method(m)] }", &snapState);
        }
#endif
#ifdef MKXP_VITA_ERROR_SCREEN
        vitaScriptsRan = true;
#endif

        if (scriptState) {
            vitaLogRubyException(StringValueCStr(scriptName));
#ifdef MKXP_VITA_FREEZE_PROBE
            {
                /* One-shot error path: class, message and up to 32 backtrace entries for freeze.log. */
                VALUE fexc = rb_errinfo();
                std::string fcls, fmsg, fbt;
                if (!NIL_P(fexc)) {
                    VALUE c = rb_class_name(rb_obj_class(fexc));
                    fcls = StringValueCStr(c);
                    VALUE m = rb_funcall(fexc, rb_intern("message"), 0);
                    fmsg = StringValueCStr(m);
                    VALUE bt = rb_funcall(fexc, rb_intern("backtrace"), 0);
                    if (RB_TYPE_P(bt, T_ARRAY)) {
                        const long n = RARRAY_LEN(bt);
                        for (long k = 0; k < n && k < 32; ++k) {
                            VALUE l = rb_obj_as_string(rb_ary_entry(bt, k));
                            fbt += "  @ ";
                            fbt += StringValueCStr(l);
                            fbt += "\n";
                        }
                        if (n > 32)
                            fbt += "  ... (" + std::to_string(n - 32) + " more)\n";
                    }
                }
                vitaFreezeScriptFailed(i, StringValueCStr(scriptName), fcls.c_str(), fmsg.c_str(), fbt.c_str());
            }
#endif

            VALUE exc = rb_errinfo();
#ifdef MKXP_VITA_ERROR_SCREEN
            vitaFailedExc = exc;
            rb_gc_register_address(&vitaFailedExc);
#endif
#ifdef MKXP_VITA_CRUMB_BUFFER
            {
                /* Breadcrumbs still in RAM go to the card before the error screen (exception kept). */
                int st = 0;
                rb_eval_string_protect("VitaCrumb.flush if defined?(VitaCrumb)", &st);
                rb_set_errinfo(exc);
            }
#endif
            VALUE msg = rb_funcall(
                exc,
                rb_intern("message"),
                0
            );

            psvDebugScreenPrintf(
                "\nSCRIPT FAILED!\n"
                "Index: %ld\n"
                "Name: %s\n"
                "Error: %s\n",
                i,
                StringValueCStr(scriptName),
                StringValueCStr(msg)
            );

            rb_set_errinfo(Qnil);
            break;
        }

        psvDebugScreenPrintf(
            "[%ld] %s OK\n",
            i,
            StringValueCStr(scriptName)
        );
    }
    psvDebugScreenPrintf(
        "Source begins:\n%.200s\n",
        reinterpret_cast<const char *>(decoded.data())
    );
} else {
    psvDebugScreenPrintf(
        "Zlib FAILED: %d\n",
        zresult
    );
}
    } 
}
}
}

    int state = 0;

VALUE value = rb_eval_string_protect(
    "t = Table.new(4,4,1); "
    "t[1,2] = 123; "
    "t[1,2]",
    &state
);

if (state) {
    psvDebugScreenPrintf("Table test FAILED\n");
    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf(
        "Table test: %ld\n",
        NUM2LONG(value)
    );
}

state = 0;

value = rb_eval_string_protect(
    "c = Color.new(255,10,20,255); "
    "c.red.to_i",
    &state
);

if (state) {
    psvDebugScreenPrintf("Color test FAILED\n");
    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf(
        "Color test: %ld\n",
        NUM2LONG(value)
    );
}

state = 0;

value = rb_eval_string_protect(
    "t = Tone.new(-100,20,30,40); "
    "t.red.to_i",
    &state
);

if (state) {
    psvDebugScreenPrintf("Tone test FAILED\n");
    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf(
        "Tone test: %ld\n",
        NUM2LONG(value)
    );
}

state = 0;

value = rb_eval_string_protect(
    "r = Rect.new(10,20,320,240); "
    "r.width",
    &state
);

if (state) {
    psvDebugScreenPrintf("Rect test FAILED\n");
    rb_set_errinfo(Qnil);
} else {
    psvDebugScreenPrintf(
        "Rect test: %ld\n",
        NUM2LONG(value)
    );
}

psvDebugScreenPrintf("\nmkxp-z core bindings OK\n");

sceClibPrintf("VITA_TRACE about to enter game loop\n");

#ifdef MKXP_VITA_ERROR_SCREEN
    if (vitaScriptsRan) {
        if (!NIL_P(vitaFailedExc) && !RTEST(rb_obj_is_kind_of(vitaFailedExc, rb_eSystemExit)))
            vitaShowErrorScreen(vitaFailedExc);
        FILE *qf = fopen(VITA_GAME_ROOT "qa.log", "a");
        if (qf) {
            fprintf(qf, "GAME_END %s\n", NIL_P(vitaFailedExc) ? "scripts_returned"
                                       : RTEST(rb_obj_is_kind_of(vitaFailedExc, rb_eSystemExit)) ? "exit" : "error");
            fclose(qf);
        }
        vitaExitApp(0);
    }
#endif
    VITA_FREEZE_MARK(MAIN_IDLE);
    while (1)
        sceKernelDelayThread(1000000);

    return 0;
}
