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
    Eq("黑龙 33047 (2转3第3段)", LookupMonsterAction("黑龙",33047), "2转3 相变 · 第3段 (5.6s)");
    Eq("黑龙 33065 (劫火)",      LookupMonsterAction("黑龙",33065), "P3 飞天劫火");
    Eq("不指定怪物也能查",        LookupMonsterAction("",33024),      "1转2 相变 · 第3段 (5.9s)");
    Eq("查不到返回空",            LookupMonsterAction("黑龙",99999),  "");
    Eq("怪物 ID 不能串到武器查询", LookupFsmName(3,-1,33047),          "");
    Eq("武器查询仍然正常",         LookupFsmName(3,-1,49326),          "登龙 (气刃兜割) —— 三个刃色共用");
    Eq("武器 ID 不会串到怪物查询", LookupMonsterAction("黑龙",49326),  "");
    printf("\n%s\n", bad?"有失败项":"全部通过");
    return bad?1:0;
}
