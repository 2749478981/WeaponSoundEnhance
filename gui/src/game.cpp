#include "game.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cwchar>
#include <vector>

// MHW 主模块默认镜像基址（15.23.00）。PlayerRoot 绝对地址 = 基址 + 偏移。
constexpr std::uint64_t kImageBase = 0x140000000ULL;

namespace {
struct GameProc {
    unsigned long pid = 0;
    unsigned long threads = 0;
    unsigned long long workingSet = 0;
};

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

unsigned long long WorkingSetOf(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc{};
    const bool ok = GetProcessMemoryInfo(h, &pmc, sizeof(pmc)) != FALSE;
    CloseHandle(h);
    return ok ? (unsigned long long)pmc.WorkingSetSize : 0ULL;
}

// 枚举所有 MonsterHunterWorld.exe。游戏崩溃/被强杀后可能留下 0 线程、工作集
// 只有 1MB 左右的僵尸进程，它们也占着同名 exe 和同一个模块基址，所以必须
// 用“还有线程 + 工作集大”排序，把它们排到最后。
void EnumGameProcs(std::vector<GameProc>& out) {
    out.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"MonsterHunterWorld.exe") != 0) continue;
            GameProc gp;
            gp.pid = pe.th32ProcessID;
            gp.threads = pe.cntThreads;
            gp.workingSet = WorkingSetOf(gp.pid);
            out.push_back(gp);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::stable_sort(out.begin(), out.end(), [](const GameProc& a, const GameProc& b) {
        if ((a.threads > 0) != (b.threads > 0)) return a.threads > 0;
        return a.workingSet > b.workingSet;
    });
}

std::uintptr_t ModuleBaseOf(unsigned long pid) {
    std::uintptr_t base = 0;
    HANDLE msnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (msnap == INVALID_HANDLE_VALUE) return 0;
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
    return base;
}

// 玩家指针链是否读得通（僵尸进程虽然内存还挂在，但这条链会断）
bool ChainReadable(void* h, std::uintptr_t base, std::uint64_t playerRootValue) {
    const std::uint64_t off = (playerRootValue >= kImageBase) ? (playerRootValue - kImageBase)
                                                             : playerRootValue;
    std::uintptr_t manager = 0, entity = 0;
    if (!ReadVal(h, base + (std::uintptr_t)off, manager) || !manager) return false;
    if (!ReadVal(h, manager + 0x50, entity) || !entity) return false;
    return true;
}
} // namespace

GameReader::~GameReader() { Detach(); }

bool GameReader::Attach(std::uint64_t playerRootValue) {
    Detach();
    mLastError.clear();

    std::vector<GameProc> procs;
    EnumGameProcs(procs);
    mCandidateCount = (int)procs.size();
    if (procs.empty()) {
        mLastError = "未找到 MonsterHunterWorld.exe（游戏没开？）";
        return false;
    }

    int alive = 0, stale = 0;
    HANDLE chosen = nullptr;
    unsigned long chosenPid = 0;
    std::uintptr_t chosenBase = 0;
    std::string openErr;
    bool haveFallback = false;

    for (const auto& gp : procs) {
        if (!gp.threads) { ++stale; continue; }   // 残留僵尸进程，跳过
        ++alive;
        HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, gp.pid);
        if (!h) {
            if (openErr.empty()) openErr = "错误码 " + std::to_string((int)GetLastError());
            continue;
        }
        const std::uintptr_t base = ModuleBaseOf(gp.pid);
        if (!base) { CloseHandle(h); continue; }

        if (playerRootValue && ChainReadable(h, base, playerRootValue)) {
            chosen = h; chosenPid = gp.pid; chosenBase = base;
            break;
        }
        // 暂时读不到（比如还在主菜单）→ 留作兜底候选，继续找更靠谱的进程
        if (!haveFallback) {
            chosen = h; chosenPid = gp.pid; chosenBase = base; haveFallback = true;
        } else {
            CloseHandle(h);
        }
    }

    if (!chosen) {
        if (!alive) {
            mLastError = "只找到 " + std::to_string(stale) +
                         " 个无响应的残留游戏进程（建议在任务管理器里结束它们后重开游戏）";
        } else if (!openErr.empty()) {
            mLastError = "打开游戏进程失败（" + openErr +
                         "）：若游戏以管理员身份运行，请右键以管理员身份运行本工具";
        } else {
            mLastError = "找到游戏进程但读不到模块基址，请重启游戏与本工具";
        }
        return false;
    }

    mHandle = chosen;
    mPid = chosenPid;
    mModuleBase = chosenBase;
    return true;
}

void GameReader::Detach() {
    if (mHandle) { CloseHandle((HANDLE)mHandle); mHandle = nullptr; }
    mPid = 0;
    mModuleBase = 0;
}

bool GameReader::Poll(std::uint64_t playerRootValue, LiveState& out) {
    out = LiveState{};
    if (!mHandle) return false;
    HANDLE h = (HANDLE)mHandle;

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
    if (!ReadVal(h, root, manager)) {
        // 进程可能已退出
        Detach();
        return false;
    }
    if (!manager) { out.error = "玩家指针为空(未进入场景)"; return true; }

    std::uintptr_t entity = 0;
    if (!ReadVal(h, manager + 0x50, entity) || !entity) {
        out.error = "实体为空(未进入场景)";
        return true;
    }
    out.inScene = true;

    ReadI32(h, entity + 0x6278, out.fsm);

    std::uintptr_t act = 0;
    if (ReadVal(h, entity + 0x468, act) && act)
        ReadI32(h, act + 0xE9C4, out.lmt);

    std::uintptr_t a = 0, b = 0, data = 0;
    if (ReadVal(h, entity + 0xC0, a) && a &&
        ReadVal(h, a + 0x8, b) && b &&
        ReadVal(h, b + 0x78, data) && data) {
        ReadI32(h, data + 0x2E8, out.weapon);
        ReadI32(h, data + 0x2EC, out.weaponId);
    }
    return true;
}

bool FindGameExeDir(std::string& out) {
    std::vector<GameProc> procs;
    EnumGameProcs(procs);
    if (procs.empty()) return false;
    const unsigned long pid = procs.front().pid;   // 已按“存活 + 工作集大”排序

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
