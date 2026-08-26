#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 一条派生攻击音效条目
struct SoundEntry {
    int weaponType = -1;           // 0..13，-1 = 任意
    int actionLmt  = -1;           // 动作 LMT，-1 = 不限
    int fsmId      = -1;           // 动作状态机 ID，-1 = 不限
    std::string name;              // 显示名（写入 Name=，插件会忽略该键）
    std::vector<std::string> sounds;   // "sounds/xxx.wav"
    std::vector<int> delays;       // 每条音效的播放延时(ms)，与 sounds 一一对应；缺省 0 = 立即
    std::vector<int> vols;         // 每条音效的音量(0..100)，与 sounds 一一对应；100 = 相对主音量无额外调整
};

struct GlobalSettings {
    std::string playerRoot = "0x1450139A0";
    int pollMs = 60;
    int debounceMs = 120;
    int volume = 50;
    int enabled = 1;
    int moreSounds = 1;
    std::string playback = "dsound";   // dsound | playsound
    int chatEcho = 1;
    int chatCommands = 1;
};

struct Hotkeys {
    int modifierKey = 17;   // Ctrl
    int reloadKey = 116;    // F5
    int volUpKey = 38;      // Up
    int volDownKey = 40;    // Down
    int setVolKey = 119;    // F8
    int setVolValue = 50;
    int toggleKey = 120;    // F9
    int moreKey = 121;      // F10
};

struct Config {
    GlobalSettings global;
    Hotkeys hotkeys;
    std::vector<SoundEntry> entries;
    std::string path;
    bool loaded = false;
};

bool LoadConfig(const std::string& path, Config& cfg);
bool SaveConfig(const std::string& path, const Config& cfg);

const char* WeaponName(int t);
std::uint64_t ParsePlayerRoot(const std::string& s, std::uint64_t defval);
