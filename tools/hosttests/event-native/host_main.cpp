// Host runner for src/vita-event-native.cpp inside a real Ruby 3.1.6 (miniruby objects): initializes
// Ruby and the native module, then runs the Ruby test script given as argv[1] (ARGV = argv[2..]).
#include <ruby.h>
#include <cstdio>
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);
extern "C" void vitaEventNativeInit(void);
int main(int argc, char **argv)
{
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();
    vitaEventNativeInit();
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
