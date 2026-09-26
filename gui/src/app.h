#pragma once
#include "config.h"
#include "game.h"
#include <string>
#include <vector>

// 条件表达式里的一项：<变量> <比较符> <数值>
struct CondTerm {
    int  var = 0;          // 下标见 app.cpp 的 kCondVars
    int  op  = 0;          // 下标见 app.cpp 的 kCondOps
    int  val = 0;
    bool orBefore = false; // 与前一项之间是 |（或），false = &（且）
};

// 编辑器里的一条「条件 -> 音效池」
struct CondRow {
    std::vector<CondTerm> terms;
    std::string rawExpr;    // terms 解析不出来时保留原文，界面转成只读文本框
    bool parsed = true;
    bool atEnd = false;     // 只在窗口结束时评（写成 SoundEnd:）
    std::string label;      // 预设给的友好名，如"成功"/"失败(掉刃)"
    std::vector<SoundSpec> pool;
};

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
        int fsmTarget = -1;       // FSMTarget=：-1 = 不限定
        std::vector<int> lmt;          // 空 = 不限
        char lmtBuf[128] = {};
        bool lmtAny = false;           // 勾选「LMT 不限」= 该 FSMId 的所有动作都触发
        std::string combo;             // 条目所属配置组合（"" = 默认组合），编辑器里可改
        std::vector<SoundSpec> pool[5]; // 0 = 默认音效；1..4 = 无刃时/白刃时/黄刃时/红刃时

        // ---- 判定（延迟判定）----
        int  judgePreset = 0;        // 0=不判定 1..N=内置预设 N+1=自定义
        bool advanced = false;       // 展开高级设置
        int  checkDelayMs = 0;
        int  checkTimeoutMs = 0;
        int  checkOffsetMs = 150;
        bool endOnAction = true;
        int  checkMode = 0;          // 1 = CheckMode=final
        std::vector<CondRow> conds;
    } editor;

    bool fsmWinOpen = false;   // 启动时不弹 FSM 查询窗（点工具栏「FSM 查询」再开）
    std::string mGameIni;      // 游戏实际读取的那份 ini（<游戏目录>\nativePC\plugins\WeaponSoundEnhance\...）
    bool idWinOpen = false;    // 共享动作 ID 库窗口（点工具栏「上传ID」打开）
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
    bool ApplyEditor();   // false = 输入有问题（例如 LMT 填了无法识别的项），不落地
    void DrawFsmWindow();
    void DrawIdShareWindow();   // 共享动作 ID 库（上传/获取）
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
    // 共享动作 ID 库（fsm_db.csv）：导出实测 / 导入合并 / 提交 / 获取最新
    std::string OpenFileDialogCsv();
    void ExportMeasuredIdsCsv();
    void ImportIdsCsv();
    void SubmitIdsToGithub();
    void FetchLatestFsmDb();
    void MergeOldIni();   // 合并旧版 ini 的动作条目（升级不丢配置）
    void MergeConfigFile(const std::string& path);   // 合并指定 ini（旧配置 / 导入的组合文件共用）
    void ExportComboCurrent();   // 把当前武器的当前组合导出为分享文件
    void ImportComboFile();      // 导入别人分享的组合文件（按条目去重合并）
    void SwitchToGameIni();   // 切到游戏实际读取的 ini（当前那份不是它时用）
    // ---- 配置组合管理 ----
    std::vector<std::string> WeaponCombos(int w) const;              // "" 在最前 + 命名组合(按出现顺序)
    void ComboRename(int w, const std::string& from, const std::string& to);
    void ComboDelete(int w, const std::string& name);
    void ComboMove(int w, const std::string& name, int dir);         // dir=-1 上移 / +1 下移
};
