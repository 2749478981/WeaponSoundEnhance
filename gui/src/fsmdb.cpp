#include "fsmdb.h"
#include <cstdlib>
#include <fstream>
#include <windows.h>

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string Lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

std::vector<FsmDbEntry> Builtin() {
    return {
        // 太刀 (3)
        {3, 7,   49267, "拔刀"},
        {3, 11,  49265, "气刃斩1"},
        {3, 11,  49256, "气刃斩1(起手帧)"},
        {3, 12,  -1,    "气刃斩1(判定帧)"},
        {3, 67,  49258, "气刃斩2"},
        {3, 68,  49260, "气刃斩3"},
        {3, 69,  49261, "气刃斩4"},
        {3, 70,  -1,    "逆袈裟"},
        {3, 86,  -1,    "见切"},
        {3, 87,  -1,    "气刃突刺开始"},
        {3, 89,  -1,    "突刺命中"},
        {3, 90,  -1,    "登龙飞天"},
        {3, 92,  -1,    "气刃兜割"},
        {3, 99,  -1,    "纳刀"},
        {3, 101, -1,    "小居"},
        {3, 102, -1,    "大居"},
        {3, 326, -1,    "猫车"},
        {3, 532, -1,    "磨刀"},
        {3, 42,  -1,    "软化"},
    };
}

std::string ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring ws(buf);
    size_t p = ws.find_last_of(L"\\/");
    if (p != std::wstring::npos) ws = ws.substr(0, p + 1);
    char mb[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, mb, sizeof(mb), nullptr, nullptr);
    return std::string(mb);
}

void LoadExternal(std::vector<FsmDbEntry>& db) {
    std::string path = ExeDir() + "fsm_db.csv";
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        // 格式：weapon,fsm,lmt,name
        int field = 0;
        std::string parts[4];
        for (char c : line) {
            if (c == ',' && field < 3) { ++field; continue; }
            parts[field] += c;
        }
        if (field < 2) continue; // 至少 weapon,fsm
        FsmDbEntry e;
        e.weapon = std::atoi(Trim(parts[0]).c_str());
        e.fsm = std::atoi(Trim(parts[1]).c_str());
        e.lmt = (field >= 2 && !Trim(parts[2]).empty()) ? std::atoi(Trim(parts[2]).c_str()) : -1;
        e.name = Trim(parts[3]);
        if (e.name.empty()) e.name = "(未命名)";
        db.push_back(e);
    }
}

} // namespace

const std::vector<FsmDbEntry>& GetFsmDb() {
    static std::vector<FsmDbEntry> db;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        db = Builtin();
        LoadExternal(db);
    }
    return db;
}

std::vector<FsmDbEntry> SearchFsmDb(const std::string& query, int weaponFilter) {
    std::vector<FsmDbEntry> out;
    std::string q = Lower(Trim(query));
    for (const auto& e : GetFsmDb()) {
        if (weaponFilter >= 0 && e.weapon >= 0 && e.weapon != weaponFilter) continue;
        if (q.empty()) {
            out.push_back(e);
            continue;
        }
        bool hit = false;
        std::string name = Lower(e.name);
        if (name.find(q) != std::string::npos) hit = true;
        if (!hit && e.fsm >= 0 && std::to_string(e.fsm).find(q) != std::string::npos) hit = true;
        if (!hit && e.lmt >= 0 && std::to_string(e.lmt).find(q) != std::string::npos) hit = true;
        if (hit) out.push_back(e);
    }
    return out;
}

std::string LookupFsmName(int weapon, int fsm, int lmt) {
    std::string fallback;
    for (const auto& e : GetFsmDb()) {
        if (weapon >= 0 && e.weapon >= 0 && e.weapon != weapon) continue;
        if (e.fsm != fsm) continue;
        if (lmt >= 0 && e.lmt >= 0 && e.lmt != lmt) continue;
        return e.name;
    }
    (void)fallback;
    return std::string();
}
