# Soak on the PC (desktop mkxp-z)

Runs the same `tools/soak/soak.rb` with a desktop mkxp-z build,
headless, to measure memory growth of the game logic without the Vita.

Game folder (symlinks only):
    mkdir lisa-pc && cd lisa-pc
    L=/path/to/your/LISA.The.Painful
    ln -s $L/Game.rgss3a; ln -s $L/Graphics; ln -s $L/Audio; ln -s $L/Fonts; cp $L/Game.ini .
    cp .../tools/pc-soak/{pc_soak_preload.rb,mkxp.json} .../tools/soak/soak.rb .
Run:
    SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null timeout 1200 /path/to/mkxp-z.x86_64
Outputs: soak.log (steps), pc_mem.log (RSS and GC stats once per route loop).

Notes: This desktop build has no Ogg
decoder, so the shim skips audio calls that raise ENOENT. The SDL error message box segfaults under the
offscreen driver: a crash at X11_ShowMessageBox means a Ruby error, read the lines before it.
