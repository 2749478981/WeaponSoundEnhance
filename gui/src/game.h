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
    bool Attach();              // 找到并打开 MonsterHunterWorld.exe
    void Detach();
    bool IsAttached() const { return mHandle != nullptr; }
    // 抓取当前状态。playerRootValue = ini 里的 PlayerRoot（绝对地址）。
    bool Poll(std::uint64_t playerRootValue, LiveState& out);
private:
    void* mHandle = nullptr;
    unsigned long mPid = 0;
    std::uintptr_t mModuleBase = 0;
};

// 查找正在运行的 MonsterHunterWorld.exe 所在目录（含结尾反斜杠，UTF-8）。未运行返回 false。
bool FindGameExeDir(std::string& out);
