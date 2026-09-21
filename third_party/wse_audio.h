// ============================================================================
//  wse_audio.h —— 音效解码（DLL 与 GUI 共用）
//
//  统一解出 **16-bit 交织 PCM**，并**保留原始采样率**（常见采样率不重采样）：
//    WAV：PCM 8/16/24/32 位整型、IEEE float 32/64、A-law / µ-law（G.711）
//    MP3 / OGG(Vorbis) / FLAC
//
//  用法：需要实现时，在同一个 TU 里先定义 WSE_AUDIO_IMPLEMENTATION 再包含本文件：
//    #define WSE_AUDIO_IMPLEMENTATION
//    #include "wse_audio.h"
//
//  依赖（third_party/，均为公有领域/MIT 单头解码器，可自由整合）：
//    minimp3.h   —— MP3
//    stb_vorbis.c—— OGG Vorbis
//    dr_flac.h   —— FLAC
//  ============================================================================
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(WSE_AUDIO_IMPLEMENTATION)
#pragma warning(push)
#pragma warning(disable: 4100 4201 4242 4244 4245 4267 4305 4324 4365 4389 4456 4702 4706 4710 4711 4820 4996 6011 6262 6387)
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#pragma warning(pop)
#else
#include "minimp3.h"
#include "stb_vorbis.c"
#include "dr_flac.h"
#endif

namespace wseaudio {

enum Format { FMT_UNKNOWN = 0, FMT_WAV = 1, FMT_MP3 = 2, FMT_OGG = 3, FMT_FLAC = 4 };

struct Pcm {
    std::vector<std::int16_t> samples;   // 交织 s16
    unsigned channels = 0;
    unsigned rate = 0;
    Format format = FMT_UNKNOWN;
    bool valid = false;
};

inline bool IsStandardRate(unsigned r) {
    switch (r) {
        case 8000: case 11025: case 16000: case 22050: case 32000:
        case 44100: case 48000: case 88200: case 96000:
            return true;
        default: return false;
    }
}

namespace {

// 1 级三角(TPDF)抖动：>16bit / float 转 16bit 时保留低电平细节
struct Dither {
    std::uint32_t s;
    Dither() : s(0x9E3779B9u) {}
    int noise() {
        s = s * 1664525u + 1013904223u; const std::uint32_t a = s >> 8;
        s = s * 1664525u + 1013904223u; const std::uint32_t b = s >> 8;
        return (int)((a & 0xFF) - (b & 0xFF));
    }
};

inline std::int16_t Clamp16(long v) {
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (std::int16_t)v;
}

// G.711 A-law -> 16-bit
inline std::int16_t ALawTo16(std::uint8_t a) {
    a = (std::uint8_t)(a ^ 0x55);
    int t = (int)(a & 0x0F) << 4;
    const int seg = (a & 0x70) >> 4;
    if (seg == 0) t += 8;
    else if (seg == 1) t += 0x108;
    else { t += 0x108; t <<= (seg - 1); }
    return (std::int16_t)((a & 0x80) ? t : -t);
}

// G.711 µ-law -> 16-bit
inline std::int16_t MuLawTo16(std::uint8_t u) {
    u = (std::uint8_t)~u;
    int t = (((int)(u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4);
    return (std::int16_t)((u & 0x80) ? (t - 0x84) : (0x84 - t));
}

// ---------- WAV ----------
bool ParseWav(const std::uint8_t* buf, std::size_t size, Pcm& out) {
    out = Pcm{};
    if (!buf || size < 44) return false;
    if (std::memcmp(buf, "RIFF", 4) != 0 || std::memcmp(buf + 8, "WAVE", 4) != 0) return false;

    std::uint16_t tag = 0, ch = 0, bits = 0;
    std::uint32_t rate = 0;
    const std::uint8_t* dataPtr = nullptr;
    std::size_t dataLen = 0;
    bool haveFmt = false, haveData = false;

    std::size_t pos = 12;
    while (pos + 8 <= size) {
        char id[5] = {};
        std::memcpy(id, buf + pos, 4);
        std::uint32_t chunk = 0;
        std::memcpy(&chunk, buf + pos + 4, 4);
        if ((std::size_t)chunk > size - (pos + 8)) break;
        if (std::memcmp(id, "fmt ", 4) == 0 && chunk >= 16) {
            std::memcpy(&tag,  buf + pos + 8 + 0, 2);
            std::memcpy(&ch,   buf + pos + 8 + 2, 2);
            std::memcpy(&rate, buf + pos + 8 + 4, 4);
            std::memcpy(&bits, buf + pos + 8 + 14, 2);
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataPtr = buf + pos + 8;
            dataLen = chunk;
            haveData = true;
        }
        pos += 8 + chunk + (chunk & 1u);
        if (haveFmt && haveData) break;
    }
    if (!haveFmt || !haveData || ch == 0 || ch > 8 || rate == 0 || dataPtr == nullptr || dataLen == 0)
        return false;

    const std::size_t bytesPerSample = bits / 8;
    if (bytesPerSample == 0) return false;
    const std::size_t blockAlign = bytesPerSample * ch;
    const std::size_t nFrames = dataLen / blockAlign;
    if (nFrames == 0) return false;

    out.samples.resize(nFrames * ch);
    out.channels = ch;
    out.rate = rate;
    out.format = FMT_WAV;

    const std::size_t n = nFrames * ch;
    if (tag == 1) {                      // PCM 整型
        switch (bits) {
            case 8:  // 无符号
                for (std::size_t i = 0; i < n; ++i)
                    out.samples[i] = (std::int16_t)(((int)dataPtr[i] - 128) << 8);
                break;
            case 16:
                std::memcpy(out.samples.data(), dataPtr, n * 2);
                break;
            case 24: {
                Dither d;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::uint8_t* b3 = dataPtr + i * 3;
                    long v = (long)b3[0] | ((long)b3[1] << 8) | ((long)b3[2] << 16);
                    if (v & 0x800000L) v |= ~0xFFFFFFL;      // 符号扩展
                    out.samples[i] = Clamp16((v >> 8) + d.noise());
                }
                break;
            }
            case 32: {
                Dither d;
                for (std::size_t i = 0; i < n; ++i) {
                    std::int32_t v;
                    std::memcpy(&v, dataPtr + i * 4, 4);
                    out.samples[i] = Clamp16(((long)v >> 16) + d.noise());
                }
                break;
            }
            default: return false;
        }
    } else if (tag == 3) {               // IEEE float
        Dither d;
        if (bits == 32) {
            for (std::size_t i = 0; i < n; ++i) {
                float f;
                std::memcpy(&f, dataPtr + i * 4, 4);
                float v = f * 32767.0f;
                if (v > 32767.0f) v = 32767.0f; if (v < -32768.0f) v = -32768.0f;
                out.samples[i] = (std::int16_t)(v + (float)d.noise());
            }
        } else if (bits == 64) {
            for (std::size_t i = 0; i < n; ++i) {
                double g;
                std::memcpy(&g, dataPtr + i * 8, 8);
                double v = g * 32767.0;
                if (v > 32767.0) v = 32767.0; if (v < -32768.0) v = -32768.0;
                out.samples[i] = (std::int16_t)(v + (double)d.noise());
            }
        } else {
            return false;
        }
    } else if (tag == 6 && bits == 8) {  // A-law
        for (std::size_t i = 0; i < n; ++i) out.samples[i] = ALawTo16(dataPtr[i]);
    } else if (tag == 7 && bits == 8) {  // µ-law
        for (std::size_t i = 0; i < n; ++i) out.samples[i] = MuLawTo16(dataPtr[i]);
    } else {
        return false;
    }
    out.valid = true;
    return true;
}

// ---------- MP3 ----------
bool DecodeMp3(const std::uint8_t* buf, std::size_t size, Pcm& out) {
    out = Pcm{};
    if (!buf || size < 4) return false;

    const std::uint8_t* p = buf;
    std::size_t left = size;
    if (left >= 10 && std::memcmp(p, "ID3", 3) == 0) {      // 跳过 ID3v2 头
        std::size_t tagSize = ((std::size_t)(p[6] & 0x7F) << 21) | ((std::size_t)(p[7] & 0x7F) << 14) |
                              ((std::size_t)(p[8] & 0x7F) << 7) | (std::size_t)(p[9] & 0x7F);
        p += 10 + tagSize;
        if (p > buf + size) return false;
        left = (std::size_t)((buf + size) - p);
    }

    mp3dec_t dec;
    mp3dec_init(&dec);

    std::vector<std::int16_t> all;
    all.reserve(4096);
    std::uint16_t ch = 0;
    unsigned hz = 0;
    std::int16_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (left > 0) {
        mp3dec_frame_info_t info;
        const int n = mp3dec_decode_frame(&dec, p, left, frame, &info);
        if (info.channels != 0) {
            if (ch == 0) { ch = (std::uint16_t)info.channels; hz = (unsigned)info.hz; }
            const std::size_t cnt = (std::size_t)(n > 0 ? n : 0) * info.channels;
            if (cnt) all.insert(all.end(), frame, frame + cnt);
        }
        if (info.frame_bytes == 0) { ++p; --left; continue; }   // 无同步帧 → 逐字节找
        p += info.frame_bytes;
        left = (std::size_t)((buf + size) - p);
    }

    if (all.empty() || ch == 0 || ch > 8 || hz == 0) return false;
    out.samples = std::move(all);
    out.channels = ch;
    out.rate = hz;
    out.format = FMT_MP3;
    out.valid = true;
    return true;
}

// ---------- OGG Vorbis ----------
bool DecodeOgg(const std::uint8_t* buf, std::size_t size, Pcm& out) {
    out = Pcm{};
    if (size > 0x7FFFFFFFu) return false;
    int ch = 0, rate = 0;
    short* raw = nullptr;
    const int perChan = stb_vorbis_decode_memory((const unsigned char*)buf, (int)size, &ch, &rate, &raw);
    if (perChan <= 0 || raw == nullptr || ch <= 0 || ch > 8 || rate <= 0) {
        if (raw) std::free(raw);
        return false;
    }
    out.samples.assign(raw, raw + (std::size_t)perChan * (std::size_t)ch);
    std::free(raw);
    out.channels = (unsigned)ch;
    out.rate = (unsigned)rate;
    out.format = FMT_OGG;
    out.valid = true;
    return true;
}

// ---------- FLAC ----------
bool DecodeFlac(const std::uint8_t* buf, std::size_t size, Pcm& out) {
    out = Pcm{};
    drflac* f = drflac_open_memory(buf, size, NULL);
    if (!f) return false;

    const unsigned ch = f->channels ? (unsigned)f->channels : 0;
    const unsigned rate = f->sampleRate ? (unsigned)f->sampleRate : 0;
    drflac_uint64 total = 0;
    if (f->totalPCMFrameCount > 0) total = (drflac_uint64)f->totalPCMFrameCount;

    // 单文件解码上限：~5 分钟 48k 立体声（再多基本就是误选长文件）
    const drflac_uint64 kMaxFrames = 16000000ULL;

    std::vector<std::int16_t> pcm;
    if (total > 0) {
        const drflac_uint64 frames = total < kMaxFrames ? total : kMaxFrames;
        pcm.resize((std::size_t)(frames * ch));
        const drflac_uint64 got = drflac_read_pcm_frames_s16(f, frames, pcm.data());
        pcm.resize((std::size_t)(got * ch));
    } else {
        std::int16_t tmp[4096 * 8];
        for (;;) {
            const drflac_uint64 got = drflac_read_pcm_frames_s16(f, 4096, tmp);
            if (got == 0) break;
            pcm.insert(pcm.end(), tmp, tmp + (std::size_t)got * ch);
            if ((drflac_uint64)pcm.size() >= kMaxFrames * ch) break;
        }
    }
    drflac_close(f);

    if (pcm.empty() || ch == 0 || ch > 8 || rate == 0) return false;
    out.samples = std::move(pcm);
    out.channels = ch;
    out.rate = rate;
    out.format = FMT_FLAC;
    out.valid = true;
    return true;
}

} // namespace

// 按文件头识别并解码：WAV / FLAC / OGG / MP3（也容忍无 ID3 的裸 MP3）
bool DecodeFileBytes(const std::uint8_t* buf, std::size_t size, Pcm& out) {
    out = Pcm{};
    if (!buf || size < 16) return false;
    if (std::memcmp(buf, "RIFF", 4) == 0 && std::memcmp(buf + 8, "WAVE", 4) == 0)
        return ParseWav(buf, size, out);
    if (std::memcmp(buf, "fLaC", 4) == 0) return DecodeFlac(buf, size, out);
    if (std::memcmp(buf, "OggS", 4) == 0) return DecodeOgg(buf, size, out);
    if (std::memcmp(buf, "ID3", 3) == 0) return DecodeMp3(buf, size, out);
    if ((buf[0] & 0xFF) == 0xFF && (buf[1] & 0xE0) == 0xE0) return DecodeMp3(buf, size, out);
    return false;   // 其它格式（AAC/M4A/WMA 等暂不支持）
}

inline const char* FormatName(Format f) {
    switch (f) {
        case FMT_WAV:  return "wav";
        case FMT_MP3:  return "mp3";
        case FMT_OGG:  return "ogg";
        case FMT_FLAC: return "flac";
        default:       return "?";
    }
}

} // namespace wseaudio