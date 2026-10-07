/*
 * Decoded-image disk cache (MKXP_VITA_IMG_DISK_CACHE): large PNGs (decoded size >= 1 MiB) are
 * stored once, after their first decode, as LZ4-compressed RGBA in <game root>/cache/, and later
 * loads read that instead of decoding the PNG (libpng ~11 MB/s on the Vita: a 960x1152 battle
 * animation sheet took 0.4-0.7 s, and LISA's MOG Anti Animation Lag frees them at every scene
 * change, so every battle decoded them again). Same pixels as the PNG decode (tight RGBA rows).
 * An entry is valid only for the same source path, size and modification time.
 */
#ifndef VITA_IMAGE_CACHE_H
#define VITA_IMAGE_CACHE_H

#include <cstddef>
#include <string>
#include <vector>

namespace VitaImageCache
{
/* Decoded images at least this big are cached (smaller ones decode fast, or sit in the PNG cache). */
const size_t kMinBytes = 1u << 20;

/* Directory for the entries (created on the first store), with the trailing '/'. */
void setDir(const std::string &dir);

/* Cached pixels of `path` if a valid entry exists: w, h and tight RGBA rows (w * 4 bytes). */
bool load(const std::string &path, int &w, int &h, std::vector<unsigned char> &px);

/* Writes the entry for `path` (best effort: any failure just leaves no entry). */
void store(const std::string &path, int w, int h, const unsigned char *px);

/* Whole file in one buffer with large read() calls (newlib stdio reads in 1 KiB steps: d47, 1.7 MB
 * took ~0.5 s to read from the memory card). False if the file cannot be opened or read. */
bool readWholeFile(const std::string &path, std::vector<unsigned char> &out);

/* Counters for PERF: hits, misses (no valid entry), stores, failed stores. */
void stats(unsigned *hits, unsigned *misses, unsigned *stores, unsigned *storeFails);
/* load() without touching the hit/miss counters (background decoder), and the hit counted later. */
bool loadQuiet(const std::string &path, int &w, int &h, std::vector<unsigned char> &px);
void countHit();
/* Decoded size recorded in the entry's header (0: no valid entry), read without the whole file. */
size_t rawBytesOf(const std::string &path);
/* The cache entry file for a source image ("" when no cache folder is set): read-ahead target. */
std::string entryPathFor(const std::string &path);
#ifdef MKXP_VITA_TEX_DIRECT_CACHE
/* load() into memory given by `dst` once the size is known: dst(ctx, w, h, &stride) returns room for
 * h rows of `stride` (>= w * 4) bytes, or null to give up. The block is decompressed in place and
 * its tight rows moved to their stride. False (and nothing written past dst's room) when there is no
 * valid entry, dst gives up, or memory for the compressed block runs out. */
typedef unsigned char *(*DstFn)(void *ctx, int w, int h, size_t *stride);
bool loadInto(const std::string &path, DstFn dst, void *ctx);
#endif
}

#endif
