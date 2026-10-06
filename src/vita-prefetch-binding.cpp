/* Ruby binding of the battle animation read-ahead (MKXP_VITA_ANIM_PREFETCH), see vita-prefetch.cpp. */
#include <ruby.h>
#include <string>
#include <sys/stat.h>

#include "vita-image-cache.h"
#include "vita-prefetch.h"
#ifdef MKXP_VITA_ANIM_PREDECODE
#include "vita-predecode.h"
#endif
#include "vita_paths.h"
#ifdef MKXP_VITA_FS_INDEX
#include "vita-fs-index.h"
#endif

extern "C" void vitaImgCacheInitC(void);   /* bitmap-vita-minimal.cpp */

/*
 * Ruby side of the battle animation read-ahead (vita-prefetch.cpp): vita_prefetch_image("Graphics/
 * Animations/Name") resolves the file the way Bitmap.new does (the name as given, then + ".png")
 * and queues what the loader will read: the decoded-image cache entry (large images) or, when the
 * background thread finds no entry, the PNG. Returns true if something was queued. vita_prefetch_clear drops the rest.
 */
static bool vitaFileExists(const std::string &path)
{
#ifdef MKXP_VITA_FS_INDEX
    const int q = vitaFsIndexQuery(path.c_str());
    if (q >= 0) return q == 1;
#endif
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
static VALUE rbPrefetchImage(VALUE self, VALUE rel)
{
    (void)self;
    const std::string base = std::string(VITA_GAME_ROOT) + StringValueCStr(rel);
    std::string src;
    if (vitaFileExists(base)) src = base;
    else if (vitaFileExists(base + ".png")) src = base + ".png";
    else return Qfalse;
    vitaImgCacheInitC();
    /* The cache entry if it exists (the background thread finds out), else the image itself. */
    const std::string entry = VitaImageCache::entryPathFor(src);
    if (entry.empty()) vitaPrefetchAdd(src);
    else vitaPrefetchAdd(entry, src);
    return Qtrue;
}
static VALUE rbPrefetchClear(VALUE self)
{
    (void)self;
    vitaPrefetchClear();
    return Qnil;
}
#ifdef MKXP_VITA_ANIM_PREDECODE
/* vita_predecode_image("Graphics/Animations/Name"): resolved as Bitmap.new does, decoded ahead
 * (vita-predecode.cpp; falls back to the read-ahead above when there is no cache entry). */
static VALUE rbPredecodeImage(VALUE self, VALUE rel)
{
    (void)self;
    const std::string base = std::string(VITA_GAME_ROOT) + StringValueCStr(rel);
    std::string src;
    if (vitaFileExists(base)) src = base;
    else if (vitaFileExists(base + ".png")) src = base + ".png";
    else return Qfalse;
    vitaImgCacheInitC();
    vitaPredecodeImage(src);
    return Qtrue;
}
/* vita_predecode_sound("Audio/SE/Name"): the key SoundEmitter::allocateBuffer receives. */
static VALUE rbPredecodeSound(VALUE self, VALUE name)
{
    (void)self;
    vitaPredecodeSound(StringValueCStr(name));
    return Qtrue;
}
static VALUE rbPredecodeClear(VALUE self)
{
    (void)self;
    vitaPredecodeClear();
    return Qnil;
}
#endif
extern "C" void vitaAnimPrefetchInit(void)
{
#ifdef MKXP_VITA_ANIM_PREDECODE
    rb_define_global_function("vita_predecode_image", RUBY_METHOD_FUNC(rbPredecodeImage), 1);
    rb_define_global_function("vita_predecode_sound", RUBY_METHOD_FUNC(rbPredecodeSound), 1);
    rb_define_global_function("vita_predecode_clear", RUBY_METHOD_FUNC(rbPredecodeClear), 0);
#endif
    rb_define_global_function("vita_prefetch_image", RUBY_METHOD_FUNC(rbPrefetchImage), 1);
    rb_define_global_function("vita_prefetch_clear", RUBY_METHOD_FUNC(rbPrefetchClear), 0);
}
