/*
 * Perf fix (MKXP_VITA_FS_INDEX): know which game files exist without asking the memory card.
 * RGSS names come without extension; the audio backend tries "", .ogg, .wav, .mp3, .mid, .midi and
 * the bitmap loader the bare name then .png (+ the decoded-image cache stats the source first).
 * vitasdk's newlib open() runs __is_dir() -> sceIoGetstat before every open, and on the card a
 * lookup of a name that does not exist costs 13-43 ms (d79/d80 block profiler: Audio/SE/Echo,
 * Graphics/Battlers/cosmic1, Characters/$Brad: 25-112 such stats per map / battle load, 0.1-0.4 s).
 * The first query in a folder lists it once (sceIoDopen/sceIoDread: regular files only, names
 * lower-cased: the card's exFAT is case-insensitive); later queries are answered from memory, and a
 * candidate known to be missing is skipped (opening it would fail: no such file, or a directory).
 * Only the read-only asset folders under the configured roots are indexed (Graphics/, Audio/,
 * Data/, Fonts/ of the game root): files there do not change while the game runs. Anything else
 * (saves, logs, the image cache, paths with "." or ".." parts) answers -1 = try for real.
 * Host test: tools/hosttests/fs-index/ (opendir/readdir instead of sceIoDopen).
 */
#include "vita-fs-index.h"

#include <cctype>
#include <cstring>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#ifdef __vita__
#include "vita_paths.h"
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace
{
struct Dir
{
    bool ok = false;   /* listed */
    std::unordered_set<std::string> files;
};
pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<std::string, Dir> gDirs;
std::vector<std::string> gRoots;
unsigned gFound = 0, gMissing = 0, gUnknown = 0, gListed = 0;

std::string lower(const std::string &s)
{
    std::string r(s);
    for (char &c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

bool listDir(const std::string &dir, Dir &d)
{
#ifdef __vita__
    const SceUID fd = sceIoDopen(dir.c_str());
    if (fd < 0) return false;
    SceIoDirent e;
    for (;;) {
        std::memset(&e, 0, sizeof(e));
        const int r = sceIoDread(fd, &e);
        if (r < 0) { sceIoDclose(fd); return false; }
        if (r == 0) break;
        if (SCE_S_ISREG(e.d_stat.st_mode)) d.files.insert(lower(e.d_name));
    }
    sceIoDclose(fd);
#else
    DIR *dp = opendir(dir.c_str());
    if (!dp) return false;
    while (dirent *e = readdir(dp)) {
        struct stat st;
        if (stat((dir + e->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode)) d.files.insert(lower(e->d_name));
    }
    closedir(dp);
#endif
    return true;
}

/* path -> folder as given (trailing '/'), its lower-cased key and the lower-cased file name, when the
 * path is under an asset root. */
bool split(const char *path, std::string &dirAsGiven, std::string &key, std::string &name)
{
    const std::string p(path ? path : "");
    bool under = false;
    for (const std::string &r : gRoots)
        if (p.size() > r.size() && lower(p.substr(0, r.size())) == lower(r)) { under = true; break; }
    if (!under || p.find('\\') != std::string::npos || p.find("//") != std::string::npos) return false;
    const size_t slash = p.rfind('/');
    if (slash == std::string::npos || slash + 1 >= p.size()) return false;
    dirAsGiven = p.substr(0, slash + 1);
    name = lower(p.substr(slash + 1));
    /* "." / ".." parts would need path normalisation: leave them to the real filesystem. */
    size_t i = 0;
    while (i < dirAsGiven.size()) {
        size_t j = dirAsGiven.find('/', i);
        if (j == std::string::npos) j = dirAsGiven.size();
        const std::string part = dirAsGiven.substr(i, j - i);
        if (part == "." || part == "..") return false;
        i = j + 1;
    }
    if (name == "." || name == "..") return false;
    key = lower(dirAsGiven);
    return true;
}
} // namespace

extern "C" void vitaFsIndexSetRoots(const char *const *roots, int n)
{
    pthread_mutex_lock(&gLock);
    gRoots.clear();
    gDirs.clear();
    for (int i = 0; i < n; ++i) gRoots.emplace_back(roots[i]);
    pthread_mutex_unlock(&gLock);
}

extern "C" int vitaFsIndexQuery(const char *path)
{
    pthread_mutex_lock(&gLock);
#ifdef __vita__
    if (gRoots.empty())
        for (const char *r : { "Graphics/", "Audio/", "Data/", "Fonts/" }) gRoots.emplace_back(std::string(VITA_GAME_ROOT) + r);
#endif
    std::string dir, key, name;
    int r = -1;
    if (split(path, dir, key, name)) {
        auto it = gDirs.find(key);
        if (it == gDirs.end()) {
            Dir d;
            d.ok = listDir(dir, d);   /* a folder that cannot be listed stays unknown (-1) */
            if (d.ok) ++gListed;
            it = gDirs.emplace(key, std::move(d)).first;
        }
        if (it->second.ok) r = it->second.files.count(name) ? 1 : 0;
    }
    if (r == 1) ++gFound; else if (r == 0) ++gMissing; else ++gUnknown;
    pthread_mutex_unlock(&gLock);
    return r;
}

extern "C" void vitaFsIndexStats(unsigned *found, unsigned *missing, unsigned *unknown, unsigned *dirs)
{
    pthread_mutex_lock(&gLock);
    *found = gFound; *missing = gMissing; *unknown = gUnknown; *dirs = gListed;
    pthread_mutex_unlock(&gLock);
}
