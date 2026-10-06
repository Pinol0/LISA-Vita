/*
 * Native Sprite_Character#update_position (MKXP_VITA_SPRITE_NATIVE), see vita-sprite-native.h.
 * d52: on the busy map 56, character sprites allocated ~720 Floats per frame (of ~970), ~19 per
 * updated sprite, almost all in update_position (4 screen_x/screen_y calls, each through the jitter
 * fix's display_x). Here the position is computed in C and set as Integers: no Float is allocated.
 * The Ruby side (main.cpp) installs it only for the character classes whose results matched the
 * Ruby methods exactly in a self-test at boot (vita_spc_eval), and falls back to the Ruby method
 * for anything unexpected (animation running, looping map, non-numeric values).
 */
#include <ruby.h>

#include "vita-sprite-native.h"

namespace
{
ID idRealX, idRealY, idJumpCount, idJumpPeak, idPriority, idDisplayX, idDisplayY, idSetX, idSetY, idSetZ;

bool num(VALUE v, double *out)
{
    if (FIXNUM_P(v)) { *out = (double)FIX2LONG(v); return true; }
    if (RB_FLOAT_TYPE_P(v)) { *out = RFLOAT_VALUE(v); return true; }
    return false;
}

/* The values for `ch` with the current $game_map; false if anything is not a plain number. */
bool compute(VALUE ch, double *sx, double *sy, long *sz)
{
    const VALUE gm = rb_gv_get("$game_map");
    if (NIL_P(gm)) return false;
    double rx, ry, dX, dY;
    const VALUE jc = rb_ivar_get(ch, idJumpCount), jp = rb_ivar_get(ch, idJumpPeak), pr = rb_ivar_get(ch, idPriority);
    if (!num(rb_ivar_get(ch, idRealX), &rx) || !num(rb_ivar_get(ch, idRealY), &ry) ||
        !num(rb_ivar_get(gm, idDisplayX), &dX) || !num(rb_ivar_get(gm, idDisplayY), &dY) ||
        !FIXNUM_P(jc) || !FIXNUM_P(jp) || !FIXNUM_P(pr))
        return false;
    vitaSpcScreen(rx, ry, dX, dY, FIX2LONG(jc), FIX2LONG(jp), FIX2LONG(pr), sx, sy, sz);
    return isfinite(*sx) && isfinite(*sy) && fabs(*sx) < 1e9 && fabs(*sy) < 1e9;
}

/* vita_spc_eval(char) -> [screen_x, screen_y, screen_z] or nil (self-test at install). */
VALUE rbEval(VALUE self, VALUE ch)
{
    (void)self;
    double sx, sy;
    long sz;
    if (!compute(ch, &sx, &sy, &sz)) return Qnil;
    return rb_ary_new_from_args(3, DBL2NUM(sx), DBL2NUM(sy), LONG2NUM(sz));
}

/* vita_spc_pos(sprite, char) -> true if x/y/z were set, false = use the Ruby method. */
VALUE rbPos(VALUE self, VALUE sprite, VALUE ch)
{
    (void)self;
    double sx, sy;
    long sz;
    if (!compute(ch, &sx, &sy, &sz)) return Qfalse;
    rb_funcall(sprite, idSetX, 1, LONG2NUM((long)sx));   /* Float -> Integer truncates toward zero, as NUM2LONG */
    rb_funcall(sprite, idSetY, 1, LONG2NUM((long)sy));
    rb_funcall(sprite, idSetZ, 1, LONG2NUM(sz));
    return Qtrue;
}
}

extern "C" void vitaSpriteNativeInit(void)
{
    idRealX = rb_intern("@real_x");
    idRealY = rb_intern("@real_y");
    idJumpCount = rb_intern("@jump_count");
    idJumpPeak = rb_intern("@jump_peak");
    idPriority = rb_intern("@priority_type");
    idDisplayX = rb_intern("@display_x");
    idDisplayY = rb_intern("@display_y");
    idSetX = rb_intern("x=");
    idSetY = rb_intern("y=");
    idSetZ = rb_intern("z=");
    rb_define_global_function("vita_spc_eval", RUBY_METHOD_FUNC(rbEval), 1);
    rb_define_global_function("vita_spc_pos", RUBY_METHOD_FUNC(rbPos), 2);
}
