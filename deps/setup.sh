#!/bin/sh
# Fetches the dependencies at the commits this port was developed against, applies the port's
# patches and builds what the CMake build links:
#   deps/mkxp-z     mkxp-z 826929e + patches/mkxp-z.patch, shader headers in build/shader/
#   deps/vitaGL     vitaGL 16fe309 + patches/vitaGL.patch, libvitaGL.a
#   deps/vita-compat/libvitacompat.a
#   deps/ruby/ruby-3.1.6 (Ruby 3.1.6 + patches/ruby-3.1.6-vita.patch), deps/ruby/build/ (config.h)
# libruby-static.a is not built here: see docs/BUILDING.md (prebuilt archive or ruby-recipe/).
# usage: deps/setup.sh [--ruby-lib path/to/libruby-static.a]
set -eu
cd "$(dirname "$0")"
D=$(pwd)
: "${VITASDK:?set VITASDK}"
PATH="$VITASDK/bin:$PATH"

MKXP_COMMIT=826929eeb3ebc4b887c011604919217a790770f4
VITAGL_COMMIT=16fe309d87761112b813be77842b20338659c460
RUBY_URL=https://cache.ruby-lang.org/pub/ruby/3.1/ruby-3.1.6.tar.xz
RUBY_SHA256=597bd1849f252d8a6863cb5d38014ac54152b508c36dca156f6356a9e63c6102

RUBY_LIB=
if [ "${1:-}" = "--ruby-lib" ]; then RUBY_LIB=$2; fi

fetch() {   # dir url commit patch
    if [ ! -d "$1" ]; then
        git clone --quiet "$2" "$1"
        git -C "$1" checkout --quiet "$3"
        git -C "$1" apply "$D/patches/$4"
        echo "$1: $3 + $4"
    else
        echo "$1: already there, left as is"
    fi
}

fetch mkxp-z https://github.com/mkxp-z/mkxp-z.git $MKXP_COMMIT mkxp-z.patch
mkdir -p mkxp-z/build/shader
( cd mkxp-z/build && for f in ../shader/*; do [ "$(basename "$f")" = meson.build ] || xxd -i "$f" > "shader/$(basename "$f").xxd"; done )

fetch vitaGL https://github.com/Rinnegatamante/vitaGL.git $VITAGL_COMMIT vitaGL.patch
make -C vitaGL -j"$(nproc)" NO_SPLASHSCREEN=1 LOG_ERRORS=1 MKXP_DIAG=1 SINGLE_THREADED_GC=1 MKXP_NO_FBO_DEPTH=1

make -C vita-compat

mkdir -p ruby
if [ ! -d ruby/ruby-3.1.6 ]; then
    [ -f ruby/ruby-3.1.6.tar.xz ] || curl -L -o ruby/ruby-3.1.6.tar.xz "$RUBY_URL"
    echo "$RUBY_SHA256  ruby/ruby-3.1.6.tar.xz" | sha256sum -c -
    tar xf ruby/ruby-3.1.6.tar.xz -C ruby
    ( cd ruby/ruby-3.1.6 && patch -p1 --quiet < "$D/patches/ruby-3.1.6-vita.patch" )
    echo "ruby/ruby-3.1.6: 3.1.6 + ruby-3.1.6-vita.patch"
fi
mkdir -p ruby/build/.ext/include/arm-eabi/ruby
cp ruby-recipe/config.h ruby/build/.ext/include/arm-eabi/ruby/config.h
if [ -n "$RUBY_LIB" ]; then cp "$RUBY_LIB" ruby/build/libruby-static.a; fi
if [ -f ruby/build/libruby-static.a ]; then
    echo "ruby/build/libruby-static.a: $(sha1sum ruby/build/libruby-static.a | cut -c1-40)"
else
    echo "ruby/build/libruby-static.a missing: see docs/BUILDING.md"
fi
