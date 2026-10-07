// Host test for VitaImageCache::loadInto (MKXP_VITA_TEX_DIRECT_CACHE): for random images, an entry
// decompressed into a destination with vitaGL's row stride (VGL_ALIGN(w, 8) * 4) must hold exactly
// the rows load() gives, write nothing past stride * h, and be refused (false, nothing written) for a
// damaged entry, a destination that gives up or a stride shorter than a row.
//   g++ -std=gnu++17 -O2 -DMKXP_VITA_TEX_DIRECT_CACHE t_load_into.cpp ../../../src/vita-image-cache.cpp \
//       ../../../third_party/lz4/lz4.c -lpng -o t_load_into && ./t_load_into
#include "../../../src/vita-image-cache.h"
#include <png.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { ++fails; std::printf("FAIL %s\n", m); } } while (0)

struct Dst {
    std::vector<unsigned char> mem;
    size_t stride = 0, strideOverride = 0;
    bool giveUp = false;
    int calls = 0, w = 0, h = 0;
};
static const size_t kGuard = 4096;
static unsigned char *dstFn(void *ctx, int w, int h, size_t *stride)
{
    Dst &d = *static_cast<Dst *>(ctx);
    ++d.calls; d.w = w; d.h = h;
    if (d.giveUp) return nullptr;
    d.stride = d.strideOverride ? d.strideOverride : (size_t)((w + 7) & ~7) * 4;
    d.mem.assign(d.stride * h + kGuard, 0xCD);
    *stride = d.stride;
    return d.mem.data();
}

/* Entries of the other format version must be misses: "store DIR" writes entries for 3 images with
 * this build's format, "stale DIR" (a build of the other version) checks that none of them loads. */
static int crossVersion(const char *mode, const std::string &root)
{
    VitaImageCache::setDir(root + "cache/");
    int bad = 0;
    for (int t = 0; t < 3; ++t) {
        const int w = 600 + t, h = 500;
        std::vector<unsigned char> px((size_t)w * h * 4, (unsigned char)(t * 40));
        const std::string src = root + "x" + std::to_string(t) + ".png";
        if (!std::strcmp(mode, "store")) {
            png_image im; std::memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
            im.width = w; im.height = h; im.format = PNG_FORMAT_RGBA;
            png_image_write_to_file(&im, src.c_str(), 0, px.data(), 0, nullptr);
            VitaImageCache::store(src, w, h, px.data());
            int lw, lh; std::vector<unsigned char> back;
            bad += !(VitaImageCache::load(src, lw, lh, back) && back == px);
        } else {
            int lw, lh; std::vector<unsigned char> back;
            Dst d;
            bad += VitaImageCache::load(src, lw, lh, back) || VitaImageCache::loadInto(src, dstFn, &d);
        }
    }
    std::printf("%s  %s: %d of 3 wrong\n", bad ? "FAIL" : "PASS", mode, bad);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 3)
        return crossVersion(argv[1], argv[2]);
    char tmpl[] = "/tmp/loadintoXXXXXX";
    const std::string root = std::string(mkdtemp(tmpl)) + "/";
    VitaImageCache::setDir(root + "cache/");
    std::mt19937 rng(7);
    auto R = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    int cases = 0, unaligned = 0;
    for (int t = 0; t < 40; ++t) {
        /* t == 4: rows wider than a chunk (MKXP_VITA_DCACHE_CHUNKS: one row per chunk) */
        const int w = t < 4 ? 512 + t : t == 4 ? 70001 : R(300, 1100);
        const int h = t == 4 ? 5 : (int)((1u << 20) / 4 / w) + R(1, 300);   /* >= 1 MiB decoded */
        if (w % 8) ++unaligned;
        std::vector<unsigned char> px((size_t)w * h * 4);
        for (size_t i = 0; i < px.size(); i += 4) {
            const unsigned k = rng() % 6;
            for (int c = 0; c < 4; ++c) px[i + c] = k < 4 ? (unsigned char)(k * 60 + c * 7) : (unsigned char)(rng() % 256);
        }
        const std::string src = root + "img" + std::to_string(t) + ".png";
        png_image im; std::memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
        im.width = w; im.height = h; im.format = PNG_FORMAT_RGBA;
        if (!png_image_write_to_file(&im, src.c_str(), 0, px.data(), 0, nullptr)) { std::printf("png write\n"); return 2; }
        VitaImageCache::store(src, w, h, px.data());

        int lw = 0, lh = 0; std::vector<unsigned char> ref;
        CHECK(VitaImageCache::load(src, lw, lh, ref) && lw == w && lh == h && ref == px, "load() reference");

        Dst d;
        CHECK(VitaImageCache::loadInto(src, dstFn, &d), "loadInto ok");
        CHECK(d.calls == 1 && d.w == w && d.h == h, "dst asked once with the size");
        bool rows = true;
        for (int y = 0; y < h && rows; ++y)
            rows = !std::memcmp(d.mem.data() + (size_t)y * d.stride, px.data() + (size_t)y * w * 4, (size_t)w * 4);
        CHECK(rows, "every row at its stride");
        bool guard = true;
        for (size_t i = d.stride * h; i < d.mem.size(); ++i) guard = guard && d.mem[i] == 0xCD;
        CHECK(guard, "nothing written past stride * h");

        Dst g; g.giveUp = true;
        CHECK(!VitaImageCache::loadInto(src, dstFn, &g) && g.calls == 1, "dst gives up -> false");
        Dst s; s.strideOverride = (size_t)w * 4 - 4;
        CHECK(!VitaImageCache::loadInto(src, dstFn, &s), "stride shorter than a row -> false");

        if (t % 5 == 0) {   /* damage one byte of the LZ4 block: refused before dst is asked */
            const std::string e = VitaImageCache::entryPathFor(src);
            FILE *f = std::fopen(e.c_str(), "r+b");
            std::fseek(f, -10, SEEK_END); int c = std::fgetc(f); std::fseek(f, -10, SEEK_END); std::fputc(c ^ 0x5a, f); std::fclose(f);
            Dst b;
            CHECK(!VitaImageCache::loadInto(src, dstFn, &b) && b.calls == 0, "damaged entry -> false, dst not asked");
        }
        if (t % 7 == 0) {   /* source changed after the entry: stale */
            struct stat st; stat(src.c_str(), &st);
            FILE *f = std::fopen(src.c_str(), "ab"); std::fputc(0, f); std::fclose(f);
            Dst b;
            CHECK(!VitaImageCache::loadInto(src, dstFn, &b) && b.calls == 0, "stale entry -> false");
        }
        Dst m;
        CHECK(!VitaImageCache::loadInto(root + "missing.png", dstFn, &m) && m.calls == 0, "no entry -> false");
        ++cases;
    }
    std::printf("cases=%d unaligned_widths=%d fails=%d\n%s\n", cases, unaligned, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
