/*
 * Decoded-image disk cache (MKXP_VITA_IMG_DISK_CACHE), see vita-image-cache.h.
 *
 * Entry file: <dir>img_<fnv64 of path>.lz4 =
 *   header { magic "VIC1", version, width, height, raw bytes, compressed bytes, source size,
 *            source mtime, path length, FNV-1a of the LZ4 block } + path bytes + LZ4 block.
 * The checksum catches a damaged block (LZ4 alone may decompress it to the right length).
 * File I/O uses open/read/write in large calls (d48): newlib stdio moves data in 1 KiB steps.
 * The path is stored and compared, so a hash collision is just a miss. A new entry is written to a
 * temporary name and renamed, so a crash while writing leaves no half entry. The buffers are
 * std::vectors (operator new): on the Vita blocks >= 128 KiB come from the big-block pool, not
 * the fragmentation-prone newlib heap (vita-big-alloc.cpp).
 * Portable C++ (stat/mkdir/stdio): host test in tools/hosttests/img-cache/.
 */
#include "vita-image-cache.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <new>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../third_party/lz4/lz4.h"
#ifdef MKXP_VITA_ANIM_PREFETCH
#include "vita-prefetch.h"
#endif

namespace VitaImageCache
{
namespace
{
struct Header
{
    char magic[4];
    uint32_t version, width, height, rawBytes, compBytes;
    uint64_t srcSize, srcMtime;
    uint32_t pathLen;
    uint32_t compFnv;
};
const uint32_t kVersion = 2;

std::string gDir;
bool gDirMade = false;
unsigned gHits = 0, gMisses = 0, gStores = 0, gStoreFails = 0;

uint64_t fnv64(const std::string &s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

uint32_t fnv32(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

std::string entryName(const std::string &path)
{
    char name[40];
    std::snprintf(name, sizeof(name), "img_%016llx.lz4", (unsigned long long)fnv64(path));
    return gDir + name;
}

bool sourceStat(const std::string &path, uint64_t &size, uint64_t &mtime)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return false;
    size = (uint64_t)st.st_size;
    mtime = (uint64_t)st.st_mtime;
    return true;
}
}

bool readWholeFile(const std::string &path, std::vector<unsigned char> &out)
{
#ifdef MKXP_VITA_ANIM_PREFETCH
    /* Read ahead by vita-prefetch.cpp (same open/fstat/read loop): hand the bytes over. */
    if (vitaPrefetchTake(path, out))
        return true;
#endif
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    struct stat st;
    bool ok = ::fstat(fd, &st) == 0 && st.st_size >= 0;
    if (ok) {
        try {
            out.resize((size_t)st.st_size);
        } catch (const std::bad_alloc &) {
            ok = false;
        }
    }
    size_t got = 0;
    while (ok && got < out.size()) {
        const size_t want = std::min<size_t>(out.size() - got, 1u << 20);
        const ssize_t n = ::read(fd, out.data() + got, want);
        if (n <= 0) { ok = false; break; }
        got += (size_t)n;
    }
    ::close(fd);
    if (!ok) out.clear();
    return ok;
}

namespace
{
bool writeAll(int fd, const void *p, size_t n)
{
    const char *c = static_cast<const char *>(p);
    while (n) {
        const ssize_t w = ::write(fd, c, std::min<size_t>(n, 1u << 20));
        if (w <= 0) return false;
        c += w;
        n -= (size_t)w;
    }
    return true;
}
}

void setDir(const std::string &dir)
{
    gDir = dir;
    gDirMade = false;
}

namespace
{
bool loadImpl(const std::string &path, int &w, int &h, std::vector<unsigned char> &px, bool count);
}
bool load(const std::string &path, int &w, int &h, std::vector<unsigned char> &px)
{
    return loadImpl(path, w, h, px, true);
}
bool loadQuiet(const std::string &path, int &w, int &h, std::vector<unsigned char> &px)
{
    return loadImpl(path, w, h, px, false);
}
void countHit()
{
    ++gHits;
}
size_t rawBytesOf(const std::string &path)
{
    if (gDir.empty()) return 0;
    const int fd = ::open(entryName(path).c_str(), O_RDONLY);
    if (fd < 0) return 0;
    Header hd;
    const bool ok = ::read(fd, &hd, sizeof(hd)) == (ssize_t)sizeof(hd) && !std::memcmp(hd.magic, "VIC1", 4) && hd.version == kVersion;
    ::close(fd);
    return ok ? hd.rawBytes : 0;
}
namespace
{
/* count: the hit/miss counters (main thread only; the background decoder of vita-predecode.cpp
 * passes false and the main thread counts the hit when it takes the pixels). */
bool loadImpl(const std::string &path, int &w, int &h, std::vector<unsigned char> &px, bool count)
{
    uint64_t size = 0, mtime = 0;
    if (gDir.empty() || !sourceStat(path, size, mtime))
        return false;
    std::vector<unsigned char> file;
    if (!readWholeFile(entryName(path), file)) {
        if (count) ++gMisses;
        return false;
    }
    bool ok = false;
    Header hd;
    try {
        const size_t pos = sizeof(hd);
        if (file.size() >= pos) {
            std::memcpy(&hd, file.data(), sizeof(hd));
            if (!std::memcmp(hd.magic, "VIC1", 4) && hd.version == kVersion && hd.srcSize == size && hd.srcMtime == mtime &&
                hd.pathLen == path.size() && hd.width > 0 && hd.height > 0 && (uint64_t)hd.width * hd.height * 4 == hd.rawBytes &&
                hd.compBytes > 0 && hd.compBytes <= (uint32_t)LZ4_compressBound((int)hd.rawBytes) &&
                file.size() == pos + hd.pathLen + hd.compBytes &&
                !std::memcmp(file.data() + pos, path.data(), path.size()) &&
                fnv32(file.data() + pos + hd.pathLen, hd.compBytes) == hd.compFnv) {
                px.resize(hd.rawBytes);
                const int n = LZ4_decompress_safe(reinterpret_cast<const char *>(file.data() + pos + hd.pathLen),
                                                  reinterpret_cast<char *>(px.data()), (int)hd.compBytes, (int)hd.rawBytes);
                ok = n == (int)hd.rawBytes;
            }
        }
    } catch (const std::bad_alloc &) {
        ok = false;
    }
    if (!ok) {
        px.clear();
        if (count) ++gMisses;
        return false;
    }
    w = (int)hd.width;
    h = (int)hd.height;
    if (count) ++gHits;
    return true;
}
}

void store(const std::string &path, int w, int h, const unsigned char *px)
{
    uint64_t size = 0, mtime = 0;
    const size_t raw = (size_t)w * h * 4;
    if (gDir.empty() || w <= 0 || h <= 0 || raw < kMinBytes || raw > 0x7fffffffu || !sourceStat(path, size, mtime))
        return;
    if (!gDirMade) {
        ::mkdir(gDir.c_str(), 0777);   /* may already exist */
        gDirMade = true;
    }
    try {
        std::vector<char> comp((size_t)LZ4_compressBound((int)raw));
        const int n = LZ4_compress_default(reinterpret_cast<const char *>(px), comp.data(), (int)raw, (int)comp.size());
        if (n <= 0) {
            ++gStoreFails;
            return;
        }
        Header hd;
        std::memcpy(hd.magic, "VIC1", 4);
        hd.version = kVersion;
        hd.width = (uint32_t)w;
        hd.height = (uint32_t)h;
        hd.rawBytes = (uint32_t)raw;
        hd.compBytes = (uint32_t)n;
        hd.srcSize = size;
        hd.srcMtime = mtime;
        hd.pathLen = (uint32_t)path.size();
        hd.compFnv = fnv32(reinterpret_cast<const unsigned char *>(comp.data()), (size_t)n);
        const std::string final_ = entryName(path), tmp = final_ + ".tmp";
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            ++gStoreFails;
            return;
        }
        const bool wrote = writeAll(fd, &hd, sizeof(hd)) && writeAll(fd, path.data(), path.size()) &&
                           writeAll(fd, comp.data(), (size_t)n);
        const bool closed = ::close(fd) == 0;
        std::remove(final_.c_str());   /* stale entry (source changed): rename does not replace on every FS */
        if (!wrote || !closed || std::rename(tmp.c_str(), final_.c_str()) != 0) {
            std::remove(tmp.c_str());
            ++gStoreFails;
            return;
        }
        ++gStores;
    } catch (const std::bad_alloc &) {
        ++gStoreFails;
    }
}

std::string entryPathFor(const std::string &path)
{
    return gDir.empty() ? std::string() : entryName(path);
}
void stats(unsigned *hits, unsigned *misses, unsigned *stores, unsigned *storeFails)
{
    *hits = gHits;
    *misses = gMisses;
    *stores = gStores;
    *storeFails = gStoreFails;
}
}
