/*
 * Native Sprite_Character#update_position (MKXP_VITA_SPRITE_NATIVE): the arithmetic of LISA's
 * Game_CharacterBase#screen_x/screen_y/screen_z with Game_Map#adjust_x/adjust_y and display_x/
 * display_y from "Event Jitter Fix" (non-looping maps), in the same order of IEEE double operations
 * as Ruby's Float, so the results are bit-identical:
 *   display = (@display * 32).floor.to_f / 32
 *   screen_x = (real_x - display_x) * 32 + 16
 *   screen_y = (real_y - display_y) * 32 + 32 - shift_y(0) - jump_height
 *   jump_height = (peak * peak - (count - peak).abs ** 2) / 2      (Integer)
 *   screen_z = priority_type * 100
 * Sprite#x= / #y= truncate a Float toward zero (mkxp-z rb_int_arg: NUM2LONG).
 * Pure C: also built by the host test (tools/hosttests/sprite-native/).
 */
#ifndef VITA_SPRITE_NATIVE_H
#define VITA_SPRITE_NATIVE_H

#include <math.h>

static inline double vitaSpcDisplay(double d) { return floor(d * 32.0) / 32.0; }

static inline long vitaSpcJumpHeight(long count, long peak)
{
    long d = count - peak;
    if (d < 0) d = -d;
    const long v = peak * peak - d * d;
    /* Ruby Integer#/ floors; v >= 0 for 0 <= count <= 2 * peak, floor anyway for safety */
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

static inline void vitaSpcScreen(double realX, double realY, double displayX, double displayY, long jumpCount, long jumpPeak,
                                 long priority, double *sx, double *sy, long *sz)
{
    const double dx = vitaSpcDisplay(displayX), dy = vitaSpcDisplay(displayY);
    *sx = (realX - dx) * 32.0 + 16.0;
    *sy = (realY - dy) * 32.0 + 32.0 - 0.0 - (double)vitaSpcJumpHeight(jumpCount, jumpPeak);
    *sz = priority * 100;
}

#endif
