# WeaponSoundEnhance v2.7

给《怪物猎人：世界 / 冰原》(15.23.00) 增加武器派生攻击音效的原生 DLL 插件。

## 更新内容（v2.7）

### 🐞 修复
- **「GUI 切了组合、重载没反应」**：最常见的原因是**GUI 编辑的 ini 和游戏读取的不是同一份**（比如 GUI 是从发布包解压目录里打开的，或手动「打开 ini」选了别处的文件）。
  - GUI 现在**优先加载游戏实际读取的那份** `<游戏目录>\nativePC\plugins\WeaponSoundEnhance\WeaponSoundEnhance.ini`（而不是 exe 同目录的）；
  - 状态栏**始终显示当前配置文件全路径**；一旦不是游戏那份，会红字提示并给一个 **「切换到游戏目录的 ini」** 按钮（那份存在就直接加载，不存在就把当前配置保存过去）；
  - 当前配置**一条动作条目都没有**时也会提示（这种配置在游戏里当然什么都切不动、也不会响）。
- **「Ctrl+F11 切组合没反应」**：
  - 现在**一定有反馈**：`wse combo -> 白刃流（2 个可用）`；只有 1 个组合/没有条目/没进场景时也会说明原因，不再默默什么都不做；
  - 切完会**写回 ini 的 `[Active]` 段**（只改这一行，其它内容原样保留）—— 以前只改内存，所以 `Ctrl+F5` 重载或重开游戏就跳回旧组合，看起来像“切了没用”。可用 `PersistActiveCombo=0` 关掉写回；
  - 新增聊天框命令：`/wse combo`（切下一个）、`/wse combo 组合名`（按名字切）、`/wse combos`（列出该武器的组合并标出当前），热键不好使时也能切。

## 更新内容（v2.6）

### 🐞 修复
- **「旧版 ini 读取失败」**：GUI 内部路径是 UTF-8，但读写 ini/csv 用的是 `std::ifstream` / `std::ofstream`，MSVC 会按 **ANSI** 代码页解释这些字节 —— 只要路径里有中文（桌面上的「怪猎配置」文件夹、中文用户目录、中文 mod 目录）就变成“文件不存在”，于是「打开 ini / 合并旧版ini」报**读取失败**，保存也会静默写到别处或失败。
  - 现在 GUI 的所有 ini/csv 读写、模板复制、文件存在判断统一走宽字符 API（新增 `fsutil.h`），中文路径正常；
  - 顺带支持 **超长路径**（>260 字符，`\\?\` 前缀）：mod 管理器/深层文件夹解出来的路径也能读写；
  - DLL 侧同样给 ini / wav / 日志的打开加了长路径支持。
  - 提示：ini **文件内容**本身没问题（UTF-8 无 BOM 即可），之前失败的只是“路径”。

## 更新内容（v2.5）

### 🐞 修复
- **「编辑里把 LMT 改成 -1 好像没用」**：编辑已有条目时 LMT 框里本来就有旧值（如 `49265`），直接把 `-1` 接在后面会变成 `49265-1`；旧代码用 `atoi` 逐段解析，会被静默截断成 `49265` —— 看着改了，实际没改，于是只能靠「手动新增条目」（新条目 LMT 框是空的，填 `-1` 才真的生效）。现在：
  - 编辑窗口新增 **「LMT 不限（该 FSMId 的所有动作都触发）」勾选框**，不用再手打 `-1`；
  - LMT 框有了正式标签，并在下方**实时回显解析结果**（`→ 不限 LMT：只要 FSMId 相同就会触发` / `→ 只匹配 LMT：49265, 49256`）；
  - 填了无法识别的项（如 `49265-1`）会**红字提示并拒绝保存该次编辑**，编辑窗口保持打开，不再静默改错。
- **重叠提示**：同武器 + 同 FSMId 的条目只要 LMT 有交集，就会在同一次动作里各响一次。把某条设成「不限」后，底部状态栏会提示 `⚠ N 组条目重叠会同时触发：…`（这时一般要删掉被它覆盖的那几条，例如 `xxx(起手帧)`）。
- **配置解析同规则**：`LMT` 现在只接受整数，`不限 / any / all / * / -1 / 空` 都表示不限；`49265-1` 这类无法识别的项会被忽略（游戏内 `Debug=1` 时日志会写出来），手写 ini 也不会再被 `atoi` 悄悄截断。

### 🐞 修复（v2.4）
- **GUI「不能捕获」**：同名游戏进程有多个时（游戏崩溃 / 被强杀后残留的 0 线程僵尸进程也占着 `MonsterHunterWorld.exe` 这个名字），GUI 之前会连到**第一个**同名进程上 → 指针链读不通，于是「连上了却什么都抓不到」。现在会遍历所有同名进程、跳过无响应的残留进程，并用玩家指针链逐个校验，只连真正在跑的那个。
- **连接状态说得清**：捕获面板会显示当前连的 `pid`；连不上时直接给出原因（找不到进程 / 打不开进程·可尝试管理员运行 / 只剩残留进程）；检测到多个同名进程时会提示。对不上状态时对照任务管理器即可。
- **旧布局 `sounds\` 自动兼容**：2.3 把数据目录收进了子目录，音效若还放在旧的 `plugins\sounds\`（与 DLL 同级）会报 `wav not found`。现在查找顺序为「数据目录 → 旧位置」，日志里也会提示建议搬移；GUI 的「打开 sounds\」和音效选择框同样兼容旧目录。
- **非 ASCII 路径**：修掉配置头里过时的音效路径说明；提交用的 CSV 读取改用宽字符 API，游戏/工具放在中文路径下也能读。

### ✨ 改进（v2.4）
- 绝对路径音效（`Sound=F:\...\x.wav`）现在可直接使用，不再被当成相对路径拼接。

## 目录布局（v2.3 起）
```
nativePC\plugins\
├─ WeaponSoundEnhance.dll                    ← 只有 DLL 在 plugins 根
└─ WeaponSoundEnhance\                       ← 数据目录
   ├─ WeaponSoundEnhance.ini.template        ← 随包配置模板
   ├─ WeaponSoundEnhance.ini                 ← 你自己的配置（首次自动由模板生成）
   ├─ fsm_db.csv                             ← 基础动作 ID 库
   ├─ fsm_db_user.csv                        ← 你自己的实测/导入 ID（更新不动它）
   ├─ WeaponSoundEnhanceGUI.exe
   ├─ 说明.txt
   └─ sounds\                                ← 音效
```
> **旧布局继续兼容**：ini 与 DLL 同目录的老装法会被自动识别，音效放在旧的 `plugins\sounds\` 也能直接读，不用搬家。

### v2.3 新增（保留）
- **配置模板机制**：发布包只带 `WeaponSoundEnhance.ini.template`；你的 ini 不存在时自动生成一份。**升级只覆盖模板，不再覆盖你的配置**，不用重填。
- **「合并旧版ini」**（GUI 工具栏）：选了旧 ini 后把里面的动作**合并**进当前配置（去重、不替换），换新版后一条都不会丢。
- **动作 ID 独立成文件、结构冻结**：
  - `fsm_db.csv` = 基础库（随包 + 「获取最新库」更新）；`fsm_db_user.csv` = 用户库（**永不被更新覆盖**，优先级更高）；
  - schema 固定为 `weapon,fsm,lmt,name`，解析忽略多余列、容忍缺列 → 以后只追加行、不改结构；
  - 文件缺失/为空时用内置数据自动播种，所以 ID 数据始终在文件里，改 ID 不需要改代码。
- **GUI「上传ID」窗口**：导出实测ID（进用户库、立刻生效，并另存提交用 CSV）/ 导入CSV合并（进用户库）/ 提交到共享库（打开提交页）/ 获取最新库（只更新基础库）。
- 修复游戏内 `Ctrl+F11`（切换武器配置组合）闪退（配置锁重入）。

## 安装
1. 把压缩包解压后得到的 `nativePC\plugins` 文件夹放进游戏根目录（或用狩技 mod 盒子把 zip 直接拖入安装）。
2. 需要怪猎前置：Stracker's Loader（`dinput8.dll` / `loader.dll`）。
3. 音效 wav（PCM 16 位）放进 `plugins\WeaponSoundEnhance\sounds\`。
4. 用记事本编辑 `plugins\WeaponSoundEnhance\WeaponSoundEnhance.ini`（首次会自动由 `.ini.template` 生成），或用 `WeaponSoundEnhanceGUI.exe`。
5. 进游戏做派生攻击触发；`Ctrl+F5` 重载配置立即生效。

## 升级（不丢配置）
- 更新时**只需覆盖 DLL 和数据目录里的 `fsm_db.csv` / `*.ini.template` / GUI**；**不要删/覆盖 `WeaponSoundEnhance.ini`**（那是你的配置）。
- `fsm_db_user.csv`（你的实测 ID）任何更新都不会被动到。
- 万一把 ini 换成新模板了：GUI 工具栏「合并旧版ini」→ 选旧 ini → 动作会合并回来。

## 配置速查
```ini
[WeaponSoundEnhance]
PlayerRoot=0x1450139A0
PollMs=60
DebounceMs=120
Volume=50
Enabled=1
MoreSounds=1
ChatEcho=1
ChatCommands=1
Hotkeys=1
Debug=0
GaugePtrOff=0x76B0      ; 太刀气刃对象偏移
GaugeValOff=0x2370      ; 太刀气刃等级偏移
ChargeValOff=0x2358     ; 大剑蓄力等级偏移
FsmTargetOff=0x6274     ; FSM target 偏移
QuestRoot=0x14500ED30   ; 任务结构入口
QuestDmgOff=0x17088     ; 任务累计伤害偏移

[Hotkeys]
ModifierKey=17   ; Ctrl
ReloadKey=116    ; F5
VolUpKey=38      ; ↑
VolDownKey=40    ; ↓
SetVolKey=119    ; F8
SetVolValue=50
ToggleKey=120    ; F9
MoreKey=121      ; F10
ComboKey=122     ; F11 切换当前武器组合

[Attack1]
WeaponType=3
FSMId=11
FSMTarget=-1           ; 填实际 target 可避免跨层 id 重号
LMT=49265,49256
Sound=sounds/气刃斩1.wav
Sound:white=sounds/气刃斩1_白1.wav|0|100|F; sounds/气刃斩1_白2.wav
```

条件判定示例：
```ini
[Attack90]
Name=真蓄命中判定
WeaponType=0
LMT=49298,49341,49342,49427,49428,49429
CheckDelayMs=1200       ; 这之前的伤害不算数（排掉真蓄第一段）
CheckTimeoutMs=3500     ; 窗口上限；0 = 不启用判定
CheckEndOn=action       ; 动作结束(含被打断)也作为判定时机
CheckOffsetMs=150       ; 在「实测最晚出伤时刻」之上留的余量
Sound:dmg>0 = sounds/hit.wav    ; 条件成立 -> 播这个
Sound       = sounds/miss.wav   ; 都不成立 -> 兜底
```

## 游戏内控制
- 热键：`Ctrl+F5` 重载 / `Ctrl+F9` 开关 / `Ctrl+F10` 额外音效(旧条目) / `Ctrl+↑↓` 音量 / `Ctrl+F8` 设定音量 / `Ctrl+F11` 切换当前武器组合。
- 聊天框：`/wse help | reload | on | off | more | one | vol N | vol+ | vol-`。

## GUI 工具（WeaponSoundEnhanceGUI.exe）
- 独立窗口编辑条目；按 默认音效/无刃时/白刃时/黄刃时/红刃时 折叠编辑；每条可勾固定、调延时/音量、试听、浏览添加；支持多 LMT、动作组与 `FSMTarget`。
- **条件判定编辑器**：预设开箱即用，勾"高级设置"可自由搭条件，不用碰表达式语法。
- **上传ID**（共享动作 ID 库）：导出实测 / 导入合并 / 提交到共享库 / 获取最新库。
- **合并旧版ini**：升级后把旧动作合并回来。
- 非太刀武器不显示刃时音效；工具栏有「启用热键」开关。

## 兼容与免责
- 兼容 15.23.00；ini 为干净模板（无预置攻击条目）；v1/v2.2 旧配置与旧目录布局均可直接沿用。
- 音效素材版权与游戏版本差异导致的地址失效由使用者自行承担。
