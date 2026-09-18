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

// ---- 共享 ID 库（fsm_db.csv）----
// 数据目录（ini 所在目录）；不设置时退回 exe 同目录
void SetFsmDbDir(const std::string& dir);
// 随包/下载的基础库：<dir>\fsm_db.csv（结构固定，更新只追加行）
std::string FsmDbPath();
// 用户自己的实测库：<dir>\fsm_db_user.csv（永不被更新覆盖，优先级高于基础库）
std::string FsmDbUserPath();
// 提交用临时文件：<dir>\fsm_db_submission.csv
std::string FsmDbSubmissionPath();
// 把 entries 按 weapon,fsm,lmt,name 写成 CSV（自动跳过重复键、清理逗号）
bool SaveFsmDbCsv(const std::string& path, const std::vector<FsmDbEntry>& entries);
// 把 src 里的条目合并进 dstPath（按 weapon/fsm/lmt 去重，追加写入），返回新增条数
int MergeFsmDbEntries(const std::vector<FsmDbEntry>& src, const std::string& dstPath);
// 把 srcPath 的条目合并进 dstPath（按 weapon/fsm/lmt 去重，追加写入），返回新增条数
int MergeFsmDbCsv(const std::string& srcPath, const std::string& dstPath);
// 让 GetFsmDb() 丢弃缓存、重新读盘（导入/下载后调用）
void ReloadFsmDb();
// 读取单个 CSV 文件（不合并内置库），供导出/预览用
std::vector<FsmDbEntry> LoadFsmDbCsv(const std::string& path);
// 内置兜底数据（仅当基础库文件缺失/为空时用来播种）
std::vector<FsmDbEntry> BuiltinFsmDb();
