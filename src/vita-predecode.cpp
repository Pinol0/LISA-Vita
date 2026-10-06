/*
 * Perf fix (MKXP_VITA_ANIM_PREDECODE): battle animation images and sound effects decoded ahead on a
 * background thread. d82 profiler, slow battle windows: Sprite_Battler#animation_process_timing
 * 1.25 ms/frame (the animation's SEs: file + whole Ogg Vorbis decode on the main thread at the
 * first play) and load_animation_bitmap 1.15 ms/frame (with d81's read-ahead the file is already
 * in RAM, but the decoded-image cache entry is still checked, LZ4-decompressed and copied on the
 * main thread: tens of ms per animation sheet).
 * At BattleManager.setup (main.cpp, VitaAnimPrefetch) the same animations are queued here:
 *   image job (source path): VitaImageCache::loadQuiet = VitaImageCache::load without its counters
 *     (same file checks, same LZ4 block, same pixels); when there is no valid entry or the decoded
 *     size does not fit the cap, the job hands over to the d81 read-ahead (vitaPrefetchAdd of the
 *     entry / the PNG), as before;
 *   SE job ("Audio/SE/<name>", the key SoundEmitter::allocateBuffer receives): vitaDecodeAudioByName
 *     = FileSystem::openRead's candidates in its order + vitaDecodeAudioAll, without OpenAL.
 * The main thread takes the result where it would compute it (bitmap-vita-minimal.cpp before
 * VitaImageCache::load; soundemitter.cpp before FileSystem::openRead): a job being worked on is
 * waited for, a queued one is dropped from the queue and done by the caller as before, a failed
 * one is done again by the caller (same errors). GL upload and OpenAL buffer stay on the main
 * thread. Results not taken are freed at the end of the battle. Caps: decoded images 8 MiB, PCM
 * 3 MiB. Portable except the two decode functions it calls: host test tools/hosttests/predecode/.
 */
#include "vita-predecode.h"

#include <deque>
#include <pthread.h>
#include <unordered_map>

#include "vita-image-cache.h"
#include "vita-prefetch.h"

bool vitaDecodeAudioByName(const char *filename, std::vector<uint8_t> &data, int &sampleSize, int &channels, int &rate);

namespace
{
enum State { QUEUED, WORKING, DONE, FAILED };
enum Kind { IMAGE, SOUND };
struct Entry
{
    State st = QUEUED;
    Kind kind = IMAGE;
    bool dropped = false;
    std::vector<unsigned char> data;
    int a = 0, b = 0, c = 0;   /* image: w, h; sound: sampleSize, channels, rate */
};
pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gWork = PTHREAD_COND_INITIALIZER, gDone = PTHREAD_COND_INITIALIZER;
std::unordered_map<std::string, Entry> gEntries;   /* key: "i:" + source path / "s:" + RGSS name */
std::deque<std::string> gQueue;
bool gStarted = false;
size_t gImgHeld = 0, gSndHeld = 0, gImgCap = 8u << 20, gSndCap = 3u << 20;
unsigned gQueued = 0, gTaken = 0, gWaited = 0, gDropped = 0, gHanded = 0;

/* Lock held: room for n more bytes of this kind (reserved now). */
bool reserve(Kind k, size_t n)
{
    size_t &held = k == IMAGE ? gImgHeld : gSndHeld;
    if (held + n > (k == IMAGE ? gImgCap : gSndCap)) return false;
    held += n;
    return true;
}
void release(Kind k, size_t n) { (k == IMAGE ? gImgHeld : gSndHeld) -= n; }

/* Lock not held. true with the result. */
bool work(const std::string &key, Kind kind, Entry &out)
{
    const std::string name = key.substr(2);
    if (kind == IMAGE) {
        const size_t raw = VitaImageCache::rawBytesOf(name);
        pthread_mutex_lock(&gLock);
        const bool room = raw && reserve(IMAGE, raw);
        pthread_mutex_unlock(&gLock);
        bool ok = false;
        if (room) {
            ok = VitaImageCache::loadQuiet(name, out.a, out.b, out.data) && out.data.size() == raw;
            if (!ok) {
                out.data.clear();
                pthread_mutex_lock(&gLock);
                release(IMAGE, raw);
                pthread_mutex_unlock(&gLock);
            }
        }
        if (!ok) {
            /* d81 behaviour: read the entry ahead, or the image itself when there is no entry. */
            pthread_mutex_lock(&gLock);
            ++gHanded;
            pthread_mutex_unlock(&gLock);
            const std::string entry = VitaImageCache::entryPathFor(name);
            if (entry.empty()) vitaPrefetchAdd(name);
            else vitaPrefetchAdd(entry, name);
        }
        return ok;
    }
    std::vector<uint8_t> pcm;
    if (!vitaDecodeAudioByName(name.c_str(), pcm, out.a, out.b, out.c)) return false;
    pthread_mutex_lock(&gLock);
    const bool room = reserve(SOUND, pcm.size());
    pthread_mutex_unlock(&gLock);
    if (!room) return false;
    out.data.swap(pcm);
    return true;
}

void *worker(void *)
{
    pthread_mutex_lock(&gLock);
    for (;;) {
        while (gQueue.empty()) pthread_cond_wait(&gWork, &gLock);
        const std::string key = gQueue.front();
        gQueue.pop_front();
        auto it = gEntries.find(key);
        if (it == gEntries.end() || it->second.st != QUEUED) continue;
        it->second.st = WORKING;
        const Kind kind = it->second.kind;
        pthread_mutex_unlock(&gLock);
        Entry res;
        const bool ok = work(key, kind, res);
        pthread_mutex_lock(&gLock);
        it = gEntries.find(key);   /* still there: only this thread erases WORKING entries */
        if (it->second.dropped) {
            if (ok) release(kind, res.data.size());
            gEntries.erase(it);
        } else {
            it->second.st = ok ? DONE : FAILED;
            it->second.data.swap(res.data);
            it->second.a = res.a; it->second.b = res.b; it->second.c = res.c;
        }
        pthread_cond_broadcast(&gDone);
    }
    return nullptr;
}

void add(const std::string &key, Kind kind)
{
    pthread_mutex_lock(&gLock);
    if (!gEntries.count(key)) {
        gEntries[key].kind = kind;
        gQueue.push_back(key);
        ++gQueued;
        if (!gStarted) {
            pthread_t t;
            pthread_attr_t a;
            pthread_attr_init(&a);
            pthread_attr_setstacksize(&a, 128 * 1024);   /* vorbis decode */
            pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
            gStarted = pthread_create(&t, &a, worker, nullptr) == 0;
            pthread_attr_destroy(&a);
        }
        pthread_cond_signal(&gWork);
    }
    pthread_mutex_unlock(&gLock);
}

bool take(const std::string &key, Entry &out)
{
    pthread_mutex_lock(&gLock);
    auto it = gEntries.find(key);
    bool got = false;
    if (it != gEntries.end() && !it->second.dropped) {
        if (it->second.st == WORKING) {
            ++gWaited;
            while ((it = gEntries.find(key)) != gEntries.end() && it->second.st == WORKING) pthread_cond_wait(&gDone, &gLock);
        }
        if (it != gEntries.end()) {
            if (it->second.st == DONE) {
                out.data.swap(it->second.data);
                out.a = it->second.a; out.b = it->second.b; out.c = it->second.c;
                release(it->second.kind, out.data.size());
                ++gTaken;
                got = true;
            }
            gEntries.erase(it);   /* QUEUED (the worker skips it), DONE handed over, or FAILED */
        }
    }
    pthread_mutex_unlock(&gLock);
    return got;
}
} // namespace

void vitaPredecodeImage(const std::string &srcPath) { add("i:" + srcPath, IMAGE); }
void vitaPredecodeSound(const std::string &rgssName) { add("s:" + rgssName, SOUND); }

bool vitaPredecodeTakeImage(const std::string &srcPath, int &w, int &h, std::vector<unsigned char> &px)
{
    Entry e;
    if (!take("i:" + srcPath, e)) return false;
    w = e.a; h = e.b;
    px.swap(e.data);
    return true;
}

bool vitaPredecodeTakeSound(const std::string &rgssName, std::vector<uint8_t> &pcm, int &sampleSize, int &channels, int &rate)
{
    Entry e;
    if (!take("s:" + rgssName, e)) return false;
    sampleSize = e.a; channels = e.b; rate = e.c;
    pcm.swap(e.data);
    return true;
}

void vitaPredecodeClear()
{
    pthread_mutex_lock(&gLock);
    gQueue.clear();
    for (auto it = gEntries.begin(); it != gEntries.end();) {
        if (it->second.st == WORKING) { it->second.dropped = true; ++it; continue; }
        if (it->second.st == DONE) release(it->second.kind, it->second.data.size());
        ++gDropped;
        it = gEntries.erase(it);
    }
    pthread_mutex_unlock(&gLock);
}

void vitaPredecodeSetCaps(size_t imageBytes, size_t soundBytes)
{
    pthread_mutex_lock(&gLock);
    gImgCap = imageBytes;
    gSndCap = soundBytes;
    pthread_mutex_unlock(&gLock);
}

extern "C" void vitaPredecodeStatsC(unsigned *queued, unsigned *taken, unsigned *waited, unsigned *dropped, unsigned *handed,
                                    unsigned *imgKb, unsigned *sndKb)
{
    pthread_mutex_lock(&gLock);
    *queued = gQueued; *taken = gTaken; *waited = gWaited; *dropped = gDropped; *handed = gHanded;
    *imgKb = (unsigned)(gImgHeld / 1024); *sndKb = (unsigned)(gSndHeld / 1024);
    pthread_mutex_unlock(&gLock);
}
