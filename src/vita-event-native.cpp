/*
 * Native Game_Event#update fast path (MKXP_VITA_EVENT_NATIVE): the EVENT_FAST check in C.
 * d72 profiler: on the maps with ~140 events (149 Fishman Town, 125 Area 2) the Ruby fast path ran
 * ~130 times per frame, ~2 ms/frame (regression on the PERF windows: ~17 us per call). Same
 * conditions in the same order as the Ruby version (main.cpp, MKXP_VITA_EVENT_FAST), same writes:
 *
 *   if $vita_event_fast && @interpreter.nil? && @trigger != 3 && !@move_route_forcing && @move_type == 0 &&
 *      @jump_count == 0 && @real_x == @x && @real_y == @y && !@step_anime && @pattern == @original_pattern &&
 *      @anime_count == 0
 *     @stop_count += 1 unless @locked
 *     $vita_ev_fast_n += 1
 *     return
 *   end
 *   super
 *
 * Comparisons between two Fixnums are done in C (Integer#== / #!= cannot be redefined by RGSS
 * scripts here: checked at install, see below); anything else calls the Ruby method (==, !=, +),
 * as the interpreter would. $vita_event_fast and $vita_ev_fast_n are bound to C variables
 * (rb_define_variable), so Ruby assignments (the toggle, the PERF reset) still work.
 * The module is VitaEventNative; main.cpp prepends it instead of the Ruby module when the boot
 * self-test (vita_event_native_check vs the Ruby condition on random states) agrees.
 * Host test with LISA's scripts: tools/hosttests/event-native/.
 */
#include <ruby.h>

namespace
{
VALUE gFast = Qfalse;   /* $vita_event_fast */
VALUE gFastN = INT2FIX(0);   /* $vita_ev_fast_n */
ID idInterpreter, idTrigger, idMoveRouteForcing, idMoveType, idJumpCount, idRealX, idRealY, idX, idY, idStepAnime,
    idPattern, idOriginalPattern, idAnimeCount, idStopCount, idLocked, idEq, idNeq, idPlus;

inline bool eq(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return a == b;
    return RTEST(rb_funcall(a, idEq, 1, b));
}

inline bool neq(VALUE a, VALUE b)
{
    if (FIXNUM_P(a) && FIXNUM_P(b))
        return a != b;
    return RTEST(rb_funcall(a, idNeq, 1, b));
}

inline VALUE plus1(VALUE v)
{
    if (FIXNUM_P(v) && FIX2LONG(v) < FIXNUM_MAX)
        return LONG2FIX(FIX2LONG(v) + 1);
    return rb_funcall(v, idPlus, 1, INT2FIX(1));
}

/* The fast-path condition, in the Ruby evaluation order (&& short-circuits). */
bool still(VALUE self)
{
    return RTEST(gFast) && NIL_P(rb_ivar_get(self, idInterpreter)) && neq(rb_ivar_get(self, idTrigger), INT2FIX(3)) &&
           !RTEST(rb_ivar_get(self, idMoveRouteForcing)) && eq(rb_ivar_get(self, idMoveType), INT2FIX(0)) &&
           eq(rb_ivar_get(self, idJumpCount), INT2FIX(0)) && eq(rb_ivar_get(self, idRealX), rb_ivar_get(self, idX)) &&
           eq(rb_ivar_get(self, idRealY), rb_ivar_get(self, idY)) && !RTEST(rb_ivar_get(self, idStepAnime)) &&
           eq(rb_ivar_get(self, idPattern), rb_ivar_get(self, idOriginalPattern)) &&
           eq(rb_ivar_get(self, idAnimeCount), INT2FIX(0));
}

void fastStep(VALUE self)
{
    if (!RTEST(rb_ivar_get(self, idLocked)))
        rb_ivar_set(self, idStopCount, plus1(rb_ivar_get(self, idStopCount)));
    gFastN = plus1(gFastN);
}

/* VitaEventNative#update */
VALUE rbUpdate(VALUE self)
{
    if (still(self)) {
        fastStep(self);
        return Qnil;
    }
    return rb_call_super(0, nullptr);
}

/* vita_event_native_check(event) -> true if the fast path applies (and was applied), false if not
 * (nothing written). Self-test and host test only. */
VALUE rbCheck(VALUE self, VALUE ev)
{
    (void)self;
    if (!still(ev))
        return Qfalse;
    fastStep(ev);
    return Qtrue;
}
} // namespace

/* main.cpp, before the game scripts and before the EVENT_FAST block assigns the globals. */
extern "C" void vitaEventNativeInit(void)
{
    idInterpreter = rb_intern("@interpreter");
    idTrigger = rb_intern("@trigger");
    idMoveRouteForcing = rb_intern("@move_route_forcing");
    idMoveType = rb_intern("@move_type");
    idJumpCount = rb_intern("@jump_count");
    idRealX = rb_intern("@real_x");
    idRealY = rb_intern("@real_y");
    idX = rb_intern("@x");
    idY = rb_intern("@y");
    idStepAnime = rb_intern("@step_anime");
    idPattern = rb_intern("@pattern");
    idOriginalPattern = rb_intern("@original_pattern");
    idAnimeCount = rb_intern("@anime_count");
    idStopCount = rb_intern("@stop_count");
    idLocked = rb_intern("@locked");
    idEq = rb_intern("==");
    idNeq = rb_intern("!=");
    idPlus = rb_intern("+");
    rb_define_variable("$vita_event_fast", &gFast);
    rb_define_variable("$vita_ev_fast_n", &gFastN);
    const VALUE mod = rb_define_module("VitaEventNative");
    rb_define_method(mod, "update", RUBY_METHOD_FUNC(rbUpdate), 0);
    rb_define_global_function("vita_event_native_check", RUBY_METHOD_FUNC(rbCheck), 1);
}
