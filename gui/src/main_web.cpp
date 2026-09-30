// ===========================================================================
//  main_web.cpp —— Sonar 配置工具的新宿主（WebView2）
//
//  职责非常单一：开一个 Win32 窗口 → 里面塞一个 WebView2 → 把 JS 和
//  Core::Handle() 用 JSON 对接起来。所有业务逻辑都在 core.cpp 里，这里不认识
//  任何"条目""组合""音效"的概念。
//
//  通信（两个方向都走 JSON 文本，避免嵌套转义把中文搞坏）：
//    JS   → 宿主：postMessage(JSON.stringify({id, method, params}))
//    宿主 → JS  ：PostWebMessageAsString('{"id":N,"ok":true,"data":...}')
//                 前端收到后自己 JSON.parse
//    宿主 → JS（主动事件）：'{"event":"live","data":...}' —— 没有 id
//
//  资源加载：把 exe 目录用 SetVirtualHostNameToFolderMapping 映射成
//  https://sonar.local/，然后打开 https://sonar.local/web/index.html。
//  这样 web/ 里的 ../weapons_icons/、../sonar_icon.png 都在映射根之内
//  （file:// 直接开的话，相对上级目录会被浏览器拦掉）。
// ===========================================================================

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <wrl.h>

#include "../third_party/webview2/include/WebView2.h"
#include "core.h"

#pragma comment(lib, "shlwapi.lib")

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

#ifndef IDI_APP
#define IDI_APP 100
#endif

namespace {

Core                 g_core;
ComPtr<ICoreWebView2Controller> g_ctrl;
ComPtr<ICoreWebView2>           g_web;
HWND                 g_hwnd = nullptr;
UINT_PTR             g_pollTimer = 0;
bool                 g_ready = false;

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring ExeDirW() {
    wchar_t buf[MAX_PATH * 2] = {};
    ::GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]) - 1));
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos) ? p : p.substr(0, s + 1);
}

// 启动期日志：WebView2 的初始化是异步的，出错时界面只会"白屏"，
// 没有任何提示。把每一步写进 <exeDir>sonar_gui.log，排查时一眼看到卡在哪。
// 【坑】用 "a, ccs=UTF-8" 打开时 MSVC 要求配 fwprintf，窄 fprintf 会被丢掉
// （文件建出来了但是空的）。所以这里用二进制模式，参数一律先转成 UTF-8 窄串。
void LogLine(const char* fmt, ...) {
    static std::wstring path = ExeDirW() + L"sonar_gui.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
    // 这个日志只在排查启动问题（典型症状：白屏）时有用，别让它无限增长：
    // 超过 64KB 就清空重来。插件目录里多一个 1KB 的日志无所谓，长到几 MB 就很讨厌。
    if (ftell(f) > 64 * 1024) {
        fclose(f);
        f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    }
    SYSTEMTIME st; ::GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---- 极简的"信封"解析：从 {id, method, params} 里挖出三段 ----
// 不引 JSON 库：前端发的信封是我们自己拼的，格式固定，够用就行。
struct Envelope { long long id = 0; std::string method; std::string params; };

// 从 pos 开始找 key 后面那个值；返回值的起止下标
bool FindKey(const std::string& s, const std::string& key, size_t& valStart) {
    std::string pat = "\"" + key + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return false;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
    valStart = p;
    return p < s.size();
}

std::string ReadJsonString(const std::string& s, size_t p) {
    if (p >= s.size() || s[p] != '"') return std::string();
    std::string out;
    for (++p; p < s.size(); ++p) {
        char c = s[p];
        if (c == '\\' && p + 1 < s.size()) {
            char n = s[++p];
            switch (n) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // 前端用 JSON.stringify 生成，中文一般原样（不转义），这里只兜底
                    if (p + 4 < s.size()) {
                        unsigned cp = 0;
                        for (int k = 1; k <= 4; ++k) {
                            char h = s[p + k];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        }
                        p += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default: out += n; break;
            }
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// 从 p 起取一个平衡的 JSON 值（对象/数组/字符串/字面量）
std::string ReadJsonValue(const std::string& s, size_t p) {
    if (p >= s.size()) return "{}";
    char c = s[p];
    if (c == '{' || c == '[') {
        char open = c, close = (c == '{') ? '}' : ']';
        int depth = 0;
        bool inStr = false;
        for (size_t i = p; i < s.size(); ++i) {
            char d = s[i];
            if (inStr) {
                if (d == '\\') { ++i; continue; }
                if (d == '"') inStr = false;
                continue;
            }
            if (d == '"') { inStr = true; continue; }
            if (d == open) ++depth;
            else if (d == close) { if (--depth == 0) return s.substr(p, i - p + 1); }
        }
        return "{}";
    }
    if (c == '"') {
        bool inStr = false;
        for (size_t i = p; i < s.size(); ++i) {
            char d = s[i];
            if (d == '\\') { ++i; continue; }
            if (d == '"') { if (i > p) return s.substr(p, i - p + 1); inStr = true; continue; }
        }
        return "{}";
    }
    size_t e = p;
    while (e < s.size() && s[e] != ',' && s[e] != '}') ++e;
    return s.substr(p, e - p);
}

Envelope ParseEnvelope(const std::string& s) {
    Envelope e;
    size_t p = 0;
    if (FindKey(s, "id", p)) {
        long long v = 0;
        size_t i = p;
        bool neg = false;
        if (i < s.size() && s[i] == '-') { neg = true; ++i; }
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; }
        e.id = neg ? -v : v;
    }
    if (FindKey(s, "method", p)) e.method = ReadJsonString(s, p);
    if (FindKey(s, "params", p)) e.params = ReadJsonValue(s, p);
    if (e.params.empty()) e.params = "{}";
    return e;
}

void PostJson(const std::string& json) {
    if (!g_web) return;
    g_web->PostWebMessageAsString(Utf8ToWide(json).c_str());
}

// 把 Core 的 {"ok":...} 前面插一个 id，变成 {"id":N,"ok":...}
void Reply(long long id, const std::string& result) {
    std::string body = result;
    if (body.empty() || body[0] != '{') body = "{\"ok\":false,\"error\":\"core returned non-object\"}";
    PostJson("{\"id\":" + std::to_string(id) + "," + body.substr(1));
}

void OnMessage(const std::string& text) {
    Envelope env = ParseEnvelope(text);
    if (env.method.empty()) { Reply(env.id, "{\"ok\":false,\"error\":\"missing method\"}"); return; }
    std::string result;
    try {
        result = g_core.Handle(env.method, env.params);
    } catch (const std::exception& ex) {
        result = std::string("{\"ok\":false,\"error\":\"") + ex.what() + "\"}";
    } catch (...) {
        result = "{\"ok\":false,\"error\":\"unknown exception in core\"}";
    }
    Reply(env.id, result);
}

// ---------------------------------------------------------------------------
//  WebView2 初始化
// ---------------------------------------------------------------------------
typedef HRESULT(STDAPICALLTYPE* PFN_CreateEnv)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

// 加载器 DLL 必须在 exe 旁边；用动态加载，这样缺 DLL 时能给一句人话而不是启动失败
PFN_CreateEnv LoadWebView2Entry() {
    std::wstring dll = ExeDirW() + L"WebView2Loader.dll";
    HMODULE h = ::LoadLibraryW(dll.c_str());
    if (!h) h = ::LoadLibraryW(L"WebView2Loader.dll");   // 再试 PATH
    if (!h) return nullptr;
    return (PFN_CreateEnv)::GetProcAddress(h, "CreateCoreWebView2EnvironmentWithOptions");
}

// 递归删目录（清 WebView2 缓存用）。失败就算了，不影响功能。
void RemoveDirRecursive(const std::wstring& dir) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring p = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirRecursive(p);
            } else {
                ::SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                ::DeleteFileW(p.c_str());
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    ::RemoveDirectoryW(dir.c_str());
}

// ★ WebView2 会把 index.html / css / js 缓存到磁盘。
//   资源改了它却继续用旧的，表现出来就是"我明明改了 CSS，界面一点没变"。
//   但每次启动都清空缓存又会让首屏明显变慢（每次都要重新读盘）。
//   折中：拿 web\index.html 的「大小 + 修改时间」当指纹，只在不一致时清一次。
//   这样升级后绝不会用到旧界面，平时启动也不再重复读盘。
void ClearWebViewCache(const std::wstring& userDataDir) {
    RemoveDirRecursive(userDataDir + L"\\EBWebView\\Default\\Cache");
    RemoveDirRecursive(userDataDir + L"\\EBWebView\\Default\\Code Cache");
    RemoveDirRecursive(userDataDir + L"\\EBWebView\\Default\\GPUCache");
}

void ClearWebViewCacheIfChanged(const std::wstring& userDataDir, const std::wstring& webRoot) {
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    std::wstring stamp = L"missing";
    if (::GetFileAttributesExW((webRoot + L"web\\index.html").c_str(),
                               GetFileExInfoStandard, &fad)) {
        wchar_t buf[64] = {};
        ::swprintf_s(buf, L"%lu.%lu", fad.nFileSizeLow, fad.ftLastWriteTime.dwLowDateTime);
        stamp = buf;
    }
    const std::wstring marker = userDataDir + L"\\sonar_assets.txt";

    // 读上次的指纹
    std::string prev;
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, marker.c_str(), L"rb") == 0 && f) {
            char buf[128] = {};
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            prev.assign(buf, n);
            fclose(f);
        }
    }
    const std::string want = WideToUtf8(stamp);
    if (prev == want) return;                    // 资源没变，缓存可以留着

    ClearWebViewCache(userDataDir);
    FILE* f = nullptr;
    if (_wfopen_s(&f, marker.c_str(), L"wb") == 0 && f) {
        fwrite(want.data(), 1, want.size(), f);
        fclose(f);
    }
    LogLine("assets changed (%s -> %s), webview cache cleared",
            prev.empty() ? "(none)" : prev.c_str(), want.c_str());
}

std::wstring UserDataDir() {
    // 允许用环境变量覆盖（排查"数据目录被占用"这类问题时很方便）
    wchar_t ov[MAX_PATH * 3] = {};
    DWORD on = ::GetEnvironmentVariableW(L"SONAR_WV_DATA", ov, (DWORD)(sizeof(ov) / sizeof(ov[0])));
    if (on && on < sizeof(ov) / sizeof(ov[0])) {
        std::wstring d(ov);
        ::CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }
    // 放 %LOCALAPPDATA%\Sonar\WebView2：插件目录可能只读/被同步，别把缓存写进游戏目录
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring base = (n && n < sizeof(buf) / sizeof(buf[0])) ? std::wstring(buf) : ExeDirW();
    std::wstring d = base + L"\\Sonar\\WebView2";
    ::CreateDirectoryW((base + L"\\Sonar").c_str(), nullptr);
    ::CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

void ResizeWebView() {
    if (!g_ctrl || !g_hwnd) return;
    RECT rc = {};
    ::GetClientRect(g_hwnd, &rc);
    g_ctrl->put_Bounds(rc);
    // 只在尺寸变化时记一行，方便对比"窗口多大 / 实际给 WebView 多大"
    static long lw = -1, lh = -1;
    const long w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w != lw || h != lh) {
        lw = w; lh = h;
        LogLine("resize: client=%ldx%ld", w, h);
    }
}

void StartPolling() {
    if (!g_pollTimer) g_pollTimer = ::SetTimer(g_hwnd, 1, 100, nullptr);
}

// 页面自检：把视口尺寸/主题/图标加载情况打进日志。
// ★ 必须等前端把数据拉回来、DOM 渲染完之后再跑 —— NavigationCompleted 触发时
//   页面里的异步 refresh() 还没回来，那时候数图标只会数到 0。
void ProbePage() {
    if (!g_web) return;
    g_web->ExecuteScript(
        L"(function(){"
        L"function R(s){var e=document.querySelector(s);"
        L"if(!e)return null;var r=e.getBoundingClientRect();"
        L"return [Math.round(r.left),Math.round(r.right),Math.round(r.width)];}"
        // 武器图标加载情况：总数 / 成功数 / 降级数 / 第一张的 URL。
        // 只看截图容易把"碎图占位符"看成图标，这里给数字。
        L"var im=document.querySelectorAll('img[src*=\"weapons_icons\"]');"
        L"var ok=0;for(var i=0;i<im.length;i++){if(im[i].naturalWidth>0)ok++;}"
        L"var fb=document.querySelectorAll('.icon-fallback').length;"
        L"var first=document.querySelector('img[src*=\"weapons_icons\"]');"
        L"return JSON.stringify({"
        L"w:innerWidth,h:innerHeight,dpr:devicePixelRatio,"
        L"theme:document.documentElement.getAttribute('data-theme'),"
        L"docScroll:document.documentElement.scrollWidth,"
        L"mainCols:getComputedStyle(document.querySelector('main')).gridTemplateColumns,"
        L"icons:ok+'/'+im.length,"
        L"iconFellBack:fb,"
        L"firstIcon:(first?first.getAttribute('src'):null),"
        L"logoOk:(function(){var l=document.getElementById('logoImg');"
        L"return l?l.naturalWidth>0:null;})()"
        L"});})()",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR json) -> HRESULT {
                LogLine("page: %s", WideToUtf8(json ? json : L"(null)").c_str());
                return S_OK;
            }).Get());
}

}  // namespace

// ---------------------------------------------------------------------------
//  窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            ResizeWebView();
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                std::string ev = g_core.PollEvents();
                if (!ev.empty()) PostJson(ev);
            } else if (wp == 2) {
                // 延迟自检：等前端把数据拉完、DOM 渲染好之后再量
                ::KillTimer(hwnd, 2);
                ProbePage();
            }
            return 0;
        case WM_SETFOCUS:
            if (g_ctrl) g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return 0;
        case WM_DESTROY:
            if (g_pollTimer) { ::KillTimer(hwnd, 1); g_pollTimer = 0; }
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    // ★ 单实例：重复双击时把已有窗口激活并退出，不弹提示框、不重复开窗。
    //   窗口类名是我们自定义的唯一名，FindWindow 很安全。
    {
        HWND w = ::FindWindowW(L"SonarConfigGUI", nullptr);
        if (w) {
            ::ShowWindow(w, SW_RESTORE);
            ::SetForegroundWindow(w);
            return 0;
        }
        // 竞态兜底：窗口可能正在创建（类已注册、窗口未建），互斥已存在就等它建完
        HANDLE mtx = ::CreateMutexW(nullptr, TRUE, L"SonarConfigGUI_SingleInstance");
        if (mtx && ::GetLastError() == ERROR_ALREADY_EXISTS) {
            ::Sleep(600);
            w = ::FindWindowW(L"SonarConfigGUI", nullptr);
            if (w) { ::ShowWindow(w, SW_RESTORE); ::SetForegroundWindow(w); }
            return 0;
        }
        // 互斥句柄故意不释放：进程生命周期内一直持有，挡住后续重复启动
    }

    // ★ DPI 感知必须最先声明。
    //   WebView2 是按"物理像素"跟宿主对账的：如果宿主是 DPI 不感知的老式进程，
    //   Windows 会做坐标虚拟化，GetClientRect 拿到的和 WebView 实际渲染尺寸对不上，
    //   页面就会按错误的视口排版 —— 表现出来就是"内容比窗口宽、右边被切掉"。
    //   v2 = 每显示器 DPI 感知（多屏不同缩放也不会错）。
    //   老系统没有这个 API 时退回 SetProcessDPIAware()。
    {
        HMODULE u32 = ::GetModuleHandleW(L"user32.dll");
        typedef BOOL(WINAPI* PFN_SetCtx)(DPI_AWARENESS_CONTEXT);
        auto setCtx = u32 ? (PFN_SetCtx)::GetProcAddress(u32, "SetProcessDpiAwarenessContext") : nullptr;
        if (setCtx) setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else ::SetProcessDPIAware();
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogLine("---- start (dpi aware) ----");

    // ---- 窗口类（图标沿用 resource.rc 里的 IDI_APP）----
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = (HICON)::LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)::LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                     ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"SonarConfigGUI";
    ::RegisterClassExW(&wc);

    RECT wr = { 0, 0, 1400, 900 };
    ::AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"Sonar 配置工具",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  wr.right - wr.left, wr.bottom - wr.top,
                                  nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        // 以前这里直接 return 1，正是"双击毫无反应"的一种来源
        const unsigned long le = (unsigned long)::GetLastError();
        LogLine("CreateWindowExW failed, lastError=%lu", le);
        ::MessageBoxW(nullptr,
            (std::wstring(L"创建窗口失败（错误码 ") + std::to_wstring(le) + L"）。\n\n"
             L"如果持续出现，请把目录下的 sonar_gui.log 发给作者。").c_str(),
            L"Sonar 配置工具", MB_ICONERROR);
        return 1;
    }
    g_hwnd = hwnd;
    g_core.SetOwnerWindow(hwnd);
    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    // ---- 启动前自检：WebView2Loader.dll / web 目录缺一不可，缺了就明确提示 ----
    {
        if (!::PathFileExistsW((ExeDirW() + L"web\\index.html").c_str())) {
            ::MessageBoxW(hwnd,
                L"缺少 web 文件夹。\n\n"
                L"界面文件必须和 WeaponSoundEnhanceGUI.exe 放在一起。\n"
                L"请把整个发布包解压到同一目录后运行，不要单独拷贝 exe。",
                L"Sonar 配置工具", MB_ICONERROR);
            return 1;
        }
        if (!::PathFileExistsW((ExeDirW() + L"weapons_icons").c_str())) {
            // 只缺图标不致命，提一声就行，不用拦着不让用
            LogLine("warning: weapons_icons folder missing, icons will show placeholders");
        }
    }

    // ---- 拿 WebView2 入口 ----
    PFN_CreateEnv createEnv = LoadWebView2Entry();
    LogLine("loader: %s", createEnv ? "ok" : "MISSING");
    if (!createEnv) {
        ::MessageBoxW(hwnd,
            L"找不到 WebView2Loader.dll。\n\n"
            L"它必须和 WeaponSoundEnhanceGUI.exe 放在同一个目录里（发布包里自带）。",
            L"Sonar 配置工具", MB_ICONERROR);
        return 1;
    }

    std::wstring dataDir = UserDataDir();
    const std::wstring webRoot = ExeDirW();          // 映射根 = exe 目录
    ClearWebViewCacheIfChanged(dataDir, webRoot);    // 别让旧 CSS/JS 赖在磁盘缓存里
    LogLine("dataDir=%s", WideToUtf8(dataDir).c_str());
    LogLine("webRoot=%s", WideToUtf8(webRoot).c_str());

    HRESULT hr = createEnv(
        nullptr, dataDir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [webRoot](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                LogLine("env handler: hr=0x%08X env=%p", (unsigned)res, (void*)env);
                if (FAILED(res) || !env) {
                    ::MessageBoxW(g_hwnd,
                        L"WebView2 初始化失败。\n\n"
                        L"请确认已安装 Microsoft Edge WebView2 Runtime"
                        L"（Win11 和大多数 Win10 自带）。",
                        L"Sonar 配置工具", MB_ICONERROR);
                    return res;
                }
                env->CreateCoreWebView2Controller(
                    g_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [webRoot](HRESULT r2, ICoreWebView2Controller* ctrl) -> HRESULT {
                            LogLine("ctrl handler: hr=0x%08X ctrl=%p", (unsigned)r2, (void*)ctrl);
                            if (FAILED(r2) || !ctrl) {
                                // 最常见的两个原因：
                                //  - 上一个实例的 WebView2 残留进程还占着数据目录（会返回 ERROR_BUSY 0x800700AA）
                                //  - 安全软件拦截了 msedgewebview2.exe 子进程
                                wchar_t hb[16] = {};
                                ::swprintf_s(hb, L"%08X", (unsigned)r2);
                                const std::wstring msg =
                                    std::wstring(L"WebView2 初始化未完成（错误码 0x") + hb + L"）。\n\n"
                                    L"常见原因与处理：\n"
                                    L"1) 上一个窗口进程可能还在后台 → 打开任务管理器结束 "
                                    L"WeaponSoundEnhanceGUI.exe 和 msedgewebview2.exe 再试；\n"
                                    L"2) 杀毒软件拦截了 WebView2 组件 → 把本目录加入白名单再试；\n"
                                    L"3) 如果反复失败，把目录下的 sonar_gui.log 发给作者。";
                                ::MessageBoxW(g_hwnd, msg.c_str(),
                                    L"Sonar 配置工具", MB_ICONERROR);
                                return r2;
                            }
                            g_ctrl = ctrl;
                            ctrl->get_CoreWebView2(&g_web);
                            LogLine("corewebview2=%p", (void*)g_web.Get());
                            if (!g_web) {
                                ::MessageBoxW(g_hwnd,
                                    L"无法取得 WebView2 内核。\n\n请把目录下的 sonar_gui.log 发给作者。",
                                    L"Sonar 配置工具", MB_ICONERROR);
                                return E_FAIL;
                            }

                            // 禁掉右键菜单/开发者工具入口以外的杂项，保持干净
                            ComPtr<ICoreWebView2Settings> st;
                            if (SUCCEEDED(g_web->get_Settings(&st)) && st) {
                                st->put_AreDefaultContextMenusEnabled(FALSE);
                                st->put_IsStatusBarEnabled(FALSE);
                                st->put_AreDevToolsEnabled(TRUE);   // 出问题能 F12 查
                            }

                            // 把 exe 目录映射成 https://sonar.local/ —— 这样
                            // web/ 里引用的 ../weapons_icons/、../sonar_icon.png
                            // 都落在映射根内部，不会被 WebView2 拦掉。
                            // 【注意】SetVirtualHostNameToFolderMapping 在
                            // ICoreWebView2_3 上，不是 ICoreWebView2 —— 要 QueryInterface。
                            ComPtr<ICoreWebView2_3> web3;
                            if (SUCCEEDED(g_web.As(&web3)) && web3) {
                                web3->SetVirtualHostNameToFolderMapping(
                                    L"sonar.local", webRoot.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            }

                            // JS → C++
                            g_web->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        // 前端发的是字符串（自己 JSON.stringify 过），
                                        // 所以用 TryGetWebMessageAsString；拿不到再退回 AsJson
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            OnMessage(WideToUtf8(raw));
                                            ::CoTaskMemFree(raw);
                                        } else if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
                                            OnMessage(WideToUtf8(raw));
                                            ::CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(),
                                nullptr);

                            ResizeWebView();
                            g_web->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                                        BOOL ok = FALSE;
                                        a->get_IsSuccess(&ok);
                                        COREWEBVIEW2_WEB_ERROR_STATUS es = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                        a->get_WebErrorStatus(&es);
                                        LogLine("nav completed: success=%d webErr=%d", (int)ok, (int)es);
                                        // 把页面真实视口/DPR/生效主题打出来 —— 布局或主题不对劲时，
                                        // 这一行能直接区分"是 CSS 没生效"还是"视口尺寸就不是我以为的"
                                        // 自检延后几秒：等前端异步 refresh() 把数据渲染完再量。
                                        // 给足时间 —— 首次启动（缓存刚清）时首屏会慢一些，
                                        // 太早量会数到"只有左栏、表格还没建"的中间状态。
                                        ::SetTimer(g_hwnd, 2, 7000, nullptr);
                                        return S_OK;
                                    }).Get(),
                                nullptr);
                            LogLine("navigating to https://sonar.local/web/index.html ...");
                            g_web->Navigate(L"https://sonar.local/web/index.html");
                            g_ready = true;
                            StartPolling();   // 实时捕获 / 更新进度靠它主动推给前端
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    LogLine("createEnv hr=0x%08X", (unsigned)hr);
    if (FAILED(hr)) {
        ::MessageBoxW(hwnd, L"无法启动 WebView2。", L"Sonar 配置工具", MB_ICONERROR);
        return 1;
    }
    LogLine("enter message loop");

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    g_web.Reset();
    g_ctrl.Reset();
    ::CoUninitialize();
    return 0;
}
