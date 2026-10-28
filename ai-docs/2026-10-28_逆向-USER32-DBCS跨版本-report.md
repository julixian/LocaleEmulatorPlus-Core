# USER32 双字节尾消息缓存的跨版本分析

日期：2026-10-28。工具：IDA Pro 9.4、MSVC、独立 Win32 探针。flavor：null。

## 结论与范围

`win_reverse` 中 Win7、Win10、Win11 的 x86/x64 六份 USER32 样本，均使用原生 64 位 TEB 的 `+0x8A0` 保存双字节尾消息，`+0x8B0` 的 64 位 `wParam` 为非零时表示仍有缓存。主要版本差异在 WOW64 下如何取得原生 TEB，而不是缓存布局。

本机独立探针验证了无需增加 API Hook 的基本路径：临时写入缓存，再调用原 ANSI EDIT 首字节回调，系统 EDIT 会自行取走尾字节。单行、多行及 ANSI 子类测试在 x86/WOW64 和 x64 均通过。同时，探针确认了 HWND 过滤被缓存绕过、无条件恢复旧快照会丢弃回调期间产生的真实尾字节。这些是实际行为，不能只凭布局一致就认为完整替换方案已经完成。

本轮只做分析和独立验证，没有修改 Core、撤销现有 `PeekMessageA` Hook 或重新生成修复包。当前修复及回归范围见 [EDIT 边界报告](2026-10-28_逆向-EDIT双字节边界-report.md)；此前授权范围见 [scope](issue3-edit-scope.md)。本轮新增授权来自用户“可以看下 win_reverse 下的几种版本和架构”。这是本机兼容性研究，不涉及外部服务或安全攻击路径。

## 样本与证据

原 IDB 均复制到 `out/input-path/versions` 后分析，关闭时不保存。每份 IDB 的 `retrieve_input_file_sha256()` 均与对应 DLL 的 SHA-256 一致；IDB 中遗留的旧输入路径未作为样本身份依据。

| Evidence | 样本相对路径 | 文件版本 | SHA-256 |
|---|---|---|---|
| E-V71 | `win_reverse/win7/x86/user32.dll` | 6.1.7601.17514 | `01EB95FA3943CF3C6B1A21E473A5C3CB9FCBCE46913B15C96CAC14E4F04075B4` |
| E-V72 | `win_reverse/win7/x64/user32_.dll` | 6.1.7601.17514 | `F7D219D75037BC98F6C69143B00AB6000A31F8B5E211E0AF514F4F4B681522A0` |
| E-V101 | `win_reverse/win10/x86/user32.dll` | 10.0.19041.4780 | `34C50885D21105C742891333248AD5D36D309B3E2312E55AF00C4C7F3FA6406A` |
| E-V102 | `win_reverse/win10/x64/user32.dll` | 10.0.19041.4717 | `8DBC0E66B480969AB6B9A802105376B4EB18481502BCEA7CADDC87BF3E707C33` |
| E-V111 | `win_reverse/win11/x86/user32.dll` | 10.0.26100.9444 | `BFF75906F4ABCC050CF85E38E20090EA0EDF7DC33E0ED60CB74CF9E1BC007527` |
| E-V112 | `win_reverse/win11/x64/user32_x64.dll` | 10.0.26100.8457 | `9BB5517465A699D9D18133BE25082E3224A8374A64B8EA2CE4A2F71ADF8A5AA5` |

分析输出包含伪代码和汇编；IDA 对部分 x86 参数、TEB 字段类型的推断不准确，以下偏移以汇编实际访存为准。

| Evidence | 本机输出文件 | content_hash / SHA-256 |
|---|---|---|
| E-A71 | `out/input-path/versions/win7-x86.analysis.txt` | `68AEEE41270B3E1437F04800612D2DB7210E12F0F563176284CB235598E8BBF9` |
| E-A72 | `out/input-path/versions/win7-x64.analysis.txt` | `C021347631119C74992AE92C9438064A36896FE12EE6DD6F528E9B28234F2DCD` |
| E-A101 | `out/input-path/versions/win10-x86.analysis.txt` | `350ADF98AF4601DF7198E032432F5F1A5BA104FD8B0E93819F30FD8A149C0913` |
| E-A102 | `out/input-path/versions/win10-x64.analysis.txt` | `4997F28E5B3860BC787E022EC3FA82EEFC6250F90CF3247F6B066A7A2C856376` |
| E-A111 | `out/input-path/versions/win11-x86.analysis.txt` | `E234DAA479CD857F45DC59FB697FFF094F68B9AC1C03D065F72E2B255376C78D` |
| E-A112 | `out/input-path/versions/win11-x64.analysis.txt` | `4FCDD457B4322CBD2C5A582B8D5DD43865D2F310F8D956E1BE116D3CFAC0659D` |
| E-R32 | `out/input-path/versions/native-cache-x86.log` | `A4D7155AF97561688F36AD140CA5EB2198BE305D7B4B6800D522BD54438AB6B0` |
| E-R64 | `out/input-path/versions/native-cache-x64.log` | `C28DEACD24686A60FA035EED16A3F97B915A1F298A7642263C2BC17499C59795` |

静态定位地址如下，地址均使用各 IDB 的加载基址。

| 样本 | 基址 | PeekMessageA | _PeekMessage | DbcsCombine |
|---|---|---|---|---|
| Win7 x86 | `7DC50000` | `7DC75F74` | `7DC706BE` | `7DCB3ABF` |
| Win7 x64 | `78C20000` | `78C33A18` | `78C350B0` | `78C79FA0` |
| Win10 x86 | `69E00000` | `69E20660` | `69E3C2D0` | `69E68FD0` |
| Win10 x64 | `180000000` | `180009680` | `18000A2F0` | `18005CCB0` |
| Win11 x86 | `10000000` | `100C3090` | `10032680` | `100888DA` |
| Win11 x64 | `180000000` | `180013540` | `1800138B0` | `18007C238` |

## Findings

### F-V01：六份样本使用相同的原生缓存布局

severity：n/a_re；category：reverse_algo；status：validated；confidence：high。
evidence_ids：E-V71/E-V72/E-V101/E-V102/E-V111/E-V112、E-A71/E-A72/E-A101/E-A102/E-A111/E-A112。

| 架构和版本 | 原生 TEB 的取得方式 | 消息缓存 | 有效标志 |
|---|---|---|---|
| Win7 x86 样本的 WOW64 路径 | `TEB32 = fs:[18h]`，再取 `[TEB32+F70h]` 的 32 位指针 | NativeTEB64 + `8A0h` | NativeTEB64 + `8B0h` 的 QWORD |
| Win10/Win11 x86 样本的 WOW64 路径 | `TEB32 = fs:[18h]`，读取 `[TEB32+FDCh]` 有符号偏移；为负时加到 TEB32 | 同上 | 同上 |
| Win7/Win10/Win11 x64 | `gs:[30h]` | 同上 | 同上 |

本机 WOW64 实测偏移为 `-8192`。这不是要硬编码的偏移值；应读取 `WowTebOffset` 字段。Win7 x86 的反编译器将 `F70h` 字段显示成 `GdiBatchCount`，Win11 x86 将 `FDCh` 显示成 `SpareUlong0`，不能直接按这些错误类型解释用途。

缓存相当于 `TEB64.Win32ClientInfo[20]`，有效标志是 `[22]`。内容占 48 字节，必须使用固定宽度原生消息布局，不能在 x86 中直接复制 `sizeof(MSG)`。

| 原生 TEB 偏移 | 内容 |
|---|---|
| `8A0h` | 64 位 hwnd |
| `8A8h` | 32 位 message，以及对齐空间 |
| `8B0h` | 64 位 wParam；非零表示仍有缓存 |
| `8B8h` | 64 位 lParam |
| `8C0h` | 32 位 time |
| `8C4h` / `8C8h` | 32 位 pt.x / pt.y |
| `8CCh` | 最后一个私有或填充 DWORD，不应赋予推测语义 |

ANSI `_PeekMessage` 优先读取此缓存。`PM_NOREMOVE` 保留 `wParam`，`PM_REMOVE` 清零整个 64 位 `wParam`。缓存分支满足消息范围后会直接返回，因而该次调用不需要进入 `NtUserPeekMessage`。`DbcsCombine` 在首字节回调期间同步调用 `PeekMessageA` 取尾字节的模式在六份样本中一致。

### F-V02：无需新增 Hook 的基本路径已在本机运行验证

severity：n/a_re；category：compatibility；status：validated；confidence：high。
evidence_ids：E-R32、E-R64；location：`out/input-path/versions/native-cache-probe.cpp`。

探针是独立程序，没有加载 LEP。运行主机 `RtlGetVersion` 返回 `10.0.26300`，ACP 为 936。当前系统 USER32 的版本与样本目录不同：

- SysWOW64：`10.0.26100.8117`，SHA-256 `56F3BC77020E21EBEEF825965929DC0D0BD87F9CAFBB08DBCD477043E4342D2E`。
- System32：`10.0.26100.9949`，SHA-256 `F285937030BCB325CAD39EE5BE4818F6666241229ADEADEA4FF1AE469DFD7EF3`。

两个架构均确认：重复 `PM_NOREMOVE` 可读同一尾字节；`PM_REMOVE` 清零；`PeekMessageW` 和不包含 `WM_CHAR` 的范围不消耗此缓存；人工嵌套借用后能恢复外层快照；默认单行、多行 EDIT 和 ANSI 子类 EDIT 均能在首字节 `D6` 回调期间取到缓存的 `D0`，最终文本为“中”。

这些结果是机制可行性证明，不是已经将替代实现接入 Core，也不是 Win7/Win10 的运行结果。

### F-V03：原生缓存分支没有按 HWND 过滤

severity：n/a_re；category：compatibility；status：validated；confidence：high。
evidence_ids：全部 E-A*、E-R32、E-R64。

六份样本的缓存读取分支检查 ANSI 标志、缓存 `wParam` 和消息范围，没有检查请求的窗口与缓存 hwnd 是否相同。本机探针中，向第二个 EDIT 调用 `PeekMessageA(..., second, WM_CHAR, WM_CHAR, PM_NOREMOVE)`，实际返回第一个 EDIT 的缓存消息。

因此借用原生缓存会继承系统的该行为，与当前临时尾字节 Hook 的严格 HWND 匹配不同。不能宣称当前 Hook 的过滤回归无需调整便可全部适用。

### F-V04：简单的保存、覆盖、恢复会丢弃新产生的真实尾字节

severity：n/a_re；category：state_management；status：validated；confidence：high。
evidence_ids：E-R32、E-R64。

两个架构均复现：先借用空缓存，消费临时尾字节；随后向探针自身窗口 `PostMessageW(WM_CHAR, U+4E2D)`，`PeekMessageA(PM_REMOVE)` 返回首字节 `D6` 并在同一槽位生成真实尾字节 `D0`；退出作用域无条件恢复最初的空快照，新的 `D0` 随即消失。探针随后主动恢复并消费该真实尾字节，只操作自己进程的窗口。

这说明直接套一个 snapshot RAII 不能作为完整修复。替代方案需要识别缓存所有权、旧缓存占用、嵌套借用，以及回调期间由系统生成的新缓存。此处没有证明所有零新增 Hook 方案都不可行，只证伪了无条件恢复快照这一实现。

## Path P-V01：在现有 W->A 边界借用原生尾消息缓存

path_type：callflow；start：Unicode `WM_CHAR` 转为两字节目标 ACP；goal：保留原 ANSI 子类及 EDIT 调用链，避免新增 API Hook。

1. 在原有 W->A 字符适配边界取得正确的原生 TEB，检查缓存状态。证据：F-V01。
2. 临时提供尾字节消息，调用保存的原 ANSI 过程交付首字节。系统 `DbcsCombine` 经原生 `PeekMessageA` 消费缓存；不需要旁路到 Unicode EDIT 类过程。证据：F-V01/F-V02。
3. 按所有权和消费状态结束借用，保留原有及回调中新产生的真实缓存。此步仍是待实现内容；无条件恢复已被 F-V04 证伪，同时应明确 F-V03 的过滤行为。

residual_risks：Win8/8.1、原生 32 位 Windows 尚无本轮样本证据；这些 x86 样本中的 WOW64 访问方式不能当作原生 32 位系统布局。LEP CP932/1252、完整输入路径、复杂重入及真实输入法尚未使用零新增 Hook 实现回归。

## 复现与 Timeline

在仓库根目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File out/input-path/versions/run-native-cache.ps1`。脚本路径及探针在忽略的 `out` 目录内，编译器路径为本机配置；重跑会改写含地址的日志及其哈希。

2026-10-28：复制并对照六份 IDB；核验所有输入指纹；提取缓存、TEB 定位和 DbcsCombine 行为；在本机 x86/WOW64 与 x64 上完成借用缓存及默认/子类 EDIT 验证；复现 HWND 过滤差异和无条件恢复快照导致尾字节丢失。
