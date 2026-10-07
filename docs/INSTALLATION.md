# Installation

> No game files are included. You need your own copy of LISA: The Painful (PC version).

## Requirements

- A PS Vita / PS TV with homebrew enabled (HENkaku / Ensō).
- Your own copy of LISA: The Painful.
- Recommended: [PSVshell](https://github.com/Electry/PSVshell) with the clocks at
  **500 / 222 / 222 / 166 MHz** (the port has only been tested with this overclock).

## Steps

1. Install `LISA-Vita-alpha1.vpk` (from the Releases page, or [build it yourself](BUILDING.md)) with
   VitaShell. The app is "Lisa: The Painful" (title ID `LISA00001`).
2. If your copy of the game has a `Game.rgss3a` archive, extract it first with an RGSS3 archive
   extractor: the port needs the plain folders. Extract it into the game folder and let its files
   replace the ones already there (a few images exist in both, and the game uses the archive's).
3. Copy the game files to `ux0:data/lisa_vita/`:

   ```
   ux0:data/lisa_vita/
   ├── Data/
   ├── Graphics/
   ├── Audio/
   └── Fonts/
   ```

4. Start the game from the LiveArea. Saves, logs and caches are written to the same folder.

## First launch

- The first time the game opens the title or load screen it pauses for about 10 seconds: vitaGL
  compiles the game's shaders and stores them in `ux0:data/shader_cache/LISA00001/`. Later launches
  load them in a fraction of a second.
- The first time a large image is shown (battle animations, big backgrounds) it is decoded and stored
  in `ux0:data/lisa_vita/cache/`, which can cause a short stutter; later uses are faster.
- Deleting either folder is safe: it is rebuilt the next time.

## Controls

| Vita | Game |
|---|---|
| D-pad or left stick | Move / select |
| Cross | Confirm |
| Circle | Cancel / menu |
| L | Dash |
| Triangle (or R), Square, Cross, Circle | Combo keys W, A, S, D in battle |
| Right stick up / left / down / right | Combo keys W, A, S, D (only the combo keys: it never confirms or cancels) |

In battle, the combo skill descriptions show the PlayStation buttons instead of the PC keys.
