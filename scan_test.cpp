// 扫描逻辑的离线测试：造一块假堆、埋一个假怪物，直接调 WeaponSoundEnhance.cpp
// 里真正的 monster::ScanRange，看 (1) 找不找得到 (2) 要多久 (3) 深度校验几次。
//
// 直接 #include 那个 .cpp，测的就是真代码，不是副本，不会和实际跑的版本走偏。
#include "WeaponSoundEnhance.cpp"

#include <cstdio>
#include <cstdlib>

static std::uint64_t xs = 0x9E3779B97F4A7C15ULL;
static std::uint64_t rnd()
{
    xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17;
    return xs;
}

int main()
{
    plugin::gLogPath = L"scan_test.log";
    ::DeleteFileW(plugin::gLogPath.c_str());

    // 最严苛的情况：这个进程里那些游戏地址全都没映射，
    // 探针必须老老实实报「读不出来」，不能炸。
    printf("---- 地址探针（全部地址无效）----\n");
    monster::AddrProbe();
    printf("没崩，日志见 scan_test.log\n\n");

    // ---- 造一块 256MB 的假堆 ----
    const std::size_t SZ = 256u << 20;
    unsigned char* heap = (unsigned char*)::VirtualAlloc(
        nullptr, SZ, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!heap) { printf("VirtualAlloc 失败\n"); return 1; }
    printf("假堆 %p, %zu MB\n", (void*)heap, SZ >> 20);

    // 填充必须像真实游戏堆：既有大量对齐指针，也有大量小整数
    // （计数器、索引、标志位）。
    //
    // 上一版只填指针和纯随机 32 位数，fsm 的 0..100000 范围检查几乎总是
    // 失败，于是测出来的深度校验密度是 0.2 次/MB，而实机是 175 次/MB ——
    // 差 900 倍。那个「全部通过」是假的，它没复现真实数据的特征。
    const std::uintptr_t hb = (std::uintptr_t)heap;
    for (std::size_t i = 0; i + 8 <= SZ; i += 8) {
        const std::uint64_t r = rnd();
        std::uint64_t v;
        const int kind = (int)(r & 7);
        if (kind < 3) {                      // 3/8 对齐指针
            v = (std::uint64_t)((hb + ((r >> 8) % (SZ - 0x10000))) & ~(std::uint64_t)15);
        } else if (kind < 7) {               // 4/8 一对小整数，专打 fsm 的范围检查
            const std::uint32_t a = (std::uint32_t)((r >> 8)  % 70000);
            const std::uint32_t b = (std::uint32_t)((r >> 32) % 70000);
            v = ((std::uint64_t)b << 32) | a;
        } else {                             // 1/8 纯随机
            v = r;
        }
        std::memcpy(heap + i, &v, 8);
    }

    // ---- 埋一个假怪物 ----
    // 挑一个 16 对齐、且后面放得下整个对象的位置
    const std::size_t MON_OFF = (SZ / 2) & ~(std::size_t)15;
    unsigned char* mon = heap + MON_OFF;
    unsigned char* act = heap + 0x100000;      // 动作对象
    unsigned char* hpo = heap + 0x200000;      // 血量对象

    const std::uintptr_t actp = (std::uintptr_t)act;
    const std::uintptr_t hpop = (std::uintptr_t)hpo;
    std::memcpy(mon + monster::OFF_ACT,   &actp, 8);
    std::memcpy(mon + monster::OFF_HPOBJ, &hpop, 8);

    const std::int32_t fsm = 40, fsmTgt = 8, lmt = 28787;   // 28787 是黑龙大咬的真实 ID；gMinActionId 会把 <100 的当噪声丢掉，所以不能用小数字
    std::memcpy(mon + monster::OFF_FSMID,  &fsm,    4);
    std::memcpy(mon + monster::OFF_FSMTGT, &fsmTgt, 4);
    std::memcpy(act + monster::OFF_LMT,    &lmt,    4);

    const float frame = 12.0f, frameEnd = 90.0f, hpMax = 60000.0f, hpCur = 55000.0f;
    std::memcpy(act + monster::OFF_FRAME,   &frame,    4);
    std::memcpy(act + monster::OFF_FRAMEND, &frameEnd, 4);
    std::memcpy(hpo + monster::OFF_HPMAX,   &hpMax,    4);
    std::memcpy(hpo + monster::OFF_HPCUR,   &hpCur,    4);

    printf("埋的怪物在 %p (堆内偏移 0x%zX)\n\n", (void*)mon, MON_OFF);

    // ---- 跑真正的扫描 ----
    monster::gList.clear();
    monster::gDeepProbes = 0;
    std::vector<unsigned char> buf;
    int regions = 0;
    std::uint64_t bytes = 0;
    const std::uint64_t t0 = ::GetTickCount64();
    const std::uint64_t deadline = t0 + 30000;   // 测试放宽到 30 秒，看真实耗时

    const bool timedOut = monster::ScanRange(monster::gList, hb, hb + SZ, 0, buf, deadline, regions, bytes);
    const std::uint64_t ms = ::GetTickCount64() - t0;

    printf("扫描完成: %llu ms, 区域 %d, %llu MB, 深度校验 %lld 次, 命中 %d 个\n",
           (unsigned long long)ms, regions, (unsigned long long)(bytes >> 20),
           monster::gDeepProbes, (int)monster::gList.size());
    if (timedOut) printf("!! 超时中断\n");

    bool found = false;
    for (std::size_t i = 0; i < monster::gList.size(); ++i) {
        const monster::Mon& m = monster::gList[i];
        const bool isPlant = (m.ptr == (std::uintptr_t)mon);
        if (isPlant) found = true;
        printf("  #%d ptr=%p HP %.0f/%.0f 动作 %d fsm %d/%d 帧 %.1f/%.1f %s\n",
               (int)i, (void*)m.ptr, m.last.hp, m.last.hpMax, m.last.lmt,
               m.last.fsmTgt, m.last.fsm, m.last.frame, m.last.frameEnd,
               isPlant ? "  <== 就是埋的那个" : "  (误报)");
    }

    // ---- 判定 ----
    int bad = 0;
    printf("\n");
    if (!found) { printf("失败: 没找到埋进去的怪物\n"); ++bad; }
    else        { printf("通过: 找到了埋进去的怪物\n"); }

    const int fps = (int)monster::gList.size() - (found ? 1 : 0);
    printf("误报: %d 个", fps);
    if (fps > 0) { printf("   偏多\n"); ++bad; } else { printf("   干净\n"); }

    // 深度校验密度只是诊断信息，本身不算失败 —— 真正要看的是 ms/MB。
    // 这块假堆的密度（4354 次/MB）比实机（175 次/MB）还高 25 倍，是故意的。
    const double perMB = bytes ? (double)monster::gDeepProbes / (double)(bytes >> 20) : 0.0;
    printf("深度校验密度: %.1f 次/MB   (仅供参考)\n", perMB);

    const double msPerMB = bytes ? (double)ms / (double)(bytes >> 20) : 0.0;
    printf("扫描速度: %.2f ms/MB", msPerMB);
    if (msPerMB > 5.0) { printf("   过慢\n"); ++bad; }
    else               { printf("   可以接受\n"); }

    ::VirtualFree(heap, 0, MEM_RELEASE);
    printf("\n%s\n", bad ? "有失败项" : "全部通过");
    return bad ? 1 : 0;
}
