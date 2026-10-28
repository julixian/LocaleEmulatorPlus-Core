# EDIT 双字节字符的边界适配

分析日期：2026-10-28。工具：IDA Pro 9.4、MSVC、x86/x64 Win32 回归程序。flavor：null。

## 概述与范围

ANSI EDIT 处理双字节字符时，会在首字节的窗口过程调用内部取出尾字节。仅连续两次调用原 ANSI 窗口过程，不能满足这种同步取消息行为。修复在 W->A 字符适配期间提供临时尾字节，并继续调用保存的原 ANSI 窗口过程；原有字符串、选择及行位置的双向边界适配保持原流程。

此前实验中整类转发到 Unicode EDIT 的旁路已删除，随之增加的宿主 ACP 字段和位置换算分支也已删除。默认 EDIT 和安装 ANSI 子类的 EDIT 均通过回归。QQ 飞车、真实 IME 组合串和其它 Windows 版本仍未验证。

授权、范围及网络约束见 [scope](issue3-edit-scope.md)。这里分析的是兼容性行为，不属于安全漏洞或攻击路径研究。

## 样本与证据

样本：`C:\Windows\SysWOW64\user32.dll`，x86 PE，2,023,488 字节，文件版本 `10.0.26100.8117`。SHA-256：`56F3BC77020E21EBEEF825965929DC0D0BD87F9CAFBB08DBCD477043E4342D2E`。

| Evidence | 来源与观察 | content_hash |
|---|---|---|
| E-001 | IDA 数据库基址 `0x10000000`；`EditWndProcA` RVA `0x8FF60`，`SLChar` RVA `0x95340`，`DbcsCombine` RVA `0x8D2FB`。单行 ANSI EDIT 的字符路径调用 `DbcsCombine`，后者用 `PeekMessageA`、`WM_CHAR` 范围及 `PM_REMOVE` 取下一字节，未取到时短暂等待后失败。 | 样本 SHA-256 如上 |
| E-imports | 导入模块包括 `win32u`、`ntdll`、`KERNELBASE` 和 Windows API sets；NLS 相关导入及上述函数的分析摘录保存在 `../out/input-path/user32-edit-analysis.txt`。 | `CEA45B59C9D3E76935CC9D4651744D087AB05F5CC769BCE6461A8B5CDB967FA5` |
| E-002 | `../out/input-path/results-x86.log`：原生及 LEP CP936/932/1252 均为 `character failures=0`。测试同时断言实际排队的 `WM_CHAR` 不会被组合字符过程取走。 | `BF3C0ACAC44230180DE61ED872A2E0C66B366EDCFAFCFF25264B9C5037E0861D` |
| E-003 | `../out/input-path/results-x64.log`：相同配置均为 `character failures=0`。 | `CF5442651C98D0C3AFF1A667D5258525BC8F5CE6A7CF53B49EED4D381FBF5AC7` |

E-001 的离线复核需要该版本样本与 IDA，按上述 RVA 查看函数。E-002/E-003 的复现命令：

```powershell
python build.py all --no-deploy
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-input-path.ps1
```

命令在仓库根目录执行，编译器和 SDK 参数可通过脚本参数覆盖。分析输出是本机生成的诊断材料，不随二进制测试包分发；重跑测试会更新日志。

## Findings

### F-001：尾字节在首字节回调期间不可见

- severity：n/a_re；category：reverse_algo；status：validated；confidence：high。
- evidence_ids：E-001、E-002、E-003。
- location：`user32!DbcsCombine`；`LocaleEmulatorPlus/Hooks/User32Hook.cpp` 的 `CallAnsiCharacterProc`。
- 影响：首字节的原 ANSI EDIT 回调尚未返回时就需要尾字节，单纯等第一次回调返回再调用第二次会丢失双字节输入。
- 修复：仅在包装 EDIT 的两字节 `WM_CHAR` 适配期间建立临时尾字节上下文，让 `PeekMessageA` 按窗口和消息过滤条件读取它。上下文结束后不再暴露；回调未消费时才继续交付第二次回调。

### F-002：整类 Unicode 旁路扩大了转换范围

- severity：n/a_re；category：design；status：validated；confidence：high。
- evidence_ids：E-001、E-002、E-003，以及当前源代码和回归程序。
- location：原 `GetSystemEditProcW` / `WindowProcW` 提前返回分支，现已移除。
- 影响：默认 EDIT 与 ANSI 子类的转发路径不同，字符串与内部缓冲区语义也跟着改变；此前宿主 ACP 的额外位置适配不能作为原 ANSI EDIT 路径的普遍规则。
- 修复：统一保留 `WindowProcW -> 消息适配 -> PrevProcA`，原有 `UserfnEDIT_TEXT_POSITION` / `KernelfnEDIT_TEXT_POSITION` 恢复原实现。

## Path P-001：保留原 ANSI 回调的输入路径

- path_type：callflow。
- start：包装后的 EDIT 接收 Unicode `WM_CHAR`。
- goal：原 ANSI EDIT 正确组合目标 ACP 字节，并保留应用子类回调与真实队列内容。
- steps：
  1. W->A 字符适配将 Unicode 转为目标 ACP；两字节 EDIT 输入建立栈上临时尾字节上下文。证据：E-002/E-003；发现：F-001。
  2. 调用保存的 `PrevProcA`；应用子类仍在调用链中。原 EDIT 的 `DbcsCombine` 从受约束的 `PeekMessageA` 适配取得尾字节。证据：E-001/E-002/E-003；发现：F-001。
  3. 退出上下文；若尾字节未被消费，则通过原回调交付。选择位置继续在原来的两处边界各自转换。证据：E-002/E-003；发现：F-002。
- residual_risks：真实 IME 组合串、其它系统版本、其它控件及在进入包装层前已损失的字符没有因此获得完整覆盖。

## 验证与限制

回归包含：五种字符消息、Unicode 窗口、普通 ANSI 子类、默认 EDIT、EDIT ANSI 子类、选择和替换、直接调用 `NtUserMessageCall` 的 ANSI 选择位置边界、真实队列哨兵、`PeekMessageA` 的窗口/消息过滤、`PM_NOREMOVE` 重复读取、嵌套 EDIT 调用和上下文清理。CP932 的半角片假名 `U+FF83` 直接输入默认 EDIT 返回 `C3`，已恢复。

新增加一个 `PeekMessageA` hook。它通常直接调用原 API，只在当前线程正在执行该 EDIT 双字节字符回调且请求匹配时提供临时尾字节；不写私有 TEB 消息缓存，不向真实队列追加尾字节。它仍是新增的 API 适配，后续维护应保留过滤、重入和生命周期回归。

普通非 ASCII `SendMessageA` / `PostMessageA` 和部分 ANSI 队列路径的兼容性仍未整体修复；最终回归仅对 ASCII A 来源消息作常规断言。没有验证真实输入法组合串、QQ 飞车、RichEdit、不同 Windows 版本或任意自定义子类的全部行为。本报告首轮静态分析针对上述 x86 样本，x64 结论来自本机运行测试。后续六份 USER32 样本的跨版本对照及无需新增 Hook 的缓存借用实验见 [跨版本报告](2026-10-28_逆向-USER32-DBCS跨版本-report.md)，该实验尚未替换当前实现。

## Timeline

2026-10-28：用户指出整类旁路与现有两处边界架构不一致；复核源代码并只读分析系统 DLL；定位 `DbcsCombine` 的同步取尾字节行为；移除旁路及宿主 ACP 补丁；完成 x86/x64 两组架构下的输入、子类、位置、队列隔离与重入回归。
