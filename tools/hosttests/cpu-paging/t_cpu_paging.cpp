// Host test for MKXP_VITA_HUE_PAGING + MKXP_VITA_CPU_PAGING (with MKXP_VITA_TEX_PAGING): the real
// src/bitmap-vita-minimal.cpp, built for the host (run.sh), against
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
#include <vitaGL.h>
#include <psp2/ctrl.h>

namespace oracle {
#include "bitmap-vita-cpu.h"
}

/* ---------- fake big pool ---------- */
static size_t gBigCap = (size_t)1 << 40, gBigUsed = 0;
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
static size_t gVglFree = (size_t)1 << 30;
extern "C" size_t vglMemFree(vglMemType) { return gVglFree / 3; }
extern "C" void *vglGetTexDataPointer(GLenum) { return nullptr; }
extern "C" int sceCtrlPeekBufferPositive(int, SceCtrlData *, int) { return 0; }

/* ---------- fake GL ---------- */
struct FakeTex { int w = 0, h = 0; std::vector<unsigned char> px; };
static std::map<GLuint, FakeTex> gTex;
static GLuint gNextTex = 1, gBound = 0;
static std::map<GLuint, int> gDeleted;   /* tex id -> deletions */
static void APIENTRY fGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; ++i) { t[i] = gNextTex++; gTex[t[i]]; } }
static void APIENTRY fDeleteTextures(GLsizei n, const GLuint *t) { for (int i = 0; i < n; ++i) { gTex.erase(t[i]); ++gDeleted[t[i]]; } }
static void APIENTRY fBindTexture(GLenum, GLuint t) { gBound = t; }
static void APIENTRY fTexImage2D(GLenum, GLint, GLint, GLsizei w, GLsizei h, GLint, GLenum, GLenum, const GLvoid *d)
{
    FakeTex &t = gTex.at(gBound);
    t.w = w; t.h = h;
    t.px.assign((size_t)w * h * 4, 0);
    if (d)
        std::memcpy(t.px.data(), d, t.px.size());
}
static void APIENTRY fTexSubImage2D(GLenum, GLint, GLint x, GLint y, GLsizei w, GLsizei h, GLenum, GLenum, const GLvoid *d)
{
    FakeTex &t = gTex.at(gBound);
    for (int r = 0; r < h; ++r)
        std::memcpy(t.px.data() + ((size_t)(y + r) * t.w + x) * 4, (const unsigned char *)d + (size_t)r * w * 4, (size_t)w * 4);
}
static void APIENTRY fTexParameteri(GLenum, GLenum, GLint) {}
static void APIENTRY fActiveTexture(GLenum) {}
static void APIENTRY fBindFramebuffer(GLenum, GLuint) {}
static void APIENTRY fGetIntegerv(GLenum p, GLint *v) { *v = p == GL_TEXTURE_BINDING_2D ? (GLint)gBound : 0; }
static void APIENTRY fDeleteFramebuffers(GLsizei, const GLuint *) {}
GLFunctions gl;

/* ---------- the rest of what bitmap-vita-minimal.cpp links against ---------- */
SharedState *SharedState::instance = nullptr;
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

extern "C" void vitaTexPagingTick();
extern "C" void vitaTexPagingStats(unsigned *evicted, unsigned *restored, unsigned *fails, unsigned *outKb);
extern "C" void vitaCpuPagingStats(unsigned *dropped, unsigned *droppedKb);

/* ---------- images ---------- */
static std::string gRoot;
static std::vector<std::vector<unsigned char>> gFilePx;
static const int kW = 256, kH = 192;   /* 192 KiB: a big-pool block */

static void writePng(const std::string &path, std::mt19937 &rng)
{
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
struct Obj { Bitmap *b; std::vector<unsigned char> exp; int w, h; bool hueClone; };
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
        const size_t cap = gBigCap;
        gBigCap = (size_t)1 << 40;
        for (int k = 0; k < 61; ++k)
            vitaTexPagingTick();
        t = &o.b->getGLTypes();
        gBigCap = cap;
    }
    auto it = gTex.find(t->tex.gl);
    if (it == gTex.end() || it->second.px != o.exp)
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

    std::mt19937 rng(argc > 1 ? std::atoi(argv[1]) : 1);
    const int kFiles = 6;
    for (int i = 0; i < kFiles; ++i)
        writePng(gRoot + "img" + std::to_string(i) + ".png", rng);

    auto R = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    std::vector<Obj> objs;
    std::map<std::pair<int, int>, int> cache;   /* (file, hue) -> objs index, as Cache keeps them */
    std::map<Bitmap *, GLuint> hueTex;   /* live hue clone -> texture it was last drawn with */
    unsigned hueRebuilt = 0;
    unsigned hueRebuildChecks = 0, retryCases = 0, escapes = 0;
    const int steps = argc > 2 ? std::atoi(argv[2]) : 6000;

    auto cacheGet = [&](int f, int hue) -> int {
        auto it = cache.find({ f, hue });
        if (it != cache.end())
            return it->second;
        auto base = cache.find({ f, 0 });
        int bi;
        if (base == cache.end()) {
            Obj o{ new Bitmap(("img" + std::to_string(f)).c_str()), gFilePx[f], kW, kH, false };
            objs.push_back(o);
            bi = (int)objs.size() - 1;
            cache[{ f, 0 }] = bi;
        } else {
            bi = base->second;
        }
        if (hue == 0)
            return bi;
        /* Cache.hue_changed_bitmap: normal_bitmap(path).clone.hue_change(hue) */
        Obj o{ new Bitmap(*objs[bi].b), objs[bi].exp, kW, kH, true };
        o.b->hueChange(hue);
        oracle::VitaBitmapCpu::hueChange(oracle::VitaBitmapCpu::Image{ kW, kH, o.exp.data() }, hue);
        objs.push_back(o);
        cache[{ f, hue }] = (int)objs.size() - 1;
        return (int)objs.size() - 1;
    };

    for (int step = 0; step < steps; ++step) {
        /* pressure phases: big pool tight / loose, vitaGL tight / loose */
        if (step % 400 == 0) {
            gBigCap = R(0, 2) ? gBigUsed + (size_t)R(2, 12) * kW * kH * 4 : (size_t)1 << 40;
            gVglFree = R(0, 1) ? (size_t)1 << 20 : (size_t)1 << 30;
        }
        const int op = R(0, 99);
        try {
            if (op < 14) {
                const int f = R(0, kFiles - 1);
                const int hue = R(0, 2) ? 0 : R(1, 359);
                cacheGet(f, hue);
            } else if (op < 22 && !objs.empty()) {
                /* a clone of anything (a hue clone of a hue clone keeps its hue) */
                const int i = R(0, (int)objs.size() - 1);
                Obj o{ new Bitmap(*objs[i].b), objs[i].exp, objs[i].w, objs[i].h, objs[i].hueClone };
                objs.push_back(o);
            } else if (op < 27 && !objs.empty()) {
                /* a second hue_change (or a first one on a file bitmap) */
                const int i = R(0, (int)objs.size() - 1);
                const int hue = R(-400, 400);
                objs[i].b->hueChange(hue);
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
                delete objs[i].b;
                objs.erase(objs.begin() + i);
            }
            /* directed: a CPU decode whose allocation fails although the pool said there was room */
            if (step % 97 == 0 && !objs.empty()) {
                const int i = R(0, (int)objs.size() - 1);
                for (int k = 0; k < 2; ++k)
                    vitaTexPagingTick();
                const unsigned before = gBigThrows;
                const size_t cap = gBigCap;
                gBigCap = gBigUsed + (size_t)kW * kH * 4 / 2;   /* no room for one more copy */
                gStatsLie = true;
                checkPixel(objs[i], R(0, objs[i].w - 1), R(0, objs[i].h - 1), step, i);
                gStatsLie = false;
                gBigCap = cap;
                if (gBigThrows > before)
                    ++retryCases;
            }
        } catch (const std::bad_alloc &) {
            ++escapes;   /* only dirty copies or copies used in this frame left: nothing to give back */
            gStatsLie = false;
            gBigCap = (size_t)1 << 40;
        }
    }
    /* every live bitmap, at the end, after a long idle stretch under pressure */
    gBigCap = gBigUsed + 1;
    gVglFree = 1 << 20;
    for (int k = 0; k < 1500; ++k)
        vitaTexPagingTick();
    gBigCap = (size_t)1 << 40;
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
    const bool covered = ev > 0 && rs > 0 && cd > 0 && hueRebuilt > 0 && retryCases > 0;
    if (!covered)
        std::printf("COVERAGE MISSING\n");
    std::printf("%s\n", gFails == 0 && covered ? "PASS" : "FAIL");
    return gFails == 0 && covered ? 0 : 1;
}
