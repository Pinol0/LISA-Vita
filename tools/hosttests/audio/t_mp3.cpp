// MP3 in the Vita audio backend (MKXP_VITA_MP3): src/vita-audio-backend.cpp + dr_mp3 as built.
//  - whole decode (SoundEmitter path) and stream (ALStream path) give the same PCM;
//  - that PCM matches ffmpeg's own decoder (independent) after aligning the encoder delay;
//  - looped stream wraps to frame 0, seekToOffset lands on the frame, non-looped ends;
//  - with MP3_DIR: every .mp3 under it decodes, as long as ffmpeg's decode of it (+-2 MP3 frames).
// Run from an audio root that has Audio/BGM/test_mp3.mp3 (setup_env.sh).
#include "filesystem.h"
#include "aldatasource.h"
#include "exception.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include "al-util.h"
bool vitaDecodeAudioAll(SDL_RWops &ops, const char *ext, std::vector<uint8_t> &data, int &ss, int &ch, int &rate, std::string &err);
static int gFmt = 0, gRate = 0;
static std::vector<uint8_t> gData;
extern "C" void alBufferData(ALuint, ALenum fmt, const ALvoid *p, ALsizei size, ALsizei freq)
{ gFmt = fmt; gRate = freq; gData.insert(gData.end(), (const uint8_t *)p, (const uint8_t *)p + size); }
static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("%s  ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++fails; } while (0)
struct DecodeHandler : FileSystem::OpenHandler {
    std::vector<uint8_t> pcm; int ss = 0, ch = 0, rate = 0; std::string err; bool ok = false;
    bool tryRead(SDL_RWops &ops, const char *e) { ok = vitaDecodeAudioAll(ops, e, pcm, ss, ch, rate, err); return ok; }
};
struct StreamHandler : FileSystem::OpenHandler {
    ALDataSource *src = nullptr; std::string err; bool looped = true;
    bool tryRead(SDL_RWops &ops, const char *e) {
        try { src = createSDLSource(ops, e, 32768, looped); return true; } catch (const Exception &x) { err = x.msg; return false; }
    }
};
static std::vector<int16_t> ffmpegDecode(const std::string &path, int ch)
{
    std::string cmd = "ffmpeg -loglevel error -i '" + path + "' -f s16le -ac " + std::to_string(ch) + " -";
    std::vector<int16_t> out;
    if (FILE *p = popen(cmd.c_str(), "r")) {
        int16_t b[4096]; size_t n;
        while ((n = fread(b, 2, 4096, p)) > 0) out.insert(out.end(), b, b + n);
        pclose(p);
    }
    return out;
}
static double ffprobeSeconds(const std::string &path)
{
    std::string cmd = "ffprobe -v error -show_entries format=duration -of csv=p=0 '" + path + "'";
    double d = -1;
    if (FILE *p = popen(cmd.c_str(), "r")) { if (fscanf(p, "%lf", &d) != 1) d = -1; pclose(p); }
    return d;
}
int main()
{
    FileSystem fs("", false);
    DecodeHandler h;
    fs.openRead(h, "Audio/BGM/test_mp3");
    CHECK(h.ok && h.ss == 2 && h.ch == 2 && h.rate == 44100 && !h.pcm.empty(), "whole decode: s16 stereo 44100 (%zu bytes) %s", h.pcm.size(), h.err.c_str());
    const std::vector<int16_t> mine((const int16_t *)h.pcm.data(), (const int16_t *)(h.pcm.data() + h.pcm.size()));
    /* against ffmpeg: best alignment of the first channel within +-3000 frames, then the RMS difference */
    const std::vector<int16_t> ref = ffmpegDecode(std::string(getenv("PWD") ? getenv("PWD") : ".") + "/Audio/BGM/test_mp3.mp3", 2);
    long best = 0; double bestErr = 1e300;
    const long span = 20000;
    for (long off = -3000; off <= 3000; ++off) {
        double e = 0; long n = 0;
        for (long i = 4000; i < 4000 + span; ++i) {
            const long j = i + off;
            if (j < 0 || (size_t)(2 * j) >= ref.size() || (size_t)(2 * i) >= mine.size()) continue;
            const double d = mine[2 * i] - ref[2 * j]; e += d * d; ++n;
        }
        if (n && e / n < bestErr) { bestErr = e / n; best = off; }
    }
    double sig = 0; for (long i = 4000; i < 4000 + span && (size_t)(2 * i) < mine.size(); ++i) sig += (double)mine[2 * i] * mine[2 * i];
    sig /= span;
    CHECK(!ref.empty() && std::sqrt(bestErr) < 0.01 * std::sqrt(sig), "matches ffmpeg's decoder: RMS diff %.2f of signal RMS %.0f at offset %ld frames", std::sqrt(bestErr), std::sqrt(sig), best);
    const long frames = (long)mine.size() / 2, refFrames = (long)ref.size() / 2;
    CHECK(std::labs(frames - refFrames) <= 2304, "length %ld frames, ffmpeg %ld", frames, refFrames);

    /* stream = whole decode; loop; seek; end */
    { StreamHandler s; fs.openRead(s, "Audio/BGM/test_mp3");
      CHECK(s.src != nullptr, "stream source created %s", s.err.c_str());
      if (s.src) {
          gData.clear(); ALDataSource::Status st; int n = 0;
          do { st = s.src->fillBuffer(AL::Buffer::ID(1)); ++n; } while (st == ALDataSource::NoError && n < 100000);
          CHECK(st == ALDataSource::WrapAround && gData == h.pcm && gRate == 44100 && gFmt == chooseALFormat(2, 2), "looped stream: %d buffers, WrapAround, PCM == whole decode", n);
          gData.clear(); s.src->fillBuffer(AL::Buffer::ID(1));
          CHECK(!gData.empty() && std::equal(gData.begin(), gData.end(), h.pcm.begin()), "after the wrap: from frame 0");
          s.src->seekToOffset(0.5); gData.clear(); s.src->fillBuffer(AL::Buffer::ID(1));
          const size_t off = (size_t)(0.5 * 44100) * 4;
          long at = -1;
          for (size_t o = 0; at < 0 && o + gData.size() <= h.pcm.size(); o += 4)
              if (!memcmp(gData.data(), h.pcm.data() + o, gData.size())) at = (long)(o / 4);
          CHECK(off < h.pcm.size() && !gData.empty() && std::equal(gData.begin(), gData.end(), h.pcm.begin() + off), "seekToOffset(0.5) lands on frame 22050 (found at frame %ld)", at);
          delete s.src; } }
    { StreamHandler s; s.looped = false; fs.openRead(s, "Audio/BGM/test_mp3");
      if (s.src) { gData.clear(); ALDataSource::Status st; int n = 0;
          do { st = s.src->fillBuffer(AL::Buffer::ID(1)); ++n; } while (st == ALDataSource::NoError && n < 100000);
          CHECK(st == ALDataSource::EndOfStream && gData == h.pcm, "non-looped stream ends with EndOfStream, same PCM"); delete s.src; } }

    /* the game's files */
    if (const char *dir = getenv("MP3_DIR")) {
        int files = 0, bad = 0;
        std::vector<std::string> todo = { dir };
        while (!todo.empty()) {
            const std::string d = todo.back(); todo.pop_back();
            DIR *dp = opendir(d.c_str()); if (!dp) continue;
            while (dirent *e = readdir(dp)) {
                const std::string name = e->d_name, p = d + "/" + name;
                if (name == "." || name == "..") continue;
                if (e->d_type == DT_DIR) { todo.push_back(p); continue; }
                if (name.size() < 4 || strcasecmp(name.c_str() + name.size() - 4, ".mp3")) continue;
                ++files;
                FILE *f = fopen(p.c_str(), "rb");
                if (!f) { ++bad; continue; }
                SDL_RWops &ops = *SDL_RWFromFP(f, SDL_TRUE);   /* closed (and freed) by the backend */
                std::vector<uint8_t> pcm; int ss = 0, ch = 0, rate = 0; std::string err;
                const bool ok = vitaDecodeAudioAll(ops, "mp3", pcm, ss, ch, rate, err);
                /* ffprobe only estimates the length of an MP3 without a Xing/Info header: ffmpeg's decode is the reference */
                const long frames = ok ? (long)(pcm.size() / (ss * ch)) : -1;
                const long want = ok ? (long)(ffmpegDecode(p, ch).size() / ch) : -2;
                if (!ok || std::labs(frames - want) > 2304) { ++bad; printf("  FAIL %s: ok=%d %ld frames vs ffmpeg %ld (est. %.2f s) %s\n", name.c_str(), ok, frames, want, ffprobeSeconds(p), err.c_str()); }
            }
            closedir(dp);
        }
        CHECK(files > 0 && bad == 0, "%d game MP3 files decoded, length = ffmpeg's decode (+-2 MP3 frames), %d wrong", files, bad);
    }
    printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
