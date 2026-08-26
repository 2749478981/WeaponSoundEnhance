#pragma once
#include "config.h"
#include "game.h"
#include <string>
#include <vector>

struct App {
    App();

    void Draw();

    void* hwnd = nullptr;      // 主窗口 HWND
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
        int weaponType = -1;
        int actionLmt = -1;
        int fsmId = -1;
        std::vector<std::string> sounds;
        std::vector<int> delays;   // 每条音效的延时(ms)，与 sounds 一一对应
        std::vector<int> vols;     // 每条音效的音量(0..100)，-1 = 跟随全局，与 sounds 一一对应
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
    void DrawEditorModal();
    void DrawFsmWindow();
    void Save();
    void SaveAs();
    void Load(const std::string& path);
    std::string OpenFileDialogIni();
    int BrowseSounds(std::vector<std::string>& out);
    void PlaySoundPreview(const std::string& rel, int vol, int delayMs);
    int CountFor(int w) const;
    void EnrichNames();
    std::string BaseDir() const;
    std::string ResolveName(int weapon, int fsm, int lmt) const;
    bool IsCapturedAdded(int weapon, int fsm, int lmt) const;
};
