// Host test for src/binding-shim.cpp raiseRbExc (MKXP_VITA_AUDIT_FIXES) inside a real Ruby 3.1.6
// (miniruby objects): every mkxp Exception type becomes the Ruby class a script expects, and the
// classes a plain `rescue` caught before (RuntimeError) are still caught by it.
#include "binding-util.h"
#include "exception.h"
#include <cstdio>
#include <cstring>
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);
void raiseRbExc(Exception *exc);
VALUE vitaRgssErrorClass();
static int gType;
static VALUE raiser(VALUE) { raiseRbExc(new Exception((Exception::Type)gType, "msg %d", gType)); return Qnil; }
static int fails = 0;
static void check(bool ok, const char *what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }
int main()
{
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();
    vitaRgssErrorClass();
    check(rb_const_defined(rb_cObject, rb_intern("RGSSError")) && rb_const_defined(rb_cObject, rb_intern("RGSSReset")),
          "RGSSError and RGSSReset defined before any error");
    const VALUE rgss = rb_const_get(rb_cObject, rb_intern("RGSSError"));
    check(RTEST(rb_class_inherited_p(rgss, rb_eStandardError)), "RGSSError < StandardError (plain rescue catches it)");
    const VALUE enoent = rb_const_get(rb_const_get(rb_cObject, rb_intern("Errno")), rb_intern("ENOENT"));
    const struct { Exception::Type t; VALUE cls; const char *name; } cases[] = {
        { Exception::NoFileError, enoent, "NoFileError -> Errno::ENOENT" },
        { Exception::RGSSError, rgss, "RGSSError -> RGSSError" },
        { Exception::SystemExit, rb_eSystemExit, "SystemExit -> SystemExit" },
        { Exception::TypeError, rb_eTypeError, "TypeError -> TypeError" },
        { Exception::ArgumentError, rb_eArgError, "ArgumentError -> ArgumentError" },
        { Exception::IOError, rb_eIOError, "IOError -> IOError" },
        { Exception::MKXPError, rb_eRuntimeError, "MKXPError -> RuntimeError (plain rescue still catches it)" },
        { Exception::RuntimeError, rb_eRuntimeError, "RuntimeError -> RuntimeError" },
    };
    for (const auto &c : cases) {
        gType = c.t;
        int state = 0;
        rb_protect(raiser, Qnil, &state);
        const VALUE e = rb_errinfo();
        rb_set_errinfo(Qnil);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s, message kept", c.name);
        VALUE msg = state ? rb_funcall(e, rb_intern("message"), 0) : Qnil;
        check(state && rb_obj_class(e) == c.cls && !NIL_P(msg) && std::strstr(StringValueCStr(msg), "msg") != nullptr, buf);
        if (c.t != Exception::SystemExit)
            check(RTEST(rb_obj_is_kind_of(e, rb_eStandardError)), "  caught by a plain rescue");
    }
    std::printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
