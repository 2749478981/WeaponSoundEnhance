# WeaponSoundEnhanceGUI

《怪物猎人：世界 冰原》(15.23.00) 武器音效拓展插件 **WeaponSoundEnhance** 的图形化配置工具。

- 单文件 exe（约 0.87 MB，静态 CRT，无需安装任何运行库）
- 技术栈：C++ / Dear ImGui / DirectX 11，**macOS 浅色风格**（红绿灯、圆角、蓝色点缀）
- 直接读写插件的 `WeaponSoundEnhance.ini`，与插件完全兼容

## 界面（三栏 macOS 布局）

- **左栏（武器分类）**：14 种武器 + 通用分类，带条目计数，选中蓝底白字。
- **中栏（条目表）**：搜索 + 新增条目 + 条目表格（名称/武器/LMT/FSM/音效/编辑/复制/删除）。
- **右栏（实时捕获）**：连接状态、当前 fsm/lmt/武器，以及**捕获历史**。

## 功能

- **增强/音效重载**：改 wav 后按 **Ctrl+F5**（或游戏聊天框输 `/wse reload`）即生效，无需重开游戏（详见下方 DLL 说明）。
- **增删音效条目**：新增 / 编辑 / 复制 / 删除，每条目可配多条音效（触发时随机播一条），可试听 / 浏览导入。
- **多武器分类**：左侧分类过滤。
- **快捷查询 FSM/LMT**：
  - `FSM 查询` 窗口：按中文名/fsm/lmt 搜索内置知识库，点选即填入新条目。
  - **捕获历史**：自动记录游戏里 fsm/lmt/weapon 的变化（去重），**fsm≠0 的派生动作用蓝底高亮**、fsm=0（自由态）置灰；点【加入条目】把历史里的某次动作一键填入新条目，解决「切回界面只显示自由态 fsm=0」的问题。
- **原生 Windows 文件弹窗**：打开 ini / 另存为 / 浏览导入 wav（多选，自动复制进 `sounds\`）。

## 构建

需要 Visual Studio 2019/2022 的「使用 C++ 的桌面开发」工作负载，双击：

```
build.bat
```

产物输出到 `out\x64\Release\WeaponSoundEnhanceGUI.exe`。Dear ImGui 源码在 `third_party\imgui\`，无需额外下载。

## 使用

把 `WeaponSoundEnhanceGUI.exe` 放到游戏 `nativePC\plugins\` 目录（与 DLL、ini、`sounds\` 同目录）双击运行。

- 首次运行同目录没有 ini 则用默认值；点「打开 ini」选已有配置，或直接编辑后「保存」。
- 若游戏正在运行，会自动定位 `nativePC\plugins\WeaponSoundEnhance.ini`。
- 保存会按工具格式重写 ini（自动按武器分组、补 `Name=`、重排 `[AttackN]`），怕覆盖先「另存为」备份。

## 扩展 FSM/LMT 知识库

在 exe 同目录放一个 `fsm_db.csv`（可选）：

```
weapon,fsm,lmt,name
3,90,-1,登龙飞天
3,67,49258,气刃斩2
```

`weapon` 0~13（`-1` 通用），`lmt -1` 不限，`name` 不含逗号，`#` 开头行忽略。内置若干太刀条目。

## 插件音效重载（WeaponSoundEnhance.dll）

`/wse reload`（或 Ctrl+F5）现在会**清空音效内存缓存**，下一次播放直接从磁盘重新读取 wav。改了 `sounds\` 里的 wav 后按一下位载即可生效，不用重开游戏。插件已加互斥锁保护缓存，重载与播放线程并发安全。

## 说明

- 内存地址基于 15.23.00；小版本变动可在 ini 调整 `PlayerRoot`（按 `模块基址 + (PlayerRoot - 0x140000000)` 换算，兼容 ASLR）。
- 实时抓取需读取游戏进程内存；Steam 版通常无需管理员，失败请以管理员身份运行。
