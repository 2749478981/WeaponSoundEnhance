// ===========================================================================
//  SonarBanks.h —— 解析游戏目录下 .nbnk(Wwise SoundBank v120)，建反查表
//
//  nbnk 结构（从 ref/11弓箭/*.nbnk.txt 的实际转储确认）：
//
//    BKHD  段：银行头（bank id / 版本 / language / project）
//    DIDX  段：Media Index —— 每 12 字节一条：
//                u32 id        （= media_id，也就是 wem 的 id）
//                u32 uOffset   （在 DATA 段里的偏移，相对 DATA 起点）
//                u32 uSize
//    DATA  段：wem 原始数据（DIDX 里的 offset 相对这里）
//    HIRC  段：层级对象表，头部 u32 个数，然后每个对象：
//                u8  eHircType
//                u32 dwSectionSize     （含 5 字节头 = type+size；步进 pos+5+sec）
//                u32 ulID              （对象自己的 ID）
//                ...各类型自己的负载     （**从 pos+9 起**，别漏了 ulID）
//
//  ⚠ 步进用 pos+5+sec，但 payload 起点是 pos+9 —— 两者差一个 ulID(4 字节)。
//    混淆会让所有对象的负载错位 4 字节，反查链彻底失效。
//
//  我们只关心三种 HIRC 类型（足够把 eventID 连到 media_id）：
//    0x04 Event  ：u32 actionCount, u32 actionIDs[actionCount]
//    0x03 Action ：u16 actionType, u32 idExt  → 目标对象 ID（Sound/Container）
//    0x02 Sound  ：先 u32 ulPluginID，再 u16 type, u16 company,
//                  u8 StreamType, u32 sourceID  → sourceID 就是 media_id
//    0x05 Random/Sequence Container：u32 子节点数 + 子节点 ID 数组（继续往下走）
//    0x06 Switch Container：同上（另有 switch 组，跳过）
//    0x07 Actor-Mixer：有子节点
//
//  反查链：eventID → actionIDs → idExt → (Sound.sourceID 或 Container.children → ...)
//          → media_id 集合
//
//  同时尝试读 Event 的 **名字**：MHW 的 nbnk 不带 STID/名字段（只有 ID），
//  所以 event 名基本拿不到，名字来自 mediaids.json 之类的外部映射或 bank 名。
// ===========================================================================
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace sonaraudio {

// 一个 Wwise 对象在 HIRC 里的种类
enum class HircType : uint8_t {
    Sound = 0x02, Action = 0x03, Event = 0x04, RandomSeq = 0x05,
    Switch = 0x06, ActorMixer = 0x07, Attenuation = 0x0E, Unknown = 0xFF,
};

// 解析出的单个银行
struct BankInfo {
    std::wstring path;
    std::string  name;                       // 文件名（不含扩展）
    uint32_t     bank_id = 0;
    // media_id → (wem 名, 字节大小)
    std::unordered_map<uint32_t, std::string> media_names;
    std::unordered_map<uint32_t, uint32_t>    media_sizes;   // media_id → wem 字节大小
    // HIRC 对象： objID → 类型 + 负载解码结果
    struct Obj {
        HircType type = HircType::Unknown;
        uint32_t action_type = 0;            // Action 专用（0x0403 = Play）
        uint32_t target_id   = 0;            // Action.idExt / Sound.sourceID
        std::vector<uint32_t> children;      // Event.actionIDs / Container.children
        size_t   body_off = 0;               // 对象负载在银行文件里的偏移（内部用）
        uint32_t body_size = 0;              // dwSectionSize（内部用）
    };
    std::unordered_map<uint32_t, Obj> objects;
};

// 全局反查表（所有银行合并）
class BankDatabase {
public:
    // 扫描 root 下（递归，depth 限制）所有 *.nbnk，解析建表。
    // 返回成功解析的银行数；日志追加到 log。
    int ScanDirectory(const std::wstring& root, std::string& log, int max_files = 4000);

    // eventID → 一组 media_id（可能多层容器展开）
    const std::vector<uint32_t>* MediaForEvent(uint32_t event_id) const;

    // media_id → "bank名/xxx.wem"
    std::string MediaName(uint32_t media_id) const;

    // media_id → wem 字节大小（DIDX 的 uSize 字段）。0 = 未知。
    uint32_t MediaSize(uint32_t media_id) const;

    // eventID → 所在 bank 名
    std::string BankNameForEvent(uint32_t event_id) const;

    // eventID → 所在 bank 的完整磁盘路径（诊断用，方便核对到底扫到了哪个文件）
    std::string BankPathForEvent(uint32_t event_id) const;

    // 本次扫描的统计（诊断：扫了哪些文件、成功几个）
    size_t files_seen() const { return files_seen_; }
    size_t files_read() const { return files_read_; }
    const std::vector<std::string>& roots_seen() const { return roots_seen_; }
    size_t parsed_hirc() const { return parsed_hirc_; }
    size_t parsed_data() const { return parsed_data_; }

    size_t bank_count() const { return banks_.size(); }
    size_t event_count() const { return event_media_.size(); }
    size_t media_count() const { return media_name_.size(); }

    // 允许外部补一条 media 注释（来自 mediaids.json 之类）
    void AddMediaAlias(uint32_t media_id, const std::string& alias);

private:
    void IndexBank(BankInfo& b);
    void ExpandTarget(const BankInfo& b, uint32_t obj_id, std::vector<uint32_t>& out_media,
                      std::unordered_set<uint32_t>& visited, int depth) const;

    std::vector<BankInfo> banks_;
    std::unordered_map<uint32_t, std::vector<uint32_t>> event_media_;
    std::unordered_map<uint32_t, std::string> media_name_;
    std::unordered_map<uint32_t, uint32_t> media_size_;      // media_id → wem 字节大小
    std::unordered_map<uint32_t, std::string> event_bank_;
    std::unordered_map<uint32_t, std::string> media_alias_;
    std::unordered_map<uint32_t, std::string> event_path_;   // eventID → bank 磁盘路径

    size_t files_seen_ = 0;   // 遇到的候选文件数（*.nbnk/*.bnk）
    size_t files_read_ = 0;   // 成功读取并尝试解析的
    size_t parsed_hirc_ = 0;  // 带 HIRC 的定义 bank（事件反查表的来源）
    size_t parsed_data_ = 0;  // 纯 DIDX+DATA 的数据 bank（只有 media 名）
    std::vector<std::string> roots_seen_;  // 扫描过的根目录（诊断）
};

// 读整个文件（返回 false 表示读不到）
bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out);

// 宽字符串 → UTF-8 窄字符串
std::string Narrow(const std::wstring& w);

}  // namespace sonaraudio
