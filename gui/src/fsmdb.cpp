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
        {3, 7,   49267, "拔刀", ""},
        {3, 11,  49265, "气刃斩1", ""},
        {3, 11,  49256, "气刃斩1(起手帧)", ""},
        {3, 12,  -1,    "气刃斩1(判定帧)", ""},
        {3, 67,  49258, "气刃斩2", ""},
        {3, 68,  49260, "气刃斩3", ""},
        {3, 69,  49261, "气刃斩4", ""},
        {3, 70,  -1,    "逆袈裟", ""},
        {3, 86,  -1,    "见切", ""},
        {3, 87,  -1,    "气刃突刺开始", ""},
        {3, 89,  -1,    "突刺命中", ""},
        {3, 90,  -1,    "登龙飞天", ""},
        {3, 92,  -1,    "气刃兜割", ""},
        {3, 99,  -1,    "纳刀", ""},
        {3, 101, -1,    "小居", ""},
        {3, 102, -1,    "大居", ""},
        {3, 326, -1,    "猫车", ""},
        {3, 532, -1,    "磨刀", ""},
        {3, 42,  -1,    "软化", ""},

        // ------------------------------------------------------------------
        //  以下为实测得到的动作 ID（lmt）。这些是靠对比动作前后的伤害/练气
        //  数值反推出来的，只确认了 lmt，没有测对应的 fsm，所以 fsm 留 -1。
        //  样本量见各条注释；来源：
        //  https://github.com/goldenplums2003/MHWI_anon_laugh_cry_great_sword
        //  https://github.com/goldenplums2003/MHWI_anon_laugh_cry_long_sword
        // ------------------------------------------------------------------

        // ---- 太刀 (3) ----
        {3, -1, 49326, "登龙 (气刃兜割) —— 三个刃色共用", ""},
        {3, -1, 49461, "大居 (居合抜刀气刃斩) · 白刃", ""},
        {3, -1, 49462, "大居 (居合抜刀气刃斩) · 黄刃", ""},
        {3, -1, 49463, "大居 (居合抜刀气刃斩) · 红刃", ""},
        {3, -1, 49322, "气刃突刺", ""},
        {3, -1, 49458, "特殊纳刀", ""},

        // ---- 大剑 (0) ----
        // 真蓄是两段攻击，两段各有独立动作 ID，按蓄力等级再分三种。
        // 注意第一段的三个 ID 不连号（1 蓄是 49298，不是 49340）。
        // 游戏有时不拆成两个动作，两段伤害会全落在第一段的 ID 里。
        {0, -1, 49298, "真蓄 1 蓄 · 第一段", ""},
        {0, -1, 49341, "真蓄 2 蓄 · 第一段", ""},
        {0, -1, 49342, "真蓄 3 蓄 · 第一段", ""},
        {0, -1, 49427, "真蓄 1 蓄 · 第二段 (大伤害)", ""},
        {0, -1, 49428, "真蓄 2 蓄 · 第二段 (大伤害)", ""},
        {0, -1, 49429, "真蓄 3 蓄 · 第二段 (大伤害)", ""},
        {0, -1, 49307, "强蓄斩 1 蓄", ""},
        {0, -1, 49308, "强蓄斩 2 蓄", ""},
        {0, -1, 49309, "强蓄斩 3 蓄", ""},
        {0, -1, 49280, "一蓄斩 (起手第一刀)", ""},
        {0, -1, 49283, "抜刀 (起手)", ""},
        {0, -1, 49256, "抜刀", ""},
        {0, -1, 49344, "铁山靠", ""},
        {0, -1, 49458, "强化射击", ""},
        {0, -1, 49459, "强化射击", ""},
        {0, -1, 49460, "强化射击", ""},
        // 蓄力/过渡段。49398 -> 49439 -> 真蓄 是固定前置链。
        {0, -1, 49398, "真蓄前置过渡", ""},
        {0, -1, 49433, "蓄力 · 第一阶段", ""},
        {0, -1, 49436, "蓄力 · 第二阶段", ""},
        {0, -1, 49439, "蓄力 · 第三阶段 (必接真蓄)", ""},
        {0, -1, 49402, "蓄力 / 过渡", ""},
        {0, -1, 49403, "蓄力 / 过渡", ""},
        {0, -1, 49412, "蓄力 / 过渡", ""},
        {0, -1, 49413, "蓄力 / 过渡", ""},
        {0, -1, 49511, "蓄力 / 过渡", ""},
    };
}


// ===========================================================================
//  怪物动作。2026-09-18/19 实测，9250 次动作切换、402 个不同动作 ID。
//  下面只收用户审过、认定确凿的五组：两段相变、劫火的两种起手、大咬。
//  只取黑龙的三档血量（hpMax 171600 / 114972 / 66000，彼此 ID 交集 84~98%）；
//  日志里另一只 hpMax=138240 的（王冰）交集只有 6~18%，已排除。
//
//  两条判定依据：
//    - 动作总帧是动作 ID 的严格函数，可以拿来认「同一招的不同方向」；
//    - 连招顺序从「谁接谁」的统计里直接读出来，每一把都完全一致。
//
//  动作 ID 本身有结构：(动画文件号 << 12) | 文件内序号。
//  所以同一招的左/右/前版本一般同组、序号挨着、总帧还一样 ——
//  28787/28788 这对大咬就是这么找出来的。
//
//  血线是「出现时的怪物血量百分比」，用来区分阶段专属招式。
// ===========================================================================
static void AppendMonsters(std::vector<FsmDbEntry>& db)
{
    struct M { const char* mon; int lmt; const char* name; };
    static const M kM[] = {
        // ---- 黑龙：一阶段 -> 二阶段 相变（血线 68~78%，整段约 29 秒，全程无敌）----
        // 一整招拆成的 11 段，顺序每把都一样。绑音效记得填同一个「动作组」，
        // 否则 11 段会连响 11 次。
        {"黑龙", 33008, "1转2 · 第1段 起手 (2.3s，和2转3共用)"},
        {"黑龙", 33017, "1转2 · 第2段 (1.6s)"},
        {"黑龙", 33018, "1转2 · 第3段 (1.8s)"},
        {"黑龙", 33019, "1转2 · 第4段 (2.3s)"},
        {"黑龙", 33020, "1转2 · 第5段 (0.06s，极短)"},
        {"黑龙", 33021, "1转2 · 第6段 (1.7s)"},
        {"黑龙", 33022, "1转2 · 第7段 (2.9s)"},
        {"黑龙", 33023, "1转2 · 第8段 (1.3s)"},
        {"黑龙", 33024, "1转2 · 第9段 (5.9s)"},
        {"黑龙", 33025, "1转2 · 第10段 (3.8s)"},
        {"黑龙", 33030, "1转2 · 第11段 收尾 (5.3s)"},

        // ---- 黑龙：二阶段 -> 三阶段 相变（血线 47~50%，整段约 41 秒，全程无敌）----
        // 伤害溢出时可能在更低的血线发生。
        // 33042 是这段独有的分岔点：33008 之后接 33042 就是二转三、接 33017 就是一转二。
        // 想提前预警二转三，盯 33042 最早也最准 —— 比末段的 33029 早约 36 秒。
        {"黑龙", 33042, "2转3 · 第2段 (1.5s，二转三的分岔点，最早的可靠信号)"},
        {"黑龙", 33043, "2转3 · 第3段 (1.8s)"},
        {"黑龙", 33044, "2转3 · 第4段 (2.0s)"},
        {"黑龙", 33045, "2转3 · 第5段 (0.06s，极短)"},
        {"黑龙", 33046, "2转3 · 第6段 (1.8s)"},
        {"黑龙", 33047, "2转3 · 第7段 (5.6s)"},
        {"黑龙", 33048, "2转3 · 第8段 (1.3s)"},
        {"黑龙", 33049, "2转3 · 第9段 (9.8s，最长)"},
        {"黑龙", 33050, "2转3 · 第10段 (3.8s)"},
        {"黑龙", 33031, "2转3 · 第11段 (6.0s)"},
        {"黑龙", 33029, "2转3 · 第12段 收尾 (4.7s)"},

        // ---- 黑龙：三阶段 飞天劫火（血线 41% 以下，整段约 28 秒）----
        // 两种起手，从第 5 段开始合流。
        // 注意：33069~33077 以前被标成「P3 专属动作(待认)」，那是错的，
        // 它们就是劫火的中后段 —— 连招统计里每次都紧跟在 33068 后面。
        {"黑龙", 33257, "P3 飞天劫火 · 起手A 第1段 (1.2s)"},
        {"黑龙", 33266, "P3 飞天劫火 · 起手A 第2段 (0.9s)"},
        {"黑龙", 33267, "P3 飞天劫火 · 起手A 第3段 (0.7s)"},
        {"黑龙", 33065, "P3 飞天劫火 · 起手A 第4段 (1.8s)"},
        {"黑龙", 33255, "P3 飞天劫火 · 起手B 第1段 (1.2s)"},
        {"黑龙", 33263, "P3 飞天劫火 · 起手B 第2段 (0.9s)"},
        {"黑龙", 33264, "P3 飞天劫火 · 起手B 第3段 (0.7s)"},
        {"黑龙", 33075, "P3 飞天劫火 · 起手B 第4段 (1.8s)"},
        {"黑龙", 33066, "P3 飞天劫火 · 第5段 (0.06s，两种起手在这合流)"},
        {"黑龙", 33067, "P3 飞天劫火 · 第6段 (1.6s)"},
        {"黑龙", 33068, "P3 飞天劫火 · 第7段 (4.2s)"},
        {"黑龙", 33069, "P3 飞天劫火 · 第8段 (1.4s)"},
        {"黑龙", 33070, "P3 飞天劫火 · 第9段 (4.9s)"},
        {"黑龙", 33071, "P3 飞天劫火 · 第10段 (3.4s)"},
        {"黑龙", 33076, "P3 飞天劫火 · 第11段 (2.9s)"},
        {"黑龙", 33077, "P3 飞天劫火 · 第12段 收尾 (2.2s)"},

        // ---- 黑龙：单招 ----
        // 大咬有至少两个方向版本。28787 是用户肉眼确认的；28788 同组、序号挨着、
        // 总帧和时长完全一样，出现次数还更多 —— 判定要两个都填才不漏。
        {"黑龙", 28787, "大咬（方向一，用户确认，3.7s）"},
        {"黑龙", 28788, "大咬（方向二，推定，3.7s）"},

        // 「高频动作」「长动作」那几条删掉了：统计上显眼不代表认得出是什么招，
        // 放进下拉栏只会让人对着一堆没名字的数字挑。等实机认出来再往回加。
    };
    for (std::size_t i = 0; i < sizeof(kM) / sizeof(kM[0]); ++i) {
        FsmDbEntry e;
        e.weapon = -1;
        e.fsm = -1;
        e.lmt = kM[i].lmt;
        e.name = kM[i].name;
        e.monster = kM[i].mon;
        db.push_back(e);
    }
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

void LoadCsvInto(const std::string& path, std::vector<FsmDbEntry>& db) {
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

bool SameKey(const FsmDbEntry& a, const FsmDbEntry& b) {
    return a.weapon == b.weapon && a.fsm == b.fsm && a.lmt == b.lmt;
}

std::string CsvName(std::string name) {
    for (auto& c : name) if (c == ',' || c == '\r' || c == '\n') c = ' ';
    return name;
}

void WriteCsvHeader(std::ostream& out) {
    out << "# WeaponSoundEnhance 动作 ID 库（基础库）\n";
    out << "# schema: weapon,fsm,lmt,name  —— 结构固定不变；解析时会忽略多余列、容忍缺列\n";
    out << "# weapon 0..13: 大剑/片手/双刀/太刀/大锤/笛子/长枪/铳枪/斩斧/盾斧/虫棍/弓箭/轻弩/重弩；-1=通用\n";
    out << "# fsm 或 lmt 未知时写 -1；name 不要包含英文逗号\n";
    out << "#\n";
    out << "# 本文件 = 随包/下载的“基础库”，可被「获取最新库」整体更新（只追加行，不改结构）。\n";
    out << "# 你自己的实测/导入请放 fsm_db_user.csv：优先级更高，且任何更新都不会覆盖它。\n";
    out << "# 贡献流程：GUI 工具栏「上传ID」→ 导出实测ID / 提交到共享库。\n";
}

// 知识库缓存：ReloadFsmDb() 后重新读盘
std::vector<FsmDbEntry> g_db;
bool g_loaded = false;
std::string g_dir;   // 数据目录（ini 所在目录）；空 = exe 同目录

bool FileExists(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

void SetFsmDbDir(const std::string& dir) {
    if (g_dir == dir) return;
    g_dir = dir;
    g_loaded = false;   // 目录变了要重新读盘
}

static std::string DataDir() { return g_dir.empty() ? ExeDir() : g_dir; }

std::string FsmDbPath() { return DataDir() + "fsm_db.csv"; }
std::string FsmDbUserPath() { return DataDir() + "fsm_db_user.csv"; }
std::string FsmDbSubmissionPath() { return DataDir() + "fsm_db_submission.csv"; }

std::vector<FsmDbEntry> LoadFsmDbCsv(const std::string& path) {
    std::vector<FsmDbEntry> v;
    LoadCsvInto(path, v);
    return v;
}

const std::vector<FsmDbEntry>& GetFsmDb() {
    if (!g_loaded) {
        g_loaded = true;
        g_db.clear();
        // 基础库：以文件 fsm_db.csv 为准（随包/下载；更新只追加行，结构固定）。
        // 文件缺失或为空时，用内置兜底数据播种出一份，让"ID 数据在文件里"成立。
        if (!FileExists(FsmDbPath())) SaveFsmDbCsv(FsmDbPath(), Builtin());
        LoadCsvInto(FsmDbPath(), g_db);
        if (g_db.empty()) {
            SaveFsmDbCsv(FsmDbPath(), Builtin());
            LoadCsvInto(FsmDbPath(), g_db);
        }
        if (g_db.empty()) g_db = Builtin();   // 连写文件都失败时的最后兜底

        // 用户库：优先级更高（同名键覆盖名字），永不被更新覆盖
        std::vector<FsmDbEntry> user;
        LoadCsvInto(FsmDbUserPath(), user);
        for (const auto& u : user) {
            bool replaced = false;
            for (auto& d : g_db)
                if (SameKey(d, u)) { d.name = u.name; replaced = true; break; }
            if (!replaced) g_db.push_back(u);
        }

        // 怪物动作只在内置表里，不进 CSV。
        //
        // fsm_db.csv 的列是 weapon,fsm,lmt,name，结构已经冻结，没有 monster 这
        // 一列。怪物条目写进去再读回来，monster 就丢了 —— 它们会变成
        // weapon=-1/fsm=-1 的「武器」动作，和武器查询串台（33029 这种怪物
        // 动作 ID 会被当成某把武器的招）。
        //
        // 放在最后追加还有一层意思：上面那轮用户库合并是按 (weapon,fsm,lmt)
        // 找同名键覆盖的，怪物条目这三项分别是 -1/-1/动作ID，正好可能和用户
        // 库里某条撞上。先加进去就会被人家的名字盖掉。
        AppendMonsters(g_db);
    }
    return g_db;
}

std::vector<FsmDbEntry> BuiltinFsmDb() { return Builtin(); }

void ReloadFsmDb() { g_loaded = false; }

bool SaveFsmDbCsv(const std::string& path, const std::vector<FsmDbEntry>& entries) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    WriteCsvHeader(out);
    std::vector<FsmDbEntry> uniq;
    for (const auto& e : entries) {
        bool dup = false;
        for (const auto& u : uniq) if (SameKey(u, e)) { dup = true; break; }
        if (!dup) uniq.push_back(e);
    }
    for (const auto& e : uniq)
        out << e.weapon << "," << e.fsm << "," << e.lmt << "," << CsvName(e.name) << "\n";
    return true;
}

int MergeFsmDbEntries(const std::vector<FsmDbEntry>& src, const std::string& dstPath) {
    if (src.empty()) return 0;
    std::vector<FsmDbEntry> dst = LoadFsmDbCsv(dstPath);

    // 目标文件不存在时先写表头，保持文件可读
    bool hadFile = false;
    { std::ifstream in(dstPath, std::ios::binary); hadFile = (bool)in; }
    std::ofstream out(dstPath, std::ios::binary | std::ios::app);
    if (!out) return 0;
    if (!hadFile) WriteCsvHeader(out);

    int added = 0;
    for (const auto& e : src) {
        bool dup = false;
        for (const auto& d : dst) if (SameKey(d, e)) { dup = true; break; }
        if (dup) continue;
        out << e.weapon << "," << e.fsm << "," << e.lmt << "," << CsvName(e.name) << "\n";
        dst.push_back(e);
        ++added;
    }
    return added;
}

int MergeFsmDbCsv(const std::string& srcPath, const std::string& dstPath) {
    return MergeFsmDbEntries(LoadFsmDbCsv(srcPath), dstPath);
}

std::vector<FsmDbEntry> SearchFsmDb(const std::string& query, int weaponFilter) {
    std::vector<FsmDbEntry> out;
    std::string q = Lower(Trim(query));
    for (const auto& e : GetFsmDb()) {
        if (!e.monster.empty()) continue;   // 怪物动作走 MonsterActions()
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
    // 库里有两类条目：知道 fsm 的，和只测出 lmt 的（fsm 记 -1）。
    // 先按 fsm 找；找不到再按 lmt 找，否则只有 lmt 的条目永远查不出名字。
    for (const auto& e : GetFsmDb()) {
        if (!e.monster.empty()) continue;          // 怪物动作不参与玩家动作反查
        if (weapon >= 0 && e.weapon >= 0 && e.weapon != weapon) continue;
        if (e.fsm < 0 || e.fsm != fsm) continue;
        if (lmt >= 0 && e.lmt >= 0 && e.lmt != lmt) continue;
        return e.name;
    }
    if (lmt >= 0) {
        for (const auto& e : GetFsmDb()) {
            if (!e.monster.empty()) continue;
            if (weapon >= 0 && e.weapon >= 0 && e.weapon != weapon) continue;
            if (e.lmt != lmt) continue;
            return e.name;
        }
    }
    return std::string();
}

std::string LookupMonsterAction(const std::string& monster, int lmt) {
    for (const auto& e : GetFsmDb()) {
        if (e.monster.empty()) continue;
        if (!monster.empty() && e.monster != monster) continue;
        if (e.lmt != lmt) continue;
        return e.name;
    }
    return std::string();
}

std::vector<std::string> ListKnownMonsters() {
    std::vector<std::string> out;
    for (const auto& e : GetFsmDb()) {
        if (e.monster.empty()) continue;
        bool has = false;
        for (const auto& m : out) if (m == e.monster) { has = true; break; }
        if (!has) out.push_back(e.monster);
    }
    return out;
}

std::vector<FsmDbEntry> MonsterActions(const std::string& monster) {
    std::vector<FsmDbEntry> out;
    for (const auto& e : GetFsmDb()) {
        if (e.monster.empty()) continue;
        if (!monster.empty() && e.monster != monster) continue;
        out.push_back(e);
    }
    return out;
}
