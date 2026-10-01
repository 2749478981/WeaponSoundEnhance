// ===========================================================================
//  SonarLayerProbe.dll —— 动作层面内存探针（独立小工具，与主插件互不影响）
//
//  用途：定位"弓箭装瓶 / 解瓶 / 平射"这类插件读不到的动作。
//       这些动作的 fsm 在主插件读取的字段(entity+0x6278)里读出来是 0，
//         · 要么动作挂在别的 layer 上，字段在别处；
//         · 要么那个字段对某些动作根本不更新。
//       本工具不做任何判定/发声，只做一件事：
//       检测到「fsm==0 且 lmt 变化」（装瓶/平射的特征帧）时，
//       把玩家实体动作对象关键区段里**发生变化的内存字段**（偏移:旧值:新值）
//       写进 SonarLayerProbe.log —— 变化的位置就是"该动作状态真正存在的地方"。
//
//  ★ v2 改进（本轮）：
//     1) 扫描窗口从 0x6000..0x7000(4KB) 扩到 0x4000..0x9000(20KB)，分块 dump。
//        layer 0 的字段很可能离 0x6278 较远，旧窗口会漏。
//     2) 手动标记：游戏内按热键（默认 Ctrl+F7）打一行 MARK 进日志，
//        方便你把"我这次按的是装瓶"和内存跳变对齐。
//     3) 变化输出带**连续值序列**：同一偏移在最近几帧的值都列出来，
//        真 fsm 字段会呈现 0 -> N -> 0 的跳变，一眼可辨。
//     4) 同时扫描 act 对象的更宽窗口（0xE800..0xF000, 2KB）。
//
//  用法：
//    1. 把 SonarLayerProbe.dll 丢进 nativePC\plugins\
//    2. 进游戏，做几次「装瓶」「解瓶」「平射」
//    3. 退出后把 plugins\SonarLayerProbe.log 发给作者
//
//  注意：内存偏移按 15.23.00；版本变了用 ini（PlayerRoot / PollMs / Hotkey）微调。
//
//  读取链与主插件一致：
//    manager = *(PlayerRoot)
//    entity  = *(manager + 0x50)
//    fsm     = *(entity + 0x6278)
//    act     = *(entity + 0x468)      → lmt = *(act + 0xE9C4)
// ===========================================================================

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <cstring>
#include <cstdio>
#include <fstream>

namespace {

std::atomic<bool> g_stop{false};
std::wstring g_dir;
std::uintptr_t g_playerRoot = 0x1450139A0ULL;
unsigned g_pollMs = 40;
unsigned g_hotkeyVK = VK_F7;      // Ctrl + F7 = 打标记
bool     g_hotkeyCtrl = true;

// ---- 扫描窗口（entity 相对偏移 → 长度）----
// 旧版只看 0x6000..0x7000；新版铺开一大片，覆盖多个可能的 layer 字段区。
constexpr std::uintptr_t kEntWinBase = 0x4000;
constexpr std::size_t    kEntWinLen  = 0x5000;   // 0x4000..0x9000 = 20KB
constexpr std::uintptr_t kActWinBase = 0xE800;
constexpr std::size_t    kActWinLen  = 0x0800;   // 0xE800..0xF000 = 2KB

// 连续值历史：记录每个偏移最近 N 帧的值，用于识别真字段跳变
constexpr int kHistFrames = 5;

// 受保护读取：游戏内存地址不一定都有效，野读会崩，必须 __try
template <typename T> bool ReadAt(std::uintptr_t p, T& out) {
    __try { out = *(T*)p; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uintptr_t ReadPtr(std::uintptr_t p) { std::uintptr_t v = 0; return ReadAt(p, v) ? v : 0; }
int ReadI32(std::uintptr_t p, int dflt = -1) { int v = dflt; return ReadAt(p, v) ? v : dflt; }

// __try 不能出现在含对象展开的函数里（会报 C2712），所以把"读一段窗口"
// 做成独立函数：里面只有 memcpy，没有对象析构问题。
bool SafeWindow(std::uintptr_t base, unsigned char* dst, std::size_t n) {
    __try {
        std::memcpy(dst, (const void*)base, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogMsg(const std::string& s) {
    const std::wstring path = g_dir + L"SonarLayerProbe.log";
    HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; ::GetLocalTime(&st);
    char buf[96];
    wsprintfA(buf, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    DWORD w = 0;
    ::WriteFile(h, buf, (DWORD)std::strlen(buf), &w, nullptr);
    ::WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr);
    ::WriteFile(h, "\r\n", 2, &w, nullptr);
    ::CloseHandle(h);
}

// 简单 ini：PlayerRoot=0x...  PollMs=N  Hotkey=F7
void LoadProxyIni() {
    const std::wstring ini = g_dir + L"SonarLayerProbe.ini";
    std::ifstream f(ini, std::ios::binary);
    if (!f) return;
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto valueOf = [&](const char* key) -> std::string {
        std::size_t p = all.find(key);
        if (p == std::string::npos) return "";
        std::size_t e = all.find('=', p);
        if (e == std::string::npos) return "";
        std::size_t s = e + 1;
        while (s < all.size() && (all[s] == ' ' || all[s] == '\t')) ++s;
        std::size_t en = all.find_first_of("\r\n", s);
        return all.substr(s, (en == std::string::npos) ? all.size() - s : en - s);
    };
    {
        std::string v = valueOf("PlayerRoot");
        if (!v.empty())
            g_playerRoot = (v.find("0x") == 0 || v.find("0X") == 0)
                ? (std::uintptr_t)std::strtoull(v.c_str(), nullptr, 16)
                : (std::uintptr_t)std::strtoull(v.c_str(), nullptr, 10);
    }
    {
        std::string v = valueOf("PollMs");
        if (!v.empty()) { unsigned n = (unsigned)std::atoi(v.c_str()); if (n >= 5 && n <= 1000) g_pollMs = n; }
    }
    {
        std::string v = valueOf("Hotkey");   // 例如 "F7" / "F8"，默认 F7
        if (!v.empty()) {
            int n = 0;
            if (v.size() >= 2 && (v[0] == 'F' || v[0] == 'f')) n = std::atoi(v.c_str() + 1);
            if (n >= 1 && n <= 12) g_hotkeyVK = (unsigned)(VK_F1 + n - 1);
        }
    }
}

// 每个被扫描偏移的值历史（用于识别"真 fsm"跳变）
struct SlotHist {
    int hist[kHistFrames] = {0,0,0,0,0};
    int cnt = 0;
    void push(int v) {
        if (cnt < kHistFrames) { hist[cnt++] = v; }
        else { for (int i = 1; i < kHistFrames; ++i) hist[i-1] = hist[i]; hist[kHistFrames-1] = v; }
    }
    bool allSame() const {
        for (int i = 1; i < cnt; ++i) if (hist[i] != hist[0]) return false;
        return true;
    }
};

void PushHistory(std::map<std::uintptr_t, SlotHist>& m, std::uintptr_t key, int v) {
    m[key].push(v);
}

// 把一个窗口里的变化列出来；同时刷新所有偏移的历史
void ScanWindow(const char* tag, std::uintptr_t base,
                const std::vector<unsigned char>& oldb,
                const std::vector<unsigned char>& newb,
                std::map<std::uintptr_t, SlotHist>& hist,
                std::string& out) {
    if (oldb.size() != newb.size()) return;
    int emitted = 0;
    for (std::size_t i = 0; i + 4 <= oldb.size(); i += 4) {
        int o = 0, nw = 0;
        std::memcpy(&o, &oldb[i], 4);
        std::memcpy(&nw, &newb[i], 4);
        const std::uintptr_t off = (std::uintptr_t)i;

        // 只关心"看起来像状态值"的字段：小整数（|v| < 100000），排除指针/浮点噪声
        const bool oSmall = (o >= -4096 && o <= 100000);
        const bool nSmall = (nw >= -4096 && nw <= 100000);

        if (o != nw && (oSmall || nSmall)) {
            SlotHist& h = hist[off];
            h.push(nw);
            // 只有在"连续几帧不是恒定值"时才值得报（恒定值进不了这里）
            char b[200];
            wsprintfA(b, "\n    %s+%04X  %d -> %d", tag, (unsigned)off, o, nw);
            out += b;
            if (++emitted >= 160) {
                out += "\n    ... (窗口内变化过多，已截断)";
                break;
            }
        }
    }
}

// ---- 候选字段快照：装瓶/解瓶/射箭是"状态机不变、只换动作"，逐帧 diff 抓不到。
//      按 Ctrl+F7 时主动读这些字段的瞬时值，一次对比就能区分三种动作。----
// ⚠ 重要：diff 里打印的 "E@+XXXX" 是**相对窗口 base(0x4000)** 的偏移！
//   所以候选字段的真实 entity 偏移 = 0x4000 + XXXX。这里直接写真实偏移。
struct Cand { std::uintptr_t off; const char* name; };
const Cand kCandE[] = {
    {0x55B8, "layerA"},   // 装瓶时 1->49153（旧 diff 显示 E@+15B8）
    {0x55BC, "layerB"},   // 成对
    {0x6274, "fsmtgt"},   // 主插件读的层
    {0x6278, "fsm"},      // 主插件读的 fsm（装瓶恒 0）
    {0x6674, "c6674"},    // 装瓶时 1->3（旧 E@+2274）
    {0x668C, "c668C"},    // 旧 E@+228C
    {0x6690, "c6690"},    // 旧 E@+2290
    {0x6924, "c6924"},    // 旧 E@+2924
    {0x6928, "c6928"},    // 旧 E@+2928
    {0x693C, "c693C"},    // 旧 E@+293C
    {0x6940, "c6940"},    // 旧 E@+2940
    {0x82D4, "c82D4"},    // 旧 E@+42D4
    {0x82E8, "c82E8"},    // 旧 E@+42E8
    {0x8930, "c8930"},    // 旧 E@+4930（单调递增计数）
    {0x893C, "c893C"},    // 旧 E@+493C
};
// act 侧候选：diff 里 "A@+XXXX" 相对窗口 base(0xE800)，真实偏移 = 0xE800 + XXXX。
// 主 lmt 在 act+0xE9C4（即旧 diff 的 A@+01C4）。
const Cand kCandA[] = {
    {0xE9C4, "lmt"},       // 主插件读的 lmt（旧 A@+01C4）
    {0x01C4, "act_01C4"},  // act 开头附近，独立于 lmt
    {0x0030, "act_30"},    // 旧 A@+0030 有过 0->1
    {0x00E0, "act_E0"},    // 旧 A@+00E0 有过 0->1
    {0x0088, "act_88"},    // 旧 A@+0088 有过 0->1
};

// 手动标记请求（热键线程置位，探针线程处理）
std::atomic<bool> g_markReq{false};

void HotkeyLoop() {
    bool wasDown = false;
    for (;;) {
        if (g_stop) return;
        ::Sleep(30);
        const bool ctrl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool key  = (::GetAsyncKeyState((int)g_hotkeyVK) & 0x8000) != 0;
        const bool down = key && (!g_hotkeyCtrl || ctrl);
        if (down && !wasDown) g_markReq.store(true, std::memory_order_release);
        wasDown = down;
    }
}

void ProbeLoop() {
    std::vector<unsigned char> oldE(kEntWinLen, 0), oldA(kActWinLen, 0);
    std::map<std::uintptr_t, SlotHist> histE, histA;
    int lastLmt = -1;
    int lastFsm = -2;

    for (;;) {
        if (g_stop) return;
        ::Sleep(g_pollMs);

        const std::uintptr_t manager = ReadPtr(g_playerRoot);
        if (!manager) { lastLmt = -1; lastFsm = -2; continue; }
        const std::uintptr_t entity = ReadPtr(manager + 0x50);
        if (!entity) { lastLmt = -1; lastFsm = -2; continue; }

        const int fsm = ReadI32(entity + 0x6278);
        const int fsmTgt = ReadI32(entity + 0x6274, -1);
        const std::uintptr_t act = ReadPtr(entity + 0x468);
        const int lmt = act ? ReadI32(act + 0xE9C4, -1) : -1;
        // 武器：和主插件一致，经三层指针 (+0xC0 → +0x8 → +0x78) 再 +0x2E8
        int weapon = -1;
        {
            const std::uintptr_t wl1 = ReadPtr(entity + 0xC0);
            const std::uintptr_t wl2 = wl1 ? ReadPtr(wl1 + 0x8) : 0;
            const std::uintptr_t wl3 = wl2 ? ReadPtr(wl2 + 0x78) : 0;
            if (wl3) weapon = ReadI32(wl3 + 0x2E8, -1);
        }

        // ---- 触发条件：手动标记 OR (fsm==0 且 lmt 变化) ----
        const bool mark = g_markReq.exchange(false, std::memory_order_acq_rel);
        const bool autoTrig = (fsm == 0 && lmt >= 0 && lmt != lastLmt);
        if (mark || autoTrig) {
            std::vector<unsigned char> ne(kEntWinLen), na(kActWinLen);
            const bool okE = SafeWindow(entity + kEntWinBase, ne.data(), ne.size());
            const bool okA = act && SafeWindow(act + kActWinBase, na.data(), na.size());

            std::string msg;
            {
                char h[220];
                wsprintfA(h, "%s fsm=%d fsmtgt=%d lmt=%d(was %d) weapon=%d | 变化:",
                          mark ? "MARK" : "trigger", fsm, fsmTgt, lmt, lastLmt, weapon);
                msg = h;
            }

            // ★ 标记时：额外打一批候选字段的**瞬时值快照**
            //   装瓶/解瓶/射箭是"状态机不变、只换动作"，diff 抓不到，必须看瞬时值。
            if (mark) {
                msg += "\n    [SNAP entity]";
                for (const Cand& c : kCandE) {
                    const int v = ReadI32(entity + c.off, -999999);
                    char b[64];
                    wsprintfA(b, " %s=%d", c.name, v);
                    msg += b;
                }
                if (act) {
                    msg += "\n    [SNAP act]";
                    for (const Cand& c : kCandA) {
                        const int v = ReadI32(act + c.off, -999999);
                        char b[64];
                        wsprintfA(b, " %s=%d", c.name, v);
                        msg += b;
                    }
                }
            }
            if (okE) ScanWindow("E@", entity + kEntWinBase, oldE, ne, histE, msg);
            if (okA) ScanWindow("A@", act + kActWinBase, oldA, na, histA, msg);

            // 附上"在历史里呈现跳变"的偏移（真 fsm 字段的特征：出现过多个不同值）
            {
                std::string jump;
                for (auto& kv : histE) {
                    if (kv.second.cnt >= 3 && !kv.second.allSame()) {
                        char b[96];
                        wsprintfA(b, " E+%04X", (unsigned)kv.first);
                        jump += b;
                    }
                }
                if (!jump.empty()) msg += "\n    [跳变候选 E]:" + jump;
            }
            if (okE) oldE.swap(ne);
            if (okA) oldA.swap(na);
            if (!okE) msg += " (entity window read failed)";
            LogMsg(msg);
        }
        lastLmt = lmt;
        lastFsm = fsm;
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t buf[MAX_PATH + 4] = {};
        ::GetModuleFileNameW(m, buf, MAX_PATH);
        g_dir = buf;
        const std::size_t s = g_dir.find_last_of(L"\\/");
        g_dir = (s == std::wstring::npos) ? L"" : g_dir.substr(0, s + 1);
        ::DisableThreadLibraryCalls(m);
        LoadProxyIni();
        char hdr[256];
        std::snprintf(hdr, sizeof(hdr),
                      "SonarLayerProbe v2 loaded. PlayerRoot=0x%llX PollMs=%u Hotkey=Ctrl+F%d "
                      "EntWin=0x%X..0x%X ActWin=0x%X..0x%X",
                      (unsigned long long)g_playerRoot, g_pollMs,
                      (int)(g_hotkeyVK - VK_F1 + 1),
                      (unsigned)kEntWinBase, (unsigned)(kEntWinBase + kEntWinLen),
                      (unsigned)kActWinBase, (unsigned)(kActWinBase + kActWinLen));
        LogMsg(hdr);
        std::thread(ProbeLoop).detach();
        std::thread(HotkeyLoop).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop = true;
    }
    return TRUE;
}
