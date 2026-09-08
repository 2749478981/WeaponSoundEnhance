# WeaponSoundEnhance v2.1

给《怪物猎人：世界 / 冰原》(15.23.00) 增加武器派生攻击音效的原生 DLL 插件。

## 更新内容

### ✨ 新增
- **每武器配置组合**：`[WeaponW:组合名]` + `[Active]`，每个武器可有多个命名组合并随时切换；切换只影响该武器，其它武器不变。GUI 武器树下「组合」下拉/新增，或游戏内 `Ctrl+F11` 循环切换。
- **太刀刃时音效**：`Sound:none/white/yellow/red=` 按当前气刃等级（无/白/黄/红）播放不同音效；未配置的刃时回退默认 `Sound=`。刃级从游戏内存读取（`GaugePtrOff`/`GaugeValOff` 可调）。
- **固定音效 F**：`路径|延时ms|音量|F`，命中该动作时总是播放；同条目其余未固定音效随机抽一条，同时叠播。
- **动作组 Group=**：同一招的多个触发条目（判定帧、升刃前后帧、多 LMT）填相同组名 → 整招只响一次，刃级以首个触发瞬间为准。
- **启用热键开关** `Hotkeys=1/0`；GUI「启用热键」勾选。
- **配置工具 GUI**：独立窗口编辑条目、按刃时池折叠、固定标记、多 LMT、延时/音量/试听/浏览添加。
- **启动预载全部 wav**（首击零延迟）；waveOut 并发上限 6 声部防卡顿。
- **一个动作多个 LMT**：`LMT=49265,49256` 命中任一即触发。

### 🐞 修复
- 修复同一条目多条音效"全部一起播"、固定音效连续重叠、气刃4 命中升刃重复播放两刃级音效（动作锁存失效导致重复触发）。
- 修复同一招多个触发瞬间/判定帧/多 LMT 的重复触发：动作组 + 逐条锁存 + Debounce。
- 修复编辑弹窗被主界面遮挡、无法移出主窗口之外、右下角缩放三角问题。
- 修复非太刀武器显示无意义"刃时"文案（差异化：非太刀只显示默认音效）。
- 修复添加/移除音效时编辑器崩溃（ImGui SameLine 访问违例）：变更改为下一帧帧首应用，不在控件提交帧内扩容/改容器。
- 修复配置写回丢新键；旧 v1 配置无需改动即可使用。

### 🔄 变更
- 发布 ini 改为**干净模板**，不再预置任何攻击条目。
- 移除已失效的 `Playback`（播放方式）选项，播放统一走 waveOut 多声部并发。

## 安装
1. 把压缩包解压后得到的 `nativePC\plugins` 文件夹放进游戏根目录（或用狩技 mod 盒子把 zip 直接拖入安装）。
2. 需要怪猎前置：Stracker's Loader（`dinput8.dll` / `loader.dll`）。
3. 把音效 wav（PCM 16 位）放进 `plugins\sounds\`。
4. 用记事本或 `WeaponSoundEnhanceGUI.exe` 编辑 `WeaponSoundEnhance.ini`（干净模板，需自行添加 `[AttackN]`）。
5. 进游戏做派生攻击触发；`Ctrl+F5` 重载配置立即生效。

## 游戏内控制
- 热键：`Ctrl+F5` 重载 / `Ctrl+F9` 开关 / `Ctrl+F10` 额外音效(旧条目) / `Ctrl+↑↓` 音量 / `Ctrl+F8` 设定音量 / `Ctrl+F11` 切换当前武器组合。
- 聊天框：`/wse help | reload | on | off | more | one | vol N | vol+ | vol-`。

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

## GUI 工具（WeaponSoundEnhanceGUI.exe）
- 独立窗口编辑条目；按 默认音效/无刃时/白刃时/黄刃时/红刃时 折叠编辑；每条可勾固定、调延时/音量、试听、浏览添加；支持多 LMT 与动作组。
- 非太刀武器不显示刃时音效；工具栏有「启用热键」开关。

## 兼容与免责
- 兼容 15.23.00；ini 为干净模板（无预置攻击条目）；v1 旧配置若无新键仍可直接使用。
- 音效素材版权与游戏版本差异导致的地址失效由使用者自行承担。
