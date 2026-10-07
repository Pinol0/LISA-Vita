#include <algorithm>
#include "vita_paths.h"
#ifdef MKXP_VITA_FS_INDEX
#include "vita-fs-index.h"
#endif
#include "bitmap.h"
#include "sharedstate.h"
#include "texpool.h"
#include "gl-util.h"
#include "glstate.h"
#include "shader.h"

#include <png.h>
#ifdef MKXP_VITA_BITMAP_LOAD_V2
#include "exception.h"
#include <cstdio>
#endif
#include <string>
#include <vector>
#include <cstring>

#include "bitmap-vita-cpu.h"
#include "vita-font.h"
/* Diagnostic only (MKXP_VITA_TIMELINE / MKXP_VITA_PERF_BITMAP): no code when OFF. */
#include "vita_diag.h"
#ifdef MKXP_VITA_IMG_DISK_CACHE
#include "vita-image-cache.h"
#include <new>
#if (defined(MKXP_VITA_HUE_PAGING) || defined(MKXP_VITA_CPU_PAGING)) && !defined(MKXP_VITA_TEX_PAGING)
#error "MKXP_VITA_HUE_PAGING / MKXP_VITA_CPU_PAGING need MKXP_VITA_TEX_PAGING"
#endif
#ifdef MKXP_VITA_LOAD_OOM_RETRY
#if !defined(MKXP_VITA_TEX_PAGING) || !defined(MKXP_VITA_BITMAP_LOAD_V2) || !defined(MKXP_VITA_PNG_DIRECT)
#error "MKXP_VITA_LOAD_OOM_RETRY needs MKXP_VITA_TEX_PAGING, MKXP_VITA_BITMAP_LOAD_V2 and MKXP_VITA_PNG_DIRECT"
#endif
#include <sys/stat.h>
extern "C" void rb_gc_start(void);   /* Ruby: full GC (the Bitmap is built inside a Ruby call) */
#endif
#if defined(MKXP_VITA_TEX_DIRECT_CACHE) && (!defined(MKXP_VITA_IMG_DISK_CACHE) || !defined(MKXP_VITA_PNG_DIRECT) || !defined(MKXP_VITA_BITMAP_NO_FBO))
#error "MKXP_VITA_TEX_DIRECT_CACHE needs MKXP_VITA_IMG_DISK_CACHE, MKXP_VITA_PNG_DIRECT and MKXP_VITA_BITMAP_NO_FBO"
#endif
static void vitaImgCacheInit()
{
    static bool done = false;
    if (!done) {
        VitaImageCache::setDir(VITA_GAME_ROOT "cache/");
        done = true;
    }
}
#ifdef MKXP_VITA_ANIM_PREFETCH
/* vita-prefetch-binding.cpp: the cache folder must be set before VitaImageCache::entryPathFor. */
extern "C" void vitaImgCacheInitC(void) { vitaImgCacheInit(); }
#endif
/* PERF (vita_diag.cpp): disk cache hits / misses / stores / failed stores. */
extern "C" void vitaDiagImgCacheStats(unsigned *h, unsigned *m, unsigned *s, unsigned *f)
{
    VitaImageCache::stats(h, m, s, f);
}
#endif

#ifdef MKXP_VITA_BITMAP_NO_FBO
/*
 * Fix (Vita): Bitmap textures without a framebuffer object. vitaGL has a fixed pool of 256 FBOs
 * (BUFFERS_NUM) and every TexPool object carries one, also while cached after release; after the
 * first battle the pool was exhausted (FBO_GEN_FAILED live=256) and the new tile atlas got no FBO
 * (empty map). This backend never renders into a Bitmap: the CPU copy is uploaded instead.
 * These textures are not cached in the TexPool (release() deletes objects without FBO).
 */
static TEXFBO vitaBitmapTexRequest(int width, int height)
{
    TEXFBO obj;
    obj.tex = TEX::gen();
    TEX::bind(obj.tex);
    TEX::setRepeat(false);
    TEX::setSmooth(false);
    TEX::allocEmpty(width, height);
    obj.width = width;
    obj.height = height;
    return obj;
}
#define VITA_BITMAP_TEX_REQUEST(w, h) vitaBitmapTexRequest((w), (h))
#else
#define VITA_BITMAP_TEX_REQUEST(w, h) shState->texPool().request((w), (h))
#endif

#ifdef MKXP_VITA_PNG_DIRECT
#include <vitaGL.h>   /* vglGetTexDataPointer */
#endif

struct BitmapPrivate
{
    TEXFBO gl;
    Bitmap *selfHires;

    std::vector<unsigned char> pixels;
    bool hasCpuPixels;

    /* File the texture was loaded from (empty for Bitmap.new(w, h) and the missing-file
     * fallback): its pixels are decoded again only when a CPU operation needs them. */
    std::string sourcePath;

#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
    /* CPU copy changed since the last upload (MKXP_VITA_BITMAP_DEFERRED_UPLOAD). */
    bool uploadPending = false;
#endif

#ifdef MKXP_VITA_PERF_BITMAP
    /* Live-bitmap list for the PERF memory fields (bmp_live / bmp_cpu_kb). */
    BitmapPrivate *diagPrev = nullptr, *diagNext = nullptr;
    static BitmapPrivate *diagHead;
#endif
#ifdef MKXP_VITA_TEX_PAGING
    /* Texture paging (MKXP_VITA_TEX_PAGING): pixels still equal the file at sourcePath (nothing has
     * written to this bitmap since it was loaded); texture given back to vitaGL; last frame the
     * texture or the CPU copy was used; frame of the last failed restore (retry backoff). */
    bool fileClean = false;
    bool evicted = false;
    unsigned lastUse = 0;
    unsigned pgFailFrame = 0;
    BitmapPrivate *pgPrev = nullptr, *pgNext = nullptr;
    static BitmapPrivate *pgHead;
#endif
#ifdef MKXP_VITA_HUE_PAGING
    /* With fileClean: the pixels are the file at sourcePath after hue_change(cleanHue) (0 = none). */
    int cleanHue = 0;
#endif

    BitmapPrivate()
        : selfHires(nullptr),
          hasCpuPixels(false)
    {
#ifdef MKXP_VITA_PERF_BITMAP
        diagNext = diagHead;
        if (diagHead)
            diagHead->diagPrev = this;
        diagHead = this;
#endif
#ifdef MKXP_VITA_TEX_PAGING
        pgNext = pgHead;
        if (pgHead)
            pgHead->pgPrev = this;
        pgHead = this;
#endif
    }

#if defined(MKXP_VITA_PERF_BITMAP) || defined(MKXP_VITA_TEX_PAGING)
    ~BitmapPrivate()
    {
#ifdef MKXP_VITA_PERF_BITMAP
        if (diagPrev)
            diagPrev->diagNext = diagNext;
        else
            diagHead = diagNext;
        if (diagNext)
            diagNext->diagPrev = diagPrev;
#endif
#ifdef MKXP_VITA_TEX_PAGING
        if (pgPrev)
            pgPrev->pgNext = pgNext;
        else
            pgHead = pgNext;
        if (pgNext)
            pgNext->pgPrev = pgPrev;
#endif
    }
#endif
};
#ifdef MKXP_VITA_TEX_PAGING
BitmapPrivate *BitmapPrivate::pgHead = nullptr;
#endif

#ifdef MKXP_VITA_PERF_BITMAP
BitmapPrivate *BitmapPrivate::diagHead = nullptr;

/* Main thread only (Bitmaps are created/destroyed there, PERF is flushed there). */
extern "C" void vitaDiagBitmapMem(int *live, uint64_t *cpuBytes, uint64_t *texBytes)
{
    int n = 0;
    uint64_t cpu = 0, tex = 0;
    for (BitmapPrivate *b = BitmapPrivate::diagHead; b; b = b->diagNext) {
        ++n;
        cpu += b->pixels.capacity();
#ifdef MKXP_VITA_TEX_PAGING
        if (b->evicted)
            continue;
#endif
        tex += (uint64_t)b->gl.width * b->gl.height * 4;
    }
    *live = n;
    *cpuBytes = cpu;
    *texBytes = tex;
}

/* GL ledger (MKXP_VITA_GL_LEDGER): texture names owned by the live Bitmaps. */
extern "C" void vitaDiagBitmapTexIds(void (*cb)(unsigned int tex, void *ctx), void *ctx)
{
    for (BitmapPrivate *b = BitmapPrivate::diagHead; b; b = b->diagNext)
        if (b->gl.tex.gl)
            cb(b->gl.tex.gl, ctx);
}
#endif

#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
static inline void vitaFlushUpload(BitmapPrivate *p);
#endif


/*
 * Bitmap vuoto reale.
 */
Bitmap::Bitmap(int width, int height, bool isHires)
{
    (void)isHires;

#ifdef MKXP_VITA_AUDIT_FIXES
    /* Fix (MKXP_VITA_AUDIT_FIXES): a negative size made width * height * 4 (int) negative, and a huge
     * one overflowed it (size_t is 32 bits here too): std::length_error / bad_alloc through the
     * binding's guard, which only catches mkxp Exceptions: abort. Now the RGSSError upstream raises.
     * A 0 size is still accepted (empty bitmap) as this backend always did: RGSS3/upstream raise for
     * it too, but text widths measured by this port's font renderer feed some sizes (ATS
     * resize_contents), so turning 0 into an error could stop the game where it works today. */
    if (width < 0 || height < 0 || (uint64_t)width * (uint64_t)height * 4 > (uint64_t)0x7fffffff)
        throw Exception(Exception::RGSSError, "failed to create bitmap");
#endif
    p = new BitmapPrivate();

    p->gl = VITA_BITMAP_TEX_REQUEST(
        width,
        height
    );
#ifdef MKXP_VITA_AUDIT_FIXES
p->pixels.resize(
    (size_t)width * height * 4,
    0
);
#else
p->pixels.resize(
    width * height * 4,
    0
);
#endif

p->hasCpuPixels = true;

TEX::bind(p->gl.tex);

#ifdef MKXP_VITA_DIAG
const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
TEX::uploadImage(
    width,
    height,
    p->pixels.data(),
    GL_RGBA
);
#ifdef MKXP_VITA_DIAG
vitaDiagSpan(VD_BMP_UPLOAD, vitaDiagT0, (uint64_t)width * height * 4, "Bitmap.new(w,h)");
#endif

FBO::unbind();
}


/*
 * TEMPORANEO:
 * Bitmap.new("Graphics/...") crea comunque una
 * vera texture GPU, ma 32x32.
 *
 * Il caricamento PNG arriverà dopo.
 */
#ifdef MKXP_VITA_PNG_CACHE
/*
 * Perf fix (MKXP_VITA_PNG_CACHE): decoded RGBA of recently loaded PNG files, LRU, 8 MiB budget,
 * images up to 2 MiB. Scripts dispose and reload the same files (draw_face: Cache.face + dispose on
 * every Window_BattleStatus refresh), which re-decoded Faces/Boys.png (~15 ms) every time.
 */
#include <list>
#include <unordered_map>
namespace {
struct VitaPngEntry { int w, h; std::vector<unsigned char> px; std::list<std::string>::iterator lru; };
std::unordered_map<std::string, VitaPngEntry> gPngCache;
std::list<std::string> gPngLru;
size_t gPngCacheBytes = 0;
const size_t kPngCacheMax = 8u << 20, kPngEntryMax = 2u << 20;

bool vitaPngCacheGet(const std::string &path, int &w, int &h, std::vector<unsigned char> &out)
{
    auto it = gPngCache.find(path);
    if (it == gPngCache.end())
        return false;
    gPngLru.splice(gPngLru.begin(), gPngLru, it->second.lru);
    w = it->second.w;
    h = it->second.h;
    out = it->second.px;
    return true;
}

#ifdef MKXP_VITA_PNG_DIRECT
/* Cache hit without copying the entry (d27: the copy was a 2 MiB transient in a fragmented heap). */
const std::vector<unsigned char> *vitaPngCacheFind(const std::string &path, int &w, int &h)
{
    auto it = gPngCache.find(path);
    if (it == gPngCache.end())
        return nullptr;
    gPngLru.splice(gPngLru.begin(), gPngLru, it->second.lru);
    w = it->second.w;
    h = it->second.h;
    return &it->second.px;
}
#endif

void vitaPngCachePut(const std::string &path, int w, int h, const std::vector<unsigned char> &px)
{
    if (px.size() > kPngEntryMax || gPngCache.count(path))
        return;
    while (gPngCacheBytes + px.size() > kPngCacheMax && !gPngLru.empty()) {
        auto old = gPngCache.find(gPngLru.back());
        gPngCacheBytes -= old->second.px.size();
        gPngCache.erase(old);
        gPngLru.pop_back();
    }
    gPngLru.push_front(path);
    VitaPngEntry &e = gPngCache[path];
    e.w = w; e.h = h; e.px = px; e.lru = gPngLru.begin();
    gPngCacheBytes += px.size();
}
} // namespace
#endif

#ifdef MKXP_VITA_PERF_BITMAP
/* PERF memory accounting (vita_diag.cpp): bytes held by the PNG cache (0 without it). */
extern "C" size_t vitaDiagPngCacheBytes()
{
#ifdef MKXP_VITA_PNG_CACHE
    return gPngCacheBytes;
#else
    return 0;
#endif
}

#endif
#ifdef MKXP_VITA_LOAD_OOM_RETRY
/* MKXP_VITA_LOAD_OOM_RETRY: vitaGL could not allocate the texture's memory (glTexImage2D out of
 * memory leaves it without data): a failed load, not an empty image. The texture is bound. */
static bool vitaUploadOk(TEXFBO &t)
{
    if (vglGetTexDataPointer(GL_TEXTURE_2D))
        return true;
    TEX::del(t.tex);
    t = TEXFBO();
    FBO::unbind();
    return false;
}
#endif

static bool vitaLoadPngToBitmap(
    const std::string &path,
    TEXFBO &out
)
{
#if defined(MKXP_VITA_PNG_CACHE) && defined(MKXP_VITA_PNG_DIRECT)
    {
        int cw = 0, ch = 0;
        if (const std::vector<unsigned char> *cpx = vitaPngCacheFind(path, cw, ch)) {
            out = VITA_BITMAP_TEX_REQUEST(cw, ch);
            TEX::bind(out.tex);
            TEX::uploadImage(cw, ch, cpx->data(), GL_RGBA);
#ifdef MKXP_VITA_LOAD_OOM_RETRY
            if (!vitaUploadOk(out))
                return false;
#endif
            FBO::unbind();
            return true;
        }
    }
#elif defined(MKXP_VITA_PNG_CACHE)
    {
        int cw = 0, ch = 0;
        std::vector<unsigned char> cpx;
        if (vitaPngCacheGet(path, cw, ch, cpx)) {
            out = VITA_BITMAP_TEX_REQUEST(cw, ch);
            TEX::bind(out.tex);
            TEX::uploadImage(cw, ch, cpx.data(), GL_RGBA);
            FBO::unbind();
            return true;
        }
    }
#endif
#if defined(MKXP_VITA_IMG_DISK_CACHE) && defined(MKXP_VITA_TEX_DIRECT_CACHE)
    vitaImgCacheInit();
    {
        /* Cache hit decompressed straight into the new texture (vita-image-cache.cpp loadInto): no
         * CPU buffer of the decoded size. */
#ifdef MKXP_VITA_DIAG
        const uint64_t vitaDcT0 = vitaDiagNow();
#endif
        struct Dst { TEXFBO t; bool made = false; } d;
        auto dstFn = [](void *ctx, int w, int h, size_t *stride) -> unsigned char * {
            Dst &dd = *static_cast<Dst *>(ctx);
            dd.t = VITA_BITMAP_TEX_REQUEST(w, h);   /* leaves the texture bound */
            dd.made = true;
            *stride = (size_t)((w + 7) & ~7) * 4;   /* vitaGL rows: VGL_ALIGN(w, 8) * 4, as PNG_DIRECT */
            return static_cast<unsigned char *>(vglGetTexDataPointer(GL_TEXTURE_2D));
        };
        if (VitaImageCache::loadInto(path, dstFn, &d)) {
#ifdef MKXP_VITA_DIAG
            vitaDiagSpan(VD_PNG_DECODE, vitaDcT0, (uint64_t)d.t.width * d.t.height * 4, "disk_cache_direct");
#endif
            out = d.t;
            FBO::unbind();
            return true;
        }
        if (d.made)
            TEX::del(d.t.tex);
    }
#elif defined(MKXP_VITA_IMG_DISK_CACHE)
    vitaImgCacheInit();
    {
#ifdef MKXP_VITA_DIAG
        const uint64_t vitaDcT0 = vitaDiagNow();
#endif
        int dw = 0, dh = 0;
        std::vector<unsigned char> dpx;
        if (VitaImageCache::load(path, dw, dh, dpx)) {
#ifdef MKXP_VITA_DIAG
            vitaDiagSpan(VD_PNG_DECODE, vitaDcT0, (uint64_t)dw * dh * 4, "disk_cache");
#endif
            out = VITA_BITMAP_TEX_REQUEST(dw, dh);
            TEX::bind(out.tex);
            TEX::uploadImage(dw, dh, dpx.data(), GL_RGBA);
            FBO::unbind();
            return true;
        }
    }
#endif
    png_image image;
    std::memset(&image, 0, sizeof(image));

    image.version = PNG_IMAGE_VERSION;

#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
#ifdef MKXP_VITA_BIG_READS
    /* Perf fix (MKXP_VITA_BIG_READS): the whole PNG in one large read, decoded from memory
     * (libpng's file reader goes through newlib stdio, 1 KiB per read on the Vita). */
    std::vector<unsigned char> vitaFileBuf;
    if (!VitaImageCache::readWholeFile(path, vitaFileBuf) ||
        !png_image_begin_read_from_memory(&image, vitaFileBuf.data(), vitaFileBuf.size()))
    {
        return false;
    }
#else
    if (!png_image_begin_read_from_file(
            &image,
            path.c_str()))
    {
        return false;
    }
#endif

    image.format = PNG_FORMAT_RGBA;

#ifdef MKXP_VITA_IMG_DISK_CACHE
    /* Large image, no cache entry yet: decode into a buffer (big-block pool), store the entry,
     * upload. Later loads take the branch above. */
    if ((size_t)image.width * image.height * 4 >= VitaImageCache::kMinBytes) {
        std::vector<unsigned char> px;
        bool haveBuf = true;
        try { px.resize(PNG_IMAGE_SIZE(image)); } catch (const std::bad_alloc &) { haveBuf = false; }
        if (haveBuf) {
            if (!png_image_finish_read(&image, nullptr, px.data(), 0, nullptr)) {
                png_image_free(&image);
                return false;
            }
#ifdef MKXP_VITA_DIAG
            vitaDiagSpan(VD_PNG_DECODE, vitaDiagT0, (uint64_t)image.width * image.height * 4, path.c_str());
#endif
            const int w = (int)image.width, h = (int)image.height;
            png_image_free(&image);
            VitaImageCache::store(path, w, h, px.data());
#ifdef MKXP_VITA_PNG_CACHE
            vitaPngCachePut(path, w, h, px);
#endif
            out = VITA_BITMAP_TEX_REQUEST(w, h);
            TEX::bind(out.tex);
            TEX::uploadImage(w, h, px.data(), GL_RGBA);
#ifdef MKXP_VITA_LOAD_OOM_RETRY
            if (!vitaUploadOk(out))
                return false;
#endif
            FBO::unbind();
            return true;
        }
    }
#endif

#if defined(MKXP_VITA_PNG_DIRECT) && defined(MKXP_VITA_BITMAP_NO_FBO)
    /*
     * Fix (MKXP_VITA_PNG_DIRECT): decode straight into the new texture's memory instead of a newlib
     * heap buffer of w*h*4 that was then copied by glTexImage2D. The large transient buffers (up to
     * 4.4 MiB for 960x1152 animation sheets) fragmented the newlib heap: d27 soak, heap used flat at
     * ~89 MiB while the arena grew 73 -> 140 MiB, then bad_alloc. vitaGL stores RGBA textures as rows
     * of VGL_ALIGN(w, 8) * 4 bytes, top to bottom, bytes as given (gpu_alloc_texture, fast_store).
     * The texture was just allocated (allocEmpty) and never used by the GPU.
     */
    {
        out = VITA_BITMAP_TEX_REQUEST(image.width, image.height);   /* leaves out.tex bound */
        unsigned char *dst = static_cast<unsigned char *>(vglGetTexDataPointer(GL_TEXTURE_2D));
        const png_int_32 stride = (png_int_32)(((image.width + 7u) & ~7u) * 4u);
        if (!dst || !png_image_finish_read(&image, nullptr, dst, stride, nullptr)) {
            png_image_free(&image);
            TEX::del(out.tex);
            out = TEXFBO();
            return false;
        }
#ifdef MKXP_VITA_DIAG
        vitaDiagSpan(VD_PNG_DECODE, vitaDiagT0, (uint64_t)image.width * image.height * 4, path.c_str());
#endif
#ifdef MKXP_VITA_PNG_CACHE
        if ((size_t)image.width * image.height * 4 <= kPngEntryMax) {
            std::vector<unsigned char> px((size_t)image.width * image.height * 4);
            for (png_uint_32 y = 0; y < image.height; ++y)
                std::memcpy(px.data() + (size_t)y * image.width * 4, dst + (size_t)y * stride, (size_t)image.width * 4);
            vitaPngCachePut(path, (int)image.width, (int)image.height, px);
        }
#endif
        FBO::unbind();
        png_image_free(&image);
        return true;
    }
#endif

    std::vector<unsigned char> pixels(
        PNG_IMAGE_SIZE(image)
    );

    if (!png_image_finish_read(
            &image,
            nullptr,
            pixels.data(),
            0,
            nullptr))
    {
        png_image_free(&image);
        return false;
    }
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_PNG_DECODE, vitaDiagT0, (uint64_t)image.width * image.height * 4, path.c_str());
#endif
#ifdef MKXP_VITA_PNG_CACHE
    vitaPngCachePut(path, (int)image.width, (int)image.height, pixels);
#endif

    out = VITA_BITMAP_TEX_REQUEST(
        image.width,
        image.height
    );

    TEX::bind(out.tex);

#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagU0 = vitaDiagNow();
#endif
    TEX::uploadImage(
        image.width,
        image.height,
        pixels.data(),
        GL_RGBA
    );
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_BMP_UPLOAD, vitaDiagU0, (uint64_t)image.width * image.height * 4, "png");
#endif

    FBO::unbind();

    png_image_free(&image);

    return true;
}

#ifdef MKXP_VITA_TEX_PAGING
#include <vitaGL.h>   /* vglMemFree */
#include <psp2/ctrl.h>
#ifndef MKXP_VITA_BITMAP_NO_FBO
#error "MKXP_VITA_TEX_PAGING needs MKXP_VITA_BITMAP_NO_FBO (bitmap textures without FBO)"
#endif
/*
 * Fix (MKXP_VITA_TEX_PAGING): RGSS3's Cache keeps every Bitmap it loaded (character sheets,
 * tilesets, faces, pictures, parallaxes, battle animations) until Cache.clear. d68: textures of
 * live Bitmaps grew 11 -> 90 MiB in 3 h; the vitaGL pools ran out, the 8 MiB tile atlas went to the
 * newlib heap (15x) or failed (7x: broken tilesets), and the heap ran out after it.
 * The texture of a Bitmap whose pixels still equal its PNG file (loaded from file, never written
 * since) can be given back and rebuilt from the file when it is needed again, with the same pixels.
 * Once per 30 frames, if vitaGL has less than kPgLow bytes free, such textures not used (drawn,
 * blitted, read on the CPU) for kPgIdle frames are released, least recently used first, until
 * kPgHigh would be free; a clean CPU copy goes too. Any write to a Bitmap clears its fileClean flag
 * for good. Restoring happens wherever this file hands the texture out (getGLTypes, bindTex,
 * uploads); the bound framebuffer and texture are kept, as vitaFlushUpload does mid-frame.
 * Test mode: L+R+DOWN toggles a stress setting (always under pressure, idle 180 frames, release every
 * candidate) to exercise eviction and restore everywhere; qa.log TEX_PAGING_STRESS ON/OFF.
 */
#ifdef MKXP_VITA_HUE_PAGING
#ifndef MKXP_VITA_PORT
#error "MKXP_VITA_HUE_PAGING needs MKXP_VITA_PORT"
#endif
static bool vitaDecodeSource(const BitmapPrivate *p, int &w, int &h, std::vector<unsigned char> &px);
#endif
namespace {
unsigned gPgFrame = 0;
bool gPgPressure = false;
bool gPgStress = false, gPgStressPrev = false;
unsigned gPgEvicted = 0, gPgRestored = 0, gPgRestoreFails = 0, gPgRounds = 0;
uint64_t gPgOutBytes = 0;
const size_t kPgLow = 40u << 20, kPgHigh = 56u << 20, kPgMinBytes = 64u << 10;
const unsigned kPgIdle = 600;

inline size_t vitaPgBytes(const BitmapPrivate *b) { return (size_t)b->gl.width * b->gl.height * 4; }

#ifdef MKXP_VITA_HUE_PAGING
inline void vitaPgDirty(BitmapPrivate *p) { p->fileClean = false; p->cleanHue = 0; }
#else
inline void vitaPgDirty(BitmapPrivate *p) { p->fileClean = false; }
#endif

bool vitaPgRestore(BitmapPrivate *p)
{
    if (p->pgFailFrame && gPgFrame - p->pgFailFrame < 60)
        return false;
    GLint prevFbo = 0, prevTex = 0;
    gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    const int w = p->gl.width, h = p->gl.height;
    TEXFBO t;
    bool ok;
    if (p->hasCpuPixels) {   /* written after the eviction: the CPU copy is the content */
        t = vitaBitmapTexRequest(w, h);
        TEX::uploadImage(w, h, p->pixels.data(), GL_RGBA);
        ok = t.tex != TEX::ID(0);
#ifdef MKXP_VITA_LOAD_OOM_RETRY
        if (ok && !vitaUploadOk(t))   /* no texture memory: a failed restore (backoff), not an empty one */
            ok = false;
#endif
#ifdef MKXP_VITA_HUE_PAGING
    } else if (p->cleanHue) {   /* hue clone: the file again, then the same hue_change */
        std::vector<unsigned char> px;
        int dw = 0, dh = 0;
        try {
            ok = vitaDecodeSource(p, dw, dh, px) && dw == w && dh == h;
        } catch (const std::bad_alloc &) {
            ok = false;
        }
        if (ok) {
            t = vitaBitmapTexRequest(w, h);
            TEX::uploadImage(w, h, px.data(), GL_RGBA);
            ok = t.tex != TEX::ID(0);
#ifdef MKXP_VITA_LOAD_OOM_RETRY
            if (ok && !vitaUploadOk(t))
                ok = false;
#endif
        }
#endif
    } else {
        ok = vitaLoadPngToBitmap(p->sourcePath, t);
        if (ok && (t.width != w || t.height != h)) {
            TEX::del(t.tex);
            ok = false;
        }
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (!ok) {
        ++gPgRestoreFails;
        p->pgFailFrame = gPgFrame ? gPgFrame : 1;
        gPgPressure = true;
        return false;
    }
    p->gl.tex = t.tex;
    p->evicted = false;
    p->pgFailFrame = 0;
    gPgOutBytes -= vitaPgBytes(p);
    ++gPgRestored;
    return true;
}

inline void vitaPgEnsure(BitmapPrivate *p)
{
    if (p->evicted)
        vitaPgRestore(p);
    p->lastUse = gPgFrame;
}

void vitaPgEvict(BitmapPrivate *b)
{
    TEXFBO t = b->gl;
    shState->texPool().release(t);   /* no FBO: deleted (deferred GL delete), never cached */
    b->gl.tex = TEX::ID(0);
    if (b->hasCpuPixels) {
        std::vector<unsigned char>().swap(b->pixels);
        b->hasCpuPixels = false;
    }
    b->evicted = true;
    gPgOutBytes += vitaPgBytes(b);
    ++gPgEvicted;
}

#ifdef MKXP_VITA_CPU_PAGING
/*
 * Fix (MKXP_VITA_CPU_PAGING): d91 crash after ~41 min / many battles: std::bad_alloc (abort) decoding
 * a 960x1152 animation sheet (4.3 MiB) with the big pool at 36.5/40 MiB used in 58 blocks and the
 * newlib heap at its 104 MiB limit. Texture paging only looks at vitaGL's free memory; the CPU copies
 * of clean bitmaps (hue_change clones of animation sheets, 4.3 MiB each, kept by Cache forever;
 * stretch_blt sources) were never given back while the textures had room.
 * A clean bitmap's CPU copy can always be decoded again (vitaEnsureCpuPixels), so here, at points
 * where no pixel pointer is held:
 *   - once per 30 frames (vitaTexPagingTick), big pool under kCpuLow free: copies idle for kCpuIdle
 *     frames go, least recently used first, until kCpuHigh would be free;
 *   - before a CPU copy is decoded or a bitmap cloned (vitaCpuMakeRoom): copies not used in this
 *     frame go until the new copy plus kCpuRoom would fit; on std::bad_alloc, every such copy goes
 *     and the decode is tried once more.
 * The bitmap being worked on is marked used in this frame (lastUse) before, so it is never chosen;
 * a copy with an upload pending is the texture's next content and stays.
 */
extern "C" void vitaBigAllocStats(unsigned *usedKb, unsigned *freeKb, unsigned *blocks, unsigned *fallbacks);
unsigned gCpuDropped = 0;
uint64_t gCpuDroppedBytes = 0;
const size_t kCpuLow = 12u << 20, kCpuHigh = 20u << 20, kCpuRoom = 8u << 20;
const unsigned kCpuIdle = 120;

size_t vitaBigFree()
{
    unsigned u = 0, f = 0, b = 0, fb = 0;
    vitaBigAllocStats(&u, &f, &b, &fb);
    return (size_t)f * 1024;
}

/* Drops CPU copies of clean bitmaps idle for `idle` frames or more until `want` bytes would be free. */
void vitaCpuRelease(size_t freeB, size_t want, unsigned idle)
{
    std::vector<BitmapPrivate *> c;
    for (BitmapPrivate *b = BitmapPrivate::pgHead; b; b = b->pgNext) {
        if (!b->fileClean || !b->hasCpuPixels || gPgFrame - b->lastUse < idle || b->pixels.capacity() < kPgMinBytes)
            continue;
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
        if (b->uploadPending)
            continue;
#endif
        c.push_back(b);
    }
    std::sort(c.begin(), c.end(), [](const BitmapPrivate *x, const BitmapPrivate *y) { return x->lastUse < y->lastUse; });
    for (BitmapPrivate *b : c) {
        if (freeB >= want)
            break;
        const size_t n = b->pixels.capacity();
        std::vector<unsigned char>().swap(b->pixels);
        b->hasCpuPixels = false;
        freeB += n;
        ++gCpuDropped;
        gCpuDroppedBytes += n;
    }
}

/* Room for a new CPU copy of `bytes` (main thread, no pixel pointer held). */
void vitaCpuMakeRoom(size_t bytes)
{
    const size_t freeB = vitaBigFree();
    if (freeB < bytes + kCpuRoom)
        vitaCpuRelease(freeB, bytes + kCpuRoom, 1);
}
#endif
#ifdef MKXP_VITA_LOAD_OOM_RETRY
/* Every texture paging could give back that was not used in this frame (MKXP_VITA_LOAD_OOM_RETRY). */
unsigned vitaPgEvictIdle()
{
    unsigned n = 0;
    for (BitmapPrivate *b = BitmapPrivate::pgHead; b; b = b->pgNext) {
        if (!b->fileClean || b->evicted || b->gl.tex == TEX::ID(0) || b->lastUse == gPgFrame || vitaPgBytes(b) < kPgMinBytes)
            continue;
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
        if (b->uploadPending)
            continue;
#endif
        vitaPgEvict(b);
        ++n;
    }
    return n;
}
#endif
} // namespace

/* main.cpp, once per Graphics.update before the frame is drawn (main thread). */
extern "C" void vitaTexPagingTick()
{
    ++gPgFrame;
    {
#ifdef MKXP_VITA_NO_DEBUG_KEYS
        const bool on = false;   /* public build: no stress-test key (MKXP_VITA_NO_DEBUG_KEYS) */
#else
        SceCtrlData pad;
        const unsigned kCombo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_DOWN;
        const bool on = sceCtrlPeekBufferPositive(0, &pad, 1) > 0 && (pad.buttons & kCombo) == kCombo;
#endif
        if (on && !gPgStressPrev) {
            gPgStress = !gPgStress;
            FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
            if (f) {
                std::fprintf(f, "TEX_PAGING_STRESS %s\n", gPgStress ? "ON" : "OFF");
                std::fclose(f);
            }
        }
        gPgStressPrev = on;
    }
    if (!gPgPressure && gPgFrame % 30)
        return;
    gPgPressure = false;
#ifdef MKXP_VITA_CPU_PAGING
    {
        const size_t bigFree = vitaBigFree();
        if (bigFree < kCpuLow)
            vitaCpuRelease(bigFree, kCpuHigh, kCpuIdle);
    }
#endif
    const size_t freeB = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM) + vglMemFree(VGL_MEM_PHYCONT);
    if (freeB >= kPgLow && !gPgStress)
        return;
    const unsigned idle = gPgStress ? 180u : kPgIdle;   /* > any transition: vitaFxSetTransMap keeps the texture id */
    std::vector<BitmapPrivate *> c;
    for (BitmapPrivate *b = BitmapPrivate::pgHead; b; b = b->pgNext) {
        if (!b->fileClean || b->evicted || b->gl.tex == TEX::ID(0) || gPgFrame - b->lastUse < idle ||
            vitaPgBytes(b) < kPgMinBytes)
            continue;
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
        if (b->uploadPending)
            continue;
#endif
        c.push_back(b);
    }
    std::sort(c.begin(), c.end(), [](const BitmapPrivate *x, const BitmapPrivate *y) { return x->lastUse < y->lastUse; });
    const size_t want = gPgStress ? (size_t)-1 : (freeB < kPgHigh ? kPgHigh - freeB : 0);
    size_t got = 0;
    unsigned n = 0;
    for (BitmapPrivate *b : c) {
        if (got >= want)
            break;
        got += vitaPgBytes(b);
        vitaPgEvict(b);
        ++n;
    }
    if (n && gPgRounds++ < (gPgStress ? 400u : 40u)) {
        FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
        if (f) {
            std::fprintf(f, "TEX_PAGING evicted=%u kb=%u free_kb_before=%u candidates=%u out_total_kb=%u\n", n,
                         (unsigned)(got / 1024), (unsigned)(freeB / 1024), (unsigned)c.size(), (unsigned)(gPgOutBytes / 1024));
            std::fclose(f);
        }
    }
}

#ifdef MKXP_VITA_CPU_PAGING
/* PERF_LITE (main.cpp). */
extern "C" void vitaCpuPagingStats(unsigned *dropped, unsigned *droppedKb)
{
    *dropped = gCpuDropped;
    *droppedKb = (unsigned)(gCpuDroppedBytes / 1024);
}
#endif

/* PERF (vita_diag.cpp). */
extern "C" void vitaTexPagingStats(unsigned *evicted, unsigned *restored, unsigned *fails, unsigned *outKb)
{
    *evicted = gPgEvicted;
    *restored = gPgRestored;
    *fails = gPgRestoreFails;
    *outKb = (unsigned)(gPgOutBytes / 1024);
}
#define VITA_PG_ENSURE(p) vitaPgEnsure(p)
#define VITA_PG_DIRTY(p) vitaPgDirty(p)
#else
#define VITA_PG_ENSURE(p) ((void)0)
#define VITA_PG_DIRTY(p) ((void)0)
#endif


Bitmap::Bitmap(const char *filename)
{
    p = new BitmapPrivate();

    std::string relativePath =
        filename ? filename : "";

    /*
     * Gli script RGSS usano percorsi relativi tipo:
     *
     * Graphics/Titles1/Dessert2 copy
     *
     * mentre noi abbiamo copiato le risorse sotto:
     *
     * <game root>/
     */
    std::string vitaPath =
        VITA_GAME_ROOT +
        relativePath;
#ifdef MKXP_VITA_PATCHES
    /* Images shipped in the package (app0:patches/, MKXP_VITA_PATCHES) are given with their full path. */
    if (relativePath.compare(0, 5, "app0:") == 0)
        vitaPath = relativePath;
#endif

    bool loaded = false;

    /*
     * Prima proviamo il filename esattamente
     * come ci arriva da Ruby.
     */
#ifdef MKXP_VITA_FS_INDEX
    /* A name the asset-folder index knows is missing would fail anyway (vita-fs-index.cpp): skip it. */
    if (vitaFsIndexQuery(vitaPath.c_str()) != 0)
#endif
    loaded = vitaLoadPngToBitmap(
        vitaPath,
        p->gl
    );

    if (loaded)
        p->sourcePath = vitaPath;
#ifdef MKXP_VITA_TEX_PAGING
    p->fileClean = loaded;
#endif

    /*
     * RPG Maker normalmente omette ".png".
     *
     * Quindi se il primo tentativo fallisce,
     * proviamo automaticamente con .png.
     */
    if (!loaded)
    {
#ifdef MKXP_VITA_FS_INDEX
        if (vitaFsIndexQuery((vitaPath + ".png").c_str()) != 0)
#endif
        loaded = vitaLoadPngToBitmap(
            vitaPath + ".png",
            p->gl
        );

        if (loaded)
            p->sourcePath = vitaPath + ".png";
#ifdef MKXP_VITA_TEX_PAGING
        p->fileClean = loaded;
#endif
    }

#ifdef MKXP_VITA_LOAD_OOM_RETRY
    /*
     * Fix (MKXP_VITA_LOAD_OOM_RETRY): d92, after ~48 min: RuntimeError "Graphics/Animations/Fire3" in
     * Cache.animation during a battle. The file exists; its load failed for lack of memory (the CPU
     * heaps were fragmented and full), and every load failure was reported as a missing file.
     * Ruby does the same for its own allocations (GC, then once more): an image that exists but did
     * not load gets one more try after the memory that can be given back is (clean CPU copies and
     * textures not used in this frame, garbage objects - Bitmaps no longer referenced - through a
     * full GC). A second failure raises an error that says so instead of a missing-file error.
     * qa.log LOAD_RETRY path=... ok=0/1 (first 32).
     */
    if (!loaded) {
        struct stat st;
        std::string existing;
        if (::stat(vitaPath.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            existing = vitaPath;
        else if (::stat((vitaPath + ".png").c_str(), &st) == 0 && S_ISREG(st.st_mode))
            existing = vitaPath + ".png";
        if (!existing.empty()) {
            unsigned cpu = 0, tex = 0;
#ifdef MKXP_VITA_CPU_PAGING
            cpu = gCpuDropped;
            vitaCpuRelease(0, (size_t)-1, 1);
            cpu = gCpuDropped - cpu;
#endif
            tex = vitaPgEvictIdle();
            rb_gc_start();
            loaded = vitaLoadPngToBitmap(existing, p->gl);
            if (loaded) {
                p->sourcePath = existing;
                p->fileClean = true;
            }
            static int logged = 0;
            if (logged < 32) {
                ++logged;
                FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
                if (f) {
                    std::fprintf(f, "LOAD_RETRY path=%s ok=%d cpu_dropped=%u tex_evicted=%u\n", relativePath.c_str(),
                                 loaded ? 1 : 0, cpu, tex);
                    std::fclose(f);
                }
            }
            if (!loaded) {
                delete p;
                p = nullptr;
                throw Exception(Exception::MKXPError, "%s: not enough memory to load the image", relativePath.c_str());
            }
        }
    }
#endif

#ifdef MKXP_VITA_BITMAP_LOAD_V2
    /*
     * As mkxp-z: a missing image raises (Exception::NoFileError -> Errno::ENOENT, message = the RGSS
     * path); an image in a format this backend cannot decode raises a clear MKXPError instead of
     * being replaced by a placeholder.
     */
    if (!loaded) {
        static const char *const otherExts[] = { ".jpg", ".jpeg", ".bmp" };
        std::string unsupported;
        for (const char *ext : otherExts) {
#ifdef MKXP_VITA_FS_INDEX
            if (vitaFsIndexQuery((vitaPath + ext).c_str()) == 0)
                continue;
#endif
            FILE *f = std::fopen((vitaPath + ext).c_str(), "rb");
            if (f) { std::fclose(f); unsupported = ext; break; }
        }
        delete p;
        p = nullptr;
        if (!unsupported.empty())
            throw Exception(Exception::MKXPError, "%s%s: image format not supported on Vita (PNG only)",
                            relativePath.c_str(), unsupported.c_str());
        throw Exception(Exception::NoFileError, "%s", relativePath.c_str());
    }
#endif
    /*
     * Fallback temporaneo.
     *
     * Se il file non viene trovato, manteniamo
     * il quadratino arancione invece di crashare.
     */
    if (!loaded)
    {
#ifdef MKXP_VITA_BITMAP_NO_FBO
        /* Same orange square, uploaded instead of cleared through an FBO; still GPU-only
         * (hasCpuPixels stays false, as before). */
        p->gl = vitaBitmapTexRequest(32, 32);
        std::vector<unsigned char> vitaOrange(32 * 32 * 4);
        for (size_t i = 0; i < vitaOrange.size(); i += 4) {
            vitaOrange[i] = 255; vitaOrange[i + 1] = 64; vitaOrange[i + 2] = 26; vitaOrange[i + 3] = 255;
        }
        TEX::bind(p->gl.tex);
        TEX::uploadImage(32, 32, vitaOrange.data(), GL_RGBA);
        FBO::unbind();
#else
        p->gl = shState->texPool().request(
            32,
            32
        );

        FBO::bind(p->gl.fbo);

        glState.viewport.set(
            IntRect(0, 0, 32, 32)
        );

        glState.clearColor.set(
            Vec4(
                1.0f,
                0.25f,
                0.10f,
                1.0f
            )
        );

        gl.Clear(
            GL_COLOR_BUFFER_BIT
        );

        FBO::unbind();
#endif
    }
}

Bitmap::~Bitmap()
{
    dispose();
}


int Bitmap::width() const
{
    guardDisposed();
    return p->gl.width;
}


int Bitmap::height() const
{
    guardDisposed();
    return p->gl.height;
}


IntRect Bitmap::rect() const
{
    guardDisposed();

    return IntRect(
        0,
        0,
        width(),
        height()
    );
}


bool Bitmap::hasHires() const
{
    guardDisposed();
    return false;
}


Bitmap *Bitmap::getHires() const
{
    guardDisposed();
    return nullptr;
}


bool Bitmap::isMega() const
{
    guardDisposed();
    return false;
}

void Bitmap::ensureNonMega() const
{
    /*
     * Come in bitmap.cpp: GUARD_MEGA lancia solo per le Mega Bitmap.
     * Il backend Vita minimale non ne crea mai (isMega() == false).
     */
    if (isDisposed())
        return;
}


TEXFBO &Bitmap::getGLTypes() const
{
    guardDisposed();
    VITA_PG_ENSURE(p);
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
    vitaFlushUpload(p);
#endif
    return p->gl;
}

#ifdef MKXP_VITA_PORT

static bool vitaLoadPngPixels(
    const char *path,
    int &width,
    int &height,
    std::vector<unsigned char> &pixels
)
{
#ifdef MKXP_VITA_PNG_CACHE
    if (vitaPngCacheGet(path, width, height, pixels))
        return true;
#endif
#ifdef MKXP_VITA_IMG_DISK_CACHE
    vitaImgCacheInit();
    if (VitaImageCache::load(path, width, height, pixels))
        return true;
#endif
    png_image image;
    std::memset(&image, 0, sizeof(image));

    image.version = PNG_IMAGE_VERSION;

#ifdef MKXP_VITA_BIG_READS
    std::vector<unsigned char> vitaFileBuf;   /* see vitaLoadPngToBitmap */
    if (!VitaImageCache::readWholeFile(path, vitaFileBuf) ||
        !png_image_begin_read_from_memory(&image, vitaFileBuf.data(), vitaFileBuf.size()))
    {
        return false;
    }
#else
    if (!png_image_begin_read_from_file(
            &image,
            path))
    {
        return false;
    }
#endif

    image.format = PNG_FORMAT_RGBA;

    pixels.resize(
        PNG_IMAGE_SIZE(image)
    );

    if (!png_image_finish_read(
            &image,
            nullptr,
            pixels.data(),
            0,
            nullptr))
    {
        png_image_free(&image);
        pixels.clear();
        return false;
    }

    width = static_cast<int>(image.width);
    height = static_cast<int>(image.height);

    png_image_free(&image);
#ifdef MKXP_VITA_IMG_DISK_CACHE
    VitaImageCache::store(path, width, height, pixels.data());   /* no-op under 1 MiB */
#endif
#ifdef MKXP_VITA_PNG_CACHE
    vitaPngCachePut(path, width, height, pixels);
#endif

    return true;
}

#ifdef MKXP_VITA_HUE_PAGING
/*
 * Fix (MKXP_VITA_HUE_PAGING): Cache.hue_changed_bitmap = normal_bitmap(path).clone + hue_change(hue),
 * kept by Cache for the whole session: 115 (sheet, hue) pairs in LISA's Animations, 4.3 MiB of CPU
 * copy + 4.3 MiB of texture each, neither of which paging could give back (written -> not clean).
 * A clone of a clean bitmap stays clean (same file); one hue_change of a clean bitmap without a hue
 * keeps it clean with the hue recorded: its pixels are the file's after the same hue_change (a
 * per-pixel function of the rgb: the same input gives the same pixels). Every other write, or a
 * second hue_change, makes it dirty as before. The file is decoded again through here.
 */
static bool vitaDecodeSource(const BitmapPrivate *p, int &w, int &h, std::vector<unsigned char> &px)
{
    if (!vitaLoadPngPixels(p->sourcePath.c_str(), w, h, px))
        return false;
    if (p->cleanHue)
        VitaBitmapCpu::hueChange(VitaBitmapCpu::Image{ w, h, px.data() }, p->cleanHue);
    return true;
}
#define VITA_DECODE_SOURCE(bp, w, h, px) vitaDecodeSource((bp), w, h, px)
#else
#define VITA_DECODE_SOURCE(bp, w, h, px) vitaLoadPngPixels((bp)->sourcePath.c_str(), w, h, px)
#endif
#ifdef MKXP_VITA_CPU_PAGING
/* VITA_DECODE_SOURCE; on std::bad_alloc the clean CPU copies not used in this frame go and it is
 * tried once more (a second failure throws as before). */
static bool vitaDecodeSourceRoom(const BitmapPrivate *bp, int &w, int &h, std::vector<unsigned char> &px)
{
    try {
        return VITA_DECODE_SOURCE(bp, w, h, px);
    } catch (const std::bad_alloc &) {
        vitaCpuRelease(0, (size_t)-1, 1);
        return VITA_DECODE_SOURCE(bp, w, h, px);
    }
}
#define VITA_DECODE_SOURCE_ROOM(bp, w, h, px) vitaDecodeSourceRoom((bp), w, h, px)
#else
#define VITA_DECODE_SOURCE_ROOM(bp, w, h, px) VITA_DECODE_SOURCE(bp, w, h, px)
#endif

void Bitmap::vitaDrawText(
    int x,
    int y,
    int w,
    int h,
    const char *text,
    int align,
    int fontSize
)
{
    guardDisposed();

    if (!p->hasCpuPixels)
        return;

    if (!text || !text[0])
        return;

    /*
     * Per ora abbiamo generato l'atlas a 24 px.
     * Manteniamo fontSize nell'interfaccia RGSS,
     * ma per questo primo test non lo usiamo.
     */
    (void)fontSize;

    /*
     * L'atlas viene caricato una sola volta.
     */
    static bool atlasTried = false;
    static bool atlasLoaded = false;

    static int atlasWidth = 0;
    static int atlasHeight = 0;

    static std::vector<unsigned char> atlasPixels;

    if (!atlasTried)
    {
        atlasTried = true;

        atlasLoaded =
            vitaLoadPngPixels(
                VITA_GAME_ROOT "Fonts/VCR_OSD_MONO_24.png",
                atlasWidth,
                atlasHeight,
                atlasPixels
            );
    }

    if (!atlasLoaded)
        return;

    /*
     * Questi valori devono corrispondere
     * allo script Pillow che abbiamo usato.
     */
    const int FIRST_CHAR = 32;
    const int LAST_CHAR  = 126;

    const int COLS   = 16;
    const int CELL_W = 32;
    const int CELL_H = 40;

    /*
     * VCR OSD Mono è monospaziato.
     * 15 px è una prima approssimazione
     * per la distanza fra caratteri a 24 px.
     */
    const int ADVANCE_X = 15;

    const size_t len =
        std::strlen(text);

    /*
     * Larghezza logica della stringa.
     */
    int textWidth =
        static_cast<int>(len) *
        ADVANCE_X;

    /*
     * Allineamento RGSS:
     *
     * 0 = sinistra
     * 1 = centro
     * 2 = destra
     */
    int dstX = x;

    if (align == 1)
    {
        dstX =
            x +
            (w - textWidth) / 2;
    }
    else if (align == 2)
    {
        dstX =
            x +
            w -
            textWidth;
    }

    /*
     * Centriamo verticalmente la cella
     * dentro il rettangolo draw_text.
     */
    int dstY =
        y +
        (h - CELL_H) / 2;

    /*
     * Disegna un carattere alla volta.
     */
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char ch =
            static_cast<unsigned char>(
                text[i]
            );

        /*
         * Il nostro atlas contiene ASCII 32-126.
         * Per ora sostituiamo eventuali caratteri
         * non disponibili con '?'.
         */
        if (ch < FIRST_CHAR ||
            ch > LAST_CHAR)
        {
            ch = '?';
        }

        /*
         * Trova la cella del carattere.
         */
        int index =
            static_cast<int>(ch) -
            FIRST_CHAR;

        int col =
            index % COLS;

        int row =
            index / COLS;

        int srcBaseX =
            col * CELL_W;

        int srcBaseY =
            row * CELL_H;

        /*
         * Posizione del carattere
         * nel Bitmap di destinazione.
         */
        int charDstX =
            dstX +
            static_cast<int>(i) *
            ADVANCE_X;

        /*
         * Copia la cella dall'atlas
         * al Bitmap RGSS.
         */
        for (int sy = 0;
             sy < CELL_H;
             ++sy)
        {
            int sourceY =
                srcBaseY + sy;

            int targetY =
                dstY + sy;

            if (sourceY < 0 ||
                sourceY >= atlasHeight)
            {
                continue;
            }

            if (targetY < 0 ||
                targetY >= height())
            {
                continue;
            }

            for (int sx = 0;
                 sx < CELL_W;
                 ++sx)
            {
                int sourceX =
                    srcBaseX + sx;

                int targetX =
                    charDstX + sx;

                if (sourceX < 0 ||
                    sourceX >= atlasWidth)
                {
                    continue;
                }

                if (targetX < 0 ||
                    targetX >= width())
                {
                    continue;
                }

                const unsigned char *src =
                    atlasPixels.data() +
                    (
                        sourceY *
                        atlasWidth +
                        sourceX
                    ) * 4;

                unsigned char *dst =
                    p->pixels.data() +
                    (
                        targetY *
                        width() +
                        targetX
                    ) * 4;

                /*
                 * Pixel completamente trasparente:
                 * non c'è niente da copiare.
                 */
                unsigned int alpha =
                    src[3];

                if (alpha == 0)
                    continue;

                unsigned int invAlpha =
                    255 - alpha;

                /*
                 * Alpha blending del glyph
                 * sopra il contenuto già presente.
                 */
                dst[0] =
                    static_cast<unsigned char>(
                        (
                            src[0] * alpha +
                            dst[0] * invAlpha
                        ) / 255
                    );

                dst[1] =
                    static_cast<unsigned char>(
                        (
                            src[1] * alpha +
                            dst[1] * invAlpha
                        ) / 255
                    );

                dst[2] =
                    static_cast<unsigned char>(
                        (
                            src[2] * alpha +
                            dst[2] * invAlpha
                        ) / 255
                    );

                dst[3] =
                    static_cast<unsigned char>(
                        std::min(
                            255u,
                            alpha +
                            (
                                dst[3] *
                                invAlpha
                            ) / 255
                        )
                    );
            }
        }
    }

    /*
     * Ricarica sulla GPU il Bitmap
     * modificato dalla CPU.
     */
    VITA_PG_DIRTY(p);
    VITA_PG_ENSURE(p);
    TEX::bind(
        p->gl.tex
    );

    TEX::uploadImage(
        width(),
        height(),
        p->pixels.data(),
        GL_RGBA
    );

    FBO::unbind();
}

/*
 * CPU operations (stretch_blt, radial_blur). The CPU copy (p->pixels) is the source of truth:
 * each operation changes it and uploads it to the texture, as vitaDrawText does. No GPU readback.
 */
#ifdef MKXP_VITA_DEBUG_BITMAP_DUMP
#include <cstdio>

static void vitaBitmapDebug(const char *op, const Bitmap *b, const std::vector<unsigned char> &px,
                            int w, int h, const char *detail)
{
    static int dumps = 0;
    uint32_t hash = 2166136261u;
    for (unsigned char c : px) { hash ^= c; hash *= 16777619u; }

    char file[96] = "";
    if (dumps < 8) {
        std::snprintf(file, sizeof(file), VITA_GAME_ROOT "bmpdump_%02d_%s.tga", dumps++, op);
        FILE *t = std::fopen(file, "wb");
        if (t) {
            const unsigned char hdr[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                (unsigned char)(w & 255), (unsigned char)(w >> 8), (unsigned char)(h & 255), (unsigned char)(h >> 8),
                32, 0x28 };  /* 32 bpp, top-left origin, 8 alpha bits */
            std::fwrite(hdr, 1, sizeof(hdr), t);
            for (size_t i = 0; i < px.size(); i += 4) {
                const unsigned char bgra[4] = { px[i + 2], px[i + 1], px[i], px[i + 3] };
                std::fwrite(bgra, 1, 4, t);
            }
            std::fclose(t);
        }
    }

    FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) {
        std::fprintf(f, "BITMAP_OP %s bitmap=%p size=%dx%d %s result_fnv=0x%08x dump=%s\n",
                     op, (const void *)b, w, h, detail, (unsigned)hash, file[0] ? file : "-");
        std::fclose(f);
    }
}
#endif

/* Makes p->pixels valid for a bitmap loaded from file (the constructor keeps only the texture). */
static bool vitaEnsureCpuPixels(BitmapPrivate *p)
{
#ifdef MKXP_VITA_TEX_PAGING
    p->lastUse = gPgFrame;
#endif
    if (p->hasCpuPixels)
        return true;
    if (p->sourcePath.empty())
        return false;   /* missing-file fallback: its colour exists only on the GPU */

    int w = 0, h = 0;
    std::vector<unsigned char> px;
#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
#ifdef MKXP_VITA_CPU_PAGING
    vitaCpuMakeRoom((size_t)p->gl.width * p->gl.height * 4);
#endif
    if (!VITA_DECODE_SOURCE_ROOM(p, w, h, px) || w != p->gl.width || h != p->gl.height)
        return false;
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_PNG_DECODE, vitaDiagT0, (uint64_t)w * h * 4, "cpu_copy");
#endif

    p->pixels.swap(px);
    p->hasCpuPixels = true;
    return true;
}

#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
/*
 * Perf fix (MKXP_VITA_BITMAP_DEFERRED_UPLOAD): CPU operations only mark the texture stale; the one
 * full upload happens when the GPU next needs the texture (Bitmap::getGLTypes / bindTex, the only
 * ways the texture leaves this file). A window refresh with ~40 draw_text calls used to upload the
 * whole contents ~40 times (PERF: 1769 uploads / 351 MiB per 120 battle frames).
 */
static void vitaUploadCpuPixelsNow(BitmapPrivate *p);
static void vitaUploadCpuPixels(BitmapPrivate *p)
{
    VITA_PG_DIRTY(p);
    p->uploadPending = true;
}
static inline void vitaFlushUpload(BitmapPrivate *p)
{
    if (p && p->uploadPending) {
        VITA_PG_ENSURE(p);
#ifdef MKXP_VITA_LOAD_OOM_RETRY
        if (p->evicted)
            return;   /* the rebuild found no texture memory: the upload waits for the next try */
#endif
        p->uploadPending = false;
        /* Called while the scene is being drawn: keep the bound framebuffer (no FBO::unbind, which
         * sent the rest of the frame to FB0: text appeared only once it stopped changing) and the
         * bound texture of the current unit. */
        GLint prevTex = 0;
        gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        TEX::bind(p->gl.tex);
#ifdef MKXP_VITA_DIAG
        const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
        TEX::uploadImage(p->gl.width, p->gl.height, p->pixels.data(), GL_RGBA);
#ifdef MKXP_VITA_DIAG
        vitaDiagSpan(VD_BMP_UPLOAD, vitaDiagT0, (uint64_t)p->gl.width * p->gl.height * 4, "deferred");
#endif
        gl.BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    }
}
static void vitaUploadCpuPixelsNow(BitmapPrivate *p)
#else
static void vitaUploadCpuPixels(BitmapPrivate *p)
#endif
{
    VITA_PG_DIRTY(p);
    VITA_PG_ENSURE(p);
    TEX::bind(p->gl.tex);
#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
    TEX::uploadImage(p->gl.width, p->gl.height, p->pixels.data(), GL_RGBA);
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_BMP_UPLOAD, vitaDiagT0, (uint64_t)p->gl.width * p->gl.height * 4, "cpu_op");
#endif
    FBO::unbind();
}

bool Bitmap::vitaDrawTextFont(int x, int y, int w, int h, const char *text, int align,
                              const VitaFontSpec &spec, VitaTextInfo *info)
{
    guardDisposed();
    VITA_DIAG_OP(VD_DRAW_TEXT);

    if (!VitaFont::available(spec))
        return false;

    /* Bitmaps loaded from file get a CPU copy on their first draw_text. */
    if (!vitaEnsureCpuPixels(p))
        return true;

    VitaFont::drawText(p->pixels.data(), p->gl.width, p->gl.height, x, y, w, h, text, align, spec, info);
    vitaUploadCpuPixels(p);
    return true;
}

#ifdef MKXP_VITA_BITMAP_WINDOW_OPS
/*
 * Window primitives on the CPU copy (MKXP_VITA_BITMAP_WINDOW_OPS), with mkxp-z semantics:
 * fill_rect/clear_rect/clear replace pixels (glClear + scissor upstream), gradient_fill_rect is a
 * per-vertex-colour quad without blending, blt is stretch_blt without scaling.
 */
static void vitaColorToBytes(const Vec4 &c, unsigned char out[4])
{
    out[0] = VitaBitmapCpu::toByte(c.x);
    out[1] = VitaBitmapCpu::toByte(c.y);
    out[2] = VitaBitmapCpu::toByte(c.z);
    out[3] = VitaBitmapCpu::toByte(c.w);
}

void Bitmap::fillRect(const IntRect &rect, const Vec4 &color)
{
    guardDisposed();
    VITA_DIAG_OP(VD_FILL_RECT);
    if (!vitaEnsureCpuPixels(p))
        return;
    unsigned char c[4];
    vitaColorToBytes(color, c);
    if (VitaBitmapCpu::fillRect(VitaBitmapCpu::Image{ p->gl.width, p->gl.height, p->pixels.data() },
                                rect.x, rect.y, rect.w, rect.h, c) > 0)
        vitaUploadCpuPixels(p);
}

void Bitmap::fillRect(int x, int y, int width, int height, const Vec4 &color)
{
    fillRect(IntRect(x, y, width, height), color);
}

void Bitmap::gradientFillRect(const IntRect &rect, const Vec4 &color1, const Vec4 &color2, bool vertical)
{
    guardDisposed();
    VITA_DIAG_OP(VD_GRADIENT);
    if (!vitaEnsureCpuPixels(p))
        return;
    const float c1[4] = { color1.x, color1.y, color1.z, color1.w };
    const float c2[4] = { color2.x, color2.y, color2.z, color2.w };
    if (VitaBitmapCpu::gradientFillRect(VitaBitmapCpu::Image{ p->gl.width, p->gl.height, p->pixels.data() },
                                        rect.x, rect.y, rect.w, rect.h, c1, c2, vertical) > 0)
        vitaUploadCpuPixels(p);
}

void Bitmap::gradientFillRect(int x, int y, int width, int height,
                              const Vec4 &color1, const Vec4 &color2, bool vertical)
{
    gradientFillRect(IntRect(x, y, width, height), color1, color2, vertical);
}

void Bitmap::clearRect(const IntRect &rect)
{
    VITA_DIAG_OP(VD_CLEAR_RECT);
    fillRect(rect, Vec4());
}

void Bitmap::clearRect(int x, int y, int width, int height)
{
    clearRect(IntRect(x, y, width, height));
}

void Bitmap::clear()
{
    guardDisposed();
    VITA_DIAG_OP(VD_CLEAR);
    if (!vitaEnsureCpuPixels(p))
        return;
    std::fill(p->pixels.begin(), p->pixels.end(), 0);
    vitaUploadCpuPixels(p);
}

void Bitmap::blt(int x, int y, const Bitmap &source, const IntRect &rect, int opacity)
{
    if (source.isDisposed())
        return;
    VITA_DIAG_OP(VD_BLT);
    stretchBlt(IntRect(x, y, std::abs(rect.w), std::abs(rect.h)), source, rect, opacity);
}
#endif

#ifdef MKXP_VITA_DEBUG_BITMAP_WINDOW
unsigned int Bitmap::vitaRegionHash(int x, int y, int w, int h) const
{
    if (!p || !p->hasCpuPixels)
        return 0;
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    const int x0 = std::max(x, 0), x1 = std::min(x + w, p->gl.width);
    const int y0 = std::max(y, 0), y1 = std::min(y + h, p->gl.height);
    unsigned int hash = 2166136261u;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0 * 4; xx < x1 * 4; ++xx) {
            hash ^= p->pixels[(size_t)yy * p->gl.width * 4 + xx];
            hash *= 16777619u;
        }
    return hash;
}
#endif

void Bitmap::stretchBlt(IntRect destRect, const Bitmap &source, IntRect sourceRect,
                        int opacity, bool smooth, enum BitmapBltMode mode)
{
    guardDisposed();

    if (source.isDisposed())
        return;
    VITA_DIAG_OP(VD_STRETCH_BLT);

    /* RGSS stretch_blt only reaches here with smooth = false and NORMAL (bitmap binding). */
    (void)smooth;
    (void)mode;

    if (!vitaEnsureCpuPixels(p))
        return;

    /* Source pixels: its CPU copy, a fresh decode of its file, or a copy of ourselves. */
    std::vector<unsigned char> decoded;
    const unsigned char *srcPx = nullptr;
    int srcW = source.p->gl.width, srcH = source.p->gl.height;

    if (&source == this) {
        decoded = p->pixels;
        srcPx = decoded.data();
    } else if (source.p->hasCpuPixels) {
        srcPx = source.p->pixels.data();
    } else if (!source.p->sourcePath.empty() &&
               VITA_DECODE_SOURCE(source.p, srcW, srcH, decoded)) {
        srcPx = decoded.data();
#ifdef MKXP_VITA_BLT_SOURCE_KEEP_CPU
        /* Perf fix: keep the decode as the source's CPU copy (it equals its texture, both come from
         * the same file), so IconSet & co. are not decoded again on every blt/draw_icon. */
        if (srcW == source.p->gl.width && srcH == source.p->gl.height) {
            source.p->pixels.swap(decoded);
            source.p->hasCpuPixels = true;
            srcPx = source.p->pixels.data();
        }
#endif
#ifdef MKXP_VITA_DIAG
        /* Count only (the decode time is inside the stretch_blt span): a source without a CPU
         * copy is decoded again on every stretch_blt/blt. */
        vitaDiagCount(VD_PNG_DECODE);
#endif
    }

    if (!srcPx)
        return;

    const long written = VitaBitmapCpu::stretchBlt(
        VitaBitmapCpu::Image{ p->gl.width, p->gl.height, p->pixels.data() },
        destRect.x, destRect.y, destRect.w, destRect.h,
        VitaBitmapCpu::ConstImage{ srcW, srcH, srcPx },
        sourceRect.x, sourceRect.y, sourceRect.w, sourceRect.h, opacity);

    if (written > 0)
        vitaUploadCpuPixels(p);

#ifdef MKXP_VITA_DEBUG_BITMAP_DUMP
    char detail[192];
    std::snprintf(detail, sizeof(detail), "dest=%d,%d,%d,%d src=%dx%d rect=%d,%d,%d,%d opacity=%d written=%ld",
                  destRect.x, destRect.y, destRect.w, destRect.h, srcW, srcH,
                  sourceRect.x, sourceRect.y, sourceRect.w, sourceRect.h, opacity, written);
    vitaBitmapDebug("stretch_blt", this, p->pixels, p->gl.width, p->gl.height, detail);
#endif
}

#ifdef MKXP_VITA_SCREEN_FX
void Bitmap::blur()
{
    guardDisposed();
    if (!vitaEnsureCpuPixels(p))
        return;
    VitaBitmapCpu::blur(VitaBitmapCpu::Image{ p->gl.width, p->gl.height, p->pixels.data() });
    vitaUploadCpuPixels(p);
}

void Bitmap::vitaSetPixelsRGBA(const unsigned char *rgba, int strideBytes)
{
    guardDisposed();
    const int w = p->gl.width, h = p->gl.height;
    p->pixels.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(p->pixels.data() + (size_t)y * w * 4, rgba + (size_t)y * strideBytes, (size_t)w * 4);
    p->hasCpuPixels = true;
    vitaUploadCpuPixels(p);
}
#endif

#ifdef MKXP_VITA_RGSS_COMPAT
/*
 * RGSS compatibility (MKXP_VITA_RGSS_COMPAT): get_pixel / set_pixel / hue_change on the CPU copy,
 * with mkxp-z semantics. Window_Base#text_color reads the windowskin with get_pixel: the old Ruby
 * stub returned white for every text colour.
 */
/*
 * Bitmap#clone / #dup (initialize_copy): a new texture with a copy of the pixels. Used by
 * Cache.hue_changed_bitmap (normal_bitmap(path).clone.hue_change(hue)): without it the clone had no
 * C++ object ("disposed mkxp object" when the enemy death animation loaded a hued bitmap).
 */
Bitmap::Bitmap(const Bitmap &other, int frame)
{
    (void)frame;   /* no animated bitmaps in this backend */
    other.guardDisposed();
    p = new BitmapPrivate();
    const int w = other.p->gl.width, h = other.p->gl.height;
#ifdef MKXP_VITA_CPU_PAGING
    other.p->lastUse = gPgFrame;
    vitaCpuMakeRoom((size_t)w * h * 4);
#endif
    p->gl = VITA_BITMAP_TEX_REQUEST(w, h);
#ifdef MKXP_VITA_HUE_FAST
    /*
     * Perf/memory fix: a source loaded from file has no CPU copy; decode it straight into the clone
     * instead of giving the source (usually a Cache entry that lives forever) a 4.4 MiB copy first
     * and then copying it again. Same pixels: the PNG the source was loaded from.
     */
    int dw = 0, dh = 0;
#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
    if (!other.p->hasCpuPixels && !other.p->sourcePath.empty() &&
        VITA_DECODE_SOURCE_ROOM(other.p, dw, dh, p->pixels) && dw == w && dh == h) {
#ifdef MKXP_VITA_DIAG
        vitaDiagSpan(VD_PNG_DECODE, vitaDiagT0, (uint64_t)w * h * 4, "clone");
#endif
    } else
#endif
    if (vitaEnsureCpuPixels(other.p)) {
        p->pixels = other.p->pixels;
    } else {
        p->pixels.assign((size_t)w * h * 4, 0);   /* GPU-only source (missing-file fallback) */
    }
    p->hasCpuPixels = true;
    TEX::bind(p->gl.tex);
    TEX::uploadImage(w, h, p->pixels.data(), GL_RGBA);
    FBO::unbind();
#ifdef MKXP_VITA_HUE_PAGING
    if (other.p->fileClean && !other.p->sourcePath.empty()) {   /* same pixels as the source: its file */
        p->sourcePath = other.p->sourcePath;
        p->fileClean = true;
        p->cleanHue = other.p->cleanHue;
    }
#endif
}

Color Bitmap::getPixel(int x, int y) const
{
    guardDisposed();
    if (x < 0 || y < 0 || x >= p->gl.width || y >= p->gl.height || !vitaEnsureCpuPixels(p))
        return Color(0, 0, 0, 0);
    const unsigned char *px = p->pixels.data() + ((size_t)y * p->gl.width + x) * 4;
    return Color(px[0], px[1], px[2], px[3]);
}

void Bitmap::setPixel(int x, int y, const Color &color)
{
    guardDisposed();
    if (x < 0 || y < 0 || x >= p->gl.width || y >= p->gl.height || !vitaEnsureCpuPixels(p))
        return;
    unsigned char *px = p->pixels.data() + ((size_t)y * p->gl.width + x) * 4;
    const double v[4] = { color.red, color.green, color.blue, color.alpha };
    for (int c = 0; c < 4; ++c)
        px[c] = (unsigned char)std::max(0.0, std::min(255.0, v[c]));
    vitaUploadCpuPixels(p);
}

void Bitmap::hueChange(int hue)
{
    guardDisposed();
    if ((hue % 360) == 0 || !vitaEnsureCpuPixels(p))
        return;
#ifdef MKXP_VITA_HUE_PAGING
    const bool recipe = p->fileClean && !p->sourcePath.empty() && p->cleanHue == 0;
#endif
    VitaBitmapCpu::hueChange(VitaBitmapCpu::Image{ p->gl.width, p->gl.height, p->pixels.data() }, hue);
    vitaUploadCpuPixels(p);
#ifdef MKXP_VITA_HUE_PAGING
    if (recipe) {   /* still the file, after this hue_change */
        p->fileClean = true;
        p->cleanHue = ((hue % 360) + 360) % 360;
    }
#endif
}
#endif

#ifdef MKXP_VITA_GPU_RADIAL_BLUR
#include "quad.h"
#include "quadarray.h"
#include "transform.h"
#include <vitaGL.h>
/*
 * Perf fix (MKXP_VITA_GPU_RADIAL_BLUR): mkxp-z's GPU radial_blur (bitmap.cpp) into a temporary
 * target, then read back into the CPU copy (the source of truth here). The CPU kernel took 14.4 s
 * at -O0 on the real battle background (Graphics.snap_to_bitmap is no longer empty).
 * Same geometry as upstream: the image plus its 4 edge mirrors, rotated `divisions` times about the
 * centre, vertex alpha 1/divisions, BlendAddition, GL_LINEAR. Returns false if it could not run.
 */
static bool vitaRadialBlurGpu(BitmapPrivate *p, int angle, int divisions)
{
    angle = std::max(0, std::min(359, angle));
    divisions = std::max(2, std::min(100, divisions));
    const int W = p->gl.width, H = p->gl.height;
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
    vitaFlushUpload(p);
#endif

    const float angleStep = (float)angle / (divisions - 1);
    const float opacity = 1.0f / divisions;
    const float baseAngle = -((float)angle / 2);

    ColorQuadArray qArray;
    qArray.resize(5);
    std::vector<Vertex> &vert = qArray.vertices;
    const FloatRect texRect(0, 0, W, H);
    int i = 0;
    i += Quad::setTexPosRect(&vert[i * 4], texRect, FloatRect(0, 0, W, H));          /* centre */
    i += Quad::setTexPosRect(&vert[i * 4], texRect, FloatRect(0, 0, W, -H));         /* upper */
    i += Quad::setTexPosRect(&vert[i * 4], texRect, FloatRect(0, H * 2, W, -H));     /* lower */
    i += Quad::setTexPosRect(&vert[i * 4], texRect, FloatRect(0, 0, -W, H));         /* left */
    i += Quad::setTexPosRect(&vert[i * 4], texRect, FloatRect(W * 2, 0, -W, H));     /* right */
    for (int k = 0; k < 4 * 5; ++k)
        vert[k].color = Vec4(1, 1, 1, opacity);
    qArray.commit();

    TEXFBO tmp;
    TEXFBO::init(tmp);
    TEXFBO::allocEmpty(tmp, W, H);
    TEXFBO::linkFBO(tmp);
    if (!tmp.fbo.gl) {
        TEXFBO::fini(tmp);
        return false;
    }

    FBO::bind(tmp.fbo);
    glState.viewport.pushSet(IntRect(0, 0, W, H));
    glState.scissorTest.pushSet(false);

    /* Transparent start (a quad: glClear on these FBOs is not reliable on vitaGL). */
    glState.blend.pushSet(false);
    FlatColorShader &fc = shState->shaders().flatColor;
    fc.bind();
    fc.projMat.set(Vec2i(W, H));
    fc.setTranslation(Vec2i());
    fc.setColor(Vec4());
    Quad clearQuad;
    clearQuad.setPosRect(FloatRect(0, 0, W, H));
    clearQuad.draw();
    glState.blend.pop();

    glState.blend.pushSet(true);
    glState.blendMode.pushSet(BlendAddition);
    Transform trans;
    trans.setOrigin(Vec2(W / 2.0f, H / 2.0f));
    trans.setPosition(Vec2(W / 2.0f, H / 2.0f));
    SimpleMatrixShader &shader = shState->shaders().simpleMatrix;
    shader.bind();
    shader.projMat.set(Vec2i(W, H));
    shader.setTexSize(Vec2i(W, H));
    gl.ActiveTexture(GL_TEXTURE0);
    VITA_PG_ENSURE(p);
    TEX::bind(p->gl.tex);
    TEX::setSmooth(true);
    for (int k = 0; k < divisions; ++k) {
        trans.setRotation(baseAngle + k * angleStep);
        shader.setMatrix(trans.getMatrix());
        qArray.draw();
    }
    TEX::setSmooth(false);
    glState.blendMode.pop();
    glState.blend.pop();
    glState.scissorTest.pop();
    glState.viewport.pop();

    glFinish();
    TEX::bind(tmp.tex);
    const unsigned char *mem = (const unsigned char *)vglGetTexDataPointer(GL_TEXTURE_2D);
    if (mem) {
        const int stride = ((W + 7) & ~7) * 4;
        for (int y = 0; y < H; ++y)
            std::memcpy(p->pixels.data() + (size_t)y * W * 4, mem + (size_t)y * stride, (size_t)W * 4);
    }
    FBO::unbind();
    TEXFBO::fini(tmp);
    return mem != nullptr;
}
#endif

void Bitmap::radialBlur(int angle, int divisions)
{
    guardDisposed();
    VITA_DIAG_OP(VD_RADIAL_BLUR);

    if (!vitaEnsureCpuPixels(p))
        return;

#ifdef MKXP_VITA_GPU_RADIAL_BLUR
    if (!vitaRadialBlurGpu(p, angle, divisions))
#endif
    VitaBitmapCpu::radialBlur(
        VitaBitmapCpu::Image{ p->gl.width, p->gl.height, p->pixels.data() }, angle, divisions);

    vitaUploadCpuPixels(p);

#ifdef MKXP_VITA_DEBUG_BITMAP_DUMP
    char detail[64];
    std::snprintf(detail, sizeof(detail), "angle=%d divisions=%d", angle, divisions);
    vitaBitmapDebug("radial_blur", this, p->pixels, p->gl.width, p->gl.height, detail);
#endif
}

#endif


void Bitmap::releaseResources()
{
#ifdef MKXP_VITA_TEX_PAGING
    if (p->evicted)
        gPgOutBytes -= vitaPgBytes(p);
    else
#endif
    shState->texPool().release(p->gl);

    delete p;
    p = nullptr;
}
Bitmap *Bitmap::spawnChild()
{

    /*
     * Il Bitmap Vita minimale non supporta mega-surface.
     *
     * Sprite::setBitmap() contiene comunque un riferimento
     * a spawnChild(), quindi il simbolo deve esistere.
     * Non verrà chiamato perché isMega() restituisce false.
     */
    return nullptr;
}
ChildPublic *Bitmap::getChildInfo()
{
    /*
     * Il backend Vita minimale non usa Mega Bitmap.
     *
     * isMega() restituisce sempre false, quindi una Bitmap
     * normale non possiede informazioni "child".
     */
    return nullptr;
}


void Bitmap::childUpdate()
{
    /*
     * Nessun child da aggiornare nel backend Vita minimale.
     *
     * Questo metodo esiste perché WindowVX/Sprite supportano
     * anche le Mega Bitmap nel renderer completo di mkxp-z.
     */
}


void Bitmap::bindTex(
    ShaderBase &shader,
    bool substituteLoresSize
)
{
    guardDisposed();

    /*
     * Il backend Vita non ha una texture hires/lores
     * alternativa, quindi questo parametro per ora
     * non cambia il comportamento.
     */
    (void)substituteLoresSize;

    VITA_PG_ENSURE(p);
#ifdef MKXP_VITA_BITMAP_DEFERRED_UPLOAD
    vitaFlushUpload(p);
#endif

    /*
     * Comunica allo shader le dimensioni della vera
     * texture associata a questa Bitmap.
     */
    shader.setTexSize(
        Vec2i(
            p->gl.width,
            p->gl.height
        )
    );

    /*
     * Attiva texture unit 0 e collega la texture GPU.
     */
    gl.ActiveTexture(
        GL_TEXTURE0
    );

    TEX::bind(
        p->gl.tex
    );
}
