// Host test for src/vita-fs-index.cpp (MKXP_VITA_FS_INDEX): a temporary game tree; every query is
// checked against an independent case-insensitive oracle (the card's exFAT): 1 only for an existing
// regular file, 0 only when no regular file of that name exists (missing, or a folder), -1 outside
// the asset roots, for "."/".." parts and for folders that cannot be listed. Also: one listing per
// folder, files created after the listing are not seen (asset folders are read-only by design).
#include "../../../src/vita-fs-index.h"
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL " __VA_ARGS__); std::printf("\n"); } } while (0)
static void touch(const std::string &p) { FILE *f = std::fopen(p.c_str(), "w"); if (f) std::fclose(f); }
static int oracle(const std::string &path)
{
    const size_t s = path.rfind('/');
    const std::string dir = path.substr(0, s + 1), name = path.substr(s + 1);
    DIR *d = opendir(dir.c_str());
    if (!d) return -1;
    int r = 0;
    while (dirent *e = readdir(d)) {
        struct stat st;
        if (strcasecmp(e->d_name, name.c_str()) == 0 && stat((dir + e->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode)) r = 1;
    }
    closedir(d);
    return r;
}
int main()
{
    char tmpl[] = "/tmp/fsidxXXXXXX";
    const std::string root = std::string(mkdtemp(tmpl)) + "/game/";
    for (const char *d : { "", "Graphics/", "Graphics/Characters/", "Graphics/Battlers/", "Graphics/Battlers/sub.png/", "Audio/", "Audio/SE/", "Data/" })
        mkdir((root + d).c_str(), 0777);
    for (const char *f : { "Graphics/Characters/$Brad.png", "Graphics/Characters/people.PNG", "Graphics/Battlers/cosmic1.png",
                           "Audio/SE/Echo.ogg", "Audio/SE/crow.OGG", "Audio/SE/noext", "Data/Map125.rvdata2", "Save01.rvdata2" })
        touch(root + f);
    const std::string g = root + "Graphics/", a = root + "Audio/", dta = root + "Data/";
    const char *roots[] = { g.c_str(), a.c_str(), dta.c_str() };
    vitaFsIndexSetRoots(roots, 3);
    std::vector<std::string> q = {
        "Graphics/Characters/$Brad", "Graphics/Characters/$Brad.png", "Graphics/Characters/$brad.PNG", "Graphics/Characters/people.png",
        "Graphics/Characters/People", "Graphics/Battlers/cosmic1", "Graphics/Battlers/cosmic1.png", "Graphics/Battlers/sub.png",
        "Graphics/Battlers/none.png", "Audio/SE/Echo", "Audio/SE/Echo.ogg", "Audio/SE/echo.wav", "Audio/SE/crow.ogg", "Audio/SE/noext",
        "Audio/SE/noext.ogg", "Data/Map125.rvdata2", "Data/map125.RVDATA2", "Data/Map999.rvdata2", "graphics/characters/$BRAD.png" };
    unsigned f0, m0, u0, dirs0;
    for (const std::string &rel : q) {
        const std::string p = root + rel;
        const int r = vitaFsIndexQuery(p.c_str()), o = oracle(p);
        // graphics/characters (lower-case folder) does not exist on the case-sensitive host: the index
        // keys folders case-insensitively (as the card), so it still answers from the listing.
        if (rel.compare(0, 9, "graphics/") == 0) CHECK(r == 1, "case-insensitive folder %s -> %d", rel.c_str(), r);
        else CHECK(r == o, "%s -> %d, oracle %d", rel.c_str(), r, o);
    }
    vitaFsIndexStats(&f0, &m0, &u0, &dirs0);
    CHECK(dirs0 == 4, "folders listed once each (graphics/characters = Graphics/Characters): %u", dirs0);
    // outside the roots, dot parts, unlistable folder: -1
    for (const std::string &p : { root + "Save01.rvdata2", root + "cache/img.lz4", root + "Graphics/./Characters/$Brad.png",
                                  root + "Graphics/Characters/../Battlers/cosmic1.png", root + "Graphics/Pictures/x.png",
                                  root + "Graphics//Characters/$Brad.png", root + "Graphics/" })
        CHECK(vitaFsIndexQuery(p.c_str()) == -1, "unknown expected for %s", p.c_str());
    // the listing is kept: a file added later is not seen (read-only folders), no new listing
    touch(root + "Audio/SE/late.ogg");
    CHECK(vitaFsIndexQuery((root + "Audio/SE/late.ogg").c_str()) == 0, "listing reused");
    unsigned f1, m1, u1, dirs1;
    vitaFsIndexStats(&f1, &m1, &u1, &dirs1);
    CHECK(dirs1 == dirs0, "no relisting %u %u", dirs1, dirs0);
    CHECK(f1 > 0 && m1 > 0 && u1 == 7, "counters %u %u %u", f1, m1, u1);
    std::printf(fails ? "FAIL %d\n" : "PASS fs-index\n", fails);
    std::system(("rm -rf " + root.substr(0, root.size() - 6)).c_str());
    return fails ? 1 : 0;
}
