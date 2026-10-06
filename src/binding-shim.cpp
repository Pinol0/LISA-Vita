#include "binding-util.h"
#include "exception.h"

#include <cassert>
#include <cstdarg>
#include <string>
#include "sharedstate.h"

//int SharedState::rgssVersion = 3;
//SharedState *SharedState::instance = nullptr;

void raiseDisposedAccess(VALUE self)
{
    (void)self;
    rb_raise(rb_eRuntimeError, "disposed mkxp object");
}

void raiseRbExc(Exception *exc)
{
    if (!exc)
        rb_raise(rb_eRuntimeError, "unknown mkxp exception");

    std::string message = exc->msg;
    Exception::Type type = exc->type;

    delete exc;

    switch (type) {
    case Exception::TypeError:
        rb_raise(rb_eTypeError, "%s", message.c_str());
        break;

    case Exception::ArgumentError:
        rb_raise(rb_eArgError, "%s", message.c_str());
        break;

    case Exception::IOError:
        rb_raise(rb_eIOError, "%s", message.c_str());
        break;

    default:
        rb_raise(rb_eRuntimeError, "%s", message.c_str());
        break;
    }
}

#ifdef MKXP_VITA_RGSS_ARGS_V2
/*
 * mkxp-z binding-util.cpp rb_get_args, ported as is (formats o S s z f i b n |, the int/float/bool
 * coercions of rb_int_arg/rb_float_arg/rb_bool_arg, arity and RB_ARG_END checks), with one fix:
 * upstream 'S' and 's' do not advance the argument pointer (the next specifier would read the same
 * VALUE); here they do.
 */
int rb_get_args(int argc, VALUE *argv, const char *format, ...)
{
    Exception *exc = 0;
    try {
        char c;
        VALUE *arg = argv;
        va_list ap;
        bool opt = false;
        int argI = 0;

        va_start(ap, format);

        while ((c = *format++)) {
            switch (c) {
            case '|':
                break;
            default:
                if (argc <= argI && !opt)
                    rb_raise(rb_eArgError, "wrong number of arguments");
                break;
            }

            if (argI >= argc)
                break;

            switch (c) {
            case 'o': {
                VALUE *obj = va_arg(ap, VALUE *);
                *obj = *arg++;
                ++argI;
                break;
            }
            case 'S': {
                VALUE *str = va_arg(ap, VALUE *);
                VALUE tmp = *arg++;
                if (!RB_TYPE_P(tmp, RUBY_T_STRING))
                    rb_raise(rb_eTypeError, "Argument %d: Expected string", argI);
                *str = tmp;
                ++argI;
                break;
            }
            case 's': {
                const char **str = va_arg(ap, const char **);
                int *len = va_arg(ap, int *);
                VALUE tmp = *arg++;
                if (!RB_TYPE_P(tmp, RUBY_T_STRING))
                    rb_raise(rb_eTypeError, "Argument %d: Expected string", argI);
                *str = RSTRING_PTR(tmp);
                *len = RSTRING_LEN(tmp);
                ++argI;
                break;
            }
            case 'z': {
                const char **str = va_arg(ap, const char **);
                VALUE tmp = *arg++;
                if (!RB_TYPE_P(tmp, RUBY_T_STRING))
                    rb_raise(rb_eTypeError, "Argument %d: Expected string", argI);
                *str = RSTRING_PTR(tmp);
                ++argI;
                break;
            }
            case 'f': {
                double *f = va_arg(ap, double *);
                rb_float_arg(*arg++, f, argI);
                ++argI;
                break;
            }
            case 'i': {
                int *i = va_arg(ap, int *);
                rb_int_arg(*arg++, i, argI);
                ++argI;
                break;
            }
            case 'b': {
                bool *b = va_arg(ap, bool *);
                rb_bool_arg(*arg++, b, argI);
                ++argI;
                break;
            }
            case 'n': {
                ID *sym = va_arg(ap, ID *);
                VALUE symVal = *arg++;
                if (!SYMBOL_P(symVal))
                    rb_raise(rb_eTypeError, "Argument %d: Expected symbol", argI);
                *sym = SYM2ID(symVal);
                ++argI;
                break;
            }
            case '|':
                opt = true;
                break;
            default:
                rb_raise(rb_eFatal, "invalid argument specifier %c", c);
            }
        }

#ifndef NDEBUG
        /* Pop the remaining pointers to check for RB_ARG_END. */
        format--;
        while ((c = *format++)) {
            switch (c) {
            case 'o': case 'S': va_arg(ap, VALUE *); break;
            case 's': va_arg(ap, const char **); va_arg(ap, int *); break;
            case 'z': va_arg(ap, const char **); break;
            case 'f': va_arg(ap, double *); break;
            case 'i': va_arg(ap, int *); break;
            case 'b': va_arg(ap, bool *); break;
            case 'n': va_arg(ap, ID *); break;
            }
        }
        if (!c && argc > argI)
            rb_raise(rb_eArgError, "wrong number of arguments");
        void *argEnd = va_arg(ap, void *);
        (void)argEnd;
        assert(argEnd == RB_ARG_END_VAL);
#endif
        va_end(ap);
        return argI;
    } catch (const Exception &e) {
        exc = new Exception(e);
    }
    if (exc)
        raiseRbExc(exc);
    return 0;
}
#else
int rb_get_args(int argc, VALUE *argv, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    int arg = 0;
    bool optional = false;

    for (const char *p = format; *p; ++p) {
        if (*p == '|') {
            optional = true;
            continue;
        }

        if (arg >= argc) {
            if (optional)
                break;

            va_end(ap);
            rb_error_arity(argc, arg + 1, arg + 1);
        }

        VALUE value = argv[arg++];

        switch (*p) {
        case 'o': {
            VALUE *out = va_arg(ap, VALUE *);
            *out = value;
            break;
        }

        case 's': {
    const char **out = va_arg(ap, const char **);
    int *len = va_arg(ap, int *);

    if (!RB_TYPE_P(value, RUBY_T_STRING)) {
        va_end(ap);
        rb_raise(
            rb_eTypeError,
            "Argument %d: Expected string",
            arg - 1
        );
    }

    *out = RSTRING_PTR(value);
    *len = RSTRING_LEN(value);

    break;
}
        case 'i': {
            int *out = va_arg(ap, int *);
            *out = NUM2INT(value);
            break;
        }

        case 'f': {
            double *out = va_arg(ap, double *);
            *out = NUM2DBL(value);
            break;
        }
case 'b': {
    bool *out = va_arg(ap, bool *);
    *out = RTEST(value);
    break;
}
        default:
            va_end(ap);
            rb_raise(
                rb_eRuntimeError,
                "unsupported rb_get_args format: %c",
                *p
            );
        }
    }

    va_end(ap);

    return arg;
}
#endif /* MKXP_VITA_RGSS_ARGS_V2 */
