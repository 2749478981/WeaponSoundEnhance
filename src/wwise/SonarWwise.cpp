// ===========================================================================
//  SonarWwise.cpp —— 手工遍历 PE 导出表，按 mangled 名查 Wwise 函数
//
//  为什么不用 GetProcAddress：
//    1) GetProcAddress 也能用（导出名就是 mangled 名），但它要求拿到的
//       HMODULE 是"数据模块"。游戏里我们拿到的是 exe 自身，没问题，
//       但 GetProcAddress 对 **导出的数据符号** 和某些转发导出支持不佳，
//       且我们想同时打印"找到了没/在什么 RVA"方便排障。
//    2) 自己解析能一次遍历把 7 个目标全找出来，只在诊断时多花几十微秒。
//
//  注意：win32 API 里 GetProcAddress 的名字匹配是逐字节精确的，
//  这里的实现同样精确匹配（mangled 名不能做大小写折叠）。
// ===========================================================================
#include "SonarWwise.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace sonaraudio {
namespace {

// 在模块导出表里查一个名字，返回运行时地址（0 = 没找到）
uintptr_t LookupExportByName(uintptr_t base, const char* wanted) {
    if (!base || !wanted) return 0;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

        const IMAGE_DATA_DIRECTORY& dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress || !dir.Size) return 0;

        const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
        const auto* names  = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
        const auto* ords   = reinterpret_cast<const WORD*>(base + exp->AddressOfNameOrdinals);
        const auto* funcs  = reinterpret_cast<const DWORD*>(base + exp->AddressOfFunctions);

        const DWORD n = exp->NumberOfNames;
        for (DWORD i = 0; i < n; ++i) {
            const char* nm = reinterpret_cast<const char*>(base + names[i]);
            if (std::strcmp(nm, wanted) == 0) {
                const WORD ord = ords[i];
                if (ord >= exp->NumberOfFunctions) return 0;
                const DWORD rva = funcs[ord];
                // 转发导出（RVA 落在导出目录内）——Wwise 不是转发，遇到就跳过
                if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) return 0;
                return base + rva;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 0;
}

// 数一下导出表里有多少个 "?xxx@SoundEngine@AK" 符号（诊断用：确认游戏确实导出了 Wwise）
int CountSoundEngineExports(uintptr_t base) {
    int cnt = 0;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress) return 0;
        const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
        const auto* names = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
            const char* nm = reinterpret_cast<const char*>(base + names[i]);
            if (std::strstr(nm, "SoundEngine@AK@@")) ++cnt;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return cnt;
    }
    return cnt;
}

template <typename T>
void Bind(uintptr_t base, const char* name, T& slot, const char* label, std::string& log) {
    const uintptr_t a = LookupExportByName(base, name);
    slot = reinterpret_cast<T>(a);
    char buf[256];
    if (a) {
        std::snprintf(buf, sizeof(buf), "  [ok]   %-34s @ 0x%p (rva 0x%llX)\n", label, (void*)a,
                      (unsigned long long)(a - base));
    } else {
        std::snprintf(buf, sizeof(buf), "  [MISS] %-34s  (导出表里没有)\n", label);
    }
    log += buf;
}

}  // namespace

bool ResolveWwiseApi(WwiseApi& out, std::string& log) {
    out = WwiseApi{};

    const HMODULE exe = ::GetModuleHandleW(nullptr);
    out.exe_base = reinterpret_cast<uintptr_t>(exe);
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "exe base = 0x%p\n", (void*)out.exe_base);
        log += buf;
    }
    if (!out.exe_base) {
        log += "严重：拿不到 exe 基址\n";
        return false;
    }

    const int nse = CountSoundEngineExports(out.exe_base);
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "导出表里 SoundEngine@AK 符号数 = %d（期望 200+）\n", nse);
        log += buf;
    }

    Bind(out.exe_base, sym::kPostEvent_byID, out.postEvent_byID, "PostEvent(byID)", log);
    Bind(out.exe_base, sym::kPostEvent_byName, out.postEvent_byName, "PostEvent(byName)", log);
    Bind(out.exe_base, sym::kPostEvent_byNameW, out.postEvent_byNameW, "PostEvent(byNameW)", log);
    Bind(out.exe_base, sym::kGetEventIDFromPlayingID, out.getEventIDFromPlayingID,
         "GetEventIDFromPlayingID", log);
    Bind(out.exe_base, sym::kGetPlayingIDsFromGameObject, out.getPlayingIDsFromGameObject,
         "GetPlayingIDsFromGameObject", log);
    Bind(out.exe_base, sym::kStopPlayingID, out.stopPlayingID, "StopPlayingID", log);
    Bind(out.exe_base, sym::kStopAll, out.stopAll, "StopAll", log);

    if (!out.ok()) {
        log += "严重：PostEvent 一个都没找到 —— hook 无法建立。\n";
        log += "可能原因：游戏版本不是 15.23.00；或导出被剥；或地址需要 AOB 兜底。\n";
        return false;
    }
    return true;
}

}  // namespace sonaraudio
