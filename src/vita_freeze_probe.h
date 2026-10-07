/*
 * Vita-only freeze probe (MKXP_VITA_FREEZE_PROBE, OFF by default). Diagnostic only.
 *
 * Markers are written to a static RAM ring buffer (no allocation, no locks, no I/O). A separate
 * watchdog thread (sceKernel only) dumps the state to <game root>/freeze.log when
 * Graphics.update stops completing frames. Without the define every macro expands to nothing.
 */
#ifndef VITA_FREEZE_PROBE_H
#define VITA_FREEZE_PROBE_H

#ifdef MKXP_VITA_FREEZE_PROBE

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum VitaFreezePhase
{
    VITA_FZ_NONE = 0,

    /* Native frame path (main thread) */
    VITA_FZ_FRAME_BEGIN = 1,
    VITA_FZ_FRAME_END,
    VITA_FZ_GRAPHICS_UPDATE_ENTER,
    VITA_FZ_GRAPHICS_UPDATE_EXIT,
    VITA_FZ_VITA_RENDER_ENTER,
    VITA_FZ_VITA_RENDER_EXIT,
    VITA_FZ_PREPARE_ENTER,
    VITA_FZ_PREPARE_EXIT,
    VITA_FZ_SCENE_FBO_SETUP_ENTER,
    VITA_FZ_SCENE_FBO_SETUP_EXIT,
    VITA_FZ_COMPOSITE_ENTER,
    VITA_FZ_COMPOSITE_EXIT,
    VITA_FZ_PREBLIT_DELAY_ENTER,
    VITA_FZ_PREBLIT_DELAY_EXIT,
    VITA_FZ_FINAL_BLIT_ENTER,
    VITA_FZ_FINAL_BLIT_EXIT,
    VITA_FZ_SWAP_ENTER,
    VITA_FZ_SWAP_EXIT,
    VITA_FZ_PERF_WRITE_ENTER,
    VITA_FZ_PERF_WRITE_EXIT,
    VITA_FZ_INPUT_UPDATE_ENTER,
    VITA_FZ_INPUT_UPDATE_EXIT,

    /* Ruby hooks (prepended modules, installed right before the "Main" script) */
    VITA_FZ_SCENE_MAP_UPDATE_ENTER = 30,
    VITA_FZ_SCENE_MAP_UPDATE_EXIT,
    VITA_FZ_GAME_MAP_UPDATE_ENTER,
    VITA_FZ_GAME_MAP_UPDATE_EXIT,
    VITA_FZ_GAME_PLAYER_UPDATE_ENTER,
    VITA_FZ_GAME_PLAYER_UPDATE_EXIT,
    VITA_FZ_MAP_EVENTS_UPDATE_ENTER,
    VITA_FZ_MAP_EVENTS_UPDATE_EXIT,
    VITA_FZ_SPRITESET_MAP_UPDATE_ENTER,
    VITA_FZ_SPRITESET_MAP_UPDATE_EXIT,
    VITA_FZ_INTERP_UPDATE_ENTER,     /* Game_Interpreter#update = @fiber.resume; a0=event_id a1=depth */
    VITA_FZ_INTERP_UPDATE_EXIT,
    VITA_FZ_INTERP_CMD,              /* a0=map_id a1=event_id a2=(depth<<16)|index a3=code */
    VITA_FZ_RUBY_SCRIPT_ENTER,       /* command_355 eval; a0=hash a1=len a2=extra 655 lines */
    VITA_FZ_RUBY_SCRIPT_EXIT,
    VITA_FZ_MOVE_SCRIPT_ENTER,       /* move route code 45 (eval); a0=hash a1=len a2=character id */
    VITA_FZ_MOVE_SCRIPT_EXIT,
    VITA_FZ_CACHE_LOAD_ENTER,        /* Cache.load_bitmap; a0=hash(folder+file) */
    VITA_FZ_CACHE_LOAD_EXIT,
    VITA_FZ_MAP_SETUP_ENTER,         /* Game_Map#setup; a0=map_id */
    VITA_FZ_MAP_SETUP_EXIT,
    VITA_FZ_PLAYER_TRANSFER_ENTER,   /* Game_Player#perform_transfer */
    VITA_FZ_PLAYER_TRANSFER_EXIT,
    VITA_FZ_SPRITESET_CREATE_ENTER,  /* unused since v2 (initialize is not hooked); kept for numbering */
    VITA_FZ_SPRITESET_CREATE_EXIT,
    VITA_FZ_SPRITESET_DISPOSE_ENTER,
    VITA_FZ_SPRITESET_DISPOSE_EXIT,
    VITA_FZ_SPRITESET_REFRESH_ENTER, /* Spriteset_Map#refresh_characters (Galv effects recreate) */
    VITA_FZ_SPRITESET_REFRESH_EXIT,

    /* Coroutine backend (linker --wrap around libruby's Context.o entry points) */
    VITA_FZ_CO_TRANSFER_ENTER = 60,  /* a0=src a1=dst a2=transfer# a3=thread id */
    VITA_FZ_CO_TRANSFER_EXIT,        /* a0=src a1=dst a2=transfer# a3=returned */
    VITA_FZ_CO_TRANSFER_EXIT_ENTER,  /* coroutine_transfer_exit: normally never returns */
    VITA_FZ_CO_TRANSFER_EXIT_RETURN, /* fallback path returned (main/unstarted target) */
    VITA_FZ_CO_INIT,
    VITA_FZ_CO_DESTROY_ENTER,
    VITA_FZ_CO_DESTROY_EXIT,

    /* main.cpp script loader / idle */
    VITA_FZ_SCRIPT_EVAL_ENTER = 70,  /* a0=script index */
    VITA_FZ_SCRIPT_EVAL_EXIT,
    VITA_FZ_SCRIPT_FAILED,           /* a0=script index; exception text kept in RAM */
    VITA_FZ_MAIN_IDLE,               /* main() reached its final sleep loop */
    VITA_FZ_RUBY_PROBE_INSTALLED,

    VITA_FZ_PHASE_COUNT
};

void vitaFreezeMark(uint32_t phase, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3);
/* Called once per completed Graphics.update: FRAME_END, ++frame_counter, FRAME_BEGIN. */
void vitaFreezeFrameDone(void);
void vitaFreezeProbeStart(void);
void vitaFreezeScriptFailed(long index, const char *name, const char *cls, const char *msg,
                            const char *backtrace);
/* Defines module VitaProbe (native marker functions) for the Ruby hooks. */
void vitaFreezeRubyInit(void);
/* Evaluates the Ruby hook definitions once. Called from rgss_main (vita_freeze_install_hooks). */
int vitaFreezeRubyInstallHooks(void);

#ifdef __cplusplus
}
#endif

#define VITA_FREEZE_MARK(ph) vitaFreezeMark(VITA_FZ_##ph, 0, 0, 0, 0)
#define VITA_FREEZE_MARK1(ph, a0) vitaFreezeMark(VITA_FZ_##ph, (uintptr_t)(a0), 0, 0, 0)

#else

#define VITA_FREEZE_MARK(ph)
#define VITA_FREEZE_MARK1(ph, a0)

#endif

#endif /* VITA_FREEZE_PROBE_H */
