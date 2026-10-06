/*
 * CPU pixel kernels for the Vita minimal Bitmap (RGBA8, row-major, no padding).
 *
 * The Vita backend keeps a CPU copy of a Bitmap's pixels as the source of truth and uploads it
 * to the texture after each change (like vitaDrawText). These kernels reproduce mkxp-z's GPU
 * results for Bitmap#stretch_blt and Bitmap#radial_blur. No GL here, so they build on the host too.
 */
#ifndef BITMAP_VITA_CPU_H
#define BITMAP_VITA_CPU_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace VitaBitmapCpu
{

struct Image
{
    int w, h;
    unsigned char *px;        /* w * h * 4 bytes */
};

struct ConstImage
{
    int w, h;
    const unsigned char *px;
};

static inline unsigned char toByte(float v)
{
    /* GL's float -> UNORM8 conversion: clamp, then round to nearest. */
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (unsigned char)(v * 255.0f + 0.5f);
}

/* Bilinear texel fetch with CLAMP_TO_EDGE at texture coordinate (u, v) in pixels. */
static inline void sampleBilinear(ConstImage img, float u, float v, float out[4])
{
    const float fx = u - 0.5f, fy = v - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float ax = fx - x0, ay = fy - y0;
    const int xa = std::max(0, std::min(img.w - 1, x0)), xb = std::max(0, std::min(img.w - 1, x0 + 1));
    const int ya = std::max(0, std::min(img.h - 1, y0)), yb = std::max(0, std::min(img.h - 1, y0 + 1));
    const unsigned char *p00 = img.px + ((size_t)ya * img.w + xa) * 4;
    const unsigned char *p10 = img.px + ((size_t)ya * img.w + xb) * 4;
    const unsigned char *p01 = img.px + ((size_t)yb * img.w + xa) * 4;
    const unsigned char *p11 = img.px + ((size_t)yb * img.w + xb) * 4;
    for (int c = 0; c < 4; ++c) {
        const float top = p00[c] + (p10[c] - p00[c]) * ax;
        const float bot = p01[c] + (p11[c] - p01[c]) * ax;
        out[c] = (top + (bot - top) * ay) / 255.0f;
    }
}

#ifdef MKXP_VITA_BLEND_MEMO
struct BlendMemo { uint32_t s, d, out; int op; bool valid; };
/* 4096 entries, main thread only (all CPU bitmap kernels run on the Ruby thread). */
static inline BlendMemo *blendMemo()
{
    static BlendMemo table[4096];
    return table;
}
#endif

/*
 * stretch_blt(dest_rect, src, src_rect, opacity), smooth = false, NORMAL mode: nearest sampling
 * plus the bitmapBlit.frag blend:
 *   co1 = src.a * opacity; co2 = dst.a * (1 - co1); a = co1 + co2
 *   rgb = a == 0 ? src.rgb : (co1 * src.rgb + co2 * dst.rgb) / a
 * Destination pixels whose sample falls outside the source bitmap are left unchanged (mkxp-z
 * shrinks both rects to the source bounds first). Negative rect sizes flip the mapping.
 * smooth = true samples with GL_LINEAR (clamp to edge) instead of GL_NEAREST: mkxp-z uses it
 * when draw_text squeezes a string into a narrower rect.
 * Returns the number of destination pixels written.
 */
static long stretchBlt(Image dst, int dx, int dy, int dw, int dh,
                       ConstImage src, int sx, int sy, int sw, int sh, int opacity,
                       bool smooth = false)
{
    opacity = std::max(0, std::min(255, opacity));
    if (opacity == 0 || dw == 0 || dh == 0 || sw == 0 || sh == 0)
        return 0;

    /* A negative destination size mirrors the image: normalize it onto the source side. */
    if (dw < 0) { dx += dw; dw = -dw; sx += sw; sw = -sw; }
    if (dh < 0) { dy += dh; dh = -dh; sy += sh; sh = -sh; }

    const float op = (float)opacity / 255.0f;
    const int x0 = std::max(dx, 0), x1 = std::min(dx + dw, dst.w);
    const int y0 = std::max(dy, 0), y1 = std::min(dy + dh, dst.h);
    long written = 0;

#ifdef MKXP_VITA_BLT_1TO1
    /* Perf fix (MKXP_VITA_BLT_1TO1): a 1:1 nearest copy (every blt, unsqueezed text) maps pixel
     * dx + k to texel sx + k. The float form below gives exactly that: ((k + 0.5) * w) / w is exact
     * in single precision (all terms far below 2^24), so floor() returns sx + k. Same texel, same
     * blend; only the per-pixel float divisions go (d56: blt 0.55 ms per call). Host test:
     * tools/hosttests/blt-1to1/. */
    const bool oneToOne = !smooth && sw == dw && sh == dh;
#endif
    for (int y = y0; y < y1; ++y) {
        /* Nearest texel for the destination pixel centre (GL_NEAREST). */
#ifdef MKXP_VITA_BLT_1TO1
        const float fy = oneToOne ? 0.0f : (float)sy + ((float)(y - dy) + 0.5f) * (float)sh / (float)dh;
        const int ty = oneToOne ? sy + (y - dy) : (int)std::floor(fy);
#else
        const float fy = (float)sy + ((float)(y - dy) + 0.5f) * (float)sh / (float)dh;
        const int ty = (int)std::floor(fy);
#endif
        if (ty < 0 || ty >= src.h)
            continue;
        for (int x = x0; x < x1; ++x) {
#ifdef MKXP_VITA_BLT_1TO1
            const float fx = oneToOne ? 0.0f : (float)sx + ((float)(x - dx) + 0.5f) * (float)sw / (float)dw;
            const int tx = oneToOne ? sx + (x - dx) : (int)std::floor(fx);
#else
            const float fx = (float)sx + ((float)(x - dx) + 0.5f) * (float)sw / (float)dw;
            const int tx = (int)std::floor(fx);
#endif
            if (tx < 0 || tx >= src.w)
                continue;

#ifdef MKXP_VITA_FAST_CPU_KERNELS
            /* Exact shortcuts of the blend below for opacity 255 and nearest sampling (host-verified
             * over all byte values): an opaque texel replaces the pixel, a transparent one leaves it
             * unchanged except that a transparent destination takes the texel's rgb. */
            if (!smooth && opacity == 255) {
                const unsigned char *s8 = src.px + ((size_t)ty * src.w + tx) * 4;
                unsigned char *d8 = dst.px + ((size_t)y * dst.w + x) * 4;
                if (s8[3] == 255) {
                    std::memcpy(d8, s8, 4);
                    ++written;
                    continue;
                }
                if (s8[3] == 0) {
                    if (d8[3] == 0) {
                        d8[0] = s8[0]; d8[1] = s8[1]; d8[2] = s8[2];
                    }
                    ++written;
                    continue;
                }
            }
#endif
#ifdef MKXP_VITA_BLEND_MEMO
            /* Perf fix (MKXP_VITA_BLEND_MEMO): with nearest sampling the blended pixel depends only
             * on (source texel, destination pixel, opacity); antialiased glyph edges repeat the same
             * few pairs. The result of the float blend below is remembered in a direct-mapped table
             * and reused for the same inputs: identical bytes (host test tools/hosttests/blt-1to1/). */
            uint32_t memoS = 0, memoD = 0;
            unsigned memoSlot = 0;
            if (!smooth) {
                const unsigned char *s8 = src.px + ((size_t)ty * src.w + tx) * 4;
                unsigned char *d8 = dst.px + ((size_t)y * dst.w + x) * 4;
                std::memcpy(&memoS, s8, 4);
                std::memcpy(&memoD, d8, 4);
                memoSlot = ((memoS * 2654435761u) ^ (memoD * 2246822519u) ^ ((uint32_t)opacity * 3266489917u)) >> 20;
                const BlendMemo &m = blendMemo()[memoSlot];
                if (m.valid && m.s == memoS && m.d == memoD && m.op == opacity) {
                    std::memcpy(d8, &m.out, 4);
                    ++written;
                    continue;
                }
            }
#endif
            float sv[4];
            if (smooth) {
                sampleBilinear(src, fx, fy, sv);
            } else {
                const unsigned char *s = src.px + ((size_t)ty * src.w + tx) * 4;
                for (int c = 0; c < 4; ++c)
                    sv[c] = s[c] / 255.0f;
            }
            unsigned char *d = dst.px + ((size_t)y * dst.w + x) * 4;

            const float sa = sv[3], da = d[3] / 255.0f;
            const float co1 = sa * op;
            const float co2 = da * (1.0f - co1);
            const float a = co1 + co2;
            for (int c = 0; c < 3; ++c) {
                const float sc = sv[c], dc = d[c] / 255.0f;
                d[c] = toByte(a == 0.0f ? sc : (co1 * sc + co2 * dc) / a);
            }
            d[3] = toByte(a);
#ifdef MKXP_VITA_BLEND_MEMO
            if (!smooth) {
                BlendMemo &m = blendMemo()[memoSlot];
                m.s = memoS; m.d = memoD; m.op = opacity; m.valid = true;
                std::memcpy(&m.out, d, 4);
            }
#endif
            ++written;
        }
    }
    return written;
}

static inline bool allTransparent(ConstImage img)
{
    const size_t n = (size_t)img.w * img.h;
    for (size_t i = 0; i < n; ++i)
        if (img.px[i * 4 + 3])
            return false;
    return true;
}

/*
 * radial_blur(angle, divisions), as mkxp-z Bitmap::radialBlur: into a cleared target, draw
 * `divisions` copies rotated about the centre from -angle/2 to +angle/2 with vertex alpha
 * 1/divisions, BlendAddition (rgb: SRC_ALPHA, ONE; alpha: ONE, ONE), bilinear sampling. Each copy
 * is the image plus its four edge mirrors (corners uncovered). The target is quantized to 8 bit
 * after every copy, like the RGBA8 framebuffer. Result replaces the image.
 */
static void radialBlur(Image img, int angle, int divisions)
{
    angle = std::max(0, std::min(359, angle));
    divisions = std::max(2, std::min(100, divisions));

    const int W = img.w, H = img.h;
    const size_t n = (size_t)W * H;
    std::vector<unsigned char> source(img.px, img.px + n * 4);
    const ConstImage src = { W, H, source.data() };

    std::memset(img.px, 0, n * 4);
    /* Exact shortcut: every contribution of a fully transparent image is zero. */
    if (allTransparent(src))
        return;

    const float angleStep = (float)angle / (float)(divisions - 1);
    const float alpha = 1.0f / (float)divisions;
    const float baseAngle = -((float)angle / 2.0f);
    const float cx = W / 2.0f, cy = H / 2.0f;

    for (int k = 0; k < divisions; ++k) {
        const float rad = (baseAngle + k * angleStep) * 3.141592654f / 180.0f;
        const float c = std::cos(rad), s = std::sin(rad);
        for (int y = 0; y < H; ++y) {
            const float ry = (y + 0.5f) - cy;
            for (int x = 0; x < W; ++x) {
                const float rx = (x + 0.5f) - cx;
                /* Inverse of x' = c x + s y, y' = -s x + c y (Transform, about the centre). */
                float px = c * rx - s * ry + cx;
                float py = s * rx + c * ry + cy;
                const bool inX = px >= 0.0f && px < W, inY = py >= 0.0f && py < H;
                if (inX && inY) {
                } else if (inX && py < 0.0f && py >= -H) {
                    py = -py;
                } else if (inX && py >= H && py < 2.0f * H) {
                    py = 2.0f * H - py;
                } else if (inY && px < 0.0f && px >= -W) {
                    px = -px;
                } else if (inY && px >= W && px < 2.0f * W) {
                    px = 2.0f * W - px;
                } else {
                    continue;
                }
                float t[4];
                sampleBilinear(src, px, py, t);
                unsigned char *d = img.px + ((size_t)y * W + x) * 4;
                const float sa = t[3] * alpha;
                for (int ch = 0; ch < 3; ++ch)
                    d[ch] = toByte(t[ch] * sa + d[ch] / 255.0f);
                d[3] = toByte(sa + d[3] / 255.0f);
            }
        }
    }
}

#if defined(MKXP_VITA_BITMAP_WINDOW_OPS) || defined(VITA_BITMAP_CPU_ALL)
/*
 * fill_rect / clear_rect / clear (mkxp-z BitmapPrivate::fillRect: glClear with a scissor box):
 * the pixels are REPLACED by the colour (no blending); negative sizes are normalized; the rect is
 * clipped to the bitmap. `c` is RGBA8. Returns the number of pixels written.
 */
static long fillRect(Image dst, int x, int y, int w, int h, const unsigned char c[4])
{
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    const int x0 = std::max(x, 0), x1 = std::min(x + w, dst.w);
    const int y0 = std::max(y, 0), y1 = std::min(y + h, dst.h);
    long written = 0;
#ifdef MKXP_VITA_FAST_CPU_KERNELS
    /* Same result: build one row of the colour, then copy it to every row. */
    if (x1 <= x0 || y1 <= y0)
        return 0;
    const size_t rowBytes = (size_t)(x1 - x0) * 4;
    unsigned char *first = dst.px + ((size_t)y0 * dst.w + x0) * 4;
    for (int xx = 0; xx < x1 - x0; ++xx)
        std::memcpy(first + (size_t)xx * 4, c, 4);
    for (int yy = y0 + 1; yy < y1; ++yy)
        std::memcpy(dst.px + ((size_t)yy * dst.w + x0) * 4, first, rowBytes);
    written = (long)(x1 - x0) * (y1 - y0);
#else
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            std::memcpy(dst.px + ((size_t)yy * dst.w + xx) * 4, c, 4);
            ++written;
        }
#endif
    return written;
}

/*
 * gradient_fill_rect (mkxp-z non-mega path: a quad with per-vertex colours, blending off, i.e.
 * REPLACE): colour at the pixel centre is c1 + (c2 - c1) * t with t = (p + 0.5 - start) / length,
 * along x (horizontal) or y (vertical). c1/c2 are normalized 0..1 (Color#norm). Same early outs as
 * upstream (no normalization of negative sizes). Returns the number of pixels written.
 */
static long gradientFillRect(Image dst, int x, int y, int w, int h,
                             const float c1[4], const float c2[4], bool vertical)
{
    if (w <= 0 || h <= 0 || x >= dst.w || y >= dst.h || w < -x || h < -y)
        return 0;
    const int x0 = std::max(x, 0), x1 = std::min(x + w, dst.w);
    const int y0 = std::max(y, 0), y1 = std::min(y + h, dst.h);
    long written = 0;
#ifdef MKXP_VITA_FAST_CPU_KERNELS
    /* Same arithmetic, evaluated once per column (horizontal) or per row (vertical). */
    const int cw = x1 - x0;
    if (cw <= 0 || y1 <= y0)
        return 0;
    std::vector<unsigned char> row((size_t)cw * 4);
    for (int yy = y0; yy < y1; ++yy) {
        if (vertical) {                       /* one colour per row */
            const float t = ((yy + 0.5f) - y) / (float)h;
            unsigned char c[4];
            for (int ch = 0; ch < 4; ++ch)
                c[ch] = toByte(c1[ch] + (c2[ch] - c1[ch]) * t);
            for (int k = 0; k < cw; ++k)
                std::memcpy(row.data() + (size_t)k * 4, c, 4);
        } else if (yy == y0) {                /* one colour per column, same for every row */
            for (int xx = x0; xx < x1; ++xx) {
                const float t = ((xx + 0.5f) - x) / (float)w;
                unsigned char *d = row.data() + (size_t)(xx - x0) * 4;
                for (int ch = 0; ch < 4; ++ch)
                    d[ch] = toByte(c1[ch] + (c2[ch] - c1[ch]) * t);
            }
        }
        std::memcpy(dst.px + ((size_t)yy * dst.w + x0) * 4, row.data(), (size_t)cw * 4);
    }
    written = (long)cw * (y1 - y0);
#else
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const float t = vertical ? ((yy + 0.5f) - y) / (float)h : ((xx + 0.5f) - x) / (float)w;
            unsigned char *d = dst.px + ((size_t)yy * dst.w + xx) * 4;
            for (int ch = 0; ch < 4; ++ch)
                d[ch] = toByte(c1[ch] + (c2[ch] - c1[ch]) * t);
            ++written;
        }
#endif
    return written;
}
#endif /* MKXP_VITA_BITMAP_WINDOW_OPS || VITA_BITMAP_CPU_ALL */

#if defined(MKXP_VITA_RGSS_COMPAT) || defined(VITA_BITMAP_CPU_ALL)
/*
 * Bitmap#hue_change as mkxp-z's hue.frag (blending off): rgb -> hsv (gamedev.stackexchange 59808),
 * h += adjust, hsv -> rgb, alpha kept. adjust = wrap(hue, 0..360) / 360. GLSL mix/step/fract/clamp
 * are evaluated in single precision like the shader.
 */
static void hueChange(Image img, int hue)
{
    hue %= 360;
    if (hue < 0)
        hue += 360;
    if (hue == 0)
        return;
    const float adjust = hue / 360.0f;
    auto mixf = [](float a, float b, float t) { return a + (b - a) * t; };
    auto fractf = [](float v) { return v - std::floor(v); };
    const size_t n = (size_t)img.w * img.h;
#ifdef MKXP_VITA_HUE_FAST
    /*
     * Perf fix (MKXP_VITA_HUE_FAST): the result depends only on the pixel's rgb, so it is memoized
     * in a direct-mapped table (exact). Animation sheets are 960x1152 and mostly one colour: the
     * float kernel over 1.1 M pixels held Cache.hue_changed_bitmap for ~1 s (d34/d35 soak stalls on
     * Graphics/Animations/explode).
     */
    const unsigned kMemo = 4096;
    std::vector<uint32_t> memoKey(kMemo, 0xffffffffu), memoVal(kMemo);
    for (size_t i = 0; i < n; ++i) {
        unsigned char *px = img.px + i * 4;
        const uint32_t key = (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16);
        const unsigned slot = ((key * 2654435761u) >> 20) & (kMemo - 1);
        if (memoKey[slot] == key) {
            const uint32_t v = memoVal[slot];
            px[0] = (unsigned char)v;
            px[1] = (unsigned char)(v >> 8);
            px[2] = (unsigned char)(v >> 16);
            continue;
        }
#else
    for (size_t i = 0; i < n; ++i) {
        unsigned char *px = img.px + i * 4;
#endif
        const float r = px[0] / 255.0f, g = px[1] / 255.0f, b = px[2] / 255.0f;
        /* rgb2hsv */
        const float K0 = 0.0f, K1 = -1.0f / 3.0f, K2 = 2.0f / 3.0f, K3 = -1.0f;
        const float s1 = (g >= b) ? 1.0f : 0.0f;                       /* step(c.b, c.g) */
        const float p0 = mixf(b, g, s1), p1 = mixf(g, b, s1);
        const float p2 = mixf(K3, K0, s1), p3 = mixf(K2, K1, s1);
        const float s2 = (r >= p0) ? 1.0f : 0.0f;                      /* step(p.x, c.r) */
        const float q0 = mixf(p0, r, s2), q1 = mixf(p1, p1, s2);
        const float q2 = mixf(p3, p2, s2), q3 = mixf(r, p0, s2);
        const float d = q0 - std::min(q3, q1);
        const float eps = 1.0e-10f;
        float h = std::fabs(q2 + (q3 - q1) / (6.0f * d + eps));
        const float s = d / (q0 + eps), v = q0;
        h += adjust;
        /* hsv2rgb */
        float out[3];
        const float Kx[3] = { 1.0f, 2.0f / 3.0f, 1.0f / 3.0f };
        for (int c = 0; c < 3; ++c) {
            const float pc = std::fabs(fractf(h + Kx[c]) * 6.0f - 3.0f);
            const float cl = std::min(std::max(pc - 1.0f, 0.0f), 1.0f);
            out[c] = v * mixf(1.0f, cl, s);
        }
        px[0] = toByte(out[0]);
        px[1] = toByte(out[1]);
        px[2] = toByte(out[2]);
#ifdef MKXP_VITA_HUE_FAST
        memoKey[slot] = key;
        memoVal[slot] = (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16);
#endif
    }
}
#endif /* MKXP_VITA_RGSS_COMPAT || VITA_BITMAP_CPU_ALL */

#if defined(MKXP_VITA_SCREEN_FX) || defined(VITA_BITMAP_CPU_ALL)
/*
 * Bitmap#blur as mkxp-z (blurH.vert/blurV.vert + blur.frag, blending off): a horizontal then a
 * vertical pass, each the mean of the pixel and its two neighbours (nearest sampling at pixel
 * centres, CLAMP_TO_EDGE), quantized to 8 bit after each pass like the RGBA8 target.
 */
static void blur(Image img)
{
    const int W = img.w, H = img.h;
    if (W <= 0 || H <= 0)
        return;
    std::vector<unsigned char> tmp((size_t)W * H * 4);
    for (int pass = 0; pass < 2; ++pass) {
        const unsigned char *src = img.px;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int ax, ay, bx, by;
                if (pass == 0) { ax = std::max(x - 1, 0); bx = std::min(x + 1, W - 1); ay = by = y; }
                else { ay = std::max(y - 1, 0); by = std::min(y + 1, H - 1); ax = bx = x; }
                const unsigned char *p0 = src + ((size_t)y * W + x) * 4;
                const unsigned char *pa = src + ((size_t)ay * W + ax) * 4;
                const unsigned char *pb = src + ((size_t)by * W + bx) * 4;
                unsigned char *d = tmp.data() + ((size_t)y * W + x) * 4;
                for (int c = 0; c < 4; ++c)
                    d[c] = toByte((p0[c] + pa[c] + pb[c]) / 255.0f / 3.0f);
            }
        std::memcpy(img.px, tmp.data(), tmp.size());
    }
}
#endif /* MKXP_VITA_SCREEN_FX || VITA_BITMAP_CPU_ALL */

} // namespace VitaBitmapCpu

#endif /* BITMAP_VITA_CPU_H */
