// Host test for src/vita-prefetch.cpp (MKXP_VITA_ANIM_PREFETCH): bytes handed over equal a direct
// read; a file being read is waited for; a queued one is left to the caller; missing files fail;
// the alt fallback (cache entry missing -> image); clear while reading; the memory cap; stats.
#include "../../../src/vita-prefetch.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>
extern unsigned vitaPrefetchTestDelayUs;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL " __VA_ARGS__); std::printf("\n"); } } while (0)
static std::vector<unsigned char> slurp(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}
static void make(const std::string &p, size_t n, unsigned seed)
{
    std::vector<unsigned char> d(n);
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)((i * 2654435761u + seed) >> 13);
    std::ofstream(p, std::ios::binary).write((const char *)d.data(), (std::streamsize)n);
}
static void wait_ms(int ms) { usleep(ms * 1000); }
int main()
{
    char tmpl[] = "/tmp/pfXXXXXX";
    const std::string d = std::string(mkdtemp(tmpl)) + "/";
    make(d + "a.png", 1 << 20, 1); make(d + "c.png", 300, 2); make(d + "img.png", 70000, 3); make(d + "big.png", 9 << 20, 4);
    unsigned q, t, w, dr, kb;
    std::vector<unsigned char> out;
    // 1. read ahead, handed over, equal to a direct read
    vitaPrefetchAdd(d + "a.png"); vitaPrefetchAdd(d + "c.png"); vitaPrefetchAdd(d + "a.png");   /* duplicate */
    wait_ms(200);
    CHECK(vitaPrefetchTake(d + "a.png", out) && out == slurp(d + "a.png"), "a handed over");
    CHECK(vitaPrefetchTake(d + "c.png", out) && out == slurp(d + "c.png"), "c handed over");
    CHECK(!vitaPrefetchTake(d + "a.png", out), "taken only once");
    vitaPrefetchStats(&q, &t, &w, &dr, &kb);
    CHECK(q == 2 && t == 2 && kb == 0, "stats after take: q=%u t=%u kb=%u", q, t, kb);
    // 2. missing file: not handed over
    vitaPrefetchAdd(d + "missing.png"); wait_ms(100);
    CHECK(!vitaPrefetchTake(d + "missing.png", out), "missing");
    // 3. alt: cache entry missing -> image read and kept under its own path
    vitaPrefetchAdd(d + "cache/img_x.lz4", d + "img.png"); wait_ms(200);
    CHECK(!vitaPrefetchTake(d + "cache/img_x.lz4", out), "entry missing");
    CHECK(vitaPrefetchTake(d + "img.png", out) && out == slurp(d + "img.png"), "alt handed over");
    // 4. being read -> waited for; queued behind it -> left to the caller
    vitaPrefetchTestDelayUs = 300000;
    vitaPrefetchAdd(d + "a.png"); vitaPrefetchAdd(d + "c.png");
    wait_ms(50);   /* a.png is being read (300 ms), c.png queued */
    CHECK(!vitaPrefetchTake(d + "c.png", out), "queued: caller reads it");
    CHECK(vitaPrefetchTake(d + "a.png", out) && out == slurp(d + "a.png"), "waited for a");
    vitaPrefetchStats(&q, &t, &w, &dr, &kb);
    CHECK(w == 1, "one wait: %u", w);
    // 5. clear while reading: dropped, memory back to 0
    vitaPrefetchAdd(d + "img.png"); wait_ms(50);
    vitaPrefetchClear();
    CHECK(!vitaPrefetchTake(d + "img.png", out), "dropped while reading");
    wait_ms(400);
    vitaPrefetchStats(&q, &t, &w, &dr, &kb);
    CHECK(kb == 0, "held after clear: %u KiB", kb);
    vitaPrefetchTestDelayUs = 0;
    // 6. cap: 9 MiB file over the 8 MiB default is not held
    vitaPrefetchAdd(d + "big.png"); wait_ms(300);
    CHECK(!vitaPrefetchTake(d + "big.png", out), "over the cap");
    vitaPrefetchStats(&q, &t, &w, &dr, &kb);
    CHECK(kb == 0, "held %u", kb);
    // 7. read ahead but never taken: clear frees it (68 KiB held, then 0)
    vitaPrefetchAdd(d + "img.png"); wait_ms(100);
    vitaPrefetchStats(&q, &t, &w, &dr, &kb);
    CHECK(kb == 68, "img held: %u KiB", kb);
    vitaPrefetchClear();
    vitaPrefetchStats(&q, &t, &w, &dr, &kb);
    CHECK(kb == 0, "held after clear: %u KiB", kb);
    CHECK(!vitaPrefetchTake(d + "img.png", out), "cleared");
    std::printf(fails ? "FAIL %d\n" : "PASS prefetch\n", fails);
    std::system(("rm -rf " + d).c_str());
    return fails ? 1 : 0;
}
