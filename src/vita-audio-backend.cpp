/*
 * Vita backend pieces for the upstream mkxp-z audio module (src/audio/*, binding/audio-binding.cpp),
 * built with MKXP_VITA_AUDIO. Upstream code is used unchanged except for small MKXP_VITA_AUDIO
 * patches (no SDL_sound, no fluidsynth); this file provides what the desktop build gets elsewhere:
 *
 *  - SyncPoint (eventthread.cpp, copied verbatim): secondary audio threads pass through it. Nothing
 *    on the Vita halts them (no focus loss / F12 reset), so passSecondarySync never blocks.
 *  - FileSystem::openRead/openReadRaw (filesystem.cpp is PhysFS-based): paths are relative to the
 *    game root; the RGSS extension supplementing tries the name as given, then .ogg .wav .mp3 .mid
 *    .midi (the FAT/exFAT filesystem is case-insensitive). The SDL_RWops handed to handlers are
 *    plain structs over FILE* whose close() does not free the struct, because handlers copy them
 *    by value (as with PhysFS upstream).
 *  - createSDLSource (upstream: SDL_sound): WAV (RIFF/WAVE, PCM 8/16/24/32-bit, float 32) streamed
 *    from the file in maxBufSize chunks like the other ALDataSources, so a BGS/ME never has to fit
 *    in RAM; any other format throws a clear "unsupported on Vita" error. mp3 is not decoded (no
 *    decoder in the toolchain).
 *  - vitaDecodeAudioAll: whole-file decode for SoundEmitter (Ogg Vorbis via vorbisfile, WAV).
 *  - Out of memory while opening/decoding becomes an mkxp Exception / decode error (upstream
 *    ALStream and SoundEmitter log it and skip the sound) instead of std::terminate.
 */
#include "eventthread.h"
#include "filesystem.h"
#include "aldatasource.h"
#include "exception.h"
#include "vita_paths.h"
#ifdef MKXP_VITA_FS_INDEX
#include "vita-fs-index.h"
#endif

#include <SDL.h>
#include <malloc.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <new>
#include <cstring>
#include <string>
#include <vector>

/* ---------------------------------------------------------------- SyncPoint (upstream) */

void SyncPoint::haltThreads()
{
    if (mainSync.locked)
        return;
    reply.lock();
    mainSync.lock();
    reply.waitForUnlock();
    secondSync.lock();
}

void SyncPoint::resumeThreads()
{
    if (!mainSync.locked)
        return;
    mainSync.unlock(false);
    secondSync.unlock(true);
}

bool SyncPoint::mainSyncLocked()
{
    return mainSync.locked;
}

void SyncPoint::waitMainSync()
{
    reply.unlock(false);
    mainSync.waitForUnlock();
}

void SyncPoint::passSecondarySync()
{
    if (!secondSync.locked)
        return;
    secondSync.waitForUnlock();
}

SyncPoint::Util::Util()
{
    mut = SDL_CreateMutex();
    cond = SDL_CreateCond();
}

SyncPoint::Util::~Util()
{
    SDL_DestroyCond(cond);
    SDL_DestroyMutex(mut);
}

void SyncPoint::Util::lock()
{
    locked.set();
}

void SyncPoint::Util::unlock(bool multi)
{
    locked.clear();
    if (multi)
        SDL_CondBroadcast(cond);
    else
        SDL_CondSignal(cond);
}

void SyncPoint::Util::waitForUnlock()
{
    SDL_LockMutex(mut);
    while (locked)
        SDL_CondWait(cond, mut);
    SDL_UnlockMutex(mut);
}

/* ---------------------------------------------------------------- FILE* SDL_RWops, copyable */

static Sint64 SDLCALL vitaRwSize(SDL_RWops *ctx)
{
    FILE *f = (FILE *)ctx->hidden.unknown.data1;
    const long cur = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    const long end = std::ftell(f);
    std::fseek(f, cur, SEEK_SET);
    return end;
}

static Sint64 SDLCALL vitaRwSeek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    FILE *f = (FILE *)ctx->hidden.unknown.data1;
    const int w = whence == RW_SEEK_SET ? SEEK_SET : whence == RW_SEEK_CUR ? SEEK_CUR : SEEK_END;
    if (std::fseek(f, (long)offset, w) != 0)
        return -1;
    return std::ftell(f);
}

static size_t SDLCALL vitaRwRead(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    return std::fread(ptr, size, maxnum, (FILE *)ctx->hidden.unknown.data1);
}

static size_t SDLCALL vitaRwWrite(SDL_RWops *, const void *, size_t, size_t)
{
    return 0;
}

static int SDLCALL vitaRwClose(SDL_RWops *ctx)
{
    FILE *f = (FILE *)ctx->hidden.unknown.data1;
    ctx->hidden.unknown.data1 = nullptr;
    return f ? std::fclose(f) : 0;   /* the struct itself is not freed: it may be a copy */
}

static bool vitaOpenRw(const std::string &path, SDL_RWops &ops)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
#ifdef MKXP_VITA_BIG_READS
    std::setvbuf(f, nullptr, _IOFBF, 64 * 1024);   /* newlib default: 1 KiB per read on the Vita */
#endif
    std::memset(&ops, 0, sizeof(ops));
    ops.size = vitaRwSize;
    ops.seek = vitaRwSeek;
    ops.read = vitaRwRead;
    ops.write = vitaRwWrite;
    ops.close = vitaRwClose;
    ops.type = SDL_RWOPS_UNKNOWN;
    ops.hidden.unknown.data1 = f;
    return true;
}

/* ---------------------------------------------------------------- FileSystem (Vita) */

FileSystem::FileSystem(const char *, bool)
    : p(nullptr)
{}

FileSystem::~FileSystem() {}

static std::string vitaGamePath(const char *filename)
{
    std::string name = filename ? filename : "";
    for (char &c : name)
        if (c == '\\')
            c = '/';
    if (name.compare(0, 4, "ux0:") == 0 || name.compare(0, 5, "app0:") == 0)
        return name;
    return std::string(VITA_GAME_ROOT) + name;
}

void FileSystem::openRead(OpenHandler &handler, const char *filename)
{
    static const char *const exts[] = { "", ".ogg", ".wav", ".mp3", ".mid", ".midi" };
    const std::string base = vitaGamePath(filename);
    int matchCount = 0;
    for (const char *ext : exts) {
#ifdef MKXP_VITA_FS_INDEX
        /* Candidates the asset-folder index knows are missing would fail to open (vita-fs-index.cpp). */
        if (vitaFsIndexQuery((base + ext).c_str()) == 0)
            continue;
#endif
        SDL_RWops ops;
        if (!vitaOpenRw(base + ext, ops))
            continue;
        ++matchCount;
        /* The handler owns ops from here (closes it, or copies it and closes later). */
        const char *e = *ext ? ext + 1 : std::strrchr(base.c_str(), '.') ? std::strrchr(base.c_str(), '.') + 1 : "";
        if (handler.tryRead(ops, e))
            return;
    }
    /* As upstream: NoFileError only when nothing matched; a file that exists but could not be parsed
     * returns normally and the caller reports the handler's error. */
    if (matchCount == 0)
        throw Exception(Exception::NoFileError, "%s", filename);
}

void FileSystem::openReadRaw(SDL_RWops &ops, const char *filename, bool)
{
    if (!vitaOpenRw(vitaGamePath(filename), ops))
        throw Exception(Exception::NoFileError, "%s", filename);
}

/* Out-of-memory message with the newlib heap state (error path only). */
static std::string vitaOomMessage(const char *what)
{
    const struct mallinfo mi = mallinfo();
    char b[160];
    std::snprintf(b, sizeof(b), "out of memory %s (heap in use %u KiB, arena %u KiB)", what,
                  (unsigned)(mi.uordblks / 1024), (unsigned)(mi.arena / 1024));
    return b;
}

/* ---------------------------------------------------------------- WAV (RIFF) parsing + streaming */

namespace {
/* Parsed "fmt " + "data" chunks of a RIFF/WAVE file. */
struct VitaWavInfo
{
    int format = 0;        /* 1 PCM, 3 IEEE float (WAVE_FORMAT_EXTENSIBLE resolved) */
    int channels = 0;
    int rate = 0;
    int bits = 0;
    int blockAlign = 0;
    Sint64 dataStart = 0;
    Sint64 dataSize = 0;
};

uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Reads the chunk headers from the current position (start of the file). */
bool vitaParseWav(SDL_RWops &ops, VitaWavInfo &wi, std::string &err)
{
    uint8_t hdr[12];
    if (SDL_RWread(&ops, hdr, 1, 12) != 12 || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
        err = "WAV: not a RIFF/WAVE file";
        return false;
    }
    bool haveFmt = false;
    for (;;) {
        uint8_t ch[8];
        if (SDL_RWread(&ops, ch, 1, 8) != 8)
            break;
        const uint32_t size = rd32le(ch + 4);
        const Sint64 body = SDL_RWtell(&ops);
        if (!std::memcmp(ch, "fmt ", 4)) {
            uint8_t f[40] = { 0 };
            const size_t n = std::min<uint32_t>(size, sizeof(f));
            if (SDL_RWread(&ops, f, 1, n) != n) { err = "WAV: truncated fmt chunk"; return false; }
            wi.format = rd16le(f);
            wi.channels = rd16le(f + 2);
            wi.rate = (int)rd32le(f + 4);
            wi.blockAlign = rd16le(f + 12);
            wi.bits = rd16le(f + 14);
            if (wi.format == 0xFFFE && n >= 26)   /* WAVE_FORMAT_EXTENSIBLE: sub-format GUID */
                wi.format = rd16le(f + 24);
            haveFmt = true;
        } else if (!std::memcmp(ch, "data", 4)) {
            wi.dataStart = body;
            wi.dataSize = size;
            const Sint64 total = SDL_RWsize(&ops);
            if (total > 0 && wi.dataStart + wi.dataSize > total)
                wi.dataSize = total - wi.dataStart;   /* tolerate a wrong size in the header */
            break;
        }
        SDL_RWseek(&ops, body + size + (size & 1), RW_SEEK_SET);
    }
    if (!haveFmt || !wi.dataStart) { err = "WAV: missing fmt or data chunk"; return false; }
    if (wi.channels < 1 || wi.channels > 2) { err = "WAV: more than 2 channels"; return false; }
    const bool pcmOk = wi.format == 1 && (wi.bits == 8 || wi.bits == 16 || wi.bits == 24 || wi.bits == 32);
    const bool floatOk = wi.format == 3 && wi.bits == 32;
    if (!pcmOk && !floatOk) {
        char b[96];
        std::snprintf(b, sizeof(b), "WAV: encoding %d/%d-bit not supported on Vita (PCM 8-32 bit, float 32)", wi.format, wi.bits);
        err = b;
        return false;
    }
    if (wi.blockAlign != wi.channels * wi.bits / 8 || wi.rate <= 0) { err = "WAV: inconsistent fmt chunk"; return false; }
    return true;
}

/* Output sample size: 8-bit stays unsigned 8 (AL MONO8/STEREO8), everything else becomes s16. */
int vitaWavOutSampleSize(const VitaWavInfo &wi) { return wi.bits == 8 ? 1 : 2; }

/* Converts nframes of file data to the output format; returns output bytes. */
size_t vitaWavConvert(const VitaWavInfo &wi, const uint8_t *in, size_t nframes, uint8_t *out)
{
    const size_t n = nframes * wi.channels;
    if (wi.bits == 8 || (wi.bits == 16 && wi.format == 1)) {
        std::memcpy(out, in, n * (wi.bits / 8));
        return n * (wi.bits / 8);
    }
    int16_t *o = reinterpret_cast<int16_t *>(out);
    for (size_t k = 0; k < n; ++k) {
        int32_t v;
        if (wi.format == 3) {
            float f;
            std::memcpy(&f, in + k * 4, 4);
            f = f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
            v = (int32_t)(f * 32767.0f);
        } else if (wi.bits == 24) {
            v = (int16_t)(in[k * 3 + 1] | in[k * 3 + 2] << 8);
        } else {   /* 32-bit PCM */
            v = (int16_t)(in[k * 4 + 2] | in[k * 4 + 3] << 8);
        }
        o[k] = (int16_t)v;
    }
    return n * 2;
}

struct VitaWavStreamSource : ALDataSource
{
    SDL_RWops src;            /* copy of the handler's ops, closed in the destructor */
    VitaWavInfo wi;
    Sint64 framePos = 0;
    Sint64 frameCount;
    bool looped;
    ALenum alFormat;
    size_t chunkFrames;
    std::vector<uint8_t> inBuf, outBuf;

    VitaWavStreamSource(SDL_RWops &ops, const VitaWavInfo &info, uint32_t maxBufSize, bool loop)
        : src(ops), wi(info), looped(loop)
    {
        frameCount = wi.dataSize / wi.blockAlign;
        const int outSS = vitaWavOutSampleSize(wi);
        alFormat = chooseALFormat(outSS, wi.channels);
        chunkFrames = std::max<size_t>(1024, (maxBufSize ? maxBufSize : 32768) / (outSS * wi.channels));
        inBuf.resize(chunkFrames * wi.blockAlign);
        outBuf.resize(chunkFrames * outSS * wi.channels);
        SDL_RWseek(&src, wi.dataStart, RW_SEEK_SET);
    }

    ~VitaWavStreamSource() { SDL_RWclose(&src); }

    Status fillBuffer(AL::Buffer::ID alBuffer)
    {
        Status st = NoError;
        if (framePos >= frameCount) {
            if (!looped)
                return EndOfStream;
            seekToOffset(0);
            st = WrapAround;
        }
        const size_t want = (size_t)std::min<Sint64>((Sint64)chunkFrames, frameCount - framePos);
        const size_t got = SDL_RWread(&src, inBuf.data(), wi.blockAlign, want);
        if (got == 0)
            return Error;
        const size_t bytes = vitaWavConvert(wi, inBuf.data(), got, outBuf.data());
        AL::Buffer::uploadData(alBuffer, alFormat, outBuf.data(), (ALsizei)bytes, wi.rate);
        framePos += got;
        if (framePos >= frameCount) {
            if (looped) {
                seekToOffset(0);
                st = WrapAround;
            } else {
                st = EndOfStream;
            }
        }
        return st;
    }

    int sampleRate() { return wi.rate; }

    void seekToOffset(double seconds)
    {
        Sint64 f = seconds <= 0 ? 0 : (Sint64)(seconds * wi.rate);
        if (f > frameCount)
            f = frameCount;
        framePos = f;
        SDL_RWseek(&src, wi.dataStart + f * wi.blockAlign, RW_SEEK_SET);
    }

    uint32_t loopStartFrames() { return 0; }

    bool setPitch(float) { return false; }
};
} // namespace

ALDataSource *createSDLSource(SDL_RWops &ops, const char *extension, uint32_t maxBufSize, bool looped)
{
    VitaWavInfo wi;
    std::string err;
    if (!vitaParseWav(ops, wi, err)) {
        SDL_RWclose(&ops);
        char sig[32];
        std::snprintf(sig, sizeof(sig), "%s", extension ? extension : "?");
        if (err == "WAV: not a RIFF/WAVE file")
            throw Exception(Exception::MKXPError, "audio format '%s' is not supported on Vita (Ogg Vorbis and WAV only)", sig);
        throw Exception(Exception::MKXPError, "%s", err.c_str());
    }
    try {
        return new VitaWavStreamSource(ops, wi, maxBufSize, looped);   /* takes ownership of ops */
    } catch (const std::bad_alloc &) {
        SDL_RWclose(&ops);
        throw Exception(Exception::MKXPError, "%s", vitaOomMessage("opening a WAV stream").c_str());
    }
}

/* ---------------------------------------------------------------- whole-file decode (SE) */

static size_t vitaVfRead(void *ptr, size_t size, size_t nmemb, void *ops)
{
    return SDL_RWread(static_cast<SDL_RWops *>(ops), ptr, size, nmemb);
}
static int vitaVfSeek(void *ops, ogg_int64_t offset, int whence)
{
    return (int)SDL_RWseek(static_cast<SDL_RWops *>(ops), offset, whence);
}
static long vitaVfTell(void *ops)
{
    return (long)SDL_RWtell(static_cast<SDL_RWops *>(ops));
}

/*
 * SoundEmitter (MKXP_VITA_AUDIO patch of soundemitter.cpp): decodes the whole file to PCM.
 * Closes ops. Returns false with err set when the data cannot be decoded.
 */
static bool vitaDecodeAudioAllImpl(SDL_RWops &ops, const char *ext, std::vector<uint8_t> &data,
                                   int &sampleSize, int &channels, int &rate, std::string &err)
{
    char sig[5] = { 0 };
    SDL_RWread(&ops, sig, 1, 4);
    SDL_RWseek(&ops, 0, RW_SEEK_SET);

    if (!std::strcmp(sig, "OggS")) {
        ov_callbacks cb = { vitaVfRead, vitaVfSeek, nullptr, vitaVfTell };
        OggVorbis_File vf;
        if (ov_open_callbacks(&ops, &vf, nullptr, 0, cb) != 0) {
            SDL_RWclose(&ops);
            err = "Vorbisfile: cannot read ogg file";
            return false;
        }
        channels = vf.vi->channels;
        rate = (int)vf.vi->rate;
        if (channels < 1 || channels > 2) {
            ov_clear(&vf);
            SDL_RWclose(&ops);
            err = "Ogg: more than 2 channels";
            return false;
        }
        sampleSize = 2;
        data.clear();
        char buf[16384];
        int section = 0;
        try {
            for (;;) {
                const long n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &section);
                if (n <= 0)
                    break;
                data.insert(data.end(), buf, buf + n);
            }
        } catch (const std::bad_alloc &) {
            ov_clear(&vf);
            SDL_RWclose(&ops);
            throw;
        }
        ov_clear(&vf);
        SDL_RWclose(&ops);
        return !data.empty() || (err = "Ogg: no samples", false);
    }

    {
        VitaWavInfo wi;
        if (vitaParseWav(ops, wi, err)) {
            const size_t frames = (size_t)(wi.dataSize / wi.blockAlign);
            sampleSize = vitaWavOutSampleSize(wi);
            channels = wi.channels;
            rate = wi.rate;
            std::vector<uint8_t> in;
            try {
                data.resize(frames * sampleSize * channels);
                if (sampleSize * channels != wi.blockAlign)
                    in.resize(4096 * wi.blockAlign);
            } catch (const std::bad_alloc &) {
                SDL_RWclose(&ops);
                throw;
            }
            if (sampleSize * channels == wi.blockAlign) {   /* 8/16-bit PCM: read in place */
                data.resize(SDL_RWread(&ops, data.data(), wi.blockAlign, frames) * wi.blockAlign);
            } else {                                         /* convert in small chunks */
                const size_t chunk = 4096;
                size_t done = 0, got;
                while (done < frames && (got = SDL_RWread(&ops, in.data(), wi.blockAlign, std::min(chunk, frames - done))) > 0)
                    done += vitaWavConvert(wi, in.data(), got, data.data() + done * sampleSize * channels) / (sampleSize * channels);
                data.resize(done * sampleSize * channels);
            }
            SDL_RWclose(&ops);
            return !data.empty() || (err = "WAV: no samples", false);
        }
        if (err != "WAV: not a RIFF/WAVE file") {
            SDL_RWclose(&ops);
            return false;
        }
        SDL_RWseek(&ops, 0, RW_SEEK_SET);
    }

    SDL_RWclose(&ops);
    err = std::string("audio format '") + (ext ? ext : "?") + "' is not supported on Vita (Ogg Vorbis and WAV only)";
    return false;
}

bool vitaDecodeAudioAll(SDL_RWops &ops, const char *ext, std::vector<uint8_t> &data,
                        int &sampleSize, int &channels, int &rate, std::string &err)
{
    try {
        return vitaDecodeAudioAllImpl(ops, ext, data, sampleSize, channels, rate, err);
    } catch (const std::bad_alloc &) {
        /* The implementation closes ops before rethrowing. */
        data.clear();
        err = vitaOomMessage("decoding the sound");
        return false;
    }
}

#ifdef MKXP_VITA_ANIM_PREDECODE
/*
 * vita-predecode.cpp (background thread): the PCM SoundEmitter::allocateBuffer would get for
 * filename, without OpenAL: the candidates of FileSystem::openRead in its order (index-skipped as
 * there), the first that opens and that vitaDecodeAudioAll decodes (SoundOpenHandler::tryRead).
 * false when none does (the main thread then goes the normal way and reports the same error).
 */
bool vitaDecodeAudioByName(const char *filename, std::vector<uint8_t> &data, int &sampleSize, int &channels, int &rate)
{
    static const char *const exts[] = { "", ".ogg", ".wav", ".mp3", ".mid", ".midi" };
    const std::string base = vitaGamePath(filename);
    for (const char *ext : exts) {
#ifdef MKXP_VITA_FS_INDEX
        if (vitaFsIndexQuery((base + ext).c_str()) == 0)
            continue;
#endif
        SDL_RWops ops;
        if (!vitaOpenRw(base + ext, ops))
            continue;
        const char *e = *ext ? ext + 1 : std::strrchr(base.c_str(), '.') ? std::strrchr(base.c_str(), '.') + 1 : "";
        std::string err;
        if (vitaDecodeAudioAll(ops, e, data, sampleSize, channels, rate, err))
            return true;
    }
    data.clear();
    return false;
}
#endif
