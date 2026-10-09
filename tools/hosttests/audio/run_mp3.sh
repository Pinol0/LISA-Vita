#!/bin/bash
# MP3 (MKXP_VITA_MP3) on the real src/vita-audio-backend.cpp + dr_mp3, against ffmpeg's decoder.
# usage: run_mp3.sh AUDIO_ROOT   (setup_env.sh's audio-root: Audio/BGM/test_mp3.mp3); env MKXP_DIR, MP3_DIR
# (a game's Audio folder: every .mp3 decoded too), VITASDK (SDL2/OpenAL headers if MKXP_DIR has none).
# Mutants: no encoder-delay correction in seekToOffset; MP3 not recognised.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../src
M=${MKXP_DIR:-$HERE/../../../deps/mkxp-z}
ROOT=$(cd "$1" && pwd)
if [ -d "$M/linux/build-x86_64/include" ]; then
    H="-I$M/linux/build-x86_64/include -I$M/linux/build-x86_64/include/SDL2 -I$M/linux/build-x86_64/include/AL -DMKXPZ_ALCDEVICE=ALCdevice"
else
    V=${VITASDK:-/usr/local/vitasdk}/arm-vita-eabi/include
    H="-idirafter $V/SDL2 -idirafter $V/AL -idirafter $V -DMKXPZ_ALCDEVICE=ALCdevice_struct"
fi
OUT=$(mktemp -d "${TMPDIR:-/tmp}/mp3.XXXXXX")
I="-I$M/src -I$M/src/audio -I$M/src/filesystem -I$M/src/util -I$M/src/etc -I$M/src/input -I$M/src/display -I$M/src/display/gl -I$M/binding $H -I$SRC"
LIBS="/usr/lib64/libSDL2-2.0.so.0 /usr/lib64/libvorbisfile.so.3 /usr/lib64/libvorbis.so.0 /usr/lib64/libogg.so.0"
F="-std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -D_GLIBCXX_ASSERTIONS -DMKXP_VITA_MP3 -DMKXP_VITA_GAME_ROOT=\"$ROOT/\""
build() { g++ $F $I "$HERE/t_mp3.cpp" "$1" "$M/src/audio/vorbissource.cpp" $LIBS -o "$2"; }
build "$SRC/vita-audio-backend.cpp" "$OUT/t"
( cd "$ROOT" && "$OUT/t" )
echo "mutation: seek without the encoder delay; MP3 not recognised"
sed 's/f == 0 ? 0 : f + mp3.delayInPCMFrames/f/' "$SRC/vita-audio-backend.cpp" > "$OUT/m1.cpp"
sed 's/return n == 3 \&\& ((h\[0\]/return false \&\& ((h[0]/' "$SRC/vita-audio-backend.cpp" > "$OUT/m2.cpp"
for m in m1 m2; do
    cmp -s "$OUT/$m.cpp" "$SRC/vita-audio-backend.cpp" && { echo "FAIL mutant $m not applied"; exit 1; }
    sed -i "s#\"../third_party/dr_mp3/dr_mp3.h\"#\"$SRC/../third_party/dr_mp3/dr_mp3.h\"#" "$OUT/$m.cpp"
    build "$OUT/$m.cpp" "$OUT/$m"
    if ( cd "$ROOT" && MP3_DIR= "$OUT/$m" > /dev/null ); then echo "FAIL mutant $m survived"; exit 1; else echo "PASS mutant $m killed"; fi
done
rm -rf "$OUT"
