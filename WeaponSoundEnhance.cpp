// ============================================================================
//  WeaponSoundEnhance.cpp
//  ----------------------------------------------------------------------------
//  Monster Hunter: World / Iceborne (15.23.00) native weapon-sound plugin.
//
//  FEATURES:
//    - When the player performs a "derived attack" (chain/combo melee move),
//      the plugin matches the configured action and plays a wav.
//    - Each attack entry can list MULTIPLE wav files; a random one is played.
//
//  STANDALONE (no Lua script engine / loader / game-audio prereqs):
//    - Does not hook the game's audio engine or call any game audio function.
//    - wav decode + playback is done in-process via winmm waveOut (built-in).
//    - Player action detection is a background polling thread, no code hook.
//
//  DETECTION (build 15.23.00; PlayerRoot is ini override-able):
//    global player pointer base   = 0x1450139A0 (default)
//    Entity (Player)   = *( *(base) + 0x50 )      [GetAddress(base, {0x50})]
//    action LMT        = *( *(Entity + 0x468) ) + 0xE9C4
//    fsmID             = *(Entity + 0x6278)
//    weapon data       = *( *( *(Entity+0xC0) + 0x8 ) + 0x78)
//    weapon type / id  = *(data + 0x2E8) / *(data + 0x2EC)
//
//  All memory reads are guarded; invalid addresses are skipped safely.
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
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <utility>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

// ===========================================================================
//  Memory-safe read utilities
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

// GetAddress semantics (multilevel pointer deref: addr=*(addr+off) for each off):
//   addr = base ; for each off: addr = *(uintptr*)(addr + off)
//   i.e. NO deref of base itself (base is the manager, first off is added to it).
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
//  Text conversion helpers
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

std::uintptr_t gRoot = 0x1450139A0ULL;   // default player base (ini override-able)
std::uintptr_t gManager = 0;             // debug: *(Root)
std::uintptr_t gEntity = 0;              // debug: *(Manager + 0x50)
std::int32_t   gLmt = -1;
std::int32_t   gFsm = -1;
std::int32_t   gWeapon = -1;
std::int32_t   gWeaponId = -1;

void Refresh()
{
    gLmt = -1; gFsm = -1; gWeapon = -1; gWeaponId = -1;
    gManager = 0; gEntity = 0;

    // Root deref once first (matches native aob.h: P0 = *(PlayerBasePool)).
    std::uintptr_t manager = 0;
    if (!mem::ReadVal(gRoot, manager) || manager == 0) return;
    gManager = manager;

    // Entity = *(Manager + 0x50)  --- GetAddress(base, {0x50}) semantics.
    const std::uint32_t c0[1] = {0x50};
    const std::uintptr_t entity = mem::Walk(manager, c0, 1);
    if (!entity) return;
    gEntity = entity;

    gFsm = mem::ReadI32(entity + 0x6278, -1);

    std::uintptr_t act = 0;
    if (mem::ReadVal(entity + 0x468, act) && act)
        gLmt = mem::ReadI32(act + 0xE9C4, -1);

    // Weapon data object: GetAddress(root, {0x50, 0xC0, 0x8, 0x78})
    static const std::uint32_t wd[3] = {0xC0, 0x8, 0x78};
    const std::uintptr_t data = mem::Walk(entity, wd, 3);
    if (data) {
        gWeapon   = mem::ReadI32(data + 0x2E8, -1);
        gWeaponId = mem::ReadI32(data + 0x2EC, -1);
    }
}

// 玩家是否处于场景中（实体指针非空，等同 mhw-toolkit 的 is_player_in_scene）。
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
//  Standalone WAV player (XAudio2)
// ===========================================================================
namespace audio
{
struct Wav
{
    std::vector<std::uint8_t> data;
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

// 把 PCM 16 位 wav 重采样到给定目标采样率(默认44100)。DirectSound 对非常规采样率
// (如 28000Hz)会拒绝，重采样到标准值后即可被接受，从而支持音量与并发。
void ResampleTo(Wav& w, std::uint32_t targetRate = 44100)
{
    if (!w.valid || w.sampleRate == targetRate) return;
    if (w.bitsPer != 16) return;   // 仅处理 16 位 PCM

    const std::size_t inSamples = (std::size_t)(w.data.size() / 2);   // 每样本2字节
    if (inSamples == 0) return;
    const std::size_t outSamples = (std::size_t)((double)inSamples * targetRate / w.sampleRate);
    if (outSamples == 0) return;

    const std::int16_t* in = reinterpret_cast<const std::int16_t*>(w.data.data());
    const double step = (double)inSamples / (double)outSamples;
    std::vector<std::int16_t> out(outSamples);
    for (std::size_t i = 0; i < outSamples; ++i) {
        double pos = (double)i * step;
        std::size_t idx = (std::size_t)pos;
        if (idx >= inSamples - 1) idx = inSamples - 1 == 0 ? 0 : inSamples - 1;
        double frac = pos - (double)idx;
        double v = in[idx] * (1.0 - frac) + (in[idx + 1 > inSamples - 1 ? inSamples - 1 : idx + 1]) * frac;
        out[i] = (std::int16_t)v;
    }
    w.data.resize(out.size() * 2);
    std::memcpy(w.data.data(), out.data(), out.size() * 2);
    w.sampleRate = targetRate;
}

// DirectSound 并发播放器：每个 .wav 都作为独立的二次缓冲区播放，由系统混音，
// 因此可以同时叠播多条音效；且不依赖游戏的音频引擎，独立出声。
// ---- winmm waveOut 多缓冲播放器 ----
// 每条音效：新开一个独立 waveOut 句柄 → 软件增益(按音量缩放 PCM) → 写入 → 播完释放。
// 由于每条音效独立句柄 + 独立线程，可**并发叠播**；且音量是**逐音效**的(软件增益)。
// 与 PlaySound 同源(winmm)，所以在能出声的机器上必定能响；不依赖游戏音频引擎/Lua。
class Engine
{
public:
    bool Init()
    {
        // waveOut 无需初始化 DSOUND；此函数仅用于验证 waveOut 可用。
        ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        mOk = true;
        return true;
    }

    // 播放：复制 PCM 并按 volume(0..1) 施加软件增益，然后异步用 waveOut 播出。
    bool Play(const Wav& w, float volume)
    {
        if (!w.valid || w.data.empty()) { mLastHr = E_INVALIDARG; return false; }
        if (w.bitsPer != 16) { mLastHr = E_INVALIDARG; return false; }   // 仅支持16位PCM

        // 把样本 + 格式打包，交给工作线程，避免静态竞态。
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

        HANDLE th = ::CreateThread(nullptr, 0, &PlayWorker_Host, ctx, 0, nullptr);
        if (!th) { delete ctx; mLastHr = E_OUTOFMEMORY; return false; }
        ::CloseHandle(th);
        return true;
    }

    void Cleanup() {}

    HRESULT LastHr() const { return mLastHr; }

private:
    struct PlayCtx {
        std::vector<std::int16_t> samples;
        WORD  channels = 2;
        DWORD rate = 44100;
        WORD  bits = 16;
    };

    static DWORD WINAPI PlayWorker_Host(LPVOID param)
    {
        std::unique_ptr<PlayCtx> ctx(static_cast<PlayCtx*>(param));
        PlayWorker(*ctx);
        return 0;
    }

    static void PlayWorker(PlayCtx& ctx)
    {
        if (ctx.samples.empty()) return;
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
        // 等待播放完
        while ((hdr.dwFlags & WHDR_DONE) == 0)
            Sleep(10);
        waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
        waveOutClose(hwo);
    }

private:
    bool mOk = false;
    HRESULT mLastHr = S_OK;
};

Engine g_audio;

} // namespace audio

// ===========================================================================
//  Plugin logic
// ===========================================================================
namespace plugin
{
HMODULE gModule = nullptr;
volatile LONG gStop = 0;

std::wstring gModuleDir;
std::wstring gIniPath;
std::wstring gLogPath;

// 游戏模块基址（用于 RVA -> 绝对地址）。
std::uintptr_t gGameBase = 0;

// --- 15.23.00 游戏内“蓝色系统消息框”函数（沿用 QuickEquipmentLoadout 确认的地址） ---
// 这是最初“能实现聊天框回显”的版本用的地址与调用约定。
const std::uintptr_t kSystemMessageRva = 0x1A540D0;
const std::uintptr_t kSystemMessageMgrRva = 0x500CE70;   // 系统消息管理器指针
typedef void (*SystemMessageFn)(void* manager, const char* utf8Buffer,
                                float duration, std::int32_t messageId, bool emphasized);
SystemMessageFn g_systemMessage = nullptr;

// --- 15.23.00 游戏聊天消息缓冲（MESSAGE_BASE，收到/发送的消息文本都在这里） ---
// 绝对 0x144F87FF0，RVA = 0x4F87FF0。正文 = *(base)+0xC0，长度 = *(base)+0xBC。
const std::uintptr_t kMessageBaseRva = 0x4F87FF0;
const std::uintptr_t kMessageLenOff  = 0xBC;
const std::uintptr_t kMessageBodyOff = 0xC0;

// --- 游戏内聊天输入挂钩地址（0 = 尚未提供，需用 CE 定位后填入） ---

std::uintptr_t gPlayerRoot = 0x1450139A0ULL;
int gPollMs = 60;
int gDebounceMs = 120;
volatile int gVolumePct = 100;
volatile int gEnabled = 1;
// 播放方式：1 = 用 winmm PlaySound（绝大多数情况都能响，单路）；
//            0 = 用 DirectSound（可并发叠播，但部分机器/驱动会失败）。
volatile int g_usePlaySound = 1;
// 游戏内聊天栏回显：1 = 用 QuickEquipmentLoadout 确认过的聊天函数显示结果（可用）。
volatile int g_useChatEcho = 1;
// 游戏聊天框 /wse 指令：读取游戏聊天消息缓冲。
volatile int g_useChatCommands = 1;

// --- 热键（默认 Ctrl 组合，可在 ini [Hotkeys] 覆盖） ---
int gModifierKey = VK_CONTROL;   // 0 = 不要求修饰键
int gReloadKey   = VK_F5;        // 重载 ini
int gVolUpKey    = VK_UP;        // 音量 +
int gVolDownKey  = VK_DOWN;      // 音量 -
int gSetVolKey   = VK_F8;        // 设定具体音量为 gSetVolValue
int gToggleKey   = VK_F9;        // 开关（启用/停用音效）
int gMoreKey     = VK_F10;       // 切换“更多备选音效”
int gMoreSounds  = 1;            // 是否使用每条目里的额外音效（0=只取第一条）
int gSetVolValue = 50;           // gSetVolKey 被按时设定的音量
bool gHotkeyInit = false;

struct AttackEntry
{
    std::int32_t weaponType = -1;
    std::int32_t actionLmt  = -1;
    std::int32_t fsmId      = -1;
    std::vector<std::string> sounds;
    // 每个动作条目独立地“是否已触发过”的锁存，防止同一次攻击重复触发。
    bool inMatch = false;
};
std::vector<AttackEntry> attacks;
std::vector<std::pair<std::string, audio::Wav> > cache;
// 同一音效文件在最近 N 毫秒内不重复播放（防止同一次攻击经由不同条目触发两次）。
std::vector<std::pair<std::string, std::uint64_t> > soundCooldown;
const int kSoundCooldownMs = 400;

void Log(const char* fmt, ...);
void ApplyVolume();

void LogInit()
{
    gLogPath = gModuleDir + L"WeaponSoundEnhance.log";
    ::DeleteFileW(gLogPath.c_str());
    Log("WeaponSoundEnhance 1.0.0 starting");
}
void Log(const char* fmt, ...)
{
    char buf[2048] = {};
    va_list ap; va_start(ap, fmt); vsnprintf_s(buf, _TRUNCATE, fmt, ap); va_end(ap);
    ::OutputDebugStringA(buf);
    FILE* f = nullptr;
    if (_wfopen_s(&f, gLogPath.c_str(), L"ab") == 0 && f) {
        SYSTEMTIME st{}; ::GetLocalTime(&st);
        fprintf(f, "[%02u:%02u:%02u.%03u] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

std::wstring ReplaceExt(const std::wstring& path, const wchar_t* newExt)
{
    std::wstring p = path;
    std::size_t dot = p.find_last_of(L'.');
    std::size_t slash = p.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return p + newExt;
    return p.substr(0, dot) + newExt;
}

// 列出 sounds\ 目录下的 .wav 文件并打印，用于判断音效文件是否存在。
void ScanSoundsFolder()
{
    std::wstring dir = gModuleDir + L"sounds";
    std::wstring pattern = dir + L"\\*.wav";
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    int count = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                Log("  found wav: %s", strconv::ToUtf8(fd.cFileName).c_str());
                ++count;
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    Log("sounds dir '%s' has %d .wav file(s)", strconv::ToUtf8(dir).c_str(), count);
}

// 以 UTF-8 读取整个文本文件（用于配置文件），自动跳过 UTF-8 BOM。
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

inline std::string Trim(const std::string& s)
{
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 逐行解析 INI。允许 UTF-8 中文值。sections 用 name 匹配。
// onValue(name, key, valueUtf8, ctx) 在遇到 key=value 时回调。
typedef void (*IniValueCb)(const std::string& name, const std::string& key, const std::string& value, void* ctx);

void WalkIni(const std::string& txt, IniValueCb cb, void* ctx)
{
    std::string section;   // 当前段名（不含方括号）
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
            section = line.substr(1, line.size() - 2);
            continue;
        }
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (!key.empty()) cb(section, key, val, ctx);
    }
}

// LoadConfig 的回调上下文
struct ConfigCtx {
    int currentAttack = 0;   // 0 = 不在 Attack 段
};

void OnIniValue(const std::string& section, const std::string& key, const std::string& value, void* ctx)
{
    (void)ctx;
    bool isGlobal  = (section == "WeaponSoundEnhance");
    bool isHotkeys = (section == "Hotkeys");
    bool isAttack  = (section.size() > 6 && section.compare(0, 6, "Attack") == 0);

    if (isGlobal) {
        if (key == "PlayerRoot") {
            const char* s = value.c_str();
            while (*s == ' ' || *s == '\t') ++s;
            if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) s += 2;
            unsigned long long hv = 0;
            if (sscanf_s(s, "%llx", &hv) == 1)
                gPlayerRoot = static_cast<std::uintptr_t>(hv);
        } else if (key == "PollMs") {
            int v = atoi(value.c_str()); if (v >= 5) gPollMs = v;
        } else if (key == "DebounceMs") {
            int v = atoi(value.c_str()); if (v >= 0) gDebounceMs = v;
        } else if (key == "Volume") {
            int v = atoi(value.c_str()); if (v < 0) v = 0; if (v > 100) v = 100; gVolumePct = v;
        } else if (key == "Enabled") {
            gEnabled = atoi(value.c_str()) != 0;
        } else if (key == "MoreSounds") {
            gMoreSounds = atoi(value.c_str()) != 0;
        } else if (key == "Playback") {
            // "playsound"/"ps" => 1 ; "dsound"/"ds" => 0
            std::string v = value; for (auto& c : v) if (c >= 'A' && c <= 'Z') c += 32;
            if (v == "dsound" || v == "ds") g_usePlaySound = 0;
            else g_usePlaySound = 1;
        } else if (key == "ChatEcho") {
            g_useChatEcho = atoi(value.c_str()) != 0;
        } else if (key == "ChatCommands") {
            g_useChatCommands = atoi(value.c_str()) != 0;
        }
        return;
    }

    if (isHotkeys) {
        if      (key == "ModifierKey") gModifierKey = atoi(value.c_str());
        else if (key == "ReloadKey")   gReloadKey   = atoi(value.c_str());
        else if (key == "VolUpKey")    gVolUpKey    = atoi(value.c_str());
        else if (key == "VolDownKey")  gVolDownKey  = atoi(value.c_str());
        else if (key == "SetVolKey")   gSetVolKey   = atoi(value.c_str());
        else if (key == "ToggleKey")   gToggleKey   = atoi(value.c_str());
        else if (key == "MoreKey")     gMoreKey     = atoi(value.c_str());
        else if (key == "SetVolValue") { int v = atoi(value.c_str()); if (v<0)v=0; if(v>100)v=100; gSetVolValue = v; }
        return;
    }

    if (!isAttack) return;

    // 段名 Attack<N>
    int idx = atoi(section.c_str() + 6);
    if (idx < 1) return;
    // 保证 attacks 至少到 idx
    while ((int)attacks.size() < idx) attacks.push_back(AttackEntry());
    AttackEntry& e = attacks[idx - 1];
    if (key == "WeaponType")      e.weaponType = atoi(value.c_str());
    else if (key == "ActionLMT")  e.actionLmt  = atoi(value.c_str());
    else if (key == "FSMId")      e.fsmId      = atoi(value.c_str());
    else if (key == "Sound") {
        // 分号或逗号分隔，保留中文（value 已是 UTF-8 字节）
        std::string cur;
        for (std::size_t i = 0; i <= value.size(); ++i) {
            char cc = (i < value.size()) ? value[i] : '\0';
            if (cc == '\0' || cc == ';' || cc == ',') {
                std::string tok = Trim(cur);
                if (!tok.empty()) e.sounds.push_back(tok);
                cur.clear();
                if (cc == '\0') break;
            } else {
                cur += cc;
            }
        }
    }
}

void LoadConfig()
{
    std::string txt;
    if (!ReadFileUtf8(gIniPath, txt) || txt.empty()) {
        // 没有配置文件时用内置默认
        player::gRoot = gPlayerRoot;
        Log("no config file; using defaults (PlayerRoot=0x%llX)",
            (unsigned long long)gPlayerRoot);
        return;
    }

    attacks.clear();
    ConfigCtx ctx;
    WalkIni(txt, &OnIniValue, &ctx);

    player::gRoot = gPlayerRoot;
    Log("config: PlayerRoot=0x%llX PollMs=%d DebounceMs=%d Volume=%d Enabled=%d MoreSounds=%d attacks=%zu",
        (unsigned long long)gPlayerRoot, gPollMs, gDebounceMs, gVolumePct, gEnabled, gMoreSounds, attacks.size());
    ApplyVolume();   // 应用音量到 wave 输出
    Log("hotkeys: Mod=%d Reload=%d VolUp=%d VolDown=%d SetVol=%d Toggle=%d More=%d SetVal=%d",
        gModifierKey, gReloadKey, gVolUpKey, gVolDownKey, gSetVolKey, gToggleKey, gMoreKey, gSetVolValue);
}

// 热键重载入口（等价于重新 LoadConfig）
void ReloadConfig() { LoadConfig(); }

// 解析游戏模块基址并解析系统消息函数指针。
void ResolveGameBase()
{
    HMODULE mod = ::GetModuleHandleW(L"MonsterHunterWorld.exe");
    if (!mod) return;
    gGameBase = reinterpret_cast<std::uintptr_t>(mod);
    // 用 QuickEquipmentLoadout 确认过的 15.23.00 系统消息函数地址（用于聊天栏回显）。
    const std::uintptr_t fn = gGameBase + kSystemMessageRva;
    g_systemMessage = reinterpret_cast<SystemMessageFn>(fn);
    Log("game base=0x%llX msgFn=0x%llX",
        (unsigned long long)gGameBase, (unsigned long long)g_systemMessage);
}

// 在游戏内聊天/系统消息框显示文本（emphasized 用紫色(1)，否则蓝色(0)）。
// 仅当 g_useChatEcho=1 且玩家在场景内时才调用原生函数；否则只写日志，避免崩溃。
void ShowMessage(const char* utf8, bool emphasized = false)
{
    if (g_useChatEcho == 0) {
        Log("[echo] %s", utf8);
        return;
    }
    if (g_systemMessage == nullptr || gGameBase == 0) return;
    if (!player::RefreshIsInScene()) return;   // 非场景状态(如标题/加载)不调用
    void* mgr = nullptr;
    if (!mem::ReadVal(gGameBase + kSystemMessageMgrRva, mgr) || mgr == nullptr) return;
    char msgbuf[0x180] = {};   // 原生函数固定复制 0x17f 字节，给足 0x180
    _snprintf_s(msgbuf, _TRUNCATE, "%s", utf8);
    g_systemMessage(mgr, msgbuf, 0.0f, -1, emphasized);
}

// 音量设置(软件增益)在每次 Play 时按 gVolumePct 施加到该条音效样本上。
// 这里不再调用 waveOutSetVolume(它会改游戏总音量)；仅做日志。
void ApplyVolume()
{
    Log("volume set -> %d (per-sound software gain)", (int)gVolumePct);
}

// 解析一条 /wse <cmd> 指令并执行。返回 true 表示已识别。
// 目前由热键线程触发（未来接入游戏聊天输入后，可从聊天框文本调用）。
bool ParseCommand(const std::string& line);

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
        ShowMessage("/wse reload | on | off | more | one | vol N | vol+ | vol- | v");
    } else if (rest == "vol+" || rest == "up") {
        gVolumePct += 5; if (gVolumePct > 100) gVolumePct = 100;
        ApplyVolume();
        char msg[0x180] = {}; _snprintf_s(msg, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
        ShowMessage(msg, true);
    } else if (rest == "vol-" || rest == "down") {
        gVolumePct -= 5; if (gVolumePct < 0) gVolumePct = 0;
        ApplyVolume();
        char msg[0x180] = {}; _snprintf_s(msg, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
        ShowMessage(msg, true);
    } else if (rest.rfind("vol", 0) == 0 || rest.rfind("v=", 0) == 0 ||
               (rest[0] >= '0' && rest[0] <= '9')) {
        // 先去掉前缀，再取数字。例如 "vol 5" -> "5"；"v 50" -> "50"；"10" -> "10"。
        std::string num = rest;
        if (num.rfind("vol", 0) == 0) num = num.substr(3);
        else if (num.rfind("v=", 0) == 0) num = num.substr(2);
        else if (num.rfind("v", 0) == 0) num = num.substr(1);
        num = Trim(num);   // 只 trim 空白，不再按空格截断
        int vv = atoi(num.c_str());
        if (vv >= 0 && vv <= 100) {
            gVolumePct = vv;
            ApplyVolume();
            char msg[0x180] = {}; _snprintf_s(msg, _TRUNCATE, "wse volume=%d", vv);
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
    for (auto& c : cmd) if (c >= 'A' && c <= 'Z') c += 32;
    if (cmd == "wse" || cmd == "wsesound" || cmd == "sound") {
        bool used = false;
        HandleWseCommand(rest, used);
        return true;
    }
    return false;
}

// 读取游戏聊天消息缓冲（mhw-toolkit ChatMessageReceiver 同款机制），
// 若内容以 /wse 开头，则执行并把缓冲清空。返回是否有指令被消费。
// 无需挂钩游戏函数，纯读内存——你在游戏聊天框里发送的消息文本会出现在这里。
bool PollChatCommand()
{
    if (g_useChatCommands == 0) return false;   // 默认关闭，避免读游戏内存导致崩溃
    if (gGameBase == 0) return false;
    // 只在玩家场景内读聊天缓冲，避免在标题/加载等状态读取不稳定内存。
    if (!player::RefreshIsInScene()) return false;
    // 正文指针 = *(MESSAGE_BASE) + 0xC0 ；长度 = *(MESSAGE_BASE) + 0xBC
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
        // 去掉尾部可能存在的 NUL/换行
        while (!msg.empty() && (msg.back() == '\0' || msg.back() == '\n' || msg.back() == '\r'))
            msg.pop_back();
    }
    if (msg.empty()) return false;

    std::string trimmed = Trim(msg);
    if (trimmed.rfind("/wse", 0) == 0 || trimmed.rfind("/wsesound", 0) == 0) {
        // 时间窗去重（双保险）：1 秒内同文本消息视为重复跳过，避免同一条物理消息反复读。
        static std::string lastHandled;
        static std::uint64_t lastHandledAt = 0;
        std::uint64_t now = ::GetTickCount64();
        if (lastHandled == trimmed && (now - lastHandledAt) < 1000)
            return false;
        lastHandled = trimmed;
        lastHandledAt = now;
        Log("[chat] '%s'", msg.c_str());
        // 处理后清空消息缓冲（mhw-toolkit 同款做法），下次读到就是新消息。
        // 仅在确认“确实可写”时清空，避免写只读页崩溃。
        if (mem::IsWritable(bodyPtr, (std::size_t)len + 1))
            std::memset(reinterpret_cast<void*>(bodyPtr), 0, (std::size_t)len + 1);
        if (mem::IsWritable(lenPtr, 4))
            std::memset(reinterpret_cast<void*>(lenPtr), 0, 4);
        return ParseCommand(msg);
    }
    return false;
}


const std::wstring AbsFor(const std::string& rel)
{
    // gModuleDir 已以 '\' 结尾；直接拼相对路径并归一化分隔符，避免出现 "\\"。
    std::wstring w = gModuleDir + strconv::ToWide(rel);
    for (auto& c : w) if (c == L'/') c = L'\\';
    // 折行双反斜杠（若有）。
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

const audio::Wav* GetCached(const std::wstring& absPath)
{
    std::string key = strconv::ToUtf8(absPath);
    for (auto& p : cache)
        if (p.first == key) return &p.second;

    HANDLE h = ::CreateFileW(absPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log("wav not found: %s", strconv::ToUtf8(absPath).c_str());
        return nullptr;
    }
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16LL << 20)) {
        ::CloseHandle(h);
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
        Log("parse failed: %s", strconv::ToUtf8(absPath).c_str());
        return nullptr;
    }
    if (w.sampleRate != 44100) {
        audio::ResampleTo(w, 44100);
        Log("resampled: %s -> rate=44100", strconv::ToUtf8(absPath).c_str());
    }
    Log("loaded: %s | ch=%u rate=%u bits=%u bytes=%zu",
        strconv::ToUtf8(absPath).c_str(), w.channels, w.sampleRate, w.bitsPer, w.data.size());
    cache.emplace_back(std::move(key), std::move(w));
    return &cache.back().second;
}

DWORD WINAPI WorkerProc(LPVOID)
{
    while (::GetModuleHandleW(L"MonsterHunterWorld.exe") == nullptr &&
           ::InterlockedCompareExchange(&gStop, 0, 0) == 0)
        ::Sleep(500);
    if (gStop) return 0;

    Log("game module found, init audio");
    ResolveGameBase();
    if (!audio::g_audio.Init()) {
        Log("winmm/waveOut init failed");
    } else {
        Log("audio ready (waveOut multi-buffer)");
    }
    ScanSoundsFolder();

    std::mt19937 rng(static_cast<unsigned>(::GetTickCount64() ^ 0x9E3779B9u));
    std::uint64_t lastTrigger = 0;
    std::uint64_t lastHeartbeat = 0;
    bool firstState = true;

    while (::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        ::Sleep(static_cast<DWORD>(gPollMs));
        if (::InterlockedCompareExchange(&gStop, 0, 0) != 0) break;

        player::Refresh();
        const int lmt = player::gLmt;
        const int fsm = player::gFsm;
        const int weapon = player::gWeapon;

        // 处理游戏聊天框里输入的 /wse 指令（纯读消息缓冲，无需挂钩）。
        PollChatCommand();

        // 心跳日志：大约每 2 秒打印一次当前读到的状态，方便判断内存链是否生效。
        const std::uint64_t nowMs = ::GetTickCount64();
        if (firstState || (nowMs - lastHeartbeat >= 4000)) {
            Log("state: weapon=%d fsm=%d lmt=%d vol=%d en=%d more=%d manager=0x%llX entity=0x%llX",
                weapon, fsm, lmt, gVolumePct, gEnabled, gMoreSounds,
                (unsigned long long)player::gManager,
                (unsigned long long)player::gEntity);
            lastHeartbeat = nowMs;
            firstState = false;
        }

        // 定期清理已播放完的 DirectSound 缓冲区。
        audio::g_audio.Cleanup();

        if (gEnabled == 0) { continue; }

        // 每个条目独立判定"是否匹配当前动作"，用锁存保证只在匹配沿触发一次。
        for (auto& e : attacks) {
            const bool match =
                (e.weaponType < 0 || e.weaponType == weapon) &&
                (e.actionLmt  < 0 || e.actionLmt  == lmt) &&
                (e.fsmId      < 0 || e.fsmId      == fsm) &&
                !e.sounds.empty();

            if (match && !e.inMatch) {
                e.inMatch = true;
                if (nowMs - lastTrigger < (std::uint64_t)gDebounceMs)
                    continue;

                // gMoreSounds=0 时只取第一条，否则随机抽。
                int useCount = static_cast<int>(e.sounds.size());
                if (!gMoreSounds) useCount = 1;
                int start = (gMoreSounds && useCount > 1)
                            ? std::uniform_int_distribution<int>(0, useCount - 1)(rng)
                            : 0;
                bool played = false;
                for (int t = 0; t < useCount; ++t) {
                    int idx = (start + t) % useCount;
                    std::wstring abs = AbsFor(e.sounds[idx]);
                    std::string pathKey = strconv::ToUtf8(abs);

                    // 冷却：同一音效文件在 kSoundCooldownMs 内不重复播放。
                    bool inCool = false;
                    for (auto& sc : soundCooldown) {
                        if (sc.first == pathKey) {
                            if (nowMs - sc.second < (std::uint64_t)kSoundCooldownMs)
                                inCool = true;
                            sc.second = nowMs;   // 刷新时间戳
                            break;
                        }
                    }
                    if (!inCool) {
                        soundCooldown.push_back(std::make_pair(pathKey, nowMs));
                        if (soundCooldown.size() > 64)   // 防止无限增长
                            soundCooldown.erase(soundCooldown.begin());
                    }
                    if (inCool) {
                        Log("COOLDOWN: skip %s", pathKey.c_str());
                        continue;
                    }

                    const audio::Wav* w = GetCached(abs);
                    if (w) {
                        bool ok = false;
                        // 主路径：winmm waveOut 多缓冲 + 软件增益（并发+每音效独立音量）。
                        ok = audio::g_audio.Play(*w, gVolumePct / 100.0f);
                        if (ok) {
                            Log("PLAY: fsm=%d lmt=%d weapon=%d -> %s | fmt ch=%u rate=%u bits=%u bytes=%zu vol=%d",
                                fsm, lmt, weapon, strconv::ToUtf8(abs).c_str(),
                                w->channels, w->sampleRate, w->bitsPer, w->data.size(), (int)gVolumePct);
                        } else {
                            // 兜底：PlaySound（单路，但一定能响）。
                            Log("waveOut Play rejected: valid=%d bits=%u ch=%u rate=%u bytes=%zu hr=0x%08X",
                                (int)w->valid, (unsigned)w->bitsPer, (unsigned)w->channels,
                                (unsigned)w->sampleRate, w->data.size(), (unsigned)audio::g_audio.LastHr());
                            ok = ::PlaySoundW(abs.c_str(), nullptr,
                                              SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
                            Log(ok ? "PLAY(PlaySound-fallback): %s" : "PLAYFAIL: %s",
                                strconv::ToUtf8(abs).c_str());
                        }
                        if (ok) {
                            played = true;
                            break;
                        }
                    } else {
                        Log("MISS: at %s (trying next)", strconv::ToUtf8(abs).c_str());
                    }
                }
                if (played) lastTrigger = nowMs;
            } else if (!match) {
                e.inMatch = false;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
//  热键线程：在游戏里通过 Ctrl+组合键 修改配置 / 重载 / 调音量。
//  默认：Ctrl+F5 重载 ini；Ctrl+↑ 音量+5；Ctrl+↓ 音量-5；
//        Ctrl+F8 设定音量为 SetVolValue；Ctrl+F9 开关；Ctrl+F10 切换更多音效。
//  这些配置项可在 ini 的 [Hotkeys] 段覆盖。
// ---------------------------------------------------------------------------
DWORD WINAPI HotkeyProc(LPVOID)
{
    bool reloadWas=false, upWas=false, downWas=false,
         setWas=false, togWas=false, moreWas=false;
    if (gHotkeyInit == false) {
        gHotkeyInit = true;
    }
    while (::InterlockedCompareExchange(&gStop, 0, 0) == 0) {
        ::Sleep(30);
        const bool mod = (gModifierKey == 0) ||
                         ((::GetAsyncKeyState(gModifierKey) & 0x8000) != 0);
        if (!mod) {
            reloadWas = upWas = downWas = setWas = togWas = moreWas = false;
            continue;
        }
        const bool reloadDown = (::GetAsyncKeyState(gReloadKey) & 0x8000) != 0;
        const bool upDown     = (::GetAsyncKeyState(gVolUpKey)  & 0x8000) != 0;
        const bool downDown   = (::GetAsyncKeyState(gVolDownKey)& 0x8000) != 0;
        const bool setDown    = (::GetAsyncKeyState(gSetVolKey) & 0x8000) != 0;
        const bool togDown    = (::GetAsyncKeyState(gToggleKey) & 0x8000) != 0;
        const bool moreDown   = (::GetAsyncKeyState(gMoreKey)   & 0x8000) != 0;

        if (reloadDown && !reloadWas) {
            ReloadConfig();
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse reloaded (vol=%d)", (int)gVolumePct);
            ShowMessage(m, true);
            Log("[hotkey] config reloaded (volume=%d enabled=%d more=%d)",
                gVolumePct, gEnabled, gMoreSounds);
        }
        reloadWas = reloadDown;

        if (upDown && !upWas) {
            gVolumePct += 5; if (gVolumePct > 100) gVolumePct = 100;
            ApplyVolume();
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
            ShowMessage(m, true);
            Log("[hotkey] volume up -> %d", gVolumePct);
        }
        upWas = upDown;

        if (downDown && !downWas) {
            gVolumePct -= 5; if (gVolumePct < 0) gVolumePct = 0;
            ApplyVolume();
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
            ShowMessage(m, true);
            Log("[hotkey] volume down -> %d", gVolumePct);
        }
        downWas = downDown;

        if (setDown && !setWas) {
            gVolumePct = gSetVolValue;
            ApplyVolume();
            char m[0x180] = {}; _snprintf_s(m, _TRUNCATE, "wse volume=%d", (int)gVolumePct);
            ShowMessage(m, true);
            Log("[hotkey] volume set -> %d", gVolumePct);
        }
        setWas = setDown;

        if (togDown && !togWas) {
            gEnabled = gEnabled ? 0 : 1;
            ShowMessage(gEnabled ? "wse enabled" : "wse disabled", true);
            Log("[hotkey] enabled toggled -> %d", gEnabled);
        }
        togWas = togDown;

        if (moreDown && !moreWas) {
            gMoreSounds = gMoreSounds ? 0 : 1;
            ShowMessage(gMoreSounds ? "wse extra sounds ON" : "wse extra sounds OFF", true);
            Log("[hotkey] more-sounds toggled -> %d", gMoreSounds);
        }
        moreWas = moreDown;
    }
    return 0;
}

// ---------------------------------------------------------------------------
//  指令输入线程：按住 Ctrl 时，把打出的字母/数字收进缓冲，回车执行。
//  例如按住 Ctrl 输入 ` /wse vol 50 ` 然后回车，即可在游戏里改音量。
//  不挂钩游戏聊天函数，纯读键盘状态（带按键沿检测），稳妥可用。
// ---------------------------------------------------------------------------
} // namespace plugin

// ===========================================================================
//  DLL entry point
// ===========================================================================
extern "C" __declspec(dllexport) BOOL WeaponSoundEnhance_IsInstalled() { return TRUE; }

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        plugin::gModule = module;
        ::DisableThreadLibraryCalls(module);
        wchar_t self[MAX_PATH] = {};
        ::GetModuleFileNameW(module, self, MAX_PATH);
        std::wstring selfw(self);
        std::size_t sl = selfw.find_last_of(L"\\/");
        plugin::gModuleDir = (sl == std::wstring::npos) ? std::wstring() : selfw.substr(0, sl + 1);
        plugin::gIniPath = plugin::ReplaceExt(selfw, L".ini");
        plugin::LogInit();
        plugin::LoadConfig();
        HANDLE t = ::CreateThread(nullptr, 0, &plugin::WorkerProc, nullptr, 0, nullptr);
        if (t) ::CloseHandle(t);
        HANDLE hk = ::CreateThread(nullptr, 0, &plugin::HotkeyProc, nullptr, 0, nullptr);
        if (hk) ::CloseHandle(hk);
    } else if (reason == DLL_PROCESS_DETACH) {
        ::InterlockedExchange(&plugin::gStop, 1);
        ::Sleep(1000);
    }
    return TRUE;
}