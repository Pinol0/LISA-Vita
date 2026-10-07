/* Host test: why does explode.png (960x1152, 20 KB, gAMA/cHRM/iCCP) decode 2x slower on the Vita than
 * Fire3.png (960x1152, 1.5 MB, no colour chunks)? Times the libpng simplified API (what
 * bitmap-vita.cpp uses) against the low-level API with no colour transforms (what SDL_image,
 * hence mkxp-z, does), and checks whether the pixels differ.
 *   gcc -O2 t_png_gamma.c -lpng -o t && ./t file.png... */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static unsigned char *simple(const char *fn, unsigned *w, unsigned *h)
{
    png_image im; memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, fn)) return NULL;
    im.format = PNG_FORMAT_RGBA;
    unsigned char *px = malloc(PNG_IMAGE_SIZE(im));
    if (!png_image_finish_read(&im, NULL, px, 0, NULL)) { free(px); return NULL; }
    *w = im.width; *h = im.height; png_image_free(&im); return px;
}

/* Low-level: expand everything to 8-bit RGBA, no gamma / colour management. */
static unsigned char *lowlevel(const char *fn, unsigned *w, unsigned *h)
{
    FILE *f = fopen(fn, "rb"); if (!f) return NULL;
    png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop i = png_create_info_struct(p);
    if (setjmp(png_jmpbuf(p))) { png_destroy_read_struct(&p, &i, NULL); fclose(f); return NULL; }
    png_init_io(p, f); png_read_info(p, i);
    int ct = png_get_color_type(p, i), bd = png_get_bit_depth(p, i);
    if (bd == 16) png_set_strip_16(p);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(p);
    if (ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8(p);
    if (png_get_valid(p, i, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(p);
    if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(p);
    if (!(ct & PNG_COLOR_MASK_ALPHA) && !png_get_valid(p, i, PNG_INFO_tRNS)) png_set_filler(p, 0xff, PNG_FILLER_AFTER);
    png_set_interlace_handling(p);
    png_read_update_info(p, i);
    *w = png_get_image_width(p, i); *h = png_get_image_height(p, i);
    unsigned char *px = malloc((size_t)*w * *h * 4);
    png_bytep *rows = malloc(sizeof(png_bytep) * *h);
    for (unsigned y = 0; y < *h; ++y) rows[y] = px + (size_t)y * *w * 4;
    png_read_image(p, rows); png_read_end(p, NULL);
    free(rows); png_destroy_read_struct(&p, &i, NULL); fclose(f); return px;
}

int main(int argc, char **argv)
{
    int fails = 0;
    for (int a = 1; a < argc; ++a) {
        unsigned w, h, w2, h2; const int N = 20;
        double t0 = now(); unsigned char *s = NULL;
        for (int k = 0; k < N; ++k) { free(s); s = simple(argv[a], &w, &h); }
        double ts = (now() - t0) / N * 1e3;
        t0 = now(); unsigned char *l = NULL;
        for (int k = 0; k < N; ++k) { free(l); l = lowlevel(argv[a], &w2, &h2); }
        double tl = (now() - t0) / N * 1e3;
        size_t n = (size_t)w * h * 4, diff = 0; int maxd = 0;
        for (size_t k = 0; k < n; ++k) { int d = abs(s[k] - l[k]); if (d) { ++diff; if (d > maxd) maxd = d; } }
        printf("%s %ux%u simple=%.2f ms lowlevel=%.2f ms ratio=%.1f differing_bytes=%zu max_diff=%d\n",
               argv[a], w, h, ts, tl, ts / tl, diff, maxd);
        free(s); free(l);
    }
    return fails;
}
