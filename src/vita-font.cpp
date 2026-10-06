/*
 * Port of mkxp-z Bitmap::textSize / Bitmap::drawText (src/display/bitmap.cpp) and of its font
 * size handling (src/display/font.cpp) onto CPU RGBA8 buffers. Defaults follow mkxp-z's config:
 * fontScale 1, fontKerning on, fontHinting 3 (TTF_HINTING_NONE), fontHeightReporting 0,
 * fontOutlineCrop on, no solid fonts. OUTLINE_SIZE is 1 as upstream.
 *
 * Not implemented (documented): VDMX-table sizes (VCR OSD Mono has no VDMX), hires bitmaps.
 */
#include "vita-font.h"
#include "vita_paths.h"
#include "bitmap-vita-cpu.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#ifdef MKXP_VITA_TEXT_CACHE
#include <list>
#include <string>
#include <unordered_map>
#endif

#if defined(MKXP_VITA_TIMELINE) || defined(MKXP_VITA_PERF_BITMAP)
/* Diagnostic only: TTF init/open/render timing and font cache hits (mkxp-z/src/vita_diag.h). */
#include "vita_diag.h"
#endif

namespace VitaFont
{

static const int OUTLINE_SIZE = 1;
static const float SQUEEZE_LIMIT = 0.5f;

static std::string gFontDir = VITA_GAME_ROOT "Fonts/";

void setFontDir(const std::string &dir) { gFontDir = dir; }

/* Family name (lower case) -> file in the Fonts directory. */
#ifdef MKXP_VITA_FONT_V2
#include <dirent.h>
/*
 * As mkxp-z SharedFontState: every .ttf/.otf in the font directory is registered under the family
 * name stored in the file (TTF_FontFaceFamilyName), lower-cased. Built once, on first use.
 */
static const std::map<std::string, std::string> &scannedFamilies()
{
    static std::map<std::string, std::string> families;
    static bool scanned = false;
    if (scanned)
        return families;
    scanned = true;
    if (!TTF_WasInit() && TTF_Init() != 0)
        return families;
    DIR *d = opendir(gFontDir.c_str());
    if (!d)
        return families;
    while (struct dirent *e = readdir(d)) {
        std::string fn = e->d_name;
        std::string ext = fn.size() > 4 ? fn.substr(fn.size() - 4) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext != ".ttf" && ext != ".otf")
            continue;
        TTF_Font *f = TTF_OpenFont((gFontDir + fn).c_str(), 12);
        if (!f)
            continue;
        if (const char *fam = TTF_FontFaceFamilyName(f)) {
            std::string lower = fam;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            families.emplace(lower, fn);   /* first file wins for a family */
        }
        TTF_CloseFont(f);
    }
    closedir(d);
    return families;
}
#endif

static const char *fileForFamily(const std::string &lower)
{
#ifdef MKXP_VITA_FONT_V2
    const auto &fam = scannedFamilies();
    auto it = fam.find(lower);
    if (it != fam.end())
        return it->second.c_str();
#endif
    if (lower == "vcr osd mono") return "VCR_OSD_MONO.ttf";
    if (lower == "vl gothic") return "VL-Gothic-Regular.ttf";
    if (lower == "vl pgothic") return "VL-PGothic-Regular.ttf";
    return nullptr;
}

static bool fileExists(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

/* First listed family with a file, else VCR OSD Mono (LISA's font), else "". Cached per list. */
static std::string resolveFile(const std::vector<std::string> &names)
{
#ifdef MKXP_VITA_FONT_MEMO
    /* Perf fix (MKXP_VITA_FONT_MEMO): draw_text_ex draws one character per call with the same font:
     * the last answer of these pure lookups is reused for the same arguments (d56: draw_text
     * 0.2 ms per call, mostly cache hits). */
    static std::vector<std::string> memoNames;
    static std::string memoFile;
    static bool memoValid = false;
    if (memoValid && names == memoNames)
        return memoFile;
#endif
    static std::map<std::string, std::string> cache;
    std::string key;
    for (const std::string &n : names) { key += n; key += '\x1f'; }
    auto it = cache.find(key);
    if (it != cache.end()) {
#ifdef MKXP_VITA_FONT_MEMO
        memoNames = names; memoFile = it->second; memoValid = true;
#endif
        return it->second;
    }

    std::string found;
    for (const std::string &n : names) {
        std::string lower = n;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        const char *f = fileForFamily(lower);
        if (f && fileExists(gFontDir + f)) { found = gFontDir + f; break; }
    }
    if (found.empty() && fileExists(gFontDir + "VCR_OSD_MONO.ttf"))
        found = gFontDir + "VCR_OSD_MONO.ttf";
    cache[key] = found;
#ifdef MKXP_VITA_FONT_MEMO
    memoNames = names; memoFile = found; memoValid = true;
#endif
    return found;
}

/* --- Windows-style height -> ppem (font.cpp calc_ppem_for_height), from the sfnt tables --- */

static unsigned rd16(const unsigned char *p) { return (unsigned)(p[0] << 8 | p[1]); }
static unsigned long rd32(const unsigned char *p) { return (unsigned long)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

struct SfntMetrics { int upem = 0; int winAscent = 0, winDescent = 0; int hheaAsc = 0, hheaDesc = 0; bool ok = false; };

static SfntMetrics readMetrics(const std::string &file)
{
    static std::map<std::string, SfntMetrics> cache;
    auto it = cache.find(file);
    if (it != cache.end())
        return it->second;

    SfntMetrics m;
    std::vector<unsigned char> d;
    if (FILE *f = std::fopen(file.c_str(), "rb")) {
        unsigned char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            d.insert(d.end(), buf, buf + n);
        std::fclose(f);
    }
    if (d.size() >= 12) {
        const unsigned numTables = rd16(&d[4]);
        for (unsigned i = 0; i < numTables && 12 + 16 * (i + 1) <= d.size(); ++i) {
            const unsigned char *e = &d[12 + 16 * i];
            const unsigned long off = rd32(e + 8);
            if (!std::memcmp(e, "head", 4) && off + 20 <= d.size())
                m.upem = (int)rd16(&d[off + 18]);
            else if (!std::memcmp(e, "OS/2", 4) && off + 78 <= d.size()) {
                m.winAscent = (int)rd16(&d[off + 74]);
                m.winDescent = std::abs((int)(short)rd16(&d[off + 76]));   /* get_fixed_windescent */
            } else if (!std::memcmp(e, "hhea", 4) && off + 8 <= d.size()) {
                m.hheaAsc = (short)rd16(&d[off + 4]);
                m.hheaDesc = (short)rd16(&d[off + 6]);
            }
        }
        m.ok = m.upem > 0;
    }
    cache[file] = m;
    return m;
}

static long mulDiv(long a, long b, long c)   /* FT_MulDiv for non-negative values */
{
    return c ? (a * b + c / 2) / c : 0;
}

#ifdef MKXP_VITA_FONT_MEMO
/* Memo of the last (file, height) -> ppem (see resolveFile); the lookup below is unchanged. */
static int ppemForSizeImpl(const std::string &file, int height);
int ppemForSize(const std::string &file, int height)
{
    static std::string memoFile;
    static int memoHeight = 0, memoPpem = 0;
    static bool memoValid = false;
    if (memoValid && height == memoHeight && file == memoFile)
        return memoPpem;
    const int ppem = ppemForSizeImpl(file, height);
    memoFile = file; memoHeight = height; memoPpem = ppem; memoValid = true;
    return ppem;
}
#define ppemForSize ppemForSizeImpl
#endif
int ppemForSize(const std::string &file, int height)
{
    const SfntMetrics m = readMetrics(file);
    if (!m.ok)
        return 0;
    if (height == 0)
        height = 16;
    if (height < 0)
        return -height;

    int units = m.winAscent + m.winDescent;
    if (units == 0)
        units = m.hheaAsc - m.hheaDesc;
    if (units <= 0)
        return height;

    int ppem = (int)mulDiv(m.upem, height, units);
    /* If rounding ends up getting a font exceeding height, choose a smaller ppem */
    if (ppem > 1 && mulDiv(units, ppem, m.upem) > height)
        --ppem;
    return std::max(ppem, 1);
}
#ifdef MKXP_VITA_FONT_MEMO
#undef ppemForSize
#endif

/* --- TTF_Font cache: one per (file, ppem, outline); style is set on every use --- */

static TTF_Font *getFont(const std::string &file, int ppem, int outline, bool bold, bool italic)
{
    static bool ttfInit = false;
    if (!ttfInit) {
#ifdef MKXP_VITA_DIAG
        const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
        if (TTF_Init() != 0)
            return nullptr;
#ifdef MKXP_VITA_DIAG
        vitaDiagSpan(VD_TTF_INIT, vitaDiagT0, 0, nullptr);
#endif
        ttfInit = true;
    }

    static std::map<std::string, TTF_Font *> cache;
    TTF_Font *font;
#ifdef MKXP_VITA_FONT_MEMO
    /* Last (file, ppem, outline) -> font; the style is still set below on every use. */
    static std::string memoFile;
    static int memoPpem = -1, memoOutline = -1;
    static TTF_Font *memoFont = nullptr;
    if (memoFont && ppem == memoPpem && outline == memoOutline && file == memoFile) {
        font = memoFont;
#ifdef MKXP_VITA_DIAG
        vitaDiagCount(VD_FONT_HIT);
#endif
        int style = TTF_STYLE_NORMAL;
        if (bold) style |= TTF_STYLE_BOLD;
        if (italic) style |= TTF_STYLE_ITALIC;
        TTF_SetFontStyle(font, style);
        return font;
    }
#endif
    char key[32];
    std::snprintf(key, sizeof(key), "|%d|%d", ppem, outline);
    const std::string k = file + key;
    auto it = cache.find(k);
    if (it != cache.end()) {
        font = it->second;
#ifdef MKXP_VITA_DIAG
        vitaDiagCount(VD_FONT_HIT);
#endif
    } else {
#ifdef MKXP_VITA_DIAG
        const uint64_t vitaDiagT0 = vitaDiagNow();
#endif
        font = TTF_OpenFont(file.c_str(), ppem);
#ifdef MKXP_VITA_DIAG
        vitaDiagSpan(VD_TTF_OPEN, vitaDiagT0, 0, k.c_str());
#endif
        if (!font)
            return nullptr;
        TTF_SetFontHinting(font, TTF_HINTING_NONE);     /* RGSS doesn't use font hinting */
        if (outline)
            TTF_SetFontOutline(font, outline);
        cache[k] = font;
    }

#ifdef MKXP_VITA_FONT_MEMO
    memoFile = file; memoPpem = ppem; memoOutline = outline; memoFont = font;
#endif
    int style = TTF_STYLE_NORMAL;
    if (bold) style |= TTF_STYLE_BOLD;
    if (italic) style |= TTF_STYLE_ITALIC;
    TTF_SetFontStyle(font, style);
    return font;
}

#ifdef MKXP_VITA_FONT_V2
bool familyExists(const std::string &name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const char *f = fileForFamily(lower);
    return f && fileExists(gFontDir + f);
}
#endif

bool available(const VitaFontSpec &spec)
{
    return !resolveFile(spec.names).empty();
}

static std::string fixupString(const char *str)
{
    std::string s(str ? str : "");
    for (char &c : s)
        if (c == '\r' || c == '\n')
            c = ' ';
    return s;
}

static uint16_t utf8ToUcs2(const char *in, const char **end)
{
    const unsigned char *s = (const unsigned char *)in;
    *end = in;
    if (s[0] == 0) return 0xFFFF;
    if (s[0] < 0x80) { *end = in + 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xE0) {
        if (!s[1] || !s[2]) return 0xFFFF;
        *end = in + 3;
        return (uint16_t)((s[0] & 0x0F) << 12 | (s[1] & 0x3F) << 6 | (s[2] & 0x3F));
    }
    if ((s[0] & 0xC0) == 0xC0) {
        if (!s[1]) return 0xFFFF;
        *end = in + 2;
        return (uint16_t)((s[0] & 0x1F) << 6 | (s[1] & 0x3F));
    }
    return 0xFFFF;
}

#ifdef MKXP_VITA_TEXT_CACHE
/* text_size results per (TTF_Font, style, string): the metrics never change for a font. */
static std::unordered_map<std::string, std::pair<int, int>> gTextSizeCache;
#endif

static bool textSizeFont(TTF_Font *font, const VitaFontSpec &spec, const char *str, int &w, int &h)
{
#ifdef MKXP_VITA_TEXT_CACHE
    char vitaHead[40];
    std::snprintf(vitaHead, sizeof(vitaHead), "%p|%d%d|", (void *)font, spec.bold, spec.italic);
    const std::string vitaKey = std::string(vitaHead) + str;
    auto vitaHit = gTextSizeCache.find(vitaKey);
    if (vitaHit != gTextSizeCache.end()) {
        w = vitaHit->second.first;
        h = vitaHit->second.second;
        return true;
    }
    if (gTextSizeCache.size() > 8192)
        gTextSizeCache.clear();
#endif
    /* freetype sometimes treats the last character as a pixel wider: add a space, remove its width */
    const std::string fixed = fixupString(str) + " ";
    int ws = 0;
    w = h = 0;
    TTF_SizeUTF8(font, fixed.c_str(), &w, &h);
    TTF_SizeUTF8(font, " ", &ws, nullptr);
    w -= ws;

    const char *endPtr;
    const uint16_t ucs2 = utf8ToUcs2(str, &endPtr);
    if (spec.italic && *endPtr == '\0' && ucs2 != 0xFFFF)
        TTF_GlyphMetrics(font, ucs2, nullptr, nullptr, nullptr, nullptr, &w);

    /* fontHeightReporting 0: RGSS normalizes the reported heights */
    h = w ? TTF_FontHeight(font) : 0;
#ifdef MKXP_VITA_TEXT_CACHE
    gTextSizeCache[vitaKey] = std::make_pair(w, h);
#endif
    return true;
}

bool textSize(const VitaFontSpec &spec, const char *str, int &w, int &h, VitaTextInfo *info)
{
    const std::string file = resolveFile(spec.names);
    if (file.empty())
        return false;
    const int ppem = ppemForSize(file, spec.size);
    TTF_Font *font = ppem ? getFont(file, ppem, 0, spec.bold, spec.italic) : nullptr;
    if (!font)
        return false;
    textSizeFont(font, spec, str, w, h);
    if (info) {
        info->fontFile = file;
        info->ppem = ppem;
        info->fontHeight = TTF_FontHeight(font);
    }
    return true;
}

/* SDL_ttf blended surfaces are ARGB8888; convert to RGBA8 bytes. */
struct Rgba { int w = 0, h = 0; std::vector<unsigned char> px; };

static bool fromSurface(SDL_Surface *s, Rgba &out)
{
    if (!s)
        return false;
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);  /* bytes R,G,B,A */
    SDL_FreeSurface(s);
    if (!conv)
        return false;
    out.w = conv->w;
    out.h = conv->h;
    out.px.resize((size_t)out.w * out.h * 4);
    for (int y = 0; y < out.h; ++y)
        std::memcpy(&out.px[(size_t)y * out.w * 4], (const unsigned char *)conv->pixels + y * conv->pitch, (size_t)out.w * 4);
    SDL_FreeSurface(conv);
    return true;
}

static inline unsigned char *at(Rgba &i, int x, int y) { return &i.px[((size_t)y * i.w + x) * 4]; }

/* bitmap.cpp applyShadow: one pixel wider/higher, black copy offset by `offset`, text blended over it. */
static void applyShadow(Rgba &in, const unsigned char c[4], int offset)
{
    Rgba out;
    out.w = in.w + offset;
    out.h = in.h + offset;
    out.px.assign((size_t)out.w * out.h * 4, 0);
    const float fr = c[0] / 255.0f, fg = c[1] / 255.0f, fb = c[2] / 255.0f;
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            unsigned char src[4] = { 0, 0, 0, 0 }, shd[4] = { 0, 0, 0, 0 };
            unsigned char *o = at(out, x, y);
            if (y < in.h && x < in.w) std::memcpy(src, at(in, x, y), 4);
            if (y >= offset && x >= offset) { shd[3] = at(in, x - offset, y - offset)[3]; }
            if (x < offset || y < offset) { std::memcpy(o, src, 4); continue; }
            if (x >= in.w || y >= in.h) { std::memcpy(o, shd, 4); continue; }
            if (src[3] == 255 || shd[3] == 0) { std::memcpy(o, src, 4); continue; }
            if (src[3] == 0 && shd[3] == 0) { std::memset(o, 0, 4); continue; }
            const float fSrcA = src[3] / 255.0f, fShdA = shd[3] / 255.0f;
            const float co2 = fShdA * (1.0f - fSrcA);
            const float fa = fSrcA + co2;
            const float co3 = fSrcA / fa;
            o[0] = (unsigned char)(std::min(std::max(fr * co3, 0.0f), 1.0f) * 255.0f);
            o[1] = (unsigned char)(std::min(std::max(fg * co3, 0.0f), 1.0f) * 255.0f);
            o[2] = (unsigned char)(std::min(std::max(fb * co3, 0.0f), 1.0f) * 255.0f);
            o[3] = (unsigned char)(std::min(std::max(fa, 0.0f), 1.0f) * 255.0f);
        }
    in = out;
}

/* bitmap.cpp blendText: text surface region inRect over the outline surface at outRect. */
static void blendText(Rgba &txt, int ix, int iy, int iw, int ih, const unsigned char in[4],
                      Rgba &out, int ox, int oy, const unsigned char oc[4], bool hasShadow)
{
    for (int i = 0; i < ih; ++i) {
        if (iy + i < 0 || iy + i >= txt.h || oy + i < 0 || oy + i >= out.h) continue;
        for (int j = 0; j < iw; ++j) {
            if (ix + j < 0 || ix + j >= txt.w || ox + j < 0 || ox + j >= out.w) continue;
            unsigned char *t = at(txt, ix + j, iy + i);
            unsigned char *o = at(out, ox + j, oy + i);
            const unsigned txtA = t[3], outA = o[3];
            if (txtA >= in[3]) {
                if (hasShadow) std::memcpy(o, t, 4);
                else { o[0] = in[0]; o[1] = in[1]; o[2] = in[2]; o[3] = in[3]; }
            } else if (outA == 0) {
                std::memcpy(o, t, 4);
            } else if (txtA != 0) {
                const int co1 = (int)txtA * in[3];
                const int co2 = (int)std::min<unsigned>(outA, oc[3]) * (in[3] - txtA);
                const int fa = co1 + co2;
                const float faInv = 1.0f / fa, co3 = co1 * faInv, co4 = co2 * faInv;
                float tr = in[0], tg = in[1], tb = in[2];
                if (hasShadow) { tr = t[0]; tg = t[1]; tb = t[2]; }
                o[0] = (unsigned char)std::min<int>((int)(tr * co3 + oc[0] * co4 + 0.001f), 255);
                o[1] = (unsigned char)std::min<int>((int)(tg * co3 + oc[1] * co4 + 0.001f), 255);
                o[2] = (unsigned char)std::min<int>((int)(tb * co3 + oc[2] * co4 + 0.001f), 255);
                o[3] = (unsigned char)(fa / in[3]);
            } else if (outA > oc[3]) {
                o[0] = oc[0]; o[1] = oc[1]; o[2] = oc[2]; o[3] = oc[3];
            }
        }
    }
}

#ifdef MKXP_VITA_TEXT_CACHE
/*
 * Perf fix (MKXP_VITA_TEXT_CACHE): the composed text surface (TTF text + outline + shadow) and its
 * text_size depend only on font file, ppem, style, colours, string and the rect size, so equal
 * draw_text calls (draw_text_ex draws one character at a time) reuse it. LRU, 4 MiB budget.
 */
namespace {
struct TextCacheEntry { int aw, ah; Rgba txt; std::list<std::string>::iterator lru; };
std::unordered_map<std::string, TextCacheEntry> gTextCache;
std::list<std::string> gTextLru;
size_t gTextCacheBytes = 0;
#ifdef MKXP_VITA_TEXT_CACHE_KB
const size_t kTextCacheMax = (size_t)MKXP_VITA_TEXT_CACHE_KB * 1024;   /* Vita memory budget */
#else
const size_t kTextCacheMax = 4u << 20;
#endif
size_t textCacheBytesForDiag() { return gTextCacheBytes; }

TextCacheEntry *textCacheFind(const std::string &key)
{
    auto it = gTextCache.find(key);
    if (it == gTextCache.end())
        return nullptr;
    gTextLru.splice(gTextLru.begin(), gTextLru, it->second.lru);
    return &it->second;
}

void textCacheStore(const std::string &key, int aw, int ah, const Rgba &txt)
{
    const size_t bytes = txt.px.size() + key.size() + 64;
    if (bytes > kTextCacheMax / 4)
        return;
    while (gTextCacheBytes + bytes > kTextCacheMax && !gTextLru.empty()) {
        auto old = gTextCache.find(gTextLru.back());
        gTextCacheBytes -= old->second.txt.px.size() + old->first.size() + 64;
        gTextCache.erase(old);
        gTextLru.pop_back();
    }
    gTextLru.push_front(key);
    TextCacheEntry &e = gTextCache[key];
    e.aw = aw; e.ah = ah; e.txt = txt; e.lru = gTextLru.begin();
    gTextCacheBytes += bytes;
}
} // namespace
#endif

bool drawText(unsigned char *px, int bmpW, int bmpH,
              int rx, int ry, int rw, int rh, const char *cstr, int align,
              const VitaFontSpec &spec, VitaTextInfo *info)
{
    const std::string file = resolveFile(spec.names);
    if (file.empty())
        return false;
    const int ppem = ppemForSize(file, spec.size);
    TTF_Font *font = ppem ? getFont(file, ppem, 0, spec.bold, spec.italic) : nullptr;
    if (!font)
        return false;
    if (info) { info->fontFile = file; info->ppem = ppem; info->fontHeight = TTF_FontHeight(font); }

    /* RGSS doesn't let you draw text backwards */
    if (rw <= 0 || rh <= 0 || rx >= bmpW || ry >= bmpH || rw < -rx || rh < -ry)
        return true;

    std::string fixed = fixupString(cstr);
    if (fixed.empty() || fixed == " ")
        return true;

    unsigned char c[4] = { spec.color[0], spec.color[1], spec.color[2], spec.color[3] };
    if (c[3] == 0)
        return true;

#ifdef MKXP_VITA_TEXT_CACHE
    char vitaKeyHead[96];
    std::snprintf(vitaKeyHead, sizeof(vitaKeyHead), "%d|%d%d%d%d|%02x%02x%02x%02x|%02x%02x%02x%02x|%d|%d|%d|",
                  ppem, spec.bold, spec.italic, spec.shadow, spec.outline,
                  spec.color[0], spec.color[1], spec.color[2], spec.color[3],
                  spec.outColor[0], spec.outColor[1], spec.outColor[2], spec.outColor[3],
                  rw, rh, std::min(bmpW - rx, rw));
    const std::string vitaKey = std::string(vitaKeyHead) + file + '\x1f' + fixed;
    if (const TextCacheEntry *hit = textCacheFind(vitaKey)) {
        /* Same placement as the end of this function, from the cached surface. */
        const int outlineSize = spec.outline ? OUTLINE_SIZE : 0;
        const int doubleOutlineSize = outlineSize * 2;
        const int alignmentWidth = hit->aw, alignmentHeight = hit->ah;
        const Rgba &txt = hit->txt;
        if (info) { info->measuredW = alignmentWidth; info->measuredH = alignmentHeight; }
        int alignX = rx;
        if (align == 1)
            alignX += (rw - (alignmentWidth + outlineSize)) / 2;
        else if (align == 2)
            alignX += rw - alignmentWidth - doubleOutlineSize;
        if (alignX < rx)
            alignX = rx;
        int alignY = ry + ((rh - alignmentHeight) / 2) - outlineSize;
        alignY = std::max(alignY, ry);
        float squeeze = (float)rw / alignmentWidth;
        squeeze = std::min(std::max(squeeze, SQUEEZE_LIMIT), 1.0f);
        int dW = std::min(rw, (int)(txt.w * squeeze));
        int dH = std::min(rh, txt.h);
        dW = std::min(dW, bmpW - alignX);
        dH = std::min(dH, bmpH - alignY);
        const int sW = (int)(dW / squeeze), sH = dH;
        if (info) {
            info->surfW = txt.w; info->surfH = txt.h; info->squeeze = squeeze;
            info->destX = alignX; info->destY = alignY; info->destW = dW; info->destH = dH;
        }
        if (dW <= 0 || dH <= 0)
            return true;
        VitaBitmapCpu::stretchBlt(VitaBitmapCpu::Image{ bmpW, bmpH, px }, alignX, alignY, dW, dH,
                                  VitaBitmapCpu::ConstImage{ txt.w, txt.h, txt.px.data() },
                                  outlineSize, outlineSize, sW, sH, 255, squeeze != 1.0f);
        return true;
    }
#endif

    int outlineSize = 0;
    unsigned char co[4] = { spec.outColor[0], spec.outColor[1], spec.outColor[2], spec.outColor[3] };
    if (spec.outline) {
        outlineSize = OUTLINE_SIZE;
        if (c[3] != 255 || co[3] != 255) {
            /* Step 1: outline alpha layered onto itself */
            const uint8_t out_alpha = ((int)co[3] * (int)c[3]) / 255;
            int co1 = out_alpha * 255, co2 = out_alpha * (255 - out_alpha), fa = co1 + co2;
            co[3] = (unsigned char)((fa + 1 + (fa >> 8)) >> 8);
            if (c[3] != 255) {
                /* Steps 2-3: text colour over 4 layers of outline */
                uint8_t out_alpha_full = co[3];
                for (int i = 0; i < 2; ++i) {
                    int a1 = out_alpha * 255, a2 = out_alpha_full * (255 - out_alpha), f = a1 + a2;
                    out_alpha_full = (uint8_t)((f + 1 + (f >> 8)) >> 8);
                }
                int c1 = c[3] * 255, c2 = out_alpha_full * (255 - c[3]), f = c1 + c2;
                const float fInv = 1.0f / f, c3 = c1 * fInv, c4 = c2 * fInv;
                c[0] = (unsigned char)std::min<int>((int)(c[0] * c3 + co[0] * c4 + 0.001f), 255);
                c[1] = (unsigned char)std::min<int>((int)(c[1] * c3 + co[1] * c4 + 0.001f), 255);
                c[2] = (unsigned char)std::min<int>((int)(c[2] * c3 + co[1] * c4 + 0.001f), 255);  /* upstream uses co.g */
                c[3] = (unsigned char)((f + 1 + (f >> 8)) >> 8);
            }
        }
    }
    const int doubleOutlineSize = outlineSize * 2;

    int alignmentWidth, alignmentHeight;
    textSizeFont(font, spec, fixed.c_str(), alignmentWidth, alignmentHeight);
    if (info) { info->measuredW = alignmentWidth; info->measuredH = alignmentHeight; }
    if (!alignmentWidth)
        return true;

    /* Trim the text to only fill double the rect width */
    int extent = 0, charLimit = 0;
    if (TTF_MeasureUTF8(font, fixed.c_str(), (int)(std::min(bmpW - rx, rw) / SQUEEZE_LIMIT), &extent, &charLimit) == 0) {
        size_t cps = 0;
        for (unsigned char ch : fixed) if ((ch & 0xC0) != 0x80) ++cps;
        if ((size_t)charLimit != cps) {
            charLimit += 4;
            for (size_t i = 0; i < fixed.size(); ++i)
                if ((fixed[i] & 0xC0) != 0x80 && charLimit-- == 0) { fixed.resize(i); break; }
        }
    }

    const SDL_Color sc = { c[0], c[1], c[2], c[3] };
    Rgba txt;
#ifdef MKXP_VITA_DIAG
    uint64_t vitaDiagT0 = vitaDiagNow();
    SDL_Surface *vitaDiagSurf = TTF_RenderUTF8_Blended(font, fixed.c_str(), sc);
    vitaDiagSpan(VD_TTF_RENDER, vitaDiagT0, 0, nullptr);
    if (!fromSurface(vitaDiagSurf, txt))
        return true;
#else
    if (!fromSurface(TTF_RenderUTF8_Blended(font, fixed.c_str(), sc), txt))
        return true;
#endif

    if (spec.shadow)
        applyShadow(txt, c, 1);

    int alignX = rx;
    if (align == 1)        /* Center: yes, half of the outline size */
        alignX += (rw - (alignmentWidth + outlineSize)) / 2;
    else if (align == 2)   /* Right: double the outline size */
        alignX += rw - alignmentWidth - doubleOutlineSize;
    if (alignX < rx)
        alignX = rx;
    int alignY = ry + ((rh - alignmentHeight) / 2) - outlineSize;
    alignY = std::max(alignY, ry);

    float squeeze = (float)rw / alignmentWidth;
    squeeze = std::min(std::max(squeeze, SQUEEZE_LIMIT), 1.0f);

    if (outlineSize) {
        TTF_Font *ofont = getFont(file, ppem, outlineSize, spec.bold, spec.italic);
        const SDL_Color soc = { co[0], co[1], co[2], co[3] };
        Rgba outl;
#ifdef MKXP_VITA_DIAG
        if (!ofont)
            return true;
        vitaDiagT0 = vitaDiagNow();
        SDL_Surface *vitaDiagOut = TTF_RenderUTF8_Blended(ofont, fixed.c_str(), soc);
        vitaDiagSpan(VD_TTF_RENDER, vitaDiagT0, 0, "outline");
        if (!fromSurface(vitaDiagOut, outl))
            return true;
#else
        if (!ofont || !fromSurface(TTF_RenderUTF8_Blended(ofont, fixed.c_str(), soc), outl))
            return true;
#endif
        /* fontOutlineCrop: Enterbrain's runtime crops the top row and left column of the text */
        const int undo = 0;
        const int iw = std::min({ (int)(rw / squeeze) - doubleOutlineSize, txt.w - outlineSize, outl.w - doubleOutlineSize }) + undo;
        const int ih = std::min({ rh - doubleOutlineSize, txt.h - outlineSize, outl.h - doubleOutlineSize }) + undo;
        blendText(txt, outlineSize - undo, outlineSize - undo, iw, ih, c,
                  outl, doubleOutlineSize - undo, doubleOutlineSize - undo, co, spec.shadow);
        txt = outl;
    }
#ifdef MKXP_VITA_TEXT_CACHE
    textCacheStore(vitaKey, alignmentWidth, alignmentHeight, txt);
#endif

    int dW = std::min(rw, (int)(txt.w * squeeze));
    int dH = std::min(rh, txt.h);
    dW = std::min(dW, bmpW - alignX);
    dH = std::min(dH, bmpH - alignY);
    const int sW = (int)(dW / squeeze), sH = dH;

    if (info) {
        info->surfW = txt.w; info->surfH = txt.h; info->squeeze = squeeze;
        info->destX = alignX; info->destY = alignY; info->destW = dW; info->destH = dH;
    }
    if (dW <= 0 || dH <= 0)
        return true;

    VitaBitmapCpu::stretchBlt(VitaBitmapCpu::Image{ bmpW, bmpH, px }, alignX, alignY, dW, dH,
                              VitaBitmapCpu::ConstImage{ txt.w, txt.h, txt.px.data() },
                              outlineSize, outlineSize, sW, sH, 255, squeeze != 1.0f);
    return true;
}

} // namespace VitaFont

#if defined(MKXP_VITA_TEXT_CACHE) && defined(MKXP_VITA_PERF_BITMAP)
/* PERF memory accounting (vita_diag.cpp): bytes held by the text cache. */
extern "C" size_t vitaDiagTextCacheBytes() { return VitaFont::textCacheBytesForDiag(); }
#endif
