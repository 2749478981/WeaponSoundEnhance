# WeaponSoundEnhance

给《怪物猎人：世界 / 冰原》增加「武器派生攻击音效」的原生 DLL 插件。

**独立插件**：不依赖任何 Lua 脚本引擎、不依赖游戏音频引擎，也不调用游戏托管音频函数。
它只需要一个公认的加载前置（如 **Stracker's Loader** / 狩技 mod 盒子）被注入即可运行。

---

## ✨ 功能

- **派生攻击触发音效**：玩家用武器进行组合/派生动作时，按配置匹配到对应动作，播放一条 wav。
- **每条动作可配多条音效**：`Sound` 用分号分隔多条，触发时**随机抽一条**播放；缺一条会自动改试同条目里存在的另一条。
- **并发叠播**：多条音效可同时叠加，互不掐断。
- **每条音效独立音量**：软件增益，只改变这条音效的响度，**不动游戏总音量**。
- **游戏内热键**：开关 / 重载 / 音量 / 额外音效切换。
- **游戏聊天框指令**：在聊天框打 `/wse ...` 即可改配置。
- **自动重采样**：任意采样率的 PCM wav 会先转成标准 44.1kHz，保证能播。
- **内存安全**：所有游戏内存读取都做页级校验，地址失效只跳过本轮，不崩溃。

---

## 📦 目录结构

```
WeaponSoundEnhance/
├─ WeaponSoundEnhance.cpp     主源码（玩家侦测 + waveOut 多缓冲播放 + 热键/指令）
├─ WeaponSoundEnhance.ini     配置文件（派生动作 → 音效清单）
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
需要安装 Visual Studio（含「使用 C++ 的桌面开发」）。脚本自动定位 MSBuild 并编译到 `out\x64\Release\WeaponSoundEnhance.dll`。

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
├─ WeaponSoundEnhance.ini     配置（派生动作 → 音效清单）
└─ sounds\                    空，放入你的 wav 音效
```

使用步骤：
1. 把整个 `nativePC\plugins\` 放进游戏根目录（或用**狩技 mod 盒子**把 zip 直接拖进去安装）。
2. **需要怪猎前置**：请使用 Stracker's Loader（`dinput8.dll` / `loader.dll`）作为公认前置。
3. 把你想要的音效（**必须是 wav 格式**，标准 PCM 16 位）放进 `sounds\`。
4. 打开 `WeaponSoundEnhance.ini`，查看**每个派生动作对应的音效文件名**（`[AttackN]` 段的 `Sound=` 值），把你的 wav 改成同名文件即可；详细对应关系见 ini。
5. 进游戏，拿对应武器做派生攻击即可触发。

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
Volume=100               ; 音量 0..100（软件增益，只影响本插件音效）
Enabled=1                ; 总开关
MoreSounds=1             ; 1=条目内多条随机播，0=只播第一条
ChatEcho=1               ; 聊天栏回显(需原生函数地址)
ChatCommands=1           ; 开启 /wse 聊天框指令

[Hotkeys]                ; 17=Ctrl；数字是 Windows 虚拟键码
ModifierKey=17
ReloadKey=116            ; Ctrl+F5 重载 ini
VolUpKey=38              ; Ctrl+↑ 音量+5
VolDownKey=40            ; Ctrl+↓ 音量-5
SetVolKey=119            ; Ctrl+F8 音量设为 SetVolValue
SetVolValue=50
ToggleKey=120            ; Ctrl+F9 开关
MoreKey=121              ; Ctrl+F10 切换额外音效

[Attack1]                ; 第 1 个派生动作
WeaponType=3             ; 武器类型(0..13)，-1=任意
ActionLMT=-1             ; 动作 LMT，-1=不限
FSMId=11                 ; 状态机 ID，-1=不限
Sound=sounds/a.wav; sounds/b.wav   ; 多条分号分隔，随机抽一条
```

`WeaponType`：0大剑、1片手、2双刀、**3太刀**、4大锤、5笛子、6长枪、7铳枪、8斩斧、
9盾斧、10虫棍、11弓箭、12轻弩、13重弩。

> 附带的 ini 已为一套太刀（类型3）动作做好示例映射（气刃斩1-4、登龙飞天、逆袈裟、
> 气刃兜割、大居、磨刀、软化、突刺命中、猫车、见切等）。你只需把同名 wav 放进 `sounds\`。

---

## ⌨️ 游戏内热键

| 键 | 效果 |
|---|---|
| Ctrl+F5 | 重载 ini |
| Ctrl+↑ / ↓ | 音量 +5 / -5 |
| Ctrl+F8 | 音量设为 SetVolValue |
| Ctrl+F9 | 开关音效 |
| Ctrl+F10 | 切换“额外音效” |

## 💬 游戏聊天框指令

在游戏聊天框输入并发送：

| 指令 | 效果 |
|---|---|
| `/wse reload` | 重载 ini |
| `/wse on` / `/wse off` | 开关音效 |
| `/wse more` / `/wse one` | 切换额外音效 |
| `/wse vol 50` | 音量设为 50 |
| `/wse 50` | 音量设为 50（简写） |
| `/wse vol+` / `/wse vol-` | 音量 ±5 |
| `/wse help` | 显示命令 |

---

## 🧠 工作原理（给二次开发者）

- **玩家侦测**：后台线程轮询玩家内存：
  - `Manager = *(PlayerRoot)` ；`Entity = *(Manager + 0x50)`
  - 动作 LMT `= *( *(Entity+0x468) ) + 0xE9C4`
  - FSM `= *(Entity + 0x6278)`
  - 武器数据 `= *( *( *(Entity+0xC0)+0x8 )+0x78)`，类型在 `+0x2E8`
- **触发去重**：每个条目 `inMatch` 锁存（只在匹配沿触发），同音效文件 400ms 冷却。
- **播放**：`winmm waveOut` 多缓冲 + 软件增益 —— 每条音效独立线程/句柄，可并发，音量按样本缩放。
- **兼容性保险**：所有读取先 `mem::IsReadable`（VirtualQuery）校验，非法地址安全跳过。

---

## 🎯 已知与可优化项（欢迎贡献）

- [ ] **预分配 waveOut 设备 / 线程池**：当前每条音效新建线程 + 设备，连招密集时可改用常驻设备池降低开销。
- [ ] **播放音量用固定 44.1kHz 混合**：若想更省 CPU，可缓存重采样后的 PCM，避免每次重采样。
- [ ] **扫描 sounds 目录自动匹配**：目前按 ini 里 `Sound` 精确文件名匹配；可改成扫描目录按文件名自动注册。
- [ ] **多版本地址兼容**：`PlayerRoot`/偏移目前针对 15.23.00，可做签名扫描自动定位不同版本。
- [ ] **游戏内音量走 Wwise 原生**：若想与游戏音量完全一致，需挂钩 Wwise 音频函数（更复杂，需逆向）。

---

## 📜 免责 / 许可

- 仅供学习交流。使用前建议备份存档。
- 因游戏版本差异导致的地址失效、以及音效素材版权，由使用者自行承担。

---

## 🔖 参考

- 玩家/武器/聊天地址结构与取值约定参考 `mhw-toolkit`（eigeen/mhw-toolkit）。
- 武器类型表为游戏内置序号。
