# 交接：找"动作 layer"字段（装瓶/解瓶/平射读不到 fsm 的问题）

> 给接手 SonarLayerProbe / layer 探针工作的 agent。
> 背景：MHW 动作状态是多层的。主插件读的 `entity+0x6278` 对**装瓶/解瓶**这类动作读到 **0**，
> 大佬指出"fsm layer 不对，要 layer0"。目标：找到这些动作真正所在 layer 的 fsm/lmt 字段。

---

## 1. 已确认的内存事实（15.23.00，逐次实测）

读取链（主插件 `WeaponSoundEnhance.cpp` 的 player 模块，ini 可覆盖）：

```
manager = *(0x1450139A0)                 // PlayerRoot
entity  = *(manager + 0x50)
fsm      = *(entity + 0x6278)            // ← 装瓶时读到 0
fsmTarget= *(entity + 0x6274)            // 装瓶时 = 3（弓的 target 层）
act      = *(entity + 0x468)
lmt      = *(act + 0xE9C4)
weapon   = *(*(*(entity + 0xC0) + 0x8) + 0x78) + 0x2E8
```

**实测样本（弓箭，装瓶/解瓶/平射各几次）**：

| 动作 | wem media（hook 捕获，可靠） | fsm(0x6278) | fsmTarget | lmt |
|---|---|---|---|---|
| **装瓶** | 873092594 (wp_bow_cmn/30) | **0** | 3 | **49153** |
| **解瓶** | 67273627 (wp_bow_cmn/4) | 65 | 3 | 49182 |
| 平射 | 217590158 (wp11/12) | 68 | 3 | 49188 |
| 其它弓招 | — | 65 / 68 / 71 / 75 / 79 | 3 | 49182 / 49188 / 49288 / 49310 |

**关键结论**：
- 装瓶时 `0x6278 = 0`，但 **`lmt = 49153` 稳定且唯一**（其它动作不是 49153）→
  装瓶的"身份"可以从 **wem media + lmt** 精确判定，不一定非要 layer 字段。
- **`entity+0x6290` 是"上一个 fsm 的缓存"**（`0x6278:79→0` 时 `0x6290:0→79`），
  **不是 layer0**，别把它当层字段。
- `entity+0x6298`(float) / `0x629C`(标志位, 0x04000000/0x00180000 等) / `0x62A0`(float)
  随招式变化，疑似招式计时/子状态，**不是 fsm**。

## 2. 已排除的范围

主 DLL 内的 `LayerProbe`（`LayerProbe=1`）会在**武器 wem 事件**到达时 dump
`entity+0x6200..0x6800` 的变化（实测 25 条，装瓶两次都在内）。
**该区域内没有独立的"装瓶 fsm"**（变化只有 0x6274/0x6278/0x6290/0x6298/0x629C/0x62A0）。

→ layer 字段应该在**别处**，建议按优先级试：

1. `act = *(entity+0x468)` 对象内部：`act+0xE000..0xF200`（lmt 在 0xE9C4 附近，
   同一对象的其它字段很可能是动作/层状态）
2. `entity+0x6000..0x7000` 全量（现在只看了 0x6200..0x6800）
3. **指针字段**：把 `entity+0x6270..0x62B0` 里疑似指针的值 dump 出来再解引用
   （layer 数组通常是指针 + count，不是内联值）
4. entity 更外层结构（`*(entity+0x468)`、`*(entity+0x470)` 等邻近指针）

## 3. 触发时机（很关键，别用时间猜）

**用 wem hook 作为"装瓶时刻"的触发器**：装瓶那一下游戏会 `PostEvent`
→ event 反查 media = **873092594**。主 DLL 已经把这个时机接出来了：

- `src/wwise/SonarAudio.cpp` 的 `LogPump()` 里，`p.media_id` 就是真 media id
- 主插件通过 `sonaraudio::SetEventSink()` 拿到事件（见 `WemEventSink`）
- `LayerProbe` 就是在这个 sink 里 `ArmLayerProbe(media)`，下一帧 dump

做新探针时沿用这个触发点，比"固定间隔 dump"准确得多。

## 4. 现成产物（可直接复用）

```
dll/WeaponSoundEnhance/
├─ src/wwise/                 ← Wwise PostEvent hook（导出表解析 + MinHook + nbnk 反查）
│   ├─ SonarWwise.{h,cpp}     ← PE 导出表按 mangled 名找 PostEvent（ASLR 安全）
│   ├─ SonarBanks.{h,cpp}     ← nbnk(HIRC) 解析 + event→media 反查
│   ├─ SonarAudio.cpp         ← hook+日志+事件 sink
│   └─ SonarIpc.{h,cpp}       ← 命名共享内存
├─ third_party/minhook/       ← MinHook（lib + 头，已链接进 DLL）
├─ ref/wwise_symbols.txt      ← exe 导出的 205 个 Wwise 符号名 + 地址
├─ ref/WseMediaSeqs.txt       ← media_id → (bank, DIDX 序号) 真值表（4925 条）
├─ ref/SonarAudio-src/        ← 原 SonarAudio 源码 + NBNK_CAPTURE_NOTES.md（nbnk 规格）
└─ ref/RingingBloom/          ← Silvris 的 C# MHW Wwise 工具源码（BNK/DIDX/HIRC 参考）
```

**hook 三要点**（踩过坑）：
1. Wwise 在 `MonsterHunterWorld.exe` 的 **PE 导出表**里（mangled 名），解析导出表拿地址；
   不要去 xref RTTI 字符串（那是我走过的弯路）。
2. **同一进程只能有一个 MinHook 实例 hook 同一函数**：两个 DLL 各带一份 MinHook 会互相覆盖
   → 现象是"hook 装上了但事件计数 0"。所以 SonarAudio.dll 已停用（改名 `.disabled`），
   功能全部并进 `WeaponSoundEnhance.dll`。
3. hook 回调里只做原子记账，名字/查表放消费线程（`LogPump`）。

## 5. 环境与操作提示

- 游戏：`F:\SteamLibrary\steamapps\common\Monster Hunter World`，插件放 `nativePC\plugins\`
- **部署时文件被锁**（游戏在跑）：目录项可以重命名 → `mv X.dll X.dll.oldlocked; cp 新 X.dll`
- 僵尸进程（tasklist 内存列 ~20K、`taskkill /F` 拒绝访问）不用强杀，重命名绕过即可
- 探针日志：`plugins\WeaponSoundEnhance.log`（`[layer]` 行）；
  wem 事件日志：`plugins\WeaponSoundEnhance_wem.log`
- 编译：VS 在 `E:\Program Files\Microsoft Visual Studio\18\Community`（v142），
  MSBuild 直接编 `WeaponSoundEnhance.vcxproj` / `gui\SonarGUI.vcxproj`（均 Release|x64）

## 6. 现成可用的替代方案（如果 layer 找不到）

装瓶**已经能被精确捕获**，不依赖 layer：

- WEM 捕获面板（GUI 右侧「WEM 音效」视图）：game 播 873092594 → 历史里出现
  `wp_bow_cmn · 第30个` → 点"＋ 添加"建条目
- 触发条件 = `Media=873092594` **+ 可选 `LMT=49153`**（编辑器里填 LMT 即可，
  用于排除"进集会/换装备/重载"时同一 event 被请求造成的误触发）
- 解瓶 = `Media=67273627`（#4）

所以 layer 探针的收益是"让**派生流(fsm/lmt)面板**也显示装瓶"，
**功能上不是必须的**——如果继续挖，请按第 2 节的优先级试。
