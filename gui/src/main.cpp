#include "app.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <windows.h>
#include <cstdio>
#include <cstring>

// 应用图标资源 ID（见 resource.rc）
#ifndef IDI_APP
#define IDI_APP 100
#endif

// 后端为避免拖入 <windows.h> 依赖，把该函数声明放进了 #if 0，这里手动前向声明
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ---------------- 主窗口（配置工具界面） ----------------
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ImGuiContext*            g_mainCtx = nullptr;

// ---------------- 独立编辑窗口 ----------------
static ID3D11Device*            g_edDevice = nullptr;
static ID3D11DeviceContext*     g_edDeviceCtx = nullptr;
static IDXGISwapChain*          g_edSwapChain = nullptr;
static ID3D11RenderTargetView*  g_edRtv = nullptr;
static UINT                     g_edResizeWidth = 0, g_edResizeHeight = 0;
static ImGuiContext*            g_edCtx = nullptr;
static HWND                     g_edHwnd = nullptr;
static bool                     g_edInit = false;
static bool                     g_edVisible = false;
static bool                     g_edCloseRequested = false;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI EditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CreateRenderTarget();
void CleanupRenderTarget();
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
bool CreateEditorWindow(HINSTANCE hInstance, float dpiScale);
void CleanupEditor();

// 统一浅色主题（主窗与编辑窗套用同一份）
static void ApplyLightStyle() {
    ImGui::StyleColorsLight();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(12, 10);
    st.FramePadding = ImVec2(11, 6);
    st.ItemSpacing = ImVec2(9, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.IndentSpacing = 18;
    st.ScrollbarSize = 11;
    st.GrabMinSize = 10;
    st.WindowRounding = 8.0f;
    st.ChildRounding = 8.0f;
    st.FrameRounding = 6.0f;
    st.PopupRounding = 8.0f;
    st.ScrollbarRounding = 8.0f;
    st.GrabRounding = 6.0f;
    st.TabRounding = 6.0f;
    st.WindowBorderSize = 0.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 1.0f;
    st.PopupBorderSize = 1.0f;

    ImVec4* c = st.Colors;
    ImVec4 text     = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
    ImVec4 textDim  = ImVec4(0.53f, 0.53f, 0.56f, 1.0f);
    ImVec4 bg       = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    ImVec4 card     = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
    ImVec4 accent   = ImVec4(0.00f, 0.48f, 1.00f, 1.0f);
    ImVec4 sep      = ImVec4(0.82f, 0.82f, 0.84f, 1.0f);
    ImVec4 frameBg  = ImVec4(0.93f, 0.93f, 0.95f, 1.0f);
    ImVec4 frameBgH = ImVec4(0.89f, 0.89f, 0.91f, 1.0f);
    ImVec4 frameBgA = ImVec4(0.85f, 0.85f, 0.88f, 1.0f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = card;
    c[ImGuiCol_PopupBg] = card;
    c[ImGuiCol_Border] = sep;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frameBg;
    c[ImGuiCol_FrameBgHovered] = frameBgH;
    c[ImGuiCol_FrameBgActive] = frameBgA;
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.74f, 0.74f, 0.77f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.67f, 0.67f, 0.71f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.60f, 0.60f, 0.64f, 1.0f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.40f, 0.86f, 1.0f);
    c[ImGuiCol_Button] = frameBg;
    c[ImGuiCol_ButtonHovered] = frameBgH;
    c[ImGuiCol_ButtonActive] = frameBgA;
    c[ImGuiCol_Header] = ImVec4(0.0f, 0.48f, 1.0f, 0.18f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.48f, 1.0f, 0.25f);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Separator] = sep;
    c[ImGuiCol_SeparatorHovered] = sep;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.75f, 0.75f, 0.78f, 1.0f);
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.97f, 0.97f, 0.98f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = sep;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.90f, 0.90f, 0.92f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.48f, 1.0f, 0.25f);
    c[ImGuiCol_NavHighlight] = accent;
}

// 加载中文字体（微软雅黑优先）到当前上下文
static void LoadChineseFont(float fontSize) {
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = nullptr;
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\Deng.ttf",
    };
    for (const char* f : fontCandidates) {
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) {
            font = io.Fonts->AddFontFromFileTTF(f, fontSize, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            if (font) break;
        }
    }
    if (!font) io.Fonts->AddFontDefault();
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 保存调用方上下文。GetOpenFileNameW / GetSaveFileNameW 等模态对话框运行期间，
    // Windows 会向主窗口派发消息，若不恢复，会把编辑窗口帧（g_edCtx）中剩余
    // 的 ImGui 调用带到 g_mainCtx 上执行，破坏 ImGui 窗口栈（CurrentWindow 悬空 → 访问违例）。
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (g_mainCtx && prevCtx != g_mainCtx) ImGui::SetCurrentContext(g_mainCtx);
    bool handled = false;
    if (g_mainCtx && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        handled = true;
    LRESULT result = 0;
    if (handled) {
        result = true;
    } else {
        switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_ResizeWidth = (UINT)LOWORD(lParam);
                g_ResizeHeight = (UINT)HIWORD(lParam);
            }
            result = 0;
            handled = true;
            break;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) { result = 0; handled = true; } // 禁用 Alt 菜单
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            result = 0;
            handled = true;
            break;
        default:
            break;
        }
        if (!handled)
            result = DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    if (prevCtx) ImGui::SetCurrentContext(prevCtx);
    return result;
}

LRESULT WINAPI EditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 与 WndProc 相同：保存/恢复调用方上下文，防止模态对话框消息把 GImGui 切走。
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (g_edCtx && prevCtx != g_edCtx) ImGui::SetCurrentContext(g_edCtx);
    bool handled = false;
    if (g_edCtx && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        handled = true;
    LRESULT result = 0;
    if (handled) {
        result = true;
    } else {
        switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_edResizeWidth = (UINT)LOWORD(lParam);
                g_edResizeHeight = (UINT)HIWORD(lParam);
            }
            result = 0;
            handled = true;
            break;
        case WM_CLOSE:
            g_edCloseRequested = true;
            result = 0;
            handled = true;
            break;
        default:
            break;
        }
        if (!handled)
            result = DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    if (prevCtx) ImGui::SetCurrentContext(prevCtx);
    return result;
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void CreateEditorRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_edSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_edDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_edRtv);
        pBackBuffer->Release();
    }
}

void CleanupEditorRenderTarget() {
    if (g_edRtv) { g_edRtv->Release(); g_edRtv = nullptr; }
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, featureLevels, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// 创建独立编辑窗口及其渲染资源/ImGui 上下文（首次打开时调用）
bool CreateEditorWindow(HINSTANCE hInstance, float dpiScale) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
    wc.lpszClassName = L"WSEEditorClass";
    RegisterClassExW(&wc);

    int ww = (int)(640 * dpiScale), wh = (int)(760 * dpiScale);
    // 独立编辑窗：固定尺寸（无右下缩放三角/最大化），保留标题栏可拖动与系统菜单。
    const DWORD edStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    g_edHwnd = CreateWindowExW(0, wc.lpszClassName, L"编辑条目", edStyle,
                               (sx - ww) / 2, (sy - wh) / 2, ww, wh,
                               nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_edHwnd) return false;

    // 独立 D3D11 设备 + 交换链
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 2,
                          D3D11_SDK_VERSION, &g_edDevice, &featureLevel, &g_edDeviceCtx) != S_OK)
        return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_edHwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIDevice* pdxgi = nullptr;
    IDXGIAdapter* pAdapter = nullptr;
    IDXGIFactory* pFactory = nullptr;
    if (g_edDevice->QueryInterface(IID_PPV_ARGS(&pdxgi)) == S_OK &&
        pdxgi->GetAdapter(&pAdapter) == S_OK &&
        pAdapter->GetParent(IID_PPV_ARGS(&pFactory)) == S_OK) {
        HRESULT hr = pFactory->CreateSwapChain(g_edDevice, &sd, &g_edSwapChain);
        pFactory->Release();
        pAdapter->Release();
        pdxgi->Release();
        if (hr != S_OK) return false;
    } else {
        return false;
    }
    CreateEditorRenderTarget();

    // 独立 ImGui 上下文（编辑窗用）
    g_edCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_edCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    LoadChineseFont(16.0f * dpiScale);
    ApplyLightStyle();

    ImGui_ImplWin32_Init(g_edHwnd);
    ImGui_ImplDX11_Init(g_edDevice, g_edDeviceCtx);
    g_edInit = true;
    g_edVisible = true;
    g_edCloseRequested = false;
    ShowWindow(g_edHwnd, SW_SHOW);
    UpdateWindow(g_edHwnd);
    ImGui::SetCurrentContext(g_mainCtx);
    return true;
}

void CleanupEditor() {
    if (!g_edInit) return;
    ImGui::SetCurrentContext(g_edCtx);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_edCtx);
    g_edCtx = nullptr;
    g_edInit = false;

    CleanupEditorRenderTarget();
    if (g_edSwapChain) { g_edSwapChain->Release(); g_edSwapChain = nullptr; }
    if (g_edDeviceCtx) { g_edDeviceCtx->Release(); g_edDeviceCtx = nullptr; }
    if (g_edDevice) { g_edDevice->Release(); g_edDevice = nullptr; }
    if (g_edHwnd) { DestroyWindow(g_edHwnd); g_edHwnd = nullptr; }
    UnregisterClassW(L"WSEEditorClass", GetModuleHandleW(nullptr));
    ImGui::SetCurrentContext(g_mainCtx);
}

// 记录并在弹窗提示一次崩溃，避免程序直接闪退、便于定位。
// 只有 MSVC 的 SEH 路径用得到，非 MSVC 下整块不编译，免得报 unused。
#ifdef _MSC_VER
static void ReportCrash(DWORD code, void* addr);

static DWORD  g_crashCode = 0;
static void*  g_crashAddr = nullptr;
#endif

// 渲染独立编辑窗口一帧的实际内容。抽成独立函数，便于在 MSVC 下用 SEH 包住。
static void RenderEditorFrameBody(App& app, const float* clear) {
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.DrawEditorDetached();
        // 保险：DrawEditorDetached 内部可能打开模态对话框（BrowseSounds 的文件选择），
        // 即使窗口过程未正确恢复，也确保后续 ImGui 提交/渲染始终在编辑窗口上下文。
        ImGui::SetCurrentContext(g_edCtx);
        ImGui::Render();
        g_edDeviceCtx->OMSetRenderTargets(1, &g_edRtv, nullptr);
        g_edDeviceCtx->ClearRenderTargetView(g_edRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_edSwapChain->Present(1, 0);
    }
}

// 崩溃时记录并恢复上下文，不闪退。
// SEH(__try/__except) 是 MSVC 专有语法，GCC/Clang(MinGW) 不支持 ——
// 非 MSVC 下退化为直接调用，崩溃保护失效，其余行为完全一致。
static void RenderEditorFrame(App& app, const float* clear) {
    ImGui::SetCurrentContext(g_edCtx);
#ifdef _MSC_VER
    __try {
        RenderEditorFrameBody(app, clear);
    } __except ((g_crashCode = GetExceptionCode(),
                 g_crashAddr = (void*)GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
                 EXCEPTION_EXECUTE_HANDLER)) {
        ReportCrash(g_crashCode, g_crashAddr);
    }
#else
    RenderEditorFrameBody(app, clear);
#endif
    ImGui::SetCurrentContext(g_mainCtx);
}

#ifdef _MSC_VER
// 记录并在弹窗提示一次崩溃，避免程序直接闪退、便于定位
static void ReportCrash(DWORD code, void* addr) {
    HMODULE hm = GetModuleHandleW(nullptr);
    DWORD_PTR base = (DWORD_PTR)hm;
    DWORD_PTR rva = ((DWORD_PTR)addr >= base) ? ((DWORD_PTR)addr - base) : 0;

    char path[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, path);
    strncat_s(path, "WSE_GUI_crash.txt", MAX_PATH - strlen(path));
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") == 0 && f) {
        SYSTEMTIME st{}; GetLocalTime(&st);
        fprintf(f, "[%02u:%02u:%02u] code=0x%08X addr=0x%p base=0x%p RVA=0x%llX\n",
                st.wHour, st.wMinute, st.wSecond, (unsigned)code, addr, (void*)base,
                (unsigned long long)rva);
        // 调用栈（最多 16 帧，绝对地址）
        void* bt[16];
        const USHORT n = RtlCaptureStackBackTrace(0, 16, bt, nullptr);
        for (USHORT i = 0; i < n; ++i)
            fprintf(f, "    frame[%u] 0x%p (RVA 0x%llX)\n", i, bt[i],
                    ((DWORD_PTR)bt[i] >= base) ? (unsigned long long)((DWORD_PTR)bt[i] - base) : 0ULL);
        fclose(f);
    }
    char msg[512];
    snprintf(msg, sizeof(msg),
             "编辑器发生访问违例(code=0x%08X, RVA=0x%llX)，已拦截避免闪退。\n"
             "请把本窗口截图，或 %s 发给我。",
             (unsigned)code, (unsigned long long)rva, path);
    MessageBoxA(nullptr, msg, "WeaponSoundEnhance GUI", MB_ICONERROR);
}
#endif // _MSC_VER

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    float dpiScale = 1.0f;
    HDC hdc = GetDC(nullptr);
    if (hdc) { dpiScale = (float)GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f; ReleaseDC(nullptr, hdc); }
    int ww = (int)(1180 * dpiScale), wh = (int)(760 * dpiScale);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
    wc.lpszClassName = L"WeaponSoundEnhanceGUI";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"WeaponSoundEnhance 配置工具 (15.23.00)",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                              nullptr, nullptr, wc.hInstance, nullptr);

    SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE));

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    g_mainCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_mainCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    LoadChineseFont(16.0f * dpiScale);
    ApplyLightStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    App app;
    app.hwnd = hwnd;
    app.dpiScale = dpiScale;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // ---- 主窗口界面 ----
        ImGui::SetCurrentContext(g_mainCtx);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Draw();
        ImGui::Render();
        const float clear[4] = { 0.96f, 0.96f, 0.97f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);

        // ---- 独立编辑窗口 ----
        if (g_edCloseRequested) {
            app.editor.open = false;
            g_edCloseRequested = false;
        }
        if (app.editor.open && !g_edInit) {
            CreateEditorWindow(hInstance, dpiScale);
            app.editHwnd = g_edHwnd;   // 同步编辑窗口句柄（供文件对话框后置顶/所有者使用）
        }
        if (app.editor.open && g_edInit && !g_edVisible) {
            ShowWindow(g_edHwnd, SW_SHOW);
            g_edVisible = true;
        }
        if (!app.editor.open && g_edInit && g_edVisible) {
            ShowWindow(g_edHwnd, SW_HIDE);
            g_edVisible = false;
        }
        if (g_edInit && g_edVisible) {
            if (g_edResizeWidth != 0 && g_edResizeHeight != 0) {
                CleanupEditorRenderTarget();
                g_edSwapChain->ResizeBuffers(0, g_edResizeWidth, g_edResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_edResizeWidth = g_edResizeHeight = 0;
                CreateEditorRenderTarget();
            }
            RenderEditorFrame(app, clear);
        }
    }

    CleanupEditor();
    app.editHwnd = nullptr;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_mainCtx);

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
