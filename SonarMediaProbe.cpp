// ===========================================================================
//  SonarMediaProbe.dll —— Wwise 播放入口特侦（只读，不 hook，零侵入）
//
//  目标：定位游戏内 Wwise（静态链接进 MonsterHunterWorld.exe）的播放入口，
//        为后续"抓正在播放的 media id"铺路。
//
//  做法（全部只读内存，安全）：
//   1. 打印模块信息与 .text/.rdata 段范围
//   2. 在 .rdata 里找 Wwise 固定字符串锚点（PostEvent / ExecuteActionOnEvent
//      / IAkStreamMgr…）—— 这些调试/断言字符串在 Wwise 库里是固定文本
//   3. 对每个锚点做简单的"代码引用扫描"：在 .text 里找
//      lea r64, [rip+rel32] / mov r64, imm32 指向该字符串的指令地址
//      —— 指令附近往往就是调用这个 API 的函数（或它的报错包装）
//   4. 顺带尝试几个“推测”的函数 prologue 特征码，命中会额外打出来
//
//  用法：放进 nativePC\plugins\，进游戏，退出后把日志发作者。
// ===========================================================================
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

namespace {

std::wstring g_dir;

void Log(const char* fmt, ...) {
    const std::wstring path = g_dir + L"SonarMediaProbe.log";
    HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; ::GetLocalTime(&st);
    char buf[96];
    wsprintfA(buf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    DWORD w = 0;
    ::WriteFile(h, buf, (DWORD)std::strlen(buf), &w, nullptr);
    va_list ap; va_start(ap, fmt);
    char big[512];
    vsprintf_s(big, fmt, ap);
    va_end(ap);
    ::WriteFile(h, big, (DWORD)std::strlen(big), &w, nullptr);
    ::WriteFile(h, "\r\n", 2, &w, nullptr);
    ::CloseHandle(h);
}

struct Seg { std::uintptr_t va; std::size_t size; std::string name; };

bool SafeRead(std::uintptr_t p, void* d, std::size_t n) {
    __try { std::memcpy(d, (void*)p, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::vector<Seg> GetSegments() {
    std::vector<Seg> out;
    const std::uintptr_t base = (std::uintptr_t)::GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER dos;
    if (!SafeRead(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return out;
    IMAGE_NT_HEADERS nt;
    if (!SafeRead(base + dos.e_lfanew, &nt, sizeof(nt))) return out;
    IMAGE_SECTION_HEADER sec[64] = {};
    const DWORD n = nt.FileHeader.NumberOfSections > 64 ? 64 : nt.FileHeader.NumberOfSections;
    if (!SafeRead(base + dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                          nt.FileHeader.SizeOfOptionalHeader, sec, n * sizeof(IMAGE_SECTION_HEADER)))
        return out;
    for (DWORD i = 0; i < n; ++i)
        out.push_back({ base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize, (const char*)sec[i].Name });
    return out;
}

// 在段里找 ascii 子串（大小写不敏感），返回命中地址列表
std::vector<std::uintptr_t> FindAsciiInRange(const Seg& seg, const char* needle) {
    std::vector<std::uintptr_t> hits;
    const std::size_t nl = std::strlen(needle);
    std::vector<unsigned char> buf(std::min<std::size_t>(seg.size, 8u << 20));
    if (!SafeRead(seg.va, buf.data(), buf.size())) return hits;
    for (std::size_t i = 0; i + nl <= buf.size(); ++i) {
        bool m = true;
        for (std::size_t k = 0; k < nl; ++k) {
            char a = (char)buf[i + k];
            if (a == needle[k]) continue;
            if (a >= 'A' && a <= 'Z' && a + 32 == needle[k]) continue;
            if (a >= 'a' && a <= 'z' && a - 32 == needle[k]) continue;
            m = false; break;
        }
        if (m) {
            hits.push_back(seg.va + i);
            i += nl - 1;
        }
    }
    return hits;
}

// .text 里找 "lea r64, [rip+rel32]" / "mov r64, imm32(=target)" 指向 target 的指令
std::vector<std::uintptr_t> FindRefsInText(const Seg& text, std::uintptr_t target) {
    std::vector<std::uintptr_t> refs;
    const std::size_t cap = std::min<std::size_t>(text.size, 64u << 20);
    std::vector<unsigned char> buf(cap);
    if (!SafeRead(text.va, buf.data(), cap)) return refs;
    for (std::size_t i = 0; i + 7 <= buf.size(); ++i) {
        const std::uintptr_t addr = text.va + i;
        // lea r64,[rip+rel32] : 48/4C 8D /0..7 05 rel32（如 48 8D 15 / 4C 8D 05）
        if ((buf[i] == 0x48 || buf[i] == 0x4C) && buf[i + 1] == 0x8D && (buf[i + 2] & 0xC7) == 0x05) {
            std::int32_t rel;
            std::memcpy(&rel, &buf[i + 3], 4);
            if (addr + 7 + rel == target) refs.push_back(addr);
            i += 3;  // 指令长 7，先探测下一字节前勿重复
        }
        // mov r64, imm32（48/4C? B8+B89? 仅常见 B8+B8..B8+17 / 48 C7 C0..）
        const unsigned char op = buf[i];
        if ((op >= 0xB8 && op <= 0xBF)) {   // mov eax..edi, imm32（高32清零）
            std::uint32_t imm;
            std::memcpy(&imm, &buf[i + 1], 4);
            if (imm == (std::uint32_t)target) refs.push_back(addr);
            i += 4;
        } else if (buf[i] == 0x48 && (buf[i + 1] >= 0xC7 && buf[i + 1] <= 0xC0 + 7) && buf[i + 2] == 0xC7) {
            std::uint32_t imm;
            std::memcpy(&imm, &buf[i + 3], 4);
            if (imm == (std::uint32_t)target) refs.push_back(addr);
            i += 6;
        }
    }
    return refs;
}

// 推测的 Wwise 函数 prologue 特征（可能命中，也可能不中；中了是额外惊喜）
const struct { const char* name; const char* hex; } kPatterns[] = {
    // 这些是无验证的“常见模式”占位，会打印出来让后续决定是否可信
    { "PostEvent-ish", "48895C240?4883EC??8B4" },
};

void Run() {
    // 1) 模块信息
    const std::uintptr_t exe = (std::uintptr_t)::GetModuleHandleW(nullptr);
    wchar_t exePath[MAX_PATH * 2];
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    Log("exe base=0x%p", (void*)exe);
    Log("exe path=%ls", exePath);
    const std::vector<Seg> segs = GetSegments();
    for (const auto& s : segs) Log("  seg %-8s va=0x%p size=0x%zX", s.name.c_str(), (void*)s.va, s.size);

    // 2) 字符串锚点
    static const char* kCands[] = {
        "PostEvent", "ExecuteActionOnEvent", "IAkStreamMgr", "CAkSoundEngine",
        "AK::SoundEngine", "StopAll", "AkEvent", "SetCue",
    };
    const Seg* rdata = nullptr;
    const Seg* text = nullptr;
    for (const auto& s : segs) {
        if (s.name == ".rdata" && !rdata) rdata = &s;
        if (s.name == ".text" && !text) text = &s;
    }
    Log("rdata=%p text=%p", rdata ? (void*)rdata->va : nullptr, text ? (void*)text->va : nullptr);
    for (const char* cand : kCands) {
        const std::vector<std::uintptr_t> hits = rdata ? FindAsciiInRange(*rdata, cand) : std::vector<std::uintptr_t>();
        if (hits.empty()) { Log("[str] %s : 未见", cand); continue; }
        for (std::size_t k = 0; k < hits.size() && k < 4; ++k) {
            Log("[str] %s @ 0x%p", cand, (void*)hits[k]);
            if (text) {
                const std::vector<std::uintptr_t> refs = FindRefsInText(*text, hits[k]);
                if (!refs.empty()) {
                    for (std::size_t r = 0; r < refs.size() && r < 8; ++r)
                        Log("   ref -> 0x%p  (%.3X)", (void*)refs[r],
                            (unsigned)(refs[r] - text->va));
                } else {
                    Log("   (无 lea/mov 直接引用)");
                }
            }
        }
    }
    Log("probe done");
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
        std::thread t = std::thread(Run);
        t.detach();
    }
    return TRUE;
}