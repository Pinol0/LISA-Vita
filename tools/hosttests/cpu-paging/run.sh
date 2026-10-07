#!/bin/sh
# Host test for MKXP_VITA_HUE_PAGING + MKXP_VITA_CPU_PAGING: builds the real src/bitmap-vita.cpp
# for the host with the stubs in stub/ and t_cpu_paging.cpp, then runs a few seeds.
# usage: run.sh ROOT_DIR [EXTRA_DEFINES] [SRC]   (ROOT_DIR: scratch dir for the test PNGs, ends with /)
# env: MKXP_DIR (patched mkxp-z, default deps/mkxp-z), VITASDK (its SDL2 headers are used on the host)
set -e
cd "$(dirname "$0")"
ROOT=$1; EXTRA=$2; SRC=${3:-../../../src/bitmap-vita.cpp}
mkdir -p "$ROOT"
OUT=${OUT:-out}; mkdir -p $OUT
printf '#define MKXP_VITA_GAME_ROOT "%s"\n' "$ROOT" > $OUT/root.h
DEFS="$(cat defs.txt) -include $OUT/root.h $EXTRA"
M=${MKXP_DIR:-../../../deps/mkxp-z}
INC="-Istub -I$M -I$M/src -I$M/src/etc -I$M/src/util -I$M/src/display -I$M/src/display/gl -I$M/build/shader -I../../../src -idirafter ${VITASDK:?set VITASDK}/arm-vita-eabi/include/SDL2 -I$(dirname "$SRC")"
g++ -std=gnu++17 -O1 -g -c $DEFS $INC "$SRC" -o $OUT/bmp.o
g++ -std=gnu++17 -O1 -g -c $DEFS $INC t_cpu_paging.cpp -o $OUT/t.o
g++ -std=gnu++17 -O1 -g -c $DEFS $INC ../../../src/vita-image-cache.cpp -o $OUT/vic.o
gcc -O1 -c -DLZ4_HEAPMODE=1 ../../../third_party/lz4/lz4.c -o $OUT/lz4.o
g++ $OUT/t.o $OUT/bmp.o $OUT/vic.o $OUT/lz4.o -lpng -o $OUT/t
rc=0
for seed in 1 2 3 4 5 6 7 8; do $OUT/t $seed ${STEPS:-6000} || rc=1; done
exit $rc
