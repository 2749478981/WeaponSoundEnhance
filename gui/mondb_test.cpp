// 怪物动作知识库的查询测试。
//
// 段号在 2026-09-19 那次数据重跑后变过一次：原来只收了相变的尾巴几段，
// 后来把起手段也串进来了，所以同一个动作 ID 的段号往后挪了。期望值跟着改过。
#include "fsmdb.h"
#include <cstdio>
static int bad=0;
static void Eq(const char* what, const std::string& got, const char* want){
    bool ok = (got == want); if(!ok) ++bad;
    printf("  %s %-34s -> [%s]%s\n", ok?"OK ":"FAIL", what, got.c_str(),
           ok?"":(std::string("  期望 [")+want+"]").c_str());
}
int main(){
    printf("已知怪物: ");
    for (const auto& m : ListKnownMonsters()) printf("%s ", m.c_str());
    printf("\n黑龙动作数: %d\n\n", (int)MonsterActions("黑龙").size());
    Eq("黑龙 33047 (2转3)",       LookupMonsterAction("黑龙",33047), "2转3 相变 · 第7段 (5.6s)");
    Eq("黑龙 33065 (劫火起手A)",  LookupMonsterAction("黑龙",33065), "P3 飞天劫火 · 起手A 第4段 (1.8s)");
    // 33069~33077 以前被当成「P3 专属动作(待认)」，实际是劫火的中后段
    Eq("黑龙 33071 (劫火中段)",   LookupMonsterAction("黑龙",33071), "P3 飞天劫火 · 第10段 (3.4s)");
    // 大咬的两个方向都要查得到，否则 ini 里漏填就没人发现
    Eq("黑龙 28787 (大咬方向一)", LookupMonsterAction("黑龙",28787), "大咬（方向一，用户确认，3.7s）");
    Eq("黑龙 28788 (大咬方向二)", LookupMonsterAction("黑龙",28788), "大咬（方向二，推定，3.7s）");
    Eq("不指定怪物也能查",        LookupMonsterAction("",33024),      "1转2 相变 · 第9段 (5.9s)");
    Eq("查不到返回空",            LookupMonsterAction("黑龙",99999),  "");
    Eq("怪物 ID 不能串到武器查询", LookupFsmName(3,-1,33047),          "");
    Eq("武器查询仍然正常",         LookupFsmName(3,-1,49326),          "登龙 (气刃兜割) —— 三个刃色共用");
    Eq("武器 ID 不会串到怪物查询", LookupMonsterAction("黑龙",49326),  "");
    printf("\n%s\n", bad?"有失败项":"全部通过");
    return bad?1:0;
}
