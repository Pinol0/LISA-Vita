#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Reference: current path (tight buffer, then vitaGL-style row copy to aligned stride).
 * New path: png_image_finish_read with row_stride = align8(w)*4 directly. Compare the texture bytes. */
int main(int argc, char **argv) {
  int fails = 0, n = 0;
  for (int i = 1; i < argc; i++) {
    png_image a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.version = b.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&a, argv[i]) || !png_image_begin_read_from_file(&b, argv[i])) continue;
    a.format = b.format = PNG_FORMAT_RGBA;
    unsigned w = a.width, h = a.height, aw = (w + 7) & ~7u;
    unsigned char *tight = malloc((size_t)w*h*4), *texA = calloc((size_t)aw*h*4,1), *texB = calloc((size_t)aw*h*4,1);
    if (!png_image_finish_read(&a, NULL, tight, 0, NULL)) { printf("FAIL decode %s\n", argv[i]); fails++; continue; }
    for (unsigned y = 0; y < h; y++) memcpy(texA + (size_t)y*aw*4, tight + (size_t)y*w*4, (size_t)w*4);
    if (!png_image_finish_read(&b, NULL, texB, (png_int_32)(aw*4), NULL)) { printf("FAIL stride %s\n", argv[i]); fails++; continue; }
    int same = 1; for (unsigned y = 0; y < h && same; y++) same = !memcmp(texA + (size_t)y*aw*4, texB + (size_t)y*aw*4, (size_t)w*4);
    if (!same) { printf("DIFF %s %ux%u\n", argv[i], w, h); fails++; }
    n++; free(tight); free(texA); free(texB);
  }
  printf("compared=%d fails=%d\n", n, fails); return fails;
}
