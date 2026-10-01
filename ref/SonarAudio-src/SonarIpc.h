// ===========================================================================
//  SonarIpc.h —— 共享内存环形缓冲写入端 + 轻量日志
//
//  设计要点：
//   * 单写多读：只有本插件（在 hook 回调线程里）写，sonar 只读。
//   * 写入顺序：先填事件体 → 内存屏障 → 再递增 write_seq（Release）。
//     读端看到 seq 更新后，事件体一定已经可见。
//   * 覆盖语义：槽位 = seq % capacity，旧事件被自然覆盖，无需清理。
//   * hook 回调里**绝不能做重活**（会拖慢游戏音频线程）：
//     写共享内存是纯 memcpy，几十纳秒；日志按需降频/限速。
// ===========================================================================
#pragma once
#include "SonarAudioIpc.h"
#include <windows.h>
#include <string>
#include <atomic>

namespace sonaraudio {

class IpcWriter {
public:
    bool Create(std::string& log);
    void Destroy();

    // 推一条事件（线程安全、无锁、非阻塞）。
    // 返回分配到的 seq（0 表示未就绪）。
    uint32_t Push(uint32_t event_id, uint32_t playing_id, uint32_t game_obj_id,
                  uint32_t media_id, uint32_t flags,
                  const char* event_name, const char* bank_name);

    void SetHookOk(bool ok) { if (hdr_) hdr_->hook_ok = ok ? 1 : 0; }

    bool ready() const { return hdr_ != nullptr; }
    uint32_t total() const { return hdr_ ? hdr_->events_total : 0; }

private:
    static void CopyName(char* dst, const char* src, uint32_t cap);

    HANDLE hMap_ = nullptr;
    SonarAudioHeader* hdr_ = nullptr;
    std::atomic<uint32_t> seq_{0};
};

// ---- 日志（追加写、带时间戳；内部限速防止刷爆磁盘）--------------------------
void LogInit(const std::wstring& dir);
void LogLine(const char* fmt, ...);

}  // namespace sonaraudio
