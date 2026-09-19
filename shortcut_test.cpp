// 桌面快捷方式的离线测试：调真正的 plugin::MakeShortcutW 建一个 .lnk，
// 再用 IShellLink 读回来，确认目标路径写对了。
//
// 建在临时目录里，不碰真桌面 —— 测试不该在谁的桌面上留东西。
#include "WeaponSoundEnhance.cpp"

#include <cstdio>
#include <string>

static int bad = 0;

int main()
{
    plugin::gLogPath = L"shortcut_test.log";
    ::DeleteFileW(plugin::gLogPath.c_str());

    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    wchar_t tmp[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = tmp;
    const std::wstring lnk = dir + L"wse_shortcut_test.lnk";
    ::DeleteFileW(lnk.c_str());

    // 拿一个一定存在的 exe 当目标
    wchar_t sysdir[MAX_PATH] = {};
    ::GetSystemDirectoryW(sysdir, MAX_PATH);
    const std::wstring target = std::wstring(sysdir) + L"\\notepad.exe";

    printf("目标 : %ls\n", target.c_str());
    printf("快捷 : %ls\n\n", lnk.c_str());

    const bool ok = plugin::MakeShortcutW(target, lnk, dir, L"测试用");
    printf("  %-4s 建快捷方式\n", ok ? "OK" : "FAIL");
    if (!ok) ++bad;

    const bool exists = plugin::FileExistsW(lnk);
    printf("  %-4s 文件确实落盘了\n", exists ? "OK" : "FAIL");
    if (!exists) ++bad;

    // 读回来对一下目标路径，光看文件存在不够 —— 空壳 .lnk 也是文件
    if (exists) {
        IShellLinkW* sl = nullptr;
        std::wstring got;
        if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_IShellLinkW, reinterpret_cast<void**>(&sl))) &&
            sl != nullptr) {
            IPersistFile* pf = nullptr;
            if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile,
                                             reinterpret_cast<void**>(&pf))) && pf) {
                if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
                    wchar_t buf[MAX_PATH] = {};
                    if (SUCCEEDED(sl->GetPath(buf, MAX_PATH, nullptr, 0))) got = buf;
                }
                pf->Release();
            }
            sl->Release();
        }
        const bool same = !got.empty() &&
                          _wcsicmp(got.c_str(), target.c_str()) == 0;
        printf("  %-4s 读回来的目标对得上  [%ls]\n", same ? "OK" : "FAIL", got.c_str());
        if (!same) ++bad;
    }

    ::DeleteFileW(lnk.c_str());
    ::CoUninitialize();

    printf("\n%s\n", bad ? "有失败项" : "全部通过");
    return bad ? 1 : 0;
}
