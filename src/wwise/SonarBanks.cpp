// ===========================================================================
//  SonarBanks.cpp —— nbnk(Wwise SoundBank v120) 解析实现
//
//  结构（已用 ref/11弓箭 的实测转储 + 二进制逐一核对，见 tools/verify_nbnk2.py）：
//
//   段表：tag(4) + size(4) + payload(size)，**顺序遍历**。
//     ⚠️ 不能按字节扫描找 tag —— DATA 段内部会出现与 'HIRC' 相同的字节序列，
//        会假命中（本实现踩过这个坑）。
//   段：BKHD(头) / DIDX(media 索引) / DATA(wem 原始数据) / HIRC(层级对象)
//
//   DIDX 每 12 字节： u32 mediaID, u32 offset(相对 DATA), u32 size
//
//   HIRC：u32 count，随后每个对象：
//     u8  eHircType
//     u32 dwSectionSize     ← **含这 5 字节头**！下一对象 = pos + 5 + sec
//     u32 ulID              ← 对象自己的 ID
//     ...payload            ← 从 pos + 9 开始！ （跳过 type+size+id 共 9 字节）
//
//   ⚠⚠ 两个极易混淆的偏移，务必分清（本实现两个坑都踩过）：
//     * **步进**用 pos + 5 + sec  —— sec 含 5 字节头 (type+size)，**不含** id。
//     * **payload 起点**是 pos + 9 —— 即 sec 的头 5 字节 之后再跳过 4 字节 id。
//     早先误把 payload 起点也写成 pos + 5，结果所有对象的负载都**错位 4 字节**
//     （读出来的是对象自己的 ID 而不是 payload 第一个字段），
//     表现为：Event 的 actionCount 读成一个巨大的数、Action 的 idExt 乱码，
//     event→media 命中率直接归零。逐条 dump 十六进制才定位到。
//   实测：按 pos+5+sec 步进，290 个对象正好结束在 HIRC 段末尾（逐字节验证过）。
//
//   payload（按类型）：
//     Event(0x04)   : u32 actionCount, u32 actionID[actionCount]
//     Action(0x03)  : u16 actionType(0x0403=Play), u32 idExt(=目标对象ID)
//     Sound(0x02)   : u32 pluginID, u8 streamType, u32 sourceID(=media_id), u32 inMemorySize
//                     ⚠ 是 AkBankSourceData：pluginID(4) + streamType(**1 字节**) + ...
//                       sourceID 在 payload +5，**inMemorySize(wem 字节大小) 在 +9**。
//                       曾误当成 u32+u16+u16+u8+u32（9 字节头），结果 media= 打出来
//                       全是 wem 大小（同 size 无法区分，如装瓶 873092594 与武器晃动
//                       575125111 size 都是 14326）。逐字节 dump 才定位。见下文再述。
//     Container(0x05 Random/0x06 Switch/0x07 ActorMixer):
//                     前面是可变的 NodeBaseParams（FX/State/RTPC 数量不定），
//                     尾部： ... eRandomMode(1) eMode(1) byBitVector(1)
//                            u32 ulNumChilds, u32 childID[ulNumChilds]
//                            ⚠ ulNumPlaylist —— **0x05/0x07 是 u16，0x06 Switch 是 u32**！
//                            Playlist[]   （各类型项长不同，我们不需要，只用于校验）
//                     → 用"三重约束"定位子节点列表（见 FindChildren）。
//                     ⚠ ulNumPlaylist 可以 > ulNumChilds（实测 25 children / 27 playlist），
//                       所以判据是 `n <= playlist`；早先统一按 u16 且要求相等，
//                       导致 **Switch 容器全部落选**（pl_att_cmn 64 次事件全链断）。
//
//  反查链：eventID → actionID → Action.idExt → (Sound.sourceID | 容器 children…) → media_id
//
//  安全：nbnk 视为**不可信输入**，每个字段读前做边界检查，任何异常立即放弃该银行。
// ===========================================================================
#include "SonarBanks.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace sonaraudio {

// 宽字符串 → UTF-8 窄字符串（对外可见，SonarAudio.cpp 也用）
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

namespace {

// ---- 带边界的顺序读取器 -----------------------------------------------------
class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    void seek(size_t p) { if (p <= n_) pos_ = p; else ok_ = false; }
    void skip(size_t k) { if (pos_ + k <= n_) pos_ += k; else ok_ = false; }

    bool u8(uint8_t& v)  { if (!ok_ || pos_ + 1 > n_) { ok_ = false; return false; } v = p_[pos_++]; return true; }
    bool u16(uint16_t& v){ if (!ok_ || pos_ + 2 > n_) { ok_ = false; return false; } std::memcpy(&v, p_ + pos_, 2); pos_ += 2; return true; }
    bool u32(uint32_t& v){ if (!ok_ || pos_ + 4 > n_) { ok_ = false; return false; } std::memcpy(&v, p_ + pos_, 4); pos_ += 4; return true; }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
};

std::string BaseNameNoExt(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    std::wstring f = (s == std::wstring::npos) ? p : p.substr(s + 1);
    size_t dot = f.find_last_of(L'.');
    if (dot != std::wstring::npos) f = f.substr(0, dot);
    return Narrow(f);
}

// ---- 顺序遍历段表（不按字节扫描！）-----------------------------------------
struct Chunk { size_t off = 0; size_t size = 0; bool found = false; };

void WalkChunks(const std::vector<uint8_t>& d, Chunk& bkhd, Chunk& didx, Chunk& hirc) {
    size_t pos = 0;
    while (pos + 8 <= d.size()) {
        const uint8_t* tag = d.data() + pos;
        uint32_t sz = 0;
        std::memcpy(&sz, d.data() + pos + 4, 4);
        const size_t body = pos + 8;
        // 大小不合理 → 停（对坏 bank 的兜底）
        if (body + (size_t)sz > d.size()) break;
        if (std::memcmp(tag, "BKHD", 4) == 0 && !bkhd.found) { bkhd = { body, sz, true }; }
        else if (std::memcmp(tag, "DIDX", 4) == 0 && !didx.found) { didx = { body, sz, true }; }
        else if (std::memcmp(tag, "HIRC", 4) == 0 && !hirc.found) { hirc = { body, sz, true }; }
        pos = body + sz;
    }
}

// ---- 容器子节点定位（三重约束，见文件头注释）--------------------------------
//  ⚠ RandomSeq(0x05)/ActorMixer(0x07) 与 Switch(0x06) 的**尾部结构不同**：
//    0x05/0x07 : ... u32 ulNumChilds, u32 childID[n], **u16 ulNumPlaylist**, Playlist[]
//    0x06      : ... u32 ulNumChilds, u32 childID[n], **u32 ulNumPlaylist**, Playlist[]
//  实测（pl_att_cmn.nbnk 的 969 字节 Switch）：children 后是 `1b 00 00 00`(=27, u32)
//  而不是 u16。原来统一按 u16 校验，导致 Switch 全部落选 → 该 bank 64 次事件全链断。
//  另外 ulNumPlaylist 可以 **> ulNumChilds**（如 25 children / 27 playlist 项），
//  所以判据用 `n <= playlist`，不是相等。
std::vector<uint32_t> FindChildren(const std::vector<uint8_t>& d, size_t body, size_t sec,
                                   const std::unordered_map<uint32_t, BankInfo::Obj>& objs,
                                   uint8_t type) {
    std::vector<uint32_t> best;
    const size_t end = body + sec;
    if (sec < 12 || end > d.size()) return best;
    const bool is_switch = (type == 0x06);
    for (size_t p = body; p + 5 <= end; ++p) {
        uint32_t n = 0;
        std::memcpy(&n, d.data() + p, 4);
        if (n < 1 || n > 4096) continue;
        const size_t ch_end = p + 4 + (size_t)n * 4;
        // Switch 的 playlist 计数是 u32，其余是 u16；都留 4 字节余量
        if (ch_end + (is_switch ? 4u : 2u) > end) continue;
        std::vector<uint32_t> ids(n);
        std::memcpy(ids.data(), d.data() + p + 4, (size_t)n * 4);
        bool all_known = true;
        for (uint32_t c : ids) { if (!objs.count(c)) { all_known = false; break; } }
        if (!all_known) continue;
        if (is_switch) {
            uint32_t pl_n = 0;
            std::memcpy(&pl_n, d.data() + ch_end, 4);
            if (pl_n < n || pl_n > 4096) continue;   // playlist 项数 >= children 数
        } else {
            uint16_t pl_n = 0;
            std::memcpy(&pl_n, d.data() + ch_end, 2);
            if (pl_n != n) continue;
        }
        best = std::move(ids);   // 取最靠后的候选（NodeBaseParams 必在其前）
    }
    return best;
}

}  // namespace

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (128ll << 20)) {
        ::CloseHandle(h); return false;
    }
    out.resize((size_t)sz.QuadPart);
    size_t total = 0;
    while (total < out.size()) {
        DWORD got = 0;
        if (!::ReadFile(h, out.data() + total, (DWORD)(out.size() - total), &got, nullptr) || got == 0) break;
        total += got;
    }
    ::CloseHandle(h);
    if (total != out.size()) { out.clear(); return false; }
    return true;
}

int BankDatabase::ScanDirectory(const std::wstring& root, std::string& log, int max_files) {
    int parsed = 0, tried = 0;
    roots_seen_.push_back(Narrow(root));
    std::vector<std::pair<std::wstring, int>> stack{ { root, 0 } };
    while (!stack.empty() && tried < max_files) {
        auto cur = stack.back(); stack.pop_back();
        const std::wstring& dir = cur.first;
        if (cur.second > 8) continue;

        WIN32_FIND_DATAW fd{};
        const std::wstring pat = dir + L"\\*";
        HANDLE h = ::FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 ||
                (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
            const std::wstring full = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                stack.push_back({ full, cur.second + 1 });
                continue;
            }
            {   // 只要 nbnk / bnk
                const wchar_t* dot = wcsrchr(fd.cFileName, L'.');
                if (!dot) continue;
                std::wstring ext = dot + 1;
                for (auto& c : ext) c = (wchar_t)towlower(c);
                if (ext != L"nbnk" && ext != L"bnk") continue;
            }
            if (++tried > max_files) break;
            ++files_seen_;

            std::vector<uint8_t> data;
            if (!ReadWholeFile(full, data)) continue;
            ++files_read_;

            BankInfo b;
            b.path = full;
            b.name = BaseNameNoExt(full);

            Chunk bkhd, didx, hirc;
            WalkChunks(data, bkhd, didx, hirc);

            if (bkhd.found && bkhd.size >= 8) {
                uint32_t bank_id = 0;
                std::memcpy(&bank_id, data.data() + bkhd.off + 4, 4);
                b.bank_id = bank_id;
            }
            if (didx.found) {
                const size_t n = didx.size / 12;
                for (size_t k = 0; k < n; ++k) {
                    uint32_t id = 0, sz = 0;
                    std::memcpy(&id, data.data() + didx.off + k * 12, 4);
                    std::memcpy(&sz, data.data() + didx.off + k * 12 + 8, 4);  // uSize
                    char nm[64];
                    std::snprintf(nm, sizeof(nm), "%u.wem", (unsigned)(k + 1));
                    b.media_names[id] = nm;
                    b.media_sizes[id] = sz;
                }
            }
            if (hirc.found) {
                // 第一趟：解所有对象（容器 children 留空，后面补）
                Reader r(data.data() + hirc.off, hirc.size);
                uint32_t count = 0;
                if (r.u32(count) && count < 200000) {
                    for (uint32_t i = 0; i < count && r.ok(); ++i) {
                        const size_t start = r.pos();
                        uint8_t type = 0; uint32_t sec = 0, id = 0;
                        if (!r.u8(type) || !r.u32(sec) || !r.u32(id)) break;
                        if (start + 5 + (size_t)sec > hirc.off + hirc.size) break;

                        BankInfo::Obj obj;
                        obj.type = (HircType)type;
                        // ⚠ payload 起点 = start + 9（type(1) + sectionSize(4) + ulID(4)）。
                        //   不是 start + 5 —— 那是指向 ulID 本身，会让后面全部字段错位 4 字节。
                        //   注意 Reader 以 (data + hirc.off) 为基址，start 是相对 HIRC 的偏移，
                        //   这里换算成 data 里的绝对偏移供第二趟 FindChildren 用。
                        obj.body_off = hirc.off + start + 9;
                        obj.body_size = sec > 4 ? sec - 4 : 0;   // 扣掉 ulID 那 4 字节

                        if (type == 0x04) {
                            uint32_t n = 0;
                            if (r.u32(n) && n < 4096) {
                                obj.children.resize(n);
                                for (uint32_t k = 0; k < n; ++k)
                                    if (!r.u32(obj.children[k])) { obj.children.resize(k); break; }
                            }
                        } else if (type == 0x03) {
                            uint16_t at = 0; uint32_t ie = 0;
                            if (r.u16(at)) { obj.action_type = at; if (r.u32(ie)) obj.target_id = ie; }
                        } else if (type == 0x02) {
                            // Sound 负载 = AkBankSourceData：
                            //   u32 pluginID     @ +0
                            //   u8  streamType   @ +4   ← 只有 1 字节！不是 u16/u32
                            //   u32 sourceID     @ +5   ← 这才是 media id
                            //   u32 inMemorySize @ +9   ← 这个才是 wem 字节大小
                            // ⚠ 血泪教训：曾把 layout 当成 u32+u16+u16+u8+u32（9 字节头），
                            //   于是 sourceID 读到了 +9 的 inMemorySize —— 日志里 media=
                            //   打的全是"wem 文件大小"（如 14326），同 size 的 wem 完全无法
                            //   区分（装瓶 873092594 与武器晃动 575125111 size 都是 14326）。
                            //   逐字节 dump payload 才定位到 streamType 是 1 字节。
                            uint32_t plugin = 0; uint8_t st = 0; uint32_t src = 0;
                            if (r.u32(plugin) && r.u8(st) && r.u32(src))
                                obj.target_id = src;
                        }
                        // 步进：start + 5 + sec（sec 含 5 字节头）
                        r.seek(start + 5 + (size_t)sec);
                        b.objects[id] = std::move(obj);
                    }
                }
                // 第二趟：容器补 children（需要全量对象表做校验）
                for (auto& kv : b.objects) {
                    BankInfo::Obj& o = kv.second;
                    if (o.type == HircType::RandomSeq || o.type == HircType::Switch ||
                        o.type == HircType::ActorMixer) {
                        o.children = FindChildren(data, o.body_off, o.body_size, b.objects,
                                                  (uint8_t)o.type);
                    }
                }
            }

            const bool has_hirc = hirc.found;
            if (!b.objects.empty() || !b.media_names.empty()) {
                IndexBank(b);
                ++parsed;
                if (has_hirc) ++parsed_hirc_;
                else         ++parsed_data_;
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "nbnk 扫描：根目录=\"%s\"\n"
                  "  候选文件 %u 个，成功读取 %u 个\n"
                  "  定义 bank（含 HIRC，能连事件）: %u 个\n"
                  "  纯数据 bank（仅 DIDX+DATA）   : %u 个\n"
                  "  反查表: event %u 条  media %u 条\n",
                  roots_seen_.empty() ? "?" : roots_seen_.back().c_str(),
                  (unsigned)files_seen_, (unsigned)files_read_,
                  (unsigned)parsed_hirc_, (unsigned)parsed_data_,
                  (unsigned)event_media_.size(), (unsigned)media_name_.size());
    log += buf;
    if (parsed_hirc_ == 0) {
        if (parsed == 0) {
            log += "  ⚠ 一个 bank 都没解析到 —— 检查 BankRoot 是否指向解包出的\n"
                   "     chunk\\sound\\wwise\\Windows（游戏内 bank 打包在 .pak 里，\n"
                   "     不会以散文件形式出现在 exe 目录）。\n";
        } else {
            log += "  ⚠ 只扫到纯数据 bank，没有带 HIRC 的定义 bank —— 事件反查表为空，\n"
                   "     日志里只会显示 ID。请确认目录里有带 HIRC 段的 nbnk。\n";
        }
    }
    return parsed;
}

void BankDatabase::IndexBank(BankInfo& b) {
    for (auto& kv : b.media_names) {
        if (media_name_.find(kv.first) == media_name_.end())
            media_name_[kv.first] = b.name + "/" + kv.second;
    }
    // wem 字节大小（DIDX uSize）—— 用于日志的 wem= 列，区分同 size 的不同 wem
    for (auto& kv : b.media_sizes) {
        if (media_size_.find(kv.first) == media_size_.end())
            media_size_[kv.first] = kv.second;
    }
    for (auto& kv : b.objects) {
        const uint32_t id = kv.first;
        const BankInfo::Obj& o = kv.second;
        if (o.type != HircType::Event) continue;
        std::vector<uint32_t> media;
        std::unordered_set<uint32_t> visited;
        for (uint32_t aid : o.children) {
            const auto it = b.objects.find(aid);
            if (it == b.objects.end()) continue;
            if (it->second.type == HircType::Action)
                ExpandTarget(b, it->second.target_id, media, visited, 0);
            else
                ExpandTarget(b, aid, media, visited, 0);
        }
        std::sort(media.begin(), media.end());
        media.erase(std::unique(media.begin(), media.end()), media.end());
        event_media_[id] = std::move(media);
        event_bank_[id] = b.name;
        event_path_[id] = Narrow(b.path);
    }
    banks_.push_back(std::move(b));
}

void BankDatabase::ExpandTarget(const BankInfo& b, uint32_t obj_id, std::vector<uint32_t>& out_media,
                                std::unordered_set<uint32_t>& visited, int depth) const {
    if (depth > 16 || !obj_id) return;
    if (!visited.insert(obj_id).second) return;
    const auto it = b.objects.find(obj_id);
    if (it == b.objects.end()) {
        if (b.media_names.count(obj_id)) out_media.push_back(obj_id);
        return;
    }
    const BankInfo::Obj& o = it->second;
    switch (o.type) {
        case HircType::Sound:
            if (o.target_id) out_media.push_back(o.target_id);
            break;
        case HircType::Action:
            ExpandTarget(b, o.target_id, out_media, visited, depth + 1);
            break;
        case HircType::RandomSeq:
        case HircType::Switch:
        case HircType::ActorMixer:
            for (uint32_t c : o.children) ExpandTarget(b, c, out_media, visited, depth + 1);
            break;
        default:
            break;
    }
}

const std::vector<uint32_t>* BankDatabase::MediaForEvent(uint32_t event_id) const {
    const auto it = event_media_.find(event_id);
    return it == event_media_.end() ? nullptr : &it->second;
}
std::string BankDatabase::MediaName(uint32_t media_id) const {
    const auto a = media_alias_.find(media_id);
    if (a != media_alias_.end()) return a->second;
    const auto it = media_name_.find(media_id);
    return it == media_name_.end() ? std::string() : it->second;
}
uint32_t BankDatabase::MediaSize(uint32_t media_id) const {
    const auto it = media_size_.find(media_id);
    return it == media_size_.end() ? 0u : it->second;
}
std::string BankDatabase::BankNameForEvent(uint32_t event_id) const {
    const auto it = event_bank_.find(event_id);
    return it == event_bank_.end() ? std::string() : it->second;
}
std::string BankDatabase::BankPathForEvent(uint32_t event_id) const {
    const auto it = event_path_.find(event_id);
    return it == event_path_.end() ? std::string() : it->second;
}
void BankDatabase::AddMediaAlias(uint32_t media_id, const std::string& alias) {
    if (!alias.empty()) media_alias_[media_id] = alias;
}

}  // namespace sonaraudio
