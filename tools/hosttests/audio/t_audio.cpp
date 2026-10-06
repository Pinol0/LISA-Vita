#include "filesystem.h"
#include "aldatasource.h"
#include "exception.h"
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include "al-util.h"
bool vitaDecodeAudioAll(SDL_RWops &ops, const char *ext, std::vector<uint8_t> &data, int &ss, int &ch, int &rate, std::string &err);
static long gUploaded = 0; static int gFmt = 0, gRate = 0;
static std::vector<uint8_t> gData;
extern "C" void alBufferData(ALuint, ALenum fmt, const ALvoid *p, ALsizei size, ALsizei freq) { gUploaded += size; gFmt = fmt; gRate = freq; gData.insert(gData.end(), (const uint8_t*)p, (const uint8_t*)p + size); }
static std::vector<uint8_t> readFile(const char *n) { std::vector<uint8_t> v; FILE *f = fopen(n, "rb"); if (!f) return v; int c; while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c); fclose(f); return v; }
int fails = 0;
#define CHECK(c, m) do { bool ok = (c); printf("%s  %s\n", ok ? "PASS" : "FAIL", m); if (!ok) ++fails; } while (0)
struct DecodeHandler : FileSystem::OpenHandler {
    std::vector<uint8_t> pcm; int ss = 0, ch = 0, rate = 0; std::string err, ext; bool ok = false;
    bool tryRead(SDL_RWops &ops, const char *e) { ext = e; ok = vitaDecodeAudioAll(ops, e, pcm, ss, ch, rate, err); return ok; }
};
struct StreamHandler : FileSystem::OpenHandler {
    ALDataSource *src = nullptr; std::string err;
    bool tryRead(SDL_RWops &ops, const char *e) {
        char sig[5] = {0}; SDL_RWread(&ops, sig, 1, 4); SDL_RWseek(&ops, 0, RW_SEEK_SET);
        try { src = !strcmp(sig, "OggS") ? createVorbisSource(ops, true) : createSDLSource(ops, e, 32768, true); return true; }
        catch (const Exception &x) { err = x.msg; return false; }
    }
};
int main() {
    FileSystem fs("", false);
    { DecodeHandler h; fs.openRead(h, "Audio/SE/test_ogg"); CHECK(h.ok && h.ext == "ogg" && h.ss == 2 && h.ch >= 1 && h.rate > 0 && !h.pcm.empty(), "SE ogg: extension supplemented (.ogg), decoded to s16 PCM");
      printf("      ogg: ch=%d rate=%d bytes=%zu\n", h.ch, h.rate, h.pcm.size()); }
    { DecodeHandler h; fs.openRead(h, "Audio/SE/test_wav"); CHECK(h.ok && h.ext == "wav" && h.ch >= 1 && h.rate > 0 && !h.pcm.empty(), "SE wav: .wav supplemented and decoded");
      printf("      wav: ch=%d rate=%d ss=%d bytes=%zu\n", h.ch, h.rate, h.ss, h.pcm.size()); }
    { DecodeHandler h; fs.openRead(h, "Audio/BGM/test_mp3"); CHECK(!h.ok && h.err.find("not supported on Vita") != std::string::npos, "mp3: file found, explicit 'not supported on Vita' error, no NoFileError");
      printf("      mp3 err: %s\n", h.err.c_str()); }
    { bool thrown = false; try { DecodeHandler h; fs.openRead(h, "Audio/SE/does_not_exist"); } catch (const Exception &e) { thrown = e.type == Exception::NoFileError; }
      CHECK(thrown, "missing file: NoFileError (-> Errno::ENOENT), as upstream"); }
    { StreamHandler h; fs.openRead(h, "Audio/SE/test_wav"); CHECK(h.src != nullptr, "stream wav source created");
      if (h.src) { int n = 0; ALDataSource::Status st; long total = 0; do { gUploaded = 0; st = h.src->fillBuffer(AL::Buffer::ID(1)); total += gUploaded; ++n; } while (st == ALDataSource::NoError && n < 1000);
        CHECK(st == ALDataSource::WrapAround && total > 0 && gRate == h.src->sampleRate(), "looped wav stream: fills buffers then wraps around");
        h.src->seekToOffset(0.5); gUploaded = 0; h.src->fillBuffer(AL::Buffer::ID(1)); CHECK(gUploaded > 0, "seek then fill");
        delete h.src; } }
    { StreamHandler h; fs.openRead(h, "Audio/SE/test_ogg"); CHECK(h.src != nullptr, "stream ogg source through upstream VorbisSource with a copied RWops");
      if (h.src) { gUploaded = 0; h.src->fillBuffer(AL::Buffer::ID(1)); CHECK(gUploaded > 0, "vorbis fillBuffer reads through the copied RWops (close must not free the copy)"); delete h.src; } }
    const struct { const char *n; int ch, rate, ss; } W[] = { {"t16s",2,44100,2}, {"t24m",1,22050,2}, {"t24ext",2,44100,2}, {"t8m",1,11025,1}, {"t32",2,24000,2}, {"tf32",1,44100,2} };
    for (const auto &w : W) {
        std::string path = std::string("Audio/SE/") + w.n; std::vector<uint8_t> exp = readFile((std::string(w.n) + ".exp").c_str());
        char m[160];
        { DecodeHandler h; fs.openRead(h, path.c_str());
          snprintf(m, sizeof m, "%s whole decode: ch/rate/ss and samples match reference (%zu bytes)", w.n, exp.size());
          CHECK(h.ok && h.ch == w.ch && h.rate == w.rate && h.ss == w.ss && h.pcm == exp, m); if (!h.ok) printf("      err: %s\n", h.err.c_str()); }
        { StreamHandler h; fs.openRead(h, path.c_str()); snprintf(m, sizeof m, "%s stream source created", w.n); CHECK(h.src, m);
          if (!h.src) { printf("      err: %s\n", h.err.c_str()); continue; }
          gData.clear(); ALDataSource::Status st; int n = 0;
          do { st = h.src->fillBuffer(AL::Buffer::ID(1)); ++n; } while (st == ALDataSource::NoError && n < 100000);
          snprintf(m, sizeof m, "%s looped stream: %d buffers, WrapAround, concatenated PCM == reference, AL format", w.n, n);
          CHECK(st == ALDataSource::WrapAround && gData == exp && gRate == w.rate && gFmt == chooseALFormat(w.ss, w.ch), m);
          gData.clear(); h.src->fillBuffer(AL::Buffer::ID(1)); CHECK(gData.size() > 0 && std::equal(gData.begin(), gData.end(), exp.begin()), "  after wrap: restarts from frame 0");
          h.src->seekToOffset(0.25); gData.clear(); h.src->fillBuffer(AL::Buffer::ID(1));
          size_t off = (size_t)(0.25 * w.rate) * w.ch * w.ss;
          CHECK(off < exp.size() && gData.size() > 0 && std::equal(gData.begin(), gData.end(), exp.begin() + off), "  seekToOffset(0.25) lands on the right frame");
          delete h.src; }
        { StreamHandler h; h.src = nullptr; SDL_RWops ops; fs.openReadRaw(ops, (path + ".wav").c_str(), false);
          ALDataSource *s = createSDLSource(ops, "wav", 32768, false); gData.clear(); ALDataSource::Status st; int n = 0;
          do { st = s->fillBuffer(AL::Buffer::ID(1)); ++n; } while (st == ALDataSource::NoError && n < 100000);
          CHECK(st == ALDataSource::EndOfStream && gData == exp, "  non-looped stream ends with EndOfStream"); delete s; }
    }
    { DecodeHandler h; fs.openRead(h, "Audio/SE/tadpcm"); CHECK(!h.ok && h.err.find("not supported on Vita") != std::string::npos, "ADPCM wav: explicit unsupported error"); printf("      err: %s\n", h.err.c_str()); }
    { StreamHandler h; fs.openRead(h, "Audio/SE/tadpcm"); CHECK(!h.src && h.err.find("not supported") != std::string::npos, "ADPCM wav stream: Exception, no source"); }
    printf("fails=%d\n", fails); return fails;
}
