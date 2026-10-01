// ===========================================================================
//  SonarAudioReader.h —— 给 **Sonar 插件** 用的现成读取端（header-only）
//
//  用法（在 Sonar 的轮询 tick 里）：
//
//      static SonarAudioReader reader;          // 成员变量，进程存活期一直用
//      reader.Attach();                          // 每帧调也无害；未连上会重试
//
//      SonarAudioEvent ev;
//      while (reader.Next(ev)) {                 // 拉走所有新事件
//          // ev.event_id / ev.media_id / ev.event_name / ev.bank_name ...
//          // 在这里做你的 sonar 玩法
//      }
//
//  契约：SonarAudio.dll 是生产者，本 reader 是消费者。
//  只读共享内存，不需要任何 IPC 通道、管道或窗口消息。
//  两边都要包含 SonarAudioIpc.h（同一份结构体定义）。
// ===========================================================================
#pragma once
#include <windows.h>
#include <atomic>
#include "SonarAudioIpc.h"

namespace sonaraudio {

class SonarAudioReader {
public:
    ~SonarAudioReader() { Detach(); }

    // 尝试连接（幂等）。返回是否已连上。进程内可每帧调用。
    bool Attach() {
        if (hdr_) {
            // 共享内存可能被重建（游戏重启），magic 变了就重连
            if (hdr_->magic == kShmMagic && hdr_->version == kShmVersion) return true;
            Detach();
        }
        HANDLE h = ::OpenFileMappingW(FILE_MAP_READ, FALSE, kShmName);
        if (!h) return false;
        void* p = ::MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
        if (!p) { ::CloseHandle(h); return false; }
        hMap_ = h;
        hdr_  = reinterpret_cast<const SonarAudioHeader*>(p);
        if (hdr_->magic != kShmMagic || hdr_->version != kShmVersion) { Detach(); return false; }
        // 从"当前时间点"开始消费，避免把历史刷屏；想全量就把它清零。
        lastSeq_ = hdr_->write_seq;
        return true;
    }

    void Detach() {
        if (hdr_) { ::UnmapViewOfFile((LPCVOID)hdr_); hdr_ = nullptr; }
        if (hMap_) { ::CloseHandle(hMap_); hMap_ = nullptr; }
    }

    bool connected() const { return hdr_ != nullptr; }
    bool hook_ok() const { return hdr_ && hdr_->hook_ok; }
    uint32_t total_captured() const { return hdr_ ? hdr_->events_total : 0; }

    // 从"我上次读到的位置"开始，把还没消费的事件吐出来。
    // 返回 false = 暂时没有更多（或没连上）。
    bool Next(SonarAudioEvent& out) {
        if (!hdr_) return false;
        const uint32_t w = hdr_->write_seq;
        if (w == lastSeq_) return false;

        const uint32_t cap = hdr_->capacity ? hdr_->capacity : kRingCapacity;
        const SonarAudioEvent* ring = reinterpret_cast<const SonarAudioEvent*>(hdr_ + 1);

        // 如果落后超过一整圈，说明读端太慢、旧数据已被覆盖 —— 跳到可读的最早位置
        if (w - lastSeq_ > cap) lastSeq_ = w - cap;

        const uint32_t idx = lastSeq_ % cap;
        const SonarAudioEvent& e = ring[idx];

        // 双保险：槽位的 seq 必须和我们期望的一致（否则被覆盖了，直接跳）
        if (e.seq != lastSeq_ + 1) { lastSeq_ = w; return false; }

        out = e;
        ++lastSeq_;
        std::atomic_thread_fence(std::memory_order_acquire);
        return true;
    }

    // 想重放全部历史（调试用）
    void RewindToOldest() {
        if (!hdr_) return;
        const uint32_t w = hdr_->write_seq;
        const uint32_t cap = hdr_->capacity ? hdr_->capacity : kRingCapacity;
        lastSeq_ = (w > cap) ? (w - cap) : 0;
    }

private:
    HANDLE hMap_ = nullptr;
    const SonarAudioHeader* hdr_ = nullptr;
    uint32_t lastSeq_ = 0;
};

}  // namespace sonaraudio
