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

// 插件按 ; 或 , 分隔音效
std::vector<std::string> SplitSounds(const std::string& v) {
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

std::vector<int> SplitDelays(const std::string& v) {
    std::vector<int> out;
    std::string cur;
    for (size_t i = 0; i <= v.size(); ++i) {
        char c = (i < v.size()) ? v[i] : '\0';
        if (c == '\0' || c == ';' || c == ',') {
            std::string t = Trim(cur);
            out.push_back(t.empty() ? 0 : std::atoi(t.c_str()));
            cur.clear();
            if (c == '\0') break;
        } else {
            cur += c;
        }
    }
    return out;
}

std::vector<int> SplitVols(const std::string& v) {
    std::vector<int> out;
    std::string cur;
    for (size_t i = 0; i <= v.size(); ++i) {
        char c = (i < v.size()) ? v[i] : '\0';
        if (c == '\0' || c == ';' || c == ',') {
            std::string t = Trim(cur);
            out.push_back(t.empty() ? 100 : std::atoi(t.c_str()));   // 100 = 相对主音量无额外调整
            cur.clear();
            if (c == '\0') break;
        } else {
            cur += c;
        }
    }
    return out;
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

bool LoadConfig(const std::string& path, Config& cfg) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // 去掉 UTF-8 BOM
    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF &&
        (unsigned char)txt[1] == 0xBB && (unsigned char)txt[2] == 0xBF)
        txt = txt.substr(3);

    cfg.entries.clear();
    std::string section;
    int curIdx = -1;
    auto ensure = [&](int idx) {
        while ((int)cfg.entries.size() < idx) cfg.entries.push_back(SoundEntry());
    };

    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string::npos) eol = txt.size();
        std::string line = Trim(txt.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            curIdx = -1;
            if (section.size() > 6 && section.compare(0, 6, "Attack") == 0)
                curIdx = std::atoi(section.c_str() + 6);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (section == "WeaponSoundEnhance") {
            if (key == "PlayerRoot") cfg.global.playerRoot = val;
            else if (key == "PollMs") cfg.global.pollMs = std::atoi(val.c_str());
            else if (key == "DebounceMs") cfg.global.debounceMs = std::atoi(val.c_str());
            else if (key == "Volume") cfg.global.volume = std::atoi(val.c_str());
            else if (key == "Enabled") cfg.global.enabled = std::atoi(val.c_str());
            else if (key == "MoreSounds") cfg.global.moreSounds = std::atoi(val.c_str());
            else if (key == "Playback") cfg.global.playback = val;
            else if (key == "ChatEcho") cfg.global.chatEcho = std::atoi(val.c_str());
            else if (key == "ChatCommands") cfg.global.chatCommands = std::atoi(val.c_str());
        } else if (section == "Hotkeys") {
            if (key == "ModifierKey") cfg.hotkeys.modifierKey = std::atoi(val.c_str());
            else if (key == "ReloadKey") cfg.hotkeys.reloadKey = std::atoi(val.c_str());
            else if (key == "VolUpKey") cfg.hotkeys.volUpKey = std::atoi(val.c_str());
            else if (key == "VolDownKey") cfg.hotkeys.volDownKey = std::atoi(val.c_str());
            else if (key == "SetVolKey") cfg.hotkeys.setVolKey = std::atoi(val.c_str());
            else if (key == "SetVolValue") cfg.hotkeys.setVolValue = std::atoi(val.c_str());
            else if (key == "ToggleKey") cfg.hotkeys.toggleKey = std::atoi(val.c_str());
            else if (key == "MoreKey") cfg.hotkeys.moreKey = std::atoi(val.c_str());
        } else if (curIdx >= 1) {
            ensure(curIdx);
            SoundEntry& e = cfg.entries[curIdx - 1];
            if (key == "WeaponType") e.weaponType = std::atoi(val.c_str());
            else if (key == "ActionLMT") e.actionLmt = std::atoi(val.c_str());
            else if (key == "FSMId") e.fsmId = std::atoi(val.c_str());
            else if (key == "Sound") e.sounds = SplitSounds(val);
            else if (key == "SoundDelay") e.delays = SplitDelays(val);
            else if (key == "SoundVol") e.vols = SplitVols(val);
            else if (key == "Name") e.name = val;
        }
    }

    cfg.path = path;
    cfg.loaded = true;
    return true;
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
    o += ";    ActionLMT   ：动作 LMT（派生动作号）。-1 = 不限。\r\n";
    o += ";    FSMId       ：动作状态机 ID。-1 = 不限。\r\n";
    o += ";    三者 AND 关系；满足即从 Sound 里随机播放一条 wav。\r\n";
    o += ";    SoundDelay  ：与 Sound 一一对应的每条音效播放延时(ms)，分号(;)分隔；缺省 0 = 立即。\r\n";
    o += ";    SoundVol    ：与 Sound 一一对应的每条音效音量(0..100)，分号(;)分隔；100/缺省 = 相对主音量无额外调整。\r\n";
    o += ";  游戏内聊天框指令：/wse reload | on | off | more | one | vol N | vol+ | vol- | help\r\n";
    o += "; ============================================================================\r\n\r\n";

    o += "[WeaponSoundEnhance]\r\n";
    o += "PlayerRoot=" + cfg.global.playerRoot + "\r\n";
    o += "PollMs=" + std::to_string(cfg.global.pollMs) + "\r\n";
    o += "DebounceMs=" + std::to_string(cfg.global.debounceMs) + "\r\n";
    o += "Volume=" + std::to_string(cfg.global.volume) + "\r\n";
    o += "Enabled=" + std::to_string(cfg.global.enabled) + "\r\n";
    o += "MoreSounds=" + std::to_string(cfg.global.moreSounds) + "\r\n";
    o += "Playback=" + cfg.global.playback + "\r\n";
    o += "ChatEcho=" + std::to_string(cfg.global.chatEcho) + "\r\n";
    o += "ChatCommands=" + std::to_string(cfg.global.chatCommands) + "\r\n\r\n";

    o += "[Hotkeys]\r\n";
    o += "ModifierKey=" + std::to_string(cfg.hotkeys.modifierKey) + "\r\n";
    o += "ReloadKey=" + std::to_string(cfg.hotkeys.reloadKey) + "\r\n";
    o += "VolUpKey=" + std::to_string(cfg.hotkeys.volUpKey) + "\r\n";
    o += "VolDownKey=" + std::to_string(cfg.hotkeys.volDownKey) + "\r\n";
    o += "SetVolKey=" + std::to_string(cfg.hotkeys.setVolKey) + "\r\n";
    o += "SetVolValue=" + std::to_string(cfg.hotkeys.setVolValue) + "\r\n";
    o += "ToggleKey=" + std::to_string(cfg.hotkeys.toggleKey) + "\r\n";
    o += "MoreKey=" + std::to_string(cfg.hotkeys.moreKey) + "\r\n";

    // 按武器分组写出（段名 AttackN 全局递增，保证唯一）
    int n = 0;
    for (int w = 0; w <= 13; ++w) {
        bool header = false;
        for (const auto& e : cfg.entries) {
            if (e.weaponType != w) continue;
            if (!header) {
                o += "\r\n; ----------------------------------------------------------------------------\r\n";
                o += std::string("; ") + WeaponName(w) + " (weaponType=" + std::to_string(w) + ")\r\n";
                o += "; ----------------------------------------------------------------------------\r\n";
                header = true;
            }
            ++n;
            o += "[Attack" + std::to_string(n) + "]\r\n";
            if (!e.name.empty()) o += "Name=" + e.name + "\r\n";
            o += "WeaponType=" + std::to_string(e.weaponType) + "\r\n";
            o += "ActionLMT=" + std::to_string(e.actionLmt) + "\r\n";
            o += "FSMId=" + std::to_string(e.fsmId) + "\r\n";
            if (!e.sounds.empty()) {
                std::string s;
                for (size_t k = 0; k < e.sounds.size(); ++k) {
                    if (k) s += "; ";
                    s += e.sounds[k];
                }
                o += "Sound=" + s + "\r\n";
                // 存在非 0 延时才写 SoundDelay（保持默认 0 的条目干净）
                bool anyDelay = false;
                for (int d : e.delays) if (d > 0) { anyDelay = true; break; }
                if (anyDelay) {
                    std::string sd;
                    for (size_t k = 0; k < e.sounds.size(); ++k) {
                        if (k) sd += "; ";
                        int d = (k < e.delays.size()) ? e.delays[k] : 0;
                        sd += std::to_string(d);
                    }
                    o += "SoundDelay=" + sd + "\r\n";
                }
                // 存在非 100 音量才写 SoundVol（100 = 相对主音量无额外调整）
                bool anyVol = false;
                for (int v : e.vols) if (v >= 0 && v != 100) { anyVol = true; break; }
                if (anyVol) {
                    std::string sv;
                    for (size_t k = 0; k < e.sounds.size(); ++k) {
                        if (k) sv += "; ";
                        int v = (k < e.vols.size()) ? e.vols[k] : 100;
                        if (v < 0) v = 100;
                        sv += std::to_string(v);
                    }
                    o += "SoundVol=" + sv + "\r\n";
                }
            }
            o += "\r\n";
        }
    }
    // 任意武器（weaponType<0）放到最后
    {
        bool header = false;
        for (const auto& e : cfg.entries) {
            if (e.weaponType >= 0) continue;
            if (!header) {
                o += "\r\n; ----------------------------------------------------------------------------\r\n";
                o += "; 通用 / 任意武器\r\n";
                o += "; ----------------------------------------------------------------------------\r\n";
                header = true;
            }
            ++n;
            o += "[Attack" + std::to_string(n) + "]\r\n";
            if (!e.name.empty()) o += "Name=" + e.name + "\r\n";
            o += "WeaponType=" + std::to_string(e.weaponType) + "\r\n";
            o += "ActionLMT=" + std::to_string(e.actionLmt) + "\r\n";
            o += "FSMId=" + std::to_string(e.fsmId) + "\r\n";
            if (!e.sounds.empty()) {
                std::string s;
                for (size_t k = 0; k < e.sounds.size(); ++k) {
                    if (k) s += "; ";
                    s += e.sounds[k];
                }
                o += "Sound=" + s + "\r\n";
                // 存在非 0 延时才写 SoundDelay（保持默认 0 的条目干净）
                bool anyDelay = false;
                for (int d : e.delays) if (d > 0) { anyDelay = true; break; }
                if (anyDelay) {
                    std::string sd;
                    for (size_t k = 0; k < e.sounds.size(); ++k) {
                        if (k) sd += "; ";
                        int d = (k < e.delays.size()) ? e.delays[k] : 0;
                        sd += std::to_string(d);
                    }
                    o += "SoundDelay=" + sd + "\r\n";
                }
                // 存在非 100 音量才写 SoundVol（100 = 相对主音量无额外调整）
                bool anyVol = false;
                for (int v : e.vols) if (v >= 0 && v != 100) { anyVol = true; break; }
                if (anyVol) {
                    std::string sv;
                    for (size_t k = 0; k < e.sounds.size(); ++k) {
                        if (k) sv += "; ";
                        int v = (k < e.vols.size()) ? e.vols[k] : 100;
                        if (v < 0) v = 100;
                        sv += std::to_string(v);
                    }
                    o += "SoundVol=" + sv + "\r\n";
                }
            }
            o += "\r\n";
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(o.data(), (std::streamsize)o.size());
    out.close();
    return true;
}
