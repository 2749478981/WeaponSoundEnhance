// 无头往返测试：用 GUI 的 LoadConfig/SaveConfig 读一遍真实 ini 再写出来，
// 检查判定条目（Check* / Sound:<表达式> / SoundEnd:）有没有被丢掉或写坏。
#include "config.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { printf("用法: cfg_roundtrip <in.ini> <out.ini>\n"); return 2; }
    Config cfg;
    if (!LoadConfig(argv[1], cfg)) { printf("读取失败\n"); return 1; }

    printf("读到 %d 条条目\n\n", (int)cfg.entries.size());
    int judged = 0, condTotal = 0;
    for (const auto& e : cfg.entries) {
        printf("  [%s] 武器=%d LMT=", e.name.c_str(), e.weaponType);
        for (size_t i = 0; i < e.lmt.size(); ++i) printf("%s%d", i ? "," : "", e.lmt[i]);
        printf("\n");
        if (e.checkTimeoutMs > 0) {
            ++judged;
            printf("      判定: delay=%d timeout=%d offset=%d endOnAction=%d\n",
                   e.checkDelayMs, e.checkTimeoutMs, e.checkOffsetMs, (int)e.endOnAction);
            for (const auto& c : e.conds) {
                ++condTotal;
                printf("      %-9s [%s] -> %d 条音效",
                       c.atEnd ? "SoundEnd:" : "Sound:", c.expr.c_str(), (int)c.pool.specs.size());
                for (const auto& sp : c.pool.specs) printf("  %s", sp.path.c_str());
                printf("\n");
            }
        }
        printf("      默认池 %d 条\n", (int)e.def.specs.size());
    }
    printf("\n带判定的条目 %d 条，条件共 %d 条\n", judged, condTotal);

    if (!SaveConfig(argv[2], cfg)) { printf("写出失败\n"); return 1; }

    // 再读一遍写出来的文件，比对是否一致
    Config cfg2;
    if (!LoadConfig(argv[2], cfg2)) { printf("回读失败\n"); return 1; }
    int bad = 0;
    if (cfg2.entries.size() != cfg.entries.size()) {
        printf("!! 条目数变了: %d -> %d\n", (int)cfg.entries.size(), (int)cfg2.entries.size());
        ++bad;
    } else {
        for (size_t i = 0; i < cfg.entries.size(); ++i) {
            const SoundEntry& a = cfg.entries[i];
            const SoundEntry& b = cfg2.entries[i];
            if (a.checkDelayMs != b.checkDelayMs || a.checkTimeoutMs != b.checkTimeoutMs ||
                a.checkOffsetMs != b.checkOffsetMs || a.endOnAction != b.endOnAction) {
                printf("!! [%s] 判定参数不一致\n", a.name.c_str()); ++bad;
            }
            if (a.conds.size() != b.conds.size()) {
                printf("!! [%s] 条件数 %d -> %d\n", a.name.c_str(),
                       (int)a.conds.size(), (int)b.conds.size()); ++bad;
                continue;
            }
            for (size_t k = 0; k < a.conds.size(); ++k) {
                if (a.conds[k].expr != b.conds[k].expr ||
                    a.conds[k].chat != b.conds[k].chat ||
                    a.conds[k].atEnd != b.conds[k].atEnd ||
                    a.conds[k].pool.specs.size() != b.conds[k].pool.specs.size()) {
                    printf("!! [%s] 条件 %d 不一致: [%s] vs [%s]\n", a.name.c_str(), (int)k,
                           a.conds[k].expr.c_str(), b.conds[k].expr.c_str());
                    ++bad;
                }
            }
            if (a.defChat != b.defChat) {
                printf("!! [%s] Chat= 不一致\n", a.name.c_str()); ++bad;
            }
            if (a.def.specs.size() != b.def.specs.size()) {
                printf("!! [%s] 默认池 %d -> %d\n", a.name.c_str(),
                       (int)a.def.specs.size(), (int)b.def.specs.size()); ++bad;
            }
        }
    }
    printf("\n%s（%d 处不一致）\n", bad ? "往返失败" : "往返一致", bad);
    return bad ? 1 : 0;
}
