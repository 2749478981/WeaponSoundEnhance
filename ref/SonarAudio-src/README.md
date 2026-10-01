# SonarAudio —— 抓取《怪物猎人：世界/冰原》正在播放的音效

> 给 **Sonar** 插件提供"游戏此刻在播什么音效"的数据源。
> 面向 MHW Iceborne **15.23.00**，原生 DLL，放进 `nativePC\plugins\` 即用。

---

## 0. 一句话

游戏把 Wwise 静态链接进 `MonsterHunterWorld.exe`，但**导出了 mangled 名字**
（`ref/wwise_symbols.txt` 里 205 个 `?xxx@SoundEngine@AK@@...`）。本插件
**hook `AK::SoundEngine::PostEvent`**，每次游戏要求播音效时拿到
`eventID / playingID / gameObjectID`，再用启动时解析的 `.nbnk` 表把
`eventID` 展开成 `media_id`（wem），写进**命名共享内存**给 Sonar 读。

零特征码、零写内存、零 `ReadProcessMemory`（在游戏进程内直接读）。

---

## 1. 为什么能这么做（关键发现）

| 事实 | 出处 / 验证 |
|---|---|
| 游戏**导出**了 Wwise 的 mangled 符号 | `ref/wwise_symbols.txt`：205 个，地址 `0x143E71CBF`..`0x143E765C6`（落在 PE 导出目录内） |
| 能用 `GetProcAddress` / 手工解析导出表拿到函数 | `src/SonarWwise.cpp` 手工遍历 `IMAGE_EXPORT_DIRECTORY`，**按名字精确匹配**；天然 ASLR 安全 |
| `PostEvent` 有 3 个重载 | byID / byName(`char*`) / byNameW(`wchar_t*`) —— 三个都 hook |
| 能反查 `playingID → eventID` | `?GetEventIDFromPlayingID@Query@SoundEngine@AK@@YAKK@Z` |
| `.nbnk` = Wwise bank v120 | `ref/11弓箭/*.nbnk.txt` 实测转储；`BKHD.dwBankGeneratorVersion = 120` |

**不需要特征码**（不像伤害钩子那样）。导出名跨版本通常也稳（Audiokinetic
自带的导出名不会变），但**函数地址会随版本漂**——所以我们只信导出表，
不写死任何绝对地址。

---

## 2. `.nbnk` 解析规格（已逐字节验证）

> 验证脚本：`tools/verify_nbnk2.py`（在 `ref/11弓箭/wp11_bow_epvsp_shell.nbnk`
> 上跑出 **290 个对象、96% 的 event 能连到 media_id**，与官方转储逐项吻合）。

### 段表：**顺序遍历**，不要按字节扫 tag
```
tag(4) + size(4) + payload(size)，依次向下
```
⚠️ **坑**：`DATA` 段内部会出现与 `HIRC` 相同的字节序列；按字节线性扫描会假命中
（开发时踩过：把 HIRC 定位到 0x1D18FD 之外的地方，解析出 2 个对象而非 290 个）。
必须按上面的结构顺序走。

段：`BKHD`(头) / `DIDX`(media 索引) / `DATA`(wem 数据) / `HIRC`(层级对象)

### DIDX：每 12 字节
```
u32 mediaID      ← 要暴露给 Sonar 的 ID
u32 offset       ← 相对 DATA 段起点
u32 size
```

### HIRC：`u32 count` + 逐个对象
```
u8  eHircType
u32 dwSectionSize   ← **含这 5 字节头**！下一对象 = pos + 5 + sec
u32 ulID            ← 对象自己的 ID
...payload          ← **从 pos + 9 起**
```

⚠️⚠️ **两个偏移必须分开算（本项目踩过，曾致命中率归零）**：

| 用途 | 公式 | 说明 |
|---|---|---|
| 跳到下一对象 | `pos + 5 + dwSectionSize` | `sec` **含** 5 字节头 `type(1)+size(4)`，**不含** `ulID` |
| **取 payload 起点** | **`pos + 9`** | 头 5 字节之后**再跨过 4 字节 `ulID`** |

- ✅ 正确步进：`next = pos + 5 + dwSectionSize` → 290 个对象正好结束在 HIRC 段末
- ❌ 错误步进：`next = pos + 9 + dwSectionSize` → 第 2 个对象就跑飞
- ✅ 正确 payload：`body = pos + 9`
- ❌ **错误 payload：`body = pos + 5`** → 指向 `ulID` 自己，**所有字段整体右移 4 字节**，
  读出来的是对象自己的 ID 而不是第一个字段。

> **踩坑实录**：早先 `body_off = pos + 5` 时，`wp11_bow_epvsp_shell.nbnk` 的
> 70 个 Event **一个都连不到 media**。症状伪装成"链断在 Switch 容器"，
> 让人误以为是容器解析没做。dump 原始十六进制才看清：Event 的 payload 首 4 字节
> 读出来 = `106248500`（正是对象 ID 本身），真正的 `actionCount` 在它后面 = `1`。
> 改成 `pos + 9` 后武器音效命中率 **18% → 89.5%**。
> **教训：结构解析"看起来成功但字段值荒谬"时，别顺着症状改上层，先 dump 字节对字段。**

### 各类型 payload（偏移相对 `pos + 9`）
| 类型 | 值 | 布局 | 用途 |
|---|---|---|---|
| Sound | `0x02` | `u32 pluginID, u8 streamType, **u32 sourceID**(@+5), u32 inMemorySize(@+9)` | **sourceID = media_id**；`+9` 是 wem 字节大小 |
| Action | `0x03` | `u16 actionType(0x0403=Play), **u32 idExt**`（idExt 在 `+2`） | idExt = 目标对象 ID |
| Event | `0x04` | `u32 actionCount, u32 actionID[actionCount]` | 入口 |
| Random/Sequence | `0x05` | 变长 NodeBaseParams + 尾部 `u32 ulNumChilds, u32 childID[n]` + `CAkPlayList` | 子节点 |
| Switch | `0x06` | 同上 | 子节点 |
| Actor-Mixer | `0x07` | 同上 | 子节点 |

**容器的子节点列表**位置不固定（前面的 NodeBaseParams 随 FX/State/RTPC 数量变化），
用**三重约束**定位（`SonarBanks.cpp::FindChildren`）：
1. 候选 `ulNumChilds` ∈ 1..4096
2. 后面 n 个 childID **全部**是已知对象
3. 紧接其后的 PlayList 计数校验 —— ⚠ **0x05/0x07 是 `u16` 且要求 `== n`；
   `0x06` Switch 是 `u32` 且只需 `>= n`**（实测 `pl_att_cmn` 有 25 children / 27 playlist）。

> **踩坑实录 2**：早先第 3 条统一按 `u16 == n` 校验，导致**所有 Switch 容器落选**，
> `pl_att_cmn`（玩家攻击通用音）64 次事件全部链断。
> 修正后：武器音效 89.5% → **98.5%**，mhwmod 解包 61.9% → **83.6%**，
> nativePC 68.2% → **74.1%**。

取**最靠后**满足条件的候选（NodeBaseParams 必在其前）。

### 反查链
```
eventID → actionID[] → Action.idExt → (Sound.sourceID | 容器 children 递归) → media_id[]
```

---

## 3. 部署

**一键部署（推荐）**：

```bat
deploy.bat              :: 编译 + 部署
deploy.bat --no-build   :: 只部署（跳过编译）
```

目标目录默认 `F:\SteamLibrary\steamapps\common\Monster Hunter World\nativePC\plugins`，
可用环境变量 `SONARAUDIO_PLUGINS` 覆盖。

> 游戏正在运行时 DLL 会被锁定，脚本会明确报错 —— 先退出游戏再跑。

手工部署则把 `dist\` 里的东西拷进游戏**根目录**的 `nativePC\plugins\`：
```
nativePC\plugins\
  ├─ SonarAudio.dll
  ├─ SonarAudio.ini              (配置)
  └─ SonarAudio.mediaids.txt     (media_id → 可读名，4655 条)
```
前置：**Stracker's Loader**（和别的原生 DLL 插件一样）。
进游戏即生效，日志在 `plugins\SonarAudio.log`。

---

## 3.5 ⚠️ 让日志显示名字（而不是一堆 ID）—— 必须配 BankRoot

**这是最容易踩的坑。** 游戏内的 `.nbnk` 是打包在 `chunk*.pak` 里的，
磁盘上**没有散落的 `.nbnk` 文件**。所以默认的"exe 所在目录"扫描几乎必然
一个 bank 都扫不到 → `eventID → media_id` 反查表为空 → 日志里只有一堆裸 ID。

**解决办法**：把已经**解包**出来的 wwise 目录告诉插件。

```ini
BankRoot=D:\mhwmod解包\chunk\sound\wwise\Windows
```

留空时插件会**自动依次尝试**这些位置（找到一个不算完，会全试一遍，
因为不同目录的 bank 是互补的）：

```
<游戏目录>\chunk\sound\wwise\Windows
<游戏目录>\mhwmod解包\chunk\sound\wwise\Windows
D:\mhwmod解包\chunk\sound\wwise\Windows
D:\mhwmod解包\chunkG8\sound\wwise\Windows
D:\mhwmod解包\sound\wwise\Windows
```

### 怎么知道扫对了？

看日志开头，插件会打印一段**扫描报告**：

```
nbnk 扫描：根目录="D:\mhwmod解包\chunk\sound\wwise\Windows"
  候选文件 288 个，成功读取 285 个
  定义 bank（含 HIRC，能连事件）: 53 个
  纯数据 bank（仅 DIDX+DATA）   : 227 个
  反查表: event 860 条  media 1457 条
```

- **定义 bank > 0** → 正常，事件能反查。
- **定义 bank = 0** → 反查表为空，日志只有 ID。日志会打印黄色警告告诉你去改 `BankRoot`。

### 为什么事件命中率不是 100%？

修正 payload 偏移后实测（全源合并）：**event→media 共 1507 条**，
分源命中率：

| 来源 | bank | event | 连到 media | 命中率 |
|---|---|---|---|---|
| 武器音效源文件 | 82 | 827 | 740 | **89.5%** |
| 游戏 nativePC | 102 | 1404 | 958 | **68.2%** |
| mhwmod 解包 | 318 | 1710 | 1058 | **61.9%** |

剩下连不到的部分属于：

- Event → `Switch` 容器按游戏状态分支，静态解析拿不全所有分支（运行时才知道走哪支）；
- 非播放类 Action（`SetSwitch` / `SetState` / `SetRTPC`），本就没有 media；
- 一部分 voice/BGM 走 **streamed** 流式资源，`sourceID` 不在同 bank 的 DIDX 里；
- **游戏本体 event（BGM / UI / 任务音）的 bank 打包在 `chunk*.pak` 里，
  磁盘上没有散文件 —— 这类 eventID 永远查不到，是正常现象，不是 bug。**

运行时靠 `PostEvent` hook 拿到的是**真实触发的 eventID**，
配合静态映射已能覆盖绝大多数可听音效。

---

## 3.6 让名字带上中文注释（武器音效专用，推荐）

`D:\下载\音效源文件（含笔记）\` 里是**按武器分类的音效源文件**——
每个包旁边都有导出好的 `ogg/` `wav/` `wem/` 目录，文件名形如
`12 （射箭）.ogg`，**部分是人工整理的中文注释**。这是最值钱的名字来源。

### 关键规律（已在全部 82 个包上验证）

- DIDX 段里的 `media_id` **严格升序**排列；
- DIDX 条数 **等于** ogg/wem 文件个数；
- 第 k 条 DIDX（0-based）↔ 文件 `{k+1:02d}.{ext}`。

所以 `media_id ↔ 文件名` 是**一一对应**的，能可靠还原。

### 生成映射表

```bat
cd dll\SonarAudio
python tools\gen_media_map.py "D:\下载\音效源文件（含笔记）" dist\SonarAudio.mediaids.txt
```

产物（UTF-8 无 BOM，TSV）：

```
# SonarAudio media_id → 可读名 映射表
20505677	wp11_bow_epvsp_shell/01.ogg （落石）
12607224	wp03_swo_epvsp/01.wav （居合拔刀斩）
8047905	wp_bow_cmn/01.ogg （瓶子声）
48352700	pl_act_vo_f_07_m/08.ogg （被击飞跪地时）
```

实测生成 **4655 条映射，其中 373 条带中文注释**。
把 `SonarAudio.mediaids.txt` 和 DLL 放一起，插件启动时会自动加载
（日志会打印 `载入 media 别名 N 条`）。

> 名字字段上限 `kNameLen = 96`。实测最长条目 88 字节（中文注释），
> 48 会截断，所以定成 96。

---

## 4. 配置 `SonarAudio.ini`

| 键 | 默认 | 说明 |
|---|---|---|
| `HookPostEvent` | 1 | 主通道：hook PostEvent（推荐开） |
| `PollEnabled` | 1 | 辅通道：轮询补偿（推荐开） |
| `PollMs` | 50 | 轮询周期（5..2000） |
| `LogEvents` | 1 | 写 `SonarAudio.log`（有限速；关掉最快） |
| `ScanBanks` | 1 | 扫 `.nbnk` 建反查表 |
| `BankRoot` | 空 | **nbnk 根目录（重要，见 §3.5）**；空 = 自动试多个常见位置 |
| `DumpFirstMs` | 0 | 调试：启动后前 N 毫秒全量记日志 |

**日志行格式**（配上 BankRoot 后）：

```
evt seq#1234 id=418036518 playing=8388999 gobj=1234567 media=35399 name=wp11_bow_epvsp_shell/3.wem  bank=wp11_bow_epvsp_shell  path=D:\mhwmod解包\chunk\sound\wwise\Windows\wp11_bow_epvsp_shell.nbnk
```

未匹配到 bank 时：

```
evt seq#1235 id=999888777 playing=8389001 gobj=1234567 media=0  [未匹配到 bank]
```

---

## 5. Sonar 侧怎么接（共享内存契约）

**接口头：`src/SonarAudioIpc.h`**（两边必须包含同一份）。

```cpp
// 共享内存名
L"Local\\SonarAudioEvents_v1"
// 布局: [SonarAudioHeader][SonarAudioEvent × 2048]   每条 64B
```

现成读取端 **`src/SonarAudioReader.h`**（header-only，拷进 Sonar 直接用）：

```cpp
#include "SonarAudioReader.h"
using namespace sonaraudio;

static SonarAudioReader reader;      // 成员变量，长生命周期

// 每帧：
reader.Attach();                     // 重试连接（幂等）
SonarAudioEvent ev;
while (reader.Next(ev)) {
    // ev.event_id     Wwise Event ID
    // ev.playing_id   Playing ID
    // ev.game_obj_id  触发者（低32位，通常玩家/怪物句柄）
    // ev.media_id     反查到的第一个 media_id
    // ev.event_name / ev.bank_name   可读名（UTF-8）
    // ev.flags        kFlagFromPostEvent / kFlagFromPolling / kFlagNameKnown
    // ev.tick_ms      GetTickCount64()
    // ev.seq          单调递增；读端靠它判断新旧，不要用时间戳
}
```

### 读端协议（重要）
- `header->write_seq` 单调递增；把它和上次的值比较就知道有没有新事件。
- 每条事件的 `seq` 严格递增，用 `seq` 判断"是不是新的"。
- 落后超过一整圈（2048 条）时，旧数据已被覆盖 —— `Next()` 会自动跳到可读的最早位置。
- **写端先写事件体、再递增 `write_seq`（Release）**，读端看到 seq 更新即数据可见。

### 玩法建议（Sonar）
- **连招/动作提示**：`event_id` 序列可当"动作指纹"（同一把武器同一招音效固定）。
- **怪物动作预判**：怪物攻击的咆哮/预备音在出招前播放 → 抓它做预警。
- **节流**：用 `tick_ms` 差值过滤同一音效的密集重复。
- **武器/场景判定**：`bank_name` 就是音效包名（如 `wp11_bow_epvsp_shell`），
  直接反映当前武器/状态。

---

## 6. 构建

```
build.bat          → out\x64\Release\SonarAudio.dll + dist\（可部署）
```
- MSVC v142，`/utf-8`，`/MT`（Release 静态 CRT，单文件免装）。
- 依赖：`deps\minhook\`（MinHook，已随仓库带 lib）。

---

## 7. 安全性（照 `MHW_DLL_Plugin_Skill.md` 的血泪清单）

- ✅ **只 hook 一个已知导出函数**；不写玩家状态、不调游戏状态函数。
- ✅ **hook 回调里绝不写盘 / new / 开线程**（0xC0000005 教训）：
  回调只做 `memcpy` 级记账 + 哈希查表 + 写共享内存；日志走独立线程。
- ✅ **`.nbnk` 当不可信输入**：每字段读前边界检查，坏 bank 直接放弃、绝不崩。
- ✅ **符号解析走导出表**，不写死绝对地址 → ASLR 安全、换版本至少能"找不到就禁用"。
- ⚠️ MinHook 会改写函数入口 5 字节。若与其它内联 hook 冲突（别的插件也 hook
  PostEvent），可能失败 —— 日志会明确写"没有成功创建任何 hook"。

---

## 8. 局限 / 已知问题

1. **`media_id` 不一定出现在同一银行的 DIDX 里**：MHW 很多音效是
   **流式（streamed）** 的，wem 存在别处；`media_id` 仍是正确的 Wwise 标识，
   但要拿到波形文件需要自己按 ID 去对应包找。
2. **`event_name` 基本为空**：MHW 的 nbnk **不带 STID/名字段**（只有 ID）。
   可读名来自 `SonarAudio.mediaids.txt` 或 `bank_name`。
3. **`byName` 重载的 eventID 是字符串哈希**（非真 ID）：MW 主要用 byID，
   byName 极少；若看到"哈希 ID"说明游戏走了名字路径。
4. **轮询通道**依赖 `GetPlayingIDsFromGameObject`，需要有效的 `gameObjectID`；
   我们靠 hook 学到的 ID，开局头几帧可能还没学到 → 此时只有 hook 通道工作。

---

## 9. 文件清单

```
dll/SonarAudio/
  SonarAudio.vcxproj / build.bat / SonarAudio.ini
  deps/minhook/                    MinHook.h + libMinHook.x64.lib
  src/
    SonarAudioIpc.h                共享内存契约（两边共用）
    SonarAudioReader.h             Sonar 侧现成读取端（header-only）
    SonarWwise.h/.cpp              PE 导出表解析 + Wwise 函数绑定
    SonarBanks.h/.cpp              nbnk 解析 + eventID→media_id 反查
    SonarIpc.h/.cpp                共享内存写入端 + 限速日志
    SonarAudio.cpp                 主入口：hook + 轮询 + 装配
  tools/verify_nbnk2.py            nbnk 解析验证脚本（对着真实 bank 跑）
  tools/scan_real_banks.py         批量扫描真实解包目录，量测 event→media 命中率
  tools/gen_media_map.py           从武器音效源文件生成 media_id → 可读名
  deploy.bat                       编译 + 部署到游戏 plugins 目录
  dist/                            可部署产物
```
