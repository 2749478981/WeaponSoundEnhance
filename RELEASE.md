# WeaponSoundEnhance v2.3

给《怪物猎人：世界 / 冰原》(15.23.00) 增加武器派生攻击音效的原生 DLL 插件。

## 更新内容（v2.3）

### 🔄 目录布局：除 DLL 外全部收进子目录
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
> **旧布局继续兼容**：ini 与 DLL 同目录的老装法会被自动识别，不用搬家。

### ✨ 新增
- **配置模板机制**：发布包只带 `WeaponSoundEnhance.ini.template`；你的 ini 不存在时自动生成一份。**升级只覆盖模板，不再覆盖你的配置**，不用重填。
- **「合并旧版ini」**（GUI 工具栏）：选了旧 ini 后把里面的动作**合并**进当前配置（去重、不替换），换新版后一条都不会丢。
- **动作 ID 独立成文件、结构冻结**：
  - `fsm_db.csv` = 基础库（随包 + 「获取最新库」更新）；`fsm_db_user.csv` = 用户库（**永不被更新覆盖**，优先级更高）；
  - schema 固定为 `weapon,fsm,lmt,name`，解析忽略多余列、容忍缺列 → 以后只追加行、不改结构；
  - 文件缺失/为空时用内置数据自动播种，所以 ID 数据始终在文件里，改 ID 不需要改代码。
- **GUI「上传ID」窗口**：导出实测ID（进用户库、立刻生效，并另存提交用 CSV）/ 导入CSV合并（进用户库）/ 提交到共享库（打开提交页）/ 获取最新库（只更新基础库）。

### 🐞 修复
- 修复游戏内 `Ctrl+F11`（切换武器配置组合）闪退：热键在已持有配置锁的情况下又调用了会再次加锁的重建函数，`std::mutex` 非递归 → 未定义行为。已拆成「已持锁 / 自行加锁」两个版本。

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
