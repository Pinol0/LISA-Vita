/*
 * Analog sticks as buttons (MKXP_VITA_ANALOG_INPUT). Header-only: the host test uses the same code.
 *
 * Left stick = D-pad: the same SCE_CTRL_UP/DOWN/LEFT/RIGHT bits, so walking, menus, Input.dir4/dir8
 * and the events' "button pressed" conditions (LISA checks up/down ~4000 times: ropes, ladders)
 * behave exactly as with the D-pad. 8 directions, the axes wider than the diagonals (60 against 30
 * degrees) so that a slightly crooked push does not walk diagonally.
 *
 * Right stick = the combo keys only, in the face buttons' places: up = Triangle (R, the W key),
 * left = Square (X, A), down = Cross (Y, S), right = Circle (Z, D). Virtual bits, bound to those
 * RGSS keys alone, never to C (confirm) or B (cancel): outside battle the right stick cannot
 * advance a message or close a menu. 4 directions.
 *
 * Both sticks engage past one radius and release under a smaller one (no flicker at the edge);
 * a flick of the right stick is one key press. Stick values: 0..255, centre 128, y grows downward.
 */
#ifndef VITA_ANALOG_H
#define VITA_ANALOG_H

#include <cstdint>

namespace VitaAnalog
{
/* virtual buttons of the right stick (bits no Vita button uses) */
const uint32_t kRightUp = 0x01000000, kRightLeft = 0x02000000, kRightDown = 0x04000000, kRightRight = 0x08000000;
/* the D-pad bits (SCE_CTRL_UP/RIGHT/DOWN/LEFT) */
const uint32_t kUp = 0x00000010, kRight = 0x00000020, kDown = 0x00000040, kLeft = 0x00000080;

const int kLeftPress = 56, kLeftRelease = 44;     /* radius out of 128 */
const int kRightPress = 80, kRightRelease = 50;

struct State
{
    uint32_t left = 0, right = 0;
};

/* 8 sectors: an axis when the other component is under tan(30 deg) of it, else a diagonal. */
inline uint32_t dir8(int dx, int dy)
{
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    const uint32_t h = dx < 0 ? kLeft : kRight, v = dy < 0 ? kUp : kDown;
    /* ay <= tan(30) * ax  <=>  ay * 10000 <= 5774 * ax (integers: no float on this path) */
    if ((long)ay * 10000 <= 5774L * ax)
        return h;
    if ((long)ax * 10000 <= 5774L * ay)
        return v;
    return h | v;
}

/* 4 sectors of 90 degrees */
inline uint32_t dir4(int dx, int dy)
{
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= ay)
        return dx < 0 ? kRightLeft : kRightRight;
    return dy < 0 ? kRightUp : kRightDown;
}

inline bool engaged(bool wasActive, int dx, int dy, int press, int release)
{
    const long r2 = (long)dx * dx + (long)dy * dy;
    const int r = wasActive ? release : press;
    return r2 >= (long)r * r;
}

/* Buttons to OR into the pad's for this frame. */
inline uint32_t update(State &s, int lx, int ly, int rx, int ry)
{
    const int ldx = lx - 128, ldy = ly - 128, rdx = rx - 128, rdy = ry - 128;
    s.left = engaged(s.left != 0, ldx, ldy, kLeftPress, kLeftRelease) ? dir8(ldx, ldy) : 0;
    s.right = engaged(s.right != 0, rdx, rdy, kRightPress, kRightRelease) ? dir4(rdx, rdy) : 0;
    return s.left | s.right;
}
}

#endif
