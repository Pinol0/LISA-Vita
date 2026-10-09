// Host test for MKXP_VITA_SCRIPT_UTF8 (src/main.cpp) linked into a real Ruby 3.1.6 (miniruby objects):
// a script with a non-ASCII regexp scans a UTF-8 note as in RGSS3 (a VX Ace game failed here with
// Encoding::CompatibilityError), and everything else is what rb_eval_string_protect gives: self, file
// name, line numbers, top-level methods / constants, local scope, exceptions.
//   ./run.sh RUBY_BUILD_DIR RUBY_SRC_DIR   (mutants: ASCII-8BIT source, no default encodings: must FAIL)
#include <ruby.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "block.inc"
extern "C" void ruby_init_loadpath(void);
extern "C" void rb_call_builtin_inits(void);

static int fails = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok)
        ++fails;
}

static std::string str(VALUE v)
{
    VALUE s = rb_inspect(v);
    return std::string(RSTRING_PTR(s), RSTRING_LEN(s));
}

static std::string gv(const char *name) { return str(rb_gv_get(name)); }

static VALUE errLineBody(VALUE e)
{
    VALUE bt = rb_funcall(e, rb_intern("backtrace"), 0);
    VALUE m = rb_funcall(e, rb_intern("message"), 0);
    return rb_ary_new_from_args(3, rb_obj_class(e), m,
                                RB_TYPE_P(bt, T_ARRAY) && RARRAY_LEN(bt) ? rb_ary_entry(bt, 0) : Qnil);
}

/* "Class: message @ first backtrace line" of the pending exception (protected: never raises). */
static std::string errLine(int state)
{
    if (!state)
        return "(no error)";
    VALUE e = rb_errinfo();
    rb_set_errinfo(Qnil);
    if (!rb_obj_is_kind_of(e, rb_eException))
        return "state " + std::to_string(state) + " without exception: " + str(e);
    int st = 0;
    VALUE a = rb_protect(errLineBody, e, &st);
    if (st) {
        rb_set_errinfo(Qnil);
        return "(exception unreadable)";
    }
    VALUE m = rb_ary_entry(a, 1);
    return str(rb_ary_entry(a, 0)) + ": " + std::string(RSTRING_PTR(m), RSTRING_LEN(m)) + " @ " + str(rb_ary_entry(a, 2));
}

/* The same source through the port's path (vitaScriptEval) or the old one (rb_eval_string_protect). */
static int run(bool utf8, const std::string &src)
{
    int state = 0;
    if (utf8)
        vitaScriptEval(src.c_str(), (long)src.size(), &state);
    else
        rb_eval_string_protect(src.c_str(), &state);
    return state;
}

int main()
{
    ruby_init();
    ruby_init_loadpath();
    rb_call_builtin_inits();
    vitaScriptEncodingInit();   // where src/main.cpp calls it: after ruby_setup, before any script

    check(gv("$__dummy") == "nil", "globals readable");
    VALUE enc = rb_eval_string("[Encoding.default_external.to_s, Encoding.default_internal.to_s]");
    check(str(enc) == "[\"UTF-8\", \"UTF-8\"]", "default_external / default_internal = UTF-8 (got " + str(enc) + ")");

    /* The database note, as Marshal gives it (UTF-8), and the shape of that game's limit-break script. */
    rb_gv_set("$note", rb_utf8_str_new_cstr("<限界突破：1,5>\n<限界突破:2,7>\nother\n"));
    int st = run(true,
        "module LB; WORD = '限界突破'; end\n"
        "class VitaFeat\n"
        "  def f_limit_param\n"
        "    r = [0] * 8\n"
        "    $note.each_line do |l|\n"
        "      memo = l.scan(/<#{LB::WORD}[：:](\\S+),(\\S+)>/).flatten\n"
        "      r[memo[0].to_i] += memo[1].to_i if memo != nil and memo.size == 2\n"
        "    end\n"
        "    r\n"
        "  end\n"
        "end\n"
        "$limit = VitaFeat.new.f_limit_param\n"
        "$lit = ['×'.encoding.to_s, /：/.encoding.to_s, __ENCODING__.to_s]\n");
    check(st == 0, "non-ASCII regexp scans a UTF-8 note: " + errLine(st));
    check(gv("$limit") == "[0, 5, 7, 0, 0, 0, 0, 0]", "values read from the note (got " + gv("$limit") + ")");
    check(gv("$lit") == "[\"UTF-8\", \"UTF-8\", \"UTF-8\"]", "literals and __ENCODING__ are UTF-8 (got " + gv("$lit") + ")");

    /* Everything but the encoding as rb_eval_string_protect: same scripts through both paths. */
    const char *names[2] = {"rb_eval_string_protect", "vitaScriptEval"};
    std::string out[2];
    for (int u = 0; u < 2; ++u) {
        std::string p = u ? "u" : "b";
        std::string o;
        int s1 = run(u, "$s_" + p + " = [self.to_s, self.class.to_s, __FILE__, __LINE__]\n"
                        "def vita_top_" + p + "; 1; end\n"
                        "VITA_TOP_" + p + " = 2\n"
                        "vita_local_" + p + " = 3\n"
                        "class VitaK_" + p + "; def m; :ok; end; end\n");
        int s2 = run(u, "$t_" + p + " = [Object.private_method_defined?(:vita_top_" + p + "), Object.const_defined?(:VITA_TOP_" + p + "),\n"
                        "  defined?(vita_local_" + p + ").inspect, VitaK_" + p + ".new.m, Module.nesting.size]\n");
        o += "s1=" + std::to_string(s1) + " " + gv(("$s_" + p).c_str()) + "\n";
        o += "s2=" + std::to_string(s2) + " " + gv(("$t_" + p).c_str()) + "\n";
        int s3 = run(u, "x = 1\n\nraise ArgumentError, 'boom'\n");
        o += "raise: state=" + std::to_string(s3) + " " + errLine(s3) + "\n";
        /* SyntaxError: same class and message; its backtrace differs (Kernel#eval adds an "in `eval'"
           frame, as in upstream mkxp-z), so only the text before " @ " is compared. */
        int s4 = run(u, "def broken(\n");
        std::string se = errLine(s4);
        o += "syntax: state=" + std::to_string(s4) + " " + se.substr(0, se.find(" @ ")) + "\n";
        out[u] = o;
        std::printf("  %s:\n%s", names[u], o.c_str());
    }
    check(out[0] == out[1], "self, __FILE__, __LINE__, top-level def / constant / local / class, exceptions: same as rb_eval_string_protect");
    check(out[1].find("[\"main\", \"Object\", \"eval\", 1]") != std::string::npos, "top self, file \"eval\", line 1");
    check(out[1].find("ArgumentError: boom @ \"eval:3:in `<main>'\"") != std::string::npos, "exception backtrace eval:3");

    std::fflush(stdout);
    return fails ? 1 : 0;
}
