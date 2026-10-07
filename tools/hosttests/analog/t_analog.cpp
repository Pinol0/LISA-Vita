// MKXP_VITA_ANALOG_INPUT: src/vita-analog.h + src/rgss-input.h as built, with the binding table of
// src/main.cpp (extracted by run.sh into bindings.inc).
#include <cmath>
#include <cstdio>
#include "../../../src/rgss-input.h"
#include "../../../src/vita-analog.h"
enum { SCE_CTRL_DOWN = 0x40, SCE_CTRL_LEFT = 0x80, SCE_CTRL_RIGHT = 0x20, SCE_CTRL_UP = 0x10, SCE_CTRL_CROSS = 0x4000,
       SCE_CTRL_CIRCLE = 0x2000, SCE_CTRL_SQUARE = 0x8000, SCE_CTRL_TRIANGLE = 0x1000, SCE_CTRL_RTRIGGER = 0x200,
       SCE_CTRL_LTRIGGER = 0x100 };
#define MKXP_VITA_ANALOG_INPUT
static RgssInput::Input vitaRgssInput(60);
#include "bindings.inc"
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { if (++fails <= 12) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static int at(double r, double deg, bool x) { const double a = deg * M_PI / 180; return (int)std::lround(128 + r * (x ? std::cos(a) : -std::sin(a))); }
int main()
{
    using namespace VitaAnalog;
    /* 1. dead zone and 2. sectors (angle 0 = right, 90 = up) */
    for (int r = 0; r < kLeftPress - 1; ++r)
        for (int d = 0; d < 360; d += 7) { State s; CHECK(update(s, at(r, d, 1), at(r, d, 0), 128, 128) == 0, "dead zone r=%d deg=%d", r, d); }
    for (int d = 0; d < 360; ++d) {
        const double m = std::fmod(d, 90.0);
        if (std::fabs(m - 30) < 1.5 || std::fabs(m - 60) < 1.5) continue;   /* rounding at the boundaries */
        State s;
        const uint32_t got = update(s, at(100, d, 1), at(100, d, 0), 128, 128);
        const int axis = (int)std::lround(d / 90.0) % 4;   /* nearest axis */
        const uint32_t ax[4] = { kRight, kUp, kLeft, kDown };
        uint32_t want;
        if (m <= 30 || m >= 60) want = ax[axis];
        else { const int q = d / 90; want = ax[q] | ax[(q + 1) % 4]; }
        CHECK(got == want, "left sector deg=%d got=%x want=%x", d, got, want);
        State t;
        const uint32_t g4 = update(t, 128, 128, at(100, d, 1), at(100, d, 0));
        const uint32_t r4[4] = { kRightRight, kRightUp, kRightLeft, kRightDown };
        if (std::fabs(m - 45) > 1.5) CHECK(g4 == r4[axis], "right sector deg=%d got=%x", d, g4);
    }
    /* 3. hysteresis */
    { State s; CHECK(update(s, 128, 128 - 50, 128, 128) == 0, "50 does not engage");
      CHECK(update(s, 128, 128 - 60, 128, 128) == kUp, "60 engages");
      CHECK(update(s, 128, 128 - 45, 128, 128) == kUp, "45 stays");
      CHECK(update(s, 128, 128 - 43, 128, 128) == 0, "43 releases");
      CHECK(update(s, 128, 128 - 50, 128, 128) == 0, "50 does not re-engage"); }
    { State s; CHECK(update(s, 128, 128, 128, 128 - 70) == 0, "right 70 does not engage");
      CHECK(update(s, 128, 128, 128, 128 - 90) == kRightUp, "right 90 engages");
      CHECK(update(s, 128, 128, 128, 128 - 55) == kRightUp, "right 55 stays");
      CHECK(update(s, 128, 128, 128, 128 - 45) == 0, "right 45 releases"); }
    /* 4. with the RGSS input and the real bindings */
    vitaInputInitBindings();
    State s;
    auto frame = [&](int lx, int ly, int rx, int ry, uint32_t pad) { vitaRgssInput.update(pad | update(s, lx, ly, rx, ry)); };
    int repeats = 0;
    for (int f = 0; f < 40; ++f) {
        frame(128, 10, 128, 128, 0);
        CHECK(vitaRgssInput.isPressed(RgssInput::Up) && vitaRgssInput.dir4() == 8 && vitaRgssInput.dir8() == 8, "left stick up = Up (frame %d)", f);
        repeats += vitaRgssInput.isRepeated(RgssInput::Up);
    }
    CHECK(repeats == 4, "Up repeats like the D-pad: %d (frame 0, 23, 29, 35)", repeats);
    frame(128, 128, 128, 128, 0);
    CHECK(vitaRgssInput.dir4() == 0, "released");
    const int keys[4][2] = { { 128, 10 }, { 10, 128 }, { 128, 246 }, { 246, 128 } };   /* up, left, down, right */
    const int rgss[4] = { RgssInput::R, RgssInput::X, RgssInput::Y, RgssInput::Z };
    for (int k = 0; k < 4; ++k) {
        int trig = 0;
        for (int f = 0; f < 30; ++f) {
            frame(128, 128, keys[k][0], keys[k][1], 0);
            trig += vitaRgssInput.isTriggered(rgss[k]);
            CHECK(!vitaRgssInput.isPressed(RgssInput::C) && !vitaRgssInput.isPressed(RgssInput::B), "right stick %d never C / B", k);
            CHECK(vitaRgssInput.dir4() == 0, "right stick does not move");
        }
        CHECK(trig == 1, "one flick = one press of key %d (%d)", rgss[k], trig);
        frame(128, 128, 128, 128, 0);
    }
    /* the D-pad and the buttons still work with the sticks centred */
    frame(128, 128, 128, 128, SCE_CTRL_CROSS);
    CHECK(vitaRgssInput.isTriggered(RgssInput::C), "Cross still confirms");
    printf("%s  %d checks\nfails=%d\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
