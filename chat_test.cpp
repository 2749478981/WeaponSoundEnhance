// 队伍喊话的两段文本逻辑的测试：
//   GUI 侧  ChatSet / ChatGet   —— 「颜色+文字」 <-> <STYL ...>...</STYL>
//   DLL 侧  ClampForChat        —— 128 字节缓冲区的截断
//
// 两边都是 #include 真源码，不是抄一份，免得和实际跑的版本走偏。
#include "config.h"          // GUI 侧
#include <cstdio>
#include <string>

// DLL 侧：整份 .cpp 拉进来，取 teamchat::ClampForChat
#include "WeaponSoundEnhance.cpp"

static int bad = 0;

static void EqStr(const char* what, const std::string& got, const std::string& want)
{
    const bool ok = (got == want);
    if (!ok) ++bad;
    printf("  %-4s %-34s got[%s] want[%s]\n", ok ? "OK" : "FAIL", what,
           got.c_str(), want.c_str());
}

static void EqInt(const char* what, int got, int want)
{
    const bool ok = (got == want);
    if (!ok) ++bad;
    printf("  %-4s %-34s got=%d want=%d\n", ok ? "OK" : "FAIL", what, got, want);
}

// 存进去再拿出来，应该和原文一模一样
static void Roundtrip(const char* what, const std::string& src,
                      int wantColor, bool wantRaw, const std::string& wantText)
{
    ChatLine cl;
    ChatSet(cl, src);
    const std::string back = ChatGet(cl);
    const bool ok = (back == src) && (cl.color == wantColor) &&
                    (cl.raw == wantRaw) && (std::string(cl.text) == wantText);
    if (!ok) ++bad;
    printf("  %-4s %-20s 色=%d raw=%d 文字[%s]\n", ok ? "OK" : "FAIL", what,
           cl.color, (int)cl.raw, cl.text);
    if (!ok) printf("        原文[%s]\n        回写[%s]\n", src.c_str(), back.c_str());
}

int main()
{
    printf("==== GUI 侧：颜色+文字 <-> STYL 标签 ====\n");
    Roundtrip("纯文字",     "打中了",                                    0, false, "打中了");
    Roundtrip("黄",         "<STYL MOJI_YELLOW_DEFAULT>成功</STYL>",     1, false, "成功");
    Roundtrip("红",         "<STYL MOJI_RED_DEFAULT>失败</STYL>",        2, false, "失败");
    Roundtrip("实际用的那句",
              "<STYL MOJI_YELLOW_DEFAULT>至尊太刀侠科目三成功！全体猎人收刀敬礼！</STYL>",
              1, false, "至尊太刀侠科目三成功！全体猎人收刀敬礼！");
    // 下面几种界面看不懂，必须原样留着，不能擅自改写用户手打的东西
    Roundtrip("表里没有的样式名", "<STYL MOJI_WHATEVER>x</STYL>",         0, true,  "");
    Roundtrip("两段不同颜色",
              "<STYL MOJI_RED_DEFAULT>a</STYL><STYL MOJI_YELLOW_DEFAULT>b</STYL>",
              0, true, "");
    Roundtrip("只有开标签",  "<STYL MOJI_RED_DEFAULT>没闭合",             0, true,  "");
    Roundtrip("标签在中间",  "前<STYL MOJI_RED_DEFAULT>中</STYL>",        0, true,  "");
    {
        ChatLine cl;
        ChatSet(cl, "");
        EqStr("空串还是空串", ChatGet(cl), "");
        EqInt("空串不算 raw", (int)cl.raw, 0);
    }
    {
        // 界面上选了颜色、打了字 -> 应该拼出标签
        ChatLine cl;
        ChatSet(cl, "顶吼成功");
        cl.color = 1;
        EqStr("选黄色后拼标签", ChatGet(cl),
              "<STYL MOJI_YELLOW_DEFAULT>顶吼成功</STYL>");
        cl.color = 0;
        EqStr("改回默认去标签", ChatGet(cl), "顶吼成功");
    }
    {
        // 文字清空 -> 不发（不能留一对空标签）
        ChatLine cl;
        ChatSet(cl, "<STYL MOJI_RED_DEFAULT>x</STYL>");
        cl.text[0] = 0;
        EqStr("文字清空就不发", ChatGet(cl), "");
    }

    printf("\n==== DLL 侧：128 字节缓冲区的截断 ====\n");
    {
        using plugin::teamchat::ClampForChat;
        EqStr("没超长就别动", ClampForChat("短句", 127), "短句");

        // 汉字不能被劈成半个：截到 7 字节时只能留 2 个汉字（6 字节）
        EqStr("按字符边界退", ClampForChat("一二三四", 7), "一二");
        EqInt("退完是合法 UTF-8", (int)ClampForChat("一二三四", 7).size(), 6);

        // 带颜色标签的：闭合标签必须补回来，否则整句会连标签一起显示出来
        const std::string styled =
            "<STYL MOJI_YELLOW_DEFAULT>至尊太刀侠科目三成功！全体猎人收刀敬礼！</STYL>";
        const std::string cut = ClampForChat(styled, 60);
        const bool closed = cut.size() >= 7 &&
                            cut.compare(cut.size() - 7, 7, "</STYL>") == 0;
        if (!closed) ++bad;
        printf("  %-4s %-34s [%s]\n", closed ? "OK" : "FAIL", "截断后仍有闭合标签", cut.c_str());
        EqInt("截断后不超过上限", (int)(cut.size() <= 60), 1);

        // 实际那句 93 字节，127 放得下，不该被动
        EqInt("实际那句原样放得下", (int)(ClampForChat(styled, 127) == styled), 1);
        printf("       （实际那句 %d 字节）\n", (int)styled.size());
    }

    printf("\n%s\n", bad ? "有失败项" : "全部通过");
    return bad ? 1 : 0;
}
