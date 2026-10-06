# Technical notes

## Overview

- **Ruby / RGSS3**: the game's own `Scripts.rvdata2` runs unchanged on Ruby 3.1.6 (MRI),
  cross-compiled for the Vita as a static library, with mkxp-z's RGSS bindings.
- **Fibers**: the Vita has no assembly coroutines for Ruby's ABI here, so Fibers run on a small
  pool of pthreads with cooperative switching (`coroutine/pthread` in the Ruby patch).
- **Rendering**: mkxp-z's renderer (tilemaps, sprites, windows, planes, viewports, transitions)
  on a patched [vitaGL](https://github.com/Rinnegatamante/vitaGL). Bitmaps keep a CPU copy for
  RGSS's pixel operations; textures and CPU copies that can be rebuilt from the game files are
  given back when memory runs low.
- **Audio**: BGM/BGS/ME/SE in Ogg Vorbis and WAV through OpenAL; WAV is streamed.
- **Memory**: large C++ blocks come from a dedicated pool so they do not fragment the heap Ruby
  uses; decoded images are cached on the memory card (LZ4).
- **Suspend/resume**: Ruby's timer-thread socket pair is repaired after standby.
- **Performance**: the hottest parts of the game's per-frame Ruby work (event and sprite updates,
  map refresh, some third-party scripts' effects) have exact shortcuts in Ruby or C.

Every fix and diagnostic is a CMake option (see `CMakeLists.txt`); `options/release.cmake`
holds the values of the release build.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | The port: Ruby embedding, bindings, Vita bitmap/audio/font backends, fixes |
| `src/shell/` | mkxp-z shared state and configuration for the Vita |
| `patches/` | Runtime patches packaged in the VPK (PlayStation button icons) |
| `deps/` | Patches for mkxp-z, vitaGL and Ruby 3.1.6, POSIX shims, setup script |
| `options/release.cmake` | The CMake options of the release build |
| `tools/` | Host tests, log analysis, LiveArea and icon tools |

## Building

See [BUILDING.md](BUILDING.md).
