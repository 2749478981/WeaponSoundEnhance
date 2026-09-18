#include "config.h"
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string ToLower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// 以 ; , 分隔
std::vector<std::string> SplitList(const std::string& v) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i <= v.size(); ++i) {
        char c = (i < v.size()) ? v[i] : '\0';
        if (c == '\0' || c == ';' || c == ',') {
            std::string t = Trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
            if (c == '\0') break;
        } else {
            cur += c;
        }
    }
    return out;
}

// path [ | delay | vol | flags ]，flags 含 F 表示固定
void ParseSpec(const std::string& token, SoundSpec& sp) {
    sp = SoundSpec{};
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i <= token.size(); ++i) {
        char c = (i < token.size()) ? token[i] : '\0';
        if (c == '\0' || c == '|') {
            parts.push_back(Trim(cur));
            cur.clear();
            if (c == '\0') break;
        } else {
            cur += c;
        }
    }
    sp.path = parts.empty() ? "" : parts[0];
    if (parts.size() >= 4) {
        sp.delay = parts[1].empty() ? 0 : std::atoi(parts[1].c_str());
        if (sp.delay < 0) sp.delay = 0;
        int v = parts[2].empty() ? 100 : std::atoi(parts[2].c_str());
        sp.vol = v < 0 ? 0 : (v > 100 ? 100 : v);
        std::string fl = ToLower(parts[3]);
        sp.fixed = fl.find('f') != std::string::npos;
    } else if (parts.size() == 3) {
        sp.delay = parts[1].empty() ? 0 : std::atoi(parts[1].c_str());
        if (sp.delay < 0) sp.delay = 0;
        int v = parts[2].empty() ? 100 : std::atoi(parts[2].c_str());
        sp.vol = v < 0 ? 0 : (v > 100 ? 100 : v);
    } else if (parts.size() == 2) {
        sp.delay = parts[1].empty() ? 0 : std::atoi(parts[1].c_str());
        if (sp.delay < 0) sp.delay = 0;
    }
}

// 追加解析一行的音效到池；纯旧式 token 登记进 plainOrder 供 SoundDelay/SoundVol 覆盖
void AppendSpecs(PoolSpec& pool, const std::string& value,
                 std::vector<std::pair<PoolSpec*, int>>& plainOrder) {
    for (const auto& tok : SplitList(value)) {
        SoundSpec sp;
        ParseSpec(tok, sp);
        if (sp.path.empty()) continue;
        bool dup = false;
        for (const auto& ex : pool.specs)
            if (ex.path == sp.path) { dup = true; break; }
        if (dup) continue;
        const bool wasPlain = (tok.find('|') == std::string::npos);
        const int idx = (int)pool.specs.size();
        pool.specs.push_back(sp);
        if (wasPlain) plainOrder.emplace_back(&pool, idx);
    }
}

} // namespace

const char* WeaponName(int t) {
    switch (t) {
        case 0:  return "大剑";
        case 1:  return "片手";
        case 2:  return "双刀";
        case 3:  return "太刀";
        case 4:  return "大锤";
        case 5:  return "笛子";
        case 6:  return "长枪";
        case 7:  return "铳枪";
        case 8:  return "斩斧";
        case 9:  return "盾斧";
        case 10: return "虫棍";
        case 11: return "弓箭";
        case 12: return "轻弩";
        case 13: return "重弩";
        default: return "任意";
    }
}

int GaugeTagIndex(const std::string& tag) {
    if (tag.empty()) return -1;
    if (tag.size() == 1 && tag[0] >= '0' && tag[0] <= '3') return tag[0] - '0';
    const char* names[4] = {"none", "white", "yellow", "red"};
    for (int i = 0; i < 4; ++i)
        if (ToLower(tag) == names[i]) return i;
    return -1;
}

const char* GaugeTagName(int level) {
    static const char* names[4] = {"none", "white", "yellow", "red"};
    return (level >= 0 && level <= 3) ? names[level] : "?";
}

const char* GaugeUiName(int level) {
    static const char* names[4] = {"无刃", "白刃", "黄刃", "红刃"};
    return (level >= 0 && level <= 3) ? names[level] : "?";
}

std::uint64_t ParsePlayerRoot(const std::string& s, std::uint64_t defval) {
    std::string v = Trim(s);
    if (v.empty()) return defval;
    const char* p = v.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!*p) return defval;
    char* end = nullptr;
    unsigned long long hv = std::strtoull(p, &end, 16);
    if (end == p) return defval;
    return static_cast<std::uint64_t>(hv);
}

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool LoadConfig(const std::string& path, Config& cfg) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF &&
        (unsigned char)txt[1] == 0xBB && (unsigned char)txt[2] == 0xBF)
        txt = txt.substr(3);

    cfg.entries.clear();
    cfg.active.clear();
    std::map<int, std::string> activeMap;
    std::string section;
    bool inAttack = false, inCombo = false;
    int curComboW = -1;
    std::string curComboName;
    SoundEntry cur;
    std::vector<int> rawDelay, rawVol;
    std::vector<std::pair<PoolSpec*, int>> plainOrder;

    auto finishEntry = [&]() {
        if (!inAttack) return;
        for (size_t i = 0; i < plainOrder.size(); ++i) {
            SoundSpec& sp = plainOrder[i].first->specs[plainOrder[i].second];
            if (i < rawDelay.size() && rawDelay[i] > 0) sp.delay = rawDelay[i];
            if (i < rawVol.size()) sp.vol = ClampInt(rawVol[i], 0, 100);
        }
        cur.combo = (inCombo && curComboW >= 0) ? curComboName : "";
        cfg.entries.push_back(std::move(cur));
        inAttack = false;
        cur = SoundEntry();
        rawDelay.clear();
        rawVol.clear();
        plainOrder.clear();
    };

    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string::npos) eol = txt.size();
        std::string line = Trim(txt.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            finishEntry();
            section = Trim(line.substr(1, line.size() - 2));
            if (section.size() >= 6 && section.compare(0, 6, "Attack") == 0) {
                inAttack = true;
                cur = SoundEntry();
            } else if (section == "Active") {
                inAttack = false; inCombo = false; curComboW = -1; curComboName.clear();
            } else if (section.size() > 6 && section.compare(0, 6, "Weapon") == 0) {
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
                inCombo = false; curComboW = -1; curComboName.clear(); inAttack = false;
            }
            continue;
        }

        // 找键值分隔的 '='。不能用第一个——条件表达式里的比较符(>= <= == !=)
        // 也含 '='，比如  Sound:dmg>0 & dAura>=0 = a.wav  按第一个切会把键截断。
        size_t eq = std::string::npos;
        for (size_t i2 = 0; i2 < line.size(); ++i2) {
            if (line[i2] != '=') continue;
            const char pv = (i2 > 0) ? line[i2 - 1] : (char)0;
            const char nx = (i2 + 1 < line.size()) ? line[i2 + 1] : (char)0;
            if (pv == '>' || pv == '<' || pv == '!' || pv == '=') continue;
            if (nx == '=') continue;
            eq = i2; break;
        }
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (!inAttack) {
            if (section == "WeaponSoundEnhance") {
                if (key == "PlayerRoot") cfg.global.playerRoot = val;
                else if (key == "PollMs") cfg.global.pollMs = std::atoi(val.c_str());
                else if (key == "DebounceMs") cfg.global.debounceMs = std::atoi(val.c_str());
                else if (key == "Volume") cfg.global.volume = std::atoi(val.c_str());
                else if (key == "Enabled") cfg.global.enabled = std::atoi(val.c_str());
                else if (key == "MoreSounds") cfg.global.moreSounds = std::atoi(val.c_str());
                else if (key == "Debug") cfg.global.debug = std::atoi(val.c_str());
                else if (key == "GaugePtrOff") cfg.global.gaugePtrOff = val;
                else if (key == "GaugeValOff") cfg.global.gaugeValOff = val;
                else if (key == "ChargeValOff") cfg.global.chargeValOff = val;
                else if (key == "FsmTargetOff") cfg.global.fsmTargetOff = val;
                else if (key == "QuestRoot") cfg.global.questRoot = val;
                else if (key == "QuestDmgOff") cfg.global.questDmgOff = val;
                else if (key == "ChatEcho") cfg.global.chatEcho = std::atoi(val.c_str());
                else if (key == "ChatCommands") cfg.global.chatCommands = std::atoi(val.c_str());
                else if (key == "Hotkeys") cfg.global.hotkeysEnabled = std::atoi(val.c_str());
            } else if (section == "Hotkeys") {
                if (key == "ModifierKey") cfg.hotkeys.modifierKey = std::atoi(val.c_str());
                else if (key == "ReloadKey") cfg.hotkeys.reloadKey = std::atoi(val.c_str());
                else if (key == "VolUpKey") cfg.hotkeys.volUpKey = std::atoi(val.c_str());
                else if (key == "VolDownKey") cfg.hotkeys.volDownKey = std::atoi(val.c_str());
                else if (key == "SetVolKey") cfg.hotkeys.setVolKey = std::atoi(val.c_str());
                else if (key == "SetVolValue") cfg.hotkeys.setVolValue = std::atoi(val.c_str());
                else if (key == "ToggleKey") cfg.hotkeys.toggleKey = std::atoi(val.c_str());
                else if (key == "MoreKey") cfg.hotkeys.moreKey = std::atoi(val.c_str());
                else if (key == "ComboKey") cfg.hotkeys.comboKey = std::atoi(val.c_str());
            } else if (section == "Active") {
                if (key.size() > 1 && (key[0] == 'W' || key[0] == 'w')) {
                    int w = std::atoi(key.c_str() + 1);
                    if (w >= -1) activeMap[w] = val;
                }
            }
            continue;
        }

        if (key == "WeaponType") {
            cur.weaponType = std::atoi(val.c_str());
        } else if (key == "FSMId") {
            cur.fsmId = std::atoi(val.c_str());
        } else if (key == "ActionLMT" || key == "LMT") {
            for (const auto& t : SplitList(val)) {
                int v = std::atoi(t.c_str());
                if (v == -1) continue;   // 不限（空列表表达）
                bool dup = false;
                for (int x : cur.lmt) if (x == v) { dup = true; break; }
                if (!dup) cur.lmt.push_back(v);
            }
        } else if (key == "Name") {
            cur.name = val;
        } else if (key == "CheckDelayMs") {
            cur.checkDelayMs = std::atoi(val.c_str());
        } else if (key == "CheckTimeoutMs") {
            cur.checkTimeoutMs = std::atoi(val.c_str());
        } else if (key == "CheckOffsetMs" || key == "CheckGraceMs") {
            cur.checkOffsetMs = std::atoi(val.c_str());
            if (cur.checkOffsetMs < 0) cur.checkOffsetMs = 0;
        } else if (key == "CheckEndOn") {
            std::string lv;
            for (char c : val) lv += (char)tolower((unsigned char)c);
            cur.endOnAction = (lv == "action");
        } else if (key == "CheckMode") {
            std::string lv;
            for (char c : val) lv += (char)tolower((unsigned char)c);
            cur.checkMode = (lv == "final") ? 1 : 0;
        } else if (key == "FSMTarget") {
            cur.fsmTarget = std::atoi(val.c_str());
        } else if (key == "Group") {
            cur.group = val;
        } else if (key == "Sound") {
            AppendSpecs(cur.def, val, plainOrder);
        } else if ((key.size() > 6 && key.compare(0, 6, "Sound:") == 0 &&
                    key.find_first_of("<>=!&|", 6) != std::string::npos) ||
                   (key.size() > 9 && key.compare(0, 9, "SoundEnd:") == 0)) {
            // 条件池：Sound:<表达式>= 或 SoundEnd:<表达式>=
            // 判据是"标签里含比较符"，不含的仍按旧的刃时 tag 解析，老配置零影响。
            const bool atEnd = (key.size() > 9 && key.compare(0, 9, "SoundEnd:") == 0);
            CondSpec cs;
            cs.expr = Trim(key.substr(atEnd ? 9 : 6));
            cs.atEnd = atEnd;
            std::vector<std::pair<PoolSpec*, int>> scratch;
            AppendSpecs(cs.pool, val, scratch);
            cur.conds.push_back(cs);
        } else if (key.size() > 6 && key.compare(0, 6, "Sound:") == 0) {
            int lvl = GaugeTagIndex(ToLower(key.substr(6)));
            if (lvl >= 0 && lvl < 4)
                AppendSpecs(cur.gauge[lvl], val, plainOrder);
        } else if (key == "SoundDelay") {
            for (const auto& t : SplitList(val))
                rawDelay.push_back(std::atoi(t.c_str()));
        } else if (key == "SoundVol") {
            for (const auto& t : SplitList(val))
                rawVol.push_back(std::atoi(t.c_str()));
        }
    }
    finishEntry();

    cfg.active = activeMap;
    cfg.path = path;
    cfg.loaded = true;
    return true;
}

// 一条音效的规范写法：默认属性且未固定 → 纯路径；否则 path|delay|vol[|F]
static std::string SpecToken(const SoundSpec& s) {
    if (!s.fixed && s.delay == 0 && s.vol == 100) return s.path;
    std::string t = s.path + "|" + std::to_string(s.delay) + "|" + std::to_string(s.vol);
    if (s.fixed) t += "|F";
    return t;
}

static std::string LmtLine(const SoundEntry& e) {
    if (e.lmt.empty()) return "ActionLMT=-1";          // v1 兼容写法
    if (e.lmt.size() == 1) return "ActionLMT=" + std::to_string(e.lmt[0]);
    std::string s = "LMT=";
    for (size_t k = 0; k < e.lmt.size(); ++k) {
        if (k) s += ",";
        s += std::to_string(e.lmt[k]);
    }
    return s;
}

static void WritePool(const PoolSpec& pool, const char* key, std::string& o) {
    if (pool.empty()) return;
    std::string s;
    for (size_t k = 0; k < pool.specs.size(); ++k) {
        if (k) s += "; ";
        s += SpecToken(pool.specs[k]);
    }
    o += std::string(key) + "=" + s + "\r\n";
}

static void WriteEntry(const SoundEntry& e, int n, std::string& o) {
    o += "[Attack" + std::to_string(n) + "]\r\n";
    if (!e.name.empty()) o += "Name=" + e.name + "\r\n";
    if (!e.group.empty()) o += "Group=" + e.group + "\r\n";
    o += "WeaponType=" + std::to_string(e.weaponType) + "\r\n";
    o += LmtLine(e) + "\r\n";
    o += "FSMId=" + std::to_string(e.fsmId) + "\r\n";
    if (e.fsmTarget >= 0) o += "FSMTarget=" + std::to_string(e.fsmTarget) + "\r\n";
    if (e.checkTimeoutMs > 0 && !e.conds.empty()) {
        if (e.checkDelayMs > 0)
            o += "CheckDelayMs=" + std::to_string(e.checkDelayMs) + "\r\n";
        o += "CheckTimeoutMs=" + std::to_string(e.checkTimeoutMs) + "\r\n";
        if (e.endOnAction) o += "CheckEndOn=action\r\n";
        if (e.checkMode) o += "CheckMode=final\r\n";
        o += "CheckOffsetMs=" + std::to_string(e.checkOffsetMs) + "\r\n";
        for (const auto& c : e.conds) {
            if (c.pool.empty() || c.expr.empty()) continue;
            const std::string k = (c.atEnd ? "SoundEnd:" : "Sound:") + c.expr;
            WritePool(c.pool, k.c_str(), o);
        }
    }
    WritePool(e.def, "Sound", o);
    for (int i = 0; i < 4; ++i) {
        std::string k = std::string("Sound:") + GaugeTagName(i);
        WritePool(e.gauge[i], k.c_str(), o);
    }
    o += "\r\n";
}

bool SaveConfig(const std::string& path, const Config& cfg) {
    std::string o;
    o += "; ============================================================================\r\n";
    o += ";  WeaponSoundEnhance.ini —— 武器音效拓展插件配置\r\n";
    o += ";  (由 WeaponSoundEnhanceGUI 生成)\r\n";
    o += ";  音效 wav 文件请放到 DLL 同目录的 sounds\\ 文件夹里。\r\n";
    o += ";\r\n";
    o += ";  武器类型表（与原版武器序号一致）：\r\n";
    o += ";    0=大剑  1=片手  2=双刀  3=太刀  4=大锤  5=笛子\r\n";
    o += ";    6=长枪  7=铳枪  8=斩斧  9=盾斧 10=虫棍 11=弓箭 12=轻弩 13=重弩\r\n";
    o += ";  每条 [AttackN] 代表一种派生攻击：\r\n";
    o += ";    WeaponType  ：武器类型（0..13）。-1 = 任意武器。\r\n";
    o += ";    ActionLMT/LMT：动作 LMT。-1 = 不限；多值用逗号分隔（LMT=）。\r\n";
    o += ";    FSMId       ：动作状态机 ID。-1 = 不限。\r\n";
    o += ";    三者 AND 关系；命中后从匹配的音效里选音效。\r\n";
    o += ";    Group=              动作组（可选）：同一招的多个触发条目填相同组名\r\n";
    o += ";                        时整招只响一次，刃色以首个触发瞬间为准。\r\n";
    o += ";    Sound=               默认音效；Sound:none/white/yellow/red= 太刀无刃时/白刃时/\r\n";
    o += ";                        黄刃时/红刃时 的音效（未配的刃时回退默认音效）。\r\n";
    o += ";    音效写法 path|延时ms|音量0..100|F ，末尾 F = 固定音效：命中时恒播，\r\n";
    o += ";                        同池其余未固定音效仍随机抽一条同时播放。\r\n";
    o += ";  游戏内聊天框指令：/wse reload | on | off | more | one | vol N | vol+ | vol- | help\r\n";
    o += "; ============================================================================\r\n\r\n";

    o += "[WeaponSoundEnhance]\r\n";
    o += "PlayerRoot=" + cfg.global.playerRoot + "\r\n";
    o += "PollMs=" + std::to_string(cfg.global.pollMs) + "\r\n";
    o += "DebounceMs=" + std::to_string(cfg.global.debounceMs) + "\r\n";
    o += "Volume=" + std::to_string(cfg.global.volume) + "\r\n";
    o += "Enabled=" + std::to_string(cfg.global.enabled) + "\r\n";
    o += "MoreSounds=" + std::to_string(cfg.global.moreSounds) + "\r\n";
    o += "Debug=" + std::to_string(cfg.global.debug) + "\r\n";
    o += "GaugePtrOff=" + cfg.global.gaugePtrOff + "\r\n";
    o += "GaugeValOff=" + cfg.global.gaugeValOff + "\r\n";
    o += "ChargeValOff=" + cfg.global.chargeValOff + "\r\n";
    o += "FsmTargetOff=" + cfg.global.fsmTargetOff + "\r\n";
    o += "QuestRoot=" + cfg.global.questRoot + "\r\n";
    o += "QuestDmgOff=" + cfg.global.questDmgOff + "\r\n";
    o += "ChatEcho=" + std::to_string(cfg.global.chatEcho) + "\r\n";
    o += "ChatCommands=" + std::to_string(cfg.global.chatCommands) + "\r\n";
    o += "Hotkeys=" + std::to_string(cfg.global.hotkeysEnabled) + "\r\n\r\n";

    o += "[Hotkeys]\r\n";
    o += "ModifierKey=" + std::to_string(cfg.hotkeys.modifierKey) + "\r\n";
    o += "ReloadKey=" + std::to_string(cfg.hotkeys.reloadKey) + "\r\n";
    o += "VolUpKey=" + std::to_string(cfg.hotkeys.volUpKey) + "\r\n";
    o += "VolDownKey=" + std::to_string(cfg.hotkeys.volDownKey) + "\r\n";
    o += "SetVolKey=" + std::to_string(cfg.hotkeys.setVolKey) + "\r\n";
    o += "SetVolValue=" + std::to_string(cfg.hotkeys.setVolValue) + "\r\n";
    o += "ToggleKey=" + std::to_string(cfg.hotkeys.toggleKey) + "\r\n";
    o += "MoreKey=" + std::to_string(cfg.hotkeys.moreKey) + "\r\n";
    o += "ComboKey=" + std::to_string(cfg.hotkeys.comboKey) + "\r\n";

    // [Active]：记录每武器当前用的组合（仅对有组合的武器写出）
    {
        std::string act;
        for (int w = 0; w <= 13; ++w) {
            bool any = false, hasNamed = false;
            for (const auto& e : cfg.entries) if (e.weaponType == w) { any = true; if (!e.combo.empty()) hasNamed = true; }
            if (!any) continue;
            std::string a;
            auto it = cfg.active.find(w);
            if (it != cfg.active.end()) a = it->second;
            if (hasNamed || !a.empty())
                act += std::string("W") + std::to_string(w) + "=" + a + "\r\n";
        }
        if (!act.empty()) o += "\r\n[Active]\r\n" + act + "\r\n";
    }

    // 按武器 + 组合分组写出（段名 AttackN 全局递增）
    int n = 0;
    auto Banner = [&](const std::string& wname, int w, bool& header) {
        if (!header) {
            o += "\r\n; ----------------------------------------------------------------------------\r\n";
            o += std::string("; ") + wname + " (weaponType=" + std::to_string(w) + ")\r\n";
            o += "; ----------------------------------------------------------------------------\r\n";
            header = true;
        }
    };
    auto Has = [&](const std::vector<std::string>& v, const std::string& e) {
        for (const auto& x : v) if (x == e) return true;
        return false;
    };

    for (int w = 0; w <= 13; ++w) {
        bool header = false;
        // 默认组合（""）
        for (const auto& e : cfg.entries)
            if (e.weaponType == w && e.combo.empty()) { Banner(WeaponName(w), w, header); WriteEntry(e, ++n, o); }
        // 命名组合（按条目中出现顺序）
        std::vector<std::string> cbOrder;
        for (const auto& e : cfg.entries)
            if (e.weaponType == w && !e.combo.empty() && !Has(cbOrder, e.combo)) cbOrder.push_back(e.combo);
        for (const auto& cb : cbOrder) {
            Banner(WeaponName(w), w, header);
            o += "\r\n[Weapon" + std::to_string(w) + ":" + cb + "]\r\n";
            for (const auto& e : cfg.entries)
                if (e.weaponType == w && e.combo == cb) WriteEntry(e, ++n, o);
        }
    }
    // 任意武器（weaponType<0）默认组合放到最后
    {
        bool header = false;
        for (const auto& e : cfg.entries)
            if (e.weaponType < 0 && e.combo.empty()) {
                if (!header) {
                    o += "\r\n; ----------------------------------------------------------------------------\r\n";
                    o += "; 通用 / 任意武器\r\n";
                    o += "; ----------------------------------------------------------------------------\r\n";
                    header = true;
                }
                WriteEntry(e, ++n, o);
            }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(o.data(), (std::streamsize)o.size());
    out.close();
    return true;
}
