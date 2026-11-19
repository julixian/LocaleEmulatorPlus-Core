# LEP 跨 UAC 提权的子进程传播测试

## 结论

**当前版本不能在本次测试的 UAC 提权路径中继续传播转区配置。** 简中宿主 ACP 936，LEP 目标日区 ACP/OEM 932、LCID 1041。普通权限父进程及其普通子进程保持日区；通过 UAC 启动的高完整性子进程恢复宿主 ACP/OEM 936、用户及系统 LCID 2052，其普通子进程也保持宿主配置。

本轮只添加测试探针和文档，没有修改 Core/Loader 实现。源码基线 `00f78be`，使用 `python build.py all --no-deploy` 重新构建。测试日期 2026-11-19，Windows 10.0.26300；UAC EnableLUA=1、ConsentPromptBehaviorAdmin=5、PromptOnSecureDesktop=1。范围及授权见 [scope](uac-propagation-scope.md)。flavor=null。

## 实测结果

三组中普通父/子进程均 `TokenIsElevated=0`、完整性 RID 8192；提权子进程及其子进程均 `TokenIsElevated=1`、RID 12288。令牌信息由每个进程自行读取，区域配置也由该进程自行调用 API 记录。

| 架构与启动方式 | 父进程 ACP | 普通子进程 ACP | 提权子进程 ACP | 提权后的下一代 ACP | 传播 |
|---|---:|---:|---:|---:|---|
| x86，`runas` | 932 | 932 | 936 | 936 | 中断 |
| x64，`runas` | 932 | 932 | 936 | 936 | 中断 |
| x64，`open` + `requireAdministrator` 清单 | 932 | 932 | 936 | 936 | 中断 |

本轮没有测试跨架构组合、另一个管理员账户的凭据提权、计划任务、COM elevation、系统服务代启动或“起点已经是管理员权限”的场景。x64 对照的 `GetUserDefaultUILanguage` 仍为宿主 2052，尽管 ACP 和用户/系统 LCID 已转为日区；本测试不将 UI 语言视作唯一传播指标，也未调查该独立现象。

## 原理与日志陷阱

当前 [LepNtCreateUserProcess](../LocaleEmulatorPlus/Hooks/NtdllHook.cpp) 在进程内截获 `NtCreateUserProcess`，加入暂停线程标志；创建成功后调用 `InjectSelfToChildProcess`，再按调用方原始要求处理恢复执行。传播依赖创建调用经过已加载 Core 的进程。

微软文档描述：Shell 的提权路径由 Application Information 服务协助创建高权限进程。[UAC 工作原理](https://learn.microsoft.com/en-us/windows-server/security/user-account-control/how-user-account-control-works)；[`runas` verb](https://learn.microsoft.com/en-us/windows/win32/shell/launch)。服务进程中的创建调用不会经过普通父进程内的 hook。本次实测结果与这一机制吻合；未附加调试器采集 AppInfo 服务调用栈。

`runas` 两组日志只记录了普通子进程的成功传播。清单提权组则有一个需要特别区分的观察：

- 父进程 PID 2156，普通子进程 PID 22308，成功转区。
- 父进程 hook 另记录了对 `elevation-probe-admin.exe` 的创建和注入成功，PID **3752**。
- `ShellExecuteExW` 最终返回的进程句柄对应 PID **38952**；其自行写出的状态为高完整性、ACP **936**。
- PID 38952 的后继进程为 PID 31024，也是高完整性、ACP 936。

因此，前一个创建尝试的 `injectStatus=00000000` 不能证明最终管理员程序已注入。应比对 Shell 返回句柄的 PID、实际目标自报的状态和对应 Core 日志。本轮未进一步调试 PID 3752 的退出阶段，不将它描述为最终提权进程。

## 复现

使用普通权限终端。每次实际测试会弹出一次 UAC，需在系统弹窗中确认。探针不写管理员专属位置，不改变系统区域设置。

```powershell
Set-Location D:\VSProj\LocaleEmulatorPlus-Core
python build.py all --no-deploy
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-elevation.ps1 -Architecture x86 -Mode runas
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-elevation.ps1 -Architecture x64 -Mode runas
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run-elevation.ps1 -Architecture x64 -Mode manifest
```

脚本默认使用本机 MSVC 14.44.35207、SDK 10.0.26100.0，可通过同名参数更换工具路径。`-BuildOnly` 只编译和准备 DLL，不触发 UAC。

`elevation-probe.cpp` 使用公开 ABI 模块及 `LepCreateProcess2` 启动普通父进程。父进程先用 `CreateProcessW` 创建普通对照子进程，再用 `ShellExecuteExW` 启动提权探针；后者再用 `CreateProcessW` 创建后继进程。各阶段生成独立 JSON，避免把外部观察器的 ACP 当作目标 ACP。脚本输出 `Propagated=false` 是有效测试结果；退出码 0 表示测试已完整执行。

结果位于 `out/elevation/<arch>/<mode>-<timestamp>/`，包括五阶段状态、Shell 结果、摘要、构建基线、二进制 SHA-256 及匹配 PID 的 Core 日志。`out/` 不纳入 Git。UAC 取消/超时会导致测试报错；不能当作传播失败。

## Evidence → Finding → Path

下列路径相对仓库根目录。E-001 至 E-003 的 `repro_command` 为上述同架构/模式的测试命令；重新执行会生成新的时间戳目录和 PID。

| Evidence | source_ref | SHA-256 |
|---|---|---|
| E-001：x86 runas 摘要 | `out/elevation/x86/runas-20261119-201954/summary.json` | `A5BDFE00222D55F7418906AF5A596F3F1CE9D312EB9D839B2F3B306AD08FDF5F` |
| E-002：x64 runas 摘要 | `out/elevation/x64/runas-20261119-202013/summary.json` | `F6C674510C84547FB4CF51E86FE65EF3D8C8FC77ADC4470E1D6D03CB193F090D` |
| E-003：x64 清单提权摘要 | `out/elevation/x64/manifest-20261119-202027/summary.json` | `1F7321CBC93BD07A1CB17EA4C08CCB99F1ADC72E379413A9585B5B468BBD26F9` |
| E-004：清单提权父进程日志 | `out/elevation/x64/manifest-20261119-202027/elevation-probe.exe.86C.log.txt` | `2773B17ABE5D9AF5BAB2AE1A8758DC33094E80E731A09DAA605BC53DA1C98473` |
| E-005：源码传播入口 | `LocaleEmulatorPlus/Hooks/NtdllHook.cpp:1415`、`:1526`、`:2961` | n/a；源码基线 00f78be |

E-004 可通过清单提权命令重新采集，PID 依运行变化；与同目录 `shell-result.json`、`elevated.json` 一起比对 PID。

F-001：category=compatibility；severity=n/a_re；status=validated；confidence=high；evidence_ids=E-001/E-002/E-003/E-005；location=子进程传播/UAC 边界。上述三组的普通传播正常，UAC 提权后的实际进程丢失配置。

F-002：category=diagnostics；severity=n/a_re；status=validated；confidence=high；evidence_ids=E-003/E-004；location=清单提权日志。一次创建尝试的注入成功日志不足以确认最终管理员进程已转区，应核对实际 PID 和状态。

P-001：path_type=callflow：普通 launcher → `LepCreateProcess2` → 日区普通父进程 → 普通子进程（F-001 对照）→ Shell/UAC → 管理员探针（F-001 中断）→ 管理员后继进程（继续宿主配置）。系统服务参与部分依据微软文档解释。

## 时间线与后续

- 20:16：首次 x86 测试普通传播通过，UAC 随后返回 1223，无管理员状态，不计入成功测试。
- 20:19：用户要求重新弹出；x86 runas 完整测试确认中断。
- 20:20：x64 runas、x64 管理员清单两组完整测试确认相同结果。

若要补齐该能力，需要让提权路径显式携带 LEP 配置，并在获得权限的启动器中先注入再执行目标；普通权限进程单靠已有的本地 `NtCreateUserProcess` hook 无法覆盖系统服务代创建的实际目标。此处仅说明设计方向，未实现修复，也未验证从管理员启动器起步的替代路径。
