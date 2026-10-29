# 宿主 DBCS 模式与 QQ 飞车输入失败的后续分析

日期：2026-10-29。工具：MSVC、dumpbin、本机独立 Win32 探针及现有 IDA 分析摘录。flavor：null。

## 概述与范围

用户反馈重新编译后的修复仍不能解决 QQ 飞车问题，环境沿用 [issue #3](https://github.com/julixian/LocaleEmulatorPlus/issues/3)：英语 Windows、非 Unicode 程序语言为英语、未启用 UTF-8 Beta，LEP 目标为简中。此前修复及其范围见 [EDIT 报告](2026-10-28_逆向-EDIT双字节边界-report.md)。

本轮确认一个现有修复尚未覆盖的机制：进程的 ACP/NLS 转为 936，并不自动改变 USER32 的宿主 DBCS 模式。在本机探针中，仅关闭 USER32 服务信息副本中的 DBCS 位，当前修复仍能向普通 ANSI 回调交付正确字节，但默认 ANSI EDIT 的字符输入全部失败。

这是与英语宿主环境相关的机制性证据，还不是 QQ 飞车实际调用路径的确诊。本轮未运行或注入 QQ 飞车，未改变系统区域设置、系统文件或用户剪贴板；没有修改 Core、增加 API Hook 或发布修复包。只在探针进程内重定向 USER32 的一个服务信息指针到私有副本，结束作用域后恢复。

## 样本与 Evidence

E-H01：`C:\Windows\SysWOW64\user32.dll`，x86，版本 `10.0.26100.8117`；SHA-256 `56F3BC77020E21EBEEF825965929DC0D0BD87F9CAFBB08DBCD477043E4342D2E`。探针脚本验证该指纹，不对其它版本套用固定地址。

E-H02：`out/input-path/versions/host-dbcs-peek-x86.asm.txt`；SHA-256 `B3215DDFDD7911ED3D787A4654EB80505C76B27CA8F50BE55C3BA62FA26857D2`。在 IDB/PE 首选基址 `0x10000000` 下，`_PeekMessage` 位于 `0x100329E0`；`0x10032B34` 读取全局 `gpsi` 指针槽 `0x100E4C10`，`0x10032B3C` 执行 `test byte ptr [eax], 2`。未设置该位时，跳过后续 ANSI 双字节消息拆分。

E-H03：已有 `out/input-path/user32-edit-analysis.txt` 的 `SLChar relevant call sites`：`gpsi & 2`、EDIT 状态及 `ECIsDBCSLeadByte` 共同决定是否调用 `DbcsCombine`。仅在 `PeekMessageA` 提供尾字节不能使被跳过的调用发生。该摘录来自本机 E-H01，详见首轮报告。

E-H04：`out/input-path/versions/host-dbcs-probe.cpp`；SHA-256 `C6E43A915C0395B1FEF462EF2B6319CFF572E31E887BF67E3081FC2234F0B01A`。探针在原生和 LEP CP936 两个子进程分别建立隐藏的普通 ANSI 窗口及默认 ANSI EDIT。每个子进程测试 DBCS 位开启/关闭两个私有快照，其余服务信息保持复制值；快照大小在本机为 28,608 字节。

E-H05：`out/input-path/versions/host-dbcs-x86.log`；SHA-256 `11259509211B3199ECA2B1E8BA78C83FB57530637CD1DE9E9E93C61F51560FB7`。两个子进程 ACP/OEM 均为 936。`child exit=0` 表示诊断执行完毕，不表示关闭 DBCS 位后的输入成功；下面明确列出失败观察。

## Findings

### F-H01：目标 ACP 与 USER32 的 DBCS 模式可以不一致

severity：n/a_re；category：compatibility；status：validated；confidence：high。
evidence_ids：E-H01/E-H02/E-H04/E-H05。

| LEP CP936 探针观察 | DBCS 位开启 | DBCS 位关闭 |
|---|---|---|
| `GetACP()` | 936 | 936 |
| `CheckDBCSEnabledExt()` | 1 | 0 |
| `GetSystemMetrics(SM_DBCSENABLED)` | 1 | 0 |
| `PostMessageW(U+4E2D)` 后的 `PeekMessageA` | 两条，`D6` / `D0` | 一条，`D0D6` |
| 经过 DispatchA 后，LEP 包装的普通 ANSI 回调 | `D6` / `D0` | `D6` / `D0` |
| 默认 EDIT：直接 `SendMessageW(WM_CHAR)` | “中” | 空文本 |
| 默认 EDIT：PostW / PeekA / DispatchA | “中” | 空文本 |
| 默认 EDIT：PostW / PeekW / DispatchW | “中” | 空文本 |

普通回调正确而系统 EDIT 仍失败，说明字符消息适配与系统类内部 DBCS 分支是不同层次的行为。当前 `PeekMessageA` Hook 在目标窗口进入取尾字节路径后才提供尾字节，不能修复之前根据宿主模式跳过组合的分支。

### F-H02：字符失败与字符串插入必须分别验证

severity：n/a_re；category：test_coverage；status：validated；confidence：high。
evidence_ids：E-H04/E-H05。

在 DBCS 位关闭时，原生及 LEP 子进程的 `SetWindowTextW`、`EM_REPLACESEL A`、`EM_REPLACESEL W` 仍均能插入“中”。因此 E-H01 的机制可复现字符输入失败，但不能据此断言已经解释 QQ 飞车粘贴失败。游戏还可能根据 DBCS 状态自行决定是否接受中文，或经过不同的剪贴板和输入路径；这些均未实测。

### F-H03：IME 组合串是未完成运行验证的另一条输入路径

severity：n/a_re；category：coverage_gap；status：hypothesis；confidence：medium。
evidence_ids：当前 Core 的 `WindowProcW`、模块 Hook 表和 `HOOK_INVENTORY.md`。

当前代码没有专门的 IMM32 组合串适配。应用可以处理 `WM_IME_COMPOSITION` 并调用 `ImmGetCompositionStringA/W` 取得字符串，此路径不等价于收到 `WM_CHAR`。该 API 可能仍受现有 NLS 替换影响，缺少专门 Hook 本身不能证明转换错误。需观察实际调用及返回字节才能确定。

官方行为参考：[WM_IME_COMPOSITION](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-composition)、[ImmGetCompositionStringA](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetcompositionstringa)。

### F-H04：简中配置下的日文默认输入语言疑点已排除

severity：n/a_re；category：scope_verification；status：validated；confidence：high。
evidence_ids：`User32Hook.cpp` 的两处 Hook 安装分支。

`LepFixDefaultInputLanguage` 的函数体固定返回日文 HKL，但安装 `SystemParametersInfoA/W` Hook 的条件是 `AnsiCodePage == CP_SHIFTJIS`，因此目标 CP936 不执行该策略。不能只看函数体就将其作为本 issue 的原因。

## Path P-H01：从目标代码页正确到字符仍被丢弃

path_type：callflow；start：进程已应用 LEP CP936；goal：定位字符进入系统类后的剩余宿主依赖。

1. Unicode“中”进入包装层，转换为 `D6 D0`。普通 ANSI 回调证明该层仍正确。证据：F-H01。
2. 原 ANSI EDIT 仍按 USER32 宿主 DBCS 标志决定是否进入组合路径；该位关闭时，现有临时尾字节上下文不能补足被跳过的处理。证据：E-H03、F-H01。
3. 字符插入失败，而字符串插入仍成功。需在游戏进程确认其窗口类型、DBCS 查询、原始 ANSI 队列使用、IME 组合串和剪贴板读取路径。证据：F-H02/F-H03。

residual_risks：本机仅验证了 x86/WOW64、指定 USER32 和两个私有服务信息模式；没有模拟完整英语 Windows，也未证明 QQ 飞车使用标准 EDIT 或已命中上述分支。直接将私有副本实验用于生产需要另行验证版本定位、所有服务信息引用、状态生命周期和与宿主相反方向的代码页组合。

## 复现与 Timeline

仓库根目录执行 `powershell -NoProfile -ExecutionPolicy Bypass -File out/input-path/versions/run-host-dbcs.ps1`。脚本复用此前构建的公开模块及修复版 Loader/Core；MSVC、SDK 和指纹均为本机条件，输出位于忽略的 `out` 目录。探针不访问用户剪贴板。

2026-10-29：用户反馈重编译后仍失败；复核 CP936 条件并排除日文 HKL Hook；对照既有 USER32 分支并用 dumpbin 核验本机地址；完成原生/LEP 子进程中 DBCS 模式的对照实验，确认字符和字符串路径的结果不同；保留实际游戏与英语宿主验证为待完成项。
