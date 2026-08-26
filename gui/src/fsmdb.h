#pragma once
#include <string>
#include <vector>

struct FsmDbEntry {
    int weapon = -1;      // 0..13，-1=通用
    int fsm = -1;
    int lmt = -1;
    std::string name;
};

// 内置 + 外部 fsm_db.csv（exe 同目录，可选）合并后的知识库
const std::vector<FsmDbEntry>& GetFsmDb();
// 关键字搜索：匹配中文名 / fsm 数字 / lmt 数字。weaponFilter: -1=全部
std::vector<FsmDbEntry> SearchFsmDb(const std::string& query, int weaponFilter);
// 反查名称，未知返回空串
std::string LookupFsmName(int weapon, int fsm, int lmt);
