#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
namespace cond {

enum VarId { V_DMG = 0, V_AURA, V_DAURA, V_CHARGE, V_DCHARGE,
             V_LMT, V_FSM, V_FSMTGT, V_MS, V_COUNT };

// 注意：查找时按长度从长到短匹配，否则 "dAura" 会被 "d" 之类的前缀吃掉
const char* const kVarNames[V_COUNT] = {
    "dmg", "aura", "dAura", "charge", "dCharge", "lmt", "fsm", "fsmTarget", "ms"
};

// 给 GUI / 文档用的中文名，顺序与 VarId 一致
const char* const kVarLabels[V_COUNT] = {
    "打出伤害", "练气/刃级", "练气变化", "蓄力等级", "蓄力变化",
    "动作ID", "FSM", "FSM层", "已过毫秒"
};

enum CmpOp { OP_GT = 0, OP_GE, OP_LT, OP_LE, OP_EQ, OP_NE, OP_COUNT };
const char* const kOpNames[OP_COUNT] = { ">", ">=", "<", "<=", "==", "!=" };

struct Vars { int v[V_COUNT]; };

struct Term {
    int  var = V_DMG;
    int  op  = OP_GT;
    int  rhs = 0;
    bool orBefore = false;   // 这一项之前的连接符是 |（false 表示 &）
};

struct Expr {
    std::vector<Term> terms;
    bool empty() const { return terms.empty(); }
};

inline std::string Trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

// 含比较符就当成表达式，否则交回旧的刃级 tag 解析（none/white/yellow/red/0..3）
inline bool LooksLikeExpr(const std::string& s)
{
    return s.find_first_of("<>=!&|") != std::string::npos;
}

inline bool ParseOne(const std::string& raw, Term& t)
{
    const std::string x = Trim(raw);
    if (x.empty()) return false;

    // 先定位比较符
    std::size_t opPos = x.find_first_of("<>=!");
    if (opPos == std::string::npos || opPos == 0) return false;

    std::string varName = Trim(x.substr(0, opPos));
    int varId = -1;
    for (int i = 0; i < V_COUNT; ++i) {
        if (varName == kVarNames[i]) { varId = i; break; }
    }
    if (varId < 0) return false;

    // 两字符的比较符优先
    int op = -1; std::size_t opLen = 0;
    const std::string rest = x.substr(opPos);
    for (int i = 0; i < OP_COUNT; ++i) {
        const std::size_t n = std::strlen(kOpNames[i]);
        if (n == 2 && rest.compare(0, 2, kOpNames[i]) == 0) { op = i; opLen = 2; break; }
    }
    if (op < 0) {
        for (int i = 0; i < OP_COUNT; ++i) {
            const std::size_t n = std::strlen(kOpNames[i]);
            if (n == 1 && rest.compare(0, 1, kOpNames[i]) == 0) { op = i; opLen = 1; break; }
        }
    }
    if (op < 0) return false;

    const std::string rhs = Trim(rest.substr(opLen));
    if (rhs.empty()) return false;
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        if (i == 0 && (rhs[i] == '-' || rhs[i] == '+')) continue;
        if (rhs[i] < '0' || rhs[i] > '9') return false;
    }

    t.var = varId; t.op = op; t.rhs = std::atoi(rhs.c_str());
    return true;
}

// 解析失败返回 false（调用方会退回旧解析并记一条日志）
inline bool Parse(const std::string& src, Expr& out)
{
    out.terms.clear();
    std::string cur;
    bool nextIsOr = false;
    bool pendingOr = false;

    for (std::size_t i = 0; i <= src.size(); ++i) {
        const char c = (i < src.size()) ? src[i] : '\0';
        if (c == '&' || c == '|' || c == '\0') {
            Term t;
            if (!ParseOne(cur, t)) return false;
            t.orBefore = nextIsOr;
            out.terms.push_back(t);
            cur.clear();
            nextIsOr = (c == '|');
            pendingOr = nextIsOr;
            (void)pendingOr;
        } else {
            cur.push_back(c);
        }
    }
    return !out.terms.empty();
}

// 求值：若干 AND 组再 OR 起来
inline bool Eval(const Expr& e, const Vars& vars)
{
    if (e.terms.empty()) return false;
    bool result = false;   // 已经闭合的 OR 结果
    bool group  = true;    // 当前 AND 组
    for (std::size_t i = 0; i < e.terms.size(); ++i) {
        const Term& t = e.terms[i];
        if (i > 0 && t.orBefore) { result = result || group; group = true; }
        const int lhs = vars.v[t.var];
        bool ok = false;
        switch (t.op) {
            case OP_GT: ok = lhs >  t.rhs; break;
            case OP_GE: ok = lhs >= t.rhs; break;
            case OP_LT: ok = lhs <  t.rhs; break;
            case OP_LE: ok = lhs <= t.rhs; break;
            case OP_EQ: ok = lhs == t.rhs; break;
            case OP_NE: ok = lhs != t.rhs; break;
            default: break;
        }
        group = group && ok;
    }
    return result || group;
}

} // namespace cond
static int fails = 0;
static void chk(const char* expr, bool wantParse, int dmg, int dAura, int aura, bool want)
{
    cond::Expr e;
    bool ok = cond::Parse(expr, e);
    if (ok != wantParse) {
        printf("  FAIL parse  %-28s  parse=%d want=%d\n", expr, (int)ok, (int)wantParse);
        ++fails; return;
    }
    if (!ok) { printf("  ok  (拒绝) %-28s\n", expr); return; }
    cond::Vars v{};
    v.v[cond::V_DMG] = dmg; v.v[cond::V_DAURA] = dAura; v.v[cond::V_AURA] = aura;
    bool r = cond::Eval(e, v);
    if (r != want) {
        printf("  FAIL eval   %-28s  dmg=%d dAura=%d aura=%d -> %d want %d\n",
               expr, dmg, dAura, aura, (int)r, (int)want);
        ++fails;
    } else {
        printf("  ok          %-28s  dmg=%-5d dAura=%-3d aura=%d -> %s\n",
               expr, dmg, dAura, aura, r ? "真" : "假");
    }
}
int main()
{
    printf("-- 基本比较 --\n");
    chk("dmg>0",   true, 100, 0, 3, true);
    chk("dmg>0",   true,   0, 0, 3, false);
    chk("dmg>=0",  true,   0, 0, 3, true);
    chk("dmg==0",  true,   0, 0, 3, true);
    chk("dmg!=0",  true,   0, 0, 3, false);
    chk("dAura<0", true,   0,-1, 2, true);
    chk("dAura<0", true,   0, 0, 3, false);

    printf("-- AND --\n");
    chk("dmg>0 & dAura>=0", true, 500,  0, 3, true);   // 大居成功
    chk("dmg>0 & dAura>=0", true, 500, -1, 2, false);  // 打出伤害但掉刃
    chk("dmg>0 & dAura>=0", true,   0,  0, 3, false);  // 没伤害

    printf("-- OR --\n");
    chk("dAura<0 | dmg==0", true,   0,  0, 3, true);
    chk("dAura<0 | dmg==0", true, 500, -1, 2, true);
    chk("dAura<0 | dmg==0", true, 500,  0, 3, false);

    printf("-- 优先级：& 高于 |，等价于 (a&b)|(c&d) --\n");
    chk("dmg>800 & aura==3 | dmg>0 & aura==1", true, 900, 0, 3, true);
    chk("dmg>800 & aura==3 | dmg>0 & aura==1", true,  50, 0, 1, true);
    chk("dmg>800 & aura==3 | dmg>0 & aura==1", true, 900, 0, 1, true);
    chk("dmg>800 & aura==3 | dmg>0 & aura==1", true,   0, 0, 3, false);

    printf("-- 空格容错 --\n");
    chk("  dmg > 0   &   dAura >= 0  ", true, 10, 0, 3, true);

    printf("-- 应当被拒绝（回退到旧的刃级 tag）--\n");
    chk("white",    false, 0,0,0,false);
    chk("red",      false, 0,0,0,false);
    chk("dmg>",     false, 0,0,0,false);
    chk("nosuch>0", false, 0,0,0,false);
    chk(">0",       false, 0,0,0,false);
    chk("dmg>abc",  false, 0,0,0,false);

    printf("\n%s  (%d 处失败)\n", fails ? "有失败" : "全部通过", fails);
    return fails != 0;
}
