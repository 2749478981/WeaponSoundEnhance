#include "app.h"
#include "fsmdb.h"
#include "imgui.h"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <urlmon.h>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <memory>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")

// 共享动作 ID 库：仓库里的 fsm_db.csv（“获取最新库”从这里下载）
static const wchar_t* const kSharedFsmDbUrl =
    L"https://raw.githubusercontent.com/2749478981/WeaponSoundEnhance/main/fsm_db.csv";
// 提交入口：GitHub 新建 issue（把导出的 CSV 粘进去/附件即可）
static const wchar_t* const kSubmitIssueUrl =
    L"https://github.com/2749478981/WeaponSoundEnhance/issues/new"
    L"?title=%E5%8A%A8%E4%BD%9CID%E6%8F%90%E4%BA%A4&body=%E8%AF%B7%E6%8A%8A%20fsm_db_submission.csv%20%E7%9A%84%E5%86%85%E5%AE%B9%E7%B2%98%E5%9C%A8%E4%B8%8B%E9%9D%A2%EF%BC%88%E6%88%96%E4%BD%9C%E4%B8%BA%E9%99%84%E4%BB%B6%E4%B8%8A%E4%BC%A0%EF%BC%89%EF%BC%9A";

namespace {

// macOS 浅色主题配色
static const ImVec4 C_ACCENT(0.00f, 0.48f, 1.00f, 1.0f);  // #007AFF
static const ImVec4 C_GREEN (0.16f, 0.78f, 0.25f, 1.0f);  // #28C840
static const ImVec4 C_AMBER (0.80f, 0.50f, 0.08f, 1.0f);  // 深琥珀（白底可读）
static const ImVec4 C_RED   (1.00f, 0.23f, 0.19f, 1.0f);  // #FF3B30
static const ImVec4 C_GRAY  (0.53f, 0.53f, 0.56f, 1.0f);  // #86868B

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

// LMT 列表的显示文本；空列表 = -1（任意）
std::string LmtText(const SoundEntry& e) {
    if (e.lmt.empty()) return "-1";
    std::string s;
    for (size_t k = 0; k < e.lmt.size(); ++k) {
        if (k) s += ",";
        s += std::to_string(e.lmt[k]);
    }
    return s;
}

// 把 LMT 列表写入编辑框文本
// ===========================================================================
//  判定条件：变量表、比较符、表达式 <-> 下拉项的互转，以及内置预设
//  变量名必须和插件 (WeaponSoundEnhance.cpp 的 cond::kVarNames) 完全一致
// ===========================================================================
static const char* const kCondVars[] = {
    "dmg", "aura", "dAura", "charge", "dCharge", "lmt", "fsm", "fsmTarget", "ms"
};
static const char* const kCondVarLabels[] = {
    "打出伤害", "太刀气刃等级", "气刃等级变化", "大剑蓄力等级", "蓄力等级变化",
    "动作ID", "FSM", "FSM层", "已过毫秒"
};
static const int kCondVarCount = 9;

static const char* const kCondOps[] = { ">", ">=", "<", "<=", "==", "!=" };
static const char* const kCondOpLabels[] = { "大于", "大于等于", "小于", "小于等于", "等于", "不等于" };
static const int kCondOpCount = 6;

static std::string CondTrim(const std::string& x) {
    size_t b = 0, e = x.size();
    while (b < e && (unsigned char)x[b] <= ' ') ++b;
    while (e > b && (unsigned char)x[e - 1] <= ' ') --e;
    return x.substr(b, e - b);
}

// 一项："dmg>0" -> {var,op,val}
static bool ParseOneTerm(const std::string& raw, CondTerm& t) {
    const std::string x = CondTrim(raw);
    size_t opPos = x.find_first_of("<>=!");
    if (x.empty() || opPos == std::string::npos || opPos == 0) return false;
    const std::string vn = CondTrim(x.substr(0, opPos));
    int vi = -1;
    for (int i = 0; i < kCondVarCount; ++i) if (vn == kCondVars[i]) { vi = i; break; }
    if (vi < 0) return false;
    const std::string rest = x.substr(opPos);
    int oi = -1; size_t ol = 0;
    for (int i = 0; i < kCondOpCount; ++i)
        if (strlen(kCondOps[i]) == 2 && rest.compare(0, 2, kCondOps[i]) == 0) { oi = i; ol = 2; break; }
    if (oi < 0)
        for (int i = 0; i < kCondOpCount; ++i)
            if (strlen(kCondOps[i]) == 1 && rest.compare(0, 1, kCondOps[i]) == 0) { oi = i; ol = 1; break; }
    if (oi < 0) return false;
    const std::string rv = CondTrim(rest.substr(ol));
    if (rv.empty()) return false;
    for (size_t i = 0; i < rv.size(); ++i) {
        if (i == 0 && (rv[i] == '-' || rv[i] == '+')) continue;
        if (rv[i] < '0' || rv[i] > '9') return false;
    }
    t.var = vi; t.op = oi; t.val = std::atoi(rv.c_str());
    return true;
}

// "dmg>0 & dAura>=0" -> terms
static bool ParseExprToTerms(const std::string& src, std::vector<CondTerm>& out) {
    out.clear();
    std::string cur; bool nextOr = false;
    for (size_t i = 0; i <= src.size(); ++i) {
        const char c = (i < src.size()) ? src[i] : (char)0;
        if (c == '&' || c == '|' || c == (char)0) {
            CondTerm t;
            if (!ParseOneTerm(cur, t)) return false;
            t.orBefore = nextOr;
            out.push_back(t);
            cur.clear();
            nextOr = (c == '|');
        } else cur += c;
    }
    return !out.empty();
}

static std::string TermsToExpr(const std::vector<CondTerm>& ts) {
    std::string o;
    for (size_t i = 0; i < ts.size(); ++i) {
        if (i) o += ts[i].orBefore ? " | " : " & ";
        o += kCondVars[ts[i].var];
        o += kCondOps[ts[i].op];
        o += std::to_string(ts[i].val);
    }
    return o;
}

// ---- 内置预设：一条就是一整套"成败判定"，用户只要挑两个 wav ----
struct JudgePreset {
    const char* name;
    int  weapon;
    const char* lmt;          // 逗号分隔
    int  delayMs, timeoutMs, offsetMs;
    struct { const char* expr; bool atEnd; const char* label; } conds[3];
    const char* fallbackLabel;   // 都不成立时（默认音效池）的含义
    const char* note;
};
static const JudgePreset kPresets[] = {
    { "太刀 · 登龙 命中/落空", 3, "49326", 0, 2500, 150,
      { { "dmg>0", false, "命中" }, { nullptr, false, nullptr } },
      "落空",
      "在登龙命中或落空时分别播放不同的wav文件。" },

    { "太刀 · 大居 成功/失败", 3, "49461,49462,49463", 0, 3000, 150,
      { { "dAura<0", false, "失败（掉刃）" }, { "dmg>0", true, "成功" }, { nullptr, false, nullptr } },
      "失败（完全落空）",
      "大居成功需要打出伤害且不掉刃，所以在判定窗口结束时进行判定，"
      "因勾选了动作结束也作为判定时机，所以在最晚出伤时间加上余量的时候进行判定" },

    { "大剑 · 真蓄 命中/落空", 0, "49298,49341,49342,49427,49428,49429", 1200, 3500, 150,
      { { "dmg>0", false, "命中" }, { nullptr, false, nullptr } },
      "落空",
      "真蓄两段：第一段约 0.65 秒、伤害小，第二段约 1.7~2.1 秒、伤害大。"
      "计伤起点设在 1200ms 正好卡在两段中间，只认第二段。" },
};
static const int kPresetCount = 3;

void FillLmtBuf(char* buf, size_t n, const std::vector<int>& lmt) {
    std::string s;
    for (size_t k = 0; k < lmt.size(); ++k) {
        if (k) s += ",";
        s += std::to_string(lmt[k]);
    }
    snprintf(buf, n, "%s", s.c_str());
}

std::string ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring ws(buf);
    size_t p = ws.find_last_of(L"\\/");
    if (p != std::wstring::npos) ws = ws.substr(0, p + 1);
    char mb[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, mb, sizeof(mb), nullptr, nullptr);
    return std::string(mb);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string Utf8FromWide(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

// 主按钮（macOS 蓝底白字）
bool PrimaryButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, C_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.40f, 0.86f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.35f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool r = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return r;
}

// macOS 风格滑杆：圆角轨道、左侧蓝色填充、圆点手柄、拖拽改值；
// 数值在右侧固定宽度区域内右对齐显示（数字变多不会使后续元素偏移）。
bool MacSlider(const char* id, int& v, int vmin, int vmax, float width, const char* suffix, float dpi) {
    ImGui::PushID(id);
    float trackH = 5.0f * dpi;
    float trackW = width;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 trackA(p0.x, p0.y + 11.0f * dpi);
    ImVec2 trackB(p0.x + trackW, trackA.y + trackH);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(trackA, trackB, IM_COL32(228, 228, 230, 255), trackH * 0.5f);

    float t = (float)(v - vmin) / (float)(vmax - vmin);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float fillW = trackW * t;
    if (fillW > 0.0f)
        dl->AddRectFilled(trackA, ImVec2(trackA.x + fillW, trackB.y), IM_COL32(0, 122, 255, 255), trackH * 0.5f);
    float cx = trackA.x + fillW;
    float cy = trackA.y + trackH * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, cy), 6.0f * dpi, IM_COL32(0, 122, 255, 255));

    int orig = v;
    {
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##sl", ImVec2(trackW, 22.0f * dpi));
        if (ImGui::IsItemActive()) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            float frac = (mp.x - trackA.x) / trackW;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            int nv = vmin + (int)(frac * (vmax - vmin) + 0.5f);
            if (nv != v) v = nv;
        }
    }

    // 数值标签：紧跟滑块、固定宽度、左对齐（数字变多时向右扩展但不推移其后元素）
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%s", v, suffix);
    float labelW = 56.0f * dpi;
    float sp = 6.0f * dpi;
    float fontH = ImGui::GetFontSize();
    ImVec2 lab0(p0.x + trackW + sp, p0.y + (22.0f * dpi - fontH) * 0.5f + 1.0f * dpi);
    dl->AddText(lab0, IM_COL32(134, 134, 139, 255), buf);

    // 占位：轨道 + 间距 + 固定标签宽
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(trackW + sp + labelW, 22.0f * dpi));
    ImGui::PopID();
    return v != orig;
}

// 按显示宽度截断文本（UTF-8 安全，超出加省略号），避免长名称撑出/截断卡片
std::string ClipText(const std::string& s, float maxW) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    std::string t;
    size_t i = 0;
    while (i < s.size()) {
        size_t clen = 1;
        unsigned char c = (unsigned char)s[i];
        if ((c & 0xE0) == 0xC0) clen = 2;
        else if ((c & 0xF0) == 0xE0) clen = 3;
        else if ((c & 0xF8) == 0xF0) clen = 4;
        std::string cand = t + s.substr(i, clen) + "…";
        if (ImGui::CalcTextSize(cand.c_str()).x > maxW) break;
        t += s.substr(i, clen);
        i += clen;
    }
    return t + "…";
}


struct PvCtx {
    std::wstring path;
    float gain;
    unsigned delayMs;
};

// 解析 PCM(16bit) 的 fmt/data 块
static bool ParseWavPcm(const std::uint8_t* buf, std::size_t size,
                        int& ch, int& rate, int& bits, std::vector<std::int16_t>& out) {
    if (size < 12 || std::memcmp(buf, "RIFF", 4) != 0) return false;
    std::size_t pos = 12;
    bool hasFmt = false, hasData = false;
    while (pos + 8 <= size) {
        std::uint8_t id[4];
        std::memcpy(id, buf + pos, 4);
        std::uint32_t sz;
        std::memcpy(&sz, buf + pos + 4, 4);
        const std::uint8_t* d = buf + pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            std::uint16_t tag;
            std::memcpy(&tag, d, 2);
            if (tag != 1) return false;   // 仅 PCM
            std::memcpy(&ch, d + 2, 2);
            std::uint32_t r;
            std::memcpy(&r, d + 4, 4);
            rate = (int)r;
            std::memcpy(&bits, d + 14, 2);
            hasFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            std::size_t n = sz / 2;
            if (pos + 8 + n * 2 <= size) {
                out.resize(n);
                std::memcpy(out.data(), d, n * 2);
            }
            hasData = true;
        }
        pos += 8 + sz + (sz & 1);
        if (hasFmt && hasData) break;
    }
    return hasFmt && hasData && bits == 16 && !out.empty();
}

static DWORD WINAPI PvWorker(LPVOID param) {
    std::unique_ptr<PvCtx> ctx((PvCtx*)param);
    if (ctx->delayMs > 0) ::Sleep(ctx->delayMs);
    HANDLE h = ::CreateFileW(ctx->path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (32LL << 20)) {
        ::CloseHandle(h);
        return 0;
    }
    std::vector<std::uint8_t> raw((std::size_t)sz.QuadPart);
    DWORD rd = 0;
    if (!::ReadFile(h, raw.data(), (DWORD)raw.size(), &rd, nullptr) || rd != (DWORD)raw.size()) {
        ::CloseHandle(h);
        return 0;
    }
    ::CloseHandle(h);

    int ch = 2, rate = 44100, bits = 16;
    std::vector<std::int16_t> pcm;
    if (!ParseWavPcm(raw.data(), raw.size(), ch, rate, bits, pcm)) return 0;

    float g = ctx->gain < 0.0f ? 0.0f : (ctx->gain > 1.0f ? 1.0f : ctx->gain);
    for (auto& s : pcm) s = (std::int16_t)(s * g);

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)ch;
    wfx.nSamplesPerSec = (DWORD)rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)((16 / 8) * ch);
    wfx.nAvgBytesPerSec = rate * wfx.nBlockAlign;
    HWAVEOUT hwo = nullptr;
    if (::waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return 0;
    std::vector<std::uint8_t> bytes((std::size_t)pcm.size() * 2);
    std::memcpy(bytes.data(), pcm.data(), bytes.size());
    WAVEHDR hdr{};
    hdr.lpData = (LPSTR)bytes.data();
    hdr.dwBufferLength = (DWORD)bytes.size();
    if (::waveOutPrepareHeader(hwo, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
        ::waveOutClose(hwo);
        return 0;
    }
    ::waveOutWrite(hwo, &hdr, sizeof(hdr));
    while ((hdr.dwFlags & WHDR_DONE) == 0) ::Sleep(10);
    ::waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
    ::waveOutClose(hwo);
    return 0;
}

static void PlayPreviewFile(const std::wstring& path, float gain, unsigned delayMs) {
    PvCtx* c = new PvCtx{ path, gain, delayMs };
    HANDLE th = ::CreateThread(nullptr, 0, &PvWorker, c, 0, nullptr);
    if (th) ::CloseHandle(th);
    else delete c;
}

} // namespace

App::App() {
    std::string ini = ExeDir() + "WeaponSoundEnhance.ini";
    if (LoadConfig(ini, cfg)) {
        EnrichNames();
        status = "已加载: " + ini;
        return;
    }
    std::string gdir;
    if (FindGameExeDir(gdir)) {
        std::string p2 = gdir + "nativePC\\plugins\\WeaponSoundEnhance.ini";
        if (LoadConfig(p2, cfg)) {
            EnrichNames();
            status = "已加载: " + p2;
            return;
        }
    }
    cfg.path = ini;
    cfg.loaded = false;
    status = "未找到 ini，请点【打开 ini】选择 nativePC\\plugins\\WeaponSoundEnhance.ini";
}

std::string App::BaseDir() const {
    if (cfg.loaded && !cfg.path.empty()) {
        size_t s = cfg.path.find_last_of("\\/");
        if (s != std::string::npos) return cfg.path.substr(0, s + 1);
    }
    return ExeDir();
}

int App::CountFor(int w) const {
    int c = 0;
    for (const auto& e : cfg.entries) if (e.weaponType == w && EntryActive(e)) ++c;
    return c;
}

std::string App::ActiveCombo(int w) const {
    auto it = cfg.active.find(w);
    return (it != cfg.active.end()) ? it->second : std::string();
}

bool App::EntryActive(const SoundEntry& e) const {
    if (e.weaponType < 0) return e.combo.empty();       // 任意武器：仅默认组合
    return e.combo == ActiveCombo(e.weaponType);
}

void App::PollGame() {
    unsigned long long now = GetTickCount64();
    if (now - mLastPoll < 250) return;   // 250ms 轮询，利于捕获动作窗口
    mLastPoll = now;
    if (!game.IsAttached()) game.Attach();
    if (!game.IsAttached()) { liveOk = false; return; }
    std::uint64_t pr = ParsePlayerRoot(cfg.global.playerRoot, 0x1450139A0ULL);
    liveOk = game.Poll(pr, live);
    if (!game.IsAttached()) liveOk = false;
    RecordHistory();
}

void App::RecordHistory() {
    if (!(liveOk && live.attached) || !live.inScene) return;
    if (live.fsm == -1) return;
    if (!history.empty()) {
        const HistEntry& last = history.back();
        if (last.fsm == live.fsm && last.lmt == live.lmt && last.weapon == live.weapon)
            return;   // 与上一次相同，去重
    }
    HistEntry h;
    h.fsm = live.fsm;
    h.lmt = live.lmt;
    h.weapon = live.weapon;
    h.weaponId = live.weaponId;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char tb[16];
    snprintf(tb, sizeof(tb), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    h.time = tb;
    history.push_back(std::move(h));
    if (history.size() > 64) history.erase(history.begin());
}

void App::DrawToolbar() {
    // macOS 红绿灯
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = 5.5f * dpiScale;
    float y = p0.y + 10 * dpiScale;
    dl->AddCircleFilled(ImVec2(p0.x + 13 * dpiScale, y), r, IM_COL32(255, 95, 87, 255));
    dl->AddCircleFilled(ImVec2(p0.x + 31 * dpiScale, y), r, IM_COL32(254, 188, 46, 255));
    dl->AddCircleFilled(ImVec2(p0.x + 49 * dpiScale, y), r, IM_COL32(40, 200, 64, 255));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 62 * dpiScale);
    ImGui::Text("WeaponSoundEnhance");
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("武器音效配置工具 (15.23.00)");

    // 右侧文件操作按钮（右对齐）；左侧预留“保存成功”反馈区，避免按钮被挤动
    ImGuiStyle& sst = ImGui::GetStyle();
    float flashW = 84.0f * dpiScale;
    const char* btns[] = { "打开 ini", "保存", "另存为" };
    float total = 0;
    for (auto b : btns) total += ImGui::CalcTextSize(b).x + sst.FramePadding.x * 2 + sst.ItemSpacing.x;
    total += flashW;
    float x = ImGui::GetWindowContentRegionMax().x - total;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    {
        ImVec2 fp0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(flashW, ImGui::GetTextLineHeight() + sst.FramePadding.y * 2.0f));
        if (mSaveFlash > 0.0f) {
            const char* t = "✓ 已保存";
            float tw = ImGui::CalcTextSize(t).x;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(fp0.x + flashW - tw, fp0.y + sst.FramePadding.y),
                ImGui::GetColorU32(C_GREEN), t);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("打开 ini")) {
        std::string p = OpenFileDialogIni();
        if (!p.empty()) Load(p);
    }
    ImGui::SameLine();
    if (PrimaryButton("保存")) Save();
    ImGui::SameLine();
    if (ImGui::Button("另存为")) SaveAs();

    ImGui::Separator();

    // 设置栏
    bool en = cfg.global.enabled != 0;
    if (ImGui::Checkbox("总开关", &en)) { cfg.global.enabled = en ? 1 : 0; mDirty = true; }
    ImGui::SameLine(0, 16);
    bool more = cfg.global.moreSounds != 0;
    if (ImGui::Checkbox("更多音效", &more)) { cfg.global.moreSounds = more ? 1 : 0; mDirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("仅对无固定(F)音效的纯旧条目生效");
    ImGui::SameLine(0, 16);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("音量");
    ImGui::SameLine();
    if (MacSlider("##vol", cfg.global.volume, 0, 100, 100 * dpiScale, "", dpiScale)) mDirty = true;
    ImGui::SameLine(0, 16);
    bool dbg = cfg.global.debug != 0;
    if (ImGui::Checkbox("调试日志", &dbg)) { cfg.global.debug = dbg ? 1 : 0; mDirty = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("插件侧 Debug=1：把每次播放/心跳写入 WeaponSoundEnhance.log");
    ImGui::SameLine(0, 16);
    bool hk = cfg.global.hotkeysEnabled != 0;
    if (ImGui::Checkbox("启用热键", &hk)) { cfg.global.hotkeysEnabled = hk ? 1 : 0; mDirty = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("插件侧 Hotkeys=1：关闭后游戏内 Ctrl 组合热键全部失效（聊天框 /wse 指令不受影响）");
    ImGui::SameLine(0, 16);
    if (ImGui::Button("FSM 查询")) fsmWinOpen = !fsmWinOpen;
    ImGui::SameLine();
    if (ImGui::Button("打开 sounds\\")) {
        std::wstring sd = Utf8ToWide(BaseDir() + "sounds");
        CreateDirectoryW(sd.c_str(), nullptr);
        ShellExecuteW((HWND)hwnd, L"open", sd.c_str(), nullptr, nullptr, SW_SHOW);
    }
}

void App::DrawWeaponTree() {
    ImGui::TextDisabled("武器分类");
    ImGui::Separator();
    auto item = [&](const char* label, int filter, int count) {
        bool sel = (weaponFilter == filter);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Header, C_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, C_ACCENT);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        char id[160];
        snprintf(id, sizeof(id), "%s   %d", label, count);
        bool clicked = ImGui::Selectable(id, sel);
        if (sel) ImGui::PopStyleColor(3);
        if (clicked) weaponFilter = filter;
    };
    int totalActive = 0;
    for (const auto& e : cfg.entries) if (EntryActive(e)) ++totalActive;
    item("全部条目", -1, totalActive);
    for (int w = 0; w <= 13; ++w)
        item(WeaponName(w), w, CountFor(w));
    int any = 0;
    for (const auto& e : cfg.entries) if (e.weaponType < 0 && e.combo.empty()) ++any;
    if (any > 0) item("通用(任意)", -2, any);

    // ---- 组合切换（仅对具体武器；不影响其它武器）----
    if (weaponFilter >= 0 && weaponFilter <= 13) {
        const int w = weaponFilter;
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("组合(当前激活)");
        std::vector<std::string> cbs;
        cbs.push_back("");   // 默认
        for (const auto& e : cfg.entries)
            if (e.weaponType == w && !e.combo.empty()) {
                bool has = false;
                for (const auto& c : cbs) if (c == e.combo) { has = true; break; }
                if (!has) cbs.push_back(e.combo);
            }
        int cur = 0;
        for (size_t i = 0; i < cbs.size(); ++i) if (cbs[i] == ActiveCombo(w)) { cur = (int)i; break; }
        ImGui::SetNextItemWidth(150 * dpiScale);
        if (ImGui::BeginCombo("##combo", cbs[cur].empty() ? "默认" : cbs[cur].c_str())) {
            for (size_t i = 0; i < cbs.size(); ++i) {
                const char* lbl = cbs[i].empty() ? "默认" : cbs[i].c_str();
                if (ImGui::Selectable(lbl, (int)i == cur)) {
                    cfg.active[w] = cbs[i];
                    mDirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered() && cbs.size() > 1)
            ImGui::SetTooltip("切换该武器的配置组合；其它武器的配置不受影响");

        // 新增组合：复制当前激活组合的条目到一个新命名组合并切换过去
        static char newCombo[64] = {};
        ImGui::SetNextItemWidth(150 * dpiScale);
        ImGui::InputTextWithHint("##ncombo", "新组合名", newCombo, sizeof(newCombo));
        ImGui::SameLine();
        if (ImGui::Button("新增组合")) {
            std::string nm = Trim(newCombo);
            if (!nm.empty()) {
                // 复制当前激活条目（该武器）到新组合
                bool has = false;
                for (const auto& e : cfg.entries) if (e.weaponType == w && e.combo == nm) has = true;
                if (!has) {
                    std::vector<SoundEntry> copy;
                    for (const auto& e : cfg.entries) if (e.weaponType == w && EntryActive(e)) { SoundEntry c = e; c.combo = nm; copy.push_back(c); }
                    for (auto& c : copy) cfg.entries.push_back(std::move(c));
                    cfg.active[w] = nm;
                    newCombo[0] = 0;
                    mDirty = true;
                    status = "已新增组合并切换： " + nm;
                } else {
                    status = "组合已存在: " + nm;
                }
            }
        }
    }
}

void App::DrawCapturePanel() {
    // 面板内整体压紧行距，同屏显示更多条目
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * dpiScale, 2.0f * dpiScale));
    // 第一行：标题 + 清空（文字与按钮对齐）
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("实时捕获");
    ImGui::SameLine(0, 8);
    if (ImGui::Button("清空")) history.clear();

    // 第二行：连接状态（紧凑，避免超出面板宽度）
    if (liveOk && live.attached) {
        ImGui::TextColored(C_GREEN, "●");
        ImGui::SameLine(0, 4);
        ImGui::Text("已连接");
        const char* wn = (live.weapon >= 0 && live.weapon <= 13) ? WeaponName(live.weapon) : "?";
        ImGui::SameLine(0, 8);
        ImGui::TextColored(C_ACCENT, "fsm %d", live.fsm);
        ImGui::SameLine(0, 6);
        ImGui::TextColored(C_ACCENT, "lmt %d", live.lmt);
        ImGui::SameLine(0, 6);
        ImGui::Text("%s", wn);
        if (!live.error.empty()) {
            float cw = ImGui::GetContentRegionAvail().x;
            ImGui::TextDisabled("%s", ClipText(live.error, cw).c_str());
        }
    } else {
        ImGui::TextColored(C_GRAY, "●");
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("未连接");
        ImGui::SameLine(0, 8);
        if (ImGui::Button("重连")) game.Detach();
    }
    ImGui::Separator();
    ImGui::TextDisabled("历史 (fsm≠0 已高亮)");

    // 只显示最新、能放下的条数，避免面板出现滚动条 / 显示不全
    float lineH2 = ImGui::GetTextLineHeight();
    float cardH = lineH2 * 2.0f + 14.0f * dpiScale;
    float gap = 1.0f * dpiScale;
    float cardW = ImGui::GetContentRegionAvail().x - 14.0f * dpiScale;   // 名称截断用（卡片内缩进留白）
    if (cardW < 40) cardW = 40;
    int shown = 0;
    for (int i = (int)history.size() - 1; i >= 0; --i) {
        const HistEntry& h = history[i];
        float rowH = (h.fsm == 0) ? (lineH2 + 10.0f * dpiScale) : (cardH + gap + 8.0f * dpiScale);
        // 每次用“实际剩余高度”判断后再显示，避免估算误差累积导致溢出/滚动条
        if (ImGui::GetContentRegionAvail().y - rowH < 2.0f * dpiScale) break;
        ImGui::PushID(i);
        if (h.fsm == 0) {
            std::string st = std::string("   ") + h.time + "  fsm 0 · 自由态";
            ImGui::TextDisabled("%s", ClipText(st, cardW + 6.0f * dpiScale).c_str());
        } else {
            const char* wn = (h.weapon >= 0 && h.weapon <= 13) ? WeaponName(h.weapon) : "?";
            std::string nm = ResolveName(h.weapon, h.fsm, h.lmt);
            // 圆角高亮卡片：紧凑两行（id 行 + 名称·时间行），[＋] 小按钮紧跟 id
            bool added = IsCapturedAdded(h.weapon, h.fsm, h.lmt);
            // 已添加的条目 → 绿色高亮；未添加 → 蓝色高亮
            ImGui::PushStyleColor(ImGuiCol_ChildBg, added ? ImVec4(0.16f, 0.78f, 0.25f, 0.14f) : ImVec4(0.0f, 0.48f, 1.0f, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_Border, added ? ImVec4(0.16f, 0.78f, 0.25f, 0.45f) : ImVec4(0.0f, 0.48f, 1.0f, 0.22f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * dpiScale, 2.0f * dpiScale));
            if (ImGui::BeginChild("##card", ImVec2(0, cardH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::Indent(4.0f * dpiScale);
                ImGui::TextColored(C_GREEN, "%s", wn);
                ImGui::SameLine(0, 5);
                ImGui::TextColored(C_ACCENT, "fsm %d", h.fsm);
                ImGui::SameLine(0, 5);
                ImGui::TextColored(C_ACCENT, "lmt %d", h.lmt);
                ImGui::SameLine(0, 10);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
                ImGui::PushStyleColor(ImGuiCol_Text, added ? C_GREEN : C_ACCENT);
                if (ImGui::Button(added ? "改" : "＋")) {
                    if (added) {
                        // 已添加：打开匹配的既有条目进行修改
                        for (int k = 0; k < (int)cfg.entries.size(); ++k) {
                            const SoundEntry& e = cfg.entries[k];
                            if (EntryActive(e) &&
                                e.fsmId == h.fsm &&
                                (e.weaponType < 0 || e.weaponType == h.weapon) &&
                                e.MatchesLmt(h.lmt)) {
                                OpenEditorEdit(k);
                                break;
                            }
                        }
                    } else {
                        std::string nm2 = ResolveName(h.weapon, h.fsm, h.lmt);
                        OpenEditorNew(h.weapon, h.fsm, h.lmt, nm2);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    if (added) ImGui::SetTooltip("已添加为条目，点【改】修改它");
                    else ImGui::SetTooltip("把 fsm %d / lmt %d / %s 填入新条目", h.fsm, h.lmt, wn);
                }
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                // 第二行：名称 · 时间（截断避免撑出卡片）
                std::string line2 = (nm.empty() ? "(未命名)" : nm) + " · " + h.time;
                ImGui::TextDisabled("%s", ClipText(line2, cardW).c_str());
                ImGui::Unindent(4.0f * dpiScale);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::Dummy(ImVec2(0, gap));
        }
        ImGui::PopID();
        ++shown;
    }
    if (history.empty())
        ImGui::TextDisabled("(还没有捕获记录：进游戏做派生动作后回来查看)");
    else if (shown < (int)history.size())
        ImGui::TextDisabled("…（更早 %d 条未显示）", (int)history.size() - shown);
    ImGui::PopStyleVar();
}

void App::DrawEntries() {
    ImGui::SetNextItemWidth(230 * dpiScale);
    ImGui::InputTextWithHint("##search", "搜索 FSM / LMT / 音效名...", searchBuf, sizeof(searchBuf));
    ImGui::SameLine(0, 10);
    if (PrimaryButton("+ 新增条目"))
        OpenEditorNew(weaponFilter >= 0 ? weaponFilter : -1);
    ImGui::Separator();

    std::string q = Trim(searchBuf);
    std::vector<int> idx;
    for (int i = 0; i < (int)cfg.entries.size(); ++i) {
        const SoundEntry& e = cfg.entries[i];
        if (weaponFilter == -2 && e.weaponType >= 0) continue;
        if (weaponFilter >= 0 && e.weaponType != weaponFilter) continue;
        if (!EntryActive(e)) continue;   // 只显示当前激活组合的条目
        if (!q.empty()) {
            bool hit = false;
            std::string nm = e.name;
            if (nm.empty()) nm = LookupFsmName(e.weaponType, e.fsmId, e.LmtAny());
            std::string lower = nm, ql = q;
            for (auto& c : lower) if (c >= 'A' && c <= 'Z') c += 32;
            for (auto& c : ql) if (c >= 'A' && c <= 'Z') c += 32;
            if (lower.find(ql) != std::string::npos) hit = true;
            if (!hit && !e.group.empty()) {
                std::string gl = e.group;
                for (auto& c : gl) if (c >= 'A' && c <= 'Z') c += 32;
                if (gl.find(ql) != std::string::npos) hit = true;
            }
            if (!hit && std::to_string(e.fsmId).find(q) != std::string::npos) hit = true;
            if (!hit && LmtText(e).find(q) != std::string::npos) hit = true;
            if (!hit) {
                for (const auto& s : e.def.specs) {
                    std::string sl = s.path;
                    for (auto& c : sl) if (c >= 'A' && c <= 'Z') c += 32;
                    if (sl.find(ql) != std::string::npos) { hit = true; break; }
                }
            }
            for (int gi = 0; gi < 4 && !hit; ++gi) {
                for (const auto& s : e.gauge[gi].specs) {
                    std::string sl = s.path;
                    for (auto& c : sl) if (c >= 'A' && c <= 'Z') c += 32;
                    if (sl.find(ql) != std::string::npos) { hit = true; break; }
                }
            }
            if (!hit) continue;
        }
        idx.push_back(i);
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.y < 80) avail.y = 80;
    ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                         ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("entries", 6, tf, avail)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
        ImGui::TableSetupColumn("武器", ImGuiTableColumnFlags_WidthFixed, 64 * dpiScale, 1);
        ImGui::TableSetupColumn("LMT", ImGuiTableColumnFlags_WidthFixed, 108 * dpiScale, 2);
        ImGui::TableSetupColumn("FSMId", ImGuiTableColumnFlags_WidthFixed, 70 * dpiScale, 3);
        ImGui::TableSetupColumn("音效", ImGuiTableColumnFlags_WidthStretch, 0.0f, 4);
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 180 * dpiScale, 5);
        ImGui::TableHeadersRow();

        for (int row = 0; row < (int)idx.size(); ++row) {
            const SoundEntry& e = cfg.entries[idx[row]];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            std::string nm = e.name;
            if (nm.empty()) nm = LookupFsmName(e.weaponType, e.fsmId, e.LmtAny());
            if (nm.empty()) nm = "条目";
            ImGui::Text("%s", nm.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(C_GREEN, "%s", WeaponName(e.weaponType));

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(C_ACCENT, "%s", LmtText(e).c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(C_ACCENT, "%d", e.fsmId);

            ImGui::TableSetColumnIndex(4);
            size_t total = e.def.specs.size();
            for (int gi = 0; gi < 4; ++gi) total += e.gauge[gi].specs.size();
            bool firstLine = true;
            for (size_t k = 0; k < e.def.specs.size(); ++k) {
                std::string label = e.def.specs[k].path + (e.def.specs[k].fixed ? " [F]" : "");
                if (!firstLine) ImGui::SameLine();
                ImGui::TextColored(C_AMBER, "%s", label.c_str());
                firstLine = false;
            }
            std::string gaugeSummary;
            if (e.weaponType == 3) {   // 刃时音效仅太刀
                for (int gi = 0; gi < 4; ++gi) {
                    if (e.gauge[gi].empty()) continue;
                    if (!gaugeSummary.empty()) gaugeSummary += " ";
                    gaugeSummary += std::string(GaugeUiName(gi)) + "时×" + std::to_string(e.gauge[gi].specs.size());
                }
            }
            if (!gaugeSummary.empty()) {
                ImGui::TextDisabled("[%s]", gaugeSummary.c_str());
            }
            if (total == 0) ImGui::TextDisabled("(无音效)");

            ImGui::TableSetColumnIndex(5);
            ImGui::PushID(row);
            ImGui::PushStyleColor(ImGuiCol_Text, C_ACCENT);
            if (ImGui::Button("编辑")) OpenEditorEdit(idx[row]);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("复制")) {
                cfg.entries.push_back(SoundEntry(e));
                mDirty = true;
                status = "已复制条目（未保存）";
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, C_RED);
            if (ImGui::Button("删除")) {
                cfg.entries.erase(cfg.entries.begin() + idx[row]);
                mDirty = true;
                status = "已删除条目（未保存）";
                ImGui::PopStyleColor();
                ImGui::PopID();
                ImGui::EndTable();
                return;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (idx.empty())
        ImGui::TextDisabled("无匹配条目。点【+ 新增条目】或右侧【加入条目】从捕获历史填入。");
}

void App::DrawStatus() {
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("| 目录: %s", BaseDir().c_str());
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("| 条目: %d", (int)cfg.entries.size());
    if (mDirty) {
        ImGui::SameLine(0, 20);
        ImGui::TextColored(C_AMBER, "● 未保存");
    }
}

void App::OpenEditorNew(int weapon, int fsm, int lmt, const std::string& name) {
    editor = Editor{};
    editor.open = true;
    editor.isNew = true;
    editor.index = -1;
    editor.weaponType = weapon;
    editor.fsmId = fsm;
    if (lmt >= 0) editor.lmt.push_back(lmt);
    FillLmtBuf(editor.lmtBuf, sizeof(editor.lmtBuf), editor.lmt);
    snprintf(editor.name, sizeof(editor.name), "%s", name.c_str());
}

void App::OpenEditorNew(int weapon) { OpenEditorNew(weapon, -1, -1, ""); }

void App::OpenEditorEdit(int index) {
    editor = Editor{};
    editor.open = true;
    editor.isNew = false;
    editor.index = index;
    const SoundEntry& e = cfg.entries[index];
    editor.weaponType = e.weaponType;
    editor.fsmId = e.fsmId;
    editor.lmt = e.lmt;
    FillLmtBuf(editor.lmtBuf, sizeof(editor.lmtBuf), editor.lmt);
    snprintf(editor.name, sizeof(editor.name), "%s", e.name.c_str());
    snprintf(editor.groupBuf, sizeof(editor.groupBuf), "%s", e.group.c_str());
    editor.pool[0] = e.def.specs;
    for (int i = 0; i < 4; ++i) editor.pool[i + 1] = e.gauge[i].specs;

    editor.checkDelayMs   = e.checkDelayMs;
    editor.checkTimeoutMs = e.checkTimeoutMs;
    editor.checkOffsetMs  = e.checkOffsetMs;
    editor.endOnAction    = e.endOnAction;
    editor.conds.clear();
    for (const auto& c : e.conds) {
        CondRow r;
        r.atEnd  = c.atEnd;
        r.parsed = ParseExprToTerms(c.expr, r.terms);
        if (!r.parsed) r.rawExpr = c.expr;
        r.pool = c.pool.specs;
        editor.conds.push_back(std::move(r));
    }
    // 认一下这条目是不是某个内置预设生成的（LMT + 条件表达式都对得上）
    editor.judgePreset = editor.conds.empty() ? 0 : (kPresetCount + 1);
    if (!editor.conds.empty()) {
        for (int pi = 0; pi < kPresetCount; ++pi) {
            const JudgePreset& ps = kPresets[pi];
            if (ps.weapon != e.weaponType) continue;
            int n = 0; while (ps.conds[n].expr) ++n;
            if ((int)editor.conds.size() != n) continue;
            bool same = true;
            for (int i = 0; i < n && same; ++i) {
                const std::string want = CondTrim(ps.conds[i].expr);
                std::string got = editor.conds[i].parsed
                                      ? TermsToExpr(editor.conds[i].terms)
                                      : editor.conds[i].rawExpr;
                // 比较时去掉空格，"dmg>0" 和 "dmg > 0" 视为同一条
                std::string a2, b2;
                for (char c2 : want) if (c2 != ' ') a2 += c2;
                for (char c2 : got)  if (c2 != ' ') b2 += c2;
                if (a2 != b2 || editor.conds[i].atEnd != ps.conds[i].atEnd) same = false;
            }
            if (same) { editor.judgePreset = pi + 1; editor.conds[0].label = ps.conds[0].label;
                        for (int i = 0; i < n; ++i) editor.conds[i].label = ps.conds[i].label;
                        break; }
        }
    }
}

void App::ApplyEditor() {
    SoundEntry e;
    e.weaponType = editor.weaponType;
    e.fsmId = editor.fsmId;
    e.name = Trim(editor.name);
    e.group = Trim(editor.groupBuf);
    // 组合归属：新条目加入当前武器的激活组合；编辑已有条目保持其原组合
    e.combo = editor.isNew
                  ? ((editor.weaponType >= 0) ? ActiveCombo(editor.weaponType) : std::string())
                  : ((editor.index >= 0 && editor.index < (int)cfg.entries.size())
                         ? cfg.entries[editor.index].combo
                         : std::string());
    // 解析 LMT 文本框：逗号/分号/空格分隔；-1 或空 = 不限
    std::string s = Trim(editor.lmtBuf);
    std::string cur;
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = (i < s.size()) ? s[i] : '\0';
        if (c == '\0' || c == ',' || c == ';' || c == ' ' || c == '\t') {
            std::string t = Trim(cur);
            if (!t.empty()) {
                int v = std::atoi(t.c_str());
                if (v != -1) {
                    bool dup = false;
                    for (int x : e.lmt) if (x == v) { dup = true; break; }
                    if (!dup) e.lmt.push_back(v);
                }
            }
            cur.clear();
            if (c == '\0') break;
        } else {
            cur += c;
        }
    }
    e.def.specs = editor.pool[0];
    for (int i = 0; i < 4; ++i) e.gauge[i].specs = editor.pool[i + 1];

    if (editor.judgePreset > 0 && !editor.conds.empty()) {
        e.checkDelayMs   = editor.checkDelayMs;
        e.checkTimeoutMs = editor.checkTimeoutMs > 0 ? editor.checkTimeoutMs : 2500;
        e.checkOffsetMs  = editor.checkOffsetMs < 0 ? 0 : editor.checkOffsetMs;
        e.endOnAction    = editor.endOnAction;
        for (const auto& r : editor.conds) {
            CondSpec c;
            c.expr  = r.parsed ? TermsToExpr(r.terms) : r.rawExpr;
            c.atEnd = r.atEnd;
            c.pool.specs = r.pool;
            if (c.expr.empty() || c.pool.empty()) continue;
            e.conds.push_back(std::move(c));
        }
    }
    if (editor.isNew) {
        cfg.entries.push_back(std::move(e));
    } else if (editor.index >= 0 && editor.index < (int)cfg.entries.size()) {
        cfg.entries[editor.index] = std::move(e);
    }
    mDirty = true;
    status = "已修改（记得保存）";
}

void App::DrawEditorDetached() {
    if (!editor.open) return;
    // 延迟添加/移除：在控件提交前先应用"上一帧请求"的变更，避免在 ImGui 提交
    // 控件的同帧内扩容/收缩 pool（会弄脏 ImGui 状态/堆，导致 SameLine 等访问违例）。
    static std::vector<std::pair<int, std::string>> pendingAdd;
    static int pendingRmPool = -1, pendingRmIndex = -1;
    if (pendingRmPool >= 0 && pendingRmPool < 5 &&
        pendingRmIndex >= 0 && pendingRmIndex < (int)editor.pool[pendingRmPool].size()) {
        editor.pool[pendingRmPool].erase(editor.pool[pendingRmPool].begin() + pendingRmIndex);
    }
    pendingRmPool = pendingRmIndex = -1;
    for (const auto& pa : pendingAdd) {
        const int p = pa.first;
        const std::string& s = pa.second;
        if (p < 0 || p >= 5 || s.empty()) continue;
        bool dup = false;
        for (const auto& ex : editor.pool[p]) if (ex.path == s) { dup = true; break; }
        if (!dup) {
            editor.pool[p].push_back(SoundSpec());
            editor.pool[p].back().path = s;
        }
    }
    pendingAdd.clear();

    // 独立编辑窗口（第二个原生窗口+独立 ImGui 上下文）。
    // 用普通窗口（自带滚动）+ 内容直接绘制，避免复杂子区/横向滚动/嵌套 group
    // 组合造成 ImGui 内部状态失衡而破坏堆。
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##edithost", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float btnH = ImGui::GetTextLineHeightWithSpacing() +
                       ImGui::GetStyle().FramePadding.y * 2.0f + 10.0f * dpiScale;
    ImGui::BeginChild("##edit", ImVec2(0, -btnH), false);

    ImGui::SetNextItemWidth(420 * dpiScale);
    ImGui::InputText("名称", editor.name, sizeof(editor.name));
    ImGui::SetNextItemWidth(340 * dpiScale);
    ImGui::InputTextWithHint("动作组(可空)", "如：气刃4（同一招多帧填同组，只响一次）", editor.groupBuf, sizeof(editor.groupBuf));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("动作组：把同一招的多个触发条目（判定帧、升刃前后的帧等）填相同组名，"
                          "整招只响一次；刃色以首个触发瞬间为准，命中升刃不会重复播放。");

    static const char* wItems[] = {
        "任意 (-1)", "0 大剑", "1 片手", "2 双刀", "3 太刀", "4 大锤", "5 笛子",
        "6 长枪", "7 铳枪", "8 斩斧", "9 盾斧", "10 虫棍", "11 弓箭", "12 轻弩", "13 重弩"
    };
    int wi = editor.weaponType + 1;
    if (wi < 0) wi = 0;
    if (wi > 14) wi = 0;
    ImGui::SetNextItemWidth(200 * dpiScale);
    ImGui::Combo("武器", &wi, wItems, 15);
    editor.weaponType = wi - 1;

    ImGui::SetNextItemWidth(190 * dpiScale);
    ImGui::InputInt("FSMId (-1=不限)", &editor.fsmId, 1, 100);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(280 * dpiScale);
    ImGui::InputTextWithHint("##lmt", "LMT（逗号分隔；空/-1=不限）", editor.lmtBuf, sizeof(editor.lmtBuf));

    // =====================================================================
    //  判定：动作匹配上只是「开窗」，接着盯一段时间，按条件挑音效池。
    //  用来做「打中/落空」「掉刃/升刃」这类必须观察一段时间才知道结果的触发。
    //  两层：预设（挑个现成的，只填 wav）/ 高级（自己配条件）。
    // =====================================================================
    static int pendingCondRm = -1;                                  // 待删除的条件行
    static std::vector<std::pair<int, std::string>> pendingCondAdd; // 待加入的音效
    if (pendingCondRm >= 0 && pendingCondRm < (int)editor.conds.size())
        editor.conds.erase(editor.conds.begin() + pendingCondRm);
    pendingCondRm = -1;
    for (const auto& pa : pendingCondAdd) {
        if (pa.first < 0 || pa.first >= (int)editor.conds.size() || pa.second.empty()) continue;
        auto& pl = editor.conds[pa.first].pool;
        bool dup = false;
        for (const auto& ex : pl) if (ex.path == pa.second) { dup = true; break; }
        if (!dup) { pl.push_back(SoundSpec()); pl.back().path = pa.second; }
    }
    pendingCondAdd.clear();

    ImGui::Separator();
    ImGui::TextColored(C_AMBER, "判定模式");

    std::vector<const char*> jItems;
    jItems.push_back("不判定");
    for (int i = 0; i < kPresetCount; ++i) jItems.push_back(kPresets[i].name);
    jItems.push_back("自定义");

    const int prevPreset = editor.judgePreset;
    ImGui::SetNextItemWidth(430 * dpiScale);
    ImGui::Combo("##judgemode", &editor.judgePreset, jItems.data(), (int)jItems.size());
    if (editor.judgePreset != prevPreset) {
        // 换预设时保留用户已经挑好的 wav（按行号对应），其余按预设重填
        std::vector<std::vector<SoundSpec>> keep;
        for (auto& r : editor.conds) keep.push_back(r.pool);
        editor.conds.clear();
        if (editor.judgePreset >= 1 && editor.judgePreset <= kPresetCount) {
            const JudgePreset& ps = kPresets[editor.judgePreset - 1];
            editor.weaponType     = ps.weapon;
            editor.checkDelayMs   = ps.delayMs;
            editor.checkTimeoutMs = ps.timeoutMs;
            editor.checkOffsetMs  = ps.offsetMs;
            editor.endOnAction    = true;
            snprintf(editor.lmtBuf, sizeof(editor.lmtBuf), "%s", ps.lmt);
            for (int i = 0; ps.conds[i].expr; ++i) {
                CondRow r;
                r.atEnd  = ps.conds[i].atEnd;
                r.label  = ps.conds[i].label;
                r.parsed = ParseExprToTerms(ps.conds[i].expr, r.terms);
                if (!r.parsed) r.rawExpr = ps.conds[i].expr;
                if (i < (int)keep.size()) r.pool = keep[i];
                editor.conds.push_back(r);
            }
        } else if (editor.judgePreset == kPresetCount + 1) {
            if (editor.checkTimeoutMs <= 0) editor.checkTimeoutMs = 2500;
            CondRow r;
            r.label = "条件";
            CondTerm t;                       // 默认 dmg > 0
            r.terms.push_back(t);
            if (!keep.empty()) r.pool = keep[0];
            editor.conds.push_back(r);
        }
    }

    if (editor.judgePreset > 0) {
        if (editor.judgePreset <= kPresetCount) {
            ImGui::TextWrapped("%s", kPresets[editor.judgePreset - 1].note);
            ImGui::TextDisabled("动作和时间参数已按实测填好，只需要给不同情况选择你想要的wav文件即可。");
        }
        ImGui::Checkbox("高级设置", &editor.advanced);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("勾选以修改具体判定时间与条件");

        if (editor.advanced) {
            ImGui::Indent();
            ImGui::SetNextItemWidth(150 * dpiScale);
            ImGui::InputInt("判定起点(ms)", &editor.checkDelayMs, 50, 200);
            if (editor.checkDelayMs < 0) editor.checkDelayMs = 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("动作开始后的判定时间起点，在此时间前不进行以下判定");
            ImGui::SameLine(0, 16);
            ImGui::SetNextItemWidth(150 * dpiScale);
            ImGui::InputInt("判定终点(ms)", &editor.checkTimeoutMs, 100, 500);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("动作开始后的判定时间终点，超过时间即不进行以下判定");

            ImGui::Checkbox("将动作结束(或打断)作为判定时机", &editor.endOnAction);
            ImGui::SetNextItemWidth(150 * dpiScale);
            ImGui::InputInt("余量(ms)", &editor.checkOffsetMs, 50, 200);
            if (editor.checkOffsetMs < 0) editor.checkOffsetMs = 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("本程序会自动记录并更新某一动作的最晚出伤时间\n"
                                  "最晚出伤时间加上余量便是动作结束的判定时间。");
            ImGui::Unindent();
        }

        ImGui::Spacing();
        for (int ci = 0; ci < (int)editor.conds.size(); ++ci) {
            CondRow& r = editor.conds[ci];
            ImGui::PushID(2000 + ci);
            const std::string exprTxt = r.parsed ? TermsToExpr(r.terms) : r.rawExpr;
            char head[240];
            snprintf(head, sizeof(head), "%s%s   [ %s ]   %d 条音效",
                     r.label.empty() ? "条件" : r.label.c_str(),
                     r.atEnd ? "（窗口结束后进行判定）" : "",
                     exprTxt.c_str(), (int)r.pool.size());
            if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                if (editor.advanced) {
                    if (!r.parsed) {
                        ImGui::TextDisabled("此表达式无法解析，保持原文：%s", r.rawExpr.c_str());
                    } else {
                        for (int ti = 0; ti < (int)r.terms.size(); ++ti) {
                            CondTerm& t = r.terms[ti];
                            ImGui::PushID(ti);
                            if (ti > 0) {
                                int oi = t.orBefore ? 1 : 0;
                                static const char* lk[] = { "并且", "或者" };
                                ImGui::SetNextItemWidth(75 * dpiScale);
                                ImGui::Combo("##lk", &oi, lk, 2);
                                t.orBefore = (oi == 1);
                                ImGui::SameLine();
                            }
                            ImGui::SetNextItemWidth(125 * dpiScale);
                            ImGui::Combo("##var", &t.var, kCondVarLabels, kCondVarCount);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(110 * dpiScale);
                            ImGui::Combo("##op", &t.op, kCondOpLabels, kCondOpCount);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(95 * dpiScale);
                            ImGui::InputInt("##val", &t.val, 0, 0);
                            if (r.terms.size() > 1) {
                                ImGui::SameLine();
                                if (ImGui::SmallButton("x")) {
                                    r.terms.erase(r.terms.begin() + ti);
                                    ImGui::PopID();
                                    break;
                                }
                            }
                            ImGui::PopID();
                        }
                        if (ImGui::SmallButton("+ ")) {
                            CondTerm t;
                            r.terms.push_back(t);
                        }
                    }
                    ImGui::Checkbox("在窗口结束后进行判定", &r.atEnd);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("勾选以在窗口结束后进行所有条件的综合判定");
                    ImGui::SameLine(0, 20);
                    if (ImGui::SmallButton("删除")) pendingCondRm = ci;
                }

                for (size_t i = 0; i < r.pool.size(); ++i) {
                    ImGui::PushID((int)i);
                    SoundSpec& sp = r.pool[i];
                    ImGui::TextColored(C_AMBER, "%s", sp.path.c_str());
                    ImGui::SameLine(0, 10);
                    if (ImGui::Button("试听")) PlaySoundPreview(sp.path, sp.vol, sp.delay);
                    ImGui::SameLine(0, 8);
                    if (ImGui::Button("移除")) {
                        r.pool.erase(r.pool.begin() + i);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (r.pool.empty()) ImGui::TextDisabled("(未添加音效)");
                if (ImGui::Button("浏览...")) {
                    std::vector<SoundSpec> tmp;
                    if (BrowseSounds(tmp) > 0)
                        for (size_t k = 0; k < tmp.size(); ++k)
                            pendingCondAdd.push_back(std::make_pair(ci, tmp[k].path));
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (editor.advanced && ImGui::Button("+ 添加条件")) {
            CondRow r;
            r.label = "条件";
            CondTerm t;
            r.terms.push_back(t);
            editor.conds.push_back(r);
        }
        if (editor.judgePreset >= 1 && editor.judgePreset <= kPresetCount)
            ImGui::TextDisabled("以上都不成立时 -> %s，播下面的「默认音效」。",
                                kPresets[editor.judgePreset - 1].fallbackLabel);
        else
            ImGui::TextDisabled("以上都不成立时，播下面的「默认音效」。");
    }

    ImGui::Separator();
    ImGui::TextDisabled("命中动作时：固定(F)音效全部播放 + 未固定中随机一条，同时叠播；");
    const bool ls = (editor.weaponType == 3);
    if (ls)
        ImGui::TextDisabled("无刃时/白刃时/黄刃时/红刃时 未配置时，回退默认音效。");
    else
        ImGui::TextDisabled("刃时音效(无/白/黄/红)仅对太刀可用，当前武器只有默认音效。");
    ImGui::Spacing();

    static char newPath[5][512] = {};
    for (int p = 0; p < 5; ++p) {
        if (p > 0 && !ls) continue;   // 非太刀：不显示刃时池
        std::vector<SoundSpec>& pool = editor.pool[p];
        char head[160];
        if (p == 0)
            snprintf(head, sizeof(head), ls ? "默认音效(任意刃时)  %d 条" : "默认音效  %d 条",
                     (int)pool.size());
        else
            snprintf(head, sizeof(head), "%s时(Sound:%s)  %d 条",
                     GaugeUiName(p - 1), GaugeTagName(p - 1), (int)pool.size());

        bool openByDefault = (p == 0) || !pool.empty();
        if (ImGui::CollapsingHeader(head, openByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            ImGui::PushID(p);

            for (size_t i = 0; i < pool.size(); ++i) {
                ImGui::PushID((int)i);
                SoundSpec& sp = pool[i];

                ImGui::TextColored(C_AMBER, "%s", sp.path.c_str());
                if (sp.fixed) { ImGui::SameLine(0, 4); ImGui::TextColored(C_RED, "F"); }
                ImGui::SameLine(0, 10);
                if (ImGui::Checkbox("固定##fx", &sp.fixed)) {}
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("固定：命中该动作时总是播放这条；同文件 400ms 内不重复");
                ImGui::SameLine(0, 10);
                if (ImGui::Button("移除")) {
                    pendingRmPool = p;
                    pendingRmIndex = (int)i;
                    ImGui::PopID();
                    break;
                }

                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("延时");
                ImGui::SameLine();
                MacSlider("##delay", sp.delay, 0, 2000, 120 * dpiScale, " ms", dpiScale);
                ImGui::SameLine(0, 12);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("音量");
                ImGui::SameLine();
                MacSlider("##vol", sp.vol, 0, 100, 90 * dpiScale, "", dpiScale);
                ImGui::SameLine(0, 14);
                if (ImGui::Button("试听")) PlaySoundPreview(sp.path, sp.vol, sp.delay);

                ImGui::PopID();
            }
            if (pool.empty())
                ImGui::TextDisabled(ls ? "(空：命中时无音效，未配置的刃时回退默认音效)"
                                       : "(空：命中时无音效)");

            char addId[32], b1[32], b2[32];
            snprintf(addId, sizeof(addId), "##add%d", p);
            snprintf(b1, sizeof(b1), "添加##p%d", p);
            snprintf(b2, sizeof(b2), "浏览...##p%d", p);
            ImGui::SetNextItemWidth(300 * dpiScale);
            ImGui::InputTextWithHint(addId, "路径，如 sounds/xxx.wav", newPath[p], sizeof(newPath[p]));
            ImGui::SameLine();
            if (ImGui::Button(b1)) {
                std::string s = Trim(newPath[p]);
                if (!s.empty()) {
                    pendingAdd.emplace_back(p, s);
                    newPath[p][0] = 0;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(b2)) BrowseSounds(pool);
            ImGui::SameLine();
            ImGui::TextDisabled("(wav 放在 sounds\\ 下)");
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    if (PrimaryButton("确定")) {
        ApplyEditor();
        editor.open = false;   // 主循环据此隐藏/关闭独立编辑窗口
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(120 * dpiScale, 0))) {
        editor.open = false;
    }
    ImGui::End();   // 宿主窗口（##edithost）
}

void App::DrawFsmWindow() {
    ImGui::SetNextWindowSize(ImVec2(380 * dpiScale, 440 * dpiScale), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72f, 0.72f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.93f, 0.93f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.93f, 0.93f, 0.95f, 1.0f));
    if (!ImGui::Begin("FSM/LMT 查询", &fsmWinOpen)) {
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        return;
    }

    ImGui::SetNextItemWidth(180 * dpiScale);
    ImGui::InputTextWithHint("##fsmq", "搜名称/fsm/lmt", fsmQuery, sizeof(fsmQuery));
    ImGui::SameLine();
    static const char* wItems[] = {
        "全部", "0 大剑", "1 片手", "2 双刀", "3 太刀", "4 大锤", "5 笛子",
        "6 长枪", "7 铳枪", "8 斩斧", "9 盾斧", "10 虫棍", "11 弓箭", "12 轻弩", "13 重弩"
    };
    int wi = fsmWeaponFilter + 1;
    ImGui::SetNextItemWidth(120 * dpiScale);
    ImGui::Combo("##wfilter", &wi, wItems, 15);
    fsmWeaponFilter = wi - 1;
    ImGui::Separator();

    auto results = SearchFsmDb(fsmQuery, fsmWeaponFilter);
    ImGui::BeginChild("fsmres", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& r : results) {
        char lmtStr[32], fsmStr[32];
        snprintf(lmtStr, sizeof(lmtStr), "%d", r.lmt);
        snprintf(fsmStr, sizeof(fsmStr), "%d", r.fsm);
        char lbl[256];
        snprintf(lbl, sizeof(lbl), "%s  ·  fsm %s  ·  lmt %s  ·  %s",
                 r.weapon >= 0 ? WeaponName(r.weapon) : "通用",
                 r.fsm >= 0 ? fsmStr : "-", r.lmt >= 0 ? lmtStr : "-", r.name.c_str());
        if (ImGui::Selectable(lbl))
            OpenEditorNew(r.weapon, r.fsm, r.lmt, r.name);
    }
    if (results.empty()) ImGui::TextDisabled("无匹配结果");
    ImGui::EndChild();
    ImGui::TextDisabled("可在 exe 同目录放 fsm_db.csv 扩展知识库 (weapon,fsm,lmt,name)");

    // ---- 共享动作 ID 库：导出实测 / 导入合并 / 提交 / 获取最新 ----
    ImGui::Separator();
    ImGui::TextColored(C_AMBER, "共享动作 ID 库");
    if (ImGui::Button("导出实测ID")) ExportMeasuredIdsCsv();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("把「实时捕获」历史 + 当前条目里的 weapon/fsm/lmt 导出为\nfsm_db_submission.csv，可自己留着或提交共享");
    ImGui::SameLine();
    if (ImGui::Button("导入CSV合并")) ImportIdsCsv();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("选择别人分享的 csv（weapon,fsm,lmt,name），合并进 exe 同目录的 fsm_db.csv");
    ImGui::SameLine();
    if (ImGui::Button("提交到共享库")) SubmitIdsToGithub();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("导出并复制到剪贴板，然后打开 GitHub 提交页；粘贴提交即可贡献你的实测 ID");
    ImGui::SameLine();
    if (ImGui::Button("获取最新库")) FetchLatestFsmDb();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("从仓库下载最新的 fsm_db.csv 覆盖到 exe 同目录（需要网络）");
    ImGui::TextDisabled("贡献一次，别人就不用再逐招测了；当前库 %d 条，本地文件: %s",
                        (int)GetFsmDb().size(), FsmDbPath().c_str());

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void App::PlaySoundPreview(const std::string& rel, int vol, int delayMs) {
    std::string full = BaseDir();
    for (char c : rel) full += (c == '/') ? '\\' : c;
    // 实际增益 = 主音量 × 该音效倍率
    float gain = (cfg.global.volume / 100.0f) * (vol / 100.0f);
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    PlayPreviewFile(Utf8ToWide(full), gain, delayMs > 0 ? (unsigned)delayMs : 0);
}

std::string App::OpenFileDialogCsv() {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = L"CSV 动作ID库 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(wchar_t);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return "";
    return Utf8FromWide(buf);
}

void App::ExportMeasuredIdsCsv() {
    // 汇总"实测到的动作 ID"：实时捕获历史 + 当前配置条目（按 weapon/fsm/lmt 去重）
    std::vector<FsmDbEntry> ids;
    auto exists = [&](int w, int f, int l) {
        for (const auto& e : ids) if (e.weapon == w && e.fsm == f && e.lmt == l) return true;
        return false;
    };
    auto add = [&](int w, int f, int l, const std::string& nm) {
        if (f <= 0 && l <= 0) return;            // 跳过自由态(0/-1)
        if (exists(w, f, l)) return;
        FsmDbEntry e;
        e.weapon = w; e.fsm = f; e.lmt = l;
        e.name = nm.empty() ? std::string("(未命名)") : nm;
        ids.push_back(e);
    };
    for (const auto& h : history)
        add(h.weapon, h.fsm, h.lmt, ResolveName(h.weapon, h.fsm, h.lmt));
    for (const auto& e : cfg.entries)
        add(e.weaponType, e.fsmId, e.LmtAny(), e.name);

    const std::string path = BaseDir() + "fsm_db_submission.csv";
    if (!SaveFsmDbCsv(path, ids)) { status = "导出失败: " + path; return; }
    status = "已导出 " + std::to_string(ids.size()) + " 条实测动作ID -> " + path;
}

void App::ImportIdsCsv() {
    const std::string p = OpenFileDialogCsv();
    if (p.empty()) return;
    const int added = MergeFsmDbCsv(p, FsmDbPath());
    ReloadFsmDb();
    EnrichNames();
    if (added > 0) {
        mDirty = true;
        status = "已合并 " + std::to_string(added) + " 条新动作ID -> " + FsmDbPath();
    } else {
        status = "没有可合并的新条目（都已存在）";
    }
}

void App::SubmitIdsToGithub() {
    ExportMeasuredIdsCsv();
    const std::string path = BaseDir() + "fsm_db_submission.csv";
    // 把 CSV 内容放进剪贴板，打开提交页后直接粘贴即可
    std::string content;
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") == 0 && f) {
            char buf[4096];
            size_t n = 0;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
            fclose(f);
        }
    }
    if (!content.empty()) ImGui::SetClipboardText(content.c_str());
    ShellExecuteW(nullptr, L"open", kSubmitIssueUrl, nullptr, nullptr, SW_SHOWNORMAL);
    status = "已导出 " + path + "，内容已复制到剪贴板；请在打开的页面粘贴提交";
}

void App::FetchLatestFsmDb() {
    const std::wstring dst = Utf8ToWide(FsmDbPath());
    const HRESULT hr = URLDownloadToFileW(nullptr, kSharedFsmDbUrl, dst.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
        ReloadFsmDb();
        EnrichNames();
        status = "已更新共享动作ID库 -> " + FsmDbPath();
    } else {
        status = "获取最新ID库失败（检查网络或代理）";
    }
}

std::string App::OpenFileDialogIni() {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = L"INI 配置文件 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(wchar_t);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return "";
    return Utf8FromWide(buf);
}

int App::BrowseSounds(std::vector<SoundSpec>& out) {
    std::string sdir = BaseDir() + "sounds";
    std::wstring target = Utf8ToWide(sdir);
    CreateDirectoryW(target.c_str(), nullptr);
    std::wstring targetNorm = target;
    if (!targetNorm.empty() && targetNorm.back() != L'\\') targetNorm += L'\\';

    wchar_t buf[16384] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    // 以编辑窗口为对话框所有者：对话框在编辑窗口上居中，关闭后激活自动回到编辑窗口，
    // 避免模态对话框结束后编辑窗口被主窗口遮挡（层级跑到主 GUI 后面）。
    ofn.hwndOwner = (editHwnd && IsWindow((HWND)editHwnd)) ? (HWND)editHwnd : (HWND)hwnd;
    ofn.lpstrFilter = L"WAV 音效 (*.wav)\0*.wav\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 16384;
    ofn.lpstrInitialDir = target.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        // 取消选择：同样把编辑窗口带回最前（对话框期间它可能被主窗口盖住）
        if (editHwnd && IsWindow((HWND)editHwnd))
            SetWindowPos((HWND)editHwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    }
    // 对话框正常返回：把编辑窗口带回 z-order 顶部
    if (editHwnd && IsWindow((HWND)editHwnd))
        SetWindowPos((HWND)editHwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    std::vector<std::wstring> files;
    wchar_t* p = buf;
    std::wstring first = p;
    size_t len = first.size();
    if (p[len + 1] == L'\0') {
        files.push_back(first);
    } else {
        std::wstring dir = first;
        p += len + 1;
        while (*p) {
            std::wstring fn = p;
            files.push_back(dir + L"\\" + fn);
            p += fn.size() + 1;
        }
    }

    int added = 0;
    for (const auto& f : files) {
        size_t slash = f.find_last_of(L"\\/");
        std::wstring fn = (slash == std::wstring::npos) ? f : f.substr(slash + 1);
        std::wstring fileDir = (slash == std::wstring::npos) ? L"" : f.substr(0, slash + 1);
        if (fileDir.empty() || _wcsicmp(fileDir.c_str(), targetNorm.c_str()) != 0)
            CopyFileW(f.c_str(), (targetNorm + fn).c_str(), FALSE);

        std::string rel = "sounds/" + Utf8FromWide(fn);
        bool dup = false;
        for (const auto& s : out) if (s.path == rel) { dup = true; break; }
        if (!dup) {
            out.push_back(SoundSpec());
            out.back().path = rel;
            ++added;
        }
    }
    return added;
}

void App::Save() {
    if (!cfg.loaded || cfg.path.empty()) { SaveAs(); return; }
    if (SaveConfig(cfg.path, cfg)) {
        mDirty = false;
        status = "已保存: " + cfg.path;
        mSaveFlash = 2.0f;
    } else {
        status = "保存失败!";
    }
}

void App::SaveAs() {
    wchar_t buf[MAX_PATH * 2] = {};
    wcscpy_s(buf, L"WeaponSoundEnhance.ini");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = L"INI 配置文件 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(wchar_t);
    ofn.lpstrDefExt = L"ini";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    std::string p = Utf8FromWide(buf);
    if (SaveConfig(p, cfg)) {
        cfg.path = p;
        cfg.loaded = true;
        mDirty = false;
        status = "已保存: " + p;
        mSaveFlash = 2.0f;
    } else {
        status = "保存失败!";
    }
}

void App::Load(const std::string& path) {
    if (LoadConfig(path, cfg)) {
        EnrichNames();
        status = "已加载: " + path;
        mDirty = false;
    } else {
        status = "加载失败: " + path;
    }
}

void App::EnrichNames() {
    for (auto& e : cfg.entries) {
        if (e.name.empty())
            e.name = LookupFsmName(e.weaponType, e.fsmId, e.LmtAny());
    }
}

std::string App::ResolveName(int weapon, int fsm, int lmt) const {
    // 先查知识库
    std::string n = LookupFsmName(weapon, fsm, lmt);
    if (!n.empty()) return n;
    // 再查用户已配置的条目（武器/fsm/lmt 匹配，-1 视为不限）
    for (const auto& e : cfg.entries) {
        if (!EntryActive(e)) continue;
        if (e.fsmId != fsm) continue;
        if (e.weaponType >= 0 && e.weaponType != weapon) continue;
        if (!e.MatchesLmt(lmt)) continue;
        if (!e.name.empty()) return e.name;
    }
    return std::string();
}

bool App::IsCapturedAdded(int weapon, int fsm, int lmt) const {
    for (const auto& e : cfg.entries) {
        if (!EntryActive(e)) continue;
        if (e.fsmId != fsm) continue;
        if (e.weaponType >= 0 && e.weaponType != weapon) continue;
        if (!e.MatchesLmt(lmt)) continue;
        return true;
    }
    return false;
}

void App::Draw() {
    PollGame();
    if (mSaveFlash > 0.0f) mSaveFlash -= ImGui::GetIO().DeltaTime;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("WeaponSoundEnhance 配置工具", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    DrawToolbar();
    ImGui::Separator();

    float sideW = 196 * dpiScale;
    float histW = 280 * dpiScale;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // 底部状态栏预留高度，避免把状态信息挤到可视区外
    float statusH = ImGui::GetTextLineHeightWithSpacing() + 14.0f * dpiScale;
    float bodyH = avail.y - statusH;
    if (bodyH < 50) bodyH = 50;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float centerW = avail.x - sideW - histW - spacing * 2;
    if (centerW < 80) centerW = 80;

    ImGui::BeginChild("##left", ImVec2(sideW, bodyH), true);
    DrawWeaponTree();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##center", ImVec2(centerW, bodyH), false);
    DrawEntries();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, bodyH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawCapturePanel();
    ImGui::EndChild();

    ImGui::Separator();
    DrawStatus();
    ImGui::End();

    // 编辑内容改由独立原生窗口（main.cpp 的第二个 ImGui 上下文）绘制，
    // 不再作为主窗内弹窗。DrawEditorDetached 由编辑窗渲染循环调用。
    if (fsmWinOpen) DrawFsmWindow();
}
