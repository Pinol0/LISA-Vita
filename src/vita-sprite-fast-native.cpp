/*
 * Perf fix (MKXP_VITA_SPRITE_FAST_NATIVE): the OFFSCREEN_SPRITES and SPRITE_FAST checks of
 * Sprite_Character#update in C. d80/d81 profiler: on the busy maps ~33 character sprites per frame
 * go through those two Ruby wrappers (off-screen skip + unchanged-sprite fast path) before the
 * stock update chain, ~0.85 ms/frame of Ruby condition code (Spriteset_Map#update_characters own
 * time). The two modules here are the same code as the Ruby ones in main.cpp (VITA_OFFSCREEN and
 * VITA_SPRITE_FAST blocks), in the same places of the chain:
 *   - every game method is still called as Ruby does (rb_funcall, same order, same short-circuit):
 *     animation?, graphic_changed?, jumping?, vita_csprite_update, super; the character's readers
 *     too, except those main.cpp found to be Game_CharacterBase's own attr_reader/attr_accessor
 *     in every character class (no bytecode: RubyVM::InstructionSequence.of = nil, same definition
 *     in each subclass): those read @name directly, on objects of those classes without a
 *     singleton class (d82 on the Vita: with an rb_funcall per reader, ~16 per sprite, the C
 *     version cost as much as the Ruby one, ~28 us per sprite: rb_funcall has no inline cache);
 *   - the sprite's own instance variables are read and written directly (@character, @vsf_*,
 *     @vita_off_*, @balloon_sprite, @cw, @ch);
 *   - == and the Integer arithmetic are done in C only for two Fixnums (and two Floats for ==),
 *     anything else calls the Ruby method, as the interpreter would;
 *   - the globals shared with the Ruby side ($vita_sf_on, $vita_sf_dx, ...) are bound to C variables
 *     (rb_define_variable, before main.cpp's blocks assign them).
 * Host test: tools/hosttests/sprite-fast/ runs LISA's Sprite_Character against both versions.
 */
#include <ruby.h>
#include <cstring>
#include <string>

namespace
{
VALUE gSfOn = Qfalse, gSfDx = Qnil, gSfDy = Qnil, gSfastN = INT2FIX(0);                        /* SPRITE_FAST */
VALUE gOffOk = Qfalse, gOffX0 = INT2FIX(0), gOffY0 = INT2FIX(0), gOffW = INT2FIX(0), gOffH = INT2FIX(0), gSkipN = INT2FIX(0);   /* OFFSCREEN */
VALUE cSpriteCharacter = Qnil, cCharacterBase = Qnil;
/* Readers that may be read as instance variables (see above). */
enum Reader { R_X, R_Y, R_REAL_X, R_REAL_Y, R_PATTERN, R_DIRECTION, R_OPACITY, R_BLEND_TYPE, R_BUSH_DEPTH, R_TRANSPARENT,
              R_PRIORITY_TYPE, R_ANIMATION_ID, R_BALLOON_ID, R_COUNT };
const char *const kReaderNames[R_COUNT] = { "x", "y", "real_x", "real_y", "pattern", "direction", "opacity", "blend_type",
                                            "bush_depth", "transparent", "priority_type", "animation_id", "balloon_id" };
bool gDirect[R_COUNT];
ID gReaderMeth[R_COUNT], gReaderIvar[R_COUNT];
ID idCharacter, idBalloonSprite, idCw, idCh, idOffReady, idOffSkipped, idVsfOk, idVsfC, idVsfDx, idVsfDy, idVsfRx, idVsfRy,
    idVsfPat, idVsfDir, idVsfOp, idVsfBt, idVsfBd, idVsfTr, idVsfPt;
ID idAnimationP, idGraphicChangedP, idAnimationId, idBalloonId, idJumpingP, idRealX, idRealY, idPattern, idDirection,
    idOpacity, idBlendType, idBushDepth, idTransparent, idPriorityType, idX, idY, idCsUpdate, idEq, idLt, idGt, idPlus,
    idMinus, idDiv, idUminus;

inline VALUE iv(VALUE o, ID id) { return rb_ivar_get(o, id); }
inline VALUE call(VALUE o, ID id) { return rb_funcall(o, id, 0); }
/* c's reader r: the instance variable when it is the stock attr method for c, else the method. */
inline bool plainCharacter(VALUE c)
{
    return !SPECIAL_CONST_P(c) && CLASS_OF(c) == rb_obj_class(c) && RTEST(rb_obj_is_kind_of(c, cCharacterBase));
}
inline VALUE rd(VALUE c, bool plain, Reader r)
{
    return plain && gDirect[r] ? rb_ivar_get(c, gReaderIvar[r]) : rb_funcall(c, gReaderMeth[r], 0);
}

/* a == b as Ruby evaluates it with a as the receiver. */
bool eq(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b)) return a == b;
    if (RB_FLOAT_TYPE_P(a) && RB_FLOAT_TYPE_P(b)) return RFLOAT_VALUE(a) == RFLOAT_VALUE(b);
    if (a == Qtrue || a == Qfalse || a == Qnil) return a == b;   /* TrueClass/FalseClass/NilClass#== : identity */
    return RTEST(rb_funcall(a, idEq, 1, b));
}
bool lt(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b)) return FIX2LONG(a) < FIX2LONG(b);
    return RTEST(rb_funcall(a, idLt, 1, b));
}
bool gt(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b)) return FIX2LONG(a) > FIX2LONG(b);
    return RTEST(rb_funcall(a, idGt, 1, b));
}
/* Integer results stay in the Fixnum range here (map coordinates, sprite sizes); else Ruby ops. */
VALUE plus(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b)) return LONG2NUM(FIX2LONG(a) + FIX2LONG(b));
    return rb_funcall(a, idPlus, 1, b);
}
VALUE minus(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b)) return LONG2NUM(FIX2LONG(a) - FIX2LONG(b));
    return rb_funcall(a, idMinus, 1, b);
}
VALUE fdiv_floor(VALUE a, long d)   /* a / d for a positive constant d (Integer#/ floors) */
{
    if (FIXNUM_P(a)) {
        const long x = FIX2LONG(a);
        long q = x / d;
        if ((x % d) != 0 && x < 0) --q;
        return LONG2NUM(q);
    }
    return rb_funcall(a, idDiv, 1, LONG2NUM(d));
}
VALUE neg(VALUE a)
{
    if (FIXNUM_P(a)) return LONG2NUM(-FIX2LONG(a));
    return rb_funcall(a, idUminus, 0);
}
inline VALUE orDefault(VALUE v, long d) { return RTEST(v) ? v : LONG2FIX(d); }   /* v || d */

/* ---- OFFSCREEN_SPRITES: Sprite_Character#vita_offscreen? + #update ---------------------------- */
bool offscreen(VALUE self)
{
    const VALUE c = iv(self, idCharacter);
    if (!RTEST(c) || rb_obj_class(self) != cSpriteCharacter) return false;
    const bool plain = plainCharacter(c);
    if (RTEST(iv(self, idBalloonSprite)) || RTEST(call(self, idAnimationP)) || gt(rd(c, plain, R_ANIMATION_ID), INT2FIX(0)) ||
        gt(rd(c, plain, R_BALLOON_ID), INT2FIX(0)))
        return false;
    const VALUE mx = plus(fdiv_floor(orDefault(iv(self, idCw), 32), 64), INT2FIX(3));
    const VALUE my = plus(fdiv_floor(orDefault(iv(self, idCh), 32), 32), INT2FIX(3));
    const VALUE tx = minus(rd(c, plain, R_X), gOffX0);
    const VALUE ty = minus(rd(c, plain, R_Y), gOffY0);
    return lt(tx, neg(mx)) || gt(tx, plus(gOffW, mx)) || lt(ty, neg(my)) || gt(ty, plus(gOffH, my));
}

VALUE rbOffUpdate(VALUE self)
{
    if (RTEST(gOffOk) && offscreen(self)) {
        if (RTEST(iv(self, idOffReady))) {
            gSkipN = plus(gSkipN, INT2FIX(1));
            rb_ivar_set(self, idOffSkipped, Qtrue);   /* sprite left stale: no SPRITE_FAST snapshot this frame */
            return Qnil;
        }
        rb_ivar_set(self, idOffSkipped, Qfalse);
        rb_call_super(0, nullptr);
        rb_ivar_set(self, idOffReady, Qtrue);
        return Qtrue;
    }
    rb_ivar_set(self, idOffSkipped, Qfalse);
    rb_ivar_set(self, idOffReady, Qfalse);
    return rb_call_super(0, nullptr);
}

/* ---- SPRITE_FAST: Sprite_Character#update --------------------------------------------------- */
bool unchanged(VALUE self, VALUE c)
{
    if (!(RTEST(gSfOn) && RTEST(iv(self, idVsfOk)) && RTEST(c) && iv(self, idVsfC) == c && !RTEST(iv(self, idBalloonSprite)) &&
          !RTEST(call(self, idAnimationP))))
        return false;
    const bool plain = plainCharacter(c);
    return eq(rd(c, plain, R_ANIMATION_ID), INT2FIX(0)) && eq(rd(c, plain, R_BALLOON_ID), INT2FIX(0)) &&
           !RTEST(call(self, idGraphicChangedP)) && !RTEST(call(c, idJumpingP)) && eq(iv(self, idVsfDx), gSfDx) &&
           eq(iv(self, idVsfDy), gSfDy) && eq(iv(self, idVsfRx), rd(c, plain, R_REAL_X)) && eq(iv(self, idVsfRy), rd(c, plain, R_REAL_Y)) &&
           eq(iv(self, idVsfPat), rd(c, plain, R_PATTERN)) && eq(iv(self, idVsfDir), rd(c, plain, R_DIRECTION)) &&
           eq(iv(self, idVsfOp), rd(c, plain, R_OPACITY)) && eq(iv(self, idVsfBt), rd(c, plain, R_BLEND_TYPE)) &&
           eq(iv(self, idVsfBd), rd(c, plain, R_BUSH_DEPTH)) && eq(iv(self, idVsfTr), rd(c, plain, R_TRANSPARENT)) &&
           eq(iv(self, idVsfPt), rd(c, plain, R_PRIORITY_TYPE));
}

VALUE rbFastUpdate(VALUE self)
{
    const VALUE c = iv(self, idCharacter);
    if (unchanged(self, c)) {
        gSfastN = plus(gSfastN, INT2FIX(1));
        call(self, idCsUpdate);
        return Qnil;
    }
    rb_call_super(0, nullptr);
    /* No snapshot while jumping: the first landed frame changes y with the same inputs (host test). */
    if (RTEST(iv(self, idOffSkipped)) || !RTEST(gSfOn) || !RTEST(c) || rb_obj_class(self) != cSpriteCharacter ||
        RTEST(call(c, idJumpingP))) {
        rb_ivar_set(self, idVsfOk, Qfalse);
        return Qfalse;
    }
    const bool plain = plainCharacter(c);
    rb_ivar_set(self, idVsfOk, Qtrue);
    rb_ivar_set(self, idVsfC, c);
    rb_ivar_set(self, idVsfDx, gSfDx);
    rb_ivar_set(self, idVsfDy, gSfDy);
    rb_ivar_set(self, idVsfRx, rd(c, plain, R_REAL_X));
    rb_ivar_set(self, idVsfRy, rd(c, plain, R_REAL_Y));
    rb_ivar_set(self, idVsfPat, rd(c, plain, R_PATTERN));
    rb_ivar_set(self, idVsfDir, rd(c, plain, R_DIRECTION));
    rb_ivar_set(self, idVsfOp, rd(c, plain, R_OPACITY));
    rb_ivar_set(self, idVsfBt, rd(c, plain, R_BLEND_TYPE));
    rb_ivar_set(self, idVsfBd, rd(c, plain, R_BUSH_DEPTH));
    rb_ivar_set(self, idVsfTr, rd(c, plain, R_TRANSPARENT));
    const VALUE pt = rd(c, plain, R_PRIORITY_TYPE);
    rb_ivar_set(self, idVsfPt, pt);
    return pt;
}

/* vita_sprite_fast_native_bind(Sprite_Character, Game_CharacterBase, ["real_x", ...]): the class the
 * instance_of? checks compare with, and the readers main.cpp verified as stock attr methods. */
VALUE rbBind(VALUE self, VALUE klass, VALUE base, VALUE names)
{
    (void)self;
    cSpriteCharacter = klass;
    cCharacterBase = base;
    rb_gc_register_mark_object(klass);
    rb_gc_register_mark_object(base);
    for (int r = 0; r < R_COUNT; ++r) gDirect[r] = false;
    Check_Type(names, T_ARRAY);
    for (long i = 0; i < RARRAY_LEN(names); ++i) {
        VALUE n = rb_ary_entry(names, i);
        for (int r = 0; r < R_COUNT; ++r)
            if (RB_TYPE_P(n, T_STRING) && std::strcmp(StringValueCStr(n), kReaderNames[r]) == 0) gDirect[r] = true;
    }
    return Qtrue;
}
/* vita_sprite_fast_native_direct -> the readers read as instance variables (qa.log). */
VALUE rbDirect(VALUE self)
{
    (void)self;
    const VALUE a = rb_ary_new();
    for (int r = 0; r < R_COUNT; ++r)
        if (gDirect[r]) rb_ary_push(a, rb_str_new_cstr(kReaderNames[r]));
    return a;
}
} // namespace

/* main.cpp, after ruby_setup and before the game scripts (the globals must be bound before the
 * VITA_OFFSCREEN / VITA_SPRITE_FAST blocks assign them). */
extern "C" void vitaSpriteFastNativeInit(void)
{
    struct { ID *id; const char *name; } ids[] = {
        { &idCharacter, "@character" }, { &idBalloonSprite, "@balloon_sprite" }, { &idCw, "@cw" }, { &idCh, "@ch" },
        { &idOffReady, "@vita_off_ready" }, { &idOffSkipped, "@vita_off_skipped" }, { &idVsfOk, "@vsf_ok" }, { &idVsfC, "@vsf_c" },
        { &idVsfDx, "@vsf_dx" }, { &idVsfDy, "@vsf_dy" }, { &idVsfRx, "@vsf_rx" }, { &idVsfRy, "@vsf_ry" }, { &idVsfPat, "@vsf_pat" },
        { &idVsfDir, "@vsf_dir" }, { &idVsfOp, "@vsf_op" }, { &idVsfBt, "@vsf_bt" }, { &idVsfBd, "@vsf_bd" }, { &idVsfTr, "@vsf_tr" },
        { &idVsfPt, "@vsf_pt" }, { &idAnimationP, "animation?" }, { &idGraphicChangedP, "graphic_changed?" },
        { &idAnimationId, "animation_id" }, { &idBalloonId, "balloon_id" }, { &idJumpingP, "jumping?" }, { &idRealX, "real_x" },
        { &idRealY, "real_y" }, { &idPattern, "pattern" }, { &idDirection, "direction" }, { &idOpacity, "opacity" },
        { &idBlendType, "blend_type" }, { &idBushDepth, "bush_depth" }, { &idTransparent, "transparent" },
        { &idPriorityType, "priority_type" }, { &idX, "x" }, { &idY, "y" }, { &idCsUpdate, "vita_csprite_update" }, { &idEq, "==" },
        { &idLt, "<" }, { &idGt, ">" }, { &idPlus, "+" }, { &idMinus, "-" }, { &idDiv, "/" }, { &idUminus, "-@" } };
    for (auto &e : ids) *e.id = rb_intern(e.name);
    rb_define_variable("$vita_sf_on", &gSfOn);
    rb_define_variable("$vita_sf_dx", &gSfDx);
    rb_define_variable("$vita_sf_dy", &gSfDy);
    rb_define_variable("$vita_sfast_n", &gSfastN);
    rb_define_variable("$vita_off_ok", &gOffOk);
    rb_define_variable("$vita_off_x0", &gOffX0);
    rb_define_variable("$vita_off_y0", &gOffY0);
    rb_define_variable("$vita_off_w", &gOffW);
    rb_define_variable("$vita_off_h", &gOffH);
    rb_define_variable("$vita_skip_n", &gSkipN);
    const VALUE off = rb_define_module("VitaOffscreenNative");
    rb_define_method(off, "update", RUBY_METHOD_FUNC(rbOffUpdate), 0);
    const VALUE fast = rb_define_module("VitaSpriteFastNative");
    rb_define_method(fast, "update", RUBY_METHOD_FUNC(rbFastUpdate), 0);
    for (int r = 0; r < R_COUNT; ++r) {
        gReaderMeth[r] = rb_intern(kReaderNames[r]);
        gReaderIvar[r] = rb_intern((std::string("@") + kReaderNames[r]).c_str());
    }
    rb_define_global_function("vita_sprite_fast_native_bind", RUBY_METHOD_FUNC(rbBind), 3);
    rb_define_global_function("vita_sprite_fast_native_direct", RUBY_METHOD_FUNC(rbDirect), 0);
}
