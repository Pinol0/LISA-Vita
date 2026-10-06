#include "config.h"

#ifdef MKXP_VITA_CONFIG_DEFAULTS
/*
 * Fix (MKXP_VITA_CONFIG_DEFAULTS): upstream fills every field in Config::read() from its JSON default
 * table; the Vita never calls read(), so the empty constructor left all scalars uninitialized
 * (heap contents). Same defaults as config.cpp (non-Windows, no SSL); SharedState and main.cpp still
 * override what the Vita needs. smoothScalingMipmaps is read by TEX::setSmooth (windows, sprites).
 */
Config::Config()
    : rgssVersion(0), debugMode(false), winConsole(false), preferMetalRenderer(false), displayFPS(false),
      printFPS(false), winResizable(true), fullscreen(false), fixedAspectRatio(true), smoothScaling(0),
      smoothScalingDown(0), bitmapSmoothScaling(0), bitmapSmoothScalingDown(0), smoothScalingMipmaps(false),
      bicubicSharpness(100), enableHires(false), textureScalingFactor(1.), framebufferScalingFactor(1.),
      atlasScalingFactor(1.), vsync(false), defScreenW(0), defScreenH(0), fixedFramerate(0), frameSkip(false),
      syncToRefreshrate(false), subImageFix(false), enableBlitting(true), maxTextureSize(0),
      integerScaling{ false, true }, manualFolderSelect(false), anyAltToggleFS(false), enableReset(true),
      enableSettings(true), allowSymlinks(true), pathCache(true), execName("Game"),
      midi{ "", false, false }, SE{ 6 }, BGM{ 1 }, useScriptNames(true), fontScale(0.0f), fontKerning(true),
      fontHinting(3), fontHeightReporting(0), fontOutlineCrop(true), editor{ false, false },
      jit{ false, 0, 100, 10000 }, yjit{ false }, dumpAtlas(false)
{
}
#else
Config::Config() {}
#endif
