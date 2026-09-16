#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// 一条音效（音效组内的一项）
struct SoundSpec {
    std::string path;     // 相对路径，如 sounds/xxx.wav
    int delay = 0;        // 播放延时(ms)
    int vol = 100;        // 0..100；100 = 相对主音量无额外调整
    bool fixed = false;   // |F 固定：命中该池时恒播；同池未固定者仍随机抽一条
};

// 一个音效组（默认音效或某刃时音效）
struct PoolSpec {
    std::vector<SoundSpec> specs;
    bool empty() const { return specs.empty(); }
};

// 一条「条件 -> 音效池」。判定窗口内按书写顺序求值，第一个成立的播。
struct CondSpec {
    std::string expr;       // 条件表达式，如 "dmg>0 & dAura>=0"
    bool atEnd = false;     // true 写成 SoundEnd:（只在窗口结束时评），false 写成 Sound:
    PoolSpec pool;
};

// 一条派生攻击音效条目
struct SoundEntry {
    int weaponType = -1;        // 0..13，-1 = 任意武器
    std::vector<int> lmt;       // 触发的 LMT 列表；空 = 不限
    int fsmId = -1;             // 动作状态机 ID，-1 = 不限
    std::string combo;          // 所属配置组合名（"" = 默认组合）
    std::string name;           // 显示名（Name=，插件匹配时忽略）
    std::string group;          // 动作组（Group=）：同一招的多个触发条目填相同组名，
                                // 整招只响一次且刃色在首个触发瞬间定格
    PoolSpec def;               // 默认音效（Sound=）；启用判定时它是"都不成立"的兜底池
    PoolSpec gauge[4];          // 刃时音效 0..3（无/白/黄/红；Sound:none|white|yellow|red）

    // ---- 延迟判定（checkTimeoutMs > 0 且 conds 非空时启用）----
    // 动作匹配上只是开窗，接着盯一段时间，按条件挑音效池。
    // 用来做"打中/落空""掉刃/升刃"这类必须观察一段时间才知道结果的触发。
    int checkDelayMs = 0;         // 从第几毫秒开始计伤害（排除招式前段的伤害）
    int checkTimeoutMs = 0;       // 窗口上限；0 = 不启用判定，行为与旧版一致
    int checkOffsetMs = 150;      // 判定点在"实测最晚出伤时刻"之上留的余量
    bool endOnAction = true;      // 动作结束(含被打断)也作为判定时机
    std::vector<CondSpec> conds;

    int LmtAny() const { return lmt.empty() ? -1 : lmt.front(); }
    bool MatchesLmt(int v) const {
        if (v < 0 || lmt.empty()) return true;
        for (int x : lmt) if (x == v) return true;
        return false;
    }
};

struct GlobalSettings {
    std::string playerRoot = "0x1450139A0";
    int pollMs = 60;
    int debounceMs = 120;
    int volume = 50;
    int enabled = 1;
    int moreSounds = 1;         // 旧全局开关：仅对无固定音效的纯旧条目生效
    std::string gaugePtrOff = "0x76B0";   // 太刀气刃对象偏移（十六进制）
    std::string gaugeValOff = "0x2370";   // 气刃等级偏移（十六进制）
    int debug = 0;              // 调试日志（插件侧 Debug=1）
    int chatEcho = 1;
    int chatCommands = 1;
    int hotkeysEnabled = 1;     // 启用热键（插件侧 Hotkeys=1）
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
    int comboKey = 122;     // F11 切换当前武器配置组合
};

struct Config {
    GlobalSettings global;
    Hotkeys hotkeys;
    std::map<int, std::string> active;   // 每武器当前组合名（""=默认）
    std::vector<SoundEntry> entries;     // 所有组合的条目（每条带 combo 标记）
    std::string path;
    bool loaded = false;
};

bool LoadConfig(const std::string& path, Config& cfg);
bool SaveConfig(const std::string& path, const Config& cfg);

const char* WeaponName(int t);
// 刃时标签：tag ∈ none|white|yellow|red 或 0..3 → 0..3，否则 -1
int GaugeTagIndex(const std::string& tag);
const char* GaugeTagName(int level);   // level 0..3 -> "none".."red"
const char* GaugeUiName(int level);    // level 0..3 -> "无刃".."红刃"
std::uint64_t ParsePlayerRoot(const std::string& s, std::uint64_t defval);
