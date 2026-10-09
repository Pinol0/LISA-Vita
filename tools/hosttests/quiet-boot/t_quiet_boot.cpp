// MKXP_VITA_QUIET_BOOT: the real boot-log block of src/main.cpp (extracted by run.sh into block.inc)
// with stubs for the debug screen, the display and the memory blocks.
//  1. boot messages are not printed; the "LISA: The Painful / Loading..." screen is;
//  2. the log goes to qa.log once, when the game scripts start (BOOT_LOG ok);
//  3. a failure while the boot screen is shown: the whole log on screen, later lines too;
//  4. a failure after the game has drawn (display not ours): nothing on screen, BOOT_LOG failed.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <png.h>
#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
typedef int SceUID;
struct SceDisplayFrameBuf { unsigned size; void *base; unsigned pitch, pixelformat, width, height; };
enum { SCE_DISPLAY_SETBUF_NEXTFRAME = 1, SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 = 0, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW = 0 };
struct PsvDebugScreenFont { unsigned char *glyphs, width, height, first, last, size_w, size_h; };
static std::string gScreen; static int gInits = 0; static char gDebugFb[4], gGlFb[4]; static void *gShown = nullptr;
static PsvDebugScreenFont gFont = { nullptr, 8, 8, 0, 255, 8, 8 }, gFont2 = { nullptr, 16, 16, 0, 255, 16, 16 };
extern "C" {
int psvDebugScreenInit() { ++gInits; gShown = gDebugFb; return 0; }
int psvDebugScreenPuts(const char *t) { gScreen += t; return (int)strlen(t); }
PsvDebugScreenFont *psvDebugScreenGetFont(void) { return &gFont; }
PsvDebugScreenFont *psvDebugScreenSetFont(PsvDebugScreenFont *f) { return f; }
PsvDebugScreenFont *psvDebugScreenScaleFont2x(PsvDebugScreenFont *) { return &gFont2; }
void psvDebugScreenSetCoordsXY(int *, int *) {}
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *fb, int) { fb->base = gShown; return 0; }
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *fb, int) { gShown = fb->base; return 0; }
SceUID sceKernelAllocMemBlock(const char *, int, unsigned, void *) { return -1; }
int sceKernelGetMemBlockBase(SceUID, void **) { return -1; }
int sceKernelFreeMemBlock(SceUID) { return 0; }
}
#define MKXP_VITA_QUIET_BOOT
#define MKXP_VITA_BOOT_TITLE "LISA: The Painful"   /* main.cpp default (CMake MKXP_VITA_BOOT_TITLE) */
#include "block.inc"
static std::string qa() { std::string s; if (FILE *f = fopen(VITA_GAME_ROOT "qa.log", "r")) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f))) s.append(b, n); fclose(f); } return s; }
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { ++fails; printf("FAIL %s\n", m); } else printf("PASS %s\n", m); } while (0)
int main(int argc, char **argv)
{
    const bool lateFailure = argc > 1;
    remove(VITA_GAME_ROOT "qa.log");
    vitaBootScreenStart();
    CHECK(gScreen.find("LISA: The Painful") != std::string::npos && gScreen.find("Loading...") != std::string::npos, "loading screen text");
    const std::string loading = gScreen;
    psvDebugScreenPrintf("LISA-Vita boot\n\n");
    psvDebugScreenPrintf("Ruby OK\n");
    psvDebugScreenPrintf("Script count: %ld\n", 153L);
    CHECK(gScreen == loading, "boot messages not printed");
    if (!lateFailure) {
        psvDebugScreenPrintf("\nRunning LISA scripts in order...\n");
        const std::string q = qa();
        CHECK(q.find("BOOT_LOG ok") != std::string::npos && q.find("Script count: 153") != std::string::npos, "log written to qa.log when the scripts start");
        psvDebugScreenPrintf("Running LISA scripts again\n");
        CHECK(qa() == q, "written once");
        psvDebugScreenPrintf("Scripts FAILED: No such file\n");
        CHECK(gScreen.find("Script count: 153") != std::string::npos && gScreen.find("Scripts FAILED") != std::string::npos, "failure during boot: whole log on screen");
        psvDebugScreenPrintf("\nCould not read x\n");
        CHECK(gScreen.find("Could not read x") != std::string::npos, "later lines printed too");
    } else {
        gShown = gGlFb;   /* vitaGL has presented a frame */
        psvDebugScreenPrintf("\nSCRIPT FAILED!\n");
        CHECK(gScreen == loading, "failure after the game drew: screen left to the game");
        CHECK(qa().find("BOOT_LOG failed") != std::string::npos, "failure logged to qa.log");
    }
    printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
