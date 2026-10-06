# Installation

> No game files are included. You need your own copy of LISA: The Painful (PC version).

## Requirements

- A PS Vita / PS TV with homebrew enabled (HENkaku / Ensō).
- Your own copy of LISA: The Painful.
- Recommended: [PSVshell](https://github.com/Electry/PSVshell) with the clocks at
  **500 / 222 / 222 / 166 MHz** (the port has only been tested with this overclock).

## Steps

1. Install the `.vpk` (from the Releases page, or [build it yourself](BUILDING.md)) with VitaShell.
2. If your copy of the game has a `Game.rgss3a` archive, extract it first with an RGSS3 archive
   extractor: the port needs the plain folders.
3. Copy the game files to `ux0:data/ruby_vita_test/`:

   ```
   ux0:data/ruby_vita_test/
   ├── Scripts.rvdata2      (a copy of Data/Scripts.rvdata2)
   ├── Data/
   ├── Graphics/
   ├── Audio/
   └── Fonts/
   ```

4. Start the game from the LiveArea. Saves, logs and caches are written to the same folder.

## Controls

| Vita | Game |
|---|---|
| D-pad | Move / select |
| Cross | Confirm |
| Circle | Cancel / menu |
| L | Dash |
| Triangle (or R), Square, Cross, Circle | Combo keys W, A, S, D in battle |

In battle, the combo skill descriptions show the PlayStation buttons instead of the PC keys.
