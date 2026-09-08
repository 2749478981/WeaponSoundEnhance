#pragma once
#include "config.h"
#include "game.h"
#include <string>
#include <vector>

struct App {
    App();

    void Draw();

    void* hwnd = nullptr;      // 主窗口 HWND
    void* editHwnd = nullptr;  // 独立编辑窗口 HWND（main.cpp 创建编辑窗口后赋值；文件对话框后用于把编辑窗口带回最前）
    float dpiScale = 1.0f;

    Config cfg;
    GameReader game;
    LiveState live;
    bool liveOk = false;
    bool mDirty = false;

    int weaponFilter = -1;     // -1=全部 -2=任意武器 0..13
    char searchBuf[160] = {};

    struct Editor {
        bool open = false;
        bool isNew = false;
        int index = -1;        // cfg.entries 索引
        char name[128] = {};
        char groupBuf[96] = {};   // 动作组（Group=）
        int weaponType = -1;
        int fsmId = -1;
        std::vector<int> lmt;          // 空 = 不限
        char lmtBuf[128] = {};
        std::vector<SoundSpec> pool[5]; // 0 = 默认音效；1..4 = 无刃时/白刃时/黄刃时/红刃时
    } editor;

    bool fsmWinOpen = false;   // 启动时不弹 FSM 查询窗（点工具栏「FSM 查询」再开）
    char fsmQuery[160] = {};
    int fsmWeaponFilter = -1;

    // 抓取历史：记录 fsm/lmt/weapon 的变化（fsm≠0 的派生动作会被高亮）
    struct HistEntry {
        int fsm;
        int lmt;
        int weapon;
        int weaponId;
        std::string time;      // "HH:MM:SS"
    };
    std::vector<HistEntry> history;

    std::string status;

    // 在独立编辑窗口（第二个 ImGui 上下文）中绘制编辑内容；由 main.cpp 的编辑窗渲染循环调用
    void DrawEditorDetached();

private:
    unsigned long long mLastPoll = 0;
    float mSaveFlash = 0.0f;   // 保存成功提示的剩余显示时间(秒)
    void PollGame();
    void RecordHistory();
    void DrawToolbar();
    void DrawWeaponTree();
    void DrawCapturePanel();
    void DrawEntries();
    void DrawStatus();
    void OpenEditorNew(int weapon, int fsm, int lmt, const std::string& name);
    void OpenEditorNew(int weapon);
    void OpenEditorEdit(int index);
    void ApplyEditor();
    void DrawFsmWindow();
    void Save();
    void SaveAs();
    void Load(const std::string& path);
    std::string OpenFileDialogIni();
    int BrowseSounds(std::vector<SoundSpec>& out);   // 追加选中的 wav 为默认属性音效
    void PlaySoundPreview(const std::string& rel, int vol, int delayMs);
    int CountFor(int w) const;
    void EnrichNames();
    std::string BaseDir() const;
    std::string ResolveName(int weapon, int fsm, int lmt) const;
    bool IsCapturedAdded(int weapon, int fsm, int lmt) const;
    // 每武器当前激活的组合名（""=默认）
    std::string ActiveCombo(int w) const;
    // 该条目是否属于"当前激活组合"（非激活条目不显示/不参与匹配）
    bool EntryActive(const SoundEntry& e) const;
};
