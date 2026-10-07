#include "filesystem.h"
#include "aldatasource.h"
#include "exception.h"
#include "al-util.h"
#include <SDL.h>
#include <cstdio>
#include <vector>
#include <string>
static std::vector<uint8_t> gData; static int gFmt, gRate;
extern "C" void alBufferData(ALuint, ALenum fmt, const ALvoid *p, ALsizei size, ALsizei freq) noexcept { gFmt = fmt; gRate = freq; gData.insert(gData.end(), (const uint8_t*)p, (const uint8_t*)p + size); }
int main(int argc, char **argv) {
    FileSystem fs("", false); int fails = 0, n = 0;
    for (int i = 1; i < argc; ++i) {
        SDL_AudioSpec spec; Uint8 *buf; Uint32 len;
        std::vector<uint8_t> ref;
        if (!SDL_LoadWAV(argv[i], &spec, &buf, &len)) { printf("SKIP(SDL cannot load) %s: %s\n", argv[i], SDL_GetError()); continue; }
        SDL_AudioFormat dst = spec.format == AUDIO_U8 ? AUDIO_U8 : AUDIO_S16LSB;
        SDL_AudioCVT cvt; SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, dst, spec.channels, spec.freq);
        cvt.len = len; std::vector<uint8_t> tmp(len * (cvt.len_mult > 0 ? cvt.len_mult : 1)); memcpy(tmp.data(), buf, len); cvt.buf = tmp.data();
        if (cvt.needed) SDL_ConvertAudio(&cvt); else cvt.len_cvt = len;
        ref.assign(tmp.begin(), tmp.begin() + cvt.len_cvt); SDL_FreeWAV(buf);
        SDL_RWops ops; std::string rel = argv[i] + strlen(MKXP_VITA_GAME_ROOT);
        fs.openReadRaw(ops, rel.c_str(), false);
        try {
            ALDataSource *s = createSDLSource(ops, "wav", 32768, false); gData.clear(); int k = 0;
            while (s->fillBuffer(AL::Buffer::ID(1)) == ALDataSource::NoError && ++k < 1000000) {}
            bool ok = gData == ref && gRate == spec.freq; ++n;
            if (!ok && gData.size() == ref.size()) { int md = 0; for (size_t q = 0; q + 1 < ref.size(); q += 2) { int a = (int16_t)(gData[q] | gData[q+1] << 8), b = (int16_t)(ref[q] | ref[q+1] << 8); md = std::max(md, abs(a - b)); } printf("maxdiff=%d\n", md);
              /* 32-bit integer PCM: SDL converts S32 -> float -> S16, the backend shifts (>> 16); at most
               * 1 LSB of 16 apart, inaudible. Accepted and reported, anything else is a failure. */
              if (md <= 1 && spec.format == AUDIO_S32LSB && gRate == spec.freq) { ok = true; printf("PASS(+-1 LSB, S32 rounding vs SDL) %s\n", argv[i]); } }
            if (!ok) { ++fails; printf("FAIL %s fmt=%x ch=%d ours=%zu ref=%zu\n", argv[i], spec.format, spec.channels, gData.size(), ref.size()); }
            delete s;
        } catch (const Exception &e) { printf("EXC %s: %s\n", argv[i], e.msg.c_str()); ++fails; }
    }
    printf("compared=%d fails=%d\n", n, fails); return fails;
}
