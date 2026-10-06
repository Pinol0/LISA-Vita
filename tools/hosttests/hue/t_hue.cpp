// Host test for MKXP_VITA_HUE_FAST (src/bitmap-vita-cpu.h): the memoized hue_change must give
// byte-identical results to the plain kernel. Inputs: real animation sheets, random noise, all hues.
//   g++ -std=gnu++17 -O2 t_hue.cpp -lpng -o t && ./t explode.png Fire3.png
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <cstdio>
#include <random>
#include <png.h>
#define MKXP_VITA_RGSS_COMPAT
namespace plain {
#include "../../../src/bitmap-vita-cpu.h"
}
#undef BITMAP_VITA_CPU_H
#define MKXP_VITA_HUE_FAST
namespace fast {
#include "../../../src/bitmap-vita-cpu.h"
}
static double ms(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }
static int fails = 0;
static void check(const char *name, int w, int h, const std::vector<unsigned char> &src, int hue, bool timeIt) {
    std::vector<unsigned char> a = src, b = src;
    auto t0 = std::chrono::steady_clock::now();
    plain::VitaBitmapCpu::hueChange({ w, h, a.data() }, hue);
    double tp = ms(t0); t0 = std::chrono::steady_clock::now();
    fast::VitaBitmapCpu::hueChange({ w, h, b.data() }, hue);
    double tf = ms(t0);
    bool ok = a == b;
    if (!ok) ++fails;
    if (timeIt || !ok) std::printf("%s  %s hue=%d plain=%.1f ms fast=%.1f ms\n", ok ? "PASS" : "FAIL", name, hue, tp, tf);
}
int main(int argc, char **argv) {
    for (int k = 1; k < argc; ++k) {
        png_image im; std::memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_file(&im, argv[k])) { std::printf("FAIL  open %s\n", argv[k]); ++fails; continue; }
        im.format = PNG_FORMAT_RGBA;
        std::vector<unsigned char> px(PNG_IMAGE_SIZE(im));
        png_image_finish_read(&im, nullptr, px.data(), 0, nullptr);
        for (int hue : { 1, 30, 180, 359, -45, 720 + 90 }) check(argv[k], im.width, im.height, px, hue, hue == 30);
        png_image_free(&im);
    }
    std::mt19937 rng(1);
    std::vector<unsigned char> noise(512 * 512 * 4);
    for (auto &c : noise) c = (unsigned char)rng();
    for (int hue = 0; hue < 360; hue += 7) check("noise", 512, 512, noise, hue, hue == 28);
    // every rgb value once (memo collisions exercised everywhere)
    std::vector<unsigned char> all(4096 * 4096 * 4);
    for (uint32_t i = 0; i < 4096u * 4096u; ++i) { all[i*4] = i & 255; all[i*4+1] = (i >> 8) & 255; all[i*4+2] = i >> 16; all[i*4+3] = 255; }
    for (int hue : { 90, 200 }) check("all_rgb", 4096, 4096, all, hue, true);
    std::printf("fails=%d\n", fails);
    return fails;
}
