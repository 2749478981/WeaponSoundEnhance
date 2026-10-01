// ===========================================================================
//  SonarAudioIpc.h —— SonarAudio 插件与 Sonar 插件之间的共享内存契约
//
//  这个头文件是**唯一的接口定义**，两个插件都必须包含它。
//  改了这里的结构体/常量，两边都要重新编译。
//
//  共享内存布局（一块命名文件映射，名字见 kShmName）：
//    [SonarAudioSharedHeader]
//    [SonarAudioEvent  x kRingCapacity]   （环形缓冲，按 write_seq 递增覆盖）
//
//  读端(sonar)协议：
//    1. 打开 kShmName（FILE_MAP_READ），校验 magic/version。
//    2. 记 last_seq = 0（或 header->write_seq 表示"只看新的"）。
//    3. 每隔几毫秒：读 header->write_seq，若 > last_seq，
//       从 (last_seq % cap) 起逐条读事件，比较每条 seq 是否 > last_seq。
//       **每条事件的 seq 单调递增**，用 seq 判断"这条是不是新出现的"，
//       不要靠时间戳（分辨率不够、会撞）。
//    4. 读到的 event_id / media_first 用下面的哈希表或自带表反查名字。
//
//  写端(SonarAudio)协议：
//    - 只递增 write_seq（Release 语义），事件体先写、seq 后写。
//    - 从不回退 write_seq，溢出靠 % kRingCapacity 覆盖最旧槽。
// ===========================================================================
#pragma once
#include <cstdint>

namespace sonaraudio {

// ---- 共享内存标识 -----------------------------------------------------------
// 注意：Local\ 前缀 = 当前会话可见（游戏和插件同会话，够用且不需要管理员）
inline constexpr const wchar_t* kShmName    = L"Local\\SonarAudioEvents_v1";
inline constexpr uint32_t       kShmMagic   = 0x53414F31u;  // 'SAO1' SonarAudio v1
inline constexpr uint32_t       kShmVersion = 1;

// 环形缓冲容量。每条 64 字节，2048 条 ≈ 128KB，很便宜。
inline constexpr uint32_t       kRingCapacity = 2048;

// 名字字段长度上限（UTF-8，不含结尾 0）
// 96 是实测出来的：武器音效包带的**中文注释**（如
// "wp11_bow_epvsp_shell/12.ogg （龙之矢飞行音效）"）最长的 UTF-8 编码
// 达到 88 字节，48 会截断掉注释。96 能完整装下全部 4655 条映射。
inline constexpr uint32_t       kNameLen = 96;

// ---- 事件标志 ---------------------------------------------------------------
enum SonarAudioFlags : uint32_t {
    kFlagNone        = 0,
    kFlagFromPostEvent = 1u << 0,  // 来自 PostEvent hook（触发瞬间）
    kFlagFromPolling   = 1u << 1,  // 来自轮询补偿（hook 漏掉的）
    kFlagNameKnown     = 1u << 2,  // 下面的名字字段有效
    kFlagStop          = 1u << 3,  // 这是一条停止事件（StopPlayingID / StopAll）
};

// ---- 单条事件（定长 64 字节，务必保持 POD & 尺寸）---------------------------
#pragma pack(push, 8)
struct SonarAudioEvent {
    uint32_t seq;          // 全局单调递增序号（读端就靠它判断新旧）
    uint32_t event_id;     // Wwise Event ID（AK::SoundEngine::PostEvent 的第一个参数）
    uint32_t playing_id;   // Wwise Playing ID（PostEvent 返回值；停止事件里也有）
    uint32_t game_obj_id;  // Wwise Game Object ID（谁触发的，通常是玩家/怪物句柄）
    uint32_t media_id;     // 从 HIRC 反查到的第一个 media_id（0 = 未知）
    uint32_t flags;        // SonarAudioFlags 位或
    uint64_t tick_ms;      // GetTickCount64()，毫秒（跨事件求差值可做节流）
    char     event_name[kNameLen];  // UTF-8，Event 名（无则空串）
    char     bank_name[kNameLen];   // UTF-8，来源 nbnk 名（无则空串）
};
#pragma pack(pop)
// 定长布局：6×u32(24) + u64(8) + 2×char[kNameLen](96) = 128 字节
static_assert(sizeof(SonarAudioEvent) == 24 + 8 + 2 * kNameLen, "SonarAudioEvent layout changed");

// ---- 共享内存头部 -----------------------------------------------------------
struct SonarAudioHeader {
    uint32_t magic;        // kShmMagic，校验用
    uint32_t version;      // kShmVersion
    uint32_t capacity;     // kRingCapacity
    uint32_t write_seq;    // 已写入的事件总数（单调递增；读端轮询它）
    uint32_t shm_size;     // 整个映射字节数（诊断用）
    uint32_t hook_ok;      // 1 = PostEvent hook 装上了
    uint32_t events_total; // 累计捕获事件数（含被覆盖的）
    uint32_t reserved;
};

// 映射总大小
inline constexpr uint32_t ShmSize() {
    return (uint32_t)(sizeof(SonarAudioHeader) + sizeof(SonarAudioEvent) * kRingCapacity);
}

// 便捷：事件数组起点（在 header 之后）
inline SonarAudioEvent* RingStart(SonarAudioHeader* h) {
    return reinterpret_cast<SonarAudioEvent*>(h + 1);
}

}  // namespace sonaraudio
