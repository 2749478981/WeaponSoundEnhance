// ============================================================================
//  WeaponSoundEnhance.cpp  (v2)
//  Monster Hunter: World / Iceborne (15.23.00) weapon-sound plugin.
//
//  Standalone: no game audio engine calls, no code hooks. Player actions are
//  detected by a background polling thread; wav playback uses winmm waveOut
//  (one handle per voice -> true multi-sound overlap).
//
//  Memory layout (15.23.00, PlayerRoot overridable in ini):
//    manager = *(PlayerRoot)                       [PlayerRoot = 0x1450139A0]
//    entity  = *(manager + 0x50)
//    fsm     = *(*(entity + 0x468) + 0xE9C4)
//    weapon  = *(*(*(entity + 0xC0) + 0x8) + 0x78) ; +0x2E8 = type, +0x2EC = id
//    LS gauge = *(*(entity + GaugePtrOff) + GaugeValOff)   (default 0x76B0/0x2370)
//
//  Config: one [Attack...] section per trigger entry. Legacy keys
//  (WeaponType/ActionLMT/FSMId/Sound/SoundDelay/SoundVol) behave exactly as v1.
//  v2 additions:
//    LMT=...            trigger on any of several LMTs (comma list)
//    Sound:<tag>=...    per-LS-gauge sound set (tag 0..3 or none/white/yellow/red);
//                       missing tag falls back to the default Sound= set
//    Group=...          logical-action id: shared by the trigger entries of one
//                       attack, the group fires once per action occurrence and
//                       freezes the gauge at the first trigger instant
//    path|delay|vol|F   per-sound spec; trailing F marks the sound "fixed"
//                       (always plays; same file is 400ms-cooldowned to avoid
//                       stacking); non-fixed sounds still pick one at random
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <memory>
#include <mutex>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <utility>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

// ===========================================================================
//  Memory-safe read helpers (every access guarded)
// ===========================================================================
namespace mem {

inline bool IsReadable(std::uintptr_t a, std::size_t n)
{
    if (a == 0 || n == 0) return false;
    for (std::size_t i = 0; i < n; i += 0x1000) {
        MEMORY_BASIC_INFORMATION mbi{};
        std::uintptr_t probe = a + i;
        if (::VirtualQuery((LPCVOID)probe, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect == PAGE_NOACCESS || mbi.Protect == PAGE_GUARD) return false;
        const DWORD rw = mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        if (rw == 0) return false;
        if (i + 0x1000 >= n) break;
        if (probe + 0x1000 < probe) return false;
    }
    return true;
}

inline bool IsWritable(std::uintptr_t a, std::size_t n)
{
    if (a == 0 || n == 0) return false;
    for (std::size_t i = 0; i < n; i += 0x1000) {
        MEMORY_BASIC_INFORMATION mbi{};
        std::uintptr_t probe = a + i;
        if (::VirtualQuery((LPCVOID)probe, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect == PAGE_NOACCESS || mbi.Protect == PAGE_GUARD) return false;
        const DWORD rw = mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        if (rw == 0) return false;
        if (i + 0x1000 >= n) break;
        if (probe + 0x1000 < probe) return false;
    }
    return true;
}

template <typename T>
inline bool ReadVal(std::uintptr_t a, T& out)
{
    if (!IsReadable(a, sizeof(T))) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(a), sizeof(T));
    return true;
}

// addr = base ; for each offset: addr = *(addr + off). No deref of base itself.
inline std::uintptr_t Walk(std::uintptr_t base, const std::uint32_t* offs, int count)
{
    if (base == 0) return 0;
    std::uintptr_t cur = base;
    for (int i = 0; i < count; ++i) {
        if (cur == 0) return 0;
        std::uintptr_t next = 0;
        if (!ReadVal(cur + offs[i], next)) return 0;
        cur = next;
    }
    return cur;
}

inline std::int32_t ReadI32(std::uintptr_t a, std::int32_t dflt)
{
    std::int32_t v = dflt;
    if (IsReadable(a, 4)) std::memcpy(&v, reinterpret_cast<const void*>(a), 4);
    return v;
}

} // namespace mem

// ===========================================================================
//  Text conversion (UTF-8 <-> wide)
// ===========================================================================
namespace strconv {

inline std::wstring ToWide(const std::string& s)
{
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

inline std::string ToUtf8(const std::wstring& w)
{
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

} // namespace strconv

// ===========================================================================
//  Player live state (refreshed by the polling thread)
// ===========================================================================
namespace player {

std::uintptr_t gRoot = 0x1450139A0ULL;   // ini override-able
std::int32_t   gLmt = -1;
std::int32_t   gFsm = -1;
std::int32_t   gWeapon = -1;
std::int32_t   gWeaponId = -1;
std::int32_t   gGauge = -1;              // long-sword spirit gauge level 0..3, -1 unknown

std::uint32_t gGaugePtrOff = 0x76B0;     // LS spirit object: *(entity + off)
std::uint32_t gGaugeValOff = 0x2370;     // gauge level value: *(obj + off)

void Refresh()
{
    gLmt = -1; gFsm = -1; gWeapon = -1; gWeaponId = -1; gGauge = -1;

    std::uintptr_t manager = 0;
    if (!mem::ReadVal(gRoot, manager) || manager == 0) return;

    const std::uint32_t c0[1] = {0x50};
    const std::uintptr_t entity = mem::Walk(manager, c0, 1);
    if (!entity) return;

    gFsm = mem::ReadI32(entity + 0x6278, -1);

    std::uintptr_t act = 0;
    if (mem::ReadVal(entity + 0x468, act) && act)
        gLmt = mem::ReadI32(act + 0xE9C4, -1);

    static const std::uint32_t wd[3] = {0xC0, 0x8, 0x78};
    const std::uintptr_t data = mem::Walk(entity, wd, 3);
    if (data) {
        gWeapon   = mem::ReadI32(data + 0x2E8, -1);
        gWeaponId = mem::ReadI32(data + 0x2EC, -1);
    }

    // LS gauge level is only meaningful (and only read) for the long sword.
    if (gWeapon == 3) {
        std::uintptr_t sp = 0;
        if (mem::ReadVal(entity + gGaugePtrOff, sp) && sp) {
            const int v = mem::ReadI32(sp + gGaugeValOff, -1);
            if (v >= 0 && v <= 3) gGauge = v;
        }
    }
}

// true when the player entity exists (in-scene; used before touching chat memory)
bool RefreshIsInScene()
{
    std::uintptr_t manager = 0;
    if (!mem::ReadVal(gRoot, manager) || manager == 0) return false;
    std::uintptr_t entity = 0;
    const std::uint32_t c0[1] = {0x50};
    entity = mem::Walk(manager, c0, 1);
    return entity != 0;
}

} // namespace player

// ===========================================================================
//  Module paths + logging
// ===========================================================================
namespace plugin {

HMODULE gModule = nullptr;
volatile LONG gStop = 0;
std::wstring gModuleDir;   // ends with '\'
std::wstring gIniPath;
std::wstring gLogPath;
volatile int gDebug = 0;   // ini Debug=1 enables per-play / heartbeat logging

void Log(const char* fmt, ...);

void LogInit()
{
    gLogPath = gModuleDir + L"WeaponSoundEnhance.log";
    ::DeleteFileW(gLogPath.c_str());
    Log("WeaponSoundEnhance 2.0 starting");
}

void LogV(const char* fmt, va_list ap)
{
    char buf[2048] = {};
    vsnprintf_s(buf, _TRUNCATE, fmt, ap);
    ::OutputDebugStringA(buf);
    FILE* f = nullptr;
    if (_wfopen_s(&f, gLogPath.c_str(), L"ab") == 0 && f) {
        SYSTEMTIME st{}; ::GetLocalTime(&st);
        fprintf(f, "[%02u:%02u:%02u.%03u] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

void Log(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

// verbose logging, only active when Debug=1
void LogD(const char* fmt, ...)
{
    if (!gDebug) return;
    va_list ap; va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

} // namespace plugin

// ===========================================================================
//  Standalone WAV player: winmm waveOut, one handle per voice, concurrency cap
// ===========================================================================
namespace audio {

struct Wav
{
    std::vector<std::uint8_t> data;   // PCM payload only (RIFF header stripped)
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPer = 0;
    bool valid = false;
};

bool ParseWav(const std::uint8_t* buf, std::size_t size, Wav& out)
{
    out = Wav{};
    if (!buf || size < 44) return false;
    if (std::memcmp(buf, "RIFF", 4) != 0 || std::memcmp(buf + 8, "WAVE", 4) != 0)
        return false;

    std::uint16_t tag = 0, ch = 0, bits = 0;
    std::uint32_t rate = 0;
    const std::uint8_t* dataPtr = nullptr;
    std::uint32_t dataLen = 0;
    bool haveFmt = false, haveData = false;

    std::size_t pos = 12;
    while (pos + 8 <= size) {
        char id[5] = {0};
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
        pos += 8 + chunk;
        if (chunk & 1) pos += 1;
    }

    if (!haveFmt || !haveData || tag != 1 || ch == 0 || rate == 0 || !dataPtr || dataLen == 0)
        return false;

    out.channels = ch; out.sampleRate = rate; out.bitsPer = bits;
    out.data.assign(dataPtr, dataPtr + dataLen);
    out.valid = true;
    return true;
}

// Linear-interpolation resample of 16-bit PCM to a standard rate (waveOut
// rejects exotic rates such as 28000 Hz; 44100 is the playback standard).
void ResampleTo(Wav& w, std::uint32_t targetRate = 44100)
{
    if (!w.valid || w.sampleRate == targetRate) return;
    if (w.bitsPer != 16) return;

    const std::size_t inSamples = w.data.size() / 2;
    if (inSamples == 0) return;
    const std::size_t outSamples = (std::size_t)((double)inSamples * targetRate / w.sampleRate);
    if (outSamples == 0) return;

    const std::int16_t* in = reinterpret_cast<const std::int16_t*>(w.data.data());
    const double step = (double)inSamples / (double)outSamples;
    std::vector<std::int16_t> out(outSamples);
    for (std::size_t i = 0; i < outSamples; ++i) {
        double pos = (double)i * step;
        std::size_t idx = (std::size_t)pos;
        if (idx >= inSamples - 1) idx = inSamples - 1;
        double frac = pos - (double)idx;
        const std::size_t nxt = (idx + 1 < inSamples) ? idx + 1 : idx;
        double v = in[idx] * (1.0 - frac) + in[nxt] * frac;
        out[i] = (std::int16_t)v;
    }
    w.data.resize(out.size() * 2);
    std::memcpy(w.data.data(), out.data(), out.size() * 2);
    w.sampleRate = targetRate;
}

const int kMaxVoices = 6;   // simultaneous waveOut voices; excess is dropped
std::atomic<int> gVoices{0};

struct Engine
{
    bool Init()
    {
        ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return true;
    }

    // Copy PCM with per-sound gain, then hand to a worker thread. Returns true
    // once the copy was accepted; the voice cap is enforced inside the worker.
    bool Play(const Wav& w, float volume, unsigned delayMs = 0)
    {
        if (!w.valid || w.data.empty()) { mLastHr = E_INVALIDARG; return false; }
        if (w.bitsPer != 16) { mLastHr = E_INVALIDARG; return false; }

        PlayCtx* ctx = new PlayCtx();
        const std::size_t n = w.data.size() / 2;
        ctx->samples.resize(n);
        const std::int16_t* src = reinterpret_cast<const std::int16_t*>(w.data.data());
        float gain = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
        for (std::size_t i = 0; i < n; ++i)
            ctx->samples[i] = (std::int16_t)(src[i] * gain);
        ctx->channels = w.channels;
        ctx->rate = w.sampleRate;
        ctx->bits = w.bitsPer;
        ctx->delayMs = delayMs;

        HANDLE th = ::CreateThread(nullptr, 0, &PlayWorker_Host, ctx, 0, nullptr);
        if (!th) { delete ctx; mLastHr = E_OUTOFMEMORY; return false; }
        ::CloseHandle(th);
        return true;
    }

    HRESULT LastHr() const { return mLastHr; }

private:
    struct PlayCtx {
        std::vector<std::int16_t> samples;
        WORD  channels = 2;
        DWORD rate = 44100;
        WORD  bits = 16;
        unsigned delayMs = 0;
    };

    struct VoiceGuard {
        VoiceGuard() { ++gVoices; }
        ~VoiceGuard() { --gVoices; }
    };

    static DWORD WINAPI PlayWorker_Host(LPVOID param)
    {
        std::unique_ptr<PlayCtx> ctx(static_cast<PlayCtx*>(param));
        if (gVoices.load() >= kMaxVoices) {
            plugin::LogD("[voice] cap %d reached, voice dropped", kMaxVoices);
            return 0;
        }
        VoiceGuard vg;
        PlayWorker(*ctx);
        return 0;
    }

    static void PlayWorker(PlayCtx& ctx)
    {
        if (ctx.samples.empty()) return;
        if (ctx.delayMs > 0) ::Sleep(ctx.delayMs);

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = ctx.channels;
        wfx.nSamplesPerSec = ctx.rate;
        wfx.wBitsPerSample = ctx.bits;
        wfx.nBlockAlign = (WORD)((ctx.bits / 8) * ctx.channels);
        wfx.nAvgBytesPerSec = ctx.rate * wfx.nBlockAlign;

        HWAVEOUT hwo = nullptr;
        if (waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return;

        std::vector<std::uint8_t> bytes(ctx.samples.size() * 2);
        std::memcpy(bytes.data(), ctx.samples.data(), bytes.size());

        WAVEHDR hdr = {};
        hdr.lpData = reinterpret_cast<LPSTR>(bytes.data());
        hdr.dwBufferLength = static_cast<DWORD>(bytes.size());
        if (waveOutPrepareHeader(hwo, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
            waveOutClose(hwo); return;
        }
        waveOutWrite(hwo, &hdr, sizeof(hdr));
        while ((hdr.dwFlags & WHDR_DONE) == 0)
            ::Sleep(10);
        waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
        waveOutClose(hwo);
    }

    HRESULT mLastHr = S_OK;
};

Engine g_audio;

} // namespace audio

// ===========================================================================
//  Config model + ini parsing
// ===========================================================================
namespace plugin {

// --- global switches (also toggled by hotkeys / chat commands) ---
std::uintptr_t gPlayerRoot = 0x1450139A0ULL;
int gPollMs = 60;
int gDebounceMs = 120;
volatile int gVolumePct = 100;
volatile int gEnabled = 1;
volatile int gMoreSounds = 1;   // legacy: only affects pools without any fixed sound
std::uint32_t gGaugePtrOff = 0x76B0;
std::uint32_t gGaugeValOff = 0x2370;

volatile int g_useChatEcho = 1;
volatile int g_useChatCommands = 1;
volatile int g_hotkeysEnabled = 1;   // 启用/关闭热键（ini [WeaponSoundEnhance] Hotkeys=0）

// --- hotkeys (defaults; ini [Hotkeys] overrides) ---
int gModifierKey = VK_CONTROL;
int gReloadKey   = VK_F5;
int gVolUpKey    = VK_UP;
int gVolDownKey  = VK_DOWN;
int gSetVolKey   = VK_F8;
int gToggleKey   = VK_F9;
int gMoreKey     = VK_F10;
int gComboKey    = VK_F11;   // 切换当前武器配置组合
int gSetVolValue = 50;

// --- game module addresses (15.23.00) ---
std::uintptr_t gGameBase = 0;
const std::uintptr_t kSystemMessageRva = 0x1A540D0;
const std::uintptr_t kSystemMessageMgrRva = 0x500CE70;
typedef void (*SystemMessageFn)(void* manager, const char* utf8Buffer,
                                float duration, std::int32_t messageId, bool emphasized);
SystemMessageFn g_systemMessage = nullptr;

const std::uintptr_t kMessageBaseRva = 0x4F87FF0;
const std::uintptr_t kMessageLenOff  = 0xBC;
const std::uintptr_t kMessageBodyOff = 0xC0;

// ===========================================================================
//  Data model
// ===========================================================================

// One selectable sound. path is relative to the module dir (usually "sounds/x.wav").
struct SoundSpec
{
    std::string path;
    int delay = 0;    // ms to wait before playback
    int vol = 100;    // 0..100, 100 = no extra gain beyond master volume
    bool fixed = false; // F flag: always plays when its pool is hit
    bool plain = true;  // pure legacy token (no |d|v|F) -> SoundDelay/SoundVol apply
};

struct Pool
{
    std::vector<SoundSpec> specs;

    bool empty() const { return specs.empty(); }
    bool HasAnyFixed() const
    {
        for (const auto& s : specs) if (s.fixed) return true;
        return false;
    }
};

// One ini [Attack...] section: trigger conditions + sound pools.
struct Attack
{
    int weaponType = -1;          // -1 = any weapon
    int fsmId = -1;               // -1 = any FSM
    std::vector<int> lmt;         // empty = any LMT
    std::string name;             // Name= display label (ignored by matching)
    std::string group;            // Group= logical action: all members fire at most
                                  // once per action occurrence (first trigger wins)
    Pool defPool;                 // default pool (tag = "any")
    Pool gaugePool[4];            // LS gauge pools, keyed by level 0..3
    bool inMatch = false;         // edge latch: fire once per action (ungrouped entries)

    bool HasSounds() const
    {
        if (!defPool.empty()) return true;
        for (int i = 0; i < 4; ++i) if (!gaugePool[i].empty()) return true;
        return false;
    }
};

// 0..3 -> names used by Sound:<tag> keys
const char* const kGaugeNames[4] = {"none", "white", "yellow", "red"};

int GaugeTagToLevel(const std::string& tag)
{
    if (tag.empty()) return -1;
    if (tag.size() == 1 && tag[0] >= '0' && tag[0] <= '3') return tag[0] - '0';
    for (int i = 0; i < 4; ++i) {
        std::string n = kGaugeNames[i];
        if (n == tag) return i;
    }
    return -1;
}

std::vector<Attack> gAttacks;
std::mutex gCfgMutex;

std::vector<Attack> SnapshotAttacks()
{
    std::lock_guard<std::mutex> lk(gCfgMutex);
    return gAttacks;
}

// --- wav cache: guarded by gCacheMutex, populated by preload ---
std::mutex gCacheMutex;
std::vector<std::pair<std::string, audio::Wav>> gCache;

// Per-file replay cooldown (random AND fixed layer), ms: stops the same sound
// file from stacking on itself across duplicate trigger instants/entries.
std::vector<std::pair<std::string, std::uint64_t>> soundCooldown;
const int kSoundCooldownMs = 400;

// Action-group state: entries sharing one Group= name belong to a single
// logical attack; the group fires at most once per action occurrence, with the
// gauge level frozen at the first trigger instant (mid-action level-ups like
// the LS spirit finisher no longer fire the attack twice).
struct GroupRt { std::uint64_t lastSeen = 0; bool fired = false; };
std::vector<std::pair<std::string, GroupRt>> g_groups;
const std::uint64_t kGroupResetMs = 450;   // quiet time before the group re-arms

GroupRt& GroupState(const std::string& g)
{
    for (auto& p : g_groups)
        if (p.first == g) return p.second;
    g_groups.emplace_back(g, GroupRt());
    return g_groups.back().second;
}

// ---- per-weapon config combos ----
// 每个武器可有多个命名"组合"(combo)，每个组合是一组该武器的 [Attack] 条目；
// [Active] 表决定每武器当前用哪个组合。切换某武器组合只影响该武器，其它武器不动。
std::map<int, std::map<std::string, std::vector<Attack>>> g_combos;  // weapon -> comboName -> attacks
std::map<int, std::string>  g_activeCombo;                            // weapon -> active comboName (""=默认)
std::map<int, std::vector<std::string>> g_comboOrder;                 // weapon -> combo names (默认""在前)

// 依据 g_combos + g_activeCombo 重建"当前激活"的攻击条目表（运行线程只遍历 gAttacks）。
void RebuildActiveAttacks()
{
    std::vector<Attack> acts;
    for (const auto& wc : g_combos) {
        const int w = wc.first;
        if (w == -1) {   // 任意武器：仅其"默认"组合常驻
            auto it = wc.second.find("");
            if (it != wc.second.end()) acts.insert(acts.end(), it->second.begin(), it->second.end());
            continue;
        }
        std::string active = "";
        auto a = g_activeCombo.find(w);
        if (a != g_activeCombo.end()) active = a->second;
        if (!wc.second.count(active)) active = "";   // 选中的组合不存在 → 回退默认
        auto it = wc.second.find(active);
        if (it != wc.second.end()) acts.insert(acts.end(), it->second.begin(), it->second.end());
    }
    {
        std::lock_guard<std::mutex> lk(gCfgMutex);
        gAttacks = std::move(acts);
        g_groups.clear();   // 组合切换后旧组状态作废
    }
}

std::uint64_t gLastTrigger = 0;

// ===========================================================================
//  Text helpers
// ===========================================================================

std::wstring ReplaceExt(const std::wstring& path, const wchar_t* newExt)
{
    std::wstring p = path;
    std::size_t dot = p.find_last_of(L'.');
    std::size_t slash = p.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return p + newExt;
    return p.substr(0, dot) + newExt;
}

inline std::string Trim(const std::string& s)
{
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::string ToLower(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// Split on ';' ',' (values may be UTF-8)
void SplitList(const std::string& value, std::vector<std::string>& out)
{
    std::string cur;
    for (std::size_t i = 0; i <= value.size(); ++i) {
        char cc = (i < value.size()) ? value[i] : '\0';
        if (cc == '\0' || cc == ';' || cc == ',') {
            std::string tok = Trim(cur);
            if (!tok.empty()) out.push_back(tok);
            cur.clear();
            if (cc == '\0') break;
        } else {
            cur += cc;
        }
    }
}

// Split one sound token on '|':  path [ | delay | vol | flags ]
// flags may contain 'F' -> fixed.
void ParseSoundSpec(const std::string& token, SoundSpec& sp)
{
    sp = SoundSpec{};
    std::vector<std::string> parts;
    std::string cur;
    for (std::size_t i = 0; i <= token.size(); ++i) {
        char cc = (i < token.size()) ? token[i] : '\0';
        if (cc == '\0' || cc == '|') {
            parts.push_back(Trim(cur));
            cur.clear();
            if (cc == '\0') break;
        } else {
            cur += cc;
        }
    }

    sp.path = parts.empty() ? "" : parts[0];
    if (parts.size() >= 4) {
        // explicit form: path|delay|vol|flags
        sp.plain = false;
        sp.delay = parts[1].empty() ? 0 : std::atoi(parts[1].c_str());
        if (sp.delay < 0) sp.delay = 0;
        int v = parts[2].empty() ? 100 : std::atoi(parts[2].c_str());
        sp.vol = v < 0 ? 0 : (v > 100 ? 100 : v);
        std::string flags = ToLower(parts[3]);
        sp.fixed = flags.find('f') != std::string::npos;
    }
    // parts.size() == 2 / 3 without flags: accept path|delay|vol too
    else if (parts.size() == 3) {
        sp.plain = false;
        sp.delay = parts[1].empty() ? 0 : std::atoi(parts[1].c_str());
        if (sp.delay < 0) sp.delay = 0;
        int v = parts[2].empty() ? 100 : std::atoi(parts[2].c_str());
        sp.vol = v < 0 ? 0 : (v > 100 ? 100 : v);
    } else if (parts.size() == 2) {
        sp.plain = false;
        sp.delay = parts[1].empty() ? 0 : std::atoi(parts[1].c_str());
        if (sp.delay < 0) sp.delay = 0;
    }
}

// Read a whole ini file as UTF-8, skipping a BOM if present.
bool ReadFileUtf8(const std::wstring& path, std::string& out)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16LL << 20)) {
        ::CloseHandle(h);
        return false;
    }
    out.resize(static_cast<std::size_t>(sz.QuadPart));
    DWORD rd = 0;
    BOOL ok = ::ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;
    out.resize(rd);
    if (out.size() >= 3 &&
        (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

std::uintptr_t ParseHex(const std::string& s0)
{
    std::string s = Trim(s0);
    const char* p = s.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    unsigned long long hv = 0;
    if (sscanf_s(p, "%llx", &hv) != 1) return 0;
    return static_cast<std::uintptr_t>(hv);
}

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ===========================================================================
//  Config loading
// ===========================================================================

// Append parsed specs to a pool. plainOrder records (pool, index) of plain
// legacy tokens so SoundDelay/SoundVol can be applied later without dangling
// pointers across vector growth.
void AppendSpecs(Pool& pool, const std::string& value,
                 std::vector<std::pair<Pool*, int>>& plainOrder)
{
    std::vector<std::string> toks;
    SplitList(value, toks);
    for (const auto& tok : toks) {
        if (tok.empty()) continue;
        SoundSpec sp;
        ParseSoundSpec(tok, sp);
        if (sp.path.empty()) continue;
        // dedupe by path within the pool (F and random sets stay disjoint)
        bool dup = false;
        for (const auto& ex : pool.specs) {
            if (ex.path == sp.path) { dup = true; break; }
        }
        if (dup) {
            LogD("config: duplicate sound '%s' ignored", sp.path.c_str());
            continue;
        }
        const int idx = (int)pool.specs.size();
        const bool wasPlain = sp.plain;
        pool.specs.push_back(std::move(sp));
        if (wasPlain) plainOrder.emplace_back(&pool, idx);
    }
}

std::wstring AbsFor(const std::string& rel);

void LoadConfig()
{
    // drop decoded wavs so edited files are re-read from disk
    { std::lock_guard<std::mutex> lk(gCacheMutex); gCache.clear(); }

    std::string txt;
    if (!ReadFileUtf8(gIniPath, txt) || txt.empty()) {
        player::gRoot = gPlayerRoot;
        {
            std::lock_guard<std::mutex> lk(gCfgMutex);
            gAttacks.clear();
            g_groups.clear();
        }
        g_combos.clear();
        g_activeCombo.clear();
        g_comboOrder.clear();
        Log("no config file; using defaults (PlayerRoot=0x%llX)",
            (unsigned long long)gPlayerRoot);
        return;
    }

    // ---- combo-aware parse ----
    // key 形式： "cb|<weapon>|<comboName>" 或 "def|<weapon>"(无组合的旧条目/默认组合)
    std::map<std::string, std::vector<Attack>> comboData;
    std::map<int, std::string> activeMap;                 // [Active] W<type>=<combo>
    bool inAttack = false, inCombo = false;
    int  curComboW = -1;
    std::string section, curComboName;
    Attack cur;
    std::vector<int> rawDelay, rawVol;
    std::vector<std::pair<Pool*, int>> plainOrder;

    auto finishEntry = [&]() {
        if (!inAttack) return;
        for (std::size_t i = 0; i < plainOrder.size(); ++i) {
            SoundSpec& sp = plainOrder[i].first->specs[plainOrder[i].second];
            if (i < rawDelay.size() && rawDelay[i] > 0) sp.delay = rawDelay[i];
            if (i < rawVol.size()) sp.vol = ClampInt(rawVol[i], 0, 100);
        }
        std::string key = (inCombo && curComboW >= 0)
                              ? ("cb|" + std::to_string(curComboW) + "|" + curComboName)
                              : ("def|" + std::to_string(cur.weaponType));
        comboData[key].push_back(std::move(cur));
        inAttack = false;
        cur = Attack();
        rawDelay.clear();
        rawVol.clear();
        plainOrder.clear();
    };

    std::size_t pos = 0;
    while (pos < txt.size()) {
        std::size_t eol = txt.find('\n', pos);
        if (eol == std::string::npos) eol = txt.size();
        std::string line = txt.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            finishEntry();
            section = Trim(line.substr(1, line.size() - 2));
            if (section.size() >= 6 && section.compare(0, 6, "Attack") == 0) {
                inAttack = true;          // 组合上下文(若有)延续
                cur = Attack();
            } else if (section == "Active") {
                inAttack = false; inCombo = false; curComboW = -1; curComboName.clear();
            } else if (section.size() > 6 && section.compare(0, 6, "Weapon") == 0) {
                // [Weapon<type>:<comboName>]
                std::string rest = section.substr(6);
                std::size_t c = rest.find(':');
                if (c != std::string::npos) {
                    curComboW = std::atoi(rest.substr(0, c).c_str());
                    curComboName = rest.substr(c + 1);
                    inCombo = true; inAttack = false;
                } else {
                    inCombo = false; curComboW = -1; curComboName.clear(); inAttack = false;
                }
            } else {
                // 其它段：重置组合上下文
                inCombo = false; curComboW = -1; curComboName.clear(); inAttack = false;
            }
            continue;
        }

        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (!inAttack) {
            if (section == "WeaponSoundEnhance") {
                if (key == "PlayerRoot")  gPlayerRoot = ParseHex(val);
                else if (key == "PollMs") { int v = std::atoi(val.c_str()); if (v >= 5) gPollMs = v; }
                else if (key == "DebounceMs") { int v = std::atoi(val.c_str()); if (v >= 0) gDebounceMs = v; }
                else if (key == "Volume") gVolumePct = ClampInt(std::atoi(val.c_str()), 0, 100);
                else if (key == "Enabled") gEnabled = std::atoi(val.c_str()) != 0;
                else if (key == "MoreSounds") gMoreSounds = std::atoi(val.c_str()) != 0;
                else if (key == "ChatEcho") g_useChatEcho = std::atoi(val.c_str()) != 0;
                else if (key == "ChatCommands") g_useChatCommands = std::atoi(val.c_str()) != 0;
                else if (key == "Hotkeys") g_hotkeysEnabled = std::atoi(val.c_str()) != 0;
                else if (key == "Debug") gDebug = std::atoi(val.c_str()) != 0;
                else if (key == "GaugePtrOff") { std::uintptr_t v = ParseHex(val); if (v) gGaugePtrOff = (std::uint32_t)v; }
                else if (key == "GaugeValOff") { std::uintptr_t v = ParseHex(val); if (v) gGaugeValOff = (std::uint32_t)v; }
            } else if (section == "Hotkeys") {
                if (key == "ModifierKey") gModifierKey = std::atoi(val.c_str());
                else if (key == "ReloadKey")   gReloadKey   = std::atoi(val.c_str());
                else if (key == "VolUpKey")    gVolUpKey    = std::atoi(val.c_str());
                else if (key == "VolDownKey")  gVolDownKey  = std::atoi(val.c_str());
                else if (key == "SetVolKey")   gSetVolKey   = std::atoi(val.c_str());
                else if (key == "ToggleKey")   gToggleKey   = std::atoi(val.c_str());
                else if (key == "MoreKey")     gMoreKey     = std::atoi(val.c_str());
                else if (key == "SetVolValue") gSetVolValue = ClampInt(std::atoi(val.c_str()), 0, 100);
                else if (key == "ComboKey")    gComboKey    = std::atoi(val.c_str());
            } else if (section == "Active") {
                // W<type>=<comboName>
                if (key.size() > 1 && (key[0] == 'W' || key[0] == 'w')) {
                    int w = std::atoi(key.c_str() + 1);
                    if (w >= -1) activeMap[w] = val;
                }
            }
            continue;
        }

        // ---- attack section keys ----
        if (key == "WeaponType") {
            cur.weaponType = std::atoi(val.c_str());
        } else if (key == "FSMId") {
            cur.fsmId = std::atoi(val.c_str());
        } else if (key == "ActionLMT" || key == "LMT") {
            std::vector<std::string> toks;
            SplitList(val, toks);
            for (const auto& t : toks) {
                int v = std::atoi(t.c_str());
                if (v == -1) continue;   // wildcard already covered by empty list
                bool dup = false;
                for (int x : cur.lmt) if (x == v) { dup = true; break; }
                if (!dup) cur.lmt.push_back(v);
            }
        } else if (key == "Name") {
            cur.name = val;
        } else if (key == "Group") {
            cur.group = val;
        } else if (key == "Sound") {
            AppendSpecs(cur.defPool, val, plainOrder);
        } else if (key.size() > 6 && key.compare(0, 6, "Sound:") == 0) {
            const int lvl = GaugeTagToLevel(ToLower(key.substr(6)));
            if (lvl >= 0 && lvl < 4)
                AppendSpecs(cur.gaugePool[lvl], val, plainOrder);
            else
                LogD("config: unknown gauge tag '%s' ignored", key.c_str());
        } else if (key == "SoundDelay") {
            std::vector<std::string> toks;
            SplitList(val, toks);
            for (const auto& t : toks)
                rawDelay.push_back(t.empty() ? 0 : std::atoi(t.c_str()));
        } else if (key == "SoundVol") {
            std::vector<std::string> toks;
            SplitList(val, toks);
            for (const auto& t : toks)
                rawVol.push_back(t.empty() ? 100 : std::atoi(t.c_str()));
        }
    }
    finishEntry();

    player::gRoot = gPlayerRoot;
    player::gGaugePtrOff = gGaugePtrOff;
    player::gGaugeValOff = gGaugeValOff;

    // 组装 per-weapon combos 结构（按出现顺序记 order）
    g_combos.clear();
    g_comboOrder.clear();
    for (const auto& kv : comboData) {
        const std::string& k = kv.first;
        int w = -1; std::string cname = "";
        if (k.compare(0, 4, "def|") == 0) {
            w = std::atoi(k.c_str() + 4);
            cname = "";
        } else if (k.compare(0, 3, "cb|") == 0) {
            std::string rest = k.substr(3);
            std::size_t p = rest.find('|');
            if (p != std::string::npos) {
                w = std::atoi(rest.substr(0, p).c_str());
                cname = rest.substr(p + 1);
            }
        }
        auto& vec = g_combos[w][cname];
        vec.insert(vec.end(), kv.second.begin(), kv.second.end());
        auto& order = g_comboOrder[w];
        bool has = false;
        for (const auto& o : order) if (o == cname) { has = true; break; }
        if (!has) order.push_back(cname);
    }
    // 默认组合 "" 放最前（保证热键从默认开始）
    for (auto& wo : g_comboOrder) {
        auto& order = wo.second;
        bool hasDef = false;
        for (const auto& o : order) if (o.empty()) { hasDef = true; break; }
        if (hasDef) {
            std::vector<std::string> sorted;
            sorted.push_back("");
            for (const auto& o : order) if (!o.empty()) sorted.push_back(o);
            order.swap(sorted);
        }
    }
    g_activeCombo = activeMap;

    RebuildActiveAttacks();
    Log("config: combos=%zu attacks=%zu", g_combos.size(), gAttacks.size());
}

void PreloadSounds();   // defined below

void ReloadConfig() { LoadConfig(); PreloadSounds(); }

// ===========================================================================
//  Wav cache / preload
// ===========================================================================

// Must be called with gCacheMutex held. Loads+decodes once, then returns ptr.
const audio::Wav* GetCached(const std::wstring& absPath)
{
    const std::string key = strconv::ToUtf8(absPath);
    for (auto& p : gCache)
        if (p.first == key) return &p.second;

    HANDLE h = ::CreateFileW(absPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log("wav not found: %s", key.c_str());
        return nullptr;
    }
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16LL << 20)) {
        ::CloseHandle(h);
        Log("wav size invalid: %s", key.c_str());
        return nullptr;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(sz.QuadPart));
    DWORD rd = 0;
    if (!::ReadFile(h, raw.data(), (DWORD)raw.size(), &rd, nullptr) || rd != (DWORD)raw.size()) {
        ::CloseHandle(h);
        return nullptr;
    }
    ::CloseHandle(h);

    audio::Wav w;
    if (!audio::ParseWav(raw.data(), raw.size(), w)) {
        Log("parse failed: %s", key.c_str());
        return nullptr;
    }
    if (w.sampleRate != 44100) {
        audio::ResampleTo(w, 44100);
        LogD("resampled: %s -> 44100", key.c_str());
    }
    LogD("loaded: %s | ch=%u rate=%u bits=%u bytes=%zu",
         key.c_str(), w.channels, w.sampleRate, w.bitsPer, w.data.size());
    gCache.emplace_back(std::move(key), std::move(w));
    return &gCache.back().second;
}

// Decode every configured wav up front so triggering never does file I/O.
void PreloadSounds()
{
    const std::vector<Attack> attacks = SnapshotAttacks();
    std::vector<std::string> absPaths;   // utf8, unique
    auto addSpecs = [&](const Pool& p) {
        for (const auto& sp : p.specs) {
            if (sp.path.empty()) continue;
            const std::string key = strconv::ToUtf8(AbsFor(sp.path));
            bool dup = false;
            for (const auto& e : absPaths) if (e == key) { dup = true; break; }
            if (!dup) absPaths.push_back(key);
        }
    };
    for (const auto& a : attacks) {
        addSpecs(a.defPool);
        for (int i = 0; i < 4; ++i) addSpecs(a.gaugePool[i]);
    }

    std::lock_guard<std::mutex> lk(gCacheMutex);
    for (const auto& p : absPaths) {
        bool cached = false;
        for (auto& e : gCache) if (e.first == p) { cached = true; break; }
        if (!cached) GetCached(strconv::ToWide(p));
    }
    Log("preload: %zu unique sound(s), cache=%zu", absPaths.size(), gCache.size());
}

// Relative "sounds/xxx.wav" -> absolute path below the module dir.
std::wstring AbsFor(const std::string& rel)
{
    std::wstring w = gModuleDir + strconv::ToWide(rel);
    for (auto& c : w) if (c == L'/') c = L'\\';
    std::wstring out;
    out.reserve(w.size());
    bool prevSlash = false;
    for (wchar_t c : w) {
        if (c == L'\\') {
            if (!prevSlash) out += c;
            prevSlash = true;
        } else {
            out += c;
            prevSlash = false;
        }
    }
    return out;
}

// ===========================================================================
//  In-game chat echo + /wse commands
// ===========================================================================

void ResolveGameBase()
{
    HMODULE mod = ::GetModuleHandleW(L"MonsterHunterWorld.exe");
    if (!mod) return;
    gGameBase = reinterpret_cast<std::uintptr_t>(mod);
    g_systemMessage = reinterpret_cast<SystemMessageFn>(gGameBase + kSystemMessageRva);
    Log("game base=0x%llX msgFn=0x%llX",
        (unsigned long long)gGameBase, (unsigned long long)g_systemMessage);
}

void ShowMessage(const char* utf8, bool emphasized = false)
{
    if (g_useChatEcho == 0) {
        Log("[echo] %s", utf8);
        return;
    }
    if (g_systemMessage == nullptr || gGameBase == 0) return;
    if (!player::RefreshIsInScene()) return;
    void* mgr = nullptr;
    if (!mem::ReadVal(gGameBase + kSystemMessageMgrRva, mgr) || mgr == nullptr) return;
    char msgbuf[0x180] = {};   // native fn copies 0x17f bytes at most
    _snprintf_s(msgbuf, _TRUNCATE, "%s", utf8);
    g_systemMessage(mgr, msgbuf, 0.0f, -1, emphasized);
}

inline bool HandleWseCommand(const std::string& rest, bool& used)
{
    used = true;
    if (rest.empty()) {
        ShowMessage("WeaponSound OK");
    } else if (rest == "reload" || rest == "re") {
        ReloadConfig();
        char msg[0x180] = {};
        _snprintf_s(msg, _TRUNCATE, "wse config reloaded (vol=%d)", (int)gVolumePct);
        ShowMessage(msg, true);
    } else if (rest == "on" || rest == "enable") {
        gEnabled = 1; ShowMessage("wse enabled", true);
    } else if (rest == "off" || rest == "disable") {
        gEnabled = 0; ShowMessage("wse disabled", true);
    } else if (rest == "more" || rest == "extra") {
        gMoreSounds = 1; ShowMessage("wse extra sounds ON", true);
    } else if (rest == "one" || rest == "single") {
        gMoreSounds = 0; ShowMessage("wse extra sounds OFF (single)", true);
    } else if (rest == "help" || rest == "h") {
        ShowMessage("/wse reload(重载ini+音效) | on | off | more | one | vol N | vol+ | vol-");
    } else if (rest == "vol+" || rest == "up") {
        gVolumePct += 5; if (gVolumePct > 100) gVolumePct = 100;
        char msg[0x180] = {}; _snprintf_s(msg, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
        ShowMessage(msg, true);
    } else if (rest == "vol-" || rest == "down") {
        gVolumePct -= 5; if (gVolumePct < 0) gVolumePct = 0;
        char msg[0x180] = {}; _snprintf_s(msg, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
        ShowMessage(msg, true);
    } else if (rest.rfind("vol", 0) == 0 || rest.rfind("v=", 0) == 0 ||
               (rest[0] >= '0' && rest[0] <= '9')) {
        std::string num = rest;
        if (num.rfind("vol", 0) == 0) num = num.substr(3);
        else if (num.rfind("v=", 0) == 0) num = num.substr(2);
        else if (num.rfind("v", 0) == 0) num = num.substr(1);
        num = Trim(num);
        int vv = std::atoi(num.c_str());
        if (vv >= 0 && vv <= 100) {
            gVolumePct = vv;
            char msg[0x180] = {};
            _snprintf_s(msg, _TRUNCATE, "wse volume=%d", vv);
            ShowMessage(msg, true);
        } else {
            ShowMessage("wse volume must be 0..100");
        }
    } else {
        ShowMessage("wse unknown command; try /wse help");
        used = false;
    }
    return used;
}

bool ParseCommand(const std::string& line)
{
    std::string s = Trim(line);
    if (s.empty() || s[0] != '/') return false;
    std::size_t sp = s.find_first_of(" \t");
    std::string cmd = (sp == std::string::npos) ? s.substr(1) : s.substr(1, sp - 1);
    std::string rest = (sp == std::string::npos) ? "" : Trim(s.substr(sp + 1));
    cmd = ToLower(cmd);
    if (cmd == "wse" || cmd == "wsesound" || cmd == "sound") {
        bool used = false;
        HandleWseCommand(rest, used);
        return true;
    }
    return false;
}

// Read the game's chat message buffer; /wse prefixed messages are executed and
// the buffer cleared. Pure memory reads, no function hooks.
bool PollChatCommand()
{
    if (g_useChatCommands == 0) return false;
    if (gGameBase == 0) return false;
    if (!player::RefreshIsInScene()) return false;

    std::uintptr_t base = 0;
    if (!mem::ReadVal(gGameBase + kMessageBaseRva, base) || base == 0) return false;

    std::uintptr_t lenPtr = base + kMessageLenOff;
    std::uintptr_t bodyPtr = base + kMessageBodyOff;
    if (!mem::IsReadable(lenPtr, 4) || !mem::IsReadable(bodyPtr, 4)) return false;

    std::int32_t len = 0;
    if (!mem::ReadVal(lenPtr, len) || len <= 0 || len > 1024) return false;

    std::string msg;
    if (mem::IsReadable(bodyPtr, (std::size_t)len + 1)) {
        msg.assign(reinterpret_cast<const char*>(bodyPtr), (std::size_t)len);
        while (!msg.empty() && (msg.back() == '\0' || msg.back() == '\n' || msg.back() == '\r'))
            msg.pop_back();
    }
    if (msg.empty()) return false;

    std::string trimmed = Trim(msg);
    if (trimmed.rfind("/wse", 0) == 0 || trimmed.rfind("/wsesound", 0) == 0) {
        static std::string lastHandled;
        static std::uint64_t lastHandledAt = 0;
        const std::uint64_t now = ::GetTickCount64();
        if (lastHandled == trimmed && (now - lastHandledAt) < 1000)
            return false;
        lastHandled = trimmed;
        lastHandledAt = now;
        LogD("[chat] '%s'", msg.c_str());
        if (mem::IsWritable(bodyPtr, (std::size_t)len + 1))
            std::memset(reinterpret_cast<void*>(bodyPtr), 0, (std::size_t)len + 1);
        if (mem::IsWritable(lenPtr, 4))
            std::memset(reinterpret_cast<void*>(lenPtr), 0, 4);
        return ParseCommand(msg);
    }
    return false;
}

// ===========================================================================
//  Trigger evaluation + playback selection
// ===========================================================================

// Per-file cooldown used by the random layer. Returns true when the file must
// be skipped; refreshes/registers the timestamp exactly like v1 did.
bool CooldownHit(const std::string& pathKey, std::uint64_t nowMs)
{
    for (auto& sc : soundCooldown) {
        if (sc.first == pathKey) {
            const bool cool = (nowMs - sc.second) < (std::uint64_t)kSoundCooldownMs;
            sc.second = nowMs;
            return cool;
        }
    }
    soundCooldown.push_back(std::make_pair(pathKey, nowMs));
    if (soundCooldown.size() > 64)
        soundCooldown.erase(soundCooldown.begin());
    return false;
}

// Play one spec. Caller already decided cooldown policy.
bool PlayOne(const SoundSpec& sp, const std::string& what)
{
    if (sp.path.empty()) return false;
    const std::wstring abs = AbsFor(sp.path);
    bool ok = false, hasWav = false;
    unsigned wch = 0, wrate = 0, wbits = 0;
    std::size_t wsz = 0;
    {
        std::lock_guard<std::mutex> lk(gCacheMutex);
        const audio::Wav* w = GetCached(abs);
        if (w) {
            hasWav = true;
            wch = w->channels; wrate = w->sampleRate; wbits = w->bitsPer;
            wsz = w->data.size();
            const float volF = ((float)gVolumePct / 100.0f) * ((float)sp.vol / 100.0f);
            ok = audio::g_audio.Play(*w, volF, (unsigned)sp.delay);
        }
    }
    if (!hasWav) {
        LogD("MISS: %s (%s)", what.c_str(), strconv::ToUtf8(abs).c_str());
        return false;
    }
    if (ok) {
        LogD("PLAY: %s -> %s | ch=%u rate=%u bits=%u bytes=%zu vol=%d",
             what.c_str(), strconv::ToUtf8(abs).c_str(), wch, wrate, wbits, wsz,
             (int)gVolumePct);
        return true;
    }
    LogD("waveOut rejected (hr=0x%08X), PlaySoundW fallback: %s",
         (unsigned)audio::g_audio.LastHr(), strconv::ToUtf8(abs).c_str());
    ok = ::PlaySoundW(abs.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
    LogD(ok ? "PLAY(fallback): %s" : "PLAYFAIL: %s", strconv::ToUtf8(abs).c_str());
    return ok;
}

// Fire one matched entry. Returns true when at least one sound played.
bool FireEntry(const Attack& e, int gauge, std::mt19937& rng, std::uint64_t nowMs,
               const std::string& tag)
{
    // choose pool: gauge level pool when present, otherwise default pool
    const Pool* pool = &e.defPool;
    bool usedGauge = false;
    if (gauge >= 0 && gauge <= 3 && !e.gaugePool[gauge].empty()) {
        pool = &e.gaugePool[gauge];
        usedGauge = true;
    }
    if (pool->empty()) return false;

    std::vector<int> fixedIdx, randomIdx;
    for (std::size_t i = 0; i < pool->specs.size(); ++i) {
        if (pool->specs[i].fixed) fixedIdx.push_back((int)i);
        else                      randomIdx.push_back((int)i);
    }

    bool any = false;
    const std::string what = tag + (usedGauge ? std::string(" gauge=") + std::to_string(gauge)
                                              : std::string(" gauge=default"));

    // fixed layer: always plays per action, but the same file is still subject
    // to the per-file cooldown so duplicate trigger entries cannot stack it.
    for (int i : fixedIdx) {
        const SoundSpec& sp = pool->specs[i];
        const std::string pathKey = strconv::ToUtf8(AbsFor(sp.path));
        if (CooldownHit(pathKey, nowMs)) {
            LogD("COOLDOWN(fixed): skip %s", pathKey.c_str());
            continue;
        }
        if (PlayOne(sp, what)) any = true;
    }

    // random layer: one pick from the non-fixed set; without any fixed sounds
    // this degenerates to the legacy whole-pool random (or first-only) mode.
    const bool hasFixed = !fixedIdx.empty();
    if (hasFixed && randomIdx.empty()) return any;   // only fixed sounds exist

    if (hasFixed) {
        const int n = (int)randomIdx.size();
        const int start = n > 1 ? std::uniform_int_distribution<int>(0, n - 1)(rng) : 0;
        for (int t = 0; t < n; ++t) {
            const SoundSpec& sp = pool->specs[randomIdx[(start + t) % n]];
            const std::string pathKey = strconv::ToUtf8(AbsFor(sp.path));
            if (CooldownHit(pathKey, nowMs)) continue;
            if (PlayOne(sp, what)) { any = true; break; }
        }
        return any;
    }

    // legacy mode: whole pool is the random set
    const int n = (int)pool->specs.size();
    int start = 0;
    int count = n;
    if (gMoreSounds) {
        if (n > 1) start = std::uniform_int_distribution<int>(0, n - 1)(rng);
    } else {
        count = 1;   // legacy: only the first sound
    }
    for (int t = 0; t < count; ++t) {
        const SoundSpec& sp = pool->specs[(start + t) % n];
        const std::string pathKey = strconv::ToUtf8(AbsFor(sp.path));
        if (CooldownHit(pathKey, nowMs)) continue;
        if (PlayOne(sp, what)) { any = true; break; }
    }
    return any;
}

// ===========================================================================
//  Worker threads
// ===========================================================================

DWORD WINAPI WorkerProc(LPVOID)
{
    while (::GetModuleHandleW(L"MonsterHunterWorld.exe") == nullptr &&
           ::InterlockedCompareExchange(&gStop, 0, 0) == 0)
        ::Sleep(500);
    if (gStop) return 0;

    Log("game module found, init audio");
    ResolveGameBase();
    audio::g_audio.Init();
    PreloadSounds();

    std::mt19937 rng(static_cast<unsigned>(::GetTickCount64() ^ 0x9E3779B9u));
    std::uint64_t lastHeartbeat = 0;
    bool firstState = true;

    while (::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        ::Sleep(static_cast<DWORD>(gPollMs));
        if (::InterlockedCompareExchange(&gStop, 0, 0) != 0) break;

        player::Refresh();
        const int lmt = player::gLmt;
        const int fsm = player::gFsm;
        const int weapon = player::gWeapon;
        const int gauge = player::gGauge;

        PollChatCommand();

        const std::uint64_t nowMs = ::GetTickCount64();
        if (firstState || (nowMs - lastHeartbeat >= 4000)) {
            LogD("state: weapon=%d fsm=%d lmt=%d gauge=%d vol=%d en=%d more=%d",
                 weapon, fsm, lmt, gauge, gVolumePct, gEnabled, gMoreSounds);
            lastHeartbeat = nowMs;
            firstState = false;
        }

        if (gEnabled == 0) continue;

        // Iterate the REAL container under the config lock. Entry latches
        // (inMatch) must persist across polls or the same action would be
        // re-fired on every poll once DebounceMs elapses. Reload swaps the
        // vector under the same lock, so iteration stays safe.
        {
            std::lock_guard<std::mutex> lk(gCfgMutex);

            // Re-arm action groups whose members have been quiet long enough.
            for (auto& g : g_groups) {
                if (g.second.fired && nowMs - g.second.lastSeen >= kGroupResetMs)
                    g.second.fired = false;
            }

            for (auto& e : gAttacks) {
                bool lmtOk = e.lmt.empty();
                if (!lmtOk)
                    for (int x : e.lmt) if (x == lmt) { lmtOk = true; break; }

                const bool match =
                    (e.weaponType < 0 || e.weaponType == weapon) &&
                    (e.fsmId < 0 || e.fsmId == fsm) &&
                    lmtOk &&
                    e.HasSounds();

                const std::string tag = "a[" + e.name + "]";

                if (!e.group.empty()) {
                    // Action-group entry: the whole group fires at most once
                    // per action occurrence. Gauge is taken at the first
                    // trigger instant, so a mid-action gauge change cannot
                    // double-fire.
                    GroupRt& st = GroupState(e.group);
                    if (match) st.lastSeen = nowMs;
                    if (!match || st.fired) continue;
                    st.fired = true;
                    if (nowMs - gLastTrigger < (std::uint64_t)gDebounceMs)
                        continue;
                    if (FireEntry(e, gauge, rng, nowMs, tag))
                        gLastTrigger = nowMs;
                    continue;
                }

                // Ungrouped entry: legacy per-entry edge-latch behaviour.
                if (match && !e.inMatch) {
                    e.inMatch = true;
                    if (nowMs - gLastTrigger < (std::uint64_t)gDebounceMs)
                        continue;
                    if (FireEntry(e, gauge, rng, nowMs, tag))
                        gLastTrigger = nowMs;
                } else if (!match) {
                    e.inMatch = false;
                }
            }
        }
    }
    return 0;
}

DWORD WINAPI HotkeyProc(LPVOID)
{
    bool reloadWas=false, upWas=false, downWas=false,
         setWas=false, togWas=false, moreWas=false, comboWas=false;
    while (::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        ::Sleep(30);
        if (g_hotkeysEnabled == 0) {
            reloadWas = upWas = downWas = setWas = togWas = moreWas = comboWas = false;
            continue;
        }
        const bool mod = (gModifierKey == 0) ||
                         ((::GetAsyncKeyState(gModifierKey) & 0x8000) != 0);
        if (!mod) {
            reloadWas = upWas = downWas = setWas = togWas = moreWas = comboWas = false;
            continue;
        }
        const bool reloadDown = (::GetAsyncKeyState(gReloadKey) & 0x8000) != 0;
        const bool upDown     = (::GetAsyncKeyState(gVolUpKey)  & 0x8000) != 0;
        const bool downDown   = (::GetAsyncKeyState(gVolDownKey)& 0x8000) != 0;
        const bool setDown    = (::GetAsyncKeyState(gSetVolKey) & 0x8000) != 0;
        const bool togDown    = (::GetAsyncKeyState(gToggleKey) & 0x8000) != 0;
        const bool moreDown   = (::GetAsyncKeyState(gMoreKey)   & 0x8000) != 0;
        const bool comboDown  = (::GetAsyncKeyState(gComboKey)  & 0x8000) != 0;

        if (reloadDown && !reloadWas) {
            ReloadConfig();
            char m[0x180] = {};
            _snprintf_s(m, _TRUNCATE, "wse reloaded (vol=%d)", (int)gVolumePct);
            ShowMessage(m, true);
            LogD("[hotkey] config reloaded (volume=%d enabled=%d more=%d)",
                 gVolumePct, gEnabled, gMoreSounds);
        }
        reloadWas = reloadDown;

        if (upDown && !upWas) {
            gVolumePct += 5; if (gVolumePct > 100) gVolumePct = 100;
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
            ShowMessage(m, true);
            LogD("[hotkey] volume up -> %d", gVolumePct);
        }
        upWas = upDown;

        if (downDown && !downWas) {
            gVolumePct -= 5; if (gVolumePct < 0) gVolumePct = 0;
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
            ShowMessage(m, true);
            LogD("[hotkey] volume down -> %d", gVolumePct);
        }
        downWas = downDown;

        if (setDown && !setWas) {
            gVolumePct = gSetVolValue;
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
            ShowMessage(m, true);
            LogD("[hotkey] volume set -> %d", gVolumePct);
        }
        setWas = setDown;

        if (togDown && !togWas) {
            gEnabled = gEnabled ? 0 : 1;
            ShowMessage(gEnabled ? "wse enabled" : "wse disabled", true);
            LogD("[hotkey] enabled toggled -> %d", gEnabled);
        }
        togWas = togDown;

        if (moreDown && !moreWas) {
            gMoreSounds = gMoreSounds ? 0 : 1;
            ShowMessage(gMoreSounds ? "wse extra sounds ON" : "wse extra sounds OFF", true);
            LogD("[hotkey] more-sounds toggled -> %d", gMoreSounds);
        }
        moreWas = moreDown;

        // 切换当前武器的配置组合（仅影响该武器；其它武器配置不变）
        if (comboDown && !comboWas) {
            const int w = player::gWeapon;
            std::string nxt;
            {
                std::lock_guard<std::mutex> lk(gCfgMutex);
                auto& order = g_comboOrder[w];
                if (!order.empty()) {
                    std::string cur = "";
                    auto a = g_activeCombo.find(w);
                    if (a != g_activeCombo.end()) cur = a->second;
                    nxt = order[0];
                    for (std::size_t k = 0; k < order.size(); ++k)
                        if (order[k] == cur) { nxt = order[(k + 1) % order.size()]; break; }
                    g_activeCombo[w] = nxt;
                    RebuildActiveAttacks();
                }
            }
            if (!nxt.empty()) {
                char m[0x180] = {};
                _snprintf_s(m, _TRUNCATE, "wse combo[%d] -> %s", w, nxt.c_str());
                ShowMessage(m, true);
                LogD("[hotkey] weapon %d combo -> %s", w, nxt.c_str());
            }
        }
        comboWas = comboDown;
    }
    return 0;
}

} // namespace plugin

// ===========================================================================
//  DLL entry point
// ===========================================================================
extern "C" __declspec(dllexport) BOOL WeaponSoundEnhance_IsInstalled() { return TRUE; }

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    using namespace plugin;
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = module;
        ::DisableThreadLibraryCalls(module);
        wchar_t self[MAX_PATH] = {};
        ::GetModuleFileNameW(module, self, MAX_PATH);
        const std::wstring selfw(self);
        std::size_t sl = selfw.find_last_of(L"\\/");
        gModuleDir = (sl == std::wstring::npos) ? std::wstring() : selfw.substr(0, sl + 1);
        gIniPath = ReplaceExt(selfw, L".ini");
        LogInit();
        LoadConfig();
        HANDLE t = ::CreateThread(nullptr, 0, &WorkerProc, nullptr, 0, nullptr);
        if (t) ::CloseHandle(t);
        HANDLE hk = ::CreateThread(nullptr, 0, &HotkeyProc, nullptr, 0, nullptr);
        if (hk) ::CloseHandle(hk);
    } else if (reason == DLL_PROCESS_DETACH) {
        ::InterlockedExchange(&gStop, 1);
        ::Sleep(1000);
    }
    return TRUE;
}
