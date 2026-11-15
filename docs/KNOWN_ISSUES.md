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
- **MIDI is not supported** (Ogg Vorbis, WAV and MP3 are): MIDI sounds stay silent.
- Encrypted archives (`.rgss3a`) are not supported: the game files must be extracted.
- The game runs at 544x416. The *Wide* screen option draws it at 736x416: scenes and events placed by hand
  for 544x416 may look wrong there (switch back to *Original*). The game over picture and the dialogue
  boxes above characters are adjusted for Wide since Alpha 0.3.
- Only the **Legacy Edition** of LISA: The Painful (RPG Maker VX Ace) works; the Definitive Edition
  (Unity) is a different program and is not supported.
- Tested only with **LISA: The Painful** and **LISA: The Joyful** (experimental): other RPG Maker VX Ace
  games may not work. Some details are still LISA-specific (bitmap font, fixed data folder per package).
- LISA: The Joyful: completed on hardware, with three crashes after long sessions (out of memory);
  save often.
- `Object#clone` behaves like `dup` (it does not keep the frozen state or singleton methods).

### Build / distribution
- Needs patched dependencies: Ruby 3.1 with a custom Fiber/pthread layer and a vitaGL fork.
- Diagnostic logs (`qa.log`, `perf.log`, …) are still written to the memory card (in the `logs/`
  folder inside the game folder).
- The game files are not included: you must own the game and copy its files yourself.
- mkxp-z is GPLv2: binaries must be distributed together with the source code.
