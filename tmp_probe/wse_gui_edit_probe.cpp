// 临时诊断：无界面驱动 GUI 编辑器，验证 LMT 编辑的各种输入到底会写出什么。
#include "app.h"   // 本目录下的 app.h 已把 private: 改成 public:（仅测试用）
#include <cstdio>
#include <string>

static void PrintLmt(const Config& cfg, const std::string& name, const char* tag) {
    for (const auto& e : cfg.entries) {
        if (e.name != name || e.combo != "里恩") continue;
        std::printf("  %-28s -> lmt=", tag);
        if (e.lmt.empty()) std::printf("ANY(不限)");
        for (size_t k = 0; k < e.lmt.size(); ++k) std::printf("%s%d", k ? "," : "", e.lmt[k]);
        std::printf("\n");
    }
}

// case: 0=清空后填 -1  1=错误地追加(-1 接在旧值后)  2=勾选“不限”  3=两个 LMT
static int RunCase(const std::string& tmplIni, const std::string& workIni, int cs,
                   const char* tag) {
    std::string bytes;
    {
        FILE* f = nullptr;
        if (fopen_s(&f, tmplIni.c_str(), "rb") != 0 || !f) return 3;
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
        fclose(f);
    }
    {
        FILE* f = nullptr;
        if (fopen_s(&f, workIni.c_str(), "wb") != 0 || !f) return 3;
        fwrite(bytes.data(), 1, bytes.size(), f);
        fclose(f);
    }

    App app;
    Config cfg;
    if (!LoadConfig(workIni, cfg)) { std::printf("load failed\n"); return 3; }
    app.cfg = cfg;
    app.cfg.path = workIni;
    app.cfg.loaded = true;

    int target = -1;
    for (int i = 0; i < (int)app.cfg.entries.size(); ++i) {
        const SoundEntry& e = app.cfg.entries[i];
        if (e.weaponType == 3 && e.combo == "里恩" && e.name == "气刃斩1") { target = i; break; }
    }
    if (target < 0) { std::printf("target not found\n"); return 3; }

    app.OpenEditorEdit(target);
    std::printf("case %d (%s): 打开编辑框 lmtBuf=[%s] lmtAny=%d\n", cs, tag,
                app.editor.lmtBuf, (int)app.editor.lmtAny);
    switch (cs) {
        case 0: std::snprintf(app.editor.lmtBuf, sizeof(app.editor.lmtBuf), "%s", "-1"); break;
        case 1: {  // 模拟用户没清空就接着敲 -1
            std::string s = app.editor.lmtBuf;
            s += "-1";
            std::snprintf(app.editor.lmtBuf, sizeof(app.editor.lmtBuf), "%s", s.c_str());
            break;
        }
        case 2: app.editor.lmtAny = true; app.editor.lmtBuf[0] = 0; break;
        case 3: std::snprintf(app.editor.lmtBuf, sizeof(app.editor.lmtBuf), "%s", "49265,49256"); break;
    }
    const bool ok = app.ApplyEditor();
    std::printf("   ApplyEditor = %s   status = %s\n", ok ? "true(已应用)" : "false(拒绝)",
                app.status.c_str());
    if (ok) {
        app.Save();
        Config back;
        LoadConfig(workIni, back);
        PrintLmt(back, "气刃斩1", "保存并重新读取");
    } else {
        PrintLmt(app.cfg, "气刃斩1", "内存中(未变)");
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: wse_gui_edit_probe <user ini>\n"); return 2; }
    const std::string tpl = argv[1];
    const std::string work = std::string(argv[1]) + ".work.ini";
    RunCase(tpl, work, 0, "清空后填 -1");
    RunCase(tpl, work, 1, "没清空直接追加 -1");
    RunCase(tpl, work, 2, "勾选 LMT 不限");
    RunCase(tpl, work, 3, "填两个 LMT");
    std::remove(work.c_str());
    return 0;
}
