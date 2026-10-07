#!/bin/sh
# Builds the environment the host tests need, in a durable folder (not /tmp). -std=gnu17: Ruby 3.1
# does not build with the C23 default of GCC >= 15 (old-style declarations in enc/jis/props.h).
#   $ENV/ruby-src     Ruby 3.1.6 + the port's Ruby patch (deps/patches/ruby-3.1.6-vita.patch of the
#                     public repository, or $RUBY_PATCH)
#   $ENV/ruby-build   host build of it, configured as the tests expect (pthread coroutines with the
#                     Vita Fiber pool code, comm-pipe repair code enabled for the host, -O1):
#                     ruby, miniruby and the miniruby objects some tests link against
#   $ENV/scripts      the game's scripts extracted from YOUR copy of Scripts.rvdata2
#                     (never part of any repository)
#   $ENV/audio-root   audio test files: tools/hosttests/audio/fixtures (synthetic) + generated tones
#   $ENV/bin/ruby     wrapper running the host ruby with its standard library
# usage: setup_env.sh ENV_DIR GAME_SCRIPTS_RVDATA2 [RUBY_TARBALL]
set -eu
ENV=$1; SCRIPTS=$2; TARBALL=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
PATCH=${RUBY_PATCH:-$HERE/../../../mkxp-vita-repo/deps/patches/ruby-3.1.6-vita.patch}
mkdir -p "$ENV"
if [ ! -d "$ENV/ruby-src" ]; then
    if [ -z "$TARBALL" ]; then
        TARBALL=$ENV/ruby-3.1.6.tar.xz
        [ -f "$TARBALL" ] || curl -L -o "$TARBALL" https://cache.ruby-lang.org/pub/ruby/3.1/ruby-3.1.6.tar.xz
    fi
    echo "597bd1849f252d8a6863cb5d38014ac54152b508c36dca156f6356a9e63c6102  $TARBALL" | sha256sum -c -
    tar xf "$TARBALL" -C "$ENV"
    mv "$ENV/ruby-3.1.6" "$ENV/ruby-src"
    ( cd "$ENV/ruby-src" && patch -p1 --quiet < "$PATCH" )
fi
if [ ! -x "$ENV/ruby-build/ruby" ]; then
    mkdir -p "$ENV/ruby-build"
    ( cd "$ENV/ruby-build" && ../ruby-src/configure --disable-install-doc --disable-shared --without-gmp \
        --with-coroutine=pthread ac_cv_func_eventfd=no ac_cv_header_sys_eventfd_h=no ac_cv_func_timer_create=no \
        ac_cv_func_timer_settime=no optflags=-O1 cflags=-std=gnu17 cppflags="-DRB_VITA_FIBER_HOST_TEST -DRB_VITA_COMM_PIPE_REPAIR_TEST" \
        > configure.log && make -j"$(nproc)" > make.log 2>&1 )
fi
if [ ! -d "$ENV/scripts" ]; then
    mkdir -p "$ENV/scripts"
    "$ENV/ruby-build/ruby" --disable-gems -I"$ENV/ruby-build/.ext/x86_64-linux" -I"$ENV/ruby-build/.ext/common" -rzlib -e '
        d = Marshal.load(File.binread(ARGV[0]))
        d.each_with_index { |(i, n, c), k| File.write("%s/%03d_%s.rb" % [ARGV[1], k, n.to_s.gsub(/[^\w\-]+/, "_")], Zlib::Inflate.inflate(c)) }' \
        "$SCRIPTS" "$ENV/scripts"
fi
# audio fixtures: the synthetic WAVs (fixtures/) plus generated tones in place of real game files
if [ ! -d "$ENV/audio-root" ]; then
    mkdir -p "$ENV/audio-root/Audio/SE" "$ENV/audio-root/Audio/BGM"
    cp "$HERE"/audio/fixtures/*.wav "$ENV/audio-root/Audio/SE/"
    ffmpeg -loglevel error -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" -ac 2 -c:a pcm_s16le "$ENV/audio-root/Audio/SE/test_wav.wav"
    ffmpeg -loglevel error -f lavfi -i "sine=frequency=660:duration=1:sample_rate=22050" -ac 1 -c:a libvorbis "$ENV/audio-root/Audio/SE/test_ogg.ogg"
    ffmpeg -loglevel error -f lavfi -i "sine=frequency=330:duration=2:sample_rate=44100" -ac 2 -c:a libmp3lame "$ENV/audio-root/Audio/BGM/test_mp3.mp3"
fi
# a ruby that finds its standard library without being installed
mkdir -p "$ENV/bin"
cat > "$ENV/bin/ruby" <<EOR
#!/bin/sh
exec "$ENV/ruby-build/ruby" --disable-gems -I"$ENV/ruby-src/lib" -I"$ENV/ruby-build/.ext/common" -I"$ENV/ruby-build/.ext/x86_64-linux" "\$@"
EOR
chmod +x "$ENV/bin/ruby"
echo "ENV ready: $ENV ($(ls "$ENV/scripts" | wc -l) scripts, $("$ENV/ruby-build/ruby" --disable-gems -v))"
