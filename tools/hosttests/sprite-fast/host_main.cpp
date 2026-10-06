// Host runner for src/vita-sprite-fast-native.cpp (MKXP_VITA_SPRITE_FAST_NATIVE) inside a real Ruby
// 3.1.6 (miniruby objects): defines the native modules, then runs t_sprite_fast.rb, whose blocks.rb
// (main.cpp's Ruby) prepends VitaOffscreenNative / VitaSpriteFastNative when they exist.
#include <ruby.h>
#include <cstdio>
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);
extern "C" void vitaSpriteFastNativeInit(void);
int main(int argc, char **argv)
{
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();
    vitaSpriteFastNativeInit();
    VALUE args = rb_ary_new();
    for (int i = 2; i < argc; ++i) rb_ary_push(args, rb_str_new_cstr(argv[i]));
    rb_gv_set("$test_args", args);
    rb_eval_string("$stdout.sync = true");
    int state = 0;
    rb_load_protect(rb_str_new_cstr(argv[1]), 0, &state);
    if (state) {
        VALUE e = rb_errinfo();
        if (rb_obj_is_kind_of(e, rb_eSystemExit)) return NUM2INT(rb_funcall(e, rb_intern("status"), 0));
        VALUE m = rb_funcall(e, rb_intern("full_message"), 0);
        std::fprintf(stderr, "%s\n", StringValueCStr(m));
        return 2;
    }
    return 0;
}
