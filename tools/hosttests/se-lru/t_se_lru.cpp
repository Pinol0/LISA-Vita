// SE cache of mkxp-z's SoundEmitter (MKXP_VITA_SE_LRU): the real SoundEmitter::allocateBuffer
// (extracted from soundemitter.cpp by run.sh into allocate_buffer.inc) and the real IntruList,
// with stub file loading. A trace of SE plays (Zipf popularity, sizes like LISA's battle SE) must give
// exactly the loads of a least-recently-used cache of the same byte budget. Without the fix (built
// without -DMKXP_VITA_SE_LRU) the loads differ: the buffer just played is the first one evicted.
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "intrulist.h"

static unsigned gLoads = 0;
static std::unordered_map<std::string, uint32_t> gSizes;

struct SoundBuffer
{
    std::string key;
    IntruListLink<SoundBuffer> link;
    uint32_t bytes = 0;
    uint8_t refCount = 1;
    SoundBuffer() : link(this) {}
    static void deref(SoundBuffer *b) { if (--b->refCount == 0) delete b; }
};
struct BufferHash
{
    std::unordered_map<std::string, SoundBuffer *> m;
    SoundBuffer *value(const std::string &k, SoundBuffer *d) const { auto it = m.find(k); return it == m.end() ? d : it->second; }
    void insert(const std::string &k, SoundBuffer *v) { m[k] = v; }
    void remove(const std::string &k) { m.erase(k); }
};
struct SoundOpenHandler { SoundBuffer *buffer = nullptr; };
struct FileSystemStub
{
    void openRead(SoundOpenHandler &h, const char *name)
    {
        ++gLoads;
        h.buffer = new SoundBuffer;
        h.buffer->bytes = gSizes[name];
    }
};
struct SharedStateStub { FileSystemStub fs; FileSystemStub &fileSystem() { return fs; } };
static SharedStateStub gState;
static SharedStateStub *shState = &gState;
struct Debug { template <class T> Debug &operator<<(const T &) { return *this; } };
static std::string vitaSoundError;
#define MKXP_VITA_AUDIO
#define SE_CACHE_MEM (4 * 1024 * 1024)

struct SoundEmitter
{
    IntruList<SoundBuffer> buffers;
    BufferHash bufferHash;
    uint32_t bufferBytes = 0;
    SoundBuffer *allocateBuffer(const std::string &filename);
};
#include "allocate_buffer.inc"

int main()
{
    std::mt19937 rng(7);
    const int kSounds = 48;
    std::vector<std::string> names;
    std::vector<double> weight;
    for (int i = 0; i < kSounds; ++i) {
        names.push_back("Audio/SE/se" + std::to_string(i));
        gSizes[names.back()] = 60000 + (uint32_t)(rng() % 660000);   /* 60-720 KB of PCM */
        weight.push_back(1.0 / std::pow(i + 1, 1.1));
    }
    std::discrete_distribution<int> pick(weight.begin(), weight.end());

    SoundEmitter se;
    /* model: LRU by bytes, same rule as upstream (evict from the least recent while over budget) */
    std::list<std::string> lru;
    std::unordered_map<std::string, std::list<std::string>::iterator> pos;
    uint64_t bytes = 0;
    unsigned modelLoads = 0, mismatch = 0;
    const int kPlays = 20000;
    for (int t = 0; t < kPlays; ++t) {
        const std::string &n = names[pick(rng)];
        const unsigned before = gLoads;
        se.allocateBuffer(n);
        const bool loaded = gLoads != before;
        auto it = pos.find(n);
        bool modelLoaded = false;
        if (it != pos.end()) {
            lru.splice(lru.begin(), lru, it->second);
        } else {
            modelLoaded = true;
            ++modelLoads;
            bytes += gSizes[n];
            while (bytes > SE_CACHE_MEM && !lru.empty()) {
                bytes -= gSizes[lru.back()];
                pos.erase(lru.back());
                lru.pop_back();
            }
            lru.push_front(n);
            pos[n] = lru.begin();
        }
        if (loaded != modelLoaded && ++mismatch <= 5)
            std::printf("  play %d %s: cache %s, LRU model %s\n", t, n.c_str(), loaded ? "loaded" : "hit", modelLoaded ? "loaded" : "hit");
    }
    std::printf("%s  %d plays: %u loads from the card (LRU model %u), %u decisions differ from LRU\n",
                mismatch ? "FAIL" : "PASS", kPlays, gLoads, modelLoads, mismatch);
    return mismatch ? 1 : 0;
}
