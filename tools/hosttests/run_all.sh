#!/bin/bash
# Runs every host test of tools/hosttests and prints one PASS / FAIL / SKIP line per test.
# usage: run_all.sh ENV_DIR DUMP_DIR [GAME_DIR] [only-test-name...]
#   ENV_DIR   made by setup_env.sh (host ruby, extracted scripts, audio fixtures)
#   DUMP_DIR  python3 tools/check_embedded_ruby.py BUILD_DIR --dump DUMP_DIR (the Ruby a build embeds)
#   GAME_DIR  your copy of the game (Graphics/, Audio/): tests on real images/sounds; SKIP without it
# env: VITASDK (cpu-paging uses its SDL2 headers), MKXP_DIR (patched mkxp-z, default deps/mkxp-z)
# Logs: $ENV_DIR/results/<test>.log. A FAIL line counts only before a "mutation" line (mutants must fail).
set -u
ENV=$(cd "$1" && pwd); DUMP=$(cd "$2" && pwd); GAME=${3:-}
shift 2; [ $# -gt 0 ] && shift
ONLY="$*"
HT=$(cd "$(dirname "$0")" && pwd)
SRC=$HT/../../src
RUBY=$ENV/bin/ruby
RB=$ENV/ruby-build; RS=$ENV/ruby-src
SCRIPTS=$ENV/scripts
MKXP=${MKXP_DIR:-$HT/../../deps/mkxp-z}
# SDL2 / OpenAL headers for the audio tests: a desktop mkxp-z build's, the system's, or VITASDK's
if [ -d "$MKXP/linux/build-x86_64/include" ]; then
    HOSTINC="-I$MKXP/linux/build-x86_64/include -I$MKXP/linux/build-x86_64/include/SDL2 -I$MKXP/linux/build-x86_64/include/AL"
    ALCDEV=ALCdevice
elif pkg-config --exists sdl2 openal 2>/dev/null; then
    HOSTINC="$(pkg-config --cflags sdl2 openal)"
    ALCDEV=ALCdevice
else
    V=${VITASDK:-/usr/local/vitasdk}/arm-vita-eabi/include   # after the host headers: only what the host lacks
    HOSTINC="-idirafter $V/SDL2 -idirafter $V/AL -idirafter $V"
    ALCDEV=ALCdevice_struct   # the older OpenAL headers of VITASDK
fi
RES=$ENV/results; mkdir -p "$RES"
W=$ENV/work; rm -rf "$W"; mkdir -p "$W"
PASS=0; FAIL=0; SKIP=0; SUMMARY=""

# t NAME DIR COMMAND...: runs COMMAND in DIR; PASS if it exits 0 and prints no FAIL line
t() {
    local name=$1 dir=$2; shift 2
    if [ -n "$ONLY" ] && ! echo " $ONLY " | grep -q " $name "; then return; fi
    local log=$RES/$name.log
    ( cd "$dir" && eval "$@" ) > "$log" 2>&1
    local rc=$?
    local fails; fails=$(awk '/^mutation/ {exit} {print}' "$log" | grep -vE "^(m[0-9]+|mut[a-z0-9_]*):" | grep -cE "^(FAIL|  FAIL|FAIL )|[^_]FAIL[: ]")
    if [ $rc -eq 0 ] && [ "$fails" -eq 0 ]; then
        PASS=$((PASS+1)); SUMMARY+="PASS  $name\n"
    else
        FAIL=$((FAIL+1)); SUMMARY+="FAIL  $name (rc=$rc, $fails FAIL lines; $log)\n"
    fi
}
skip() { SKIP=$((SKIP+1)); SUMMARY+="SKIP  $1 ($2)\n"; }

GA=""; [ -n "$GAME" ] && GA=$GAME/Graphics/Animations
SHEETS=""; [ -n "$GA" ] && SHEETS="'$GA/explode.png' '$GA/Tiger.png'"   # 960x1152 battle animation sheets

# --- C/C++ unit tests ---
t big-alloc     "$HT/big-alloc"     "g++ -std=gnu++17 -O1 -DMKXP_VITA_BIG_ALLOC_MMAP -I. t_big_alloc.cpp -o $W/big_alloc && $W/big_alloc"
t heap-ledger   "$HT/heap-ledger"   "g++ -std=gnu++17 -O1 -I. t_heap_ledger.cpp -o $W/heap_ledger && $W/heap_ledger"
t blt-1to1      "$HT/blt-1to1"      "g++ -std=gnu++17 -O2 t_blt.cpp -o $W/blt && $W/blt"
t pthread-parms "$HT/pthread-parms" "./run.sh"
t fs-index      "$HT/fs-index"      "./run.sh"
t prefetch      "$HT/prefetch"      "./run.sh"
t img-cache-into "$HT/img-cache"    "g++ -std=gnu++17 -O2 -DMKXP_VITA_TEX_DIRECT_CACHE t_load_into.cpp $SRC/vita-image-cache.cpp $SRC/../third_party/lz4/lz4.c -lpng -o $W/load_into && $W/load_into"
t img-cache-chunks "$HT/img-cache" "g++ -std=gnu++17 -O2 -DMKXP_VITA_TEX_DIRECT_CACHE -DMKXP_VITA_DCACHE_CHUNKS t_load_into.cpp $SRC/vita-image-cache.cpp $SRC/../third_party/lz4/lz4.c -lpng -o $W/load_into_ch && $W/load_into_ch && rm -rf $W/xv && mkdir -p $W/xv/a $W/xv/b && $W/load_into store $W/xv/a/ && $W/load_into_ch stale $W/xv/a/ && $W/load_into_ch store $W/xv/b/ && $W/load_into stale $W/xv/b/"
t cpu-paging    "$HT/cpu-paging"    "MKXP_DIR=$MKXP ./run.sh $W/cpu-paging-root/"
t cpu-kernels   "$HT/cpu-kernels"   "g++ -std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -fsanitize=float-cast-overflow -D_GLIBCXX_ASSERTIONS t_kernel_fuzz.cpp -o $W/kfuzz && $W/kfuzz && echo mutation: && ! OLD=1 $W/kfuzz > /dev/null 2>&1"
t binding-guards "$HT/binding-guards" "python3 t_guards.py && echo mutation: && ! MUTATE=1 python3 t_guards.py"
t se-lru        "$HT/se-lru"        "MKXP_DIR=$MKXP ./run.sh"
t quiet-boot    "$HT/quiet-boot"    "./run.sh"
t analog        "$HT/analog"        "./run.sh"
t audio         "$HT/audio/fixtures" "I='-I$MKXP/src -I$MKXP/src/audio -I$MKXP/src/filesystem -I$MKXP/src/util -I$MKXP/src/etc -I$MKXP/src/input -I$MKXP/src/display -I$MKXP/src/display/gl -I$MKXP/binding $HOSTINC -I$SRC -DMKXPZ_ALCDEVICE=$ALCDEV'; g++ -std=gnu++17 -O1 -DMKXP_VITA_GAME_ROOT='\"$ENV/audio-root/\"' \$I ../t_audio.cpp $SRC/vita-audio-backend.cpp $MKXP/src/audio/vorbissource.cpp /usr/lib64/libSDL2-2.0.so.0 /usr/lib64/libvorbisfile.so.3 /usr/lib64/libvorbis.so.0 /usr/lib64/libogg.so.0 -o $W/audio && $W/audio"
if [ -n "$GAME" ]; then
    t hue        "$HT/hue"          "g++ -std=gnu++17 -O2 t_hue.cpp -lpng -o $W/hue && $W/hue $SHEETS"
    t img-cache  "$HT/img-cache"    "g++ -std=gnu++17 -O2 t_img_cache.cpp $SRC/vita-image-cache.cpp $SRC/../third_party/lz4/lz4.c -lpng -o $W/img_cache && $W/img_cache $SHEETS"
    t img-cache-chunks-real "$HT/img-cache" "g++ -std=gnu++17 -O2 -DMKXP_VITA_DCACHE_CHUNKS t_img_cache.cpp $SRC/vita-image-cache.cpp $SRC/../third_party/lz4/lz4.c -lpng -o $W/img_cache_ch && $W/img_cache_ch $SHEETS"
    t png-stride "$HT/png"          "gcc -O2 png_stride_test.c -lpng -o $W/png_stride && find '$GAME/Graphics' -name '*.png' -print0 | xargs -0 $W/png_stride"
else
    for n in hue img-cache img-cache-chunks-real png-stride; do skip $n "needs GAME_DIR"; done
fi

# --- Ruby blocks of src/main.cpp on the game's own scripts ---
t def-wrappers  "$HT/def-wrappers"  "$RUBY t_def_wrappers.rb $SRC/main.cpp"
t offscreen     "$HT/offscreen"     "$RUBY t_offscreen.rb"
t event-fast    "$HT/event-fast"    "$RUBY t_event_fast.rb $SCRIPTS"
t map-refresh   "$HT/map-refresh"   "$RUBY t_map_refresh.rb $SCRIPTS"
t combo-fast    "$HT/combo-fast"    "$RUBY t_combo_fast.rb $SCRIPTS"
t mog-cap       "$HT/mog-cap"       "$RUBY t_mog_cap.rb $SCRIPTS"
t save-fast     "$HT/save-fast"     "$RUBY t_save_fast.rb $SCRIPTS"
t region-incr   "$HT/region-incr"   "python3 gen_block.py && $RUBY t_region_incr.rb $SCRIPTS"
t sprite-fast   "$HT/sprite-fast"   "DEFINES=MKXP_VITA_SPRITE_SCROLL python3 gen_blocks.py && $RUBY t_sprite_fast.rb $SCRIPTS"
t sprite-native "$HT/sprite-native" "gcc -O2 -ffp-contract=off spc_math.c -o spc_math -lm && $RUBY t_sprite_native.rb $SCRIPTS"
t display-memo  "$HT/display-memo"  "$RUBY t_display_memo.rb $SCRIPTS $DUMP"
t galv-shadow   "$HT/galv-shadow"   "$RUBY t_galv_shadow.rb $SCRIPTS $DUMP"
t save-crumb    "$HT/save-crumb"    "$RUBY t_save_crumb.rb $DUMP"
t interp-hybrid "$HT/interp-hybrid" "./run.sh $SCRIPTS $DUMP $RUBY"
t error-screen  "$HT/error-screen"  "python3 extract.py $SRC/main.cpp > $W/error_screen.rb && $RUBY t_error_screen.rb $W/error_screen.rb"
t patches-loader "$HT/patches-loader" "$RUBY t_loader.rb"
t map-prof      "$HT/map-prof"      "python3 extract.py $SRC/main.cpp > $W/map_prof.rb && cd $W && $RUBY $HT/map-prof/t_map_prof.rb $W/map_prof.rb"
t obj-hist      "$HT/obj-hist"      "python3 extract.py $SRC/main.cpp > $W/obj_hist.rb && cd $W && $RUBY $HT/obj-hist/t_obj_hist.rb $W/obj_hist.rb"
if [ -f "$HT/../ps-icons/Skills.rvdata2" ] && [ -n "$GAME" ]; then
    t ps-buttons "$HT/ps-buttons"   "$RUBY t_ps_buttons.rb '$GAME/Data/Skills.rvdata2'"
else
    skip ps-buttons "needs GAME_DIR and tools/ps-icons/Skills.rvdata2 (local, made by patch_skill_descriptions.rb)"
fi

# --- libruby patch: comm-pipe repair under injected socket faults (host ruby has the repair code) ---
t ruby-commpipe "$HT/ruby-commpipe" "gcc -O1 -shared -fPIC fault.c -o $W/fault.so -ldl && for m in err eof block; do echo \"mode \$m\"; FAULT_AT_MS=300 FAULT_MODE=\$m LD_PRELOAD=$W/fault.so timeout 60 $RUBY t.rb | grep -q 'OK items=150 signals=30' || { echo \"FAIL mode \$m\"; exit 1; }; FAULT_AT_MS=300 FAULT_PERIOD_MS=400 FAULT_COUNT=3 FAULT_MODE=\$m LD_PRELOAD=$W/fault.so timeout 60 $RUBY t2.rb | grep -q '^OK ' || { echo \"FAIL t2 mode \$m\"; exit 1; }; done; echo PASS"
t sprite-native-install "$HT/sprite-native" "gcc -O2 -ffp-contract=off spc_math.c -o spc_math -lm && TMPDIR=$W $RUBY t_install.rb $SCRIPTS"
if [ -n "${AUDIO_DIR:-}" ]; then
    t audio-real "$HT/audio" "I='-I$MKXP/src -I$MKXP/src/audio -I$MKXP/src/filesystem -I$MKXP/src/util -I$MKXP/src/etc -I$MKXP/src/input -I$MKXP/src/display -I$MKXP/src/display/gl -I$MKXP/binding $HOSTINC -I$SRC -DMKXPZ_ALCDEVICE=$ALCDEV'; g++ -std=gnu++17 -O1 -DMKXP_VITA_GAME_ROOT='\"/\"' \$I t_real.cpp $SRC/vita-audio-backend.cpp $MKXP/src/audio/vorbissource.cpp /usr/lib64/libSDL2-2.0.so.0 /usr/lib64/libvorbisfile.so.3 /usr/lib64/libvorbis.so.0 /usr/lib64/libogg.so.0 -o $W/audio_real && find '$AUDIO_DIR' -iname '*.wav' -print0 | xargs -0 $W/audio_real"
else
    skip audio-real "set AUDIO_DIR to the game's Audio folder"
fi
# Not run (kept for reference): ruby-prof/prof_test.rb and slow-load/slow_test.rb are snapshots of
# older versions of their blocks (RUBY_PROF is covered by def-wrappers + prof-sub; SLOW_LOAD_LOG is off).

# --- C extensions inside a real Ruby 3.1.6 (miniruby objects) ---
t fiber-pool    "$HT/fiber-pool"    "$RUBY t_fiber_pool.rb"
t event-native  "$HT/event-native"  "./run.sh $RB $RS $SCRIPTS"
t heap-census   "$HT/heap-census"   "./run.sh $RB $RS"
t prof-sub      "$HT/prof-sub"      "./run.sh $RB $RS"
t exc-map       "$HT/exc-map"       "./run.sh $RB $RS $MKXP"

echo "== host tests: $PASS PASS, $FAIL FAIL, $SKIP SKIP"
printf "$SUMMARY"
[ $FAIL -eq 0 ]
