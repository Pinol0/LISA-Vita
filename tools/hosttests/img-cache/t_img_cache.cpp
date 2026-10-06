// Host test for src/vita-image-cache.cpp with real LISA animation sheets.
//   g++ -std=gnu++17 -O2 t_img_cache.cpp ../../../src/vita-image-cache.cpp ../../../third_party/lz4/lz4.c -lpng -o t && ./t PNG...
#include "../../../src/vita-image-cache.h"
#include <png.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <vector>
static int fails = 0;
#define CHECK(c, m) do { bool ok_ = (c); std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", m); if (!ok_) ++fails; } while (0)
static bool decode(const std::string &p, int &w, int &h, std::vector<unsigned char> &px) {
    png_image im; std::memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, p.c_str())) return false;
    im.format = PNG_FORMAT_RGBA; px.resize(PNG_IMAGE_SIZE(im));
    bool ok = png_image_finish_read(&im, nullptr, px.data(), 0, nullptr); w = im.width; h = im.height; png_image_free(&im); return ok;
}
static double ms(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); }
int main(int argc, char **argv) {
    char tmpl[] = "/tmp/imgcacheXXXXXX"; std::string dir = std::string(mkdtemp(tmpl)) + "/cache/";
    VitaImageCache::setDir(dir);
    for (int a = 1; a < argc; ++a) {
        // work on a copy, so the test can change its mtime
        std::string src = dir.substr(0, dir.size() - 6) + "src" + std::to_string(a) + ".png";
        { FILE *i = std::fopen(argv[a], "rb"), *o = std::fopen(src.c_str(), "wb"); char b[65536]; size_t n;
          while ((n = std::fread(b, 1, sizeof b, i)) > 0) std::fwrite(b, 1, n, o); std::fclose(i); std::fclose(o); }
        int w, h; std::vector<unsigned char> px;
        auto t0 = std::chrono::steady_clock::now(); decode(src, w, h, px); double td = ms(t0);
        int w2, h2; std::vector<unsigned char> out;
        CHECK(!VitaImageCache::load(src, w2, h2, out), "no entry before the first store");
        t0 = std::chrono::steady_clock::now(); VitaImageCache::store(src, w, h, px.data()); double ts = ms(t0);
        t0 = std::chrono::steady_clock::now(); bool hit = VitaImageCache::load(src, w2, h2, out); double tl = ms(t0);
        struct stat st; stat((dir + "x").substr(0, 0).c_str(), &st);
        CHECK(hit && w2 == w && h2 == h && out == px, (std::string("identical pixels after store+load: ") + argv[a]).c_str());
        std::printf("      %dx%d decode %.1f ms, store %.1f ms, load %.1f ms\n", w, h, td, ts, tl);
        struct utimbuf ut = { 1000000000, 1000000000 }; utime(src.c_str(), &ut);
        CHECK(!VitaImageCache::load(src, w2, h2, out), "source mtime changed -> miss");
        VitaImageCache::store(src, w, h, px.data());
        CHECK(VitaImageCache::load(src, w2, h2, out) && out == px, "re-stored after the change -> hit again");
    }
    // corrupted entry -> miss, never a crash
    if (argc > 1) {
        std::string src = dir.substr(0, dir.size() - 6) + "src1.png";
        int w, h; std::vector<unsigned char> px, out; decode(src, w, h, px);
        std::string cmd = "for f in " + dir + "img_*.lz4; do printf 'garbage' | dd of=$f bs=1 seek=200 conv=notrunc 2>/dev/null; done";
        system(cmd.c_str());
        bool hit = VitaImageCache::load(src, w, h, out);
        CHECK(!hit, "corrupted LZ4 block: miss (checksum)");
    }
    // PNG decoded from a whole-file buffer (MKXP_VITA_BIG_READS) == decoded from the file
    for (int a = 1; a < argc; ++a) {
        std::vector<unsigned char> buf, px1, px2; int w1, h1;
        decode(argv[a], w1, h1, px1);
        bool rd = VitaImageCache::readWholeFile(argv[a], buf);
        png_image im; std::memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
        bool ok = rd && png_image_begin_read_from_memory(&im, buf.data(), buf.size());
        if (ok) { im.format = PNG_FORMAT_RGBA; px2.resize(PNG_IMAGE_SIZE(im)); ok = png_image_finish_read(&im, nullptr, px2.data(), 0, nullptr); }
        CHECK(ok && px1 == px2, (std::string("PNG from memory == PNG from file: ") + argv[a]).c_str());
    }
    { std::vector<unsigned char> b; CHECK(!VitaImageCache::readWholeFile("/nonexistent/x.png", b), "readWholeFile: missing file -> false"); }
    // small images are not cached
    std::vector<unsigned char> small(64 * 64 * 4, 7);
    std::string s = dir.substr(0, dir.size() - 6) + "src1.png";
    unsigned h0, m0, st0, f0; VitaImageCache::stats(&h0, &m0, &st0, &f0);
    VitaImageCache::store(s, 64, 64, small.data());
    unsigned h1, m1, st1, f1; VitaImageCache::stats(&h1, &m1, &st1, &f1);
    CHECK(st1 == st0, "images under 1 MiB are not stored");
    std::printf("fails=%d\n", fails); return fails;
}
