// ===========================================================================
//  WseWwiseProbe.dll —— 验证：我们自己的 DLL 直接 hook Wwise PostEvent
//
//  这是"把 wem 捕获并进 WeaponSoundEnhance.dll"的**验证步**：
//   1) 解析 MonsterHunterWorld.exe 的 PE 导出表，按 mangled 名找
//        AK::SoundEngine::PostEvent（3 个重载）+ 反查/停止函数
//   2) 用 MinHook 挂 detour
//   3) hook 回调只做原子记账；后台线程写日志（不写盘/不分配/不加锁）
//
//  只写日志、只读内存 —— 不触发音效、不改游戏状态。
//  退出：DETACH 置停止标志并等线程收尾；不停留后台线程（避免上次那种残留）。
// ===========================================================================
#include <windows.h>
#include <atomic>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstdint>

#include "MinHook.h"

namespace {

std::atomic<bool> g_stop{false};
std::wstring g_dir;

void Log(const char* fmt, ...) {
    const std::wstring path = g_dir + L"WseWwiseProbe.log";
    HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; ::GetLocalTime(&st);
    char head[80];
    wsprintfA(head, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    DWORD w = 0;
    ::WriteFile(h, head, (DWORD)strlen(head), &w, nullptr);
    va_list ap; va_start(ap, fmt);
    char body[512];
    vsprintf_s(body, fmt, ap);
    va_end(ap);
    ::WriteFile(h, body, (DWORD)strlen(body), &w, nullptr);
    ::WriteFile(h, "\r\n", 2, &w, nullptr);
    ::CloseHandle(h);
}

// ---- 导出表解析（ASLR 安全）------------------------------------------------
uintptr_t LookupExport(uintptr_t base, const char* wanted) {
    if (!base || !wanted) return 0;
    __try {
        const auto* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress || !dir.Size) return 0;
        const auto* exp = (const IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
        const auto* names = (const DWORD*)(base + exp->AddressOfNames);
        const auto* ords = (const WORD*)(base + exp->AddressOfNameOrdinals);
        const auto* funcs = (const DWORD*)(base + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
            const char* nm = (const char*)(base + names[i]);
            if (strcmp(nm, wanted) == 0) {
                const WORD ord = ords[i];
                if (ord >= exp->NumberOfFunctions) return 0;
                const DWORD rva = funcs[ord];
                if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) return 0;
                return base + rva;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 0;
}

int CountAkExports(uintptr_t base) {
    int cnt = 0;
    __try {
        const auto* dos = (const IMAGE_DOS_HEADER*)base;
        const auto* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress) return 0;
        const auto* exp = (const IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
        const auto* names = (const DWORD*)(base + exp->AddressOfNames);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i)
            if (strstr((const char*)(base + names[i]), "SoundEngine@AK@@")) ++cnt;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return cnt; }
    return cnt;
}

// ---- 符号名（mangled，取自 ref/wwise_symbols.txt）--------------------------
constexpr const char* kPost_byID =
    "?PostEvent@SoundEngine@AK@@YAKK_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z";
constexpr const char* kPost_byName =
    "?PostEvent@SoundEngine@AK@@YAKPEBD_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z";
constexpr const char* kPost_byNameW =
    "?PostEvent@SoundEngine@AK@@YAKPEB_W_KKP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXKPEAUAkExternalSourceInfo@@K@Z";
constexpr const char* kGetEvtFromPlaying =
    "?GetEventIDFromPlayingID@Query@SoundEngine@AK@@YAKK@Z";

using PostByID_t  = uint32_t (*)(uint32_t, uint64_t, uint32_t, void*, void*, uint32_t, void*, uint32_t);
using PostByName_t = uint32_t (*)(const char*, uint64_t, uint32_t, void*, void*, uint32_t, void*, uint32_t);
using PostByNameW_t = uint32_t (*)(const wchar_t*, uint64_t, uint32_t, void*, void*, uint32_t, void*, uint32_t);
using GetEvtByPlaying_t = uint32_t (*)(uint32_t);

PostByID_t   g_trampID = nullptr;
PostByName_t g_trampName = nullptr;
PostByNameW_t g_trampNameW = nullptr;
GetEvtByPlaying_t g_getEvtFromPlaying = nullptr;

std::atomic<uint64_t> g_events{0};
std::atomic<uint32_t> g_lastEvent{0}, g_lastPlaying{0}, g_lastGobj{0};

uint32_t __cdecl Hook_PostByID(uint32_t ev, uint64_t gobj, uint32_t flags, void* cb,
                               void* cookie, uint32_t nExt, void* ext, uint32_t pid) {
    const uint32_t playing = g_trampID ? g_trampID(ev, gobj, flags, cb, cookie, nExt, ext, pid) : 0;
    g_lastEvent.store(ev); g_lastPlaying.store(playing); g_lastGobj.store((uint32_t)gobj);
    g_events.fetch_add(1, std::memory_order_relaxed);
    return playing;
}
uint32_t __cdecl Hook_PostByName(const char* name, uint64_t gobj, uint32_t flags, void* cb,
                                 void* cookie, uint32_t nExt, void* ext, uint32_t pid) {
    const uint32_t playing = g_trampName ? g_trampName(name, gobj, flags, cb, cookie, nExt, ext, pid) : 0;
    uint32_t ev = 0;
    if (name) for (const char* p = name; *p; ++p) ev = ev * 131u + (uint8_t)*p;
    g_lastEvent.store(ev); g_lastPlaying.store(playing); g_lastGobj.store((uint32_t)gobj);
    g_events.fetch_add(1, std::memory_order_relaxed);
    return playing;
}
uint32_t __cdecl Hook_PostByNameW(const wchar_t* name, uint64_t gobj, uint32_t flags, void* cb,
                                  void* cookie, uint32_t nExt, void* ext, uint32_t pid) {
    const uint32_t playing = g_trampNameW ? g_trampNameW(name, gobj, flags, cb, cookie, nExt, ext, pid) : 0;
    g_lastEvent.store(0); g_lastPlaying.store(playing); g_lastGobj.store((uint32_t)gobj);
    g_events.fetch_add(1, std::memory_order_relaxed);
    return playing;
}

void Worker() {
    const uintptr_t exe = (uintptr_t)::GetModuleHandleW(nullptr);
    Log("exe base=0x%p  SoundEngine@AK 导出符号=%d", (void*)exe, CountAkExports(exe));

    const uintptr_t pID = LookupExport(exe, kPost_byID);
    const uintptr_t pName = LookupExport(exe, kPost_byName);
    const uintptr_t pNameW = LookupExport(exe, kPost_byNameW);
    const uintptr_t pGet = LookupExport(exe, kGetEvtFromPlaying);
    Log("PostEvent byID    = 0x%p (rva 0x%llX)", (void*)pID, (unsigned long long)(pID ? pID - exe : 0));
    Log("PostEvent byName  = 0x%p (rva 0x%llX)", (void*)pName, (unsigned long long)(pName ? pName - exe : 0));
    Log("PostEvent byNameW = 0x%p (rva 0x%llX)", (void*)pNameW, (unsigned long long)(pNameW ? pNameW - exe : 0));
    Log("GetEventIDFromPlayingID = 0x%p", (void*)pGet);
    g_getEvtFromPlaying = (GetEvtByPlaying_t)pGet;

    if (MH_Initialize() != MH_OK) { Log("MH_Initialize 失败"); return; }
    int n = 0;
    if (pID && MH_CreateHook((LPVOID)pID, (LPVOID)&Hook_PostByID, (LPVOID*)&g_trampID) == MH_OK) ++n;
    if (pName && MH_CreateHook((LPVOID)pName, (LPVOID)&Hook_PostByName, (LPVOID*)&g_trampName) == MH_OK) ++n;
    if (pNameW && MH_CreateHook((LPVOID)pNameW, (LPVOID)&Hook_PostByNameW, (LPVOID*)&g_trampNameW) == MH_OK) ++n;
    Log("MH_CreateHook 成功 %d/3", n);
    if (n == 0) { Log("没有可用 hook，退出"); return; }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { Log("MH_EnableHook 失败"); return; }
    Log("hook 已启用 —— 进游戏做动作看事件计数");

    uint64_t lastSeen = 0;
    uint64_t lastBeat = 0;
    while (!g_stop.load()) {
        ::Sleep(200);
        const uint64_t now = ::GetTickCount64();
        const uint64_t total = g_events.load();
        if (total != lastSeen) {
            const uint32_t ev = g_lastEvent.load(), pl = g_lastPlaying.load(), go = g_lastGobj.load();
            uint32_t rev = 0;
            if (g_getEvtFromPlaying && pl) rev = g_getEvtFromPlaying(pl);
            Log("event: id=%u playing=%u gobj=%u  反查eventID=%u  (累计 %llu)",
                ev, pl, go, rev, (unsigned long long)total);
            lastSeen = total;
        }
        if (now - lastBeat > 10000) {
            Log("心跳：累计事件 %llu", (unsigned long long)total);
            lastBeat = now;
        }
    }
    Log("worker 退出");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t buf[MAX_PATH + 4] = {};
        ::GetModuleFileNameW(m, buf, MAX_PATH);
        g_dir = buf;
        const size_t s = g_dir.find_last_of(L"\\/");
        g_dir = (s == std::wstring::npos) ? L"" : g_dir.substr(0, s + 1);
        ::DisableThreadLibraryCalls(m);
        std::thread(Worker).detach();
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop.store(true);
        ::Sleep(400);   // 让 worker 收尾，不留后台线程
    }
    return TRUE;
}