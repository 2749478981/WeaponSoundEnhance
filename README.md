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

> Release zip 解压后为 `nativePC\plugins\` 结构（`WeaponSoundEnhance.dll` + `WeaponSoundEnhance\` 子目录：`*.ini.template`、`fsm_db.csv`、GUI、空的 `sounds\`），拖进狩技 mod 盒子或放入游戏目录即可。

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
zip 解压后是 **`nativePC\plugins\`** 结构。**只有 DLL 放在 `plugins\` 根，其余文件都在同名子目录里**，避免文件挤在一起：

```
nativePC\plugins\
├─ WeaponSoundEnhance.dll                    ← 插件本体（必须在这里，前置才认）
└─ WeaponSoundEnhance\                       ← 数据目录（其余全在这）
   ├─ WeaponSoundEnhance.ini.template        ← 随包配置模板
   ├─ fsm_db.csv                             ← 基础动作 ID 库（可被「获取最新库」更新）
   ├─ WeaponSoundEnhanceGUI.exe              ← 配置工具
   ├─ 说明.txt
   └─ sounds\                                ← 把你的 wav 放这里
```

使用步骤：
1. 把整个 `nativePC\plugins\` 放进游戏根目录（或用**狩技 mod 盒子**把 zip 直接拖进去安装）。
2. **需要怪猎前置**：请使用 Stracker's Loader 前置。
3. 把你想要的音效（**wav**，标准 PCM 16 位）放进 `WeaponSoundEnhance\sounds\`。
4. 打开 `WeaponSoundEnhance\WeaponSoundEnhance.ini` 添加 `[AttackN]`（见下节配置），或用 `WeaponSoundEnhance\WeaponSoundEnhanceGUI.exe` 编辑。
   首次运行若没有 `WeaponSoundEnhance.ini`，插件/工具会**自动由 `*.ini.template` 生成一份**。
5. 进游戏，拿对应武器做派生攻击即可触发。

> ini 为**干净模板**，不含预置攻击条目，请按下方配置说明自行添加。
> **旧布局仍然兼容**：如果你的 `WeaponSoundEnhance.ini` 与 DLL 同目录（v2.2 及更早的装法），插件会继续用那一份；音效放在旧的 `plugins\sounds\` 也照旧能读，不用搬家。

### 升级指南（不丢配置）

- **更新时只需要覆盖 DLL 和数据目录里的 `fsm_db.csv` / `*.ini.template` / GUI**；`WeaponSoundEnhance.ini` 是你自己的，别删、别覆盖。
- 已经误换成新模板、想找回旧动作：用 GUI 工具栏 **「合并旧版ini」** 选旧 ini，动作会被**合并**（不替换）进当前配置，重复条目自动跳过。
- 你实测的动作 ID 存在 `WeaponSoundEnhance\fsm_db_user.csv`，**任何更新都不会动它**。

### 从源码构建
1. 编译得到 `WeaponSoundEnhance.dll`（见上一节）和 `gui\out\x64\Release\WeaponSoundEnhanceGUI.exe`。
2. `WeaponSoundEnhance.dll` 放进游戏 `nativePC\plugins\`；`WeaponSoundEnhanceGUI.exe`、`WeaponSoundEnhance.ini`、`fsm_db.csv` 与 `sounds\` 放进 `nativePC\plugins\WeaponSoundEnhance\`（或用 `refresh_dist.ps1` 一键打包出 `dist\`）。
3. 其余同上。

### 常见问题
- **默认组合的条目保存后跑到别的组合里 / 某把武器读不出默认组合**：v2.8 起已修（旧版保存时默认组合没写 `[Weapon<类型>]` 段头，条目会掉进上一个命名组合的段里；武器不对还会串到别的武器，例如长枪条目变成「武器6的组合 DIO」）。已经存坏的配置：用新版 GUI 打开（状态栏会提示“已修正 N 条”）再保存一次即恢复。手写 ini 时记住：默认组合的条目放 `[Weapon3]` 段，命名组合放 `[Weapon3:组合名]` 段，条目里的 `WeaponType` 要和段头武器一致。
- **切了组合没反应 / 改了配置游戏里没变化**：先看 GUI 状态栏的 **配置文件** 路径 —— 必须是游戏读取的那份 `<游戏目录>\nativePC\plugins\WeaponSoundEnhance\WeaponSoundEnhance.ini`。v2.7 起 GUI 默认就加载这一份，路径不一致时会红字提示并给「切换到游戏目录的 ini」按钮。另外配置里**一条条目都没有**时，游戏里自然什么都切不动。
- **Ctrl+F11 切组合没反应**：v2.7 起一定会有回显（组合名 / 只有 1 个组合 / 该武器没有条目 / 未进场景），并且切完会写回 ini 的 `[Active]`，重载和重开游戏都保持。热键不好用可以直接在聊天框打 `/wse combos` 看列表、`/wse combo 组合名` 切换。
- **「打开 ini / 合并旧版ini」报读取失败**：v2.6 起已修 —— GUI 以前用 ANSI 路径 API 读写文件，路径里只要有中文（桌面的「怪猎配置」文件夹、中文用户名目录、中文 mod 目录）或路径超过 260 字符就会“找不到文件”。现在 ini/csv 读写与 DLL 的 ini/wav/日志都走宽字符 + 长路径 API。
- **改了 LMT 却“没生效”**：LMT 框里原本就有旧值，直接把 `-1` 接在后面会变成 `49265-1`（旧版本会被 `atoi` 静默当成 `49265`）。现在编辑窗口有 **「LMT 不限」勾选框**，要“该 FSMId 的所有动作都触发”直接勾它；LMT 框下方会实时显示解析结果，填错会红字拦截。v2.5 起 `LMT` 只接受整数，`-1 / any / all / * / 不限 / 空` 都表示不限。
- **GUI 显示「已连接」但实时捕获一直是空的**：多半是后台残留了**同名僵尸进程**（游戏崩溃或被强杀后留下的 0 线程进程，也叫 `MonsterHunterWorld.exe`）。v2.4 起 GUI 会自动跳过它们、只连真正在跑的那个，并在状态栏显示所连 `pid` 与失败原因；也可以自己去任务管理器结束掉那些只占 1MB 内存的 `MonsterHunterWorld.exe`。
- **升级后音效全都不响、日志里全是 `wav not found`**：v2.3 起音效应放在 `plugins\WeaponSoundEnhance\sounds\`；还留在旧的 `plugins\sounds\` 也能读（v2.4 起自动回退，日志会提示建议搬移）。另外音效是**启动时预载**的，放好文件后重进游戏或按 `Ctrl+F5` 重载。
- **连不上游戏进程**：若游戏以管理员身份运行，本工具也需要右键「以管理员身份运行」。

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
ChargeValOff=0x2358      ; 大剑蓄力等级偏移（与刃级同对象）
FsmTargetOff=0x6274      ; FSM target 偏移
QuestRoot=0x14500ED30    ; 任务结构入口（累计伤害用）
QuestDmgOff=0x17088      ; 任务累计伤害偏移（只含本人）

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
- FSM 目标层：`FSMTarget=3`。FSM 是 `(target, id)` 二元组，不同层里 id 可能重号（实测 fsmID 102 在通用层是别的动作、太刀层才是大居）；填上 target 可避免跨层误触发，不写（`-1`）则只比 `FSMId`（旧行为）。
- 条件判定：见下节 `CheckTimeoutMs` / `Sound:<表达式>`。
- 刃时音效（太刀）：`Sound:white=sounds/白1.wav|0|100|F; sounds/白2.wav`（还有 `none/yellow/red`；未配置回退默认 `Sound=`）。注意：**判定条目不走刃时池**，它只在自己的条件池 + 默认 `Sound=` 里选。

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

**判定条目的几点注意**

- 判定条目**不走动作组** `Group=`，也**不受全局 Debounce 限制** —— 观察窗本身负责去重（同一次判定只会出一次声）。所以「同招多帧只响一次」这类需求，判定条目不需要、也暂不支持再叠 `Group=`。
- 判定条目**不参与刃时池**（`Sound:white/...` 等）：它只在"自己的条件池 + 默认 `Sound=`"里挑。想按刃级分音效就把它写成普通条目或写进条件表达式（用 `aura`/`dAura`）。
- 想让所有条件都等到窗口结束再评，可加 `CheckMode=final`（等价于把每条都写成 `SoundEnd:`）。
- 手写的高级键（`CheckMode`、`FSMTarget`、`ChargeValOff`/`QuestRoot`/`QuestDmgOff` 等）GUI 会原样保留、保存时不会丢。

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
Sound=sounds/主1.wav

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
- **共享动作 ID 库**：点工具栏 **「上传ID」** 打开独立窗口 —「导出实测ID」把实时捕获历史导出为 `fsm_db_submission.csv`；「导入CSV合并」把别人分享的 ID 合进本地 `fsm_db.csv`；「提交到共享库」导出+复制并打开提交页，粘贴即可贡献；「获取最新库」从仓库下载最新 `fsm_db.csv`。**有人测过一次，别人就不用再逐招试了。**

> GUI 独立程序，可单独使用，也可仅用于生成 ini。

### 共享动作 ID 库（fsm_db.csv / fsm_db_user.csv）

ID 数据是**独立文件**，和 ini 完全分开，升级互不影响。schema 固定为 `weapon,fsm,lmt,name`
（解析时忽略多余列、容忍缺列，所以以后只会**追加行**，不会改结构）：

| 文件（在数据目录 `WeaponSoundEnhance\` 下） | 作用 | 升级时 |
| --- | --- | --- |
| `fsm_db.csv` | **基础库**：随包附带 + 「获取最新库」下载 | 可被覆盖/更新 |
| `fsm_db_user.csv` | **用户库**：你「导出实测ID」或「导入CSV合并」的内容 | **永不被动**，且优先级高于基础库 |

- 仓库根目录的 [`fsm_db.csv`](fsm_db.csv) 就是基础库的来源；文件缺失/为空时，工具会用内置兜底数据**播种**出一份，所以数据始终在文件里而不是写死在代码里。
- 贡献流程：GUI 工具栏「上传ID」→「导出实测ID」（进用户库，立刻生效）/「提交到共享库」（打开提交页，粘贴即可）→ 合并进仓库 `fsm_db.csv` → 其他人「获取最新库」。
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
