// 临时诊断：用 GUI 的 config.cpp 解析用户 ini，输出每条目的
// 武器/组合/是否激活/LMT/FSMId，看编辑的条目到底是不是“当前组合”。
#include "config.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: wse_cfg_probe <ini>\n"); return 2; }
    Config cfg;
    if (!LoadConfig(argv[1], cfg)) { std::printf("load failed\n"); return 1; }

    std::printf("entries=%zu  active combos:\n", cfg.entries.size());
    for (const auto& kv : cfg.active)
        std::printf("   W%d -> [%s]\n", kv.first, kv.second.c_str());

    auto ActiveCombo = [&](int w) {
        auto it = cfg.active.find(w);
        return (it != cfg.active.end()) ? it->second : std::string();
    };
    int activeCount = 0;
    for (size_t i = 0; i < cfg.entries.size(); ++i) {
        const SoundEntry& e = cfg.entries[i];
        const bool act = (e.weaponType < 0) ? e.combo.empty() : (e.combo == ActiveCombo(e.weaponType));
        if (act) ++activeCount;
        std::string lm;
        if (e.lmt.empty()) lm = "-1(wildcard)";
        for (size_t k = 0; k < e.lmt.size(); ++k) {
            if (k) lm += ",";
            lm += std::to_string(e.lmt[k]);
        }
        std::printf("%2zu  w=%-3d combo=%-10s active=%d  lmt=%-24s fsm=%-5d name=%s\n",
                    i + 1, e.weaponType, e.combo.empty() ? "(default)" : e.combo.c_str(),
                    (int)act, lm.c_str(), e.fsmId, e.name.c_str());
    }
    std::printf("active entries = %d / %zu\n", activeCount, cfg.entries.size());
    return 0;
}
