#include "binding-types.h"
#include "vita_paths.h"
#include "binding-util.h"
#include "disposable-binding.h"
#include "bitmap.h"
#include "etc.h"
#include "vita-font.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

/* Diagnostic only (MKXP_VITA_TIMELINE / MKXP_VITA_PERF_BITMAP): no code when OFF. */
#include "vita_diag.h"

DEF_TYPE(Bitmap);

void bitmapInitProps(Bitmap *bitmap, VALUE self)
{
    /*
     * Nel backend Vita minimale Font è ancora
     * implementato come classe Ruby.
     *
     * WindowVX usa questa funzione anche quando crea
     * automaticamente Bitmap.new(1, 1) per contents.
     */
    (void)bitmap;

    VALUE fontClass =
        rb_const_get(
            rb_cObject,
            rb_intern("Font")
        );

    VALUE fontObj =
        rb_funcall(
            fontClass,
            rb_intern("new"),
            0
        );

    rb_iv_set(
        self,
        "@font",
        fontObj
    );
}

#ifdef MKXP_VITA_DEBUG_BITMAP_WINDOW
/* Diagnostic only: log the next N Bitmap window operations after vita_bitmap_log_arm(N)
 * (Scene_Battle#start and #terminate), with the region checksum after the operation. */
static int vitaBmpLogLeft = 0;

static VALUE vitaBmpLogArm(VALUE self, VALUE n)
{
    (void)self;
    vitaBmpLogLeft = NUM2INT(n);
    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) { std::fprintf(f, "BMPWIN_LOG_ARMED next=%d\n", vitaBmpLogLeft); std::fclose(f); }
    return Qnil;
}

static void vitaBmpLog(const char *op, Bitmap *b, int x, int y, int w, int h, const char *extra)
{
    if (vitaBmpLogLeft <= 0)
        return;
    --vitaBmpLogLeft;
    const long frame = NUM2LONG(rb_funcall(rb_const_get(rb_cObject, rb_intern("Graphics")), rb_intern("frame_count"), 0));
    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (!f)
        return;
    std::fprintf(f, "BMPWIN frame=%ld %s bitmap=%p size=%dx%d rect=%d,%d,%d,%d hash=0x%08x%s%s\n",
                 frame, op, (void *)b, b->width(), b->height(), x, y, w, h,
                 b->vitaRegionHash(x, y, w, h), extra ? " " : "", extra ? extra : "");
    std::fclose(f);
}
#define VITA_BMP_LOG(op, b, x, y, w, h, extra) vitaBmpLog(op, b, x, y, w, h, extra)
#else
#define VITA_BMP_LOG(op, b, x, y, w, h, extra) ((void)0)
#endif

#ifdef MKXP_VITA_BITMAP_LOAD_V2
/* Bitmap.new(filename) raises like mkxp-z (Errno::ENOENT, unsupported format): guarded. */
RB_METHOD_GUARD(bitmapVitaInitialize)
#else
RB_METHOD(bitmapVitaInitialize)
#endif
{
    Bitmap *bitmap = nullptr;

    if (argc == 1)
    {
        VALUE filenameObj = argv[0];
        VALUE filenameStr = rb_obj_as_string(filenameObj);

        bitmap = new Bitmap(
            StringValueCStr(filenameStr)
        );
    }
    else if (argc == 2)
    {
        int width;
        int height;

        rb_get_args(
            argc,
            argv,
            "ii",
            &width,
            &height
            RB_ARG_END
        );

        bitmap = new Bitmap(
            width,
            height
        );
    }
    else
    {
        rb_raise(
            rb_eArgError,
            "Bitmap.new expects filename or width,height"
        );
    }

        setPrivateData(self, bitmap);
    VITA_BMP_LOG("NEW", bitmap, 0, 0, bitmap->width(), bitmap->height(), argc == 1 ? "file" : "empty");

    bitmapInitProps(
        bitmap,
        self
    );

    return self;
}
#ifdef MKXP_VITA_BITMAP_LOAD_V2
RB_METHOD_GUARD_END
#endif


#ifdef MKXP_VITA_AUDIT_FIXES
/* Fix (MKXP_VITA_AUDIT_FIXES): guarded as upstream (bitmap-binding.cpp RB_METHOD_GUARD): on a disposed
 * bitmap Bitmap::width throws (RGSSError "disposed bitmap"); unguarded, the C++ exception crossed Ruby's
 * C frames: std::terminate, abort. */
RB_METHOD_GUARD(bitmapVitaWidth)
#else
RB_METHOD(bitmapVitaWidth)
#endif
{
    RB_UNUSED_PARAM;

    Bitmap *bitmap =
        getPrivateData<Bitmap>(self);

    return INT2FIX(
        bitmap->width()
    );
}
#ifdef MKXP_VITA_AUDIT_FIXES
RB_METHOD_GUARD_END
#endif


#ifdef MKXP_VITA_AUDIT_FIXES
/* Fix (MKXP_VITA_AUDIT_FIXES): guarded as upstream (bitmap-binding.cpp RB_METHOD_GUARD): on a disposed
 * bitmap Bitmap::height throws (RGSSError "disposed bitmap"); unguarded, the C++ exception crossed Ruby's
 * C frames: std::terminate, abort. */
RB_METHOD_GUARD(bitmapVitaHeight)
#else
RB_METHOD(bitmapVitaHeight)
#endif
{
    RB_UNUSED_PARAM;

    Bitmap *bitmap =
        getPrivateData<Bitmap>(self);

    return INT2FIX(
        bitmap->height()
    );
}
#ifdef MKXP_VITA_AUDIT_FIXES
RB_METHOD_GUARD_END
#endif

RB_METHOD_GUARD(bitmapVitaRect)
{
    RB_UNUSED_PARAM;

    Bitmap *b = getPrivateData<Bitmap>(self);

    IntRect rect;
    rect = b->rect();

    Rect *r = new Rect(rect);

    return wrapObject(r, RectType);
}
RB_METHOD_GUARD_END

/* Font state of a Bitmap's @font (Ruby Font from the main.cpp engine stub). */
static unsigned char vitaColorByte(double v)
{
    return (unsigned char)std::max(0.0, std::min(255.0, v));
}

static void vitaColorFromRuby(VALUE colorObj, unsigned char out[4])
{
    if (NIL_P(colorObj))
        return;
    Color *c = getPrivateDataCheck<Color>(colorObj, ColorType);
    out[0] = vitaColorByte(c->red);
    out[1] = vitaColorByte(c->green);
    out[2] = vitaColorByte(c->blue);
    out[3] = vitaColorByte(c->alpha);
}

static void vitaFontSpecFromRuby(VALUE fontObj, VitaFontSpec &spec)
{
    if (NIL_P(fontObj))
        return;

    VALUE name = rb_iv_get(fontObj, "@name");
    if (NIL_P(name))
        name = rb_funcall(rb_const_get(rb_cObject, rb_intern("Font")), rb_intern("default_name"), 0);
    if (RB_TYPE_P(name, T_ARRAY)) {
        for (long i = 0; i < RARRAY_LEN(name); ++i) {
            VALUE n = rb_obj_as_string(rb_ary_entry(name, i));
            spec.names.push_back(StringValueCStr(n));
        }
    } else if (!NIL_P(name)) {
        VALUE n = rb_obj_as_string(name);
        spec.names.push_back(StringValueCStr(n));
    }

    VALUE size = rb_iv_get(fontObj, "@size");
    if (!NIL_P(size))
        spec.size = NUM2INT(size);
    spec.bold = RTEST(rb_iv_get(fontObj, "@bold"));
    spec.italic = RTEST(rb_iv_get(fontObj, "@italic"));
    spec.outline = RTEST(rb_iv_get(fontObj, "@outline"));
    spec.shadow = RTEST(rb_iv_get(fontObj, "@shadow"));
    vitaColorFromRuby(rb_iv_get(fontObj, "@color"), spec.color);
    vitaColorFromRuby(rb_iv_get(fontObj, "@out_color"), spec.outColor);
}

#ifdef MKXP_VITA_DEBUG_TEXT_LOG
/* Diagnostic only: log the next N draw_text calls after vita_text_log_arm(N) (Scene_Battle#start). */
static int vitaTextLogLeft = 0;
static int vitaTextLogIndex = 0;

static VALUE vitaTextLogArm(VALUE self, VALUE n)
{
    (void)self;
    vitaTextLogLeft = NUM2INT(n);
    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) { std::fprintf(f, "TEXT_LOG_ARMED next=%d\n", vitaTextLogLeft); std::fclose(f); }
    return Qnil;
}

static void vitaTextLog(Bitmap *b, int x, int y, int w, int h, const char *text, int align,
                        const VitaFontSpec &spec, const VitaTextInfo &info, bool ttf)
{
    if (vitaTextLogLeft <= 0)
        return;
    --vitaTextLogLeft;
    char preview[28];
    size_t n = 0;
    for (const char *c = text; *c && n < sizeof(preview) - 1; ++c)
        preview[n++] = (*c == '"' || (unsigned char)*c < 32) ? '_' : *c;
    preview[n] = 0;
    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (!f)
        return;
    std::fprintf(f, "TEXT %03d bmp=%p %dx%d rect=%d,%d,%d,%d align=%d \"%s\" len=%u "
                    "req_size=%d name=\"%s\" bold=%d italic=%d outline=%d shadow=%d color=%u,%u,%u,%u out=%u,%u,%u,%u | "
                    "%s file=%s ppem=%d font_h=%d text_size=%dx%d surf=%dx%d squeeze=%.3f dest=%d,%d,%d,%d | "
                    "legacy_atlas=%dx40\n",
                 vitaTextLogIndex++, (void *)b, b->width(), b->height(), x, y, w, h, align, preview,
                 (unsigned)std::strlen(text), spec.size, spec.names.empty() ? "" : spec.names[0].c_str(),
                 spec.bold, spec.italic, spec.outline, spec.shadow,
                 spec.color[0], spec.color[1], spec.color[2], spec.color[3],
                 spec.outColor[0], spec.outColor[1], spec.outColor[2], spec.outColor[3],
                 ttf ? "ttf" : "FALLBACK_ATLAS24", info.fontFile.empty() ? "-" : info.fontFile.c_str(),
                 info.ppem, info.fontHeight, info.measuredW, info.measuredH, info.surfW, info.surfH,
                 info.squeeze, info.destX, info.destY, info.destW, info.destH,
                 (int)std::strlen(text) * 15);
    std::fclose(f);
}
#endif

#ifdef MKXP_VITA_AUDIT_FIXES
/* Fix (MKXP_VITA_AUDIT_FIXES): guarded as upstream (disposed bitmap, out of memory: Ruby exceptions). */
RB_METHOD_GUARD(bitmapDrawText)
#else
RB_METHOD(bitmapDrawText)
#endif
{
    Bitmap *bitmap =
        getPrivateData<Bitmap>(self);

    /*
     * RGSS: draw_text(rect, str, align = 0)
     *       draw_text(x, y, width, height, str, align = 0)
     */
    int x, y, w, h;
    VALUE textObj;
    int align = 0;

    if (argc == 2 || argc == 3)
    {
        VALUE rectObj = argv[0];

        x = NUM2INT(rb_funcall(rectObj, rb_intern("x"), 0));
        y = NUM2INT(rb_funcall(rectObj, rb_intern("y"), 0));
        w = NUM2INT(rb_funcall(rectObj, rb_intern("width"), 0));
        h = NUM2INT(rb_funcall(rectObj, rb_intern("height"), 0));

        textObj = argv[1];

        if (argc == 3)
            align = NUM2INT(argv[2]);
    }
    else if (argc == 5 || argc == 6)
    {
        x = NUM2INT(argv[0]);
        y = NUM2INT(argv[1]);
        w = NUM2INT(argv[2]);
        h = NUM2INT(argv[3]);

        textObj = argv[4];

        if (argc == 6)
            align = NUM2INT(argv[5]);
    }
    else
    {
        rb_raise(rb_eArgError,
                 "wrong number of arguments (%d for 2..3 or 5..6)", argc);
    }

    /* Come in RGSS2/3: il testo può essere qualsiasi oggetto (to_s). */
    textObj = rb_obj_as_string(textObj);

    VALUE fontObj =
        rb_iv_get(
            self,
            "@font"
        );

    VALUE fontSizeVal =
        rb_funcall(
            fontObj,
            rb_intern("size"),
            0
        );

    VitaFontSpec spec;
    vitaFontSpecFromRuby(fontObj, spec);
    VitaTextInfo info;
    const char *text = StringValueCStr(textObj);

    /* TTF renderer with the Bitmap's Font; the fixed 24 px atlas only if no TTF is installed. */
    const bool ttf = bitmap->vitaDrawTextFont(x, y, w, h, text, align, spec, &info);
    if (!ttf)
        bitmap->vitaDrawText(
            x,
            y,
            w,
            h,
            text,
            align,
            NUM2INT(fontSizeVal)
        );

#ifdef MKXP_VITA_DEBUG_TEXT_LOG
    vitaTextLog(bitmap, x, y, w, h, text, align, spec, info, ttf);
#endif
#ifdef MKXP_VITA_DEBUG_BITMAP_WINDOW
    {
        char extra[48];
        size_t n = 0;
        extra[n++] = '"';
        for (const char *c = text; *c && n < sizeof(extra) - 3; ++c)
            extra[n++] = ((unsigned char)*c < 32 || *c == '"') ? '_' : *c;
        extra[n++] = '"';
        extra[n] = 0;
        vitaBmpLog("DRAW_TEXT", bitmap, x, y, w, h, extra);
    }
#endif

    return self;
}
#ifdef MKXP_VITA_AUDIT_FIXES
RB_METHOD_GUARD_END
#endif

#ifdef MKXP_VITA_FONT_V2
/* Font.exist?(name): true if the TTF resolver finds a file for that family name. */
static VALUE vitaFontExist(VALUE self, VALUE name)
{
    (void)self;
    VALUE n = rb_obj_as_string(name);
    return VitaFont::familyExists(StringValueCStr(n)) ? Qtrue : Qfalse;
}
#endif

/* RGSS: text_size(str) -> Rect, with the same font/metrics as draw_text. */
RB_METHOD_GUARD(bitmapVitaTextSize)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    VITA_DIAG_OP(VD_TEXT_SIZE);

    VALUE strObj;
    rb_get_args(argc, argv, "o", &strObj RB_ARG_END);
    strObj = rb_obj_as_string(strObj);

    (void)b->rect();   /* raises on a disposed bitmap, like upstream */

    VitaFontSpec spec;
    vitaFontSpecFromRuby(rb_iv_get(self, "@font"), spec);

    int w = 0, h = 0;
    if (!VitaFont::textSize(spec, StringValueCStr(strObj), w, h)) {
        /* No TTF: the legacy estimate of the removed Ruby stub (15 px per character). */
        w = (int)NUM2LONG(rb_str_length(strObj)) * 15;
        h = spec.size;
    }

    Rect *r = new Rect(IntRect(0, 0, w, h));
    return wrapObject(r, RectType);
}
RB_METHOD_GUARD_END


/* RGSS: stretch_blt(dest_rect, src_bitmap, src_rect[, opacity]) (as mkxp-z bitmap-binding.cpp). */
RB_METHOD_GUARD(bitmapVitaStretchBlt)
{
    Bitmap *b = getPrivateData<Bitmap>(self);

    VALUE destRectObj;
    VALUE srcObj;
    VALUE srcRectObj;
    int opacity = 255;

    rb_get_args(argc, argv, "ooo|i", &destRectObj, &srcObj, &srcRectObj,
                &opacity RB_ARG_END);

    Bitmap *src = getPrivateDataCheck<Bitmap>(srcObj, BitmapType);
    if (src) {
        Rect *destRect = getPrivateDataCheck<Rect>(destRectObj, RectType);
        Rect *srcRect = getPrivateDataCheck<Rect>(srcRectObj, RectType);

        b->stretchBlt(destRect->toIntRect(), *src, srcRect->toIntRect(), opacity);
#ifdef MKXP_VITA_DEBUG_BITMAP_WINDOW
        const IntRect d = destRect->toIntRect();
        vitaBmpLog("STRETCH_BLT", b, d.x, d.y, d.w, d.h, nullptr);
#endif
    }

    return self;
}
RB_METHOD_GUARD_END

/* RGSS: radial_blur(angle, division) */
RB_METHOD_GUARD(bitmapVitaRadialBlur)
{
    Bitmap *b = getPrivateData<Bitmap>(self);

    int angle, divisions;
    rb_get_args(argc, argv, "ii", &angle, &divisions RB_ARG_END);

    b->radialBlur(angle, divisions);

    return Qnil;
}
RB_METHOD_GUARD_END

#ifdef MKXP_VITA_SCREEN_FX
/* RGSS: blur (mkxp-z: 3-tap horizontal + vertical mean). */
RB_METHOD_GUARD(bitmapVitaBlur)
{
    RB_UNUSED_PARAM;
    Bitmap *b = getPrivateData<Bitmap>(self);
    b->blur();
    return self;
}
RB_METHOD_GUARD_END

#ifdef MKXP_VITA_GFX_V2
#include "gl-util.h"
/* sharedstate-vita.cpp: Graphics.transition's map. vita_fx_trans_map(bitmap_or_nil, vague). */
extern "C" void vitaFxSetTransMap(GLuint tex, int vague);
static VALUE vitaFxTransMap(VALUE self, VALUE bmp, VALUE vague)
{
    (void)self;
    if (NIL_P(bmp)) {
        vitaFxSetTransMap(0, 0);
        return Qnil;
    }
    Bitmap *b = getPrivateData<Bitmap>(bmp);
    int v = NUM2INT(vague);
    v = v < 1 ? 1 : (v > 256 ? 256 : v);   /* upstream clamp(vague, 1, 256) */
    vitaFxSetTransMap(b->getGLTypes().tex.gl, v);
    return Qnil;
}
#endif

/* sharedstate-vita.cpp: last composited frame (screen size RGBA8, row 0 = top), after glFinish. */
extern "C" bool vitaFxSnapPixels(const unsigned char **px, int *strideBytes);
#include "vita_screen.h"

/* Graphics.snap_to_bitmap helper: vita_graphics_snap(bitmap) copies the last frame into it. */
static VALUE vitaGraphicsSnap(VALUE self, VALUE bmp)
{
    (void)self;
    Bitmap *b = getPrivateData<Bitmap>(bmp);
    const unsigned char *px = nullptr;
    int stride = 0;
    if (b && b->width() == VITA_SCREEN_W && b->height() == VITA_SCREEN_H && vitaFxSnapPixels(&px, &stride))
        b->vitaSetPixelsRGBA(px, stride);
    return bmp;
}
#endif

#ifdef MKXP_VITA_RGSS_COMPAT
/* RGSS: get_pixel(x, y) -> Color, set_pixel(x, y, color), hue_change(hue) (as mkxp-z bitmap-binding.cpp). */
/* RGSS: clone / dup (as mkxp-z bitmapInitializeCopy); the Font is duplicated, not shared. */
RB_METHOD_GUARD(bitmapVitaInitializeCopy)
{
    rb_check_argc(argc, 1);
    VALUE origObj = argv[0];
    if (!OBJ_INIT_COPY(self, origObj))
        return self;
    Bitmap *orig = getPrivateData<Bitmap>(origObj);
    Bitmap *b = new Bitmap(*orig);
    setPrivateData(self, b);
    bitmapInitProps(b, self);
    VALUE origFont = rb_iv_get(origObj, "@font");
    if (!NIL_P(origFont))
        rb_iv_set(self, "@font", rb_funcall(origFont, rb_intern("dup"), 0));
    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(bitmapVitaGetPixel)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    int x, y;
    rb_get_args(argc, argv, "ii", &x, &y RB_ARG_END);
    Color *color = new Color(b->getPixel(x, y));
    return wrapObject(color, ColorType);
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(bitmapVitaSetPixel)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    int x, y;
    VALUE colorObj;
    rb_get_args(argc, argv, "iio", &x, &y, &colorObj RB_ARG_END);
    Color *color = getPrivateDataCheck<Color>(colorObj, ColorType);
    b->setPixel(x, y, *color);
    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(bitmapVitaHueChange)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    int hue;
    rb_get_args(argc, argv, "i", &hue RB_ARG_END);
    b->hueChange(hue);
    return self;
}
RB_METHOD_GUARD_END
#endif

#ifdef MKXP_VITA_BITMAP_WINDOW_OPS
/* Window primitives (MKXP_VITA_BITMAP_WINDOW_OPS): argument forms as mkxp-z bitmap-binding.cpp. */
RB_METHOD_GUARD(bitmapVitaBlt)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    int x, y;
    VALUE srcObj, srcRectObj;
    int opacity = 255;
    rb_get_args(argc, argv, "iioo|i", &x, &y, &srcObj, &srcRectObj, &opacity RB_ARG_END);
    Bitmap *src = getPrivateDataCheck<Bitmap>(srcObj, BitmapType);
    if (src) {
        Rect *srcRect = getPrivateDataCheck<Rect>(srcRectObj, RectType);
        const IntRect r = srcRect->toIntRect();
        b->blt(x, y, *src, r, opacity);
        VITA_BMP_LOG("BLT", b, x, y, std::abs(r.w), std::abs(r.h), nullptr);
    }
    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(bitmapVitaFillRect)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    VALUE colorObj;
    IntRect r;
    if (argc == 2) {
        VALUE rectObj;
        rb_get_args(argc, argv, "oo", &rectObj, &colorObj RB_ARG_END);
        r = getPrivateDataCheck<Rect>(rectObj, RectType)->toIntRect();
    } else {
        rb_get_args(argc, argv, "iiiio", &r.x, &r.y, &r.w, &r.h, &colorObj RB_ARG_END);
    }
    Color *color = getPrivateDataCheck<Color>(colorObj, ColorType);
    b->fillRect(r, color->norm);
    VITA_BMP_LOG("FILL_RECT", b, r.x, r.y, r.w, r.h, nullptr);
    return self;
}
RB_METHOD_GUARD_END

#ifdef MKXP_VITA_SKY_SNAP
/* Diagnostics (MKXP_VITA_SKY_SNAP): Bitmap#vita_save_png(path) -> true / false. */
RB_METHOD_GUARD(bitmapVitaSavePng)
{
    rb_check_argc(argc, 1);
    Bitmap *b = getPrivateData<Bitmap>(self);
    VALUE path = argv[0];
    return b->vitaSavePng(StringValueCStr(path)) ? Qtrue : Qfalse;
}
RB_METHOD_GUARD_END
#endif

RB_METHOD_GUARD(bitmapVitaGradientFillRect)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    VALUE color1Obj, color2Obj;
    bool vertical = false;
    IntRect r;
    if (argc == 3 || argc == 4) {
        VALUE rectObj;
        rb_get_args(argc, argv, "ooo|b", &rectObj, &color1Obj, &color2Obj, &vertical RB_ARG_END);
        r = getPrivateDataCheck<Rect>(rectObj, RectType)->toIntRect();
    } else {
        rb_get_args(argc, argv, "iiiioo|b", &r.x, &r.y, &r.w, &r.h, &color1Obj, &color2Obj, &vertical RB_ARG_END);
    }
    Color *color1 = getPrivateDataCheck<Color>(color1Obj, ColorType);
    Color *color2 = getPrivateDataCheck<Color>(color2Obj, ColorType);
    b->gradientFillRect(r, color1->norm, color2->norm, vertical);
    VITA_BMP_LOG("GRADIENT", b, r.x, r.y, r.w, r.h, vertical ? "vertical" : "horizontal");
    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(bitmapVitaClearRect)
{
    Bitmap *b = getPrivateData<Bitmap>(self);
    IntRect r;
    if (argc == 1) {
        VALUE rectObj;
        rb_get_args(argc, argv, "o", &rectObj RB_ARG_END);
        r = getPrivateDataCheck<Rect>(rectObj, RectType)->toIntRect();
    } else {
        rb_get_args(argc, argv, "iiii", &r.x, &r.y, &r.w, &r.h RB_ARG_END);
    }
    b->clearRect(r);
    VITA_BMP_LOG("CLEAR_RECT", b, r.x, r.y, r.w, r.h, nullptr);
    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(bitmapVitaClear)
{
    RB_UNUSED_PARAM;
    Bitmap *b = getPrivateData<Bitmap>(self);
    b->clear();
    VITA_BMP_LOG("CLEAR", b, 0, 0, b->width(), b->height(), nullptr);
    return self;
}
RB_METHOD_GUARD_END
#endif

void bitmapBindingInitVitaMinimal()
{
    VALUE klass =
        rb_define_class(
            "Bitmap",
            rb_cObject
        );

    rb_define_alloc_func(
        klass,
        classAllocate<&BitmapType>
    );

    disposableBindingInit<Bitmap>(klass);

    _rb_define_method(
        klass,
        "initialize",
        bitmapVitaInitialize
    );

    _rb_define_method(
        klass,
        "width",
        bitmapVitaWidth
    );

    _rb_define_method(
        klass,
        "height",
        bitmapVitaHeight
    );
    _rb_define_method(
        klass,
        "rect",
        bitmapVitaRect
    );

rb_define_method(
    klass,
    "draw_text",
    RUBY_METHOD_FUNC(bitmapDrawText),
    -1
);

    _rb_define_method(klass, "stretch_blt", bitmapVitaStretchBlt);
    _rb_define_method(klass, "text_size", bitmapVitaTextSize);
#ifdef MKXP_VITA_FONT_V2
    rb_define_global_function("vita_font_exist", RUBY_METHOD_FUNC(vitaFontExist), 1);
#endif
#ifdef MKXP_VITA_BITMAP_WINDOW_OPS
    _rb_define_method(klass, "blt", bitmapVitaBlt);
    _rb_define_method(klass, "fill_rect", bitmapVitaFillRect);
    _rb_define_method(klass, "gradient_fill_rect", bitmapVitaGradientFillRect);
#ifdef MKXP_VITA_SKY_SNAP
    _rb_define_method(klass, "vita_save_png", bitmapVitaSavePng);
#endif
    _rb_define_method(klass, "clear_rect", bitmapVitaClearRect);
    _rb_define_method(klass, "clear", bitmapVitaClear);
#endif
#ifdef MKXP_VITA_DEBUG_BITMAP_WINDOW
    rb_define_global_function("vita_bitmap_log_arm", RUBY_METHOD_FUNC(vitaBmpLogArm), 1);
#endif
#ifdef MKXP_VITA_DEBUG_TEXT_LOG
    rb_define_global_function("vita_text_log_arm", RUBY_METHOD_FUNC(vitaTextLogArm), 1);
#endif
    _rb_define_method(klass, "radial_blur", bitmapVitaRadialBlur);
#ifdef MKXP_VITA_RGSS_COMPAT
    _rb_define_method(klass, "initialize_copy", bitmapVitaInitializeCopy);
    _rb_define_method(klass, "get_pixel", bitmapVitaGetPixel);
    _rb_define_method(klass, "set_pixel", bitmapVitaSetPixel);
    _rb_define_method(klass, "hue_change", bitmapVitaHueChange);
#endif
#ifdef MKXP_VITA_SCREEN_FX
    _rb_define_method(klass, "blur", bitmapVitaBlur);
    rb_define_global_function("vita_graphics_snap", RUBY_METHOD_FUNC(vitaGraphicsSnap), 1);
#ifdef MKXP_VITA_GFX_V2
    rb_define_global_function("vita_fx_trans_map", RUBY_METHOD_FUNC(vitaFxTransMap), 2);
#endif
#endif
}
