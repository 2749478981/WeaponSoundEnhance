# WeaponSoundEnhance

<div align="center">
  <img src="assets/icon.png" alt="WeaponSoundEnhance icon" width="256">
</div>

给《怪物猎人：世界 / 冰原》增加「武器派生攻击音效」的原生 DLL 插件。

**独立插件**：不依赖 Lua 脚本引擎、不依赖游戏音频引擎，也不调用游戏托管音频函数。
只需一个公认的加载前置（如 **Stracker's Loader** / 狩技 mod 盒子）被注入即可运行。

## 📥 下载 / Release

[![GitHub release](https://img.shields.io/github/release/2749478981/WeaponSoundEnhance.svg?style=flat-square)](https://github.com/2749478981/WeaponSoundEnhance/releases)
[![GitHub stars](https://img.shields.io/github/stars/2749478981/WeaponSoundEnhance.svg?style=flat-square)](https://github.com/2749478981/WeaponSoundEnhance)
[![License](https://img.shields.io/github/license/2749478981/WeaponSoundEnhance.svg?style=flat-square)](LICENSE)

- 最新发布：<https://github.com/2749478981/WeaponSoundEnhance/releases/latest>

> Release zip 解压后为 `nativePC\plugins\` 结构（dll + ini + 空的 sounds\），拖进狩技 mod 盒子或放入游戏目录即可。

---

## ✨ 功能

- **派生攻击触发音效**：玩家做组合/派生动作时，按配置匹配到对应动作，播放一条 wav。
- **每条动作多条音效**：`Sound` 分号分隔多条，触发时**随机抽一条**；缺一条会自动试同条目里存在的另一条。
- **并发叠播**：多条音效可同时叠加，互不掐断；上限 6 个声部防卡顿。
- **固定音效 F**：`路径|延时|音量|F`，命中该动作时**总是播放**，同条目其余未固定音效仍随机抽一条同时叠播。
- **每条音效独立音量**：软件增益，只改这条音效响度，**不动游戏总音量**。
- **太刀刃时音效**：`Sound:none/white/yellow/red=` 按当前气刃等级（无/白/黄/红）播放不同音效；未配置的刃时回退默认 `Sound=`。刃级直接从游戏内存读取（ini 可覆盖偏移）。
- **条件判定**：命中动作后不立刻出声，而是盯一段时间再按结果挑音效池——可以做「这招打中了没有」「太刀掉刃了没有」这种必须观察一会儿才知道结果的触发。条件写成 `dmg>0 & dAura>=0` 这样的表达式，每条条件有自己独立的音效池。详见下方[条件判定](#条件判定)。
- **动作组 Group=**：同一招跨越多个触发点（判定帧、升刃前后帧、多 LMT）时填相同组名，整招只响一次，刃级以首个触发瞬间为准。
- **每武器配置组合**：每个武器可有多个命名组合（`[WeaponW:名]`），`[Active]` 决定每武器当前用哪个；切换只影响该武器。GUI 下拉或游戏内 `Ctrl+F11` 切换。
- **游戏内热键**：开关 / 重载 / 音量 / 额外音效 / 切换组合。
- **游戏聊天框指令**：聊天框打 `/wse ...` 即可改配置。
- **自动重采样**：任意采样率 PCM wav 先转成标准 44.1kHz，保证能播。
- **内存安全**：所有游戏内存读取都做页级校验，地址失效只跳过本轮，不崩溃。

---

## 📦 目录结构

```
WeaponSoundEnhance/
├─ WeaponSoundEnhance.cpp     主源码（玩家侦测 + waveOut 多缓冲播放 + 热键/指令）
├─ WeaponSoundEnhance.ini     配置模板（派生动作 → 音效清单）
├─ WeaponSoundEnhance.vcxproj MSVC 工程（VS 2019/2022，v142）
├─ CMakeLists.txt             CMake 工程（可选）
├─ build.bat                  一键构建脚本
├─ sounds/                    把 wav 放这里
└─ README.md
```

> 编译产物在 `out\x64\Release\WeaponSoundEnhance.dll`。

---

## 🛠 构建

### 方法 A：双击 build.bat
需要 Visual Studio（含「使用 C++ 的桌面开发」）。脚本自动定位 MSBuild 并编译到 `out\x64\Release\WeaponSoundEnhance.dll`。

### 方法 B：Visual Studio
用 VS 打开 `WeaponSoundEnhance.vcxproj`，选 `Release | x64`，生成即可。

### 方法 C：CMake
```bat
cmake -B build -A x64
cmake --build build --config Release
```

> 源码含中文注释，需要 `/utf-8` 编译（vcxproj / CMakeLists 已配置）。

---

## 🎮 安装与使用

### 从 Release 下载的 zip（大部分玩家用这个）
zip 解压后是 **`nativePC\plugins\`** 结构：

```
nativePC\plugins\
├─ WeaponSoundEnhance.dll     插件本体
├─ WeaponSoundEnhance.ini     配置模板
└─ sounds\                    空，放入你的 wav 音效
```

使用步骤：
1. 把整个 `nativePC\plugins\` 放进游戏根目录（或用**狩技 mod 盒子**把 zip 直接拖进去安装）。
2. **需要怪猎前置**：请使用 Stracker's Loader 前置。
3. 把你想要的音效（**wav**，标准 PCM 16 位）放进 `sounds\`。
4. 打开 `WeaponSoundEnhance.ini` 添加 `[AttackN]`（见下节配置），或用配套 GUI 编辑。
5. 进游戏，拿对应武器做派生攻击即可触发。

> ini 为**干净模板**，不含预置攻击条目，请按下方配置说明自行添加。

### 从源码构建
1. 编译得到 `WeaponSoundEnhance.dll`（见上一节）。
2. 把 `WeaponSoundEnhance.dll`、`WeaponSoundEnhance.ini`、`sounds\` 一起放进游戏 `nativePC\plugins\`。
3. 其余同上。

---

## ⚙️ 配置（WeaponSoundEnhance.ini）

```ini
[WeaponSoundEnhance]
PlayerRoot=0x1450139A0   ; 15.23.00 玩家基址（换版本改这里）
PollMs=60                ; 轮询间隔(ms)
DebounceMs=120           ; 同一动作最短触发间隔(ms)
Volume=50                ; 音量 0..100（软件增益，只影响本插件音效）
Enabled=1                ; 总开关
MoreSounds=1             ; 旧开关：1=条目内随机播，0=只播第一条（仅对无固定音效的旧条目）
ChatEcho=1               ; 聊天栏回显
ChatCommands=1           ; 开启 /wse 聊天框指令
Hotkeys=1                ; 游戏内热键总开关
Debug=0                  ; 调试日志（每次播放/心跳写入 log）
GaugePtrOff=0x76B0       ; 太刀气刃对象偏移
GaugeValOff=0x2370       ; 太刀气刃等级偏移

[Hotkeys]                ; 17=Ctrl；数字是 Windows 虚拟键码
ModifierKey=17
ReloadKey=116            ; Ctrl+F5 重载 ini
VolUpKey=38              ; Ctrl+↑ 音量+5
VolDownKey=40            ; Ctrl+↓ 音量-5
SetVolKey=119            ; Ctrl+F8 音量设为 SetVolValue
SetVolValue=50
ToggleKey=120            ; Ctrl+F9 开关
MoreKey=121              ; Ctrl+F10 切换额外音效
ComboKey=122             ; Ctrl+F11 切换当前武器配置组合
```

### 动作条目
```ini
[Attack1]                ; 第 1 个派生动作
WeaponType=3             ; 武器类型(0..13)，-1=任意
ActionLMT=-1             ; 动作 LMT，-1=不限
FSMId=11                 ; 状态机 ID，-1=不限
Sound=sounds/a.wav; sounds/b.wav            ; 多条分号分隔，随机抽一条
```

- 多 LMT：`LMT=49265,49256`（命中任一即触发）。
- 固定音效：`Sound=sounds/hit.wav|0|100|F; sounds/other.wav`（`F` 者恒播，未固定者随机一条同播）。
- 动作组：`Group=气刃斩1`（同招多帧只响一次）。
- 刃时音效（太刀）：`Sound:white=sounds/白1.wav|0|100|F; sounds/白2.wav`（还有 `none/yellow/red`；未配置回退默认 `Sound=`）。

### 条件判定

普通条目是「匹配到动作 → 立刻播音效」。有些需求没法这样做：
*这一发真蓄打中了没有？* *这个大居掉刃了没有？* —— 动作刚开始时结果还没发生。

配上 `CheckTimeoutMs` 之后，匹配到动作只是**开一个观察窗**，插件在窗口里持续读
伤害/气刃等变化，再按条件挑该播哪个音效池。

```ini
[Attack90]
Name=真蓄命中判定
WeaponType=0
LMT=49298,49341,49342,49427,49428,49429
CheckDelayMs=1200            ; 这之前的伤害不算数（排掉真蓄第一段）
CheckTimeoutMs=3500          ; 窗口上限
CheckEndOn=action            ; 动作结束(含被打断)也作为判定时机
CheckOffsetMs=150            ; 在「实测最晚出伤时刻」之上留的余量
Sound:dmg>0 = sounds/hit.wav ; 条件成立 -> 播这个
Sound        = sounds/miss.wav ; 都不成立 -> 兜底
```

| 键 | 含义 |
| --- | --- |
| `CheckDelayMs` | 动作开始后这么多毫秒之内的变化不计入。用来排除多段攻击里前面那段。 |
| `CheckTimeoutMs` | 窗口上限；`0` = 不启用判定，行为与旧版完全一致。 |
| `CheckEndOn` | `action` = 动作结束（含被打断）也作为判定时机；`time` = 只看时间。 |
| `CheckOffsetMs` | 判定点 = 该招**实测最晚一次出伤的时刻** + 这个余量。插件自己记录并更新最晚出伤时刻，不用手填。只接受正数。 |

**可用变量**

| 变量 | 含义 |
| --- | --- |
| `dmg` | 窗口内**自己**打出的伤害。只统计本人，不含队友，联机下不会误判。 |
| `aura` / `dAura` | 太刀气刃等级 0~3 / 它的变化量（掉刃为负、升刃为正）。 |
| `charge` / `dCharge` | 大剑蓄力等级 / 它的变化量。 |
| `lmt` / `fsm` / `fsmTarget` | 当前动作。 |
| `ms` | 窗口已经过去多少毫秒。 |

比较符 `>` `>=` `<` `<=` `==` `!=`，用 `&`（且）和 `|`（或）连接，`&` 优先级更高，暂不支持括号。

**判定时机**：`Sound:` 是一成立就播，适合已成定局的条件；`SoundEnd:` 只在窗口结束时评，
适合还可能被推翻的条件。太刀大居正好两种都要——

```ini
Sound:dAura<0  = sounds/fail.wav   ; 掉刃已成定局，立刻出声
SoundEnd:dmg>0 = sounds/success.wav ; 「打出伤害」在掉刃前也成立，得等窗口结束才算数
Sound           = sounds/fail.wav   ; 完全落空
```

条件按写的顺序依次评，第一条成立的赢；都不成立就播兜底的 `Sound=`。

> GUI 里这一套有预设，不用手写表达式，见下方「配置工具」一节。

### 每武器配置组合
```ini
[Active]
W3=太刀-主
W8=斩斧-拳

[Weapon3:太刀-主]
[Attack20]
WeaponType=3
FSMId=11
LMT=49265,49256
Sound:sounds/主1.wav

[Weapon3:太刀-备用]
[Attack21]
...
```
- 旧式不带 `[WeaponW:名]` 段的 `[AttackN]` 属于该武器的"默认"组合。
- `[Active]` 决定每武器当前用哪个组合；无 `[Active]` 行则用默认组合。
- 切换只改对应武器的 `[Active]` 行，其它武器配置不变。

> 武器类型表：0大剑、1片手、2双刀、**3太刀**、4大锤、5笛子、6长枪、7铳枪、8斩斧、9盾斧、10虫棍、11弓箭、12轻弩、13重弩。

---

## 🖥 配置工具（WeaponSoundEnhanceGUI.exe）

- 独立窗口编辑每武器动作条目；按**默认音效 / 无刃时 / 白刃时 / 黄刃时 / 红刃时**折叠编辑。
- 每条音效可勾"固定"、调延时/音量、试听、浏览添加；支持多 LMT 与「动作组」。
- 左侧武器树下按武器切换**组合**、**新增组合**；非太刀武器不显示刃时音效。
- 工具栏"启用热键"开关；编辑为此插件与游戏内配置保持同一套格式。
- **条件判定编辑器**：条目里选个预设（太刀登龙 / 太刀大居 / 大剑真蓄）就把动作、时间参数、条件全部填好，只剩给每种结果挑 wav；勾"高级设置"可以用下拉框自己搭条件（变量 / 比较符 / 数值 + 并且·或者），不用碰表达式语法。手写在 ini 里的表达式界面认不出来时会原样保留，不会被改写掉。
- **共享动作 ID 库**：`FSM/LMT 查询`窗口底部 —「导出实测ID」把实时捕获历史导出为 `fsm_db_submission.csv`；「导入CSV合并」把别人分享的 ID 合进本地 `fsm_db.csv`；「提交到共享库」导出+复制并打开提交页，粘贴即可贡献；「获取最新库」从仓库下载最新 `fsm_db.csv`。**有人测过一次，别人就不用再逐招试了。**

> GUI 独立程序，可单独使用，也可仅用于生成 ini。

### 共享动作 ID 库（fsm_db.csv）

- 仓库根目录的 [`fsm_db.csv`](fsm_db.csv) 是社区共享的动作 ID 库，格式 `weapon,fsm,lmt,name`。
- 本地放一份到 GUI 的 exe 同目录即可被加载（会与内置数据合并、自动去重）。
- 贡献流程：GUI「导出实测ID」/「提交到共享库」→ 在提交页粘贴 CSV（或附件上传）→ 合并进 `fsm_db.csv` → 其他人「获取最新库」即可拿到。
- ID 会被用来给条目/查询显示动作名，也能帮别人少走弯路地定位 FSM/LMT。

---

## ⌨️ 游戏内热键

| 键 | 效果 |
|---|---|
| Ctrl+F5 | 重载 ini |
| Ctrl+↑ / ↓ | 音量 +5 / -5 |
| Ctrl+F8 | 音量设为 SetVolValue |
| Ctrl+F9 | 开关音效 |
| Ctrl+F10 | 切换“额外音效” |
| Ctrl+F11 | 切换当前武器配置组合 |

> 关闭 `Hotkeys=0` 后以上热键全部失效；`/wse` 聊天指令不受影响。

## 💬 游戏聊天框指令

在游戏聊天框输入并发送：

| 指令 | 效果 |
|---|---|
| `/wse reload` | 重载 ini |
| `/wse on` / `/wse off` | 开关音效 |
| `/wse more` / `/wse one` | 切换额外音效（仅旧条目） |
| `/wse vol 50` | 音量设为 50 |
| `/wse vol+` / `/wse vol-` | 音量 ±5 |
| `/wse help` | 显示命令 |

---

## 🧠 工作原理（给二次开发者）

- **玩家侦测**：后台线程轮询玩家内存：
  - `Manager = *(PlayerRoot)`；`Entity = *(Manager + 0x50)`
  - 动作 LMT `= *( *(Entity+0x468) ) + 0xE9C4`
  - FSM `= *(Entity + 0x6278)`
  - 武器数据 `= *( *( *(Entity+0xC0)+0x8 )+0x78)`，类型在 `+0x2E8`
  - 太刀刃级 `= *( *(Entity+GaugePtrOff) + GaugeValOff)`
- **触发去重**：每条目 `inMatch` 锁存（只在匹配沿触发）；动作组 `Group` 整招一次；同音效文件 400ms 冷却。
- **播放**：`winmm waveOut` 多缓冲 + 软件增益 —— 每条音效独立线程/句柄，可并发，音量按样本缩放。
- **兼容性保险**：所有读取先 `mem::IsReadable`（VirtualQuery）校验，非法地址安全跳过。

---

## 📜 免责 / 许可

- 仅供学习交流。使用前建议备份存档。
- 因游戏版本差异导致的地址失效、以及音效素材版权，由使用者自行承担。

---

## 🔖 参考

- 玩家/武器/聊天地址结构与取值约定参考 `mhw-toolkit`（eigeen/mhw-toolkit）。
- 武器类型表为游戏内置序号。
