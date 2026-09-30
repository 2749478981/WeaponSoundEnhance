// ===========================================================================
//  SonarAudioViewer.exe —— SonarAudio.dll 实时音效监视器（配套查看工具）
//
//  用途：另一个 agent 的 SonarAudio.dll 已把游戏内“正在播放的音效”
//        （PostEvent → media id → bank/path）hook 出来，写进
//        plugins\SonarAudio.log。本工具实时跟随该日志，把每一行
//        media id 翻译成可读名（plugins\SonarAudio.mediaids.txt），
//        控制台持续刷新——“游戏当前在播什么”一目了然。
//
//  用法：
//    SonarAudioViewer.exe [日志路径] [mediaid表路径]
//    默认: plugins\SonarAudio.log + plugins\SonarAudio.mediaids.txt
//    提示: 先在 SonarAudio.ini 里 LogEvents=1（默认就是 1）
//    退出: 按 Ctrl+C 或关闭窗口，无后台残留
//
//  附带共享内存探测：会尝试打开 Local\SonarAudioEvents_v1，
//  能打开就把头部前 64 字节 dump 出来（观察用，不做猜想解析）。
// ===========================================================================
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <type_traits>

namespace {

std::atomic<bool> g_stop{false};
BOOL WINAPI CtrlHandler(DWORD) { g_stop = true; return TRUE; }

std::wstring ToW(const std::string& u8) {
    if (u8.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), nullptr, 0);
    std::wstring w(n, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), &w[0], n);
    return w;
}

std::string ToU8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 通用输出：真控制台用 UTF-8 代码页显示中文；重定向到文件时直接写 UTF-8 字节
void Print(const std::wstring& s) {
    const std::string u8 = ToU8(s);
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w = 0;
    ::WriteFile(h, u8.data(), (DWORD)u8.size(), &w, nullptr);
}

std::string ReadFileUtf8(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER sz{};
    ::GetFileSizeEx(h, &sz);
    std::string out;
    if (sz.QuadPart > 0 && sz.QuadPart < (1 << 26)) {
        out.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        ::ReadFile(h, &out[0], (DWORD)sz.QuadPart, &rd, nullptr);
        out.resize(rd);
    }
    ::CloseHandle(h);
    return out;
}

// mediaids.txt: "media_id TAB 名字(UTF-8)"
std::unordered_map<unsigned, std::wstring> LoadMediaMap(const std::wstring& path) {
    std::unordered_map<unsigned, std::wstring> map;
    const std::string txt = ReadFileUtf8(path);
    if (txt.empty()) { Print(L"[警告] 读不到 media 映射表：" + path + L"\n"); return map; }
    std::size_t pos = 0;
    int n = 0;
    while (pos < txt.size()) {
        std::size_t eol = txt.find('\n', pos);
        if (eol == std::string::npos) eol = txt.size();
        std::string line = txt.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        unsigned id = 0;
        try { id = (unsigned)std::stoul(line.substr(0, tab)); } catch (...) { continue; }
        map[id] = ToW(line.substr(tab + 1));
        ++n;
    }
    return map;
}

struct Evt { unsigned long long seq = 0; unsigned id = 0; unsigned playing = 0;
             unsigned long long gobj = 0; unsigned media = 0;
             std::wstring bank, path; bool ok = false; };

Evt ParseLine(const std::string& line) {
    Evt e;
    auto grab = [&](const char* key, std::string& dst) {
        const std::string k(key);
        std::size_t p = line.find(k);
        if (p == std::string::npos) return;
        p += k.size();
        std::size_t en = p;
        while (en < line.size() && line[en] != ' ' && line[en] != '\t' && line[en] != '\r') ++en;
        dst = line.substr(p, en - p);
    };
    auto gnum = [&](const char* key, auto& dst) {
        std::string s; grab(key, s);
        if (s.empty()) return;
        try { dst = (std::remove_reference_t<decltype(dst)>)std::stoull(s); } catch (...) {}
    };
    gnum("seq#", e.seq);
    gnum("media=", e.media);
    std::string bank, path;
    grab("bank=", bank);
    grab("path=", path);
    e.bank = ToW(bank);
    e.path = ToW(path);
    if (bank.empty() && path.empty() && e.media == 0) return e;
    e.ok = true;
    return e;
}

void DumpSharedMemoryHead() {
    HANDLE m = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SonarAudioEvents_v1");
    if (!m) {
        Print(L"[共享内存] Local\\SonarAudioEvents_v1 打不开（DLL 未加载或未初始化）—— 纯日志模式\n");
        return;
    }
    LPVOID v = ::MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!v) { ::CloseHandle(m); Print(L"[共享内存] 映射失败\n"); return; }
    char buf[64];
    std::memcpy(buf, v, 64);
    std::wstring hex;
    for (int i = 0; i < 64; ++i) { char t[4]; wsprintfA(t, "%02X ", (unsigned char)buf[i]); hex += ToW(t); }
    Print(L"[共享内存] Local\\SonarAudioEvents_v1 已打开，头部 64B：\n  " + hex + L"\n");
    ::UnmapViewOfFile(v);
    ::CloseHandle(m);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleCtrlHandler(CtrlHandler, TRUE);
    ::SetConsoleOutputCP(CP_UTF8);

    const std::wstring defLog = L"F:\\SteamLibrary\\steamapps\\common\\Monster Hunter World\\nativePC\\plugins\\SonarAudio.log";
    const std::wstring defMap = L"F:\\SteamLibrary\\steamapps\\common\\Monster Hunter World\\nativePC\\plugins\\SonarAudio.mediaids.txt";
    std::wstring logPath = (argc > 1) ? argv[1] : defLog;
    std::wstring mapPath = (argc > 2) ? argv[2] : defMap;

    Print(L"SonarAudio 实时音效监视器\n");
    Print(L"日志: " + logPath + L"\n");
    Print(L"映射: " + mapPath + L"\n");

    const auto map = LoadMediaMap(mapPath);
    Print(L"mediaid 表已加载 " + std::to_wstring(map.size()) + L" 条\n\n");
    DumpSharedMemoryHead();

    // 打开日志，记录起始大小（避免从头重放旧内容太多）
    std::ifstream log(logPath, std::ios::binary);
    if (!log.good()) {
        Print(L"[!] 日志文件还不存在。请用兼容方式启动游戏（让 SonarAudio.dll 被加载），\n"
              L"   进行装瓶/射箭等动作后再看本窗口。按回车继续等待…\n");
    }
    std::streamsize start = 0;
    if (log.good()) {
        log.seekg(0, std::ios::end);
        start = (std::streamsize)log.tellg();
        log.seekg(0, std::ios::beg);
        log.seekg(start > 8192 ? start - 8192 : 0);  // 最多从末尾往前 8KB 回看
    }

    unsigned long long shown = 0;
    std::wstring pendingLine;
    while (!g_stop) {
        // 增量读
        if (!log.good()) log.open(logPath, std::ios::binary);
        if (log.good()) {
            std::string line;
            while (std::getline(log, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.size() < 8) continue;
                // 心跳行忽略，事件行才显示
                Evt e = ParseLine(line);
                if (!e.ok) continue;
                std::wstring name = L"(无映射)";
                const auto it = map.find(e.media);
                if (it != map.end()) name = it->second;
                std::wstring out = L"[★] media=" + std::to_wstring(e.media) + L"  " + name;
                if (!e.bank.empty()) out += L"   bank=" + e.bank;
                if (!e.path.empty()) out += L"   path=" + e.path;
                out += L"\n";
                Print(out);
                ++shown;
            }
            log.clear();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    Print(L"\n已退出（无后台残留）。\n");
    return 0;
}