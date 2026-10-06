/*
 * Text rendering for the Vita minimal Bitmap: mkxp-z's Bitmap::textSize / Bitmap::drawText
 * (SDL2_ttf over FreeType, Windows-style size -> ppem) ported to CPU RGBA8 buffers.
 * No GL and no Ruby here, so the same code runs in the host tests.
 */
#ifndef VITA_FONT_H
#define VITA_FONT_H

#include <string>
#include <vector>

struct VitaFontSpec
{
    std::vector<std::string> names;   /* Font#name: String or Array, in order */
    int size = 24;
    bool bold = false;
    bool italic = false;
    bool outline = true;
    bool shadow = false;
    unsigned char color[4] = { 255, 255, 255, 255 };
    unsigned char outColor[4] = { 0, 0, 0, 128 };
};

/* What the renderer actually used, for the diagnostic log. */
struct VitaTextInfo
{
    std::string fontFile;             /* empty: no TTF found (caller falls back) */
    int ppem = 0;
    int fontHeight = 0;
    int surfW = 0, surfH = 0;         /* rendered text (with outline) surface */
    float squeeze = 1.0f;
    int destX = 0, destY = 0, destW = 0, destH = 0;
    int measuredW = 0, measuredH = 0; /* textSize() of the drawn string */
};

namespace VitaFont
{

/* Directory holding the TTF files, e.g. "ux0:data/ruby_vita_test/Fonts/". */
void setFontDir(const std::string &dir);

#ifdef MKXP_VITA_FONT_V2
/* Font.exist?: a TTF exists for this family name (no fallback font). */
bool familyExists(const std::string &name);
#endif

/* True if a TTF could be resolved for these names (else callers keep the legacy atlas). */
bool available(const VitaFontSpec &spec);

/* Bitmap#text_size: (w, h). Returns false if no TTF is available. */
bool textSize(const VitaFontSpec &spec, const char *str, int &w, int &h, VitaTextInfo *info = nullptr);

/* Bitmap#draw_text into an RGBA8 buffer. Returns false if no TTF is available,
 * true otherwise (also when nothing had to be drawn). */
bool drawText(unsigned char *px, int bmpW, int bmpH,
              int rx, int ry, int rw, int rh, const char *str, int align,
              const VitaFontSpec &spec, VitaTextInfo *info = nullptr);

/* Windows-style ppem for a TTF file and RGSS size (exposed for tests). 0 on error. */
int ppemForSize(const std::string &file, int size);

} // namespace VitaFont

#endif /* VITA_FONT_H */
