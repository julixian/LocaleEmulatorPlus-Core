# Shell 提权共同入口及 Win7/10/11 差异

## 结论

找到了可供 LEP 研究的共同入口：**`AicLaunchAdminProcess`**。本机 Win11 的 x86、x64 探针中，显式 `runas` 与管理员清单触发的提权均命中该函数，直接调用者均为 `CInvokeCreateProcessVerb::CallCreateProcess`。它位于客户端进程内，负责准备参数并向 AppInfo 发起 RPC；因此能在系统代创建目标之前接到请求。

Win7、Win10 的 x86/x64 样本也都有这个函数，9 个参数的含义保持一致。主要兼容差异是模块归属，以及 **x86 的机器级调用约定**：Win7 全部栈传参，Win10/11 前两个参数改用 ECX/EDX。这一差异无法仅凭 PDB 声明判断。

这使“在一处内部入口转交提权请求”成为有证据支持的方案，但本轮没有实现生产 hook、辅助启动器或跨 UAC 传播修复。范围见 [scope](uac-propagation-scope.md)，此前传播失败实测见 [测试报告](2026-11-19_测试-UAC子进程传播-report.md)。日期 2026-11-19，flavor=null。

## 样本与来源

`win_reverse` 现有 Win7/10 样本缺少这次所需的 Shell 模块。本轮补充下载到 `out/uac-entry/`，不覆盖已有样本：

- Win11：本机 System32/SysWOW64 的模块副本，均为 10.0.26100.9974。操作系统报告 build 26300，不能用 OS build 代替模块版本。
- Win10：10.0.19041.4717 的 `Windows.Storage.dll`，PE 从微软公共符号服务器下载，SHA-256 与 Winbindex 索引匹配；对应 PDB 按 PE 内 RSDS GUID/age 获取。
- Win7：微软官方 KB3080446 更新包中的 GDR 6.1.7601.18952 `shell32.dll`，仅解包。该更新包含这一版本的文件，见 [微软文件清单](https://support.microsoft.com/en-us/servicing/os/windows/2015/10/ms15-109-description-of-the-security-update-for-windows-shell-october-13-2015-1)。未声称与 `win_reverse` 中 USER32 的补丁版本完全一致。

| 样本 | 模块 | `AicLaunchAdminProcess` RVA | 实际传参/清栈 |
|---|---|---:|---|
| Win7 x86，6.1.7601.18952 | shell32.dll | `0xDE54C` | 9 参数走栈，`ret 0x24` |
| Win7 x64，6.1.7601.18952 | shell32.dll | `0xCD590` | Windows x64 ABI |
| Win10 x86，10.0.19041.4717 | Windows.Storage.dll | `0x1C5510` | 前 2 参数 ECX/EDX，其余走栈，`ret 0x1C` |
| Win10 x64，10.0.19041.4717 | Windows.Storage.dll | `0x1B7164` | Windows x64 ABI |
| Win11 x86，10.0.26100.9974 | Windows.Storage.dll | `0x1F69DC` | 前 2 参数 ECX/EDX，其余走栈，`ret 0x1C` |
| Win11 x64，10.0.26100.9974 | Windows.Storage.dll | `0x32CEB4` | Windows x64 ABI |

RVA 只用于标识本次样本，不是可套用到其它版本的固定偏移。

## 本机动态验证

使用已有 `tests/elevation-probe.cpp` 的普通权限 `--parent` 模式，通过 CDB 在 `AicLaunchAdminProcess` 入口截停，并终止自己的探针。四组入口定位在真正发出提权 RPC 前结束，不产生 UAC；后面的句柄权限检查单独经过一次正常 UAC。

两种启动方式在 x86/x64 上均得到以下链路：

```text
Windows.Storage!ShellExecuteExW
  → ShellExecuteNormal / CShellExecute
  → CBindAndInvokeStaticVerb
  → CInvokeCreateProcessVerb::CallCreateProcess
  → AicLaunchAdminProcess
  → AppInfo RPC
```

本机两种路径到入口时的第三参数为 `0x11`（显式 runas）和 `0x10`（清单提权），第四参数均为 `0x04080414`。这两个观察值不作为所有系统的固定规则。第三参数与第四参数是两组不同用途的标志，不能合并理解。

入口携带的目标路径和完整命令行均指向正确探针；清单提权时指向 `elevation-probe-admin.exe`。本轮动态验证覆盖 `ShellExecuteExW` 的上述两条路径，没有逐一测试 A/W 四个公开 API、COM elevation、直接 AppInfo RPC 或其它服务代启动路径。

## 参数与内部工作

六个样本 PDB 的语义声明均包含以下 9 个参数，返回值为 DWORD/ULONG 风格的 Win32 错误码，而非 NTSTATUS：

| 参数序号 | 含义 |
|---:|---|
| 1 | 应用程序路径，UTF-16 |
| 2 | 可写的完整命令行，UTF-16 |
| 3 | AIC 请求标志 |
| 4 | 进程创建标志 |
| 5 | 工作目录 |
| 6 | 所属窗口 HWND |
| 7 | STARTUPINFOW 指针 |
| 8 | PROCESS_INFORMATION 输出指针 |
| 9 | ELEVATION_REASON 输出指针 |

本机反编译可完整看到：建立 AppInfo RPC 绑定、处理工作目录/桌面/映射盘路径、整理启动信息、初始化异步 RPC、调用 `Ndr64AsyncClientCall`、等待完成、填充进程信息与提权原因，以及失败清理。Win7/10 的同名函数也包含相同 AppInfo 接口标识和创建 RPC 的核心流程。

Win11 内部可见 `AicpCreateBindingHandleEx`、窗口 band 判断等处理，与旧版的函数体和分支布局不同。对 LEP 而言，截在此函数边界可避免复制这些内部 RPC 细节；但参数、返回错误、取消、句柄和启动行为仍需保持。

部分旧版函数有非连续冷代码块，当前局部反编译保留 `JUMPOUT`，因此未将伪代码当作完整源代码，也未据此宣称所有错误路径已经复原。关键调用约定另以机器码控制流和真实返回指令核验。

## x86：PDB 声明不能直接当作 hook 原型

三版 x86 PDB 都将函数标记为原始 `__stdcall` 声明，但 Win10/11 的实际代码保存了 ECX/EDX 中的前两个参数。

Win7 x86 的入口在 SEH prologue 后从 `[ebp+8]`、`[ebp+0xC]` 读取前两个参数，返回指令位于 RVA `0xDEAE1`，为 `ret 0x24`。

Win10 x86 的入口在 SEH prologue 后有：

```asm
mov [ebp-0x2D8], ecx
mov [ebp-0x2D4], edx
```

返回指令位于 RVA `0x1C5892`，为 `ret 0x1C`。本机 Win11 x86 的返回指令位于 RVA `0x1F6F78`，同为 `ret 0x1C`；CDB 实际观察到 ECX 为应用路径、EDX 为完整命令行，栈上第一个参数已经是请求标志 `0x11/0x10`。

因此 Win10/11 这两个 x86 样本可用与 `__fastcall` 一致的桥接布局，Win7 需使用全栈布局。这里描述的是机器布局；不能只根据 PDB 的 `YG` 修饰名决定调用约定，也不能保证其它补丁版本必定沿用这一优化结果。

## 不依赖固定偏移的定位线索

六个 PE 均只有一份 UTF-16 字符串：

```text
201ef99a-7fa0-444c-9399-19ba84f12a1a
```

该字符串用于建立 AppInfo RPC 绑定。已核对的 IDA 引用直接落在 `AicLaunchAdminProcess` 内；本机/Win10 x64 的反编译也显示由该函数使用此值建立绑定。

| 样本 | GUID 字符串 RVA | 已核对的引用 RVA |
|---|---:|---:|
| Win7 x86 | `0xDE8D0` | `0xDE64C` |
| Win7 x64 | `0x476F00` | `0xCD722` |
| Win10 x86 | `0x620B8` | `0x1C5613` |
| Win10 x64 | `0x63B510` | 反编译确认 |
| Win11 x86 | `0x80258` | `0x1F6AE0` |
| Win11 x64 | `0x78A870` | 反编译确认 |

这提供了候选定位方法：先确定所属模块，查唯一 GUID 字符串，再解析代码引用、恢复所属函数入口，并核验调用方、RPC 行为和参数布局。x64 可以利用异常展开信息辅助确定函数边界；x86 还需要处理 SEH prologue、非连续代码和调用点。

**本轮验证的是定位线索，尚未编写或验证生产级自动定位器。** GUID 唯一不能单独成为打补丁条件；也不建议把所有 RPC 客户端调用作为 hook 入口。

Win7 x64 的直接调用者是 `_SHCreateProcess`，Win10/11 样本则是 `CInvokeCreateProcessVerb::CallCreateProcess`。Win7 x86 已找到唯一直接 call（RVA `0xDE520`），但当前局部 IDA 数据未恢复该调用点的所属函数名；未把它强行标注为某个类方法。

## 返回句柄能否直接用于注入

本机额外在一次正常 UAC 执行中检查了入口返回时的 PROCESS_INFORMATION。目标已提权，PID 39900，自报完整性 RID 12288；入口返回 EAX=0。

| 句柄 | GrantedAccess | 对 LEP 注入的意义 |
|---|---:|---|
| 目标进程 | `0x121101` | 缺少 PROCESS_VM_READ/WRITE/OPERATION 等权限 |
| 目标主线程 | `0x121802` | 有暂停/恢复权限，但缺少 GET_CONTEXT/SET_CONTEXT |

因此，至少本机这条路径中，不能在 `AicLaunchAdminProcess` 返回后直接拿系统给普通调用方的句柄完成现有注入。只暂停目标或把创建标志加上 CREATE_SUSPENDED，并不能补出句柄权限。

该检查只查询句柄权限，没有写目标内存、修改目标线程或测试其它访问提升方法；随后正常继续 Shell 流程，让探针运行并退出。Win7/10 未实测返回句柄权限，不能把这两个掩码泛化到旧系统。

## 对 LEP 的实现判断

该入口值得作为下一步原型的拦截点：针对已验证的 Shell 路径，可以研究在一个内部函数处转交请求，暂不需要分别接管四个公开 ShellExecute API。现有模块通知/内存 patch 框架可作为安装和卸载的基础；当前仓库尚未接入对应模块及函数。

实现时至少需要区分 Win7 的 shell32 与 Win10/11 的 Windows.Storage，并按实际机器码选择 x86 桥接布局。后续可以保留原始提权流程，由已获得管理员权限的 LEP 辅助启动器创建并注入真正目标，再把真正目标的句柄、PID、启动结果和提权原因交回原调用者。该转交方案尚未实现，也没有验证取消、调用者等待、所有启动标志及跨账户提权的完整行为。

## Evidence → Finding → Path

全部原始材料位于仓库的 `out/uac-entry/`，该目录被 Git 忽略。完整 PE/PDB 标识、PE SHA-256、日志 SHA-256 与下载信息索引见 `evidence-manifest.json`、`win10-downloads.json` 和 `win7-pdb-downloads.json`。

E-001 至 E-005 的 content_hash 为 E-006 清单中逐文件 SHA-256；E-006 中 `evidence-manifest.json` 的 content_hash 为 `F032B8596C9661B81355FE7A87190111BA589C55EC6A9264983D471DD9E04797`。清单核验已通过，四份入口跟踪和 x86 RET 布局也已作交叉检查。

为便于在其它机器重新取得准确样本，六个 PE 的 SHA-256 如下：

| 样本 | SHA-256 |
|---|---|
| Win7 x86 | `4E65B519821552D3B02736883B695C4953E9DDEA1A61F22FF37D67DBBA7BBD4D` |
| Win7 x64 | `5DF8FE90FC504E836153605AC3267BAACC7E2B1408D78ECD95C241A6D5AFBCCA` |
| Win10 x86 | `6CB63AE61F1F56FF9476CD86919644F24065B00C470277FEE798B10B57F7031A` |
| Win10 x64 | `807FA5092E6953B79C0EF533C5B2C3133867264C970749EE8B0E2ADBF94F244A` |
| Win11 x86 | `3ADB30122530E5C585E91CF613248B26796D403086B4826811E9179883D5D1B8` |
| Win11 x64 | `D31D11307CB87A6A9E26F7B805B480717B78E8833DCECE75CBEE319D14654F74` |

| Evidence | source_ref | 内容与复现 |
|---|---|---|
| E-001 | `hit-runas-x64.log`、`hit-manifest-x64.log` | x64 两条路径均到 AIC 入口，见下方 CDB 命令 |
| E-002 | `hit-runas-x86.log`、`hit-manifest-x86.log` | x86 两条路径均到 AIC 入口，前两参数为 ECX/EDX |
| E-003 | 六个 `*-aic.json`、四个 `*-locator.json` | 匹配 PE/PDB 的 IDA 输出；旧版冷块局部恢复限制已说明 |
| E-004 | `x86-abi-assembly.json` | 三版 x86 的实际指令控制流、RET 清栈值及直接调用点 |
| E-005 | `aic-handles-x64.log`、`handle-test/elevated.json` | AIC 成功返回时句柄权限及真正目标的提权状态 |
| E-006 | `evidence-manifest.json`、下载元数据/HTML | 六个 PE、对应 PDB RSDS 标识及原始材料哈希 |

F-001：severity=n/a_re；status=validated；confidence=high；evidence_ids=E-001/E-002/E-003；location=`AicLaunchAdminProcess`。它是本机两种 Shell 提权路径的共同客户端入口，旧版样本存在相同语义入口。旧版只做静态验证。

F-002：severity=n/a_re；status=validated；confidence=high；evidence_ids=E-002/E-003/E-004；location=x86 参数边界。Win7 全栈，与 Win10/11 的 ECX/EDX+7 栈参数布局不同；不能共用一个无条件 stdcall 原型。

F-003：severity=n/a_re；status=validated；confidence=high；evidence_ids=E-005；location=AIC 返回 PROCESS_INFORMATION。普通调用方收到的本机句柄缺少现有注入所需权限，返回后直接注入的简化方案在该权限条件下不成立。

F-004：severity=n/a_re；status=candidate；confidence=medium；evidence_ids=E-003/E-004/E-006；location=入口定位。唯一 AppInfo GUID 及其引用可作为跨版本定位锚点，但生产定位器和失败回退尚未实现。

P-001：path_type=callflow：A 中 ShellExecuteExW → Shell 静态 verb 创建逻辑 → AicLaunchAdminProcess（F-001）→ AppInfo RPC → 管理员 B；普通权限 A 的 NtCreateUserProcess hook 无法覆盖服务侧最终创建。

P-002：path_type=solve：按模块选样 → AppInfo GUID 及代码引用定位候选入口（F-004）→ 核对 x86 机器布局（F-002）→ 研究由管理员辅助进程接手创建/注入；不能依赖受限返回句柄直接写 B（F-003）。最后一步是设计方向，尚未实现。

## 复现入口跟踪

先通过 `tests/run-elevation.ps1 -Architecture x64 -Mode manifest -BuildOnly` 准备两个探针。CDB 中以源码对应的普通权限探针执行 `--parent`，结果目录需要预先存在：

```powershell
Set-Location D:\VSProj\LocaleEmulatorPlus-Core
New-Item -ItemType Directory -Path out/uac-entry/repro -Force | Out-Null
& 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe' `
  -y 'srv*D:\VSProj\LocaleEmulatorPlus-Core\out\uac-entry\symbols*https://msdl.microsoft.com/download/symbols' `
  -logo out/uac-entry/repro-x64.log `
  -c 'bu Windows_Storage!AicLaunchAdminProcess ".echo AIC_ENTRY; du @rcx; du @rdx; r r8d,r9d; dq @rsp+28 L5; kn 16; .kill; q"; g' `
  out/elevation/x64/elevation-probe.exe --parent `
  D:/VSProj/LocaleEmulatorPlus-Core/out/uac-entry/repro `
  D:/VSProj/LocaleEmulatorPlus-Core/out/elevation/x64/elevation-probe-admin.exe manifest
```

改成目标 `elevation-probe.exe`、模式 `runas` 可对照显式提权。x86 用 x86 CDB，入口改为检查 ECX/EDX 与 ESP 上的参数，详见已有四份跟踪日志。若系统符号未成功解析，不要让探针继续越过未知断点；先解决符号加载再重跑。

IDA 分析使用技能附带的 `start.ps1` / `open.ps1`，仅打开副本。分析脚本在 `out/uac-entry/decompile-aic.py`、`locator-aic.py`；RPC 调用器为 `ida-eval.ps1`。它们是本轮材料，尚未整理成仓库正式工具。第三方需先取得匹配样本/PDB，不能仅靠报告中的 RVA 对其它版本重放。

## 时间线

- 20:41–20:46：加载本机符号，确认 x64 两种提权路径都命中 AicLaunchAdminProcess。
- 20:47–20:53：补充官方 Win7/10 PE/PDB，完成三个版本的 x64/x86 入口和声明对照。
- 20:54–20:59：核对 x86 的寄存器传参与 RET 清栈，完成本机 x86 两条路径跟踪。
- 20:59：经正常 UAC 检查 AIC 返回句柄权限；目标正常执行并退出。
- 后续：整理跨版本差异及边界，未改动生产实现。
