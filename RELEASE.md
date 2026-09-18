# WeaponSoundEnhance v2.2.1

给《怪物猎人：世界 / 冰原》(15.23.00) 增加武器派生攻击音效的原生 DLL 插件。

## 更新内容（v2.2.1）

### 🐞 修复
- **修复游戏内 `Ctrl+F11`（切换武器配置组合）闪退**：该热键在**已持有配置锁**的情况下，又调用了会**再次加锁**的重建函数；`std::mutex` 是非递归的，同线程二次加锁属未定义行为（MSVC 下抛 `system_error` → 进程直接终止）。现拆成「已持锁 `RebuildActiveAttacksLocked()`」和「自行加锁 `RebuildActiveAttacks()`」两个版本，热键路径改用前者。

## 更新内容（v2.2）

### ✨ 新增
- **条件判定**（社区贡献）：匹配到动作后不立刻出声，而是开一个观察窗，按结果挑音效池——适合「这招打中了没有」「掉刃了没有」这类必须观察一会儿才知道结果的触发。条件写成 `dmg>0 & dAura>=0` 这样的表达式，每条条件有独立的音效池；`Sound:` 一成立就播，`SoundEnd:` 只在窗口结束时评。GUI 内置预设（太刀登龙 / 太刀大居 / 大剑真蓄），选完只需给每种结果挑 wav。详见 README「条件判定」。
- **共享动作 ID 库**：点工具栏 **「上传ID」** 打开独立窗口 —「导出实测ID」把实时捕获历史导出为 `fsm_db_submission.csv`；「导入CSV合并」把别人分享的 ID 合进本地 `fsm_db.csv`；「提交到共享库」导出+复制并打开提交页；「获取最新库」从仓库下载最新库。**有人测过一次，别人就不用再逐招试了。**
- **`FSMTarget=` 目标层匹配**：FSM 是 `(target, id)` 二元组，不同层里 id 会重号（实测 fsmID 102 在通用层是别的动作、太刀层才是大居）。条目可选填 `FSMTarget=` 一起匹配；不写则只比 `FSMId`（旧行为）。
- **GUI 支持 `CheckMode=final`**（所有条件都等到窗口结束再评），并新增可配偏移 `ChargeValOff` / `FsmTargetOff` / `QuestRoot` / `QuestDmgOff`。

### 🐞 修复
- **GUI 保存不再吞掉插件新增的键**：`ChargeValOff` / `FsmTargetOff` / `QuestRoot` / `QuestDmgOff`（全局）与 `FSMTarget` / `CheckMode`（条目）此前会被 GUI 保存时丢弃，现已解析并写回。
- **`fsmTarget` 偏移改为可配**（`FsmTargetOff`，默认 `0x6274`），原先写死在代码里。
- README 组合示例里的笔误 `Sound:sounds/..` 修正为 `Sound=sounds/..`。

### 🔄 行为说明
- 判定条目**不走动作组 `Group=`、也不受全局 Debounce 限制**（观察窗自身负责去重）。
- 判定条目**不参与刃时池**（`Sound:white/...`），只在「条件池 + 默认 `Sound=`」里挑；想按刃级分流就写进条件表达式（用 `aura` / `dAura`）。

## 安装
1. 把压缩包解压后得到的 `nativePC\plugins` 文件夹放进游戏根目录（或用狩技 mod 盒子把 zip 直接拖入安装）。
2. 需要怪猎前置：Stracker's Loader（`dinput8.dll` / `loader.dll`）。
3. 把音效 wav（PCM 16 位）放进 `plugins\sounds\`。
4. 用记事本或 `WeaponSoundEnhanceGUI.exe` 编辑 `WeaponSoundEnhance.ini`（干净模板，需自行添加 `[AttackN]`）。
5. 进游戏做派生攻击触发；`Ctrl+F5` 重载配置立即生效。

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
- **条件判定编辑器**：预设开箱即用，勾"高级设置"可自由搭条件（变量 / 比较符 / 数值 + 并且·或者），不用碰表达式语法。
- **共享动作 ID 库**：导出实测 / 导入合并 / 提交到共享库 / 获取最新（见上）。
- 非太刀武器不显示刃时音效；工具栏有「启用热键」开关。

## 兼容与免责
- 兼容 15.23.00；ini 为干净模板（无预置攻击条目）；v1 旧配置若无新键仍可直接使用。
- 音效素材版权与游戏版本差异导致的地址失效由使用者自行承担。
