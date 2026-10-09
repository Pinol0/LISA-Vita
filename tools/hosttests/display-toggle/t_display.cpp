// Host test for MKXP_VITA_DISPLAY_TOGGLE / MKXP_VITA_DISPLAY_MENU: the real block of
// src/shell/sharedstate-vita.cpp (run.sh extracts it). Mode read from display.cfg at the first frame
// (missing / unknown -> original), set / toggled and written, found by the next launch; the final
// picture: original = aspect width x 544 centred (711 for LISA, 725 for 640x480, 962 for Wide's
// 736x416), stretch = 960x544, pixel = the RGSS screen 1:1 centred.
//   ./run.sh   (mutants: the file is not read back; pixel scaled; the mode not written)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "block.inc"

static int fails = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++fails;
}
static std::string cfg()
{
    char b[32] = { 0 };
    FILE *f = std::fopen(VITA_GAME_ROOT "display.cfg", "r");
    if (!f)
        return "(none)";
    if (!std::fgets(b, sizeof(b), f))
        b[0] = 0;
    std::fclose(f);
    return b;
}

struct R { int x, y, w, h; };
static R rect(int aspectW, int sw, int sh)
{
    R r;
    vitaDisplayRect(aspectW, sw, sh, &r.x, &r.y, &r.w, &r.h);
    return r;
}
static bool is(R r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; }

int main(int argc, char **argv)
{
    /* each "launch" is a new process: argv[1] says what this one checks */
    const std::string step = argc > 1 ? argv[1] : "";
    if (step == "first") {
        check(vitaDisplayGet() == 0, "no display.cfg: original");
        check(is(rect(711, 544, 416), 124, 0, 711, 544), "original: 711x544 at x 124 (LISA)");
        check(is(rect(725, 640, 480), 117, 0, 725, 544), "original: 725 wide for a 640x480 game");
        vitaDisplayToggle();
        check(vitaDisplayGet() == 1 && is(rect(711, 544, 416), 0, 0, 960, 544), "SELECT: stretch, 960x544");
        check(cfg() == "stretch\n", "display.cfg says stretch");
    } else if (step == "second") {
        check(vitaDisplayGet() == 1, "next launch: still stretch (read from display.cfg)");
        vitaDisplaySet(2);
        check(is(rect(711, 544, 416), 208, 64, 544, 416), "pixel: 544x416 1:1 at (208, 64)");
        check(cfg() == "pixel\n", "display.cfg says pixel");
        vitaDisplaySet(3);
        check(is(rect(962, 736, 416), -1, 0, 962, 544), "wide (game at 736x416): 962x544, as original");
        check(cfg() == "wide\n", "display.cfg says wide");
    } else if (step == "third") {
        check(vitaDisplayGet() == 3, "next launch: wide (read from display.cfg)");
        vitaDisplaySet(9);
        check(vitaDisplayGet() == 0 && cfg() == "original\n", "unknown mode: original");
        vitaDisplayToggle();
        vitaDisplayToggle();
        check(vitaDisplayGet() == 0, "SELECT twice: back to original");
    } else if (step == "junk") {
        check(vitaDisplayGet() == 0 && is(rect(711, 544, 416), 124, 0, 711, 544), "unknown display.cfg content: original");
    }
    std::printf("qa.log: %s", [] { char b[64] = { 0 }; FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "r"); std::string all; if (f) { while (std::fgets(b, sizeof(b), f)) all += b; std::fclose(f); } return all.empty() ? std::string("(empty)\n") : all; }().c_str());
    return fails ? 1 : 0;
}
