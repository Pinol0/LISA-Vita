/* Extracts an RPG Maker VX Ace archive (Game.rgss3a, "RGSSAD" version 3) into a folder, for analysing
 * your own copy of a game on the PC (the port itself needs the files extracted; nothing from a game is
 * ever part of this project).
 *   cc -O2 -o rgss3a_extract rgss3a_extract.c && ./rgss3a_extract Game.rgss3a OUT_DIR
 * Format: header "RGSSAD\0\3", a u32 seed (key = seed * 9 + 3); then entries until an offset of 0:
 * offset, size, data key, name length (each u32 XOR key), the name (byte i XOR key >> 8*(i%4));
 * the data is XORed 4 bytes at a time with its key, which becomes key * 7 + 3 after every 4 bytes. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint32_t rd(FILE *f)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) { fprintf(stderr, "truncated archive\n"); exit(1); }
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void mkdirs(char *path)   /* every folder of path (the file name itself excluded) */
{
    for (char *p = path + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(path, 0755) != 0 && errno != EEXIST) { perror(path); exit(1); }
            *p = '/';
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s Game.rgss3a OUT_DIR\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "RGSSAD\0\3", 8)) { fprintf(stderr, "not an RGSS3 archive\n"); return 1; }
    uint32_t key = rd(f) * 9u + 3u;
    unsigned files = 0;
    unsigned long long bytes = 0;
    for (;;) {
        const uint32_t off = rd(f) ^ key;
        if (off == 0)
            break;
        const uint32_t size = rd(f) ^ key, dkey0 = rd(f) ^ key, nlen = rd(f) ^ key;
        if (nlen == 0 || nlen > 4096) { fprintf(stderr, "bad entry\n"); return 1; }
        char *name = malloc(strlen(argv[2]) + nlen + 2);
        sprintf(name, "%s/", argv[2]);
        char *n = name + strlen(name);
        for (uint32_t i = 0; i < nlen; ++i) {
            int c = fgetc(f);
            if (c == EOF) { fprintf(stderr, "truncated name\n"); return 1; }
            c ^= (key >> (8 * (i % 4))) & 0xff;
            n[i] = c == '\\' ? '/' : (char)c;
            if (n[i] == '.' && i > 0 && n[i - 1] == '.') { fprintf(stderr, "unsafe name\n"); return 1; }
        }
        n[nlen] = 0;
        const long next = ftell(f);
        unsigned char *buf = malloc(size ? size : 1);
        if (fseek(f, (long)off, SEEK_SET) || fread(buf, 1, size, f) != size) { fprintf(stderr, "truncated data: %s\n", n); return 1; }
        uint32_t dk = dkey0;
        for (uint32_t i = 0; i < size; ++i) {
            buf[i] ^= (dk >> (8 * (i % 4))) & 0xff;
            if (i % 4 == 3)
                dk = dk * 7u + 3u;
        }
        mkdirs(name);
        FILE *o = fopen(name, "wb");
        if (!o || fwrite(buf, 1, size, o) != size) { perror(name); return 1; }
        fclose(o);
        free(buf);
        free(name);
        fseek(f, next, SEEK_SET);
        ++files;
        bytes += size;
    }
    fclose(f);
    printf("%u files, %llu bytes\n", files, bytes);
    return 0;
}
