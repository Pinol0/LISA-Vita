## Known issues

### Stability
- **Memory**: available memory is tight. In long sessions with many battles the game may still
  run out of memory and crash.
- Very long sessions (several hours) have not been fully tested.

### Performance
- 60 fps is not locked: in exploration there can still be a few light stutters (Ruby GC, loading).
- Some heavy maps (many events and shadows on screen) can drop below 60 fps.
- Tested only with the PSVshell overclock at 500/222/222/166 MHz; at stock clocks performance is lower.
- Occasional short pauses from Ruby's garbage collector.
- First launch: a pause of about 10 s while the shaders are compiled, and short stutters the first
  time large images are shown (see [Installation](INSTALLATION.md#first-launch)).

### Compatibility
- **MP3 and MIDI are not supported** (only Ogg Vorbis and WAV): those sounds stay silent.
- Encrypted archives (`.rgss3a`) are not supported: the game files must be extracted.
- Only the 544x416 resolution.
- Tested only with **LISA: The Painful**: other RPG Maker VX Ace games may not work.
  Some details are still LISA-specific (bitmap font, fixed folder `ux0:data/lisa_vita/`).
- `Object#clone` behaves like `dup` (it does not keep the frozen state or singleton methods).

### Build / distribution
- Needs patched dependencies: Ruby 3.1 with a custom Fiber/pthread layer and a vitaGL fork.
- Diagnostic logs (`qa.log`, `perf.log`, …) are still written to the memory card.
- The game files are not included: you must own the game and copy its files yourself.
- mkxp-z is GPLv2: binaries must be distributed together with the source code.
