// Fuzz test of the CPU bitmap kernels (src/bitmap-vita-cpu.h, as compiled for the Vita: FAST_CPU_KERNELS,
// BLT_1TO1, BLEND_MEMO, HUE_FAST): fill_rect, gradient_fill_rect, stretch_blt / blt, radial_blur, blur,
// hue_change with random and extreme arguments (negative, huge, INT_MIN/INT_MAX: what a script can pass
// through rb_get_args "i").
//  1. the kernels with MKXP_VITA_AUDIT_FIXES (64-bit rect arithmetic) write exactly the same bytes as
//     the kernels without it for every argument whose arithmetic fits an int;
//  2. with MKXP_VITA_AUDIT_FIXES, extreme arguments: guard zones around the image untouched, source
//     unchanged, and no undefined behaviour (the build traps on it: signed overflow, float->int).
//   g++ -std=gnu++17 -O1 -fsanitize=undefined -fsanitize-trap=all -fsanitize=float-cast-overflow \
//       -D_GLIBCXX_ASSERTIONS t_kernel_fuzz.cpp -o t && ./t
//   (OLD=1: run the extreme arguments on the kernels without the fix: they must trap.)
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#define MKXP_VITA_FAST_CPU_KERNELS
#define MKXP_VITA_BLT_1TO1
#define MKXP_VITA_BLEND_MEMO
#define MKXP_VITA_HUE_FAST
#define VITA_BITMAP_CPU_ALL
namespace old {
#include "../../../src/bitmap-vita-cpu.h"
}
#undef BITMAP_VITA_CPU_H
#undef VITA_COORD
#undef VITA_COORD_INT
#define MKXP_VITA_AUDIT_FIXES
namespace fix {
#include "../../../src/bitmap-vita-cpu.h"
}

static const size_t kGuard = 256;
struct Buf {
    std::vector<unsigned char> m;
    int w, h;
    Buf(int w_, int h_, std::mt19937 &rng) : m(kGuard * 2 + (size_t)w_ * h_ * 4), w(w_), h(h_)
    {
        for (auto &c : m) c = (unsigned char)rng();
        std::memset(m.data(), 0xA5, kGuard);
        std::memset(m.data() + m.size() - kGuard, 0x5A, kGuard);
    }
    unsigned char *px() { return m.data() + kGuard; }
    bool guardsOk() const
    {
        for (size_t i = 0; i < kGuard; ++i)
            if (m[i] != 0xA5 || m[m.size() - 1 - i] != 0x5A) return false;
        return true;
    }
};

struct Call { int kind, a[10]; bool b; unsigned char c[4]; };

template <class Img, class CImg, class F1, class F2, class F3, class F4, class F5, class F6>
static void run(const Call &k, Buf &d, Buf &s, F1 fill, F2 grad, F3 blt, F4 radial, F5 blur, F6 hue)
{
    Img di{ d.w, d.h, d.px() };
    CImg si{ s.w, s.h, s.px() };
    const float f1[4] = { k.c[0] / 255.f, k.c[1] / 255.f, k.c[2] / 255.f, k.c[3] / 255.f }, f2[4] = { 0.1f, 0.5f, 1.0f, 0.7f };
    switch (k.kind) {
    case 0: fill(di, k.a[0], k.a[1], k.a[2], k.a[3], k.c); break;
    case 1: grad(di, k.a[0], k.a[1], k.a[2], k.a[3], f1, f2, k.b); break;
    case 2: blt(di, k.a[0], k.a[1], k.a[2], k.a[3], si, k.a[4], k.a[5], k.a[6], k.a[7], k.a[8], k.b); break;
    case 3: radial(di, k.a[0], k.a[1]); break;
    case 4: blur(di); break;
    case 5: hue(di, k.a[0]); break;
    }
}

int main()
{
    const bool oldExtreme = getenv("OLD") != nullptr;
    std::mt19937 rng(12345);
    auto R = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    static const int ext[] = { INT_MIN, INT_MIN + 1, -1000000000, -65536, -1, 0, 1, 65536, 1000000000, INT_MAX - 1, INT_MAX };
    int fails = 0, same = 0, extreme = 0, wide = 0;
    /* Directed edge cases the random draw rarely builds: a source origin near the int limits
     * walked by a destination rect that starts off the bitmap (sx + (x - dx) leaves the int). */
    static const int edge[] = { INT_MIN, INT_MIN + 3, -5, 0, INT_MAX - 3, INT_MAX };
    int directed = 0;
    for (int so : edge)
        for (int d0 : { -20, 0, INT_MIN + 1 })
            for (int len : { 30, -30, INT_MAX, INT_MIN })
                for (int sc = 0; sc < 3; ++sc) {
                    std::mt19937 f(directed);
                    Buf d(20, 20, f), sbuf(20, 20, f);
                    const std::vector<unsigned char> before = sbuf.m;
                    fix::VitaBitmapCpu::Image di{ d.w, d.h, d.px() };
                    fix::VitaBitmapCpu::ConstImage si{ sbuf.w, sbuf.h, sbuf.px() };
                    const int slen = sc == 0 ? len : sc == 1 ? 7 : len / -2;
                    fix::VitaBitmapCpu::stretchBlt(di, d0, 0, len, 10, si, so, 0, slen, 10, 200, sc == 2);
                    fix::VitaBitmapCpu::stretchBlt(di, 0, d0, 10, len, si, 0, so, 10, slen, 255, false);
                    ++directed;
                    if ((!d.guardsOk() || sbuf.m != before) && ++fails <= 10) std::printf("FAIL directed edge case %d\n", directed);
                }
    std::printf("%s  %d directed edge cases (source origin near the int limits): no UB trap, guards intact\n",
                fails ? "FAIL" : "PASS", directed);
    for (int t = 0; t < 60000; ++t) {
        const bool ex = R(0, 2) == 0;
        auto arg = [&](int lo, int hi) { return ex && R(0, 2) ? ext[R(0, (int)(sizeof(ext) / sizeof(ext[0])) - 1)] : R(lo, hi); };
        Call k;
        k.kind = R(0, 5);
        k.b = R(0, k.kind == 2 ? 4 : 1) == 0;   /* stretch_blt: smooth in 1 of 5 */
        for (int i = 0; i < 4; ++i) k.c[i] = (unsigned char)R(0, 255);
        if (k.kind == 0 || k.kind == 1) { k.a[0] = arg(-50, 50); k.a[1] = arg(-50, 50); k.a[2] = arg(-60, 60); k.a[3] = arg(-60, 60); }
        if (k.kind == 2) {
            const int w = arg(-60, 60), h = arg(-60, 60);
            const bool one = R(0, 1);
            k.a[0] = arg(-50, 50); k.a[1] = arg(-50, 50); k.a[2] = w; k.a[3] = h; k.a[4] = arg(-50, 50); k.a[5] = arg(-50, 50);
            k.a[6] = one ? w : arg(-60, 60); k.a[7] = one ? h : arg(-60, 60); k.a[8] = arg(-300, 300);
        }
        if (k.kind == 3) { k.a[0] = arg(-400, 400); k.a[1] = arg(-10, 120); }
        if (k.kind == 5) k.a[0] = arg(-1000, 1000);
        const int dw = R(1, 40), dh = R(1, 40), sw = R(1, 40), sh = R(1, 40);
        std::mt19937 fill1(t), fill2(t);
        Buf d1(dw, dh, fill1), s1(sw, sh, fill1), d2(dw, dh, fill2), s2(sw, sh, fill2);
        const std::vector<unsigned char> srcBefore = s2.m;
        if (!ex || oldExtreme)
            run<old::VitaBitmapCpu::Image, old::VitaBitmapCpu::ConstImage>(k, d1, s1, old::VitaBitmapCpu::fillRect,
                old::VitaBitmapCpu::gradientFillRect, old::VitaBitmapCpu::stretchBlt, old::VitaBitmapCpu::radialBlur,
                old::VitaBitmapCpu::blur, old::VitaBitmapCpu::hueChange);
        run<fix::VitaBitmapCpu::Image, fix::VitaBitmapCpu::ConstImage>(k, d2, s2, fix::VitaBitmapCpu::fillRect,
            fix::VitaBitmapCpu::gradientFillRect, fix::VitaBitmapCpu::stretchBlt, fix::VitaBitmapCpu::radialBlur,
            fix::VitaBitmapCpu::blur, fix::VitaBitmapCpu::hueChange);
        if (ex) {
            ++extreme;
        } else {
            ++same;
            if (d1.m != d2.m && ++fails <= 10) std::printf("FAIL fixed kernel differs on normal arguments (case %d kind %d)\n", t, k.kind);
            if (k.kind == 2) {
                /* the 64-bit instantiation (extreme rects) gives the same bytes as the int one */
                std::mt19937 fill3(t);
                Buf d3(dw, dh, fill3), s3(sw, sh, fill3);
                fix::VitaBitmapCpu::Image di{ d3.w, d3.h, d3.px() };
                fix::VitaBitmapCpu::ConstImage si{ s3.w, s3.h, s3.px() };
                fix::VitaBitmapCpu::stretchBltT<long long>(di, k.a[0], k.a[1], k.a[2], k.a[3], si, k.a[4], k.a[5], k.a[6], k.a[7], k.a[8], k.b);
                ++wide;
                if (d3.m != d2.m && ++fails <= 10) std::printf("FAIL 64-bit stretch_blt differs from the int one (case %d)\n", t);
            }
        }
        if (!d2.guardsOk() && ++fails <= 10) std::printf("FAIL guard overwritten (case %d kind %d)\n", t, k.kind);
        if (s2.m != srcBefore && ++fails <= 10) std::printf("FAIL source changed (case %d kind %d)\n", t, k.kind);
    }
    std::printf("%s  %d calls with normal arguments: identical bytes with and without the fix\n", fails ? "FAIL" : "PASS", same);
    std::printf("%s  %d stretch_blt calls: 64-bit and int instantiations identical\n", fails ? "FAIL" : "PASS", wide);
    std::printf("%s  %d calls with extreme arguments: guards intact, sources unchanged, no UB trap\nfails=%d\n",
                fails ? "FAIL" : "PASS", extreme, fails);
    return fails ? 1 : 0;
}
