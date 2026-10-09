// Host test for MKXP_VITA_HUE_PAGING + MKXP_VITA_CPU_PAGING (with MKXP_VITA_TEX_PAGING): the real
// src/bitmap-vita.cpp, built for the host (run.sh), against
//   - a fake GL that keeps every texture's pixels (TexImage2D / TexSubImage2D),
//   - a fake big pool: C++ blocks of 128 KiB or more count against a cap and throw std::bad_alloc
//     past it, as vita-big-alloc.cpp's pool does once the newlib heap is full too,
//   - vglMemFree / the big pool's free figure set by the test (pressure on and off).
// Random sessions of RGSS-like operations (Cache: load, clone + hue_change; get_pixel, set_pixel,
// blt from a bitmap, render = getGLTypes, frames = vitaTexPagingTick, dispose) are checked after every
// step against a model of what every bitmap must contain: the texture handed to the renderer and every
// CPU read must equal the model, whatever was evicted, dropped and rebuilt in between.
// Coverage counters make sure hue clones really were given back and rebuilt, and that a decode
// failing with std::bad_alloc was retried after the clean copies went.
#include <GL/gl.h>
#include <png.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "bitmap.h"
#include "etc.h"
#include "gl-util.h"
#include "sharedstate.h"
#include "texpool.h"
#include "shader.h"
#include "vita-font.h"
#include "vita-image-cache.h"
#include "exception.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <vitaGL.h>
#include <psp2/ctrl.h>

namespace oracle {
#include "bitmap-vita-cpu.h"
}

/* ---------- fake big pool ---------- */
static size_t gBigCap = (size_t)1 << 40, gBigUsed = 0, gMaxAlloc = 0;
volatile int gInLoad = 0;
static unsigned gBigThrows = 0;
static bool gStatsLie = false;   /* report a free pool: vitaCpuMakeRoom does nothing, the decode throws */
static const size_t kBig = 128 * 1024;
struct Hdr { size_t n; size_t pad; };
void *operator new(size_t n)
{
    if (n >= kBig && gBigUsed + n > gBigCap) {
        ++gBigThrows;
        throw std::bad_alloc();
    }
    if (n > gMaxAlloc)
        gMaxAlloc = n;
    Hdr *h = (Hdr *)std::malloc(sizeof(Hdr) + n);
    if (!h)
        throw std::bad_alloc();
    h->n = n;
    if (n >= kBig)
        gBigUsed += n;
    return h + 1;
}
void operator delete(void *p) noexcept
{
    if (!p)
        return;
    Hdr *h = (Hdr *)p - 1;
    if (h->n >= kBig)
        gBigUsed -= h->n;
    std::free(h);
}
void operator delete(void *p, size_t) noexcept { operator delete(p); }
void *operator new[](size_t n) { return operator new(n); }
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete[](void *p, size_t) noexcept { operator delete(p); }

extern "C" void vitaBigAllocStats(unsigned *usedKb, unsigned *freeKb, unsigned *blocks, unsigned *fallbacks)
{
    *usedKb = (unsigned)(gBigUsed / 1024);
    *freeKb = gStatsLie ? (1u << 30) : (unsigned)((gBigCap > gBigUsed ? gBigCap - gBigUsed : 0) / 1024);
    *blocks = *fallbacks = 0;
}

/* ---------- fake vitaGL / psp2 ---------- */
/* Texture memory as vitaGL keeps it: rows of VGL_ALIGN(w, 8) * 4 bytes, from a budget; a texture
 * whose memory does not fit has no data (gpu_alloc_texture leaves tex->data NULL). */
static size_t gVglCap = (size_t)1 << 40, gVglUsed = 0;
extern "C" size_t vglMemFree(vglMemType) { return (gVglCap > gVglUsed ? gVglCap - gVglUsed : 0) / 3; }
extern "C" int sceCtrlPeekBufferPositive(int, SceCtrlData *, int) { return 0; }

/* ---------- fake GL ---------- */
/* Texture memory is vitaGL's, not the C++ heap's: kept out of the fake big pool (malloc). */
template <class T> struct MallocAlloc {
    typedef T value_type;
    MallocAlloc() = default;
    template <class U> MallocAlloc(const MallocAlloc<U> &) {}
    T *allocate(size_t n) { T *p = (T *)std::malloc(n * sizeof(T)); if (!p) throw std::bad_alloc(); return p; }
    void deallocate(T *p, size_t) { std::free(p); }
    bool operator==(const MallocAlloc &) const { return true; }
    bool operator!=(const MallocAlloc &) const { return false; }
};
struct FakeTex { int w = 0, h = 0; size_t stride = 0; bool ok = false; std::vector<unsigned char, MallocAlloc<unsigned char>> px; };
static std::map<GLuint, FakeTex> gTex;
static GLuint gNextTex = 1, gBound = 0;
static std::map<GLuint, int> gDeleted;   /* tex id -> deletions */
static unsigned gDirectPtrs = 0;          /* vglGetTexDataPointer handed out texture memory */
extern "C" void *vglGetTexDataPointer(GLenum)
{
    auto it = gTex.find(gBound);
    if (it == gTex.end() || !it->second.ok)
        return nullptr;
    ++gDirectPtrs;
    return it->second.px.data();
}
static void texFree(FakeTex &t)
{
    if (t.ok)
        gVglUsed -= t.px.size();
    t.ok = false;
    decltype(t.px)().swap(t.px);
}
static void APIENTRY fGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; ++i) { t[i] = gNextTex++; gTex[t[i]]; } }
static void APIENTRY fDeleteTextures(GLsizei n, const GLuint *t)
{
    for (int i = 0; i < n; ++i) {
        auto it = gTex.find(t[i]);
        if (it != gTex.end()) { texFree(it->second); gTex.erase(it); }
        ++gDeleted[t[i]];
    }
}
static void APIENTRY fBindTexture(GLenum, GLuint t) { gBound = t; }
static void APIENTRY fTexImage2D(GLenum, GLint, GLint, GLsizei w, GLsizei h, GLint, GLenum, GLenum, const GLvoid *d)
{
    FakeTex &t = gTex.at(gBound);
    texFree(t);
    t.w = w; t.h = h; t.stride = (size_t)((w + 7) & ~7) * 4;
    const size_t n = t.stride * h;
    if (gVglUsed + n > gVglCap)
        return;   /* out of memory: no data */
    t.ok = true;
    t.px.assign(n, 0);
    gVglUsed += n;
    if (d)
        for (int r = 0; r < h; ++r)
            std::memcpy(t.px.data() + r * t.stride, (const unsigned char *)d + (size_t)r * w * 4, (size_t)w * 4);
}
static void APIENTRY fTexSubImage2D(GLenum, GLint, GLint x, GLint y, GLsizei w, GLsizei h, GLenum, GLenum, const GLvoid *d)
{
    FakeTex &t = gTex.at(gBound);
    if (!t.ok)
        return;
    for (int r = 0; r < h; ++r)
        std::memcpy(t.px.data() + (size_t)(y + r) * t.stride + (size_t)x * 4, (const unsigned char *)d + (size_t)r * w * 4, (size_t)w * 4);
}
static bool texContent(GLuint id, std::vector<unsigned char> &out)
{
    auto it = gTex.find(id);
    if (it == gTex.end() || !it->second.ok)
        return false;
    const FakeTex &t = it->second;
    out.resize((size_t)t.w * t.h * 4);
    for (int r = 0; r < t.h; ++r)
        std::memcpy(out.data() + (size_t)r * t.w * 4, t.px.data() + r * t.stride, (size_t)t.w * 4);
    return true;
}
static void APIENTRY fTexParameteri(GLenum, GLenum, GLint) {}
static void APIENTRY fActiveTexture(GLenum) {}
static void APIENTRY fBindFramebuffer(GLenum, GLuint) {}
static void APIENTRY fGetIntegerv(GLenum p, GLint *v) { *v = p == GL_TEXTURE_BINDING_2D ? (GLint)gBound : 0; }
static void APIENTRY fDeleteFramebuffers(GLsizei, const GLuint *) {}
GLFunctions gl;

/* ---------- the rest of what bitmap-vita.cpp links against ---------- */
/* shState-> is only used for texPool() (stubbed below): any non-null, aligned object will do
 * (a null instance made every releaseResources a member call on a null pointer: UB). */
alignas(64) static unsigned char gStateBuf[4096];
SharedState *SharedState::instance = reinterpret_cast<SharedState *>(gStateBuf);
static char gPoolDummy;
TexPool &SharedState::texPool() const { return *(TexPool *)&gPoolDummy; }
void TexPool::release(TEXFBO &o) { TEXFBO::fini(o); }
FBO::ID FBO::boundFramebufferID;
void ShaderBase::setTexSize(const Vec2i &) {}
Color::Color(double r, double g, double b, double a) : red(r), green(g), blue(b), alpha(a) {}
int Color::serialSize() const { return 0; }
void Color::serialize(char *) const {}
bool VitaFont::available(const VitaFontSpec &) { return false; }
bool VitaFont::drawText(unsigned char *, int, int, int, int, int, int, const char *, int, const VitaFontSpec &, VitaTextInfo *) { return false; }
#ifdef MKXP_VITA_BITMAP_GC
/* Ruby's oldmalloc as vitaGcReport reads it: bytes reported since the last major GC, limit 16 MiB. */
static long long gOldmalloc = 0;
static unsigned gReportGcs = 0;
#endif
/* Ruby's full GC: frees the bitmaps the game no longer references (the test's garbage list). */
static std::vector<Bitmap *> gGarbage;
static unsigned gGcRuns = 0;
extern "C" void rb_gc_start(void)
{
    ++gGcRuns;
#ifdef MKXP_VITA_BITMAP_GC
    if (gOldmalloc > ((long long)16 << 20))
        ++gReportGcs;
    gOldmalloc = 0;
#endif
    for (Bitmap *b : gGarbage)
        delete b;
    gGarbage.clear();
}

#ifdef MKXP_VITA_BITMAP_GC
/* Ruby's count of memory held outside its heap (MKXP_VITA_BITMAP_GC): must come back to 0. */
static long long gGcAdjust = 0;
static unsigned gGcCharges = 0;
extern "C" void rb_gc_adjust_memory_usage(ssize_t diff)
{
    gGcAdjust += diff;
    if (diff > 0) {
        ++gGcCharges;
        gOldmalloc += diff;
    }
}
extern "C" uintptr_t rb_intern(const char *name) { return !std::strcmp(name, "oldmalloc_increase_bytes") ? 1 : 2; }
extern "C" uintptr_t rb_id2sym(uintptr_t id) { return id; }
extern "C" size_t rb_gc_stat(uintptr_t key) { return key == 1 ? (size_t)gOldmalloc : (size_t)16 << 20; }
#endif

extern "C" void vitaTexPagingTick();
extern "C" void vitaTexPagingStats(unsigned *evicted, unsigned *restored, unsigned *fails, unsigned *outKb);
extern "C" void vitaCpuPagingStats(unsigned *dropped, unsigned *droppedKb);

/* ---------- images ---------- */
static std::string gRoot;
static std::vector<std::vector<unsigned char>> gFilePx;
/* >= 1 MiB decoded (VitaImageCache::kMinBytes: the disk-cache path), even files with a width that
 * is a multiple of 8 (vitaGL rows tight), odd ones not (rows padded) */
static std::vector<int> gFileW, gFileH;
static const size_t kImg = (size_t)520 * 512 * 4;

static void writePng(const std::string &path, std::mt19937 &rng)
{
    const int kW = gFileW.size() % 2 ? 515 : 520, kH = 512;
    gFileW.push_back(kW); gFileH.push_back(kH);
    std::vector<unsigned char> px((size_t)kW * kH * 4);
    static const unsigned char pal[8][4] = { {255, 0, 0, 255}, {0, 200, 40, 255}, {20, 40, 230, 255}, {250, 250, 250, 128},
                                             {0, 0, 0, 0}, {128, 64, 32, 255}, {90, 200, 200, 200}, {255, 128, 0, 255} };
    for (size_t i = 0; i < (size_t)kW * kH; ++i) {
        const unsigned k = rng() % 10;
        if (k < 8) std::memcpy(&px[i * 4], pal[k], 4);
        else for (int c = 0; c < 4; ++c) px[i * 4 + c] = (unsigned char)(rng() % 256);
    }
    png_image im;
    std::memset(&im, 0, sizeof(im));
    im.version = PNG_IMAGE_VERSION;
    im.width = kW; im.height = kH; im.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&im, path.c_str(), 0, px.data(), 0, nullptr)) { std::printf("png write failed\n"); std::exit(2); }
    /* the model reads the file back the way the loader does (PNG_FORMAT_RGBA) */
    png_image rd;
    std::memset(&rd, 0, sizeof(rd));
    rd.version = PNG_IMAGE_VERSION;
    png_image_begin_read_from_file(&rd, path.c_str());
    rd.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> back(PNG_IMAGE_SIZE(rd));
    png_image_finish_read(&rd, nullptr, back.data(), 0, nullptr);
    gFilePx.push_back(back);
}

/* ---------- model ---------- */
static unsigned gEntryLoads = 0, gDirectLoads = 0;
struct Obj { Bitmap *b; std::vector<unsigned char> exp; int w, h; bool hueClone; bool fromFile = false; bool written = false; };
/* vitaGL out of memory for a texture whose content comes from the CPU (clone, hue, set_pixel, blt):
 * nothing retries those uploads - a known limit, counted, not part of what this test checks. */
static unsigned gCpuTexNoMem = 0;
static int gFails = 0;
static void fail(const char *what, int step, int idx)
{
    if (++gFails <= 10)
        std::printf("FAIL step=%d obj=%d %s\n", step, idx, what);
}

extern "C" void vitaTexPagingTick();
extern "C" void vitaTexPagingStats(unsigned *evicted, unsigned *restored, unsigned *fails, unsigned *outKb);
static unsigned gRestoreRetries = 0;
static unsigned restoreFails()
{
    unsigned ev = 0, rs = 0, fl = 0, ok = 0;
    vitaTexPagingStats(&ev, &rs, &fl, &ok);
    return fl;
}

static void checkRender(Obj &o, int step, int idx)
{
    const unsigned fl = restoreFails();
    TEXFBO *t = &o.b->getGLTypes();
    if (restoreFails() != fl) {
        /* the rebuild found no memory (texture paging's backoff: not drawn, tried again 60 frames
         * later); with room again it must come back with the right pixels */
        ++gRestoreRetries;
        const size_t cap = gBigCap, vcap = gVglCap;
        gBigCap = (size_t)1 << 40;
        gVglCap = (size_t)1 << 40;
        for (int k = 0; k < 61; ++k)
            vitaTexPagingTick();
        t = &o.b->getGLTypes();
        gBigCap = cap;
        gVglCap = vcap;
    }
    std::vector<unsigned char> px;
    if (!texContent(t->tex.gl, px))
    {
        if (gTex.count(t->tex.gl) && (!o.fromFile || o.written))
            ++gCpuTexNoMem;
        else
            fail(!gTex.count(t->tex.gl) ? "no texture" : "file bitmap texture without memory", step, idx);
    }
    else if (px != o.exp)
        fail("texture != model", step, idx);
}

static void checkPixel(Obj &o, int x, int y, int step, int idx)
{
    const Color c = o.b->getPixel(x, y);
    const unsigned char *e = &o.exp[((size_t)y * o.w + x) * 4];
    if (c.red != e[0] || c.green != e[1] || c.blue != e[2] || c.alpha != e[3])
        fail("get_pixel != model", step, idx);
}

int main(int argc, char **argv)
{
    gRoot = MKXP_VITA_GAME_ROOT;
    std::memset(&gl, 0, sizeof(gl));   /* a call the fake does not implement crashes the test */
    gl.GenTextures = fGenTextures; gl.DeleteTextures = fDeleteTextures; gl.BindTexture = fBindTexture;
    gl.TexImage2D = fTexImage2D; gl.TexSubImage2D = fTexSubImage2D; gl.TexParameteri = fTexParameteri;
    gl.ActiveTexture = fActiveTexture; gl.BindFramebuffer = fBindFramebuffer; gl.GetIntegerv = fGetIntegerv;
    gl.DeleteFramebuffers = fDeleteFramebuffers;

#ifdef MKXP_VITA_AUDIT_FIXES
    {   /* Bitmap.new(w, h) sizes (MKXP_VITA_AUDIT_FIXES): negative or overflowing -> RGSSError, 0 kept */
        auto raises = [](int w, int h) {
            try { Bitmap b(w, h); } catch (const Exception &e) { return e.type == Exception::RGSSError; }
            return false;
        };
        bool zeroOk = true;
        try { Bitmap b(0, 4); zeroOk = b.width() == 0 && b.height() == 4; } catch (...) { zeroOk = false; }
        const bool ok = raises(-1, 5) && raises(5, -1) && raises(INT_MIN, 3) && raises(70000, 70000) && raises(32768, 16384) &&
                        !raises(1, 1) && !raises(544, 416) && zeroOk;
        std::printf("%s  Bitmap.new sizes: negative/overflowing raise RGSSError, 0 and normal sizes work\n", ok ? "PASS" : "FAIL");
        if (!ok) ++gFails;
    }
#endif
    std::remove((gRoot + "qa.log").c_str());
    if (std::system(("rm -rf '" + gRoot + "cache'").c_str()) != 0) return 2;   /* entries of an earlier run are stale */
    std::mt19937 rng(argc > 1 ? std::atoi(argv[1]) : 1);
    const int kFiles = 6;
    for (int i = 0; i < kFiles; ++i)
        writePng(gRoot + "img" + std::to_string(i) + ".png", rng);

#ifdef MKXP_VITA_CLONE_OOM_RETRY
    {   /* Bitmap#clone of a file bitmap (texture only) with no room for its CPU copy, where only a full GC
           gives memory back: an unreferenced dirty bitmap (a VX Ace game, a hue animation sheet at the
           start of a battle). Then nothing to give back: bad_alloc, and the new texture not lost. */
        Bitmap *src = new Bitmap("img0");
        Bitmap *junk = new Bitmap(520, 512);
        junk->setPixel(0, 0, Color(1, 2, 3, 4));   /* dirty: CPU paging cannot drop it, only GC frees it */
        gGarbage.push_back(junk);
        const unsigned gc0 = gGcRuns;
        gBigCap = gBigUsed + kImg / 2;
        gStatsLie = true;
        Bitmap *c = nullptr;
        try { c = new Bitmap(*src); } catch (const std::bad_alloc &) {}
        gStatsLie = false;
        gBigCap = (size_t)1 << 40;
        bool ok = c && gGcRuns > gc0;
        if (c) {
            Obj o{ c, gFilePx[0], gFileW[0], gFileH[0], false, true };
            const int f0 = gFails;
            checkRender(o, -1, -1);
            checkPixel(o, 7, 9, -1, -1);
            ok = ok && gFails == f0;
            delete c;
        }
        std::printf("%s  clone with no room: a full GC gives memory back, the clone has the file's pixels\n", ok ? "PASS" : "FAIL");
        if (!ok) ++gFails;
        const size_t texBefore = gTex.size();
        gBigCap = gBigUsed + kImg / 2;
        gStatsLie = true;
        bool threw = false;
        try { Bitmap *d = new Bitmap(*src); delete d; } catch (const std::bad_alloc &) { threw = true; }
        gStatsLie = false;
        gBigCap = (size_t)1 << 40;
        ok = threw && gTex.size() == texBefore;
        std::printf("%s  clone with nothing to give back: bad_alloc, no texture left behind (%zu -> %zu)\n",
                    ok ? "PASS" : "FAIL", texBefore, gTex.size());
        if (!ok) ++gFails;
        unsigned rOk = 0, rFail = 0;
        if (FILE *q = std::fopen((gRoot + "qa.log").c_str(), "r")) {
            char line[512];
            while (std::fgets(line, sizeof line, q))
                if (!std::strncmp(line, "CLONE_RETRY", 11)) (std::strstr(line, " ok=1") ? rOk : rFail)++;
            std::fclose(q);
        }
        ok = rOk == 1 && rFail == 1;
        std::printf("%s  qa.log CLONE_RETRY ok=1 once, ok=0 once (%u, %u)\n", ok ? "PASS" : "FAIL", rOk, rFail);
        if (!ok) ++gFails;
        delete src;
    }
#endif
#ifdef MKXP_VITA_BITMAP_GC
    {   /* Bitmap.new(w, h) with no room for its CPU copy, where only a full GC gives memory back (a VX
           Ace game: a 640x480 Bitmap.new at the start of a battle); then nothing to give back:
           bad_alloc and no texture left behind; reported bytes: w*h*4 per bitmap, back when it goes. */
        Bitmap *junk = new Bitmap(520, 512);
        junk->setPixel(0, 0, Color(1, 2, 3, 4));
        const long long charged = gGcAdjust;
        bool ok = charged == (long long)520 * 512 * 4;
        gGarbage.push_back(junk);
        const unsigned gc0 = gGcRuns;
        gBigCap = gBigUsed + kImg / 2;
        Bitmap *b = nullptr;
        try { b = new Bitmap(520, 512); } catch (const std::bad_alloc &) {}
        gBigCap = (size_t)1 << 40;
        ok = ok && b && gGcRuns > gc0 && gGcAdjust == charged;   /* junk freed by the GC, b charged */
        if (b) {
            Obj o{ b, std::vector<unsigned char>((size_t)520 * 512 * 4, 0), 520, 512, false };
            const int f0 = gFails;
            checkRender(o, -1, -1);
            ok = ok && gFails == f0;
        }
        std::printf("%s  Bitmap.new with no room: a full GC gives memory back, reported bytes follow (%lld)\n", ok ? "PASS" : "FAIL", gGcAdjust);
        if (!ok) ++gFails;
        const size_t texBefore = gTex.size();
        const long long adjBefore = gGcAdjust;
        gBigCap = gBigUsed + kImg / 2;
        bool threw = false;
        try { Bitmap *d = new Bitmap(520, 512); delete d; } catch (const std::bad_alloc &) { threw = true; }
        gBigCap = (size_t)1 << 40;
        ok = threw && gTex.size() == texBefore && gGcAdjust == adjBefore;
        std::printf("%s  Bitmap.new with nothing to give back: bad_alloc, no texture, nothing reported (%zu -> %zu)\n",
                    ok ? "PASS" : "FAIL", texBefore, gTex.size());
        if (!ok) ++gFails;
        delete b;
        unsigned rOk = 0, rFail = 0;
        if (FILE *q = std::fopen((gRoot + "qa.log").c_str(), "r")) {
            char line[512];
            while (std::fgets(line, sizeof line, q))
                if (!std::strncmp(line, "NEW_RETRY", 9)) (std::strstr(line, " ok=1") ? rOk : rFail)++;
            std::fclose(q);
        }
        ok = rOk == 1 && rFail == 1 && gGcAdjust == 0;
        std::printf("%s  qa.log NEW_RETRY ok=1 once, ok=0 once (%u, %u); reported bytes back to 0 (%lld)\n", ok ? "PASS" : "FAIL", rOk, rFail, gGcAdjust);
        if (!ok) ++gFails;
    }
#endif
    auto R = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    std::vector<Obj> objs;
    std::map<std::pair<int, int>, int> cache;   /* (file, hue) -> objs index, as Cache keeps them */
    std::map<Bitmap *, GLuint> hueTex;   /* live hue clone -> texture it was last drawn with */
    unsigned hueRebuilt = 0;
    unsigned hueRebuildChecks = 0, retryCases = 0, escapes = 0, directedLoads = 0, oomErrors = 0;
    const int steps = argc > 2 ? std::atoi(argv[2]) : 6000;

    auto cacheGet = [&](int f, int hue) -> int {
        auto it = cache.find({ f, hue });
        if (it != cache.end())
            return it->second;
        auto base = cache.find({ f, 0 });
        int bi;
        if (base == cache.end()) {
            struct stat st;
            const bool entry = ::stat(VitaImageCache::entryPathFor(gRoot + "img" + std::to_string(f) + ".png").c_str(), &st) == 0;
            const bool roomy = gBigCap > gBigUsed + ((size_t)16 << 20) && gVglCap > gVglUsed + ((size_t)64 << 20);
            gMaxAlloc = 0;
            gInLoad = entry && roomy;
            Bitmap *nb = new Bitmap(("img" + std::to_string(f)).c_str());
            gInLoad = 0;
            if (entry && roomy) {   /* a cache hit: no CPU buffer of the decoded size (MKXP_VITA_TEX_DIRECT_CACHE) */
                ++gEntryLoads;
                if (gMaxAlloc < gFilePx[f].size()) ++gDirectLoads;
            }
            Obj o{ nb, gFilePx[f], gFileW[f], gFileH[f], false, true };
            objs.push_back(std::move(o));   /* a copy of the model can throw (fake pool) and orphan o.b */
            bi = (int)objs.size() - 1;
            cache[{ f, 0 }] = bi;
        } else {
            bi = base->second;
        }
        if (hue == 0)
            return bi;
        /* Cache.hue_changed_bitmap: normal_bitmap(path).clone.hue_change(hue) */
        /* the model's copy first: it can throw bad_alloc too (fake pool), which would orphan the clone */
        Obj o{ nullptr, objs[bi].exp, objs[bi].w, objs[bi].h, true };
        o.b = new Bitmap(*objs[bi].b);
        try {
            o.b->hueChange(hue);
        } catch (...) {   /* an escape (bad_alloc): the clone is the test's, not referenced anywhere */
            delete o.b;
            throw;
        }
        oracle::VitaBitmapCpu::hueChange(oracle::VitaBitmapCpu::Image{ o.w, o.h, o.exp.data() }, hue);
        objs.push_back(std::move(o));   /* a copy of the model can throw (fake pool) and orphan o.b */
        cache[{ f, hue }] = (int)objs.size() - 1;
        return (int)objs.size() - 1;
    };

    for (int step = 0; step < steps; ++step) {
        /* pressure phases: big pool tight / loose, vitaGL tight / loose */
        if (step % 400 == 0) {
            gBigCap = R(0, 2) ? gBigUsed + (size_t)R(2, 12) * kImg : (size_t)1 << 40;
            gVglCap = R(0, 1) ? gVglUsed + (size_t)R(45, 90) * kImg : (size_t)1 << 40;   /* paging keeps ~40-56 MiB free */
        }
        int op = R(0, 99);
        if (objs.size() > 60)
            op = 99;   /* keep the working set bounded: drop one */
        try {
            if (op < 14) {
                const int f = R(0, kFiles - 1);
                const int hue = R(0, 2) ? 0 : R(1, 359);
                cacheGet(f, hue);
            } else if (op < 22 && !objs.empty()) {
                /* a clone of anything (a hue clone of a hue clone keeps its hue) */
                const int i = R(0, (int)objs.size() - 1);
                Obj o{ nullptr, objs[i].exp, objs[i].w, objs[i].h, objs[i].hueClone };   /* model first (see cacheGet) */
                o.b = new Bitmap(*objs[i].b);
                objs.push_back(std::move(o));   /* a copy of the model can throw (fake pool) and orphan o.b */
            } else if (op < 27 && !objs.empty()) {
                /* a second hue_change (or a first one on a file bitmap) */
                const int i = R(0, (int)objs.size() - 1);
                const int hue = R(-400, 400);
                objs[i].b->hueChange(hue);
                objs[i].written = true;
                oracle::VitaBitmapCpu::hueChange(oracle::VitaBitmapCpu::Image{ objs[i].w, objs[i].h, objs[i].exp.data() }, hue);
            } else if (op < 40 && !objs.empty()) {
                const int i = R(0, (int)objs.size() - 1);
                checkPixel(objs[i], R(0, objs[i].w - 1), R(0, objs[i].h - 1), step, i);
            } else if (op < 44 && !objs.empty()) {
                /* a write: the bitmap is dirty from now on */
                const int i = R(0, (int)objs.size() - 1);
                const int x = R(0, objs[i].w - 1), y = R(0, objs[i].h - 1);
                const unsigned char c[4] = { (unsigned char)R(0, 255), (unsigned char)R(0, 255), (unsigned char)R(0, 255), (unsigned char)R(0, 255) };
                objs[i].b->setPixel(x, y, Color(c[0], c[1], c[2], c[3]));
                objs[i].written = true;
                std::memcpy(&objs[i].exp[((size_t)y * objs[i].w + x) * 4], c, 4);
            } else if (op < 52 && !objs.empty()) {
                /* blt from a bitmap into a fresh window-like bitmap: the result must be the oracle
                 * kernel's on the model's source pixels */
                const int i = R(0, (int)objs.size() - 1);
                const int w = R(1, 64), h = R(1, 64), sx = R(0, objs[i].w - 1), sy = R(0, objs[i].h - 1), dx = R(0, 10);
                Obj d{ new Bitmap(64, 64), std::vector<unsigned char>((size_t)64 * 64 * 4, 0), 64, 64, false };
                d.b->blt(dx, 0, *objs[i].b, IntRect(sx, sy, w, h), 255);
                oracle::VitaBitmapCpu::stretchBlt(oracle::VitaBitmapCpu::Image{ 64, 64, d.exp.data() }, dx, 0, w, h,
                                                  oracle::VitaBitmapCpu::ConstImage{ objs[i].w, objs[i].h, objs[i].exp.data() },
                                                  sx, sy, w, h, 255);
                checkRender(d, step, -1);
                delete d.b;
            } else if (op < 80 && !objs.empty()) {
                const int i = R(0, (int)objs.size() - 1);
                checkRender(objs[i], step, i);
                if (objs[i].hueClone) {   /* given back and rebuilt while alive: a new texture */
                    const GLuint t = objs[i].b->getGLTypes().tex.gl;
                    auto it = hueTex.find(objs[i].b);
                    if (it != hueTex.end() && it->second != t)
                        ++hueRebuilt;
                    hueTex[objs[i].b] = t;
                }
            } else if (op < 97) {
                const int frames = R(0, 3) ? R(1, 40) : R(100, 700);
                for (int k = 0; k < frames; ++k)
                    vitaTexPagingTick();
            } else if (!objs.empty()) {
                const int i = R(0, (int)objs.size() - 1);
                for (auto it = cache.begin(); it != cache.end();)
                    it = it->second == i ? cache.erase(it) : std::next(it);
                for (auto &kv : cache)
                    if (kv.second > i) --kv.second;
                hueTex.erase(objs[i].b);
                if (R(0, 1))
                    delete objs[i].b;               /* disposed */
                else
                    gGarbage.push_back(objs[i].b);  /* no longer referenced: freed by the next GC */
                objs.erase(objs.begin() + i);
            }
            /* directed: a CPU decode whose allocation fails although the pool said there was room */
            if (step % 97 == 0 && !objs.empty()) {
                const int i = R(0, (int)objs.size() - 1);
                for (int k = 0; k < 2; ++k)
                    vitaTexPagingTick();
                const unsigned before = gBigThrows;
                const size_t cap = gBigCap;
                gBigCap = gBigUsed + kImg / 2;   /* no room for one more copy */
                gStatsLie = true;
                checkPixel(objs[i], R(0, objs[i].w - 1), R(0, objs[i].h - 1), step, i);
                gStatsLie = false;
                gBigCap = cap;
                if (gBigThrows > before)
                    ++retryCases;
            }
            /* directed: a new image loaded with no room for it anywhere: the retry gives memory back */
            if (step % 131 == 0) {
                for (int k = 0; k < 2; ++k)
                    vitaTexPagingTick();
                const size_t bc = gBigCap, vc = gVglCap;
                gBigCap = gBigUsed + kImg / 4;
                gVglCap = gVglUsed + kImg / 4;
                const int f = R(0, kFiles - 1);
                try {
                    Obj o{ new Bitmap(("img" + std::to_string(f)).c_str()), gFilePx[f], gFileW[f], gFileH[f], false, true };
                    objs.push_back(std::move(o));   /* a copy of the model can throw (fake pool) and orphan o.b */
                    ++directedLoads;
                    checkRender(objs.back(), step, (int)objs.size() - 1);
                } catch (const Exception &e) {
                    ++oomErrors;
                    if (e.msg.find("not enough memory") == std::string::npos)
                        fail(e.msg.c_str(), step, -2);
                }
                gBigCap = bc;
                gVglCap = vc;
            }
        } catch (const Exception &e) {
            ++oomErrors;
            if (e.msg.find("not enough memory") == std::string::npos)
                fail(e.msg.c_str(), step, -3);
            gBigCap = gVglCap = (size_t)1 << 40;
        } catch (const std::bad_alloc &) {
            ++escapes;   /* only dirty copies or copies used in this frame left: nothing to give back */
            gStatsLie = false;
            gBigCap = (size_t)1 << 40;
        }
    }
    /* every live bitmap, at the end, after a long idle stretch under pressure */
    gBigCap = gBigUsed + 1;
    gVglCap = gVglUsed + (1 << 20);
    for (int k = 0; k < 1500; ++k)
        vitaTexPagingTick();
    gBigCap = (size_t)1 << 40;
    gVglCap = (size_t)1 << 40;
    for (size_t i = 0; i < objs.size(); ++i) {
        checkRender(objs[i], steps, (int)i);
        checkPixel(objs[i], R(0, objs[i].w - 1), R(0, objs[i].h - 1), steps, (int)i);
        if (objs[i].hueClone)
            ++hueRebuildChecks;
    }
    unsigned ev = 0, rs = 0, fl = 0, ok = 0, cd = 0, ck = 0;
    vitaTexPagingStats(&ev, &rs, &fl, &ok);
    vitaCpuPagingStats(&cd, &ck);
    std::printf("steps=%d objs=%zu evicted=%u restored=%u restore_fail=%u cpu_drop=%u (%u KiB) hue_rebuilt=%u "
                "hue_final_checks=%u retry_cases=%u restore_retries=%u big_throws=%u escapes=%u fails=%d\n",
                steps, objs.size(), ev, rs, fl, cd, ck, hueRebuilt, hueRebuildChecks, retryCases, gRestoreRetries, gBigThrows, escapes, gFails);
    for (auto &o : objs)
        delete o.b;
    rb_gc_start();
#ifdef MKXP_VITA_BITMAP_GC
    {
        unsigned logged = 0;
        if (FILE *q = std::fopen((gRoot + "qa.log").c_str(), "r")) {
            char line[512];
            while (std::fgets(line, sizeof line, q))
                if (!std::strncmp(line, "BITMAP_GC n=", 12)) ++logged;
            std::fclose(q);
        }
        const bool ok = gGcAdjust == 0 && gGcCharges > 100 && gReportGcs > 0 && logged == std::min(gReportGcs, 32u);
        std::printf("%s  every bitmap gone: bytes reported to the GC back to 0 (%lld, %u charges); %u major GCs past 16 MiB, %u in qa.log\n",
                    ok ? "PASS" : "FAIL", gGcAdjust, gGcCharges, gReportGcs, logged);
        if (!ok) ++gFails;
    }
#endif
    unsigned retryOk = 0, retryFail = 0;
    if (FILE *q = std::fopen((gRoot + "qa.log").c_str(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, q))
            if (!std::strncmp(line, "LOAD_RETRY", 10)) (std::strstr(line, " ok=1") ? retryOk : retryFail)++;
        std::fclose(q);
        std::remove((gRoot + "qa.log").c_str());
    }
    std::printf("cache_entry_loads=%u direct=%u directed_loads=%u load_retry_ok=%u load_retry_fail=%u oom_errors=%u gc_runs=%u tex_ptrs=%u cpu_tex_nomem=%u\n",
                gEntryLoads, gDirectLoads, directedLoads, retryOk, retryFail, oomErrors, gGcRuns, gDirectPtrs, gCpuTexNoMem);
    const bool covered = ev > 0 && rs > 0 && cd > 0 && hueRebuilt > 0 && retryCases > 0 &&
                         gEntryLoads > 0 && gDirectLoads == gEntryLoads && retryOk > 0 && gGcRuns > 1;
    if (!covered)
        std::printf("COVERAGE MISSING\n");
    std::printf("%s\n", gFails == 0 && covered ? "PASS" : "FAIL");
    return gFails == 0 && covered ? 0 : 1;
}
