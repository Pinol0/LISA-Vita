// Host runner for src/vita-prof-sub.inc (MKXP_VITA_PROF_SUB) inside a real Ruby 3.1.6 (miniruby
// objects): fake microsecond clock (vita_test_tick), vita_prof_sub_dump = the PERF fragment.
#include <ruby.h>
#include <cstdint>
#include <cstdio>
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);
static uint64_t gClock = 1000;
static inline uint64_t vitaDiagNow() { return gClock; }
static const uint64_t kProfMask = (1u << 30) - 1;   /* as vita_diag.cpp */
static inline uint64_t profSince(VALUE t0) { return (vitaDiagNow() - (uint64_t)FIX2LONG(t0)) & kProfMask; }
#include "../../../src/vita-prof-sub.inc"
static VALUE rbTick(VALUE self, VALUE us) { (void)self; gClock += NUM2ULONG(us); return Qnil; }
static VALUE rbDump(VALUE self)
{
    (void)self;
    static char buf[8192];
    const int n = subAppend(buf, sizeof(buf));
    return rb_str_new(buf, n);
}
int main(int argc, char **argv)
{
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();
    rb_define_global_function("vita_prof_sub_slot", RUBY_METHOD_FUNC(rbProfSubSlot), 1);
    rb_define_global_function("vita_prof_sub_enter", RUBY_METHOD_FUNC(rbProfSubEnter), 0);
    rb_define_global_function("vita_prof_sub", RUBY_METHOD_FUNC(rbProfSub), 2);
    rb_define_global_function("vita_test_tick", RUBY_METHOD_FUNC(rbTick), 1);
    rb_define_global_function("vita_prof_sub_dump", RUBY_METHOD_FUNC(rbDump), 0);
    VALUE args = rb_ary_new();
    for (int i = 2; i < argc; ++i) rb_ary_push(args, rb_str_new_cstr(argv[i]));
    rb_gv_set("$test_args", args);
    int state = 0;
    rb_load_protect(rb_str_new_cstr(argv[1]), 0, &state);
    if (state) {
        VALUE m = rb_funcall(rb_errinfo(), rb_intern("full_message"), 0);
        std::fprintf(stderr, "%s\n", StringValueCStr(m));
        return 2;
    }
    VALUE rc = rb_gv_get("$test_rc");
    fflush(stdout);
    return FIXNUM_P(rc) ? FIX2INT(rc) : 3;
}
