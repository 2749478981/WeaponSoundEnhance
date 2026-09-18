#pragma once
#include <cstdint>
#include <string>

struct LiveState {
    bool attached = false;
    unsigned long pid = 0;
    std::uintptr_t moduleBase = 0;
    std::uintptr_t playerRoot = 0;   // 解析后的绝对地址
    bool inScene = false;
    std::int32_t fsm = -1;
    std::int32_t lmt = -1;
    std::int32_t weapon = -1;        // 武器类型 0..13
    std::int32_t weaponId = -1;
    std::string error;
};

class GameReader {
public:
    ~GameReader();
    // 找到并打开 MonsterHunterWorld.exe。传入 playerRoot 时会逐个候选进程验证
    // 玩家指针链，避免连到残留的僵尸进程上（工作集很小、0 线程）。
    bool Attach(std::uint64_t playerRootValue = 0);
    void Detach();
    bool IsAttached() const { return mHandle != nullptr; }
    // 抓取当前状态。playerRootValue = ini 里的 PlayerRoot（绝对地址）。
    bool Poll(std::uint64_t playerRootValue, LiveState& out);
    // 最近一次 Attach 失败的原因（给 GUI 显示“为什么连不上”）
    const std::string& LastError() const { return mLastError; }
    // 本次枚举到的同名游戏进程数量（含残留僵尸）
    int CandidateCount() const { return mCandidateCount; }
private:
    void* mHandle = nullptr;
    unsigned long mPid = 0;
    std::uintptr_t mModuleBase = 0;
    std::string mLastError;
    int mCandidateCount = 0;
};

// 查找正在运行的 MonsterHunterWorld.exe 所在目录（含结尾反斜杠，UTF-8）。未运行返回 false。
bool FindGameExeDir(std::string& out);
