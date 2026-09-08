# WeaponSoundEnhance v2.1

给《怪物猎人：世界 / 冰原》(15.23.00) 增加武器派生攻击音效的原生 DLL 插件。

## 安装
1. 把压缩包解压后得到的 `nativePC\plugins` 文件夹放进游戏根目录（或用狩技 mod 盒子把 zip 直接拖入安装）。
2. 需要怪猎前置：Stracker's Loader（`dinput8.dll` / `loader.dll`）。
3. 把音效 wav（PCM 16 位）放进 `plugins\sounds\`。
4. 用记事本或 `WeaponSoundEnhanceGUI.exe` 编辑 `WeaponSoundEnhance.ini`（干净模板，需自行添加 `[AttackN]`）。
5. 进游戏做派生攻击触发；`Ctrl+F5` 重载配置立即生效。

## 功能
- 指定派生动作命中时播放一条 wav；每条动作可配多条（分号分隔），触发时随机抽一条。
- 固定音效：写法 `路径|延时ms|音量|F`，命中时总是播放；同条目其余未固定音效随机抽一条，同时叠播。同文件 400ms 内不重复。
- 太刀刃时音效：`Sound:none/white/yellow/red=…` 按当前气刃等级（无/白/黄/红）播放不同音效；未配置的刃时回退默认 `Sound=`。刃级从游戏内存读取（ini 的 `GaugePtrOff`/`GaugeValOff` 可调）。
- 动作组：同一招的多个触发条目填相同 `Group=` 名 → 整招只响一次，刃级以首个触发瞬间为准。
- 每武器配置组合：`[WeaponW:组合名]` 段定义某武器的一套映射，`[Active]` 决定每武器当前用哪个；切换只影响该武器。游戏内 `Ctrl+F11` 切当前武器组合，GUI 里用武器树下的「组合」下拉切换/新增。
- 支持并发叠播（上限 6 声部）、每条音效独立音量、自动重采样到 44.1kHz；内存读取全部页级校验。
- 游戏内热键总开关 `Hotkeys=1/0`；`/wse` 聊天指令可改配置。

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
GaugePtrOff=0x76B0
GaugeValOff=0x2370

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
LMT=49265,49256
Sound=sounds/气刃斩1.wav
Sound:white=sounds/气刃斩1_白1.wav|0|100|F; sounds/气刃斩1_白2.wav
Sound:red=sounds/气刃斩1_红.wav
```

## 游戏内控制
- 热键：`Ctrl+F5` 重载 / `Ctrl+F9` 开关 / `Ctrl+F10` 额外音效(旧条目) / `Ctrl+↑↓` 音量 / `Ctrl+F8` 设定音量 / `Ctrl+F11` 切换当前武器组合。
- 聊天框：`/wse help | reload | on | off | more | one | vol N | vol+ | vol-`。

## GUI 工具（WeaponSoundEnhanceGUI.exe）
- 独立窗口编辑条目；按 默认音效/无刃时/白刃时/黄刃时/红刃时 折叠编辑；每条可勾固定、调延时/音量、试听、浏览添加；支持多 LMT 与动作组。
- 非太刀武器不显示刃时音效；工具栏有「启用热键」开关。

## 兼容与免责
- 兼容 15.23.00；ini 为干净模板（无预置攻击条目）；v1 旧配置若无新键仍可直接使用。
- 音效素材版权与游戏版本差异导致的地址失效由使用者自行承担。
