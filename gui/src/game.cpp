#include "game.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>

// MHW 主模块默认镜像基址（15.23.00）。PlayerRoot 绝对地址 = 基址 + 偏移。
constexpr std::uint64_t kImageBase = 0x140000000ULL;

namespace {
bool ReadVal(void* h, std::uintptr_t addr, std::uintptr_t& out) {
    out = 0;
    SIZE_T rd = 0;
    return ReadProcessMemory(h, (LPCVOID)addr, &out, sizeof(out), &rd) && rd == sizeof(out);
}
bool ReadI32(void* h, std::uintptr_t addr, std::int32_t& out) {
    out = -1;
    SIZE_T rd = 0;
    return ReadProcessMemory(h, (LPCVOID)addr, &out, sizeof(out), &rd) && rd == sizeof(out);
}
} // namespace

GameReader::~GameReader() { Detach(); }

bool GameReader::Attach() {
    Detach();
    unsigned long pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"MonsterHunterWorld.exe") == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (!pid) { Detach(); return false; }

    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) { Detach(); return false; }

    std::uintptr_t base = 0;
    HANDLE msnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (msnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(msnap, &me)) {
            do {
                if (_wcsicmp(me.szModule, L"MonsterHunterWorld.exe") == 0) {
                    base = (std::uintptr_t)me.modBaseAddr;
                    break;
                }
            } while (Module32NextW(msnap, &me));
        }
        CloseHandle(msnap);
    }

    mHandle = h;
    mPid = pid;
    mModuleBase = base;
    return true;
}

void GameReader::Detach() {
    if (mHandle) { CloseHandle(mHandle); mHandle = nullptr; }
    mPid = 0;
    mModuleBase = 0;
}

bool GameReader::Poll(std::uint64_t playerRootValue, LiveState& out) {
    out = LiveState{};
    if (!mHandle) return false;

    out.attached = true;
    out.pid = mPid;
    out.moduleBase = mModuleBase;
    if (!mModuleBase) { out.error = "等待游戏模块加载..."; return true; }

    std::uint64_t off = (playerRootValue >= kImageBase)
                            ? (playerRootValue - kImageBase)
                            : playerRootValue;
    std::uintptr_t root = mModuleBase + (std::uintptr_t)off;
    out.playerRoot = root;

    std::uintptr_t manager = 0;
    if (!ReadVal(mHandle, root, manager)) {
        // 进程可能已退出
        Detach();
        return false;
    }
    if (!manager) { out.error = "玩家指针为空(未进入场景)"; return true; }

    std::uintptr_t entity = 0;
    if (!ReadVal(mHandle, manager + 0x50, entity) || !entity) {
        out.error = "实体为空(未进入场景)";
        return true;
    }
    out.inScene = true;

    ReadI32(mHandle, entity + 0x6278, out.fsm);

    std::uintptr_t act = 0;
    if (ReadVal(mHandle, entity + 0x468, act) && act)
        ReadI32(mHandle, act + 0xE9C4, out.lmt);

    std::uintptr_t a = 0, b = 0, data = 0;
    if (ReadVal(mHandle, entity + 0xC0, a) && a &&
        ReadVal(mHandle, a + 0x8, b) && b &&
        ReadVal(mHandle, b + 0x78, data) && data) {
        ReadI32(mHandle, data + 0x2E8, out.weapon);
        ReadI32(mHandle, data + 0x2EC, out.weaponId);
    }
    return true;
}

bool FindGameExeDir(std::string& out) {
    unsigned long pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"MonsterHunterWorld.exe") == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (!pid) return false;

    std::wstring path;
    HANDLE msnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (msnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(msnap, &me)) {
            do {
                if (_wcsicmp(me.szModule, L"MonsterHunterWorld.exe") == 0) {
                    path = me.szExePath;
                    break;
                }
            } while (Module32NextW(msnap, &me));
        }
        CloseHandle(msnap);
    }
    if (path.empty()) return false;
    size_t s = path.find_last_of(L"\\/");
    if (s != std::wstring::npos) path = path.substr(0, s + 1);

    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    std::string mb(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &mb[0], n, nullptr, nullptr);
    out = mb;
    return true;
}
