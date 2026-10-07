#include "gl-fun.h"
#include "vita_paths.h"
#include "vita_perf.h"
#include "vita_diag.h"
#ifdef MKXP_VITA_DIAG
#include <cstdio>
#endif

GLFunctions gl = {};

#ifdef MKXP_VITA_PERF_PROFILE
/* Profiling only: count the calls mkxp makes through the gl table, then forward to vitaGL. */
static void APIENTRY vitaPerfDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{ VITA_PERF_COUNT(DRAW_ELEMENTS); ::glDrawElements(mode, count, type, indices); }
static void APIENTRY vitaPerfBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,
                                             GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{ VITA_PERF_COUNT(BLIT_FRAMEBUFFER); ::glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter); }
static void APIENTRY vitaPerfBindTexture(GLenum target, GLuint texture)
{ VITA_PERF_COUNT(TEXTURE_BINDS); ::glBindTexture(target, texture); }
static void APIENTRY vitaPerfUseProgram(GLuint program)
{ VITA_PERF_COUNT(PROGRAM_BINDS); ::glUseProgram(program); }
static void APIENTRY vitaPerfBindFramebuffer(GLenum target, GLuint framebuffer)
{ VITA_PERF_COUNT(FBO_BINDS); ::glBindFramebuffer(target, framebuffer); }
static void APIENTRY vitaPerfBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{ VITA_PERF_COUNT(BUFFER_DATA); ::glBufferData(target, size, data, usage); }
static void APIENTRY vitaPerfBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{ VITA_PERF_COUNT(BUFFER_SUBDATA); ::glBufferSubData(target, offset, size, data); }
static void APIENTRY vitaPerfTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                                        GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{ VITA_PERF_COUNT(TEX_IMAGE); ::glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels); }
static void APIENTRY vitaPerfTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                           GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{ VITA_PERF_COUNT(TEX_SUBIMAGE); ::glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); }
#endif

#ifdef MKXP_VITA_GL_NOOP_SKIP
/*
 * Fix (MKXP_VITA_GL_NOOP_SKIP): mkxp-z draws empty quad arrays (count 0) every frame and uploads
 * empty ranges (size 0) when a sprite is created; GL treats both as no-ops, vitaGL also skips them
 * but records GL_INVALID_VALUE and, with LOG_ERRORS, logs a line (d49: file writes at every step).
 * Skipped here, before vitaGL: same result, no error.
 */
static _PFNGLDRAWELEMENTSPROC vitaNoopPrevDrawElements;
static _PFNGLBUFFERSUBDATAPROC vitaNoopPrevBufferSubData;
static void APIENTRY vitaNoopDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{ if (count > 0) vitaNoopPrevDrawElements(mode, count, type, indices); }
static void APIENTRY vitaNoopBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{ if (size > 0) vitaNoopPrevBufferSubData(target, offset, size, data); }
#endif

#ifdef MKXP_VITA_DIAG
/* Diagnostic only (vita_diag.h): time shader compile/link and texture uploads through the gl table.
 * Installed after the PERF thunks, so they also keep the PERF counters. */
static void APIENTRY vitaDiagCompileShader(GLuint shader)
{ VITA_DIAG_T0(t0); ::glCompileShader(shader); VITA_DIAG_SPAN(VD_GL_COMPILE, t0, 0, nullptr); }
static void APIENTRY vitaDiagLinkProgram(GLuint program)
{ VITA_DIAG_T0(t0); ::glLinkProgram(program); VITA_DIAG_SPAN(VD_GL_LINK, t0, 0, nullptr); }
static void APIENTRY vitaDiagTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                                        GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    VITA_PERF_COUNT(TEX_IMAGE);
    VITA_DIAG_T0(t0);
    ::glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    VITA_DIAG_SPAN(VD_GL_TEXIMAGE, t0, (uint64_t)width * height * 4, pixels ? "data" : "alloc");
}
static void APIENTRY vitaDiagTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                           GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    VITA_PERF_COUNT(TEX_SUBIMAGE);
    VITA_DIAG_T0(t0);
    ::glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    VITA_DIAG_SPAN(VD_GL_TEXSUBIMAGE, t0, (uint64_t)width * height * 4, nullptr);
}
/* Framebuffer objects alive (vitaGL has a fixed pool of BUFFERS_NUM = 256) and failed generations. */
extern "C" {
int vitaDiagFboLive = 0;
int vitaDiagFboGenFail = 0;
}
static void APIENTRY vitaDiagGenFramebuffers(GLsizei n, GLuint *ids)
{
    for (GLsizei i = 0; i < n; ++i)
        ids[i] = 0;
    ::glGenFramebuffers(n, ids);
    for (GLsizei i = 0; i < n; ++i) {
        if (ids[i]) {
            ++vitaDiagFboLive;
        } else if (vitaDiagFboGenFail++ == 0) {
            char d[48];
            snprintf(d, sizeof(d), "live=%d", vitaDiagFboLive);
            VITA_DIAG_MARK("FBO_GEN_FAILED", d);
            FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
            if (f) { fprintf(f, "FBO_GEN_FAILED first live=%d\n", vitaDiagFboLive); fclose(f); }
        }
    }
}
/*
 * Framebuffers ever bound for drawing (vitaGL gives each one a dedicated GXM render target on first
 * use; about 47 is the practical limit) - the live count and the maximum reached.
 */
#include <set>
static std::set<GLuint> vitaDiagRtFbos;
extern "C" {
int vitaDiagFboRtLive = 0;
int vitaDiagFboRtMax = 0;
}
static void APIENTRY vitaDiagBindFramebuffer(GLenum target, GLuint framebuffer)
{
    VITA_PERF_COUNT(FBO_BINDS);
    ::glBindFramebuffer(target, framebuffer);
    if (framebuffer && target != GL_READ_FRAMEBUFFER && vitaDiagRtFbos.insert(framebuffer).second) {
        vitaDiagFboRtLive = (int)vitaDiagRtFbos.size();
        if (vitaDiagFboRtLive > vitaDiagFboRtMax) {
            vitaDiagFboRtMax = vitaDiagFboRtLive;
            if (vitaDiagFboRtMax >= 40) {
                FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
                if (f) { fprintf(f, "FBO_RT_LIVE_MAX %d (fbo_live=%d)\n", vitaDiagFboRtMax, vitaDiagFboLive); fclose(f); }
            }
        }
    }
}
static void APIENTRY vitaDiagDeleteFramebuffers(GLsizei n, const GLuint *ids)
{
    for (GLsizei i = 0; i < n; ++i)
        if (ids[i]) {
            --vitaDiagFboLive;
            vitaDiagRtFbos.erase(ids[i]);
        }
    vitaDiagFboRtLive = (int)vitaDiagRtFbos.size();
    ::glDeleteFramebuffers(n, ids);
}
#endif

#ifdef MKXP_VITA_DEFERRED_GL_DELETE
#include <vector>
namespace {
struct VitaPendingDelete { GLuint tex, fbo; unsigned int swap; };
std::vector<VitaPendingDelete> gVitaPendingDeletes;
unsigned int gVitaSwapCount = 0;
const unsigned int kVitaDeleteAfterSwaps = 6;  /* vitaGL FRAME_PURGE_FREQ (4) + margin */
}

void vitaDeferTexFboDelete(GLuint tex, GLuint fbo)
{
    /* The FBO goes at once: vitaGL already delays the destruction of its render target by
     * FRAME_PURGE_FREQ swaps (GC), and render targets are a scarce resource. Only the texture memory,
     * which vitaGL may free immediately while the GPU still renders into it, is delayed. */
    if (fbo)
        gl.DeleteFramebuffers(1, &fbo);
    if (tex)
        gVitaPendingDeletes.push_back(VitaPendingDelete{ tex, 0, gVitaSwapCount });
}

int vitaDeferredGLPending()
{
    return (int)gVitaPendingDeletes.size();
}

#ifdef MKXP_VITA_ATLAS_PARK
/* Swaps so far (sharedstate-vita.cpp: age of a parked tile atlas). */
extern "C" unsigned int vitaSwapCountNow(void)
{
    return gVitaSwapCount;
}
#endif

#ifdef MKXP_VITA_GL_LEDGER
extern "C" void vitaGlLedgerSwapCheck();
#endif
void vitaDeferredGLDeleteTick()
{
#ifdef MKXP_VITA_GL_LEDGER
    vitaGlLedgerSwapCheck();   /* vitaGL allocations >= 4 MiB during the swap / outside wrapped calls */
#endif
    ++gVitaSwapCount;
    size_t keep = 0;
    for (size_t i = 0; i < gVitaPendingDeletes.size(); ++i) {
        VitaPendingDelete &d = gVitaPendingDeletes[i];
        if (gVitaSwapCount - d.swap >= kVitaDeleteAfterSwaps) {
            if (d.fbo)
                gl.DeleteFramebuffers(1, &d.fbo);
            if (d.tex)
                gl.DeleteTextures(1, &d.tex);
        } else {
            gVitaPendingDeletes[keep++] = d;
        }
    }
    gVitaPendingDeletes.resize(keep);
}

#ifdef MKXP_VITA_SCENE_FLUSH
/*
 * Fix (MKXP_VITA_SCENE_FLUSH): at the start of every scene (Scene_Base#main, before the new scene
 * creates its resources), give back the GPU memory the previous scene released. Disposed textures
 * wait 6 swaps in our queue and then FRAME_PURGE_FREQ (4) swaps in vitaGL's purge ring, so the new
 * scene allocated while the old one's memory was still held. d61: after a battle that filled the
 * VRAM, RAM and PHYCONT pools, the map's textures (tile atlases) went to VGL_MEM_EXTERNAL = the
 * newlib heap (+25 MiB, never returned) and Ruby hit NoMemoryError at the next transfer.
 * The screen is frozen (Graphics.freeze) at that point: waiting for the GPU (glFinish) costs about one
 * frame and makes it safe to delete the queued textures and run the whole purge ring at once.
 */
#include <vitaGL.h>
#include <cstdio>
extern "C" int garbage_collector(unsigned int args, void *arg);   /* vitaGL gxm.c (SINGLE_THREADED_GC) */
extern "C" void vitaSceneGpuFlush(void)
{
    static int logged = 0;
    const size_t ram0 = vglMemFree(VGL_MEM_RAM), vram0 = vglMemFree(VGL_MEM_VRAM), pc0 = vglMemFree(VGL_MEM_PHYCONT);
    const size_t pending = gVitaPendingDeletes.size();
    glFinish();
    for (size_t i = 0; i < gVitaPendingDeletes.size(); ++i) {
        VitaPendingDelete &d = gVitaPendingDeletes[i];
        if (d.fbo)
            gl.DeleteFramebuffers(1, &d.fbo);
        if (d.tex)
            gl.DeleteTextures(1, &d.tex);
    }
    gVitaPendingDeletes.clear();
    for (int i = 0; i < 4; ++i)   /* FRAME_PURGE_FREQ: every slot of the purge ring */
        garbage_collector(0, nullptr);
    if (logged < 40) {
        ++logged;
        FILE *f = std::fopen(VITA_GAME_ROOT "qa.log", "a");
        if (f) {
            std::fprintf(f, "SCENE_FLUSH pending=%u ram_kb=%u->%u vram_kb=%u->%u phycont_kb=%u->%u\n", (unsigned)pending,
                         (unsigned)(ram0 / 1024), (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024), (unsigned)(vram0 / 1024),
                         (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024), (unsigned)(pc0 / 1024),
                         (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024));
            std::fclose(f);
        }
    }
}
#endif
#endif

#ifdef MKXP_VITA_VITAGL_DIAG
/*
 * Diagnostic only: log sink of the local diagnostic vitaGL (deps/vitaGL, built with
 * LOG_ERRORS=1 MKXP_DIAG=1): its vgl_log() error lines go to <game root>/vitagl.log (open, append,
 * close per line). Each line gets the process time; a line identical to the previous one is only
 * counted ("REPEATED n") so harmless spam cannot fill the 2000-line cap and hide later errors
 * (d18: glDrawElements count 0 warnings filled the cap). vgl_log may be called from vitaGL's
 * garbage collector thread, hence the lock.
 */
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <psp2/kernel/processmgr.h>
#include <pthread.h>
static pthread_mutex_t vglAppLogLock = PTHREAD_MUTEX_INITIALIZER;
static char vglAppLogPrev[256];
static unsigned vglAppLogRepeats = 0;
static int vglAppLogLines = 0;
#ifdef MKXP_VITA_VGL_LOG_BUFFER
static char vglAppLogBuf[16384];
static int vglAppLogLen = 0;
/* Called once per PERF window from the main thread (vita_diag.cpp). */
extern "C" void vitaVglLogFlush(void)
{
    static char out[sizeof(vglAppLogBuf)];
    pthread_mutex_lock(&vglAppLogLock);
    const int n = vglAppLogLen;
    if (n) std::memcpy(out, vglAppLogBuf, (size_t)n);
    vglAppLogLen = 0;
    pthread_mutex_unlock(&vglAppLogLock);
    if (!n) return;
    FILE *f = fopen(VITA_GAME_ROOT "vitagl.log", "a");
    if (f) { fwrite(out, 1, (size_t)n, f); fclose(f); }
}
#endif
extern "C" void vgl_app_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&vglAppLogLock);
    if (!std::strcmp(line, vglAppLogPrev)) {
        ++vglAppLogRepeats;
        pthread_mutex_unlock(&vglAppLogLock);
        return;
    }
    if (vglAppLogLines < 2000) {
#ifdef MKXP_VITA_VGL_LOG_BUFFER
        /* Perf fix (MKXP_VITA_VGL_LOG_BUFFER): lines kept in RAM, written once per PERF window by
         * vitaVglLogFlush(). d49: the open/append/close per new line ran inside prepareDraw and
         * composite at every walking step (~13 ms each). */
        const int room = (int)sizeof(vglAppLogBuf) - vglAppLogLen;
        int n = 0;
        if (vglAppLogRepeats && room > 40)
            n += snprintf(vglAppLogBuf + vglAppLogLen, room, "  REPEATED %u more times\n", vglAppLogRepeats);
        if (room - n > 300)
            n += snprintf(vglAppLogBuf + vglAppLogLen + n, room - n, "[%llu] %s",
                          (unsigned long long)sceKernelGetProcessTimeWide(), line);
        vglAppLogLen += n;
#else
        FILE *f = fopen(VITA_GAME_ROOT "vitagl.log", "a");
        if (f) {
            if (vglAppLogRepeats)
                fprintf(f, "  REPEATED %u more times\n", vglAppLogRepeats);
            fprintf(f, "[%llu] %s", (unsigned long long)sceKernelGetProcessTimeWide(), line);
            fclose(f);
        }
#endif
        ++vglAppLogLines;
    }
    vglAppLogRepeats = 0;
    std::strcpy(vglAppLogPrev, line);
    pthread_mutex_unlock(&vglAppLogLock);
}
#endif

#ifdef MKXP_VITA_GL_LEDGER
/*
 * Diagnostic only (MKXP_VITA_GL_LEDGER): accounting of every texture / buffer created through the
 * mkxp gl table, with its size, age and creator (return address of the gl.GenTextures/GenBuffers
 * call, i.e. the function that inlined TEX::gen / VBO::gen). gl_ledger.log groups the live
 * non-Bitmap objects by creator, to name what keeps vitaGL memory that no live Bitmap owns.
 * Main thread only (all mkxp GL calls are). Installed last: chains to the previous thunks.
 */
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <malloc.h>
namespace {
struct LedgerObj { uintptr_t creator; uint32_t bytes; uint16_t w, h; uint32_t bornS; };
std::unordered_map<GLuint, LedgerObj> gLTex, gLBuf;
GLuint gLBoundTex[16];
int gLUnit = 0;
GLuint gLBoundArray = 0, gLBoundElem = 0;
uint64_t gLTexBytes = 0, gLBufBytes = 0;
uint64_t gLLastDumpBytes = 0;
int gLDumps = 0;
_PFNGLGENTEXTURESPROC gLPrevGenTextures;
_PFNGLDELETETEXTURESPROC gLPrevDeleteTextures;
_PFNGLACTIVETEXTUREPROC gLPrevActiveTexture;
_PFNGLBINDTEXTUREPROC gLPrevBindTexture;
_PFNGLTEXIMAGE2DPROC gLPrevTexImage2D;
_PFNGLGENBUFFERSPROC gLPrevGenBuffers;
_PFNGLDELETEBUFFERSPROC gLPrevDeleteBuffers;
_PFNGLBINDBUFFERPROC gLPrevBindBuffer;
_PFNGLBUFFERDATAPROC gLPrevBufferData;
uint32_t nowS() { return (uint32_t)(sceKernelGetProcessTimeWide() / 1000000); }
uint32_t bytesPerPixel(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
        return 2;
    switch (format) {
    case GL_RGB: return 3;
    case GL_ALPHA: case GL_LUMINANCE: return 1;
    case GL_LUMINANCE_ALPHA: return 2;
    default: return 4;
    }
}
}

/*
 * Attribution of vitaGL allocations >= 4 MiB (vitaGL-vita-d21: vgl_diag_bigalloc_count, weak so the
 * ledger also links against a vitaGL without it): a GL call during which the counter moved is logged
 * with the mkxp caller and the texture involved; what happens outside these calls is reported once
 * per swap as unattributed.
 */
extern "C" __attribute__((weak)) volatile unsigned int vgl_diag_bigalloc_count;
extern "C" __attribute__((weak)) volatile unsigned int vgl_diag_bigalloc_last_size;
namespace {
unsigned gLBigSeen = 0;
int gLBigLines = 0;
_PFNGLTEXSUBIMAGE2DPROC gLPrevTexSubImage2D;
_PFNGLDRAWELEMENTSPROC gLPrevDrawElements;
_PFNGLBLITFRAMEBUFFERPROC gLPrevBlitFramebuffer;
_PFNGLBINDFRAMEBUFFERPROC gLPrevBindFramebuffer;
_PFNGLFRAMEBUFFERTEXTURE2DPROC gLPrevFramebufferTexture2D;
_PFNGLCLEARPROC gLPrevClear;
inline unsigned bigCount() { return &vgl_diag_bigalloc_count ? vgl_diag_bigalloc_count : 0; }
void bigLog(const char *line)
{
    if (gLBigLines >= 400)
        return;
    ++gLBigLines;
    FILE *f = fopen(VITA_GAME_ROOT "gl_ledger.log", "a");
    if (!f)
        return;
    fputs(line, f);
    fclose(f);
}
void bigCheck(const char *fn, uintptr_t caller, GLuint tex)
{
    const unsigned c = bigCount();
    if (c == gLBigSeen)
        return;
    const unsigned n = c - gLBigSeen;
    gLBigSeen = c;
    char line[256];
    auto it = gLTex.find(tex);
    if (it != gLTex.end())
        snprintf(line, sizeof(line), "BIGALLOC_BY t_s=%u fn=%s n=%u last_size=%u caller=%p tex=%u %ux%u tex_creator=%p\n",
                 nowS(), fn, n, vgl_diag_bigalloc_last_size, (void *)caller, tex, it->second.w, it->second.h,
                 (void *)it->second.creator);
    else
        snprintf(line, sizeof(line), "BIGALLOC_BY t_s=%u fn=%s n=%u last_size=%u caller=%p tex=%u\n", nowS(), fn, n,
                 vgl_diag_bigalloc_last_size, (void *)caller, tex);
    bigLog(line);
}
}
#define VITA_BIG_BEGIN() const unsigned vitaBig0 = bigCount(); (void)vitaBig0; gLBigSeen = vitaBig0

static void APIENTRY vitaLedgerGenTextures(GLsizei n, GLuint *ids)
{
    const uintptr_t creator = (uintptr_t)__builtin_return_address(0);
    gLPrevGenTextures(n, ids);
    for (GLsizei i = 0; i < n; ++i)
        if (ids[i])
            gLTex[ids[i]] = LedgerObj{ creator, 0, 0, 0, nowS() };
}
static void APIENTRY vitaLedgerDeleteTextures(GLsizei n, const GLuint *ids)
{
    for (GLsizei i = 0; i < n; ++i) {
        auto it = gLTex.find(ids[i]);
        if (it != gLTex.end()) {
            gLTexBytes -= it->second.bytes;
            gLTex.erase(it);
        }
        for (GLuint &b : gLBoundTex)
            if (b == ids[i])
                b = 0;
    }
    gLPrevDeleteTextures(n, ids);
}
static void APIENTRY vitaLedgerActiveTexture(GLenum unit)
{
    gLUnit = (int)(unit - GL_TEXTURE0) & 15;
    gLPrevActiveTexture(unit);
}
static void APIENTRY vitaLedgerBindTexture(GLenum target, GLuint tex)
{
    gLBoundTex[gLUnit] = tex;
    gLPrevBindTexture(target, tex);
}
static void APIENTRY vitaLedgerTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                                          GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    VITA_BIG_BEGIN();
    gLPrevTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    bigCheck("TexImage2D", (uintptr_t)__builtin_return_address(0), gLBoundTex[gLUnit]);
    if (level != 0)
        return;
    auto it = gLTex.find(gLBoundTex[gLUnit]);
    if (it == gLTex.end())
        return;
    const uint32_t bytes = (uint32_t)width * (uint32_t)height * bytesPerPixel(format, type);
    gLTexBytes += bytes;
    gLTexBytes -= it->second.bytes;
    it->second.bytes = bytes;
    it->second.w = (uint16_t)width;
    it->second.h = (uint16_t)height;
}
static void APIENTRY vitaLedgerGenBuffers(GLsizei n, GLuint *ids)
{
    const uintptr_t creator = (uintptr_t)__builtin_return_address(0);
    gLPrevGenBuffers(n, ids);
    for (GLsizei i = 0; i < n; ++i)
        if (ids[i])
            gLBuf[ids[i]] = LedgerObj{ creator, 0, 0, 0, nowS() };
}
static void APIENTRY vitaLedgerDeleteBuffers(GLsizei n, const GLuint *ids)
{
    for (GLsizei i = 0; i < n; ++i) {
        auto it = gLBuf.find(ids[i]);
        if (it != gLBuf.end()) {
            gLBufBytes -= it->second.bytes;
            gLBuf.erase(it);
        }
        if (gLBoundArray == ids[i]) gLBoundArray = 0;
        if (gLBoundElem == ids[i]) gLBoundElem = 0;
    }
    gLPrevDeleteBuffers(n, ids);
}
static void APIENTRY vitaLedgerBindBuffer(GLenum target, GLuint buf)
{
    if (target == GL_ARRAY_BUFFER) gLBoundArray = buf;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) gLBoundElem = buf;
    gLPrevBindBuffer(target, buf);
}
static void APIENTRY vitaLedgerBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    gLPrevBufferData(target, size, data, usage);
    const GLuint id = target == GL_ARRAY_BUFFER ? gLBoundArray : (target == GL_ELEMENT_ARRAY_BUFFER ? gLBoundElem : 0);
    auto it = gLBuf.find(id);
    if (it == gLBuf.end())
        return;
    gLBufBytes += (uint32_t)size;
    gLBufBytes -= it->second.bytes;
    it->second.bytes = (uint32_t)size;
}

static void APIENTRY vitaLedgerTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                             GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    VITA_BIG_BEGIN();
    gLPrevTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    bigCheck("TexSubImage2D", (uintptr_t)__builtin_return_address(0), gLBoundTex[gLUnit]);
}
static void APIENTRY vitaLedgerDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    VITA_BIG_BEGIN();
    gLPrevDrawElements(mode, count, type, indices);
    bigCheck("DrawElements", (uintptr_t)__builtin_return_address(0), gLBoundTex[0]);
}
static void APIENTRY vitaLedgerBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,
                                               GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{
    VITA_BIG_BEGIN();
    gLPrevBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    bigCheck("BlitFramebuffer", (uintptr_t)__builtin_return_address(0), 0);
}
static void APIENTRY vitaLedgerBindFramebuffer(GLenum target, GLuint fbo)
{
    VITA_BIG_BEGIN();
    gLPrevBindFramebuffer(target, fbo);
    bigCheck("BindFramebuffer", (uintptr_t)__builtin_return_address(0), 0);
}
static void APIENTRY vitaLedgerFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                                    GLint level)
{
    VITA_BIG_BEGIN();
    gLPrevFramebufferTexture2D(target, attachment, textarget, texture, level);
    bigCheck("FramebufferTexture2D", (uintptr_t)__builtin_return_address(0), texture);
}
static void APIENTRY vitaLedgerClear(GLbitfield mask)
{
    VITA_BIG_BEGIN();
    gLPrevClear(mask);
    bigCheck("Clear", (uintptr_t)__builtin_return_address(0), 0);
}

/* Once per swap (vitaDeferredGLDeleteTick / PERF): allocations outside the wrapped GL calls. */
extern "C" void vitaGlLedgerSwapCheck()
{
    bigCheck("outside_wrapped_calls(swap/other)", 0, 0);
}

extern "C" void vitaGlLedgerTotals(int *ntex, uint64_t *texBytes, int *nbuf, uint64_t *bufBytes)
{
    *ntex = (int)gLTex.size();
    *texBytes = gLTexBytes;
    *nbuf = (int)gLBuf.size();
    *bufBytes = gLBufBytes;
}

/* bitmap-vita.cpp (MKXP_VITA_PERF_BITMAP): texture names of the live Bitmaps. */
extern "C" void vitaDiagBitmapTexIds(void (*cb)(unsigned int tex, void *ctx), void *ctx);

extern "C" void vitaGlLedgerDump(const char *reason)
{
    std::unordered_set<GLuint> bitmapTex;
    vitaDiagBitmapTexIds([](unsigned int tex, void *ctx) { static_cast<std::unordered_set<GLuint> *>(ctx)->insert(tex); },
                         &bitmapTex);
    struct Group { uintptr_t creator; unsigned n; uint64_t bytes; };
    std::vector<Group> groups;
    std::vector<std::pair<GLuint, const LedgerObj *>> other;
    uint64_t bmpBytes = 0, otherBytes = 0;
    unsigned bmpN = 0;
    for (const auto &kv : gLTex) {
        if (bitmapTex.count(kv.first)) {
            bmpBytes += kv.second.bytes;
            ++bmpN;
            continue;
        }
        otherBytes += kv.second.bytes;
        other.push_back({ kv.first, &kv.second });
        auto g = std::find_if(groups.begin(), groups.end(), [&](const Group &x) { return x.creator == kv.second.creator; });
        if (g == groups.end())
            groups.push_back(Group{ kv.second.creator, 1, kv.second.bytes });
        else {
            ++g->n;
            g->bytes += kv.second.bytes;
        }
    }
    std::sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) { return a.bytes > b.bytes; });
    std::sort(other.begin(), other.end(), [](const auto &a, const auto &b) { return a.second->bytes > b.second->bytes; });
    std::vector<Group> bgroups;
    for (const auto &kv : gLBuf) {
        auto g = std::find_if(bgroups.begin(), bgroups.end(), [&](const Group &x) { return x.creator == kv.second.creator; });
        if (g == bgroups.end())
            bgroups.push_back(Group{ kv.second.creator, 1, kv.second.bytes });
        else {
            ++g->n;
            g->bytes += kv.second.bytes;
        }
    }
    std::sort(bgroups.begin(), bgroups.end(), [](const Group &a, const Group &b) { return a.bytes > b.bytes; });

    FILE *f = fopen(VITA_GAME_ROOT "gl_ledger.log", "a");
    if (!f)
        return;
    const struct mallinfo mi = mallinfo();
    fprintf(f, "===== LEDGER #%d t_s=%u reason=%s anchor=%p =====\n", gLDumps++, nowS(), reason ? reason : "",
            (void *)&vitaGlLedgerDump);
    fprintf(f, "tex_n=%u tex_kb=%llu bitmap_tex_n=%u bitmap_tex_kb=%llu other_tex_n=%u other_tex_kb=%llu buf_n=%u buf_kb=%llu"
               " vgl_free_ram_kb=%u vgl_free_vram_kb=%u vgl_free_phycont_kb=%u heap_used_kb=%u heap_arena_kb=%u\n",
            (unsigned)gLTex.size(), (unsigned long long)(gLTexBytes / 1024), bmpN, (unsigned long long)(bmpBytes / 1024),
            (unsigned)other.size(), (unsigned long long)(otherBytes / 1024), (unsigned)gLBuf.size(),
            (unsigned long long)(gLBufBytes / 1024), (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024),
            (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024),
            (unsigned)(mi.uordblks / 1024), (unsigned)(mi.arena / 1024));
    for (size_t i = 0; i < groups.size() && i < 16; ++i)
        fprintf(f, "  other_tex creator=%p n=%u kb=%llu\n", (void *)groups[i].creator, groups[i].n,
                (unsigned long long)(groups[i].bytes / 1024));
    const uint32_t t = nowS();
    for (size_t i = 0; i < other.size() && i < 12; ++i) {
        const LedgerObj &o = *other[i].second;
        fprintf(f, "  big_other_tex id=%u %ux%u kb=%u age_s=%u creator=%p\n", other[i].first, o.w, o.h, o.bytes / 1024,
                t - o.bornS, (void *)o.creator);
    }
    for (size_t i = 0; i < bgroups.size() && i < 8; ++i)
        fprintf(f, "  buf creator=%p n=%u kb=%llu\n", (void *)bgroups[i].creator, bgroups[i].n,
                (unsigned long long)(bgroups[i].bytes / 1024));
    fclose(f);
    gLLastDumpBytes = gLTexBytes + gLBufBytes;
}

/* Called once per PERF window: dump when the live GL bytes moved by more than 1 MiB. */
extern "C" void vitaGlLedgerMaybeDump(unsigned int window)
{
    const uint64_t cur = gLTexBytes + gLBufBytes;
    const uint64_t d = cur > gLLastDumpBytes ? cur - gLLastDumpBytes : gLLastDumpBytes - cur;
    if (d > 1024 * 1024 || window % 50 == 0) {
        char r[32];
        snprintf(r, sizeof(r), "perf_win=%u", window);
        vitaGlLedgerDump(r);
    }
}
#endif

void initGLFunctions()
{
#define GL_FUN(name, type) gl.name = (type) ::gl##name;

    GL_20_FUN
    GL_ES_FUN
    GL_FBO_FUN
    GL_FBO_BLIT_FUN
    GL_VAO_FUN

#undef GL_FUN

    // Queste estensioni di debug non sono esposte da vitaGL.
    gl.DebugMessageCallback = nullptr;
    gl.StringMarker = nullptr;

    // Stiamo usando il backend GLES/vitaGL.
    gl.glsles = true;

    // Per ora manteniamo conservative queste capability.
    gl.unpack_subimage = false;
    gl.npot_repeat = false;

#ifdef MKXP_VITA_PERF_PROFILE
    gl.DrawElements = vitaPerfDrawElements;
    gl.BlitFramebuffer = vitaPerfBlitFramebuffer;
    gl.BindTexture = vitaPerfBindTexture;
    gl.UseProgram = vitaPerfUseProgram;
    gl.BindFramebuffer = vitaPerfBindFramebuffer;
    gl.BufferData = vitaPerfBufferData;
    gl.BufferSubData = vitaPerfBufferSubData;
    gl.TexImage2D = vitaPerfTexImage2D;
    gl.TexSubImage2D = vitaPerfTexSubImage2D;
#endif
#ifdef MKXP_VITA_DIAG
    gl.CompileShader = vitaDiagCompileShader;
    gl.LinkProgram = vitaDiagLinkProgram;
    gl.TexImage2D = vitaDiagTexImage2D;
    gl.TexSubImage2D = vitaDiagTexSubImage2D;
    gl.GenFramebuffers = vitaDiagGenFramebuffers;
    gl.DeleteFramebuffers = vitaDiagDeleteFramebuffers;
    gl.BindFramebuffer = vitaDiagBindFramebuffer;
#endif
#ifdef MKXP_VITA_GL_NOOP_SKIP
    vitaNoopPrevDrawElements = gl.DrawElements;   gl.DrawElements = vitaNoopDrawElements;
    vitaNoopPrevBufferSubData = gl.BufferSubData; gl.BufferSubData = vitaNoopBufferSubData;
#endif
#ifdef MKXP_VITA_GL_LEDGER
    gLPrevGenTextures = gl.GenTextures;       gl.GenTextures = vitaLedgerGenTextures;
    gLPrevDeleteTextures = gl.DeleteTextures; gl.DeleteTextures = vitaLedgerDeleteTextures;
    gLPrevActiveTexture = gl.ActiveTexture;   gl.ActiveTexture = vitaLedgerActiveTexture;
    gLPrevBindTexture = gl.BindTexture;       gl.BindTexture = vitaLedgerBindTexture;
    gLPrevTexImage2D = gl.TexImage2D;         gl.TexImage2D = vitaLedgerTexImage2D;
    gLPrevGenBuffers = gl.GenBuffers;         gl.GenBuffers = vitaLedgerGenBuffers;
    gLPrevDeleteBuffers = gl.DeleteBuffers;   gl.DeleteBuffers = vitaLedgerDeleteBuffers;
    gLPrevBindBuffer = gl.BindBuffer;         gl.BindBuffer = vitaLedgerBindBuffer;
    gLPrevBufferData = gl.BufferData;         gl.BufferData = vitaLedgerBufferData;
    gLPrevTexSubImage2D = gl.TexSubImage2D;   gl.TexSubImage2D = vitaLedgerTexSubImage2D;
    gLPrevDrawElements = gl.DrawElements;     gl.DrawElements = vitaLedgerDrawElements;
    gLPrevBlitFramebuffer = gl.BlitFramebuffer; gl.BlitFramebuffer = vitaLedgerBlitFramebuffer;
    gLPrevBindFramebuffer = gl.BindFramebuffer; gl.BindFramebuffer = vitaLedgerBindFramebuffer;
    gLPrevFramebufferTexture2D = gl.FramebufferTexture2D; gl.FramebufferTexture2D = vitaLedgerFramebufferTexture2D;
    gLPrevClear = gl.Clear;                   gl.Clear = vitaLedgerClear;
#endif
}
