// Host test for src/vita-predecode.cpp (MKXP_VITA_ANIM_PREDECODE) with the real VitaImageCache and
// vita-prefetch; the Vita sound decoder (vitaDecodeAudioByName) is replaced by a deterministic stub.
// usage: run.sh PNG... (PNGs of at least 1 MiB decoded, e.g. Graphics/Animations sheets)
#include "vita-image-cache.h"
#include "vita-predecode.h"
#include "vita-prefetch.h"
#include <png.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unistd.h>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL " __VA_ARGS__); std::printf("\n"); } } while (0)
unsigned gSoundDelayUs = 0;
bool vitaDecodeAudioByName(const char *filename, std::vector<uint8_t> &data, int &ss, int &ch, int &rate)
{
    if (gSoundDelayUs) usleep(gSoundDelayUs);
    if (std::strstr(filename, "missing")) return false;
    data.assign(200000, 0);
    for (size_t i = 0; i < data.size(); ++i) data[i] = (uint8_t)(i * 31 + std::strlen(filename));
    ss = 2; ch = 2; rate = 44100;
    return true;
}
static bool decode(const std::string &p, int &w, int &h, std::vector<unsigned char> &px)
{
    png_image im; std::memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, p.c_str())) return false;
    im.format = PNG_FORMAT_RGBA; px.resize(PNG_IMAGE_SIZE(im));
    bool ok = png_image_finish_read(&im, nullptr, px.data(), 0, nullptr); w = im.width; h = im.height; png_image_free(&im); return ok;
}
static std::vector<unsigned char> slurp(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}
static void wait_ms(int ms) { usleep(ms * 1000); }
extern "C" void vitaPredecodeStatsC(unsigned *, unsigned *, unsigned *, unsigned *, unsigned *, unsigned *, unsigned *);
int main(int argc, char **argv)
{
    char tmpl[] = "/tmp/predecXXXXXX";
    const std::string root = std::string(mkdtemp(tmpl)) + "/";
    VitaImageCache::setDir(root + "cache/");
    std::vector<std::string> srcs;
    for (int a = 1; a < argc; ++a) {
        const std::string s = root + "img" + std::to_string(a) + ".png";
        std::ofstream(s, std::ios::binary) << std::ifstream(argv[a], std::ios::binary).rdbuf();
        srcs.push_back(s);
    }
    CHECK(srcs.size() >= 2, "need at least 2 PNGs");
    // entries for all but the last image
    for (size_t i = 0; i + 1 < srcs.size(); ++i) {
        int w, h; std::vector<unsigned char> px;
        CHECK(decode(srcs[i], w, h, px) && px.size() >= VitaImageCache::kMinBytes, "decode %s (>= 1 MiB)", srcs[i].c_str());
        VitaImageCache::store(srcs[i], w, h, px.data());
    }
    vitaPredecodeSetCaps(64u << 20, 3u << 20);
    // 1. images with an entry: pixels equal VitaImageCache::load
    for (size_t i = 0; i + 1 < srcs.size(); ++i) vitaPredecodeImage(srcs[i]);
    wait_ms(1500);
    for (size_t i = 0; i + 1 < srcs.size(); ++i) {
        int w1, h1, w2, h2; std::vector<unsigned char> a, b;
        CHECK(vitaPredecodeTakeImage(srcs[i], w1, h1, a), "taken %zu", i);
        CHECK(VitaImageCache::load(srcs[i], w2, h2, b) && w1 == w2 && h1 == h2 && a == b, "pixels == load %zu", i);
        CHECK(!vitaPredecodeTakeImage(srcs[i], w1, h1, a), "taken once %zu", i);
    }
    // 2. no entry -> handed to the read-ahead of the PNG bytes
    const std::string last = srcs.back();
    vitaPredecodeImage(last); wait_ms(500);
    { int w, h; std::vector<unsigned char> px; CHECK(!vitaPredecodeTakeImage(last, w, h, px), "no entry: not decoded"); }
    { std::vector<unsigned char> bytes; CHECK(vitaPrefetchTake(last, bytes) && bytes == slurp(last), "no entry: PNG read ahead"); }
    // 3. over the cap -> handed to the read-ahead of the entry
    vitaPredecodeSetCaps(1024, 3u << 20);
    vitaPredecodeImage(srcs[0]); wait_ms(500);
    { int w, h; std::vector<unsigned char> px; CHECK(!vitaPredecodeTakeImage(srcs[0], w, h, px), "over cap: not decoded"); }
    { std::vector<unsigned char> bytes; const std::string e = VitaImageCache::entryPathFor(srcs[0]);
      CHECK(vitaPrefetchTake(e, bytes) && bytes == slurp(e), "over cap: entry read ahead"); }
    vitaPredecodeSetCaps(64u << 20, 3u << 20);
    // 4. sounds: equal the decoder; failure -> false
    vitaPredecodeSound("Audio/SE/Echo"); vitaPredecodeSound("Audio/SE/missing"); wait_ms(300);
    { std::vector<uint8_t> a, b; int ss, ch, r, ss2, ch2, r2;
      CHECK(vitaPredecodeTakeSound("Audio/SE/Echo", a, ss, ch, r), "sound taken");
      vitaDecodeAudioByName("Audio/SE/Echo", b, ss2, ch2, r2);
      CHECK(a == b && ss == ss2 && ch == ch2 && r == r2, "sound == decoder");
      CHECK(!vitaPredecodeTakeSound("Audio/SE/missing", a, ss, ch, r), "failed decode -> caller decodes"); }
    // 5. being decoded -> waited for; queued -> left to the caller
    gSoundDelayUs = 300000;
    vitaPredecodeSound("Audio/SE/a"); vitaPredecodeSound("Audio/SE/b"); wait_ms(50);
    { std::vector<uint8_t> p; int ss, ch, r;
      CHECK(!vitaPredecodeTakeSound("Audio/SE/b", p, ss, ch, r), "queued -> caller");
      CHECK(vitaPredecodeTakeSound("Audio/SE/a", p, ss, ch, r) && p.size() == 200000, "waited for a"); }
    // 6. clear while working: dropped, memory back to 0
    vitaPredecodeSound("Audio/SE/c"); wait_ms(50);
    vitaPredecodeClear();
    { std::vector<uint8_t> p; int ss, ch, r; CHECK(!vitaPredecodeTakeSound("Audio/SE/c", p, ss, ch, r), "dropped"); }
    wait_ms(500);
    gSoundDelayUs = 0;
    // 7. done but never taken: clear frees it
    vitaPredecodeImage(srcs[0]); vitaPredecodeSound("Audio/SE/d"); wait_ms(800);
    unsigned q, t, w, d, h, ikb, skb;
    vitaPredecodeStatsC(&q, &t, &w, &d, &h, &ikb, &skb);
    CHECK(ikb > 1000 && skb == 195, "held before clear: %u KiB images, %u KiB sound", ikb, skb);
    vitaPredecodeClear();
    vitaPredecodeStatsC(&q, &t, &w, &d, &h, &ikb, &skb);
    CHECK(ikb == 0 && skb == 0, "held after clear: %u %u", ikb, skb);
    CHECK(w == 1 && h == 2, "waits %u, handed %u", w, h);
    std::printf(fails ? "FAIL %d\n" : "PASS predecode\n", fails);
    std::system(("rm -rf " + root).c_str());
    return fails ? 1 : 0;
}
