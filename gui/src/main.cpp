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
static UINT                     g_edBufW = 0, g_edBufH = 0;   // 当前交换链缓冲尺寸

// ---------------- 独立 FSM/LMT 查询窗口 ----------------
static ID3D11Device*            g_fsDevice = nullptr;
static ID3D11DeviceContext*     g_fsDeviceCtx = nullptr;
static IDXGISwapChain*          g_fsSwapChain = nullptr;
static ID3D11RenderTargetView*  g_fsRtv = nullptr;
static UINT                     g_fsResizeWidth = 0, g_fsResizeHeight = 0;
static ImGuiContext*            g_fsCtx = nullptr;
static HWND                     g_fsHwnd = nullptr;
static bool                     g_fsInit = false;
static bool                     g_fsVisible = false;
static bool                     g_fsCloseRequested = false;
static UINT                     g_fsBufW = 0, g_fsBufH = 0;   // 当前交换链缓冲尺寸

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI EditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI FsmWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void CreateRenderTarget();
void CleanupRenderTarget();
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
bool CreateEditorWindow(HINSTANCE hInstance, float dpiScale);
void CleanupEditor();
bool CreateFsmWindow(HINSTANCE hInstance, float dpiScale);
void CleanupFsm();
void CreateEditorRenderTarget();
void CleanupEditorRenderTarget();
void CreateFsmRenderTarget();
void CleanupFsmRenderTarget();

// 每帧核对「客户区尺寸」与「交换链缓冲尺寸」，不一致就重建缓冲。
// 只靠 WM_SIZE 会漏（拖拽/最大化/DPI 变化时可能来不及处理），漏掉就会出现
// 内容只占左边一块、右边是黑底的观感。
static void SyncSwapChainSize(HWND hwnd, IDXGISwapChain* sc, UINT& bufW, UINT& bufH,
                              void (*cleanup)(), void (*create)()) {
    if (!hwnd || !sc) return;
    RECT rc = {};
    if (!::GetClientRect(hwnd, &rc)) return;
    const UINT w = (UINT)(rc.right - rc.left);
    const UINT h = (UINT)(rc.bottom - rc.top);
    if (w == 0 || h == 0) return;
    if (w == bufW && h == bufH) return;
    cleanup();
    if (FAILED(sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return;
    create();
    bufW = w;
    bufH = h;
}

// ---- 拖动窗口期间的即时重绘 ----
// 拖动边框时 Windows 会在 DefWindowProc 里跑模态循环，主渲染循环被卡住，
// 新露出的区域就会一直黑着（松手才恢复）。所以在窗口过程的 WM_SIZE/WM_PAINT 里
// 直接同步重绘一帧，把黑块消掉。
static App* g_app = nullptr;
static bool g_paintingNow = false;
// 拖动缩放时的即时重绘不走 vsync，避免等待垂直同步造成内容滞后（背景已由类刷填充，不会黑）
static bool g_noVsyncPresent = false;

static void RenderEditorFrame(App& app, const float* clear);
static void RenderFsmFrame(App& app, const float* clear);

static void KillBlackOnResizeEditor() {
    if (!g_app || !g_edInit || !g_edVisible || g_paintingNow) return;
    g_paintingNow = true;
    g_noVsyncPresent = true;
    SyncSwapChainSize(g_edHwnd, g_edSwapChain, g_edBufW, g_edBufH,
                      CleanupEditorRenderTarget, CreateEditorRenderTarget);
    const float clear[4] = { 0.96f, 0.96f, 0.97f, 1.0f };
    RenderEditorFrame(*g_app, clear);
    g_noVsyncPresent = false;
    g_paintingNow = false;
}

static void KillBlackOnResizeFsm() {
    if (!g_app || !g_fsInit || !g_fsVisible || g_paintingNow) return;
    g_paintingNow = true;
    g_noVsyncPresent = true;
    SyncSwapChainSize(g_fsHwnd, g_fsSwapChain, g_fsBufW, g_fsBufH,
                      CleanupFsmRenderTarget, CreateFsmRenderTarget);
    const float clear[4] = { 0.96f, 0.96f, 0.97f, 1.0f };
    RenderFsmFrame(*g_app, clear);
    g_noVsyncPresent = false;
    g_paintingNow = false;
}

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
                KillBlackOnResizeEditor();   // 拖动中即时重绘，避免新露出区域发黑
            }
            result = 0;
            handled = true;
            break;
        case WM_PAINT:
            KillBlackOnResizeEditor();
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

LRESULT WINAPI FsmWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 与 EditorWndProc 相同：保存/恢复上下文，防止模态对话框消息把 GImGui 切走
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (g_fsCtx && prevCtx != g_fsCtx) ImGui::SetCurrentContext(g_fsCtx);
    bool handled = false;
    if (g_fsCtx && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        handled = true;
    LRESULT result = 0;
    if (handled) {
        result = true;
    } else {
        switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_fsResizeWidth = (UINT)LOWORD(lParam);
                g_fsResizeHeight = (UINT)HIWORD(lParam);
                KillBlackOnResizeFsm();   // 拖动中即时重绘
            }
            result = 0;
            handled = true;
            break;
        case WM_PAINT:
            KillBlackOnResizeFsm();
            break;
        case WM_CLOSE:
            g_fsCloseRequested = true;
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

void CreateFsmRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_fsSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_fsDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_fsRtv);
        pBackBuffer->Release();
    }
}

void CleanupFsmRenderTarget() {
    if (g_fsRtv) { g_fsRtv->Release(); g_fsRtv = nullptr; }
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
// 拖动缩放时新露出的区域：类背景刷让系统立刻用浅色填上（否则会是黑块）
static HBRUSH g_lightBrush = nullptr;
static HBRUSH LightBrush() {
    if (!g_lightBrush) g_lightBrush = ::CreateSolidBrush(RGB(245, 245, 247));
    return g_lightBrush;
}

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
    wc.hbrBackground = LightBrush();
    wc.lpszClassName = L"WSEEditorClass";
    RegisterClassExW(&wc);

    int ww = (int)(680 * dpiScale), wh = (int)(820 * dpiScale);
    // 独立编辑窗：可自由拉伸（WS_THICKFRAME）、可最大化，标题栏可拖动
    const DWORD edStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
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
    {
        RECT rc = {};
        if (::GetClientRect(g_edHwnd, &rc)) {
            g_edBufW = (UINT)(rc.right - rc.left);
            g_edBufH = (UINT)(rc.bottom - rc.top);
        }
    }

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

bool CreateFsmWindow(HINSTANCE hInstance, float dpiScale) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = FsmWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
    wc.hbrBackground = LightBrush();
    wc.lpszClassName = L"WSEFsmClass";
    RegisterClassExW(&wc);

    int ww = (int)(430 * dpiScale), wh = (int)(540 * dpiScale);
    // 独立查询窗：允许拉伸（WS_THICKFRAME），标题栏可拖动
    const DWORD fsStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
    const int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    g_fsHwnd = CreateWindowExW(0, wc.lpszClassName, L"FSM/LMT 查询", fsStyle,
                               (sx - ww) / 2 + 40, (sy - wh) / 2 + 40, ww, wh,
                               nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_fsHwnd) return false;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 2,
                          D3D11_SDK_VERSION, &g_fsDevice, &featureLevel, &g_fsDeviceCtx) != S_OK)
        return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_fsHwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIDevice* pdxgi = nullptr;
    IDXGIAdapter* pAdapter = nullptr;
    IDXGIFactory* pFactory = nullptr;
    if (g_fsDevice->QueryInterface(IID_PPV_ARGS(&pdxgi)) == S_OK &&
        pdxgi->GetAdapter(&pAdapter) == S_OK &&
        pAdapter->GetParent(IID_PPV_ARGS(&pFactory)) == S_OK) {
        HRESULT hr = pFactory->CreateSwapChain(g_fsDevice, &sd, &g_fsSwapChain);
        pFactory->Release();
        pAdapter->Release();
        pdxgi->Release();
        if (hr != S_OK) return false;
    } else {
        return false;
    }
    CreateFsmRenderTarget();
    {
        RECT rc = {};
        if (::GetClientRect(g_fsHwnd, &rc)) {
            g_fsBufW = (UINT)(rc.right - rc.left);
            g_fsBufH = (UINT)(rc.bottom - rc.top);
        }
    }

    // 独立 ImGui 上下文（FSM 查询窗用）
    g_fsCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_fsCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    LoadChineseFont(16.0f * dpiScale);
    ApplyLightStyle();

    ImGui_ImplWin32_Init(g_fsHwnd);
    ImGui_ImplDX11_Init(g_fsDevice, g_fsDeviceCtx);
    g_fsInit = true;
    g_fsVisible = true;
    g_fsCloseRequested = false;
    ShowWindow(g_fsHwnd, SW_SHOW);
    UpdateWindow(g_fsHwnd);
    ImGui::SetCurrentContext(g_mainCtx);
    return true;
}

void CleanupFsm() {
    if (!g_fsInit) return;
    ImGui::SetCurrentContext(g_fsCtx);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_fsCtx);
    g_fsCtx = nullptr;
    g_fsInit = false;

    CleanupFsmRenderTarget();
    if (g_fsSwapChain) { g_fsSwapChain->Release(); g_fsSwapChain = nullptr; }
    if (g_fsDeviceCtx) { g_fsDeviceCtx->Release(); g_fsDeviceCtx = nullptr; }
    if (g_fsDevice) { g_fsDevice->Release(); g_fsDevice = nullptr; }
    if (g_fsHwnd) { DestroyWindow(g_fsHwnd); g_fsHwnd = nullptr; }
    UnregisterClassW(L"WSEFsmClass", GetModuleHandleW(nullptr));
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
        // 后端 NewFrame 之后再强制一次：画布尺寸严格等于当前客户区，
        // 任何"后端缓存/时序"造成的尺寸滞后都被这一步覆盖掉（否则会黑边或内容被裁）。
        RECT rc = {};
        if (::GetClientRect(g_edHwnd, &rc)) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
            io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        }
        ImGui::NewFrame();
        app.DrawEditorDetached();
        // 保险：DrawEditorDetached 内部可能打开模态对话框（BrowseSounds 的文件选择），
        // 即使窗口过程未正确恢复，也确保后续 ImGui 提交/渲染始终在编辑窗口上下文。
        ImGui::SetCurrentContext(g_edCtx);
        ImGui::Render();
        g_edDeviceCtx->OMSetRenderTargets(1, &g_edRtv, nullptr);
        g_edDeviceCtx->ClearRenderTargetView(g_edRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_edSwapChain->Present(g_noVsyncPresent ? 0u : 1u, 0);
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

// FSM/LMT 查询独立窗口
static void RenderFsmFrameBody(App& app, const float* clear) {
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        RECT rc = {};
        if (::GetClientRect(g_fsHwnd, &rc)) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
            io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        }
        ImGui::NewFrame();
        app.DrawFsmWindow();
        // 保险：与编辑窗一致，确保后续提交/渲染始终在查询窗上下文
        ImGui::SetCurrentContext(g_fsCtx);
        ImGui::Render();
        g_fsDeviceCtx->OMSetRenderTargets(1, &g_fsRtv, nullptr);
        g_fsDeviceCtx->ClearRenderTargetView(g_fsRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_fsSwapChain->Present(g_noVsyncPresent ? 0u : 1u, 0);
    }
}

static void RenderFsmFrame(App& app, const float* clear) {
    ImGui::SetCurrentContext(g_fsCtx);
#ifdef _MSC_VER
    __try {
        RenderFsmFrameBody(app, clear);
    } __except ((g_crashCode = GetExceptionCode(),
                 g_crashAddr = (void*)GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
                 EXCEPTION_EXECUTE_HANDLER)) {
        ReportCrash(g_crashCode, g_crashAddr);
    }
#else
    RenderFsmFrameBody(app, clear);
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
    g_app = &app;   // 供窗口过程在拖动缩放的模态循环里即时重绘
    // 调试钩子：设置环境变量 WSE_OPEN_FSM=1 时启动即打开 FSM 查询独立窗口（用于冒烟/截图；不设置时无影响）
    char envBuf[16] = {};
    if (GetEnvironmentVariableA("WSE_OPEN_FSM", envBuf, sizeof(envBuf)) > 0 && envBuf[0] == '1')
        app.fsmWinOpen = true;
    if (GetEnvironmentVariableA("WSE_OPEN_EDITOR", envBuf, sizeof(envBuf)) > 0 && envBuf[0] == '1')
        app.OpenEditorNew(3);   // 打开太刀的编辑窗口（冒烟用）

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
            SyncSwapChainSize(g_edHwnd, g_edSwapChain, g_edBufW, g_edBufH,
                              CleanupEditorRenderTarget, CreateEditorRenderTarget);
            RenderEditorFrame(app, clear);
        }

        // ---- 独立 FSM/LMT 查询窗口 ----
        if (g_fsCloseRequested) {
            app.fsmWinOpen = false;
            g_fsCloseRequested = false;
        }
        if (app.fsmWinOpen && !g_fsInit) {
            CreateFsmWindow(hInstance, dpiScale);
        }
        if (app.fsmWinOpen && g_fsInit && !g_fsVisible) {
            ShowWindow(g_fsHwnd, SW_SHOW);
            g_fsVisible = true;
        }
        if (!app.fsmWinOpen && g_fsInit && g_fsVisible) {
            ShowWindow(g_fsHwnd, SW_HIDE);
            g_fsVisible = false;
        }
        if (g_fsInit && g_fsVisible) {
            SyncSwapChainSize(g_fsHwnd, g_fsSwapChain, g_fsBufW, g_fsBufH,
                              CleanupFsmRenderTarget, CreateFsmRenderTarget);
            RenderFsmFrame(app, clear);
        }
    }

    CleanupFsm();
    CleanupEditor();
    if (g_lightBrush) { ::DeleteObject(g_lightBrush); g_lightBrush = nullptr; }
    app.editHwnd = nullptr;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_mainCtx);

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
