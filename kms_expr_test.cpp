#include "WeaponSoundEnhance.cpp"
#include <cstdio>
static int bad=0;
static void T(const char* expr,int dmg,int lmt,bool want){
    plugin::cond::Expr e;
    if(!plugin::cond::Parse(expr,e)){ printf("  FAIL 解析不了: %s\n",expr); ++bad; return; }
    plugin::cond::Vars v{};
    v.v[plugin::cond::V_DMG]=dmg; v.v[plugin::cond::V_LMT]=lmt;
    bool got=plugin::cond::Eval(e,v);
    if(got!=want){ printf("  FAIL %-24s dmg=%-4d lmt=%-6d -> %d (want %d)\n",expr,dmg,lmt,got,want); ++bad; }
    else printf("  OK   %-24s dmg=%-4d lmt=%-6d -> %d\n",expr,dmg,lmt,got);
}
int main(){
    const char* E="dmg>0 & lmt==49326";
    printf("科目三条件: %s\n", E);
    T(E,267,49326,true);    // 成功那次
    T(E,94, 49161,false);   // 误报那次：平A
    T(E,0,  49326,false);   // 登龙但没伤害
    T(E,261,49326,true);    // 后面那次真登龙
    T(E,94, 49326,true);    // 登龙打中但伤害低，仍算成功（不按伤害大小卡）
    printf("\n%s\n", bad?"有失败项":"全部通过");
    return bad?1:0;
}
