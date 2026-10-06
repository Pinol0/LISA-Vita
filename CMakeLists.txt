cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(
            CMAKE_TOOLCHAIN_FILE
            "$ENV{VITASDK}/share/vita.toolchain.cmake"
            CACHE PATH "toolchain file"
        )
    else()
        message(FATAL_ERROR "VITASDK is not defined")
    endif()
endif()

project(mkxp_vita_minimal LANGUAGES C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Lisa: The Painful" CACHE STRING "LiveArea title")
set(VITA_TITLEID "MKXP00001" CACHE STRING "Title ID")
set(VITA_VERSION "01.00")

# Dependencies (deps/setup.sh fetches, patches and builds them there).
set(MKXP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/mkxp-z" CACHE PATH "patched mkxp-z source tree")
set(RUBY_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/ruby/ruby-3.1.6" CACHE PATH "patched Ruby 3.1.6 source tree")
set(RUBY_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/ruby/build" CACHE PATH "Ruby build directory (libruby-static.a, .ext/include)")
set(VITA_COMPAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/vita-compat" CACHE PATH "vita-compat (POSIX shims for Ruby)")

add_executable(mkxp_vita_minimal
    src/main.cpp
    src/binding-shim.cpp
    src/bitmap-binding-vita.cpp
    src/bitmap-vita-minimal.cpp
    src/vita-font.cpp
    common/debugScreen.c

    ${MKXP_DIR}/src/etc/table.cpp
    ${MKXP_DIR}/binding/table-binding.cpp
    ${MKXP_DIR}/src/etc/etc.cpp
    ${MKXP_DIR}/binding/etc-binding.cpp
    ${MKXP_DIR}/binding/module_rpg.cpp
    ${MKXP_DIR}/binding/windowvx-binding.cpp
    ${MKXP_DIR}/binding/viewport-binding.cpp
    ${MKXP_DIR}/binding/tilemapvx-binding.cpp
    ${MKXP_DIR}/binding/plane-binding.cpp
    ${MKXP_DIR}/binding/sprite-binding.cpp
    src/gl-fun-vita.cpp

    src/shell/config_shim.cpp
    src/shell/sharedstate_test.cpp

    ${MKXP_DIR}/src/display/gl/vertex.cpp
    ${MKXP_DIR}/src/display/gl/glstate.cpp
    ${MKXP_DIR}/src/display/gl/shader.cpp
    ${MKXP_DIR}/src/display/gl/gl-meta.cpp
    ${MKXP_DIR}/src/display/gl/scene.cpp
    ${MKXP_DIR}/src/display/viewport.cpp
    ${MKXP_DIR}/src/display/gl/texpool.cpp
    ${MKXP_DIR}/src/display/gl/tilequad.cpp
    ${MKXP_DIR}/src/display/sprite.cpp
    ${MKXP_DIR}/src/display/windowvx.cpp
    ${MKXP_DIR}/src/display/tilemapvx.cpp
    ${MKXP_DIR}/src/display/autotilesvx.cpp
    ${MKXP_DIR}/src/display/gl/tileatlasvx.cpp
    ${MKXP_DIR}/src/display/plane.cpp
)

target_compile_definitions(mkxp_vita_minimal PRIVATE
    GLES2_HEADER
    MKXP_VITA_MINIMAL_TEST
)

target_link_options(mkxp_vita_minimal PRIVATE
    -Wl,--undefined=pthread_once
    -Wl,--section-start=.init_array=0x81800000
    -Wl,--gc-sections
    -Wl,-Map=/tmp/mkxp-vita.map
)

# Diagnostic only: leak one 16-byte block at the start of main() to shift the heap layout.
option(MKXP_VITA_HEAP_SHIFT_TEST "Allocate 16 bytes before ruby_setup()" OFF)
if(MKXP_VITA_HEAP_SHIFT_TEST)
    set_source_files_properties(src/main.cpp PROPERTIES COMPILE_DEFINITIONS MKXP_VITA_HEAP_SHIFT_TEST)
endif()

# Diagnostic only: reproduce the pthread libruby root fiber allocation (324 bytes) with arm32 libruby.
option(MKXP_VITA_ROOTFIBER_ALLOC_TEST "Allocate the root rb_fiber_t with the pthread size" OFF)
if(MKXP_VITA_ROOTFIBER_ALLOC_TEST)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-rootfiber-alloc-test.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=ruby_mimmalloc)
endif()

# Diagnostic only: pull the libpthread members the pthread Context.o drags in, without calling them.
option(MKXP_VITA_FORCE_PTHREAD_MEMBERS "Force-link the 5 extra libpthread members" OFF)
if(MKXP_VITA_FORCE_PTHREAD_MEMBERS)
    target_link_options(mkxp_vita_minimal PRIVATE
        -Wl,--undefined=pthread_attr_setstack
        -Wl,--undefined=pthread_attr_setstackaddr
        -Wl,--undefined=pthread_cancel
        -Wl,--undefined=pthread_mutexattr_init
        -Wl,--undefined=pthread_mutexattr_settype)
endif()

# Diagnostic only: pull just the pthread_attr_setstack/setstackaddr pair, without calling it.
option(MKXP_VITA_FORCE_PTHREAD_ATTR_STACK "Force-link pthread_attr_setstack(addr) only" OFF)
if(MKXP_VITA_FORCE_PTHREAD_ATTR_STACK)
    target_link_options(mkxp_vita_minimal PRIVATE
        -Wl,--undefined=pthread_attr_setstack
        -Wl,--undefined=pthread_attr_setstackaddr)
endif()

# Diagnostic only: pull just pthread_cancel, without calling it.
option(MKXP_VITA_FORCE_PTHREAD_CANCEL "Force-link pthread_cancel only" OFF)
if(MKXP_VITA_FORCE_PTHREAD_CANCEL)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--undefined=pthread_cancel)
endif()

# Per-frame qa.log markers (FRAME/SWAP_*/FINAL_BLIT_*) in vitaRenderFrame(); ON keeps the old behaviour.
option(MKXP_VITA_PER_FRAME_QA_LOG "Write per-frame markers to qa.log" ON)
if(NOT MKXP_VITA_PER_FRAME_QA_LOG)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_NO_PER_FRAME_QA_LOG)
endif()

# Sync test only: glFinish() right before vglSwapBuffers() in vitaRenderFrame().
option(MKXP_VITA_GLFINISH_BEFORE_SWAP "Call glFinish() before each vglSwapBuffers()" OFF)
if(MKXP_VITA_GLFINISH_BEFORE_SWAP)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GLFINISH_BEFORE_SWAP)
endif()

# Log bisect only: per-frame qa.log call sites to disable, e.g. "FINAL_BLIT_BEFORE;FINAL_BLIT_AFTER;FRAME".
set(MKXP_VITA_DISABLE_QA_SITES "" CACHE STRING "Per-frame qa.log call sites to disable")
foreach(site IN LISTS MKXP_VITA_DISABLE_QA_SITES)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_NO_QA_${site})
endforeach()

# Timing test only: microseconds of sceKernelDelayThread right before the final blit (0 = off).
set(MKXP_VITA_PRE_BLIT_DELAY_US 0 CACHE STRING "Pre-blit delay in microseconds (0 = disabled)")
if(MKXP_VITA_PRE_BLIT_DELAY_US GREATER 0)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PRE_BLIT_DELAY_US=${MKXP_VITA_PRE_BLIT_DELAY_US})
endif()

# Test only: SELECT cycles the pre-blit delay 10000/5000/2000/0 us at runtime (needs PRE_BLIT_DELAY_US > 0).
option(MKXP_VITA_DELAY_TOGGLE "SELECT cycles the pre-blit delay at runtime" OFF)
if(MKXP_VITA_DELAY_TOGGLE)
    if(NOT MKXP_VITA_PRE_BLIT_DELAY_US GREATER 0)
        message(FATAL_ERROR "MKXP_VITA_DELAY_TOGGLE requires MKXP_VITA_PRE_BLIT_DELAY_US > 0")
    endif()
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DELAY_TOGGLE)
endif()

# Timing test only: microseconds of busy-wait (no sleep/yield) right before the final blit (0 = off).
set(MKXP_VITA_PRE_BLIT_BUSYWAIT_US 0 CACHE STRING "Pre-blit busy-wait in microseconds (0 = disabled)")
if(MKXP_VITA_PRE_BLIT_BUSYWAIT_US GREATER 0)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PRE_BLIT_BUSYWAIT_US=${MKXP_VITA_PRE_BLIT_BUSYWAIT_US})
endif()

# Timing test only: microseconds of sceKernelDelayThread after each vglSwapBuffers (0 = off).
set(MKXP_VITA_POST_SWAP_DELAY_US 0 CACHE STRING "Post-swap delay in microseconds (0 = disabled)")
if(MKXP_VITA_POST_SWAP_DELAY_US GREATER 0)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_POST_SWAP_DELAY_US=${MKXP_VITA_POST_SWAP_DELAY_US})
endif()

# Profiling only: per-region frame timing kept in RAM, summary on tty0 every 300 frames.
# Also silences the per-frame VITA_FRAME_TRACE prints (one-shot markers stay).
option(MKXP_VITA_PERF_PROFILE "Time vitaRenderFrame() regions in RAM and print periodic summaries" OFF)
if(MKXP_VITA_PERF_PROFILE)
    # Also the subsystem timers/counters of mkxp-z/src/vita_perf.h (tilemap/sprite/window, gl table).
    set_property(SOURCE src/shell/sharedstate_test.cpp
                        src/gl-fun-vita.cpp
                        ${MKXP_DIR}/src/display/tilemapvx.cpp
                        ${MKXP_DIR}/src/display/sprite.cpp
                        ${MKXP_DIR}/src/display/windowvx.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PERF_PROFILE)
endif()

# Profiling only: also append the summaries to ux0:data/ruby_vita_test/perf.log,
# one open/write/close every 300 frames, after vglSwapBuffers.
option(MKXP_VITA_PERF_PROFILE_FILE "Append MKXP_VITA_PERF_PROFILE summaries to perf.log" OFF)
if(MKXP_VITA_PERF_PROFILE_FILE)
    if(NOT MKXP_VITA_PERF_PROFILE)
        message(FATAL_ERROR "MKXP_VITA_PERF_PROFILE_FILE requires MKXP_VITA_PERF_PROFILE")
    endif()
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PERF_PROFILE_FILE)
endif()

# Diagnostic only: the "DBG ..." psvDebugScreenPrintf calls of the WindowVX render path
# (prepare/rebuildBaseVert/redrawBaseTex). OFF compiles them out. Affects windowvx.cpp only.
option(MKXP_VITA_WINDOW_DEBUG_PRINTS "Keep the WindowVX DBG debug-screen prints" OFF)
if(MKXP_VITA_WINDOW_DEBUG_PRINTS)
    set_property(SOURCE ${MKXP_DIR}/src/display/windowvx.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_WINDOW_DEBUG_PRINTS)
endif()

# Liveness test only: counts completed vitaRenderFrame() calls and appends to
# ux0:data/ruby_vita_test/heartbeat.log after vglSwapBuffers at frames 60,120,...,3840,
# plus one MAP_RENDER_ENTER line with the count at NEW_GAME_ENTER.
option(MKXP_VITA_HEARTBEAT_LOG "Write heartbeat.log at a few fixed frame counts" OFF)
if(MKXP_VITA_HEARTBEAT_LOG)
    set_property(SOURCE src/shell/sharedstate_test.cpp src/main.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_HEARTBEAT_LOG)
endif()

# Test only: GLMeta blits (used only by TileAtlasVX::build here) draw a quad with the simple shader
# instead of calling glBlitFramebuffer. Affects gl-meta.cpp only.
option(MKXP_VITA_TILEATLAS_SHADER_BLIT "Build the VX tile atlas with the shader blit fallback" OFF)
if(MKXP_VITA_TILEATLAS_SHADER_BLIT)
    set_property(SOURCE ${MKXP_DIR}/src/display/gl/gl-meta.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TILEATLAS_SHADER_BLIT)
endif()

# Fix: GLMeta's shader blit (used here only by TileAtlasVX::build with MKXP_VITA_TILEATLAS_SHADER_BLIT)
# projects onto the real offscreen target size instead of the fixed 544x416. Affects gl-meta.cpp only.
option(MKXP_VITA_BLIT_PROJ_FIX "Use the blit target size as projection in the GLMeta shader blit" OFF)
if(MKXP_VITA_BLIT_PROJ_FIX)
    set_property(SOURCE ${MKXP_DIR}/src/display/gl/gl-meta.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BLIT_PROJ_FIX)
endif()

# Diagnostic only: log TilemapVX draw state and dump (TGA) the bound scene framebuffer around the
# first ground/above DrawElements with groundQuads > 0. Uses glFinish/glReadPixels once. tilemapvx.cpp only.
option(MKXP_VITA_DUMP_TILEMAP_DRAW "Dump the first TilemapVX draw" OFF)
if(MKXP_VITA_DUMP_TILEMAP_DRAW)
    set_property(SOURCE ${MKXP_DIR}/src/display/tilemapvx.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DUMP_TILEMAP_DRAW)
endif()

# Diagnostic only: log and dump (TGA) the first TileAtlasVX::build with source bitmaps to
# ux0:data/ruby_vita_test/. Uses glFinish/glReadPixels once. Affects tileatlasvx.cpp only.
option(MKXP_VITA_DUMP_TILEATLAS "Dump the first VX tile atlas and its sources" OFF)
if(MKXP_VITA_DUMP_TILEATLAS)
    set_property(SOURCE ${MKXP_DIR}/src/display/gl/tileatlasvx.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DUMP_TILEATLAS)
endif()

# Diagnostic only: logs the first call into the Ruby coroutine backend.
option(MKXP_VITA_COROUTINE_PROBE "Log first coroutine backend calls to qa.log" OFF)
if(MKXP_VITA_COROUTINE_PROBE)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-coroutine-probe.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE
        -Wl,--wrap=coroutine_initialize_main
        -Wl,--wrap=coroutine_initialize
        -Wl,--wrap=coroutine_transfer
    )
endif()

target_compile_options(mkxp_vita_minimal PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu17>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>

    -ffunction-sections
    -fdata-sections
)

target_include_directories(mkxp_vita_minimal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common

    ${RUBY_SRC_DIR}/include
    ${RUBY_SRC_DIR}
    ${RUBY_BUILD_DIR}/.ext/include/arm-eabi
    ${VITA_COMPAT_DIR}/include

    ${MKXP_DIR}
    ${MKXP_DIR}/src
    ${MKXP_DIR}/src/etc
    ${MKXP_DIR}/src/util
    ${MKXP_DIR}/binding

    $ENV{VITASDK}/arm-vita-eabi/include/SDL2
    $ENV{VITASDK}/arm-vita-eabi/include

    ${MKXP_DIR}/src/display
    ${MKXP_DIR}/src/display/gl

    ${MKXP_DIR}/build/shader
)

set(MKXP_RUBY_STATIC_LIB "${RUBY_BUILD_DIR}/libruby-static.a"
    CACHE FILEPATH "Ruby static library linked into mkxp_vita_minimal")

# Real Bitmap#clear / clear_rect / fill_rect / gradient_fill_rect / blt on the CPU pixel copy
# (mkxp-z semantics) instead of the Ruby no-ops of the main.cpp engine stub.
option(MKXP_VITA_BITMAP_WINDOW_OPS "Real Bitmap clear/clear_rect/fill_rect/gradient_fill_rect/blt" OFF)
option(MKXP_VITA_DEBUG_BITMAP_WINDOW "Log Bitmap window operations around battles (needs WINDOW_OPS)" OFF)
set(VITA_TAG_BMPWIN "")
if(MKXP_VITA_BITMAP_WINDOW_OPS)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp src/main.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BITMAP_WINDOW_OPS)
    set(VITA_TAG_BMPWIN " bitmap_window_ops=ON")
endif()
if(MKXP_VITA_DEBUG_BITMAP_WINDOW)
    if(NOT MKXP_VITA_BITMAP_WINDOW_OPS)
        message(FATAL_ERROR "MKXP_VITA_DEBUG_BITMAP_WINDOW requires MKXP_VITA_BITMAP_WINDOW_OPS")
    endif()
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp src/main.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_BITMAP_WINDOW)
endif()

# Final presentation mode (sharedstate_test.cpp only). Empty = decided by MKXP_VITA_ASPECT_CORRECT
# (unchanged behaviour). 0 = fullscreen stretch 960x544 (same code as ASPECT_CORRECT OFF);
# 1 = aspect 711x544 (same code as ASPECT_CORRECT ON); 2 = pixel-perfect 1x, 544x416 centred
# (bars 208/208 left/right, 64/64 top/bottom), always NEAREST.
set(MKXP_VITA_PRESENTATION_MODE "" CACHE STRING "Final presentation: empty (legacy), 0 fullscreen, 1 aspect, 2 pixel-perfect 1x")
set(VITA_TAG_PRESENT "")
if(MKXP_VITA_PRESENTATION_MODE STREQUAL "0")
    if(MKXP_VITA_ASPECT_CORRECT)
        message(FATAL_ERROR "MKXP_VITA_PRESENTATION_MODE=0 (fullscreen) conflicts with MKXP_VITA_ASPECT_CORRECT=ON")
    endif()
elseif(MKXP_VITA_PRESENTATION_MODE STREQUAL "1")
    set(MKXP_VITA_ASPECT_CORRECT ON CACHE BOOL "Aspect-correct final blit with black side bars" FORCE)
elseif(MKXP_VITA_PRESENTATION_MODE STREQUAL "2")
    if(MKXP_VITA_ASPECT_CORRECT)
        message(FATAL_ERROR "MKXP_VITA_PRESENTATION_MODE=2 (pixel-perfect) conflicts with MKXP_VITA_ASPECT_CORRECT=ON")
    endif()
    if(MKXP_VITA_FINAL_FILTER STREQUAL "LINEAR")
        message(FATAL_ERROR "MKXP_VITA_PRESENTATION_MODE=2 is a 1:1 copy: MKXP_VITA_FINAL_FILTER must be NEAREST")
    endif()
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS MKXP_VITA_PIXEL_PERFECT)
    set(VITA_TAG_PRESENT " present=pixelperfect_544x416")
elseif(NOT MKXP_VITA_PRESENTATION_MODE STREQUAL "")
    message(FATAL_ERROR "MKXP_VITA_PRESENTATION_MODE must be empty, 0, 1 or 2")
endif()

# Final presentation only (sharedstate_test.cpp): 544x416 scaled uniformly to ASPECT_WIDTH x 544,
# centred with black bars, instead of stretched to 960x544. RGSS resolution unchanged.
option(MKXP_VITA_ASPECT_CORRECT "Aspect-correct final blit with black side bars" OFF)
set(MKXP_VITA_ASPECT_WIDTH 711 CACHE STRING "Final blit width with MKXP_VITA_ASPECT_CORRECT (544 px high)")
set(MKXP_VITA_FINAL_FILTER "NEAREST" CACHE STRING "Final blit filter: NEAREST (current) or LINEAR")
set(VITA_TAG_ASPECT "")
if(MKXP_VITA_ASPECT_CORRECT)
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS MKXP_VITA_ASPECT_CORRECT MKXP_VITA_ASPECT_WIDTH=${MKXP_VITA_ASPECT_WIDTH})
    set(VITA_TAG_ASPECT " aspect=${MKXP_VITA_ASPECT_WIDTH}x544")
endif()
if(MKXP_VITA_FINAL_FILTER STREQUAL "LINEAR")
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS MKXP_VITA_FINAL_FILTER_LINEAR)
    set(VITA_TAG_ASPECT "${VITA_TAG_ASPECT} final_filter=linear")
elseif(MKXP_VITA_FINAL_FILTER STREQUAL "SHARP_BILINEAR")
    if(NOT MKXP_VITA_ASPECT_CORRECT)
        message(FATAL_ERROR "MKXP_VITA_FINAL_FILTER=SHARP_BILINEAR is implemented for the aspect-correct presentation")
    endif()
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS MKXP_VITA_FINAL_SHARP_BILINEAR)
    set(VITA_TAG_ASPECT "${VITA_TAG_ASPECT} final_filter=sharp_bilinear")
elseif(NOT MKXP_VITA_FINAL_FILTER STREQUAL "NEAREST")
    message(FATAL_ERROR "MKXP_VITA_FINAL_FILTER must be NEAREST, LINEAR or SHARP_BILINEAR")
endif()

# Diagnostic only: log the first 100 draw_text calls of each battle (font state, text_size,
# renderer ppem/surface/dest) to qa.log. Affects bitmap-binding-vita.cpp and main.cpp only.
option(MKXP_VITA_DEBUG_TEXT_LOG "Log draw_text calls at battle start" OFF)
if(MKXP_VITA_DEBUG_TEXT_LOG)
    set_property(SOURCE src/bitmap-binding-vita.cpp src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_TEXT_LOG)
endif()

# Diagnostic only: log every Bitmap stretch_blt/radial_blur to qa.log and dump the first 8
# results as TGA (ux0:data/ruby_vita_test/bmpdump_NN_<op>.tga). Affects bitmap-vita-minimal.cpp only.
option(MKXP_VITA_DEBUG_BITMAP_DUMP "Log and dump Bitmap CPU operations" OFF)
if(MKXP_VITA_DEBUG_BITMAP_DUMP)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_BITMAP_DUMP)
endif()

# Debug only, never in a normal build: boot shortcuts to the first battle (map 41 event 2, troop 3).
# CHECKPOINT: if ux0:data/ruby_vita_test/debug_checkpoint_battle1.rvdata2 exists, load it and boot
# into Scene_Map; otherwise boot normally and record it on the first idle frame on map 41.
# DIRECT_BATTLE: renderer/binding harness, new game straight into Scene_Battle with troop 3.
option(MKXP_VITA_DEBUG_BOOT_CHECKPOINT "DEBUG: boot from/record the pre-first-battle checkpoint" OFF)
# DEBUG: soak test (map transfers, menu, battles in a loop) when R is held at boot; never saves.
option(MKXP_VITA_DEBUG_SOAK "DEBUG: soak test while R is held at boot (soak.log)" OFF)
if(MKXP_VITA_DEBUG_SOAK)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_SOAK)
endif()
option(MKXP_VITA_DEBUG_DIRECT_BATTLE "DEBUG: boot straight into Scene_Battle (troop 3)" OFF)
if(MKXP_VITA_DEBUG_BOOT_CHECKPOINT AND MKXP_VITA_DEBUG_DIRECT_BATTLE)
    message(FATAL_ERROR "MKXP_VITA_DEBUG_BOOT_CHECKPOINT and MKXP_VITA_DEBUG_DIRECT_BATTLE are exclusive")
endif()
set(VITA_TAG_DEBUG_BOOT "")
if(MKXP_VITA_DEBUG_BOOT_CHECKPOINT)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_BOOT_CHECKPOINT)
    set(VITA_TAG_DEBUG_BOOT " debug_boot=checkpoint")
endif()
if(MKXP_VITA_DEBUG_DIRECT_BATTLE)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_DIRECT_BATTLE)
    set(VITA_TAG_DEBUG_BOOT " debug_boot=direct_battle")
endif()

# Diagnostic only (vita_diag.cpp, mkxp-z/src/vita_diag.h). Both OFF by default; OFF = no code.
# TIMELINE: one-shot timelines with absolute timestamps, boot (process start -> 30 frames after the
#   first Scene_Map#start) to boot_timeline.log and each battle entry (command_301 -> 60 frames after
#   Scene_Battle#start) to battle_timeline.log: Ruby milestones, script eval, ShaderSet, shader
#   compile/link, PNG decode, TTF init/open, texture uploads, atlas build, frame regions.
# PERF_BITMAP: per-window (120 frames) Bitmap/text/upload counters appended to the PERF lines, and
#   scene=BATTLE on the PERF line during Scene_Battle. Needs MKXP_VITA_PERF_PROFILE.
option(MKXP_VITA_TIMELINE "Boot / battle-entry timelines (boot_timeline.log, battle_timeline.log)" OFF)
option(MKXP_VITA_PERF_BITMAP "Bitmap/text/upload counters on the PERF lines" OFF)
set(VITA_TAG_DIAG "")
if(MKXP_VITA_DEBUG_SOAK)
    set(VITA_TAG_DIAG " soak=R_at_boot")
endif()
set(VITA_DIAG_SOURCES src/main.cpp src/gl-fun-vita.cpp src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp
    src/vita-font.cpp src/vita_diag.cpp src/shell/sharedstate_test.cpp
    ${MKXP_DIR}/src/display/tilemapvx.cpp ${MKXP_DIR}/src/display/windowvx.cpp)
if(MKXP_VITA_TIMELINE)
    set_property(SOURCE ${VITA_DIAG_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TIMELINE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} timeline=ON")
endif()
if(MKXP_VITA_PERF_BITMAP)
    if(NOT MKXP_VITA_PERF_PROFILE)
        message(FATAL_ERROR "MKXP_VITA_PERF_BITMAP requires MKXP_VITA_PERF_PROFILE")
    endif()
    set_property(SOURCE ${VITA_DIAG_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PERF_BITMAP)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} perf_bitmap=ON")
endif()
if(MKXP_VITA_TIMELINE OR MKXP_VITA_PERF_BITMAP)
    target_sources(mkxp_vita_minimal PRIVATE src/vita_diag.cpp)
endif()

# Diagnostic only: tileset/atlas state to ux0:data/ruby_vita_test/tileset.log: Ruby map/tileset
# names and slot bitmaps, Bitmap loads/releases, TilemapVX slot changes/rebuilds, and after each
# atlas build a pixel comparison atlas vs sources + source texture vs PNG; first 8 atlases as TGA.
option(MKXP_VITA_DEBUG_TILESET "Tileset/atlas diagnostics to tileset.log" OFF)
# Heavy part (20-25 s per atlas build): TGA dumps of the first 8 atlases + source texture vs PNG.
option(MKXP_VITA_DEBUG_TILESET_DUMP "With DEBUG_TILESET: also atlas TGA dumps and PNG re-verification" OFF)
if(MKXP_VITA_DEBUG_TILESET_DUMP)
    if(NOT MKXP_VITA_DEBUG_TILESET)
        message(FATAL_ERROR "MKXP_VITA_DEBUG_TILESET_DUMP requires MKXP_VITA_DEBUG_TILESET")
    endif()
    set_property(SOURCE ${MKXP_DIR}/src/display/gl/tileatlasvx.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_TILESET_DUMP)
endif()
if(MKXP_VITA_DEBUG_TILESET)
    set_property(SOURCE src/main.cpp src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp
                        ${MKXP_DIR}/src/display/tilemapvx.cpp ${MKXP_DIR}/src/display/gl/tileatlasvx.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DEBUG_TILESET)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} debug_tileset=ON")
endif()

# Debug only: "map,x,y" = new game placed straight on that map (e.g. "22,60,16"). Empty = off.
set(MKXP_VITA_DEBUG_BOOT_MAP "" CACHE STRING "DEBUG: boot a new game on map,x,y")
if(NOT MKXP_VITA_DEBUG_BOOT_MAP STREQUAL "")
    # With MKXP_VITA_DEBUG_BOOT_CHECKPOINT: map boot only while L is held at startup, else checkpoint.
    if(MKXP_VITA_DEBUG_DIRECT_BATTLE)
        message(FATAL_ERROR "MKXP_VITA_DEBUG_BOOT_MAP is exclusive with MKXP_VITA_DEBUG_DIRECT_BATTLE")
    endif()
    string(REPLACE "," ";" VITA_BOOT_MAP_LIST "${MKXP_VITA_DEBUG_BOOT_MAP}")
    list(LENGTH VITA_BOOT_MAP_LIST VITA_BOOT_MAP_N)
    if(NOT VITA_BOOT_MAP_N EQUAL 3)
        message(FATAL_ERROR "MKXP_VITA_DEBUG_BOOT_MAP must be map,x,y")
    endif()
    list(GET VITA_BOOT_MAP_LIST 0 VITA_BOOT_MAP_ID)
    list(GET VITA_BOOT_MAP_LIST 1 VITA_BOOT_MAP_X)
    list(GET VITA_BOOT_MAP_LIST 2 VITA_BOOT_MAP_Y)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        MKXP_VITA_DEBUG_BOOT_MAP_ID=${VITA_BOOT_MAP_ID} MKXP_VITA_DEBUG_BOOT_MAP_X=${VITA_BOOT_MAP_X}
        MKXP_VITA_DEBUG_BOOT_MAP_Y=${VITA_BOOT_MAP_Y})
    if(MKXP_VITA_DEBUG_BOOT_CHECKPOINT)
        set(VITA_TAG_DEBUG_BOOT " debug_boot=checkpoint+L:map${VITA_BOOT_MAP_ID}@${VITA_BOOT_MAP_X},${VITA_BOOT_MAP_Y}")
    else()
        set(VITA_TAG_DEBUG_BOOT " debug_boot=map${VITA_BOOT_MAP_ID}@${VITA_BOOT_MAP_X},${VITA_BOOT_MAP_Y}")
    endif()
endif()

# Fix: Input keys as Symbol or Integer (event button conditions: jump etc.) and the LISA layout
# (Triangle=R, Square=X, Cross=C+Y, Circle=B+Z, L=L+A, R=R). main.cpp only.
option(MKXP_VITA_INPUT_V2 "Integer RGSS keys + LISA combo button layout" OFF)
# Fix: save files under ux0:data/ruby_vita_test/ and save_file_exists? without Dir.glob. main.cpp only.
option(MKXP_VITA_SAVE_PATH "Save files in ux0:data/ruby_vita_test/" OFF)
# Diagnostic only: scene/save/flash breadcrumbs to breadcrumb.log (native crash localisation). main.cpp only.
option(MKXP_VITA_SCENE_BREADCRUMB "Scene/save breadcrumbs to breadcrumb.log" OFF)
# Fix: Graphics.brightness/fadeout/fadein/freeze/transition/snap_to_bitmap and Bitmap#blur.
option(MKXP_VITA_SCREEN_FX "Real Graphics brightness/freeze/transition/snap and Bitmap#blur" OFF)
# Fix: Viewport tone (RGB) / color / flash rendering (sharedstate_test.cpp only).
option(MKXP_VITA_VIEWPORT_FX "Viewport tone/color/flash effects" OFF)
# Fix: TexPool without caching (sharedstate_test.cpp only): cached FBOs keep vitaGL render targets.
option(MKXP_VITA_TEXPOOL_NO_CACHE "Delete released TexPool objects instead of caching them" OFF)
if(MKXP_VITA_TEXPOOL_NO_CACHE)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TEXPOOL_NO_CACHE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} texpool_no_cache=ON")
endif()
# Perf: Bitmap#radial_blur on the GPU (mkxp-z geometry) + readback (bitmap-vita-minimal.cpp only).
option(MKXP_VITA_GPU_RADIAL_BLUR "GPU radial_blur with readback into the CPU copy" OFF)
if(MKXP_VITA_GPU_RADIAL_BLUR)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GPU_RADIAL_BLUR)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gpu_radial_blur=ON")
endif()
# Perf: a file bitmap used as blt/stretch_blt source keeps its decoded pixels (bitmap-vita-minimal.cpp only).
option(MKXP_VITA_BLT_SOURCE_KEEP_CPU "Keep the CPU copy of file bitmaps used as blt sources" OFF)
if(MKXP_VITA_BLT_SOURCE_KEEP_CPU)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BLT_SOURCE_KEEP_CPU)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} blt_source_keep_cpu=ON")
endif()
# The patched vitaGL built by deps/setup.sh (deps/vitaGL/libvitaGL.a: NO_SPLASHSCREEN=1 LOG_ERRORS=1
# MKXP_DIAG=1 SINGLE_THREADED_GC=1 MKXP_NO_FBO_DEPTH=1), with its own vitaGL.h; its error log goes to
# <game root>/vitagl.log. Empty = the vitaGL installed in VITASDK.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/deps/vitaGL/libvitaGL.a)
    set(VITA_VITAGL_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR}/deps/vitaGL/libvitaGL.a)
else()
    set(VITA_VITAGL_DEFAULT "")
endif()
set(MKXP_VITA_VITAGL_LIB "${VITA_VITAGL_DEFAULT}" CACHE FILEPATH "Local libvitaGL.a (empty = installed vitaGL)")
if(MKXP_VITA_VITAGL_LIB STREQUAL "")
    set(VITA_VITAGL_LIB vitaGL)
else()
    set(VITA_VITAGL_LIB ${MKXP_VITA_VITAGL_LIB})
    get_filename_component(VITA_VITAGL_DIR ${MKXP_VITA_VITAGL_LIB} DIRECTORY)
    if(EXISTS ${VITA_VITAGL_DIR}/source/vitaGL.h)
        target_include_directories(mkxp_vita_minimal BEFORE PRIVATE ${VITA_VITAGL_DIR}/source)
    endif()
    set_property(SOURCE src/gl-fun-vita.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_VITAGL_DIAG)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} vitagl=diag")
endif()

# Game data root (mkxp-z/src/vita_paths.h). Empty = "ux0:data/ruby_vita_test/". Must end with '/'.
set(MKXP_VITA_GAME_ROOT "" CACHE STRING "Game data directory on the Vita, e.g. ux0:data/lisa/")
if(NOT MKXP_VITA_GAME_ROOT STREQUAL "")
    target_compile_definitions(mkxp_vita_minimal PRIVATE "MKXP_VITA_GAME_ROOT=\"${MKXP_VITA_GAME_ROOT}\"")
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} game_root=${MKXP_VITA_GAME_ROOT}")
endif()

# Fix: TEXFBO deletions deferred by a few swaps (gl-util.h, target-wide): vitaGL does not track
# textures rendered into as FBO attachments and could free them while the GPU still writes them.
option(MKXP_VITA_DEFERRED_GL_DELETE "Delete TEXFBO objects a few swaps after release" OFF)
if(MKXP_VITA_DEFERRED_GL_DELETE)
    target_compile_definitions(mkxp_vita_minimal PRIVATE MKXP_VITA_DEFERRED_GL_DELETE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} deferred_gl_delete=ON")
endif()
# Fix: RGSS Bitmap#get_pixel / set_pixel / hue_change (text colours come from get_pixel on the
# windowskin) and a Graphics.play_movie no-op.
option(MKXP_VITA_RGSS_COMPAT "Real get_pixel/set_pixel/hue_change + play_movie stub" OFF)
if(MKXP_VITA_RGSS_COMPAT)
    set_property(SOURCE src/main.cpp src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_RGSS_COMPAT)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} rgss_compat=ON")
endif()
# Perf: faster fill_rect / gradient_fill_rect and exact opacity-255 shortcuts in stretch_blt/blt
# (bitmap-vita-cpu.h; host-verified identical output).
option(MKXP_VITA_FAST_CPU_KERNELS "Faster exact CPU bitmap kernels" OFF)
if(MKXP_VITA_FAST_CPU_KERNELS)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FAST_CPU_KERNELS)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} fast_cpu_kernels=ON")
endif()
# Perf fix: Tone notifies valueChanged only on a real change (etc.cpp only). Window_Base#update_tone
# sets the same tone every frame, which made every WindowVX redraw its base texture every frame.
option(MKXP_VITA_TONE_CHANGE_ONLY "Tone::valueChanged only when the tone really changes" OFF)
if(MKXP_VITA_TONE_CHANGE_ONLY)
    set_property(SOURCE ${MKXP_DIR}/src/etc/etc.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TONE_CHANGE_ONLY)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} tone_change_only=ON")
endif()
# Perf: cache of composed draw_text surfaces + text_size results (vita-font.cpp only).
option(MKXP_VITA_TEXT_CACHE "Cache draw_text surfaces and text_size results" OFF)
if(MKXP_VITA_TEXT_CACHE)
    set_property(SOURCE src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TEXT_CACHE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} text_cache=ON")
endif()
# Perf: LRU cache (8 MiB) of decoded PNG files (bitmap-vita-minimal.cpp only).
option(MKXP_VITA_PNG_CACHE "Cache decoded PNG files" OFF)
if(MKXP_VITA_PNG_CACHE)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PNG_CACHE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} png_cache=ON")
endif()
# Perf: one texture upload per modified Bitmap when the GPU needs it (bitmap-vita-minimal.cpp only).
option(MKXP_VITA_BITMAP_DEFERRED_UPLOAD "Defer Bitmap texture uploads to first GPU use" OFF)
# RGSS3 argument parsing as mkxp-z (binding-shim.cpp): all rb_get_args formats (o S s z f i b n |),
# upstream int/float/bool coercions and arity checks. Required by the upstream audio binding ('z').
option(MKXP_VITA_RGSS_ARGS_V2 "Upstream rb_get_args (all formats, coercions, arity)" OFF)
if(MKXP_VITA_RGSS_ARGS_V2)
    set_property(SOURCE src/binding-shim.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_RGSS_ARGS_V2)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} rgss_args_v2=ON")
endif()

# RGSS3 Audio: upstream mkxp-z audio module (OpenAL + vorbisfile) with a Vita backend
# (src/vita-audio-backend.cpp: SyncPoint, FileSystem::openRead, WAV source/decoder).
# Ogg Vorbis and WAV only; MIDI and MP3 raise explicit "not supported on Vita" errors.
option(MKXP_VITA_AUDIO "Real RGSS Audio (BGM/BGS/ME/SE) via the mkxp-z audio module" OFF)
if(MKXP_VITA_AUDIO)
    if(NOT MKXP_VITA_RGSS_ARGS_V2)
        message(FATAL_ERROR "MKXP_VITA_AUDIO requires MKXP_VITA_RGSS_ARGS_V2 (the audio binding uses rb_get_args 'z')")
    endif()
    target_sources(mkxp_vita_minimal PRIVATE
        src/vita-audio-backend.cpp
        ${MKXP_DIR}/src/audio/audio.cpp
        ${MKXP_DIR}/src/audio/audiostream.cpp
        ${MKXP_DIR}/src/audio/alstream.cpp
        ${MKXP_DIR}/src/audio/soundemitter.cpp
        ${MKXP_DIR}/src/audio/vorbissource.cpp
        ${MKXP_DIR}/binding/audio-binding.cpp)
    target_compile_definitions(mkxp_vita_minimal PRIVATE MKXP_VITA_AUDIO MKXPZ_ALCDEVICE=ALCdevice_struct)
    target_include_directories(mkxp_vita_minimal PRIVATE ${MKXP_DIR}/src/audio ${MKXP_DIR}/src/filesystem
        ${MKXP_DIR}/src/input $ENV{VITASDK}/arm-vita-eabi/include/AL)
    set(VITA_AUDIO_LIBS openal vorbisfile vorbis ogg)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} audio=ON")
endif()

# RGSS3 filesystem: real Ruby Dir.glob (no stub) + startup FS self-test to stdout.log. main.cpp only.
option(MKXP_VITA_FS_V2 "Real Dir.glob + filesystem self-test" OFF)
if(MKXP_VITA_FS_V2)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FS_V2)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} fs_v2=ON")
endif()
# Diagnostic only: live GL textures/buffers with size, age and creator; gl_ledger.log groups the
# non-Bitmap ones by creator (vitaGL memory held by no live Bitmap). Needs MKXP_VITA_PERF_BITMAP.
option(MKXP_VITA_GL_LEDGER "GL texture/buffer ledger (gl_ledger.log, PERF gl_* fields)" OFF)
if(MKXP_VITA_GL_LEDGER)
    if(NOT MKXP_VITA_PERF_BITMAP)
        message(FATAL_ERROR "MKXP_VITA_GL_LEDGER requires MKXP_VITA_PERF_BITMAP")
    endif()
    set_property(SOURCE src/gl-fun-vita.cpp src/vita_diag.cpp src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GL_LEDGER)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gl_ledger=ON")
endif()
# Fix: Config::Config() (config_shim.cpp) initializes every field to the mkxp-z defaults instead of
# leaving them uninitialized (upstream fills them in Config::read(), never called on the Vita).
option(MKXP_VITA_CONFIG_DEFAULTS "Config fields initialized to the mkxp-z defaults" OFF)
if(MKXP_VITA_CONFIG_DEFAULTS)
    set_property(SOURCE src/shell/config_shim.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CONFIG_DEFAULTS)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} config_defaults=ON")
endif()
# Fix: an mkxp Exception thrown while rendering (Graphics.update) becomes a Ruby exception as
# upstream (RB_METHOD_GUARD) instead of std::terminate. main.cpp only.
option(MKXP_VITA_RENDER_EXC "Graphics.update converts mkxp exceptions to Ruby exceptions" OFF)
if(MKXP_VITA_RENDER_EXC)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_RENDER_EXC)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} render_exc=ON")
endif()
# Fix: PNG files decoded straight into the texture memory (no newlib heap buffer). Needs
# MKXP_VITA_BITMAP_NO_FBO. bitmap-vita-minimal.cpp only.
option(MKXP_VITA_PNG_DIRECT "Decode PNG files straight into texture memory" OFF)
if(MKXP_VITA_PNG_DIRECT)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PNG_DIRECT)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} png_direct=ON")
endif()
# Perf fix: Bitmap#hue_change memoized per rgb (exact), and Bitmap#clone of a file bitmap decodes the
# PNG straight into the clone (no 4.4 MiB CPU copy left in the Cache source). bitmap-vita-minimal.cpp.
option(MKXP_VITA_HUE_FAST "Memoized hue_change and direct clone decode" OFF)
if(MKXP_VITA_HUE_FAST)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_HUE_FAST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} hue_fast=ON")
endif()
# Light diagnostic: Cache.load_bitmap calls slower than 300 ms -> slow_load.log (path, hue, ms). main.cpp.
option(MKXP_VITA_SLOW_LOAD_LOG "Log slow Cache.load_bitmap calls to slow_load.log" OFF)
if(MKXP_VITA_SLOW_LOAD_LOG)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SLOW_LOAD_LOG)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} slow_load_log=ON")
endif()
# Light diagnostic: newlib descriptor slots in use (PERF fd_* fields, fd_table.log at 64/128/200/240).
option(MKXP_VITA_FD_DIAG "PERF descriptor table usage and fd_table.log" OFF)
if(MKXP_VITA_FD_DIAG)
    if(NOT MKXP_VITA_PERF_BITMAP)
        message(FATAL_ERROR "MKXP_VITA_FD_DIAG requires MKXP_VITA_PERF_BITMAP")
    endif()
    set_property(SOURCE src/vita_diag.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FD_DIAG)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} fd_diag=ON")
endif()
# Light profiler: Ruby time per script phase / per map event, map id, GC time on the PERF line.
option(MKXP_VITA_RUBY_PROF "PERF Ruby phase profiler" OFF)
if(MKXP_VITA_RUBY_PROF)
    if(NOT MKXP_VITA_PERF_BITMAP OR NOT MKXP_VITA_TIMELINE)
        message(FATAL_ERROR "MKXP_VITA_RUBY_PROF requires MKXP_VITA_PERF_BITMAP and MKXP_VITA_TIMELINE")
    endif()
    set_property(SOURCE src/vita_diag.cpp src/main.cpp src/vita_freeze_probe.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_RUBY_PROF)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} ruby_prof=ON")
endif()
# Perf fix: ARM cores at 444 MHz (default for applications: 333), re-applied after resume.
# main.cpp (+ PERF cpu_mhz/bus_mhz/gpu_mhz in vita_diag.cpp).
option(MKXP_VITA_CPU_444 "ARM clock 444 MHz" OFF)
if(MKXP_VITA_CPU_444)
    set_property(SOURCE src/main.cpp src/vita_diag.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CPU_444)
    target_link_libraries(mkxp_vita_minimal ScePower_stub)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} cpu_444=ON")
endif()
# Perf fix: the runtime pre-blit delay (MKXP_VITA_DELAY_TOGGLE) starts at 0 us instead of
# MKXP_VITA_PRE_BLIT_DELAY_US. sharedstate_test.cpp.
option(MKXP_VITA_DELAY_START_ZERO "Pre-blit delay starts at 0 (SELECT cycles as before)" OFF)
if(MKXP_VITA_DELAY_START_ZERO)
    if(NOT MKXP_VITA_DELAY_TOGGLE)
        message(FATAL_ERROR "MKXP_VITA_DELAY_START_ZERO requires MKXP_VITA_DELAY_TOGGLE")
    endif()
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DELAY_START_ZERO)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} delay_start_zero=ON")
endif()
# Test: L held at boot applies tuned RUBY_GC_* parameters (A/B in one build). main.cpp; needs RUBY_PROF.
option(MKXP_VITA_GC_TUNE_L "L at boot: tuned Ruby GC parameters" OFF)
if(MKXP_VITA_GC_TUNE_L)
    if(NOT MKXP_VITA_RUBY_PROF)
        message(FATAL_ERROR "MKXP_VITA_GC_TUNE_L requires MKXP_VITA_RUBY_PROF")
    endif()
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GC_TUNE_L)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gc_tune_l=ON")
endif()
# Perf fix: character sprites far off screen skip Sprite_Character#update (L+R+START toggles). main.cpp.
option(MKXP_VITA_OFFSCREEN_SPRITES "Skip updates of off-screen character sprites" OFF)
if(MKXP_VITA_OFFSCREEN_SPRITES)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_OFFSCREEN_SPRITES)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} offscreen_sprites=ON")
endif()
# Perf fix: Ruby GC parameters between defaults and the d40 L set (L at boot = defaults). main.cpp.
option(MKXP_VITA_GC_MID "Medium Ruby GC parameters" OFF)
if(MKXP_VITA_GC_MID)
    if(MKXP_VITA_GC_TUNE_L)
        message(FATAL_ERROR "MKXP_VITA_GC_MID and MKXP_VITA_GC_TUNE_L are exclusive")
    endif()
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GC_MID)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gc_mid=ON")
endif()
# Perf fix: exact fast path of Game_Event#update for still events (stock update chain only). main.cpp.
option(MKXP_VITA_EVENT_FAST "Fast path for still events" OFF)
if(MKXP_VITA_EVENT_FAST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_EVENT_FAST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} event_fast=ON")
endif()
# Perf fix: exact fast path of Sprite_Character#update for unchanged sprites (stock chain only). main.cpp.
option(MKXP_VITA_SPRITE_FAST "Fast path for unchanged character sprites" OFF)
if(MKXP_VITA_SPRITE_FAST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SPRITE_FAST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} sprite_fast=ON")
endif()
# Light diagnostic: frame-time percentiles and counts per PERF window (fp50/fp90/fp99, f_over*). sharedstate_test.cpp.
option(MKXP_VITA_FRAME_HIST "PERF frame-time distribution" OFF)
if(MKXP_VITA_FRAME_HIST)
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FRAME_HIST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} frame_hist=ON")
endif()
# Perf fix: decoded large PNGs cached on the memory card as LZ4 (<game root>/cache/). bitmap-vita-minimal.cpp.
option(MKXP_VITA_IMG_DISK_CACHE "Disk cache of decoded large images (LZ4)" OFF)
if(MKXP_VITA_IMG_DISK_CACHE)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-image-cache.cpp third_party/lz4/lz4.c)
    set_property(SOURCE third_party/lz4/lz4.c APPEND PROPERTY COMPILE_DEFINITIONS LZ4_HEAPMODE=1)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_IMG_DISK_CACHE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} img_disk_cache=ON")
endif()
# Perf fix: Galv Region Effects keeps the unchanged region sprites instead of recreating all at each step. main.cpp.
option(MKXP_VITA_REGION_INCR "Incremental Galv region-effect sprites" OFF)
if(MKXP_VITA_REGION_INCR)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_REGION_INCR)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} region_incr=ON")
endif()
# Perf fix: PNG files read in one large read() and decoded from memory; 64 KiB stdio buffer for audio
# files (newlib stdio reads 1 KiB at a time on the Vita). Needs IMG_DISK_CACHE (readWholeFile).
option(MKXP_VITA_BIG_READS "Large reads for PNG and audio files" OFF)
if(MKXP_VITA_BIG_READS)
    if(NOT MKXP_VITA_IMG_DISK_CACHE)
        message(FATAL_ERROR "MKXP_VITA_BIG_READS requires MKXP_VITA_IMG_DISK_CACHE")
    endif()
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-audio-backend.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BIG_READS)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} big_reads=ON")
endif()
# Fix: empty glDrawElements / glBufferSubData skipped in the gl table (GL no-ops; vitaGL logged an error). gl-fun-vita.cpp.
option(MKXP_VITA_GL_NOOP_SKIP "Skip zero-count draws and zero-size buffer uploads" OFF)
if(MKXP_VITA_GL_NOOP_SKIP)
    set_property(SOURCE src/gl-fun-vita.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GL_NOOP_SKIP)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gl_noop_skip=ON")
endif()
# Diagnostic fix: vitaGL log lines buffered in RAM, written once per PERF window. gl-fun-vita.cpp.
option(MKXP_VITA_VGL_LOG_BUFFER "Buffered vitagl.log" OFF)
if(MKXP_VITA_VGL_LOG_BUFFER)
    set_property(SOURCE src/gl-fun-vita.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_VGL_LOG_BUFFER)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} vgl_log_buffer=ON")
endif()
# Perf fix: main thread pinned to user core 0, other threads to cores 1-2 (vita-thread-affinity.cpp).
option(MKXP_VITA_CORE0_MAIN "Main thread alone on core 0" OFF)
if(MKXP_VITA_CORE0_MAIN)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-thread-affinity.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=sceKernelCreateThread)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CORE0_MAIN)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} core0_main=ON")
endif()
# Diagnostic only: Ruby allocation profile by type / profiler phase / sampled method (PERF alloc_*). vita_diag.cpp.
option(MKXP_VITA_ALLOC_PROF "Ruby allocation profile" OFF)
if(MKXP_VITA_ALLOC_PROF)
    if(NOT MKXP_VITA_RUBY_PROF)
        message(FATAL_ERROR "MKXP_VITA_ALLOC_PROF requires MKXP_VITA_RUBY_PROF")
    endif()
    set_property(SOURCE src/vita_diag.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ALLOC_PROF)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} alloc_prof=ON")
endif()
# Perf fix: Sprite_Character#update_position in C, installed after a boot self-test (vita-sprite-native.cpp).
option(MKXP_VITA_SPRITE_NATIVE "Native Sprite_Character#update_position" OFF)
if(MKXP_VITA_SPRITE_NATIVE)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-sprite-native.cpp)
    set_property(SOURCE src/vita-sprite-native.cpp APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SPRITE_NATIVE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} sprite_native=ON")
endif()
# Perf fix: exact shortcut of Game_Map#refresh when the condition inputs did not change. main.cpp.
option(MKXP_VITA_MAP_REFRESH_FAST "Skip unchanged event page recomputation" OFF)
if(MKXP_VITA_MAP_REFRESH_FAST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_MAP_REFRESH_FAST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} map_refresh_fast=ON")
endif()
# Perf fix: 1:1 nearest blits with integer texel indices (bitmap-vita-cpu.h, host-verified identical);
# last-answer memo of the font lookups (vita-font.cpp).
option(MKXP_VITA_BLT_1TO1 "Integer 1:1 blit path" OFF)
if(MKXP_VITA_BLT_1TO1)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BLT_1TO1)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} blt_1to1=ON")
endif()
option(MKXP_VITA_FONT_MEMO "Memo of the last font lookups" OFF)
if(MKXP_VITA_FONT_MEMO)
    set_property(SOURCE src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FONT_MEMO)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} font_memo=ON")
endif()
# Perf fix: memo of the float blend result per (texel, pixel, opacity) in the CPU blit kernel. bitmap-vita-cpu.h.
option(MKXP_VITA_BLEND_MEMO "Blend result memo in the blit kernel" OFF)
if(MKXP_VITA_BLEND_MEMO)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BLEND_MEMO)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} blend_memo=ON")
endif()
# Steady 30 fps: logic at 60 steps/s, every other step rendered, 2-vblank swap (L+R+SELECT: 30 <-> 60). main.cpp.
option(MKXP_VITA_FPS30 "Steady 30 fps (frameskip 2, logic 60)" OFF)
if(MKXP_VITA_FPS30)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FPS30)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} fps30=ON")
endif()
# Perf fix: save/load screen: no exception for empty slots, second header read of a slot in the same frame
# rebuilt with Marshal (memo dropped on save/delete). main.cpp.
option(MKXP_VITA_SAVE_FAST "Faster save/load screen headers" OFF)
if(MKXP_VITA_SAVE_FAST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SAVE_FAST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} save_fast=ON")
endif()
# Light diagnostic: SAVE_SCREEN line in qa.log with the cost of opening the save/load screen. main.cpp.
option(MKXP_VITA_SAVE_PROF "Save screen cost breakdown" OFF)
if(MKXP_VITA_SAVE_PROF)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SAVE_PROF)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} save_prof=ON")
endif()
# Light diagnostic: slow_frames.log + GC timing without the Ruby phase hooks (the C side of RUBY_PROF only).
option(MKXP_VITA_SLOW_FRAMES "slow_frames.log without Ruby phase hooks" OFF)
if(MKXP_VITA_SLOW_FRAMES)
    if(NOT MKXP_VITA_PERF_BITMAP OR NOT MKXP_VITA_FRAME_HIST OR MKXP_VITA_RUBY_PROF)
        message(FATAL_ERROR "MKXP_VITA_SLOW_FRAMES requires PERF_BITMAP and FRAME_HIST, and replaces RUBY_PROF")
    endif()
    set_property(SOURCE src/vita_diag.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_RUBY_PROF)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} slow_frames=ON")
endif()
# Perf fix: skip redraws of LISA's combo list that would draw the same pixels. main.cpp.
option(MKXP_VITA_COMBO_FAST "Skip identical Window_ComboSkillList redraws" OFF)
if(MKXP_VITA_COMBO_FAST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_COMBO_FAST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} combo_fast=ON")
endif()
# Diagnostic cost fix: breadcrumb.log kept open with sync instead of open/close per line. main.cpp.
option(MKXP_VITA_CRUMB_PERSIST "Persistent breadcrumb.log file" OFF)
if(MKXP_VITA_CRUMB_PERSIST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CRUMB_PERSIST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} crumb_persist=ON")
endif()
# Fix: at every scene start, wait for the GPU and free the textures released by the previous scene
# (our deferred queue + vitaGL purge ring), so new textures do not spill into the newlib heap.
option(MKXP_VITA_SCENE_FLUSH "GPU memory flush at scene start" OFF)
if(MKXP_VITA_SCENE_FLUSH)
    if(NOT MKXP_VITA_DEFERRED_GL_DELETE)
        message(FATAL_ERROR "MKXP_VITA_SCENE_FLUSH requires MKXP_VITA_DEFERRED_GL_DELETE")
    endif()
    set_property(SOURCE src/gl-fun-vita.cpp src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SCENE_FLUSH)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} scene_flush=ON")
endif()
# Fix: MOG Anti Animation Lag's kept animation bitmaps are disposed (unused ones only) when the vitaGL
# texture pools run low. main.cpp.
option(MKXP_VITA_MOG_CAP "Cap MOG anti-lag animation garbage on low GPU memory" OFF)
if(MKXP_VITA_MOG_CAP)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_MOG_CAP)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} mog_cap=ON")
endif()
# Fix: the game ends as in RGSS3 (app closes when the scripts return / exit; an escaped exception
# is shown on screen until X/O, then the app closes). main.cpp.
option(MKXP_VITA_ERROR_SCREEN "Error screen for escaped Ruby exceptions; exit when the game ends" OFF)
if(MKXP_VITA_ERROR_SCREEN)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ERROR_SCREEN)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} error_screen=ON")
endif()
# Light diagnostic: per-method wall/CPU time of slow map changes (qa.log MAP_LOAD). main.cpp.
option(MKXP_VITA_MAP_PROF "Time slow map changes (Scene_Map#perform_transfer / #start)" OFF)
if(MKXP_VITA_MAP_PROF)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_MAP_PROF)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} map_prof=ON")
endif()
# Diagnostic: class histogram of Ruby objects when the live count keeps growing (qa.log OBJ_HIST). main.cpp.
option(MKXP_VITA_OBJ_HIST "Ruby object class histogram on sustained live-slot growth" OFF)
if(MKXP_VITA_OBJ_HIST)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_OBJ_HIST)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} obj_hist=ON")
endif()
# Diagnostic: one qa.log line (size, allocator state) before operator new throws std::bad_alloc.
option(MKXP_VITA_OOM_LOG "Log failed operator new requests" OFF)
if(MKXP_VITA_OOM_LOG)
    set_property(SOURCE src/vita-big-alloc.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_OOM_LOG)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} oom_log=ON")
endif()
# Fix: textures of Bitmaps still equal to their PNG file are released under vitaGL memory pressure
# (least recently used, idle >= 10 s) and rebuilt from the file when used again. bitmap-vita-minimal.cpp.
option(MKXP_VITA_TEX_PAGING "Page out idle file-backed bitmap textures when GPU memory is low" OFF)
if(MKXP_VITA_TEX_PAGING)
    set_property(SOURCE src/main.cpp src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TEX_PAGING)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} tex_paging=ON")
endif()
# Fix: released tile atlases are parked and reused (no 8 MiB allocation per map change);
# tile atlas build raises instead of crashing when out of memory. sharedstate_test.cpp, tileatlasvx.cpp.
option(MKXP_VITA_ATLAS_PARK "Reuse released tile atlas textures" OFF)
if(MKXP_VITA_ATLAS_PARK)
    set_property(SOURCE src/shell/sharedstate_test.cpp src/gl-fun-vita.cpp
                        ${MKXP_DIR}/src/display/gl/tileatlasvx.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ATLAS_PARK)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} atlas_park=ON")
endif()
# Feature: game patches in the package (app0:patches/, index.txt order), loaded after the game's
# scripts; Bitmap.new accepts app0: paths. Files: patches/ (added to the VPK below). main.cpp.
option(MKXP_VITA_PATCHES "Load the game patches shipped in app0:patches/" OFF)
if(MKXP_VITA_PATCHES)
    set_property(SOURCE src/main.cpp src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PATCHES)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} patches=ON")
endif()
# Fix: the scene framebuffers get a depth/stencil surface, so vitaGL's scissor is exact (window
# contents / viewports clipped at the pixel instead of the GPU tile). sharedstate_test.cpp.
option(MKXP_VITA_SCENE_STENCIL "Depth/stencil surface on the scene framebuffers (exact scissor)" OFF)
if(MKXP_VITA_SCENE_STENCIL)
    set_property(SOURCE src/shell/sharedstate_test.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SCENE_STENCIL)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} scene_stencil=ON")
endif()
# Perf fix: Game_Event#update fast path (EVENT_FAST) in C, installed after a boot self-test.
option(MKXP_VITA_EVENT_NATIVE "Native Game_Event#update fast path" OFF)
if(MKXP_VITA_EVENT_NATIVE)
    if(NOT MKXP_VITA_EVENT_FAST)
        message(FATAL_ERROR "MKXP_VITA_EVENT_NATIVE requires MKXP_VITA_EVENT_FAST")
    endif()
    target_sources(mkxp_vita_minimal PRIVATE src/vita-event-native.cpp)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_EVENT_NATIVE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} event_native=ON")
endif()
# Perf fix: Ruby Fiber threads run on core 0 with the main thread (a Fiber is a coroutine of it;
# on cores 1-2 they waited behind the audio threads, ~600 us per switch). vita-pthread-parms.cpp.
option(MKXP_VITA_FIBER_CORE0 "Fiber threads on user core 0" OFF)
if(MKXP_VITA_FIBER_CORE0)
    if(NOT MKXP_VITA_PTHREAD_PARMS_FIX)
        message(FATAL_ERROR "MKXP_VITA_FIBER_CORE0 requires MKXP_VITA_PTHREAD_PARMS_FIX (pte_threadStart wrapper)")
    endif()
    set_property(SOURCE src/vita-pthread-parms.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FIBER_CORE0)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} fiber_core0=ON")
endif()
# Diagnostic: one thread-handoff benchmark (pthread cond vs kernel semaphores, core 0 vs 1-2) after
# 1800 Graphics.update calls (qa.log HANDOFF_BENCH) + pthread_join time on the PERF line.
option(MKXP_VITA_HANDOFF_BENCH "Thread handoff benchmark + pthread_join timing" OFF)
if(MKXP_VITA_HANDOFF_BENCH)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-handoff-bench.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=pthread_join)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_HANDOFF_BENCH)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} handoff_bench=ON")
endif()
# Diagnostic (d77): per-method breakdown of the update phases (PERF sub_top=, self/inclusive time per
# Class#method): every *update* method of the per-frame objects (Spriteset_Battle/Map, Sprite_Battler,
# Game_Map, Game_Player, Scene_Battle/Map, ...), only #update of the classes with many instances.
option(MKXP_VITA_PROF_SUB "Per-method update profiler (PERF sub_top)" OFF)
if(MKXP_VITA_PROF_SUB)
    if(NOT MKXP_VITA_RUBY_PROF)
        message(FATAL_ERROR "MKXP_VITA_PROF_SUB requires MKXP_VITA_RUBY_PROF")
    endif()
    set_property(SOURCE src/vita_diag.cpp src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PROF_SUB)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} prof_sub=ON")
endif()
# Perf fix: blocking pthread waits without pthread-embedded's 500 us cancellation poll (pthread_cancel
# is not linked; checked at run time). vita-sem-nocancel.cpp. PERF sem_nc.
option(MKXP_VITA_SEM_NOCANCEL "sem_wait / untimed sem_timedwait -> sem_wait_nocancel" OFF)
if(MKXP_VITA_SEM_NOCANCEL)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-sem-nocancel.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=sem_wait,--wrap=sem_timedwait)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} sem_nocancel=ON")
endif()
# Diagnostic (d79): time the main thread spends blocked in kernel calls (I/O, semaphores, mutexes,
# delays, thread joins, GXM / vblank waits), per slow frame and per PERF window. vita-block-prof.cpp.
option(MKXP_VITA_BLOCK_PROF "Main-thread blocking profiler (blk_* fields)" OFF)
if(MKXP_VITA_BLOCK_PROF)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-block-prof.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE "-Wl,--wrap=sceKernelWaitSema,--wrap=sceKernelLockMutex,--wrap=sceKernelLockLwMutex,--wrap=sceKernelDelayThread,--wrap=sceKernelDelayThreadCB,--wrap=sceKernelWaitThreadEnd,--wrap=sceKernelWaitThreadEndCB,--wrap=sceIoOpen,--wrap=sceIoRead,--wrap=sceIoWrite,--wrap=sceIoClose,--wrap=sceIoLseek,--wrap=sceIoLseek32,--wrap=sceIoGetstat,--wrap=sceIoDopen,--wrap=sceIoDread,--wrap=sceIoRemove,--wrap=sceIoRename,--wrap=sceIoSyncByFd,--wrap=sceIoPread,--wrap=sceGxmFinish,--wrap=sceGxmDisplayQueueFinish,--wrap=sceGxmNotificationWait,--wrap=sceDisplayWaitVblankStart,--wrap=sceDisplayWaitVblankStartMulti")
    set_property(SOURCE src/vita_freeze_probe.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BLOCK_PROF)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} block_prof=ON")
endif()
# Perf fix: breadcrumb.log lines buffered in RAM, one append per second / at exit / before the error
# screen (instead of one card write per line). Needs MKXP_VITA_SCENE_BREADCRUMB.
option(MKXP_VITA_CRUMB_BUFFER "Buffered breadcrumb.log" OFF)
if(MKXP_VITA_CRUMB_BUFFER)
    if(NOT MKXP_VITA_SCENE_BREADCRUMB)
        message(FATAL_ERROR "MKXP_VITA_CRUMB_BUFFER requires MKXP_VITA_SCENE_BREADCRUMB")
    endif()
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CRUMB_BUFFER)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} crumb_buffer=ON")
endif()
# Perf fix: Marshal.dump(obj, file) written with one IO#write (same bytes): saves in one card write.
option(MKXP_VITA_SAVE_WRITE_ONCE "Marshal.dump to a File in one write" OFF)
if(MKXP_VITA_SAVE_WRITE_ONCE)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SAVE_WRITE_ONCE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} save_write_once=ON")
endif()
# Perf fix: index of the read-only asset folders (Graphics/ Audio/ Data/ Fonts/): extension candidates
# known to be missing are not opened (each miss = a 13-43 ms stat on the card). vita-fs-index.cpp.
option(MKXP_VITA_FS_INDEX "Asset folder index: skip missing extension candidates" OFF)
if(MKXP_VITA_FS_INDEX)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-fs-index.cpp)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-audio-backend.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FS_INDEX)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} fs_index=ON")
endif()
# Perf fix: battle animation images read ahead on a background thread at BattleManager.setup
# (vita-prefetch.cpp; VitaImageCache::readWholeFile takes them). PERF pf. Needs IMG_DISK_CACHE.
option(MKXP_VITA_ANIM_PREFETCH "Background read-ahead of battle animation images" OFF)
if(MKXP_VITA_ANIM_PREFETCH)
    if(NOT MKXP_VITA_IMG_DISK_CACHE)
        message(FATAL_ERROR "MKXP_VITA_ANIM_PREFETCH requires MKXP_VITA_IMG_DISK_CACHE (VitaImageCache::readWholeFile)")
    endif()
    target_sources(mkxp_vita_minimal PRIVATE src/vita-prefetch.cpp src/vita-prefetch-binding.cpp)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-image-cache.cpp src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ANIM_PREFETCH)
    if(MKXP_VITA_FS_INDEX)
        set_property(SOURCE src/vita-prefetch-binding.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FS_INDEX)
    endif()
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} anim_prefetch=ON")
endif()
# Perf fix: event commands that can never suspend run without a Fiber; the Fiber is created at the
# first command that may wait (main.cpp VitaInterpHybrid). qa.log INTERP_HYBRID.
option(MKXP_VITA_INTERP_HYBRID "Interpreter: non-suspending commands without a Fiber" OFF)
if(MKXP_VITA_INTERP_HYBRID)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_INTERP_HYBRID)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} interp_hybrid=ON")
endif()
# Perf fix: the OFFSCREEN_SPRITES and SPRITE_FAST checks of Sprite_Character#update in C
# (vita-sprite-fast-native.cpp; same calls, same order). Needs both of them.
option(MKXP_VITA_SPRITE_FAST_NATIVE "Native OFFSCREEN/SPRITE_FAST sprite checks" OFF)
if(MKXP_VITA_SPRITE_FAST_NATIVE)
    if(NOT MKXP_VITA_SPRITE_FAST OR NOT MKXP_VITA_OFFSCREEN_SPRITES)
        message(FATAL_ERROR "MKXP_VITA_SPRITE_FAST_NATIVE requires MKXP_VITA_SPRITE_FAST and MKXP_VITA_OFFSCREEN_SPRITES")
    endif()
    target_sources(mkxp_vita_minimal PRIVATE src/vita-sprite-fast-native.cpp)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SPRITE_FAST_NATIVE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} sprite_fast_native=ON")
endif()
# Perf fix: battle animation images (decoded-image cache entries) and their SEs decoded on a background
# thread at BattleManager.setup (vita-predecode.cpp). PERF pd. Needs ANIM_PREFETCH and AUDIO.
option(MKXP_VITA_ANIM_PREDECODE "Background decode of battle animation images and SEs" OFF)
if(MKXP_VITA_ANIM_PREDECODE)
    if(NOT MKXP_VITA_ANIM_PREFETCH OR NOT MKXP_VITA_AUDIO)
        message(FATAL_ERROR "MKXP_VITA_ANIM_PREDECODE requires MKXP_VITA_ANIM_PREFETCH and MKXP_VITA_AUDIO")
    endif()
    target_sources(mkxp_vita_minimal PRIVATE src/vita-predecode.cpp)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/vita-audio-backend.cpp src/vita-prefetch-binding.cpp
        ${MKXP_DIR}/src/audio/soundemitter.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ANIM_PREDECODE)
    target_include_directories(mkxp_vita_minimal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} anim_predecode=ON")
endif()
# Diagnostic (d85): perf.log / slow_frames.log / vitagl.log written every N PERF windows instead of
# every window (each write stalls 10-60 ms on the card). Empty = every window.
set(MKXP_VITA_LOG_BATCH "" CACHE STRING "Write the diagnostic logs every N PERF windows")
if(NOT MKXP_VITA_LOG_BATCH STREQUAL "")
    set_property(SOURCE src/vita_diag.cpp src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_LOG_BATCH=${MKXP_VITA_LOG_BATCH})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} log_batch=${MKXP_VITA_LOG_BATCH}")
endif()
# Diagnostic (d85): slow_frames.log threshold in ms (empty: 50).
set(MKXP_VITA_SLOW_FRAME_MS "" CACHE STRING "slow_frames.log threshold (ms)")
if(NOT MKXP_VITA_SLOW_FRAME_MS STREQUAL "")
    set_property(SOURCE src/vita_diag.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SLOW_FRAME_MS=${MKXP_VITA_SLOW_FRAME_MS})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} slow_frame_ms=${MKXP_VITA_SLOW_FRAME_MS}")
endif()
# Perf fix (d86): SPRITE_FAST also when only the map display moved (scrolling): Sprite#update +
# update_position instead of the full update. Needs SPRITE_FAST (Ruby version, not SPRITE_FAST_NATIVE).
option(MKXP_VITA_SPRITE_SCROLL "SPRITE_FAST scroll path (position only)" OFF)
if(MKXP_VITA_SPRITE_SCROLL)
    if(NOT MKXP_VITA_SPRITE_FAST OR MKXP_VITA_SPRITE_FAST_NATIVE)
        message(FATAL_ERROR "MKXP_VITA_SPRITE_SCROLL requires MKXP_VITA_SPRITE_FAST without MKXP_VITA_SPRITE_FAST_NATIVE")
    endif()
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SPRITE_SCROLL)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} sprite_scroll=ON")
endif()
# Perf fix (d86): Game_Map#display_x/display_y recomputed only when @display_x/@display_y change, when
# their bytecode is a pure function of that variable (main.cpp VitaDisplayMemo). qa.log DISPLAY_MEMO.
option(MKXP_VITA_DISPLAY_MEMO "Memoize pure Game_Map#display_x/display_y" OFF)
if(MKXP_VITA_DISPLAY_MEMO)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DISPLAY_MEMO)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} display_memo=ON")
endif()
# Perf tuning (d86): RUBY_GC_HEAP_FREE_SLOTS for MKXP_VITA_GC_MID (empty: 60000).
set(MKXP_VITA_GC_FREE_SLOTS "" CACHE STRING "GC_MID free slots after a GC")
if(NOT MKXP_VITA_GC_FREE_SLOTS STREQUAL "")
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GC_FREE_SLOTS=${MKXP_VITA_GC_FREE_SLOTS})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gc_free_slots=${MKXP_VITA_GC_FREE_SLOTS}")
endif()
# Perf tuning (d87): RUBY_GC_HEAP_INIT_SLOTS (empty: 300000) and RUBY_GC_HEAP_FREE_SLOTS_MAX_RATIO
# (empty: Ruby default) for MKXP_VITA_GC_MID.
set(MKXP_VITA_GC_INIT_SLOTS "" CACHE STRING "GC_MID initial heap slots")
if(NOT MKXP_VITA_GC_INIT_SLOTS STREQUAL "")
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GC_INIT_SLOTS=${MKXP_VITA_GC_INIT_SLOTS})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gc_init_slots=${MKXP_VITA_GC_INIT_SLOTS}")
endif()
set(MKXP_VITA_GC_MAX_RATIO "" CACHE STRING "GC_MID free slots max ratio")
if(NOT MKXP_VITA_GC_MAX_RATIO STREQUAL "")
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GC_MAX_RATIO=${MKXP_VITA_GC_MAX_RATIO})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gc_max_ratio=${MKXP_VITA_GC_MAX_RATIO}")
endif()
# Fix (d89): initialize the Zlib extension of libruby (RGSS3 has Zlib). main.cpp.
option(MKXP_VITA_ZLIB "Ruby Zlib module (RGSS3)" OFF)
if(MKXP_VITA_ZLIB)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ZLIB)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} zlib=ON")
endif()
# Release monitoring (d89): map, battle, GC counts and Ruby heap on the PERF line (needs PERF_PROFILE).
option(MKXP_VITA_PERF_LITE "Map/battle/GC fields on the PERF line" OFF)
if(MKXP_VITA_PERF_LITE)
    set_property(SOURCE src/main.cpp src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_PERF_LITE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} perf_lite=ON")
endif()
# Perf fix (d90): Galv Character Effects refresh_effects keeps identical shadow sprites instead of
# recreating them (main.cpp VitaGalvShadow). qa.log GALV_SHADOW_KEEP.
option(MKXP_VITA_GALV_SHADOW_KEEP "Galv refresh_effects: keep identical shadow sprites" OFF)
if(MKXP_VITA_GALV_SHADOW_KEEP)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GALV_SHADOW_KEEP)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} galv_shadow_keep=ON")
endif()
option(MKXP_VITA_HUE_PAGING "hue_change clones of clean bitmaps stay clean (file + hue): texture paging can give them back" OFF)
if(MKXP_VITA_HUE_PAGING)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_HUE_PAGING)
endif()
option(MKXP_VITA_CPU_PAGING "Give back CPU copies of clean bitmaps when the big pool runs low (and retry a failed decode)" OFF)
if(MKXP_VITA_CPU_PAGING)
    if(MKXP_VITA_BIG_ALLOC_MB STREQUAL "" OR NOT MKXP_VITA_TEX_PAGING)
        message(FATAL_ERROR "MKXP_VITA_CPU_PAGING needs MKXP_VITA_BIG_ALLOC_MB and MKXP_VITA_TEX_PAGING")
    endif()
    set_property(SOURCE src/main.cpp src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CPU_PAGING)
endif()

# Diagnostic only: per-caller accounting of the newlib heap (heap_callers.log, PERF hl_* fields).
option(MKXP_VITA_HEAP_LEDGER "newlib heap per-caller ledger (wraps _malloc_r/_free_r/_realloc_r/_calloc_r/_memalign_r)" OFF)
if(MKXP_VITA_HEAP_LEDGER)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-heap-ledger.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=_malloc_r -Wl,--wrap=_free_r -Wl,--wrap=_realloc_r
                        -Wl,--wrap=_calloc_r -Wl,--wrap=_memalign_r)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_HEAP_LEDGER)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} heap_ledger=ON")
endif()
# Fix: release newlib's per-thread slot (256-entry table) of every thread before it is deleted:
# SDL's Vita threads (mkxp-z audio streams/fades) never gave it back (vita-thread-reent.cpp).
option(MKXP_VITA_THREAD_REENT_FIX "Release newlib reent slots in sceKernelDeleteThread" OFF)
if(MKXP_VITA_THREAD_REENT_FIX)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-thread-reent.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=sceKernelDeleteThread)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} thread_reent_fix=ON")
endif()
# Fix: free pthread-embedded's per-thread ThreadParms block (never freed: one leak per Ruby Fiber).
option(MKXP_VITA_PTHREAD_PARMS_FIX "Free pthread_create's ThreadParms in a pte_threadStart wrapper" OFF)
if(MKXP_VITA_PTHREAD_PARMS_FIX)
    target_sources(mkxp_vita_minimal PRIVATE src/vita-pthread-parms.cpp)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=pte_threadStart)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} pthread_parms_fix=ON")
endif()
# Fix: a Sprite left on a disposed Viewport is not drawn instead of raising "disposed viewport"
# from Graphics.update (mkxp-z sprite.cpp prepare).
option(MKXP_VITA_DISPOSED_VP_SKIP "Sprites on a disposed viewport are skipped while rendering" OFF)
if(MKXP_VITA_DISPOSED_VP_SKIP)
    set_property(SOURCE ${MKXP_DIR}/src/display/sprite.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_DISPOSED_VP_SKIP)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} disposed_vp_skip=ON")
endif()
# Fix: C++ allocations >= 128 KiB from a dedicated sceClibMspace pool (vita-big-alloc.cpp) instead
# of the newlib heap, where large blocks interleaved with small Ruby allocations fragmented it.
# Size in MiB; lower MKXP_VITA_HEAP_MB by the same amount to keep the total budget.
set(MKXP_VITA_BIG_ALLOC_MB "" CACHE STRING "large C++ block pool in MiB (empty: off)")
if(NOT MKXP_VITA_BIG_ALLOC_MB STREQUAL "")
    target_sources(mkxp_vita_minimal PRIVATE src/vita-big-alloc.cpp)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BIG_ALLOC MKXP_VITA_BIG_ALLOC_MB=${MKXP_VITA_BIG_ALLOC_MB})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} big_alloc_mb=${MKXP_VITA_BIG_ALLOC_MB}")
    # Ruby's Fiber stacks (mmap -> vita-compat memalign) from the same pool.
    option(MKXP_VITA_BIG_ALLOC_MMAP "Anonymous mmap (Ruby fiber stacks) from the large-block pool" OFF)
    if(MKXP_VITA_BIG_ALLOC_MMAP)
        set_property(SOURCE src/vita-big-alloc.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BIG_ALLOC_MMAP)
        target_link_options(mkxp_vita_minimal PRIVATE -Wl,--wrap=mmap -Wl,--wrap=munmap)
        set(VITA_TAG_DIAG "${VITA_TAG_DIAG} big_alloc_mmap=ON")
    endif()
endif()
# Memory budget: sound-effect cache (upstream 10 MiB) and draw_text cache (4 MiB) sizes; empty = default.
set(MKXP_VITA_SE_CACHE_MB "" CACHE STRING "SE cache size in MiB (empty: upstream 10)")
if(NOT MKXP_VITA_SE_CACHE_MB STREQUAL "")
    set_property(SOURCE ${MKXP_DIR}/src/audio/soundemitter.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SE_CACHE_MB=${MKXP_VITA_SE_CACHE_MB})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} se_cache_mb=${MKXP_VITA_SE_CACHE_MB}")
endif()
set(MKXP_VITA_TEXT_CACHE_KB "" CACHE STRING "draw_text cache size in KiB (empty: 4096)")
if(NOT MKXP_VITA_TEXT_CACHE_KB STREQUAL "")
    set_property(SOURCE src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_TEXT_CACHE_KB=${MKXP_VITA_TEXT_CACHE_KB})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} text_cache_kb=${MKXP_VITA_TEXT_CACHE_KB}")
endif()
# Memory budget: newlib heap size in MiB (empty = newlib default, 128). main.cpp only.
set(MKXP_VITA_HEAP_MB "" CACHE STRING "newlib heap size in MiB (empty: default 128)")
if(NOT MKXP_VITA_HEAP_MB STREQUAL "")
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_HEAP_MB=${MKXP_VITA_HEAP_MB})
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} heap_mb=${MKXP_VITA_HEAP_MB}")
endif()
# Fix: vitaGL initialized without MSAA (vglInit() defaults to 4x): 4-sample depth buffers of every
# FBO (32 MiB for the tile atlas) exhausted the memory pools and the newlib heap. main.cpp only.
option(MKXP_VITA_NO_MSAA "vitaGL without multisampling (vglInit defaults to 4x)" OFF)
if(MKXP_VITA_NO_MSAA)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_NO_MSAA)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} no_msaa=ON")
endif()
# Log of libruby v5's communication-pipe repairs (suspend/resume) to ruby_pipe.log. main.cpp only.
# Needs MKXP_RUBY_STATIC_LIB = pthread-coop-fiberexit-v5-O2 (older libruby never calls it).
option(MKXP_VITA_RUBY_PIPE_LOG "Log Ruby communication-pipe repairs (libruby v5)" OFF)
if(MKXP_VITA_RUBY_PIPE_LOG)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_RUBY_PIPE_LOG)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} ruby_pipe_log=ON")
endif()
# RGSS3 Bitmap.new(filename) errors as mkxp-z: missing file -> Errno::ENOENT, non-PNG -> error.
option(MKXP_VITA_BITMAP_LOAD_V2 "Bitmap load errors raise like mkxp-z (no placeholder)" OFF)
if(MKXP_VITA_BITMAP_LOAD_V2)
    set_property(SOURCE src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BITMAP_LOAD_V2)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} bitmap_load_v2=ON")
endif()
# RGSS3 Font: real Font.exist? and class defaults (main.cpp, bitmap-binding-vita.cpp).
option(MKXP_VITA_FONT_V2 "Real Font.exist? + RGSS3 Font defaults" OFF)
if(MKXP_VITA_FONT_V2)
    set_property(SOURCE src/main.cpp src/bitmap-binding-vita.cpp src/vita-font.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FONT_V2)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} font_v2=ON")
endif()
# RGSS3 Input as mkxp-z (src/rgss-input.h, host-tested) + separate Vita binding table. main.cpp only.
option(MKXP_VITA_INPUT_V3 "mkxp-z Input semantics (repeat, dir4, dir8) with a Vita binding table" OFF)
foreach(vopt IN ITEMS INPUT_V2 INPUT_V3 SAVE_PATH SCENE_BREADCRUMB)
    if(MKXP_VITA_${vopt})
        set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_${vopt})
        string(TOLOWER "${vopt}" vtag)
        set(VITA_TAG_DIAG "${VITA_TAG_DIAG} ${vtag}=ON")
    endif()
endforeach()
if(MKXP_VITA_SCREEN_FX)
    set_property(SOURCE src/main.cpp src/bitmap-vita-minimal.cpp src/bitmap-binding-vita.cpp
                        src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_SCREEN_FX)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} screen_fx=ON")
endif()
# RGSS3 Graphics as mkxp-z (needs SCREEN_FX): transition bitmaps (TransShader) and upstream prog,
# explicit unsupported resize_screen/play_movie, msgbox/msgbox_p/save_data globals.
option(MKXP_VITA_GFX_V2 "mkxp-z Graphics.transition + explicit unsupported APIs + RGSS globals" OFF)
if(MKXP_VITA_GFX_V2)
    if(NOT MKXP_VITA_SCREEN_FX)
        message(FATAL_ERROR "MKXP_VITA_GFX_V2 requires MKXP_VITA_SCREEN_FX")
    endif()
    set_property(SOURCE src/main.cpp src/bitmap-binding-vita.cpp src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_GFX_V2)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} gfx_v2=ON")
endif()
# Fix: gray part of Viewport tones (mkxp-z ping-pong; needs SCREEN_FX and VIEWPORT_FX).
option(MKXP_VITA_VIEWPORT_GRAY "Viewport tone gray via scene ping-pong" OFF)
if(MKXP_VITA_VIEWPORT_GRAY)
    if(NOT MKXP_VITA_SCREEN_FX OR NOT MKXP_VITA_VIEWPORT_FX)
        message(FATAL_ERROR "MKXP_VITA_VIEWPORT_GRAY requires MKXP_VITA_SCREEN_FX and MKXP_VITA_VIEWPORT_FX")
    endif()
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_VIEWPORT_GRAY)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} viewport_gray=ON")
endif()
if(MKXP_VITA_VIEWPORT_FX)
    set_property(SOURCE src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_VIEWPORT_FX)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} viewport_fx=ON")
endif()
if(MKXP_VITA_BITMAP_DEFERRED_UPLOAD)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BITMAP_DEFERRED_UPLOAD)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} deferred_upload=ON")
endif()

# Fix: Bitmap textures without FBOs (vitaGL has only 256; exhausted after the first battle, then
# the new tile atlas had no FBO). Affects bitmap-vita-minimal.cpp only.
option(MKXP_VITA_BITMAP_NO_FBO "Create Bitmap textures without framebuffer objects" OFF)
if(MKXP_VITA_BITMAP_NO_FBO)
    set_property(SOURCE src/bitmap-vita-minimal.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_BITMAP_NO_FBO)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} bitmap_no_fbo=ON")
endif()

# Fix: rebuild the VX tile atlas into a fresh texture/FBO (vitaGL copy-on-write left rebuilt
# atlases stale: wrong tiles after a tileset change). Affects tilemapvx.cpp only.
option(MKXP_VITA_ATLAS_FRESH_TEX "Fresh atlas texture/FBO for every TileAtlasVX rebuild" OFF)
if(MKXP_VITA_ATLAS_FRESH_TEX)
    set_property(SOURCE ${MKXP_DIR}/src/display/tilemapvx.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_ATLAS_FRESH_TEX)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} atlas_fresh_tex=ON")
endif()

# Fix: Dir.chdir to the game root at startup (relative paths of the scripts). main.cpp only.
option(MKXP_VITA_CHDIR_GAME_ROOT "Change the Ruby working directory to the game root" OFF)
if(MKXP_VITA_CHDIR_GAME_ROOT)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_CHDIR_GAME_ROOT)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} chdir_game_root=ON")
endif()

# Fix: $stdout/$stderr -> ux0:data/ruby_vita_test/stdout.log (STDOUT writes raise EBADF on the Vita).
option(MKXP_VITA_STDOUT_TO_FILE "Redirect Ruby $stdout/$stderr to stdout.log" OFF)
if(MKXP_VITA_STDOUT_TO_FILE)
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_STDOUT_TO_FILE)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} stdout_file=ON")
endif()

# Fix: do not build the Bicubic/Lanczos3 shaders (unused: smoothScaling is NearestNeighbor).
# Their two Lanczos3 links take ~76 s of the first ShaderSet construction. Changes the ShaderSet
# layout, so the definition is target-wide.
option(MKXP_VITA_NO_SCALING_SHADERS "Leave the Bicubic/Lanczos3 shaders out of ShaderSet" OFF)
if(MKXP_VITA_NO_SCALING_SHADERS)
    target_compile_definitions(mkxp_vita_minimal PRIVATE MKXP_VITA_NO_SCALING_SHADERS)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} no_scaling_shaders=ON")
endif()

# Diagnostic only: freeze probe. RAM ring buffer of main-thread/Ruby/coroutine markers and a
# watchdog thread that writes ux0:data/ruby_vita_test/freeze.log when frames stop for ~2 s.
# Coroutine markers come from --wrap around libruby's Context.o entry points (libruby unchanged).
option(MKXP_VITA_FREEZE_PROBE "Freeze probe: RAM markers + watchdog freeze.log" OFF)
if(MKXP_VITA_FREEZE_PROBE)
    if(MKXP_VITA_COROUTINE_PROBE)
        message(FATAL_ERROR "MKXP_VITA_FREEZE_PROBE and MKXP_VITA_COROUTINE_PROBE both wrap coroutine_transfer")
    endif()
    if(MKXP_RUBY_STATIC_LIB MATCHES "-O2")
        set(VITA_TAG_RUBY "O2")
    elseif(MKXP_RUBY_STATIC_LIB MATCHES "-O3")
        set(VITA_TAG_RUBY "O3")
    else()
        set(VITA_TAG_RUBY "O0")
    endif()
    if(CMAKE_BUILD_TYPE OR CMAKE_CXX_FLAGS MATCHES "-O")
        set(VITA_TAG_MKXP "custom(${CMAKE_BUILD_TYPE}${CMAKE_CXX_FLAGS})")
    else()
        set(VITA_TAG_MKXP "O0")
    endif()
    if(MKXP_VITA_WINDOW_DEBUG_PRINTS)
        set(VITA_TAG_WINDOW "ON")
    else()
        set(VITA_TAG_WINDOW "OFF")
    endif()
    if(NOT "${MKXP_VITA_PRE_BLIT_DELAY_US}" STREQUAL "")
        set(VITA_TAG_PREBLIT "${MKXP_VITA_PRE_BLIT_DELAY_US}")
    else()
        set(VITA_TAG_PREBLIT "0")
    endif()
    if(MKXP_VITA_DELAY_TOGGLE)
        set(VITA_TAG_PREBLIT "${VITA_TAG_PREBLIT}+select_toggle")
    endif()
# Diagnostic builds carry their build directory name (PERF_BUILD line), so a log can be matched to
# the VPK that produced it; the OFF build has no diagnostic tag and stays unchanged.
if(NOT VITA_TAG_DIAG STREQUAL "")
    get_filename_component(VITA_BUILD_DIRNAME "${CMAKE_BINARY_DIR}" NAME)
    set(VITA_TAG_DIAG "${VITA_TAG_DIAG} build=${VITA_BUILD_DIRNAME}")
endif()
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vita_build_tag.h" CONTENT
"#define MKXP_VITA_BUILD_TAG \"ruby=${VITA_TAG_RUBY} mkxp=${VITA_TAG_MKXP} window_debug=${VITA_TAG_WINDOW} preblit_us=${VITA_TAG_PREBLIT} freeze_probe=ON${VITA_TAG_BMPWIN}${VITA_TAG_ASPECT}${VITA_TAG_PRESENT}${VITA_TAG_DEBUG_BOOT}${VITA_TAG_DIAG}\"\n")
    target_sources(mkxp_vita_minimal PRIVATE src/vita_freeze_probe.cpp)
    target_include_directories(mkxp_vita_minimal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    set_property(SOURCE src/vita_freeze_probe.cpp src/main.cpp
                        src/shell/sharedstate_test.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS MKXP_VITA_FREEZE_PROBE)
    target_link_options(mkxp_vita_minimal PRIVATE
        -Wl,--wrap=coroutine_transfer
        -Wl,--wrap=coroutine_transfer_exit
        -Wl,--wrap=coroutine_initialize
        -Wl,--wrap=coroutine_destroy
    )
endif()

# Diagnostic only: inert padding after libruby / libpthread to reproduce the pthread build layout.
option(MKXP_VITA_LAYOUT_PAD_TEST "Insert inert layout padding after libruby and libpthread" OFF)
set(MKXP_VITA_PAD_A_TEXT 0 CACHE STRING "Padding .text bytes after libruby")
set(MKXP_VITA_PAD_A_RODATA 0 CACHE STRING "Padding .rodata bytes after libruby")
set(MKXP_VITA_PAD_B_TEXT 0 CACHE STRING "Padding .text bytes after libpthread")
if(MKXP_VITA_LAYOUT_PAD_TEST)
    add_library(vita_layout_pad_a STATIC src/vita-layout-pad.c)
    target_compile_definitions(vita_layout_pad_a PRIVATE VITA_PAD_NAME=a
        VITA_PAD_TEXT_SIZE=${MKXP_VITA_PAD_A_TEXT} VITA_PAD_RODATA_SIZE=${MKXP_VITA_PAD_A_RODATA})
    add_library(vita_layout_pad_b STATIC src/vita-layout-pad.c)
    target_compile_definitions(vita_layout_pad_b PRIVATE VITA_PAD_NAME=b
        VITA_PAD_TEXT_SIZE=${MKXP_VITA_PAD_B_TEXT} VITA_PAD_RODATA_SIZE=0)
    target_link_options(mkxp_vita_minimal PRIVATE
        -Wl,--undefined=vita_layout_pad_text_a
        -Wl,--undefined=vita_layout_pad_rodata_a
        -Wl,--undefined=vita_layout_pad_text_b)
    set(VITA_PAD_A vita_layout_pad_a)
    set(VITA_PAD_B vita_layout_pad_b)
endif()

# Diagnostic only: inert 0x98-byte .text blob after libpthread, mimicking pthread_cancel.o's
# layout footprint without defining pthread_cancel.
option(MKXP_VITA_FAKE_CANCEL_TEST "Link an inert stand-in for pthread_cancel.o" OFF)
if(MKXP_VITA_FAKE_CANCEL_TEST)
    add_library(vita_fake_cancel STATIC src/vita-layout-pad.c)
    target_compile_definitions(vita_fake_cancel PRIVATE VITA_PAD_NAME=fakecancel
        VITA_PAD_TEXT_SIZE=152 VITA_PAD_RODATA_SIZE=0)
    target_link_options(mkxp_vita_minimal PRIVATE -Wl,--undefined=vita_layout_pad_text_fakecancel)
    set(VITA_FAKE_CANCEL vita_fake_cancel)
endif()

target_link_libraries(mkxp_vita_minimal
    ${MKXP_RUBY_STATIC_LIB}
    ${VITA_PAD_A}

    SDL2_ttf
    ${VITA_AUDIO_LIBS}

    freetype

    png
    SDL2
    png16
    z
    bz2
    pthread
    ${VITA_PAD_B}
    ${VITA_FAKE_CANCEL}
    m

    ${VITA_COMPAT_DIR}/libvitacompat.a


    SceAudio_stub
    SceAudioIn_stub
    SceCtrl_stub
    SceMotion_stub
    SceHid_stub
    SceTouch_stub

    ${VITA_VITAGL_LIB}
    vitashark
    SceShaccCgExt
    SceShaccCg_stub
    mathneon
    taihen_stub
    SceGxm_stub
    SceDisplay_stub
    SceKernelDmacMgr_stub
    SceAppMgr_stub
    SceCommonDialog_stub
)


vita_create_self(
    mkxp_vita_minimal.self
    mkxp_vita_minimal
)

# Package contents besides the executable: LiveArea (icon, background, startup image: 8-bit
# palette PNGs, see tools/livearea/; not in the repository, each one is packaged if present) and,
# with MKXP_VITA_PATCHES, the game patches.
set(VITA_LIVEAREA_FILES)
foreach(img icon0.png livearea/contents/bg.png livearea/contents/startup.png)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/${img})
        list(APPEND VITA_LIVEAREA_FILES FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/${img} sce_sys/${img})
    endif()
endforeach()
set(VITA_VPK_FILES
    ${VITA_LIVEAREA_FILES}
    FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
if(MKXP_VITA_PATCHES)
    list(APPEND VITA_VPK_FILES
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/patches/index.txt patches/index.txt
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/patches/ps_buttons.rb patches/ps_buttons.rb
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/patches/ps_buttons_overlay.png patches/ps_buttons_overlay.png)
endif()

vita_create_vpk(
    mkxp_vita_minimal.vpk
    ${VITA_TITLEID}
    mkxp_vita_minimal.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    ${VITA_VPK_FILES}
)
