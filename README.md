# LISA: The Painful — PS Vita Port

An unofficial native PS Vita port of **LISA: The Painful**, based on
mkxp-z and the RGSS3 runtime.

> Current status: Alpha / Work in Progress

[GAMEPLAY SCREENSHOT OR GIF]

## About

This project aims to make LISA: The Painful playable natively on PS Vita.

The port uses mkxp-z as its base, with extensive Vita-specific work for
rendering, audio, memory management, Ruby/RGSS3 compatibility and performance.

## Current Status

The game is currently playable on real PS Vita hardware.

Tested functionality includes:

- Main story progression
- Maps and events
- Battles and combo input
- Save/load
- BGM, BGS, ME and SE
- Suspend/resume
- Custom RGSS3 scripts
- Vita controls
- Long-session stability testing

Performance is still being optimized, particularly on maps with a large
number of active events.

## Installation

This repository does **not** contain any LISA: The Painful game assets.

You must own a legitimate copy of the game and provide the required game
files yourself.

See [Installation Guide](docs/INSTALLATION.md).

## Compatibility / Known Issues

This is still an alpha-quality port.

Known limitations and performance issues are documented in
[Known Issues](docs/KNOWN_ISSUES.md).

## Performance

The port targets up to 60 FPS.

Performance depends heavily on the number and complexity of RGSS3 events
running on a map. Significant optimization work has already been done to move
performance-critical code from Ruby into native code.

## Technical Information

The Vita port includes work on:

- mkxp-z / RGSS3 adaptation
- native ARM Ruby build
- Vita rendering backend
- audio streaming
- custom memory management
- Ruby Fiber support
- Vita suspend/resume handling
- Vita-specific performance optimizations

More information is available in [Technical Notes](docs/TECHNICAL.md).

## Disclaimer

This is an unofficial fan project and is not affiliated with Dingaling
Productions, Serenity Forge or the official developers/publishers of LISA.

No copyrighted game assets are distributed with this project.

## Credits

See [CREDITS.md](CREDITS.md).

## License

See [LICENSE](LICENSE).
