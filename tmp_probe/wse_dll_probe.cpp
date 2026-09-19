// 临时诊断：把真实 DLL 源码包含进来，加载用户 ini，打印“游戏里真正生效的条目”
// 并模拟若干 (武器, fsm, lmt) 状态看命中哪些条目 —— 用真实代码验证 LMT=-1 是否是通配。
#include "..\WeaponSoundEnhance.cpp"
#include <cstdio>

static void DumpLmt(const plugin::Attack& e) {
    if (e.lmt.empty()) { std::printf("ANY"); return; }
    for (size_t i = 0; i < e.lmt.size(); ++i) std::printf("%s%d", i ? "," : "", e.lmt[i]);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: wse_dll_probe <ini>\n"); return 2; }
    plugin::gIniPath = strconv::ToWide(argv[1]);
    plugin::gDataDir = L"";
    plugin::LoadConfig();

    std::printf("gAttacks (真正生效) = %zu\n", plugin::gAttacks.size());
    for (const auto& e : plugin::gAttacks) {
        std::printf("  w=%-3d fsm=%-5d tgt=%-4d lmt=", e.weaponType, e.fsmId, e.fsmTarget);
        DumpLmt(e);
        std::printf("  group=[%s] defSounds=%zu name=%s\n", e.group.c_str(),
                    e.defPool.specs.size(), e.name.c_str());
    }

    const int samples[][3] = {
        {3, 11, 49265}, {3, 11, 49256}, {3, 11, 49999}, {3, 67, 49258},
        {3, 12, 11111}, {3, 90, 22222}, {0, 999, 49298},
    };
    for (const auto& s : samples) {
        std::printf("state w=%d fsm=%d lmt=%d ->", s[0], s[1], s[2]);
        int n = 0;
        for (const auto& e : plugin::gAttacks) {
            bool lmtOk = e.lmt.empty();
            if (!lmtOk) for (int x : e.lmt) if (x == s[2]) { lmtOk = true; break; }
            const bool m = (e.weaponType < 0 || e.weaponType == s[0]) &&
                           (e.fsmId < 0 || e.fsmId == s[1]) && lmtOk && e.HasSounds();
            if (m) { std::printf(" [%s]", e.name.c_str()); ++n; }
        }
        if (!n) std::printf(" (无)");
        std::printf("\n");
    }
    return 0;
}
