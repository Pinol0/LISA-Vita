/*
 * Perf fix (MKXP_VITA_ANIM_PREFETCH): battle animation images read ahead on a background thread.
 * d78 profiler: Sprite_Base#load_animation_bitmap took ~170 ms when an animation started for the
 * first time (d80 block profiler: mostly the main thread blocked reading the file from the card,
 * e.g. Graphics/Animations/State2.png 3 reads / 150 ms), a visible stop in the middle of a battle.
 * At BattleManager.setup (main.cpp) the animations the party and the troop can use are queued;
 * this thread (created on demand; new threads run on cores 1-2, MKXP_VITA_CORE0_MAIN) reads each
 * whole file during the battle transition. VitaImageCache::readWholeFile asks here first: a file
 * read ahead is handed over (moved, no copy), one being read is waited for, one still queued is
 * taken out of the queue and read by the caller as before. The bytes are those the caller would
 * have read (same open/fstat/read loop). Unused entries are dropped at the end of the battle; at
 * most 8 MiB are held (a file that would exceed it is not read ahead).
 * Portable (pthreads, open/read): host test tools/hosttests/prefetch/.
 */
#include "vita-prefetch.h"

#include <algorithm>
#include <deque>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace
{
enum State { QUEUED, READING, DONE, FAILED };
struct Entry { State st = QUEUED; bool dropped = false; std::string alt; std::vector<unsigned char> data; };
pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gWork = PTHREAD_COND_INITIALIZER, gDone = PTHREAD_COND_INITIALIZER;
std::unordered_map<std::string, Entry> gEntries;
std::deque<std::string> gQueue;
bool gStarted = false;
size_t gHeld = 0, gCap = 8u << 20;
unsigned gQueued = 0, gTaken = 0, gWaited = 0, gDropped = 0;

enum ReadResult { READ_OK, READ_NOFILE, READ_FAILED };
/* Same reading as VitaImageCache::readWholeFile; size checked against the cap first. */
#ifdef VITA_PREFETCH_TEST
} // namespace
unsigned vitaPrefetchTestDelayUs = 0;   /* host test: a slow card */
namespace
{
#endif
ReadResult readFile(const std::string &path, std::vector<unsigned char> &out)
{
#ifdef VITA_PREFETCH_TEST
    if (vitaPrefetchTestDelayUs) usleep(vitaPrefetchTestDelayUs);
#endif
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return READ_NOFILE;
    struct stat st;
    bool ok = ::fstat(fd, &st) == 0 && st.st_size >= 0;
    if (ok) {
        pthread_mutex_lock(&gLock);
        ok = gHeld + (size_t)st.st_size <= gCap;
        if (ok) gHeld += (size_t)st.st_size;   /* reserved now, released if the read fails */
        pthread_mutex_unlock(&gLock);
    }
    if (ok) {
        try { out.resize((size_t)st.st_size); } catch (...) { ok = false; }
        size_t got = 0;
        while (ok && got < out.size()) {
            const ssize_t n = ::read(fd, out.data() + got, std::min<size_t>(out.size() - got, 1u << 20));
            if (n <= 0) { ok = false; break; }
            got += (size_t)n;
        }
        if (!ok) {
            pthread_mutex_lock(&gLock);
            gHeld -= (size_t)st.st_size;
            pthread_mutex_unlock(&gLock);
            out.clear();
        }
    }
    ::close(fd);
    return ok ? READ_OK : READ_FAILED;
}

/* Lock held. Read path (entry already READING) without the lock, then publish the result. */
ReadResult readEntry(const std::string &path)
{
    pthread_mutex_unlock(&gLock);
    std::vector<unsigned char> data;
    const ReadResult r = readFile(path, data);
    pthread_mutex_lock(&gLock);
    auto it = gEntries.find(path);   /* still there: only this thread erases READING entries */
    if (it->second.dropped) {
        if (r == READ_OK) gHeld -= data.size();
        gEntries.erase(it);
    } else {
        it->second.st = r == READ_OK ? DONE : FAILED;
        it->second.data.swap(data);
    }
    pthread_cond_broadcast(&gDone);
    return r;
}

void *worker(void *)
{
    pthread_mutex_lock(&gLock);
    for (;;) {
        while (gQueue.empty()) pthread_cond_wait(&gWork, &gLock);
        const std::string path = gQueue.front();
        gQueue.pop_front();
        auto it = gEntries.find(path);
        if (it == gEntries.end() || it->second.st != QUEUED) continue;
        it->second.st = READING;
        const std::string alt = it->second.alt;
        if (readEntry(path) == READ_NOFILE && !alt.empty() && !gEntries.count(alt)) {
            gEntries[alt].st = READING;
            ++gQueued;
            readEntry(alt);
        }
    }
    return nullptr;
}
} // namespace

void vitaPrefetchSetCap(size_t bytes)
{
    pthread_mutex_lock(&gLock);
    gCap = bytes;
    pthread_mutex_unlock(&gLock);
}

void vitaPrefetchAdd(const std::string &path, const std::string &alt)
{
    pthread_mutex_lock(&gLock);
    if (!gEntries.count(path)) {
        gEntries[path].alt = alt;
        gQueue.push_back(path);
        ++gQueued;
        if (!gStarted) {
            pthread_t t;
            pthread_attr_t a;
            pthread_attr_init(&a);
            pthread_attr_setstacksize(&a, 64 * 1024);
            pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
            gStarted = pthread_create(&t, &a, worker, nullptr) == 0;
            pthread_attr_destroy(&a);
        }
        pthread_cond_signal(&gWork);
    }
    pthread_mutex_unlock(&gLock);
}

bool vitaPrefetchTake(const std::string &path, std::vector<unsigned char> &out)
{
    pthread_mutex_lock(&gLock);
    auto it = gEntries.find(path);
    bool got = false;
    if (it != gEntries.end() && !it->second.dropped) {
        if (it->second.st == READING) {
            ++gWaited;
            while ((it = gEntries.find(path)) != gEntries.end() && it->second.st == READING) pthread_cond_wait(&gDone, &gLock);
        }
        if (it != gEntries.end()) {
            if (it->second.st == DONE) {
                out.swap(it->second.data);
                gHeld -= out.size();
                ++gTaken;
                got = true;
            }
            gEntries.erase(it);   /* QUEUED (the worker skips it), DONE handed over, or FAILED */
        }
    }
    pthread_mutex_unlock(&gLock);
    return got;
}

void vitaPrefetchClear()
{
    pthread_mutex_lock(&gLock);
    gQueue.clear();
    for (auto it = gEntries.begin(); it != gEntries.end();) {
        if (it->second.st == READING) { it->second.dropped = true; ++it; continue; }
        if (it->second.st == DONE) gHeld -= it->second.data.size();
        ++gDropped;
        it = gEntries.erase(it);
    }
    pthread_mutex_unlock(&gLock);
}

void vitaPrefetchStats(unsigned *queued, unsigned *taken, unsigned *waited, unsigned *dropped, unsigned *kbHeld)
{
    pthread_mutex_lock(&gLock);
    *queued = gQueued; *taken = gTaken; *waited = gWaited; *dropped = gDropped; *kbHeld = (unsigned)(gHeld / 1024);
    pthread_mutex_unlock(&gLock);
}

extern "C" void vitaPrefetchStatsC(unsigned *queued, unsigned *taken, unsigned *waited, unsigned *dropped, unsigned *kbHeld)
{
    vitaPrefetchStats(queued, taken, waited, dropped, kbHeld);
}
