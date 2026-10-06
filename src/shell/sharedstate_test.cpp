#include <algorithm>
#include "sharedstate.h"
#include "vita_paths.h"
#include "global-ibo.h"
#include "scene.h"
#include "config.h"
#include "texpool.h"
#include "glstate.h"
#include "gl-util.h"
#include "gl-fun.h"

#include "shader.h"
#include "quad.h"

#include <vitaGL.h>
#include <psp2/kernel/clib.h>

#ifdef MKXP_VITA_FREEZE_PROBE
/* Diagnostic only: freeze probe markers (mkxp_vita_minimal/src/vita_freeze_probe.h). */
#include "../vita_freeze_probe.h"
#include "vita_build_tag.h"
#else
#define VITA_FREEZE_MARK(ph)
#endif

/* Per-frame qa.log call sites in vitaRenderFrame(); each can be disabled on its own (log bisect). */
#ifdef MKXP_VITA_NO_PER_FRAME_QA_LOG
#define MKXP_VITA_NO_QA_FINAL_BLIT_BEFORE
#define MKXP_VITA_NO_QA_FINAL_BLIT_AFTER
#define MKXP_VITA_NO_QA_FRAME
#define MKXP_VITA_NO_QA_SWAP_BEFORE
#define MKXP_VITA_NO_QA_SWAP_AFTER
#endif

#if defined(MKXP_VITA_POST_SWAP_DELAY_US) || defined(MKXP_VITA_PRE_BLIT_DELAY_US)
#include <psp2/kernel/threadmgr.h>
#endif
#ifdef MKXP_VITA_DELAY_TOGGLE
#include <psp2/ctrl.h>
#include <stdio.h>
static int vitaCurrentDelayUs = 0;   /* pre-blit delay of the last frame (PERF delay_us=) */
#endif
#ifdef MKXP_VITA_PRE_BLIT_BUSYWAIT_US
#include <psp2/kernel/processmgr.h>
#endif
/* Diagnostic only (MKXP_VITA_TIMELINE / MKXP_VITA_PERF_BITMAP): see mkxp-z/src/vita_diag.h. */
#include "vita_diag.h"
#ifdef MKXP_VITA_PERF_LITE
extern "C" int vitaPerfLiteAppend(char *buf, int cap);   /* main.cpp */
#endif
#ifdef MKXP_VITA_PERF_BITMAP
extern "C" int vitaDiagPerfAppend(char *buf, int cap);
/* vita_diag.cpp (MKXP_VITA_RUBY_PROF): slow-frame log, called at the end of every frame. */
extern "C" __attribute__((weak)) void vitaDiagFrameEnd(unsigned long long frameUs, unsigned long long rubyUs);
extern "C" __attribute__((weak)) void vitaDiagPhaseMark(int which, int begin);   /* vita_diag.cpp: thread CPU per phase */
extern int vitaDiagBattleScene;
#endif
SharedState *SharedState::instance = nullptr;
int SharedState::rgssVersion = 3;

static GlobalIBO *testGlobalIBO = nullptr;
static Config *testConfig = nullptr;
static TexPool *testTexPool = nullptr;
static GLState *testGLState = nullptr;

static unsigned int testStampCounter = 0;


/*
 * Piccola Scene reale di mkxp-z.
 *
 * Scene::geometry è protected, quindi una sottoclasse
 * può inizializzarla.
 */
#ifdef MKXP_VITA_VIEWPORT_GRAY
/* Scene target the composite is drawing into (A or B, see vitaGrayPass). */
static TEXFBO *vitaSceneCur = nullptr;
#ifdef MKXP_VITA_SCENE_STENCIL
/*
 * Fix (MKXP_VITA_SCENE_STENCIL): the framebuffers the scene is drawn into get a depth/stencil
 * surface. vitaGL's glScissor is exact only through the stencil surface's mask bit
 * (update_scissor_test); without it only sceGxmSetRegionClip remains, which clips at tile
 * granularity, so window contents outside the window's clip rect (a battle command list longer
 * than its window) stayed visible below it. MKXP_NO_FBO_DEPTH (our vitaGL) gives no depth surface to
 * framebuffers without a depth/stencil attachment: the scene buffers (544x416, ~0.9 MiB each) now
 * have one; bitmaps, atlases and effect buffers still do not. mkxp never enables GL_DEPTH_TEST.
 */
static void vitaSceneDepthStencil(TEXFBO &t)   /* t.fbo bound */
{
    GLuint rb = 0;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, t.width, t.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
}
#endif
static void vitaGrayPass(float gray, const IntRect &viewportRect);
#endif

class VitaTestScene : public Scene
{
public:
#ifdef MKXP_VITA_PERF_PROFILE
    /* Profiling only: top-level element count for the periodic summary. */
    int vitaElementCount() const { return elements.getSize(); }
#endif

    VitaTestScene()
    {
        geometry.rect = IntRect(
            0,
            0,
            960,
            544
        );

        geometry.orig = Vec2i(
            0,
            0
        );
    }

#ifdef MKXP_VITA_VIEWPORT_FX
    /*
     * Fix (MKXP_VITA_VIEWPORT_FX): Viewport tone / color / flash, as mkxp-z's ScreenScene
     * (graphics.cpp requestViewportRender): full-screen flat quads with hardware blending, clipped
     * by the viewport's scissor box. Screen fade (color alpha), flashes and RGB tints go through here.
     * Not yet: the gray part of the tone (needs a copy of the scene; counted in vitaViewportGrayTones).
     */
    void requestViewportRender(const Vec4 &c, const Vec4 &f, const Vec4 &t) override
    {
        const bool toneRGBEffect = t.xyzNotNull();
        const bool colorEffect = c.w > 0;
        const bool flashEffect = f.w > 0;
        if (t.w != 0) {
            ++vitaViewportGrayTones;
#ifdef MKXP_VITA_VIEWPORT_GRAY
            vitaGrayPass(t.w, glState.scissorBox.get());
#endif
        }
        if (!toneRGBEffect && !colorEffect && !flashEffect)
            return;

        static Quad *quad = nullptr;
        if (!quad) {
            quad = new Quad();
            quad->setTexPosRect(FloatRect(0, 0, 544, 416), FloatRect(0, 0, 544, 416));
        }

        FlatColorShader &shader = shState->shaders().flatColor;
        shader.bind();
        shader.applyViewportProj();
        shader.setTranslation(Vec2i());
        glState.blend.pushSet(true);

        if (toneRGBEffect) {
            Vec4 add, sub;
            if (t.x > 0) add.x = t.x;
            if (t.y > 0) add.y = t.y;
            if (t.z > 0) add.z = t.z;
            if (t.x < 0) sub.x = -t.x;
            if (t.y < 0) sub.y = -t.y;
            if (t.z < 0) sub.z = -t.z;
            gl.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            if (add.xyzNotNull()) {
                gl.BlendEquation(GL_FUNC_ADD);
                shader.setColor(add);
                quad->draw();
            }
            if (sub.xyzNotNull()) {
                gl.BlendEquation(GL_FUNC_REVERSE_SUBTRACT);
                shader.setColor(sub);
                quad->draw();
            }
        }
        if (colorEffect || flashEffect) {
            gl.BlendEquation(GL_FUNC_ADD);
            gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        }
        if (colorEffect) {
            shader.setColor(c);
            quad->draw();
        }
        if (flashEffect) {
            shader.setColor(f);
            quad->draw();
        }

        glState.blendMode.refresh();
        glState.blend.pop();
    }

    unsigned int vitaViewportGrayTones = 0;
#endif
};


static VitaTestScene *testScene = nullptr;


SharedState::SharedState()
    : p(nullptr)
{
    testConfig = new Config();

    /*
     * Config minima per Bitmap.
     * Per ora niente texture hires.
     */
    testConfig->enableHires = false;
    testConfig->textureScalingFactor = 1.0;
    /* Campi usati da GLMeta (HAVE_NATIVE_BLIT) e TilemapVX:
     * Config::Config() dello shim non li inizializza. */
    testConfig->smoothScaling = 0;        /* NearestNeighbor */
    testConfig->smoothScalingDown = 0;
    testConfig->atlasScalingFactor = 1.0;
    /* Upstream default (config.cpp): TileAtlasVX divides by it when building tile texcoords. */
    testConfig->framebufferScalingFactor = 1.0;
    testConfig->dumpAtlas = false;
    testConfig->bitmapSmoothScaling = 0;      /* NearestNeighbor */
    testConfig->bitmapSmoothScalingDown = 0;
#ifdef MKXP_VITA_AUDIO
    /* mkxp-z defaults (config.cpp): one BGM track, six SE sources. */
    testConfig->BGM.trackCount = 1;
    testConfig->SE.sourceCount = 6;
#endif

    testTexPool = new TexPool(
        16 * 1024 * 1024
    );
#ifdef MKXP_VITA_TEXPOOL_NO_CACHE
    /* Fix: released textures/FBOs are deleted at once instead of cached. Each cached FBO kept its
     * GXM render target alive (vitaGL: ~47 usable); Scene_Save's 17 new windows went past it. */
    testTexPool->disable();
#endif

    testGlobalIBO = new GlobalIBO();
    testGlobalIBO->ensureSize(1);

    testScene = new VitaTestScene();

    testStampCounter = 0;

    SharedState::instance = this;
}


SharedState::~SharedState()
{
    delete testScene;
    testScene = nullptr;

    delete testGlobalIBO;
    testGlobalIBO = nullptr;

    delete testTexPool;
    testTexPool = nullptr;

    delete testConfig;
    testConfig = nullptr;

    SharedState::instance = nullptr;
}


Scene *SharedState::screen() const
{
    return testScene;
}


unsigned int SharedState::genTimeStamp()
{
    return testStampCounter++;
}


GlobalIBO &SharedState::globalIBO()
{
    return *testGlobalIBO;
}


void SharedState::ensureQuadIBO(size_t minSize)
{
    testGlobalIBO->ensureSize(minSize);
}

#ifdef MKXP_VITA_ATLAS_PARK
/*
 * Fix (MKXP_VITA_ATLAS_PARK): a released tile atlas (8 MiB, 2048x1024 RGBA + FBO) is parked instead of
 * deleted and handed out again by the next request of the same size once it has not been used for
 * kParkSwaps frames (> vitaGL FRAME_PURGE_FREQ 4: no copy-on-write when it is rebuilt, the reason
 * for MKXP_VITA_ATLAS_FRESH_TEX). TileAtlasVX::build clears the whole atlas first, so a reused one
 * gives the same pixels as a new one. d68: every map change allocated a fresh 8 MiB block and freed
 * the old one later; the churn fragmented the vitaGL pools until the atlas went to the newlib heap
 * (15x) or failed (7x). After the first two, no atlas is allocated again. At most kMaxParked kept.
 */
#include <vector>
extern "C" unsigned int vitaSwapCountNow(void);   /* gl-fun-vita.cpp */
namespace {
struct VitaParkedAtlas { TEXFBO tex; unsigned int swap; };
std::vector<VitaParkedAtlas> gVitaParked;
unsigned gVitaAtlasReused = 0, gVitaAtlasNew = 0;
const unsigned int kParkSwaps = 8;
const size_t kMaxParked = 2;
}
extern "C" void vitaAtlasParkStats(unsigned *reused, unsigned *fresh, unsigned *parked)
{
    *reused = gVitaAtlasReused;
    *fresh = gVitaAtlasNew;
    *parked = (unsigned)gVitaParked.size();
}
#endif

void SharedState::requestAtlasTex(int w, int h, TEXFBO &out)
{
#ifdef MKXP_VITA_ATLAS_PARK
    {
        const unsigned int now = vitaSwapCountNow();
        int best = -1;
        for (size_t i = 0; i < gVitaParked.size(); ++i) {
            const VitaParkedAtlas &a = gVitaParked[i];
            if (a.tex.width == w && a.tex.height == h && now - a.swap >= kParkSwaps &&
                (best < 0 || a.swap < gVitaParked[best].swap))
                best = (int)i;
        }
        if (best >= 0) {
            out = gVitaParked[best].tex;
            gVitaParked.erase(gVitaParked.begin() + best);
            ++gVitaAtlasReused;
            return;
        }
        ++gVitaAtlasNew;
    }
#endif
    TEXFBO tex;
    TEXFBO::init(tex);
    TEXFBO::allocEmpty(tex, w, h);
    TEXFBO::linkFBO(tex);
    out = tex;
}

void SharedState::releaseAtlasTex(TEXFBO &tex)
{
    if (tex.tex == TEX::ID(0))
        return;
#ifdef MKXP_VITA_ATLAS_PARK
    if (gVitaParked.size() >= kMaxParked) {
        size_t oldest = 0;
        for (size_t i = 1; i < gVitaParked.size(); ++i)
            if (gVitaParked[i].swap < gVitaParked[oldest].swap)
                oldest = i;
        TEXFBO::fini(gVitaParked[oldest].tex);
        gVitaParked.erase(gVitaParked.begin() + oldest);
    }
    gVitaParked.push_back(VitaParkedAtlas{ tex, vitaSwapCountNow() });
    return;
#endif
    TEXFBO::fini(tex);
}
ShaderSet &SharedState::shaders() const
{
    /* Creato solo se usato: con smoothScaling=0 GLMeta usa il blit
     * nativo e questo non viene mai chiamato. */
#ifdef MKXP_VITA_DIAG
    /* Diagnostic: time the first construction (all runtime shader compiles/links). */
    static ShaderSet *set = nullptr;
    if (!set) {
        VITA_DIAG_T0(vitaDiagT0);
        set = new ShaderSet();
        VITA_DIAG_SPAN(VD_SHADERSET, vitaDiagT0, 0, "first use");
    }
#else
    static ShaderSet *set = new ShaderSet();
#endif
    return *set;
}

Quad &SharedState::gpQuad() const
{
    static Quad *quad = new Quad();
    return *quad;
}

#ifdef MKXP_VITA_AUDIO
/*
 * Audio (MKXP_VITA_AUDIO): the upstream mkxp-z Audio needs RGSSThreadData (config + SyncPoint), the
 * FileSystem and an OpenAL context. Created on first use (first Audio.* call from Ruby).
 */
#include "eventthread.h"
#include "filesystem.h"
#include "audio.h"
#include <AL/alc.h>

RGSSThreadData &SharedState::rtData() const
{
    static RGSSThreadData *rt = nullptr;
    if (!rt) {
        ALCdevice *dev = alcOpenDevice(nullptr);
        if (dev) {
            ALCcontext *ctx = alcCreateContext(dev, nullptr);
            if (ctx)
                alcMakeContextCurrent(ctx);
        }
        FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
        if (f) { fprintf(f, "AUDIO_INIT alc_device=%p\n", (void *)dev); fclose(f); }
        rt = new RGSSThreadData(nullptr, "", nullptr, dev, 60, 1, *testConfig, nullptr);
    }
    return *rt;
}

FileSystem &SharedState::fileSystem() const
{
    static FileSystem *fs = new FileSystem("", false);
    return *fs;
}

Audio &SharedState::audio() const
{
    static Audio *a = Audio::vitaCreate(rtData());
    return *a;
}
#endif

Config &SharedState::config() const
{
    return *testConfig;
}

TexPool &SharedState::texPool() const
{
    return *testTexPool;
}
GLState &SharedState::_glState() const
{
    return *testGLState;
}

void vitaSetGLState(GLState *state)
{
    testGLState = state;
}
/*
 * TRACE VITA: i punti per-frame vengono stampati solo nei primi 10 frame,
 * ogni 300 frame, e per 10 frame dopo ogni TileAtlasVX::build.
 */
extern "C" int vitaTraceBurst;
int vitaTraceBurst = 0;

#define VITA_FRAME_TRACE(what) \
    do { if (vitaTrace) sceClibPrintf("VITA_TRACE frame=%u %s\n", vitaTraceFrame, what); } while (0)

#ifdef MKXP_VITA_PERF_PROFILE
/*
 * PROFILING VITA: tempi per regione di vitaRenderFrame() accumulati in RAM
 * (sceKernelGetProcessTimeWide, microsecondi), riepilogo su tty0 (e perf.log) ogni 120 frame.
 * Regioni di primo livello esclusive tra loro; i sotto-timer di vita_perf.h sono inclusivi e
 * annidati dentro prepare (tilemap/sprite/window prepare) o composite (tilemap/sprite/window draw).
 * Fase TITLE fino al primo TileAtlasVX::build (vitaTraceBurst == 10 dopo
 * prepareDraw), poi MAP; il cambio di fase chiude la finestra corrente.
 * I VITA_FRAME_TRACE per-frame sono disattivati; i marker one-shot restano.
 */
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include "vita_perf.h"

#define VITA_PERF_WINDOW_FRAMES 120

SceUInt64 vitaPerfSubUs[VITA_PERF_SUB_COUNT];
unsigned int vitaPerfCounters[VITA_PERF_CNT_COUNT];

#undef VITA_FRAME_TRACE
#define VITA_FRAME_TRACE(what) do { (void)vitaTrace; } while (0)

enum {
    VITA_PERF_OUTSIDE,   /* uscita vitaRenderFrame precedente -> ingresso (lato Ruby) */
    VITA_PERF_TOTAL,     /* ingresso -> uscita vitaRenderFrame */
    VITA_PERF_PREPARE,   /* prepareDraw() */
    VITA_PERF_SETUP,     /* FBO bind + viewport + quad nero */
    VITA_PERF_COMPOSITE, /* testScene->composite() */
    VITA_PERF_DELAY,     /* sceKernelDelayThread pre-blit */
    VITA_PERF_BLIT,      /* BindFramebuffer x2 + BlitFramebuffer + FBO::unbind */
    VITA_PERF_SWAP,      /* vglSwapBuffers */
    VITA_PERF_REST,      /* TOTAL meno le regioni sopra */
    VITA_PERF_FRAME,     /* OUTSIDE + TOTAL dello stesso frame (senza il riepilogo) */
    VITA_PERF_COUNT
};

static const char *const vitaPerfNames[VITA_PERF_COUNT] = {
    "ruby", "render", "prepare", "setup", "composite", "delay", "final_blit", "swap", "rest", "frame"
};

static const char *const vitaPerfSubNames[VITA_PERF_SUB_COUNT] = {
    "tilemap_prepare", "tilemap_rebuild", "atlas_rebuild", "sprite_prepare", "window_prepare",
    "tilemap_draw", "sprite_draw", "window_draw"
};

static const char *const vitaPerfCounterNames[VITA_PERF_CNT_COUNT] = {
    "draw_elements", "blit_framebuffer", "texture_binds", "program_binds", "fbo_binds",
    "buffer_data", "buffer_subdata", "tex_image", "tex_subimage", "tilemap_rebuilds",
    "atlas_rebuilds", "tilemap_draws", "sprite_draws", "window_draws"
};

struct VitaPerfStat
{
    SceUInt64 total;
    SceUInt64 max;
    SceUInt64 min;
    unsigned int n;
};

static VitaPerfStat vitaPerfStats[VITA_PERF_COUNT];
static unsigned int vitaPerfWindow = 0;
static unsigned int vitaPerfAtlasBuilds = 0;
static bool vitaPerfMap = false;
static SceUInt64 vitaPerfLastExit = 0;

#ifdef MKXP_VITA_PERF_PROFILE_FILE
/*
 * Le righe di riepilogo vengono accodate in RAM e scritte su perf.log solo a fine
 * frame (dopo vglSwapBuffers) nei frame multipli di VITA_PERF_WINDOW_FRAMES (120): un solo fopen/fclose.
 * La riga del cambio fase (generata dopo prepareDraw) aspetta la scrittura successiva.
 */
#if defined(MKXP_VITA_PERF_BITMAP) && defined(MKXP_VITA_LOG_BATCH)
static char vitaPerfPending[131072];   /* d85: written every MKXP_VITA_LOG_BATCH windows */
#elif defined(MKXP_VITA_LOG_BATCH)
static char vitaPerfPending[32768];    /* d88: short PERF lines (no PERF_BITMAP), same cadence */
#elif defined(MKXP_VITA_PERF_BITMAP)
static char vitaPerfPending[16384];
#else
static char vitaPerfPending[4096];
#endif
static int vitaPerfPendingLen = 0;

static void vitaPerfWritePending()
{
    if (vitaPerfPendingLen == 0)
        return;

    VITA_FREEZE_MARK(PERF_WRITE_ENTER);
    FILE *f = fopen(VITA_GAME_ROOT "perf.log", "a");
    if (f) {
#ifdef MKXP_VITA_FREEZE_PROBE
        /* One line per run, before the first summary; PERF lines keep their format. */
        static bool vitaPerfBuildWritten = false;
        if (!vitaPerfBuildWritten) {
            static const char vitaPerfBuildLine[] = "PERF_BUILD " MKXP_VITA_BUILD_TAG "\n";
            fwrite(vitaPerfBuildLine, 1, sizeof(vitaPerfBuildLine) - 1, f);
            vitaPerfBuildWritten = true;
        }
#endif
        fwrite(vitaPerfPending, 1, (size_t)vitaPerfPendingLen, f);
        fclose(f);
    }
    VITA_FREEZE_MARK(PERF_WRITE_EXIT);

    vitaPerfPendingLen = 0;
}
#endif

#ifdef MKXP_VITA_FRAME_HIST
/* Stability (MKXP_VITA_FRAME_HIST): every frame time of the window, for percentiles and counts of
 * frames over 1/2/3/6 vblanks (PERF fhist=). */
static SceUInt64 vitaFrameUs[512];
static unsigned int vitaFrameN = 0;
#endif
static inline void vitaPerfAdd(int i, SceUInt64 dt)
{
#ifdef MKXP_VITA_FRAME_HIST
    if (i == VITA_PERF_FRAME && vitaFrameN < 512)
        vitaFrameUs[vitaFrameN++] = dt;
#endif
    VitaPerfStat &st = vitaPerfStats[i];
    st.total += dt;
    if (st.n == 0 || dt > st.max)
        st.max = dt;
    if (st.n == 0 || dt < st.min)
        st.min = dt;
    ++st.n;
}

/*
 * Una riga per finestra. Regioni: <nome>_ms=avg/min/max per frame. Sotto-timer: <nome>_ms=avg per
 * frame (somma nella finestra / frame). Contatori: totale nella finestra.
 */
static void vitaPerfFlush(unsigned int lastFrame)
{
    const unsigned int frames = vitaPerfStats[VITA_PERF_TOTAL].n;
    if (frames == 0)
        return;

    const VitaPerfStat &fr = vitaPerfStats[VITA_PERF_FRAME];
    const double frameAvgUs = fr.n ? (double)fr.total / fr.n : 0.0;

#ifdef MKXP_VITA_PERF_BITMAP
    char line[8192];   /* d52: alloc_* fields */
    int len = snprintf(line, sizeof(line),
#ifdef MKXP_VITA_DELAY_TOGGLE
        "PERF win=%u scene=%s delay_us=%d frame=%u frames=%u fps=%.1f atlas_builds_total=%u elems=%d",
        vitaPerfWindow, vitaDiagBattleScene ? "BATTLE" : (vitaPerfMap ? "MAP" : "TITLE"), vitaCurrentDelayUs, lastFrame, frames,
#else
        "PERF win=%u scene=%s frame=%u frames=%u fps=%.1f atlas_builds_total=%u elems=%d",
        vitaPerfWindow, vitaDiagBattleScene ? "BATTLE" : (vitaPerfMap ? "MAP" : "TITLE"), lastFrame, frames,
#endif
#else
    char line[1600];
    int len = snprintf(line, sizeof(line),
        "PERF win=%u scene=%s frame=%u frames=%u fps=%.1f atlas_builds_total=%u elems=%d",
        vitaPerfWindow, vitaPerfMap ? "MAP" : "TITLE", lastFrame, frames,
#endif
        frameAvgUs > 0.0 ? 1000000.0 / frameAvgUs : 0.0,
        vitaPerfAtlasBuilds, testScene ? testScene->vitaElementCount() : -1);

    for (int i = 0; i < VITA_PERF_COUNT && len > 0 && len < (int)sizeof(line); ++i) {
        const VitaPerfStat &st = vitaPerfStats[i];
        len += snprintf(line + len, sizeof(line) - len, " %s_ms=%.2f/%.2f/%.2f",
            vitaPerfNames[i],
            st.n ? (double)st.total / st.n / 1000.0 : 0.0,
            (double)st.min / 1000.0, (double)st.max / 1000.0);
    }

    for (int i = 0; i < VITA_PERF_SUB_COUNT && len > 0 && len < (int)sizeof(line); ++i)
        len += snprintf(line + len, sizeof(line) - len, " %s_ms=%.2f",
            vitaPerfSubNames[i], (double)vitaPerfSubUs[i] / frames / 1000.0);

    for (int i = 0; i < VITA_PERF_CNT_COUNT && len > 0 && len < (int)sizeof(line); ++i)
        len += snprintf(line + len, sizeof(line) - len, " %s=%u",
            vitaPerfCounterNames[i], vitaPerfCounters[i]);

    if (len > 0 && len < (int)sizeof(line))
        len += snprintf(line + len, sizeof(line) - len, " draw_calls=%u vbo_uploads=%u",
            vitaPerfCounters[VITA_PERF_CNT_DRAW_ELEMENTS] + vitaPerfCounters[VITA_PERF_CNT_BLIT_FRAMEBUFFER],
            vitaPerfCounters[VITA_PERF_CNT_BUFFER_DATA] + vitaPerfCounters[VITA_PERF_CNT_BUFFER_SUBDATA]);

#ifdef MKXP_VITA_FRAME_HIST
    if (len > 0 && len < (int)sizeof(line) && vitaFrameN) {
        std::sort(vitaFrameUs, vitaFrameUs + vitaFrameN);
        unsigned int over17 = 0, over34 = 0, over50 = 0, over100 = 0;
        for (unsigned int k = 0; k < vitaFrameN; ++k) {
            const SceUInt64 v = vitaFrameUs[k];
            over17 += v > 17500; over34 += v > 34200; over50 += v > 50900; over100 += v > 100000;
        }
        len += snprintf(line + len, sizeof(line) - len, " fp50=%.1f fp90=%.1f fp99=%.1f fmax=%.1f f_over17=%u f_over34=%u f_over50=%u f_over100=%u",
            vitaFrameUs[vitaFrameN / 2] / 1000.0, vitaFrameUs[vitaFrameN * 9 / 10] / 1000.0,
            vitaFrameUs[vitaFrameN * 99 / 100] / 1000.0, vitaFrameUs[vitaFrameN - 1] / 1000.0,
            over17, over34, over50, over100);
        vitaFrameN = 0;
    }
#endif
#ifdef MKXP_VITA_PERF_BITMAP
    /* Bitmap/text/upload counters of this window: bmp_<op>=count/total_ms/KiB. */
    if (len > 0 && len < (int)sizeof(line))
        len += vitaDiagPerfAppend(line + len, (int)sizeof(line) - len);
#ifdef MKXP_VITA_VIEWPORT_FX
    /* Viewport tones with a gray part (not rendered yet), cumulative. */
    if (len > 0 && len < (int)sizeof(line) && testScene)
        len += snprintf(line + len, sizeof(line) - len, " vp_gray_tones=%u", testScene->vitaViewportGrayTones);
#endif
    if (len >= (int)sizeof(line))
        len = (int)sizeof(line) - 1;
#endif
#ifdef MKXP_VITA_PERF_LITE
    if (len > 0 && len < (int)sizeof(line) - 1)
        len += vitaPerfLiteAppend(line + len, (int)sizeof(line) - len);
#endif

    sceClibPrintf("%s\n", line);

#ifdef MKXP_VITA_PERF_PROFILE_FILE
    if (len > 0) {
        const int lineLen = len < (int)sizeof(line) ? len : (int)sizeof(line) - 1;
        if (vitaPerfPendingLen + lineLen + 1 <= (int)sizeof(vitaPerfPending)) {
            memcpy(vitaPerfPending + vitaPerfPendingLen, line, (size_t)lineLen);
            vitaPerfPendingLen += lineLen;
            vitaPerfPending[vitaPerfPendingLen++] = '\n';
        }
    }
#endif

    for (int i = 0; i < VITA_PERF_COUNT; ++i)
        vitaPerfStats[i] = VitaPerfStat();
    memset(vitaPerfSubUs, 0, sizeof(vitaPerfSubUs));
    memset(vitaPerfCounters, 0, sizeof(vitaPerfCounters));

    ++vitaPerfWindow;
}
#endif

#ifdef MKXP_VITA_HEARTBEAT_LOG
/*
 * HEARTBEAT VITA: contatore monotono dei vitaRenderFrame() completati.
 * Scrive heartbeat.log solo dopo vglSwapBuffers, ai frame 60,120,240,...,3840.
 * NEW_GAME_ENTER (lato Ruby) salva solo il contatore; la riga MAP_RENDER_ENTER
 * viene scritta una volta sola, dopo il present del frame successivo.
 */
static unsigned int vitaHeartbeatFrames = 0;
static bool vitaHeartbeatMapPending = false;
static bool vitaHeartbeatMapDone = false;
static unsigned int vitaHeartbeatMapFrame = 0;

void vitaHeartbeatMarkNewGame()
{
    if (vitaHeartbeatMapPending || vitaHeartbeatMapDone)
        return;

    vitaHeartbeatMapFrame = vitaHeartbeatFrames;
    vitaHeartbeatMapPending = true;
}

static void vitaHeartbeatAfterPresent()
{
    const unsigned int frame = ++vitaHeartbeatFrames;

    bool beat = false;
    switch (frame) {
    case 60: case 120: case 240: case 480: case 960: case 1920: case 3840:
        beat = true;
        break;
    default:
        break;
    }

    if (!beat && !vitaHeartbeatMapPending)
        return;

    FILE *f = fopen(VITA_GAME_ROOT "heartbeat.log", "a");
    if (f) {
        if (vitaHeartbeatMapPending)
            fprintf(f, "MAP_RENDER_ENTER totalFrame=%u\n", vitaHeartbeatMapFrame);
        if (beat)
            fprintf(f, "HEARTBEAT frame=%u\n", frame);
        fclose(f);
    }

    if (vitaHeartbeatMapPending) {
        vitaHeartbeatMapPending = false;
        vitaHeartbeatMapDone = true;
    }
}
#endif

#ifdef MKXP_VITA_SCREEN_FX
/*
 * Fix (MKXP_VITA_SCREEN_FX): Graphics.brightness / fadeout / fadein, Graphics.freeze / transition
 * and Graphics.snap_to_bitmap, which were no-ops. Applied on the 544x416 scene before the final blit:
 *  - brightness: black quad with alpha 1 - brightness/255 over the scene (mkxp-z brightnessQuad);
 *  - freeze: copy of the last frame; while frozen the frozen copy is presented;
 *  - transition(prog): SimpleTransShader mix(frozen, current, prog) into a third target
 *    (image-mask transitions are shown as this plain fade).
 */
static int vitaFxBrightness = 255;
static bool vitaFxFrozen = false;
static float vitaFxProg = -1.0f;
static TEXFBO vitaFxFrozenFBO, vitaFxTransFBO;
static TEXFBO *vitaFxLastScene = nullptr;
#ifdef MKXP_VITA_GFX_V2
/* Graphics.transition(duration, filename, vague): the transition map (TransShader) and vague/256. */
static GLuint vitaFxTransMapTex = 0;
static float vitaFxTransVague = 0.0f;

extern "C" void vitaFxSetTransMap(GLuint tex, int vague)
{
    vitaFxTransMapTex = tex;
    vitaFxTransVague = vague / 256.0f;
}
#endif

static TEXFBO &vitaFxTarget(TEXFBO &t)
{
    if (t.tex == TEX::ID(0)) {
        TEXFBO::init(t);
        TEXFBO::allocEmpty(t, 544, 416);
        TEXFBO::linkFBO(t);
    }
    return t;
}

static Quad &vitaFxQuad()
{
    static Quad *q = nullptr;
    if (!q) {
        q = new Quad();
        q->setTexPosRect(FloatRect(0, 0, 544, 416), FloatRect(0, 0, 544, 416));
    }
    return *q;
}

/* dst <- src, 1:1, blending off (same shader path as the TileAtlasVX blits). */
static void vitaFxCopy(TEXFBO &dst, const TEXFBO &src)
{
    FBO::bind(dst.fbo);
    glState.viewport.pushSet(IntRect(0, 0, 544, 416));
    glState.scissorTest.pushSet(false);
    glState.blend.pushSet(false);
    SimpleShader &shader = shState->shaders().simple;
    shader.bind();
    shader.applyViewportProj();
    shader.setTranslation(Vec2i());
    shader.setTexSize(Vec2i(544, 416));
    TEX::bind(src.tex);
    vitaFxQuad().draw();
    glState.blend.pop();
    glState.scissorTest.pop();
    glState.viewport.pop();
}

extern "C" void vitaFxSetBrightness(int value)
{
    vitaFxBrightness = value < 0 ? 0 : (value > 255 ? 255 : value);
}

extern "C" void vitaFxFreeze(void)
{
    if (!vitaFxLastScene)
        return;
    vitaFxCopy(vitaFxTarget(vitaFxFrozenFBO), *vitaFxLastScene);
    FBO::unbind();
    vitaFxFrozen = true;
    vitaFxProg = -1.0f;
}

extern "C" bool vitaFxIsFrozen(void)
{
    return vitaFxFrozen;
}

/* prog in [0,1]: transition step; < 0: end (unfreeze). No effect unless frozen. */
extern "C" void vitaFxTransition(float prog)
{
    if (prog < 0.0f) {
        vitaFxFrozen = false;
        vitaFxProg = -1.0f;
        return;
    }
    if (vitaFxFrozen)
        vitaFxProg = prog > 1.0f ? 1.0f : prog;
}

extern "C" bool vitaFxSnapPixels(const unsigned char **px, int *strideBytes)
{
    if (!vitaFxLastScene)
        return false;
    glFinish();
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    TEX::bind(vitaFxLastScene->tex);
    *px = (const unsigned char *)vglGetTexDataPointer(GL_TEXTURE_2D);
    *strideBytes = ((544 + 7) & ~7) * 4;
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    return *px != nullptr;
}

#ifdef MKXP_VITA_VIEWPORT_GRAY
/*
 * Fix (MKXP_VITA_VIEWPORT_GRAY): the gray part of a Viewport tone, as mkxp-z ScreenScene: the scene
 * continues in the other buffer (ping-pong). If the viewport does not cover the screen, the current
 * buffer is first copied whole; then GrayShader samples the old buffer inside the viewport scissor.
 * Only A -> B switches happen in a frame (never back into an FBO already left).
 */
static TEXFBO vitaSceneExtra[2];
static int vitaGrayPassesThisFrame = 0;   /* reset at the start of every frame */
static TEXFBO *vitaSceneBase = nullptr;   /* buffer A (sceneFBO) of this frame */
static void vitaGrayPass(float gray, const IntRect &viewportRect)
{
    if (!vitaSceneCur || !vitaSceneBase)
        return;
    TEXFBO &src = *vitaSceneCur;
    /* A -> B -> C; only a 3rd gray viewport in one frame reuses a buffer (A again: ping-pong). */
    const int k = vitaGrayPassesThisFrame++;
    TEXFBO &dst = (k < 2) ? vitaFxTarget(vitaSceneExtra[k]) : (&src == vitaSceneBase ? vitaSceneExtra[0] : *vitaSceneBase);
#ifdef MKXP_VITA_SCENE_STENCIL
    {
        static bool withStencil[2] = { false, false };
        if (k < 2 && !withStencil[k]) {
            FBO::bind(dst.fbo);
            vitaSceneDepthStencil(dst);
            withStencil[k] = true;
        }
    }
#endif
    const bool encloses = viewportRect.x <= 0 && viewportRect.y <= 0 &&
                          viewportRect.x + viewportRect.w >= 544 && viewportRect.y + viewportRect.h >= 416;
    if (!encloses)
        vitaFxCopy(dst, src);            /* binds dst; scissor/blend restored afterwards */
    else
        FBO::bind(dst.fbo);
    glState.blend.pushSet(false);
    GrayShader &shader = shState->shaders().gray;
    shader.bind();
    shader.setGray(gray);
    shader.applyViewportProj();
    shader.setTranslation(Vec2i());
    shader.setTexSize(Vec2i(544, 416));
    TEX::bind(src.tex);
    vitaFxQuad().draw();
    glState.blend.pop();
    vitaSceneCur = &dst;
}
#endif

/* After composite: brightness into the scene; returns the target the final blit presents. */
static TEXFBO &vitaFxApply(TEXFBO &scene)
{
    vitaFxLastScene = &scene;
    if (vitaFxBrightness < 255) {
        FBO::bind(scene.fbo);
        glState.viewport.pushSet(IntRect(0, 0, 544, 416));
        glState.scissorTest.pushSet(false);
        glState.blend.pushSet(true);
        glState.blendMode.pushSet(BlendNormal);
        FlatColorShader &shader = shState->shaders().flatColor;
        shader.bind();
        shader.applyViewportProj();
        shader.setTranslation(Vec2i());
        shader.setColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f - vitaFxBrightness / 255.0f));
        vitaFxQuad().draw();
        glState.blendMode.pop();
        glState.blend.pop();
        glState.scissorTest.pop();
        glState.viewport.pop();
    }
    if (!vitaFxFrozen)
        return scene;
    if (vitaFxProg < 0.0f)
        return vitaFxFrozenFBO;

    TEXFBO &out = vitaFxTarget(vitaFxTransFBO);
    FBO::bind(out.fbo);
    glState.viewport.pushSet(IntRect(0, 0, 544, 416));
    glState.scissorTest.pushSet(false);
    glState.blend.pushSet(false);
#ifdef MKXP_VITA_GFX_V2
    if (vitaFxTransMapTex) {
        /* As mkxp-z Graphics::transition with a transition bitmap (trans.frag). */
        TransShader &shader = shState->shaders().trans;
        shader.bind();
        shader.applyViewportProj();
        shader.setTranslation(Vec2i());
        shader.setTexSize(Vec2i(544, 416));
        shader.setFrozenScene(vitaFxFrozenFBO.tex);
        shader.setCurrentScene(scene.tex);
        shader.setTransMap(TEX::ID(vitaFxTransMapTex));
        shader.setVague(vitaFxTransVague);
        shader.setProg(vitaFxProg);
        vitaFxQuad().draw();
    } else
#endif
    {
    SimpleTransShader &shader = shState->shaders().simpleTrans;
    shader.bind();
    shader.applyViewportProj();
    shader.setTranslation(Vec2i());
    shader.setTexSize(Vec2i(544, 416));
    shader.setFrozenScene(vitaFxFrozenFBO.tex);
    shader.setCurrentScene(scene.tex);
    shader.setProg(vitaFxProg);
    vitaFxQuad().draw();
    }
    glState.blend.pop();
    glState.scissorTest.pop();
    glState.viewport.pop();
    return out;
}
#endif

#ifdef MKXP_VITA_FINAL_SHARP_BILINEAR
/*
 * Final presentation filter "sharp bilinear" (MKXP_VITA_FINAL_FILTER=SHARP_BILINEAR): 544x416 is scaled
 * by a non-integer factor (1.307 to 711x544), so NEAREST makes some source pixels 1 screen pixel wide and
 * others 2 (uneven, shimmering when scrolling), and LINEAR blurs everything. Sharp bilinear keeps the
 * centre of every source pixel flat and blends only a seam one screen pixel wide between two of them
 * (never more than two texels): per axis, with rr = 0.5 - 0.5 / scale and cd = fract(t) - 0.5,
 * texel = floor(t) + 0.5 + (cd - clamp(cd, -rr, rr)) * scale, sampled with GL_LINEAR. Drawn as one quad into the default framebuffer instead of the glBlitFramebuffer; on any
 * shader/link error it logs to qa.log once and the NEAREST blit stays in use.
 */
static GLuint vitaSbProg = 0, vitaSbVbo = 0;
static GLint vitaSbTexSize = -1, vitaSbScale = -1, vitaSbTex = -1;
static int vitaSbState = 0;   /* 0 not tried, 1 ready, -1 failed */

static void vitaSbLog(const char *what, GLuint obj, bool program)
{
    char info[512] = "";
    GLsizei n = 0;
    if (obj) {
        if (program) gl.GetProgramInfoLog(obj, sizeof(info) - 1, &n, info);
        else gl.GetShaderInfoLog(obj, sizeof(info) - 1, &n, info);
    }
    FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) { fprintf(f, "SHARP_BILINEAR %s %s\n", what, info); fclose(f); }
}

static bool vitaSbInit()
{
    static const char *vs =
        "attribute vec2 position;\n"
        "attribute vec2 texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "  v_texCoord = texCoord;\n"
        "  gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "precision highp float;\n"
        "uniform sampler2D texture;\n"
        "uniform vec2 texSize;\n"
        "uniform vec2 scale;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "  vec2 t = v_texCoord * texSize;\n"
        "  vec2 cd = fract(t) - vec2(0.5);\n"
        "  vec2 rr = vec2(0.5) - vec2(0.5) / scale;\n"
        "  vec2 f = (cd - clamp(cd, -rr, rr)) * scale + vec2(0.5);\n"
        "  gl_FragColor = texture2D(texture, (floor(t) + f) / texSize);\n"
        "}\n";
    GLuint v = gl.CreateShader(GL_VERTEX_SHADER), f = gl.CreateShader(GL_FRAGMENT_SHADER);
    gl.ShaderSource(v, 1, &vs, nullptr);
    gl.ShaderSource(f, 1, &fs, nullptr);
    gl.CompileShader(v);
    gl.CompileShader(f);
    GLint ok = 0;
    gl.GetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) { vitaSbLog("VERTEX_COMPILE_FAILED", v, false); return false; }
    gl.GetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) { vitaSbLog("FRAGMENT_COMPILE_FAILED", f, false); return false; }
    vitaSbProg = gl.CreateProgram();
    gl.AttachShader(vitaSbProg, v);
    gl.AttachShader(vitaSbProg, f);
    gl.BindAttribLocation(vitaSbProg, 0, "position");
    gl.BindAttribLocation(vitaSbProg, 1, "texCoord");
    gl.LinkProgram(vitaSbProg);
    gl.GetProgramiv(vitaSbProg, GL_LINK_STATUS, &ok);
    if (!ok) { vitaSbLog("LINK_FAILED", vitaSbProg, true); return false; }
    vitaSbTexSize = gl.GetUniformLocation(vitaSbProg, "texSize");
    vitaSbScale = gl.GetUniformLocation(vitaSbProg, "scale");
    vitaSbTex = gl.GetUniformLocation(vitaSbProg, "texture");
    /* Full-viewport strip; v = 0 at the top of the screen, as the flipped blit (src y 0 -> dst top). */
    static const GLfloat quad[16] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
    };
    gl.GenBuffers(1, &vitaSbVbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vitaSbVbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    vitaSbLog("READY", 0, false);
    return true;
}

/* Draws src (544x416) into the default framebuffer at x..x+w, full height. False: use the blit. */
static bool vitaSbPresent(TEXFBO &src, int x, int w)
{
    if (vitaSbState == 0)
        vitaSbState = vitaSbInit() ? 1 : -1;
    if (vitaSbState < 0)
        return false;

    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glState.viewport.pushSet(IntRect(x, 0, w, 544));
    glState.scissorTest.pushSet(false);
    glState.blend.pushSet(false);
    glState.program.set(vitaSbProg);
    gl.Uniform2f(vitaSbTexSize, 544.f, 416.f);
    gl.Uniform2f(vitaSbScale, (float)w / 544.f, 544.f / 416.f);
    gl.Uniform1i(vitaSbTex, 0);

    gl.ActiveTexture(GL_TEXTURE0);
    TEX::bind(src.tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (gl.BindVertexArray)
        gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, vitaSbVbo);
    gl.EnableVertexAttribArray(0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const GLvoid *)0);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const GLvoid *)(2 * sizeof(GLfloat)));
    ::glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);   /* not in the mkxp gl table */
    gl.DisableVertexAttribArray(0);
    gl.DisableVertexAttribArray(1);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);

    /* The scene texture is sampled NEAREST everywhere else (transitions, snapshots). */
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glState.blend.pop();
    glState.scissorTest.pop();
    glState.viewport.pop();
    return true;
}
#endif

void vitaRenderFrame()
{
#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT0 = sceKernelGetProcessTimeWide();
#endif

    static unsigned int vitaTraceFrame = 0;
    ++vitaTraceFrame;

    const bool vitaTrace =
        vitaTraceFrame <= 10 || (vitaTraceFrame % 300) == 0 || vitaTraceBurst > 0;

    if (vitaTraceBurst > 0)
        --vitaTraceBurst;

    if (vitaTraceFrame == 1)
        sceClibPrintf("VITA_TRACE first frame (primo draw titolo)\n");

    VITA_FREEZE_MARK(VITA_RENDER_ENTER);
    VITA_FRAME_TRACE("before prepareDraw");
    /*
     * Come in mkxp-z (ScreenScene::composite): prepareDraw()
     * prima di rilegare il target, perché WindowVX può lasciare
     * legato il proprio FBO offscreen.
     */
    VITA_FREEZE_MARK(PREPARE_ENTER);
#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagPrep0 = vitaDiagNow();
#endif
#if defined(MKXP_VITA_FRAME_HIST) && defined(MKXP_VITA_PERF_BITMAP)
    if (vitaDiagPhaseMark) vitaDiagPhaseMark(0, 1);
#endif
    SharedState::instance->prepareDraw();
#if defined(MKXP_VITA_FRAME_HIST) && defined(MKXP_VITA_PERF_BITMAP)
    if (vitaDiagPhaseMark) vitaDiagPhaseMark(0, 0);
#endif
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_FRAME_PREPARE, vitaDiagPrep0, 0, nullptr);
#endif
    VITA_FREEZE_MARK(PREPARE_EXIT);

    VITA_FRAME_TRACE("after prepareDraw");

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT1 = sceKernelGetProcessTimeWide();

    /* TileAtlasVX::build durante questo prepareDraw() imposta vitaTraceBurst = 10. */
    if (vitaTraceBurst == 10) {
        ++vitaPerfAtlasBuilds;
        if (!vitaPerfMap) {
            vitaPerfFlush(vitaTraceFrame - 1);
            vitaPerfMap = true;
        }
    }
#endif

    /*
     * Come il PingPong di mkxp-z: la scena viene disegnata in un
     * FBO 544x416 (spazio logico RGSS), non direttamente su FB0.
     * Il TEXFBO viene richiesto una sola volta al texPool e
     * riutilizzato per tutta la durata dell'app.
     */
    VITA_FREEZE_MARK(SCENE_FBO_SETUP_ENTER);
    static TEXFBO sceneFBO;

    if (sceneFBO.tex == TEX::ID(0)) {
        sceneFBO = testTexPool->request(544, 416);
#ifdef MKXP_VITA_SCENE_STENCIL
        FBO::bind(sceneFBO.fbo);
        vitaSceneDepthStencil(sceneFBO);
#endif
    }

    FBO::bind(sceneFBO.fbo);
#ifdef MKXP_VITA_VIEWPORT_GRAY
    vitaSceneBase = &sceneFBO;
    vitaSceneCur = &sceneFBO;
    vitaGrayPassesThisFrame = 0;
#endif

    testGLState->viewport.set(
        IntRect(
            0,
            0,
            544,
            416
        )
    );

    /*
     * Pulizia del sceneFBO con un quad nero fullscreen: su vitaGL
     * gl.Clear non ha effetto su questo FBO (verificato con il test
     * del quad magenta), mentre un draw normale sì.
     */
    static FlatColorShader vitaClearShader;
    static Quad vitaClearQuad;

    vitaClearShader.bind();
    vitaClearShader.applyViewportProj();
    vitaClearShader.setTranslation(Vec2i());
    vitaClearShader.setColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));

    vitaClearQuad.setPosRect(FloatRect(0, 0, 544, 416));
    vitaClearQuad.draw();
    VITA_FREEZE_MARK(SCENE_FBO_SETUP_EXIT);

    /*
     * Disegna automaticamente tutti gli elementi
     * registrati nella Scene, nel loro ordine Z.
     */
    VITA_FRAME_TRACE("before composite");

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT2 = sceKernelGetProcessTimeWide();
#endif

    VITA_FREEZE_MARK(COMPOSITE_ENTER);
#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagComp0 = vitaDiagNow();
#endif
#if defined(MKXP_VITA_FRAME_HIST) && defined(MKXP_VITA_PERF_BITMAP)
    if (vitaDiagPhaseMark) vitaDiagPhaseMark(1, 1);
#endif
    testScene->composite();
#if defined(MKXP_VITA_FRAME_HIST) && defined(MKXP_VITA_PERF_BITMAP)
    if (vitaDiagPhaseMark) vitaDiagPhaseMark(1, 0);
#endif
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_FRAME_COMPOSITE, vitaDiagComp0, 0, nullptr);
#endif
    VITA_FREEZE_MARK(COMPOSITE_EXIT);

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT3 = sceKernelGetProcessTimeWide();
#endif

    VITA_FRAME_TRACE("after composite");

    /*
     * Blit finale su FB0, capovolto in verticale, come
     * metaBlitBufferFlippedScaled() di mkxp-z (percorso nativo
     * di GLMeta: BindFramebuffer READ/DRAW + BlitFramebuffer).
     * Scaling invariato rispetto a prima: 544x416 -> 960x544.
     */
    VITA_FRAME_TRACE("before blit");

#ifndef MKXP_VITA_NO_QA_FINAL_BLIT_BEFORE
    FILE *f_blit = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f_blit) { fprintf(f_blit, "FINAL_BLIT_BEFORE\n"); fclose(f_blit); }
#endif

#if defined(MKXP_VITA_PRE_BLIT_DELAY_US) && defined(MKXP_VITA_DELAY_TOGGLE)
    /*
     * Test (MKXP_VITA_DELAY_TOGGLE): the pre-blit delay starts at MKXP_VITA_PRE_BLIT_DELAY_US and
     * SELECT cycles it 10000 -> 5000 -> 2000 -> 0 -> 10000 us at runtime (qa.log + PERF delay_us=).
     */
    {
        static const int vitaDelaySteps[] = { 10000, 5000, 2000, 0 };
        static int vitaDelayIdx = -1;
        static bool vitaSelectPrev = false;
        if (vitaDelayIdx < 0) {
            vitaDelayIdx = 0;
            for (int i = 0; i < 4; ++i)
                if (vitaDelaySteps[i] == MKXP_VITA_PRE_BLIT_DELAY_US)
                    vitaDelayIdx = i;
#ifdef MKXP_VITA_DELAY_START_ZERO
            /* Perf fix: no pre-blit delay by default (d36/d37: 0 ms without the d15 crash, maps
             * 42 -> 57 fps); SELECT still cycles 0 -> 10000 -> 5000 -> 2000 us as a fallback. */
            vitaDelayIdx = 3;
#endif
        }
        SceCtrlData vitaPad;
        const bool vitaSelect = sceCtrlPeekBufferPositive(0, &vitaPad, 1) > 0 && (vitaPad.buttons & SCE_CTRL_SELECT);
        if (vitaSelect && !vitaSelectPrev) {
            vitaDelayIdx = (vitaDelayIdx + 1) % 4;
            FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
            if (f) { fprintf(f, "DELAY_TOGGLE pre_blit_delay_us=%d\n", vitaDelaySteps[vitaDelayIdx]); fclose(f); }
        }
        vitaSelectPrev = vitaSelect;
        vitaCurrentDelayUs = vitaDelaySteps[vitaDelayIdx];
        VITA_FREEZE_MARK(PREBLIT_DELAY_ENTER);
        if (vitaCurrentDelayUs > 0)
            sceKernelDelayThread(vitaCurrentDelayUs);
        VITA_FREEZE_MARK(PREBLIT_DELAY_EXIT);
    }
#elif defined(MKXP_VITA_PRE_BLIT_DELAY_US)
    /* Timing test only: wait between composite() and the final blit (FINAL_BLIT_BEFORE call site). */
    VITA_FREEZE_MARK(PREBLIT_DELAY_ENTER);
    sceKernelDelayThread(MKXP_VITA_PRE_BLIT_DELAY_US);
    VITA_FREEZE_MARK(PREBLIT_DELAY_EXIT);
#endif

#ifdef MKXP_VITA_PRE_BLIT_BUSYWAIT_US
    /* Timing test only: spin for the same time without sleeping or yielding.
     * The inner loop only thins out timer reads; the duration comes from the timer. */
    {
        const SceUInt64 vitaSpinStart = sceKernelGetProcessTimeWide();
        while (sceKernelGetProcessTimeWide() - vitaSpinStart < (SceUInt64)MKXP_VITA_PRE_BLIT_BUSYWAIT_US) {
            for (volatile int vitaSpin = 0; vitaSpin < 1000; ++vitaSpin) {
            }
        }
    }
#endif

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT4 = sceKernelGetProcessTimeWide();
#endif

    VITA_FREEZE_MARK(FINAL_BLIT_ENTER);
#ifdef MKXP_VITA_SCREEN_FX
#ifdef MKXP_VITA_VIEWPORT_GRAY
    /* The composite may have continued in another buffer (gray viewport tones). */
    TEXFBO &vitaFxOut = vitaFxApply(*vitaSceneCur);
#else
    TEXFBO &vitaFxOut = vitaFxApply(sceneFBO);
#endif
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, vitaFxOut.fbo.gl);
#else
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, sceneFBO.fbo.gl);
#endif
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

#ifdef MKXP_VITA_FINAL_FILTER_LINEAR
#define VITA_FINAL_FILTER GL_LINEAR
#else
#define VITA_FINAL_FILTER GL_NEAREST
#endif

#if defined(MKXP_VITA_ASPECT_CORRECT) || defined(MKXP_VITA_PIXEL_PERFECT)
    /*
     * Aspect-correct presentation (final blit only; sceneFBO and RGSS coordinates unchanged):
     * 544x416 scaled uniformly to MKXP_VITA_ASPECT_WIDTH x 544, centred, black side bars.
     * The whole Vita framebuffer is cleared first so the bars never keep old content.
     */
    {
        glState.viewport.pushSet(IntRect(0, 0, 960, 544));
        glState.scissorTest.pushSet(false);
        glState.clearColor.pushSet(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        gl.Clear(GL_COLOR_BUFFER_BIT);
        glState.clearColor.pop();
        glState.scissorTest.pop();
        glState.viewport.pop();

#ifdef MKXP_VITA_PIXEL_PERFECT
        /*
         * Pixel-perfect 1x (MKXP_VITA_PRESENTATION_MODE=2): 544x416 copied 1:1, centred in
         * 960x544: bars 208 left/right, 64 top/bottom. Vertical flip as the other modes.
         */
        gl.BlitFramebuffer(
            0, 0, 544, 416,
            208, 480, 208 + 544, 64,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );
#else
        const int vitaAspectX = (960 - MKXP_VITA_ASPECT_WIDTH) / 2;
#ifdef MKXP_VITA_FINAL_SHARP_BILINEAR
#ifdef MKXP_VITA_SCREEN_FX
        if (!vitaSbPresent(vitaFxOut, vitaAspectX, MKXP_VITA_ASPECT_WIDTH))
#else
        if (!vitaSbPresent(sceneFBO, vitaAspectX, MKXP_VITA_ASPECT_WIDTH))
#endif
#endif
        gl.BlitFramebuffer(
            0, 0, 544, 416,
            vitaAspectX, 544, vitaAspectX + MKXP_VITA_ASPECT_WIDTH, 0,
            GL_COLOR_BUFFER_BIT,
            VITA_FINAL_FILTER
        );
#endif
    }
#else
    gl.BlitFramebuffer(
        0, 0, 544, 416,
        0, 544, 960, 0,
        GL_COLOR_BUFFER_BIT,
        VITA_FINAL_FILTER
    );
#endif

    FBO::unbind();
    VITA_FREEZE_MARK(FINAL_BLIT_EXIT);

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT5 = sceKernelGetProcessTimeWide();
#endif

#ifndef MKXP_VITA_NO_QA_FINAL_BLIT_AFTER
    FILE *f_blit_after = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f_blit_after) { fprintf(f_blit_after, "FINAL_BLIT_AFTER\n"); fclose(f_blit_after); }
#endif

    VITA_FRAME_TRACE("after blit");

    /*
     * Presenta il frame sul display Vita.
     */
    VITA_FRAME_TRACE("before vglSwapBuffers");

#ifndef MKXP_VITA_NO_QA_FRAME
    static unsigned int frameCounter = 0;
    ++frameCounter;
    FILE *f = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f) {
        fprintf(f, "FRAME %u\n", frameCounter);
        fclose(f);
    }
#endif

#ifndef MKXP_VITA_NO_QA_SWAP_BEFORE
    FILE *f_swap = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f_swap) { fprintf(f_swap, "SWAP_BEFORE\n"); fclose(f_swap); }
#endif

#ifdef MKXP_VITA_GLFINISH_BEFORE_SWAP
    /* Sync test only: wait for the GPU to finish all queued work before presenting. */
    glFinish();
#endif

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT6 = sceKernelGetProcessTimeWide();
#endif

    VITA_FREEZE_MARK(SWAP_ENTER);
#ifdef MKXP_VITA_DIAG
    const uint64_t vitaDiagSwap0 = vitaDiagNow();
#endif
    vglSwapBuffers(GL_FALSE);
#ifdef MKXP_VITA_DIAG
    vitaDiagSpan(VD_FRAME_SWAP, vitaDiagSwap0, 0, nullptr);
#endif
    VITA_FREEZE_MARK(SWAP_EXIT);

#ifdef MKXP_VITA_PERF_PROFILE
    const SceUInt64 vitaT7 = sceKernelGetProcessTimeWide();
#endif

#ifdef MKXP_VITA_POST_SWAP_DELAY_US
    /* Timing test only: slow the CPU->GPU frame rate after each present. */
    sceKernelDelayThread(MKXP_VITA_POST_SWAP_DELAY_US);
#endif

#ifndef MKXP_VITA_NO_QA_SWAP_AFTER
    FILE *f_swap_after = fopen(VITA_GAME_ROOT "qa.log", "a");
    if (f_swap_after) { fprintf(f_swap_after, "SWAP_AFTER\n"); fclose(f_swap_after); }
#endif

    VITA_FRAME_TRACE("after vglSwapBuffers");

#ifdef MKXP_VITA_HEARTBEAT_LOG
    vitaHeartbeatAfterPresent();
#endif

#ifdef MKXP_VITA_PERF_PROFILE
    {
        const SceUInt64 vitaT8 = sceKernelGetProcessTimeWide();
        const SceUInt64 total = vitaT8 - vitaT0;
        const SceUInt64 parts =
            (vitaT1 - vitaT0) + (vitaT2 - vitaT1) + (vitaT3 - vitaT2) +
            (vitaT4 - vitaT3) + (vitaT5 - vitaT4) + (vitaT7 - vitaT6);

        if (vitaPerfLastExit != 0) {
            vitaPerfAdd(VITA_PERF_OUTSIDE, vitaT0 - vitaPerfLastExit);
            vitaPerfAdd(VITA_PERF_FRAME, vitaT8 - vitaPerfLastExit);
#if defined(MKXP_VITA_FRAME_HIST) && defined(MKXP_VITA_PERF_BITMAP)
            if (vitaDiagFrameEnd)
                vitaDiagFrameEnd(vitaT8 - vitaPerfLastExit, vitaT0 - vitaPerfLastExit);
#endif
        }
        vitaPerfAdd(VITA_PERF_TOTAL, total);
        vitaPerfAdd(VITA_PERF_PREPARE, vitaT1 - vitaT0);
        vitaPerfAdd(VITA_PERF_SETUP, vitaT2 - vitaT1);
        vitaPerfAdd(VITA_PERF_COMPOSITE, vitaT3 - vitaT2);
        vitaPerfAdd(VITA_PERF_DELAY, vitaT4 - vitaT3);
        vitaPerfAdd(VITA_PERF_BLIT, vitaT5 - vitaT4);
        vitaPerfAdd(VITA_PERF_SWAP, vitaT7 - vitaT6);
        vitaPerfAdd(VITA_PERF_REST, total - parts);

        if ((vitaTraceFrame % VITA_PERF_WINDOW_FRAMES) == 0) {
            vitaPerfFlush(vitaTraceFrame);
#ifdef MKXP_VITA_PERF_PROFILE_FILE
#ifdef MKXP_VITA_LOG_BATCH
            if (vitaPerfWindow % MKXP_VITA_LOG_BATCH == 0 || vitaPerfPendingLen > (int)sizeof(vitaPerfPending) * 3 / 4)
#endif
            vitaPerfWritePending();
#endif
        }

        /* Il tempo del riepilogo resta fuori da tutte le regioni. */
        vitaPerfLastExit = sceKernelGetProcessTimeWide();
    }
#endif
#ifdef MKXP_VITA_DEFERRED_GL_DELETE
    /* Deletes the TEXFBOs released at least a few swaps ago (gl-util.h). */
    vitaDeferredGLDeleteTick();
#endif
#ifdef MKXP_VITA_DIAG
    /* Closes/writes the one-shot timelines on their frame count (after the swap). */
    vitaDiagFrameDone();
#endif
    VITA_FREEZE_MARK(VITA_RENDER_EXIT);
}
