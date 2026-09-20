# Windows 7 USER32 默认 EDIT 字符集逆向记录

> 分析日期：2026-09-21  
> 任务类型：PE / USER32 兼容性逆向（flavor = null）  
> 工具链：IDA Pro 9.4、MSVC 构建工具、Git

## 1. 概述与范围

本次仅分析并修改 `LocaleEmulatorPlus-Core`，目标是补齐 Windows 7 x86/x64 系统 EDIT 控件默认字体路径的字符集虚拟化。授权来自仓库所有者在当前会话中的直接请求；未修改、构建或运行 `D:\VSProj\JLXHP`。按用户要求，本次不启动目标游戏，运行时效果由用户在 Windows 7 环境验证。

## 2. 样本与静态分析

| 样本 | 架构 | 大小 | SHA-256 |
|---|---:|---:|---|
| `win_reverse/win7/x86/user32.dll` | x86 | 833024 | `01EB95FA3943CF3C6B1A21E473A5C3CB9FCBCE46913B15C96CAC14E4F04075B4` |
| `win_reverse/win7/x64/user32_.dll` | x64 | 1008128 | `F7D219D75037BC98F6C69143B00AB6000A31F8B5E211E0AF514F4F4B681522A0` |

两份 PE 的导入、导出和函数边界均已在 IDA 中检查。Win7 的 `ECSetFont(nullptr)` 不调用 Win10/Win11 使用的 DPI server-info getter，而是直接从 `gpsi` 复制默认宽度、高度和 60 字节 `TEXTMETRICW`，随后把 `tmCharSet` 缓存在 EDIT 私有对象中：x86 为 `EDIT+0xC0`，x64 为 `EDIT+0xF8`。

## 3. Evidence

### E-001

- title: Win7 x86 EDIT 直接缓存 `gpsi` 默认字体字符集
- observed_at: 2026-09-21
- source_type: file
- source_ref: `win_reverse/win7/x86/user32.dll`, IDA `ECSetFont` at `0x7DC822B9`
- content_hash: `01EB95FA3943CF3C6B1A21E473A5C3CB9FCBCE46913B15C96CAC14E4F04075B4`
- artifact_path: `win_reverse/win7/x86/user32.dll`
- repro_command: 在 IDA 中载入样本并跳转 `0x7DC822B9`；默认分支在 `0x7DC82456` 后读取 `gpsi+0xC8C/+0xC90/+0xC94`，在 `0x7DC82399` 写入 `EDIT+0xC0`
- raw_excerpt: 唯一复制锚点为 `6A 0F 81 C6 ?? ?? ?? ?? 59 8D 7D ?? F3 A5`
- linked_workitem: n/a
- supersedes: none

### E-002

- title: Win7 x64 EDIT 直接缓存 `gpsi` 默认字体字符集
- observed_at: 2026-09-21
- source_type: file
- source_ref: `win_reverse/win7/x64/user32_.dll`, IDA `ECSetFont` at `0x78C74784`
- content_hash: `F7D219D75037BC98F6C69143B00AB6000A31F8B5E211E0AF514F4F4B681522A0`
- artifact_path: `win_reverse/win7/x64/user32_.dll`
- repro_command: 在 IDA 中载入样本并跳转 `0x78C74784`；确认默认分支从 `gpsi+3212/+3216/+3220` 取宽、高和度量，并写入 `EDIT+0xF8`
- raw_excerpt: 唯一复制锚点以 `41 B8 3C 00 00 00 8B 82 ... E8` 开始
- linked_workitem: n/a
- supersedes: none

### E-003

- title: LEP 已安装 Win7 专用语义定位和实例缓存修补
- observed_at: 2026-09-21
- source_type: file
- source_ref: `LocaleEmulatorPlus/Hooks/User32Hook.cpp`
- content_hash: n/a
- artifact_path: n/a
- repro_command: `rg -n "FindDefaultEditSetFont|LepDefaultEditSetFont" LocaleEmulatorPlus/Hooks/User32Hook.cpp`
- raw_excerpt: 定位器拒绝歧义匹配；hook 仅在 Windows 7、新式 getter 未命中、且 `Font == nullptr` 时改写目标 EDIT 实例缓存
- linked_workitem: n/a
- supersedes: none

### E-004

- title: x86/x64 构建通过
- observed_at: 2026-09-21
- source_type: command
- source_ref: `build.bat`
- content_hash: n/a
- artifact_path: n/a
- repro_command: `.\build.bat all`
- raw_excerpt: `[x86] Build succeeded.`；`[x64] Build succeeded.`
- linked_workitem: n/a
- supersedes: none

## 4. Finding

### F-001

- title: Win7 默认 EDIT 乱码来自未虚拟化的实例级 charset 缓存
- severity: n/a_re
- category: reverse_algo
- status: candidate
- evidence_ids: [E-001, E-002, E-003, E-004]
- location: `LocaleEmulatorPlus/Hooks/User32Hook.cpp:136`
- impact: 系统 EDIT 使用默认字体时仍按宿主 CP936 解释 ANSI 初始文本；只修 DC/GDI 或 Win10/11 getter 无法覆盖该路径
- confidence: high
- repro_steps: 使用目标 ACP/charset 启动包含默认系统 EDIT 的 x86 程序，在 Win7 对比修补前后初始文本；日志应出现 `user32 Win7 default EDIT ECSetFont` 和实例缓存改写记录
- remediation: 语义定位 `ECSetFont`，调用原函数后仅对 `Font == nullptr` 的目标 EDIT 实例写入 LEP 目标 charset；不修改 `gpsi` 或显式字体
- optional_attack: n/a

## 5. Path

### P-001

- title: 默认 EDIT 文本乱码的数据流与修补点
- path_type: callflow
- start: 应用创建系统 EDIT 并使用默认字体
- goal: 后续 ANSI/Wide 转换读取目标 charset
- steps:
  1. action: `ECSetFont(nullptr)` 从宿主 `gpsi` 复制默认 `TEXTMETRICW` — evidence: E-001/E-002 — finding: F-001
  2. action: USER32 把 `tmCharSet` 写入 EDIT 私有缓存 — evidence: E-001/E-002 — finding: F-001
  3. action: LEP trampoline 返回后把该实例缓存改为目标 charset — evidence: E-003 — finding: F-001
  4. action: x86/x64 构建并部署 — evidence: E-004 — finding: F-001
- residual_risks: 尚待用户在真实 Win7 x86/x64 环境做动态验证；语义不唯一或结构不完整时修补会安全地不安装

## 6. 时间线

- 2026-09-21：确认 Win10/Win11 getter 模型不适用于 Win7。
- 2026-09-21：分别恢复 Win7 x86/x64 `ECSetFont` 默认分支和缓存偏移。
- 2026-09-21：实现 Windows 7 限定的语义定位、hook、卸载与日志。
- 2026-09-21：x86/x64 构建成功；未启动目标程序。

