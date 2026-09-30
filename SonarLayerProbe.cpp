// ===========================================================================
//  SonarLayerProbe.dll —— 动作层面内存探针（独立小工具，与主插件互不影响）
//
//  用途：定位"弓箭装瓶 / 平射"这类插件读不到的动作。
//       这些动作的 fsm 在主插件读取的字段(entity+0x6278)里读出来是 0，
//         · 要么动作挂在别的 layer 上，字段在别处；
//         · 要么那个字段对某些动作根本不更新。
//       本工具不做任何判定/发声，只做一件事：
//       检测到「fsm==0 且 lmt 变化」（装瓶/平射的特征帧）时，
//       把玩家实体 / 动作对象关键区段里**发生变化的内存字段**（偏移:旧值:新值）
//       写进 SonarLayerProbe.log —— 变化的位置就是"该动作状态真正存在的地方"。
//
//  用法：
//    1. 把 SonarLayerProbe.dll 丢进 nativePC\plugins\
//    2. 进游戏，做几次「装瓶」和「平射」
//    3. 退出后把 plugins\SonarLayerProbe.log 发给作者
//
//  注意：内存偏移按 15.23.00；版本变了用 ini（PlayerRoot / PollMs）微调。
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
#include <atomic>
#include <thread>
#include <cstring>
#include <fstream>

namespace {

std::atomic<bool> g_stop{false};
std::wstring g_dir;
std::uintptr_t g_playerRoot = 0x1450139A0ULL;
unsigned g_pollMs = 40;

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
    wsprintfA(buf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    DWORD w = 0;
    ::WriteFile(h, buf, (DWORD)std::strlen(buf), &w, nullptr);
    ::WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr);
    ::WriteFile(h, "\r\n", 2, &w, nullptr);
    ::CloseHandle(h);
}

// 简单 ini：PlayerRoot=0x...  PollMs=N
void LoadProxyIni() {
    const std::wstring ini = g_dir + L"SonarLayerProbe.ini";
    std::ifstream f(ini, std::ios::binary);
    if (!f) return;
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::size_t p = all.find("PlayerRoot");
    if (p != std::string::npos) {
        std::size_t e = all.find('=', p);
        if (e != std::string::npos) {
            std::size_t s = e + 1;
            std::size_t en = all.find_first_of("\r\n", s);
            std::string v = all.substr(s, (en == std::string::npos) ? all.size() - s : en - s);
            if (v.find("0x") == 0 || v.find("0X") == 0) g_playerRoot = (std::uintptr_t)std::strtoull(v.c_str(), nullptr, 16);
            else g_playerRoot = (std::uintptr_t)std::strtoull(v.c_str(), nullptr, 10);
        }
    }
    p = all.find("PollMs");
    if (p != std::string::npos) {
        std::size_t e = all.find('=', p);
        if (e != std::string::npos) {
            unsigned v = (unsigned)std::atoi(all.c_str() + e + 1);
            if (v >= 5 && v <= 1000) g_pollMs = v;
        }
    }
}

void DumpChanges(const char* label, std::uintptr_t base,
                 const std::vector<unsigned char>& oldb,
                 const std::vector<unsigned char>& newb, std::string& out) {
    if (oldb.size() != newb.size()) return;
    int n = 0;
    for (std::size_t i = 0; i + 4 <= oldb.size() && n < 96; i += 4) {
        int o = 0, nw = 0;
        std::memcpy(&o, &oldb[i], 4);
        std::memcpy(&nw, &newb[i], 4);
        if (o != nw) {
            // 打印相对窗口偏移（E@ = entity+0x6000 起，A@ = act+0xE800 起）
            char b[160];
            wsprintfA(b, "\n    %s+%04X  %08X -> %08X", label,
                      (unsigned)(i), (unsigned long)o, (unsigned long)nw);
            out += b;
            ++n;
        }
    }
}

void ProbeLoop() {
    std::vector<unsigned char> oldE(0x1000, 0), oldA(0x400, 0);
    int lastLmt = -1;
    for (;;) {
        if (g_stop) return;
        ::Sleep(g_pollMs);

        const std::uintptr_t manager = ReadPtr(g_playerRoot);
        if (!manager) { lastLmt = -1; continue; }
        const std::uintptr_t entity = ReadPtr(manager + 0x50);
        if (!entity) { lastLmt = -1; continue; }

        const int fsm = ReadI32(entity + 0x6278);
        const std::uintptr_t act = ReadPtr(entity + 0x468);
        const int lmt = act ? ReadI32(act + 0xE9C4, -1) : -1;
        // 武器武器：和主插件一致，要经三层指针 (+0xC0 → +0x8 → +0x78) 再 +0x2E8
        int weapon = -1;
        {
            const std::uintptr_t wl1 = ReadPtr(entity + 0xC0);
            const std::uintptr_t wl2 = wl1 ? ReadPtr(wl1 + 0x8) : 0;
            const std::uintptr_t wl3 = wl2 ? ReadPtr(wl2 + 0x78) : 0;
            if (wl3) weapon = ReadI32(wl3 + 0x2E8, -1);
        }

        // 特征帧：fsm==0 且 lmt 与上一帧不同 —— 装瓶/平射这类动作的样子。
        // 装瓶时 lmt 可能从一个稳定值跳到装瓶专属值；记录"状态跳变"的完整上下文。
        if (fsm == 0 && lmt >= 0 && lmt != lastLmt) {
            std::string msg = "trigger: fsm=0 lmt=" + std::to_string(lmt) +
                              " (was " + std::to_string(lastLmt) + ") weapon=" + std::to_string(weapon) +
                              " fsmTarget=" + std::to_string(ReadI32(entity + 0x6274, -1));
            // 作业对象（0x468 指针）+ 玩家实体的两个窗口：
            //   act+0xE800.. (0x400)  含 lmt(0xE9C4) 及其邻居 —— 动作状态常在这
            //   entity+0x6000..(0x1000) 含 fsm(0x6278) 及其邻居
            std::vector<unsigned char> ne(0x1000), na(0x400);
            const bool okE = SafeWindow(entity + 0x6000, ne.data(), ne.size());
            const bool okA = act && SafeWindow(act + 0xE800, na.data(), na.size());
            DumpChanges("E@", entity + 0x6000, oldE, ne, msg);
            if (okA) DumpChanges("A@", act + 0xE800, oldA, na, msg);
            oldE.swap(ne); if (okA) oldA.swap(na);
            msg += okE ? "" : " (entity window read failed)";
            LogMsg(msg);
        }
        lastLmt = lmt;
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
        char hdr[160];
        std::snprintf(hdr, sizeof(hdr), "SonarLayerProbe loaded. PlayerRoot=0x%llX PollMs=%u",
                      (unsigned long long)g_playerRoot, g_pollMs);
        LogMsg(hdr);
        std::thread(ProbeLoop).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop = true;
    }
    return TRUE;
}