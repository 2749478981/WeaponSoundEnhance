# 给 Sonar / 音效 agent 的同步说明（v2）

> 承接 `NBNK_CAPTURE_NOTES.md`。那份讲 **nbnk 怎么解析**，这份讲
> **解析之外踩到的现实问题 + 我们这边新加的东西**，避免你重复趟一遍。
> 环境同上：**MHW / Iceborne 15.23.00**，插件放 `nativePC\plugins\`。

---

## 0. 一句话

**nbnk 解析已经解决了"这个 eventID 对应什么音效"；
但"这个音效到底该不该在此时放"是另一回事。**
我们在 WeaponSoundEnhance 侧抓到一个很反直觉的结论：

> **FSM / lmt 完全无法区分弓的「装瓶 / 解瓶 / 射箭」——
> 三者都落在同一层（fsmtgt=3），lmt 全等于 `49153`。
> 唯一可靠的动作指纹是 `wem media id` 本身。**

---

## 1. 关键结论：FSM 是分层的，而且弓的动作全挤在一层

用户在游戏里按 Ctrl+F7 做 MARK 打点，探针（`SonarLayerProbe`）实测：

| 字段 | 地址 | 装瓶时 | 解瓶时 | 射箭时 |
|---|---|---|---|---|
| 主状态 `fsm` | `entity+0x6278` | **0** | **0** | 0 / 非0 |
| 层选择 `fsmtgt` | `entity+0x6274` | **3** | **3** | 3 |
| lmt | `lmt` (=`act+0xE9C4`) | **49153** | **49153** | **49153** |
| layerA | `entity+0x55B8` | 1→49153 | 49153→1 | — |

**16 次 MARK 全部命中同一组值**，说明：
- 大家一开始只读 `entity+0x6278`（主状态字段）确实抓不到装瓶 ——
  因为**装瓶不写主状态层，它走的是另一个 layer**（`fsmtgt=3` 那一层）。
  大佬说"要读 layer0"的意思就是这个方向，但：
- **切到 layer0 也没用**：layer0 的 lmt 在装瓶/解瓶/射箭时**都是 49153**，
  它只表达"弓已持握"这个通用态，**分辨不了子动作**。
- `entity+0x6278` 在轻弩/近战武器上语义正常（非0），但**弓的装瓶动作就是不在这一层**。

> ⚠️ 所以「靠 fsm/lmt 抓装瓶解瓶」这条路**已经可以判定为走不通**，
> 别再把时间花在找 layer / 更深的 fsm 偏移上了。

---

## 2. 真正可用的指纹：wem media id

`WeaponSoundEnhance_wem.log` 里（媒体已解析出可读名），弓相关实测：

| 动作 | 可读名 | media_id | 备注 |
|---|---|---|---|
| **装瓶（涂瓶）** | `wp_bow_cmn/30.ogg` | **873092594** | 用户口中的"30" |
| **射箭（开火）** | `wp11_bow_epvsp_shell/12.ogg` | **217590158** |  |
| 一段蓄力 | `.../51.ogg` | 829824005 |  |
| 瓶子声 | `wp_bow_cmn/01.ogg` | 8047905 |  |
| **循环音（元凶）** | `wp_bow_cmn/10.ogg` | **182089195** | **约 1 次/秒，永不停**，用户口中的"10" |

**这就是用户原始症状的完整解释**：

- "每次重载播放一次装瓶" → 重载时引擎重放 bank，`873092594` 被误触发。
- "偶尔装瓶时同时捕获到 30 和 10" → 真装瓶音(`30`/`873092594`)
  **加**了那条从不停歇的循环音(`10`/`182089195`)。
- "解瓶也捕获到 10" → 循环音根本没停，解瓶只是"顺便被看到"。
- "解瓶偶尔是 4 和 10" → `4` 是另一个瓶子相关音，`10` 仍是循环音。
- "偶尔两次 10 / 偶尔啥也捕获不到" → 循环音是背景持续，
  真事件 `873092594` **触发不稳定**（时机/静默期相关）。

**结论：想做「装瓶/解瓶」识别，请按 media id 判断，不要碰 FSM。**
`873092594` = 装瓶，`217590158` = 射箭。循环音 `182089195` 应当被过滤掉。

---

## 3. 我们这边（WeaponSoundEnhance）新加的抑制机制

为了压掉"进集会区/重载时误触发的装瓶音"，加了两个 ini 参数
（**生效的是数据目录那份**：`nativePC\plugins\WeaponSoundEnhance\WeaponSoundEnhance.ini`，
不是源码目录那份）：

| 参数 | 默认 | 作用 |
|---|---|---|
| `KillSettleMs` | `2500` | 武器刚切换后的静默窗口(ms)，窗口内丢弃 wem 事件 |
| `WemBurstCap` | `6` | 60ms 内同一批 wem 事件数上限，超出的丢弃（滤突发/循环音） |

实现位于 `WeaponSoundEnhance.cpp` 的 `WemEventSink()`：
在原来的 `gWeapon` 判定之后插入 1.5) 静默窗 与 1.6) 突发批抑制。
`player::Refresh()` 末尾维护 `gStableSinceMs` / `gLastWeaponStable`。

> 📌 **如果你也要在 Sonar 侧做误触发过滤**，思路可以直接借用：
> **按 media id 白名单 + 最小间隔**，比按 FSM 判定稳得多。

---

## 4. 一个未解之谜（你要是有余力可以帮看）

日志里有一大批 `media=0 [未匹配到 bank]` 的**高频事件**：
约 **15:01 起、12 个固定 id 循环、gobj 数量暴涨**。
怀疑是**另一个 mod 或环境音源**在灌事件（不是我们插件的问题，但会污染捕获）。
排查方向：看 `WeaponSoundEnhance_wem.log` 里这些 id 是否来自同一个 gameObject，
或对照用户装的其他 mod 的 bank。

---

## 5. 交互 IPC 沿用的约定（不变）

- Sonar 解析出的事件/媒体信息走命名共享内存 IPC：
  `SonarIpc.{h,cpp}` + `SonarAudioIpc.h`；消费端 `SonarAudioReader.h`。
- **`RecordEvent()` 在 hook 回调里绝不写盘 / 不 new / 不开线程**：
  只做哈希查表 + 写共享内存 + 写单槽无锁 `PendingLog`，落盘交给独立线程。
  这条规则很硬 —— 违反会在音效高频回调里卡主线程。
- `SonarAudio` 已并入 `WeaponSoundEnhance.dll` 一起构建（不是独立 DLL 了），
  hook 已确认装上、205 个导出符号可用、nbnk 扫描正常
  （bank=592 / event=2503 / media=10071）。

---

## 6. 给上层的建议（一句话版）

1. **别再找弓动作的 FSM/lmt 了** —— 已证伪，同层同值。
2. **用 media id 认动作**：装瓶 `873092594`、射箭 `217590158`；
   过滤循环音 `182089195`。
3. **误触发抑制用「media 白名单 + 最小间隔」**，别用状态机。
4. 共享内存 IPC 的形状没变，消费者不用改。

---

## 7. 日志瘦身 —— ⛔ 已回滚，**不要再做**（2026-10-01 16:19）

> ⚠️ **重要边界**：用户明确「**sonar 就是 WeaponSoundEnhance**」，
> 且要求「**别改它的源码/功能**」。所以下面这套改动**已全部回滚**，
> 只是留档说明"做过什么、为什么撤"，**不要重新应用**。

曾经的改动（改 WSE 副本 `src/wwise/SonarIpc.cpp` / `SonarAudio.cpp`）：
- `LogLine` 每行 open/close 文件 → 常驻句柄；
- 加 `LogMaxKB` 大小上限轮转；`LogPump` 加 `LogUnmatched` 默认不写未匹配事件；
- 限速 2000→600/秒；心跳 30s→120s。

**回滚原因**：这些都算改 WSE 的**源码/功能**，超出授权范围。
**回滚方式**：`git checkout -- src/wwise/{SonarAudio,SonarIpc}.cpp SonarIpc.h`
（工作区已 clean，`grep LogUnmatched/LogMaxKB` 无残留）；
用回滚后源码重编 `WeaponSoundEnhance.dll` 并部署（`5d62764e…`）；
ini 里新增的两个键已删除。

**给后来者**：
- 「日志记太多」若还要处理，**首选不改代码的办法** —— 直接调 ini 现有键：
  `LogEvents=0`（完全不写盘）或调大/调小 `PollMs`。源码默认值不动。
- 真要改代码，**必须先问用户**再动，别自作主张。

---

## 8. wwiseutil（hpxro7/wwiseutil）评估 —— 结论：**基本没用，不用引**

仓库：<https://github.com/hpxro7/wwiseutil>（C#，GPLv3，最后更新 2018-10）。

**它能做的**（`-unpack` / `-replace` / `-loop`，GUI + CLI）：
- 解包 bnk/nbnk/pck/npck → 导出里面的 `.wem`；
- 替换 bank 里的 wem（更新元数据，可大可小）；
- 编辑循环点。**就是 MHW 音效 mod 的"打包/替换"工具**。

**它做不了的**（对我们无价值的部分）：
- ❌ **不给 HIRC 结构 / event→media 反查** —— 它不解事件链，只搬 wem 字节。
- ❌ 不导出 eventID / mediaID 映射表（GUI 只显示 wem id 给"替换"用）。
- ❌ 不支持 MHW 特有的 `.nbnk` 解析细节（我们的 `SonarBanks` 已逐字节搞定）。

**为什么不需要**：
1. 我们**已经有**更专门的**运行时**方案：`SonarBanks.cpp`（解析）+ hook PostEvent（抓实时事件）。
   wwiseutil 是**离线文件工具**，解决的是"改 bank 内容"，不是"抓正在播什么音"。
2. 唯一的交集是"解包 wem 拿名字" —— 但**已经有了**：
   - 外部映射表 `SonarAudio.mediaids.txt`（4655 条，`gen_media_map.py` 生成）；
   - 参考实现 RingingBloom（`ref/RingingBloom`）已覆盖同能力且更全。
3. GPLv3 许可，引入 C# 工具链还会污染纯 C++ 的构建流程。

**唯一可能有用的场景**（备查，不是现在必须）：
- 若将来要**批量把某 bank 的 wem 换成自定义音**（做音效替换 mod），
  可以用它 `-replace` 快速试；但这属于 **WeaponSoundEnhance 的"音效替换"需求**，
  与 SonarAudio 无关，且 RingingBloom 多半也能干。
- `-unpack -v` 出来的 wem 列表可以**交叉核对**我们 DIDX 解析的 mediaID 顺序是否一致
  （一次性 sanity check，价值有限）。
