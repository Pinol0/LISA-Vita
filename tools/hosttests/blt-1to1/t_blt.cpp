// Host test for MKXP_VITA_BLT_1TO1 + MKXP_VITA_BLEND_MEMO: the stretchBlt kernel with and without the
// 1:1 integer path and the blend memo
// must write identical bytes. g++ -std=gnu++17 -O2 t_blt.cpp -o t && ./t
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <random>
#define MKXP_VITA_FAST_CPU_KERNELS
namespace ref {
#include "../../../src/bitmap-vita-cpu.h"
}
#undef BITMAP_VITA_CPU_H
#define MKXP_VITA_BLT_1TO1
#define MKXP_VITA_BLEND_MEMO
namespace fast {
#include "../../../src/bitmap-vita-cpu.h"
}
int main() {
    std::mt19937 rng(5); int fails = 0, oneToOne = 0, n = 200000;
    auto R = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    for (int t = 0; t < n; ++t) {
        int sw_ = R(1, 120), sh_ = R(1, 120), dw_ = R(1, 160), dh_ = R(1, 160);
        std::vector<unsigned char> src(sw_ * sh_ * 4), d1(dw_ * dh_ * 4), d2;
        // half the cases from a small palette (glyph edges: the same few pairs repeat -> memo hits)
        const bool pal = R(0, 1);
        static const unsigned char P[6] = { 0, 32, 96, 128, 200, 255 };
        for (auto &c : src) { int k = R(0, 9); c = pal ? P[R(0, 5)] : (k < 3 ? 0 : k < 6 ? 255 : R(0, 255)); }
        for (auto &c : d1) { int k = R(0, 9); c = pal ? P[R(0, 5)] : (k < 3 ? 0 : k < 5 ? 255 : R(0, 255)); }
        d2 = d1;
        int w = R(-20, 100), h = R(-20, 100);
        if (R(0, 3)) { /* mostly 1:1 (blt, text) */ }
        int rw = R(0, 3) ? w : R(-40, 120), rh = R(0, 3) ? h : R(-40, 120);
        int dx = R(-30, dw_ + 10), dy = R(-30, dh_ + 10), sx = R(-10, sw_ + 5), sy = R(-10, sh_ + 5);
        int op = R(0, 4) ? 255 : R(0, 255); bool smooth = R(0, 5) == 0;
        if (!smooth && rw == w && rh == h && w > 0 && h > 0) ++oneToOne;
        long a = ref::VitaBitmapCpu::stretchBlt({dw_, dh_, d1.data()}, dx, dy, w, h, {sw_, sh_, src.data()}, sx, sy, rw, rh, op, smooth);
        long b = fast::VitaBitmapCpu::stretchBlt({dw_, dh_, d2.data()}, dx, dy, w, h, {sw_, sh_, src.data()}, sx, sy, rw, rh, op, smooth);
        if (a != b || d1 != d2) { if (++fails < 5) std::printf("MISMATCH t=%d dst=%dx%d src=%dx%d d=%d,%d,%d,%d s=%d,%d,%d,%d op=%d smooth=%d\n", t, dw_, dh_, sw_, sh_, dx, dy, w, h, sx, sy, rw, rh, op, smooth); }
    }
    std::printf("%s  %d random blits (%d 1:1), identical output: %d mismatches\n", fails ? "FAIL" : "PASS", n, oneToOne, fails);
    return fails ? 1 : 0;
}
