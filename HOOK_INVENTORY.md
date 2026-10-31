# LocaleEmulatorPlus Hook 与状态改写清单

本文档按“工作项”组织当前 hook、模式查找和非 hook 状态改写。公共 hook 机制只在前面说明一次；每个工作项分别说明 hook/改写时机、x86/x64 差异、查找方式和作用。

## 公共机制

### 初始化顺序

`LocaleEmulatorPlus` 初始化时先校验并挂接 `LEP_BOOTSTRAP_PAYLOAD`，从其中读取 Loader 已解析的 `LEP_RUNTIME_ENVIRONMENT` 配置，再构造注册表映射、初始化 HookPort、安装 ntdll hook，随后注册 `LdrRegisterDllNotification()` 处理后加载模块。若 kernel32/kernelbase 已经加载，会主动调用 `HookKernel32Routines()`；USER32/GDI32 则通常由 DLL notification 触发。

### Inline Hook

宏定义在 `LocaleEmulatorPlus/LocaleEmulatorPlus.h`。

- `LepHookFromEAT(base, name)`：以 `lep::HashExportName(#name)` 查找模块导出，入口跳到 `LepName`，trampoline 存到 `HookStub.StubName`。
- `LepHookFromEATOp(base, name, op)`：同上，但显式指定 patch op。
- `LepFunctionJump(name)`：对已经扫描/查表得到的函数入口安装跳转。
- `LepFunctionCall(name)`：改写 call site，不改写函数入口。

`OpJump` 是 5 字节相对跳：

```asm
E9 rel32
```

目标必须在当前指令结束地址的 +/-2GB 范围内。

`OpJumpIndirect` 在 x64 下是 14 字节 RIP-relative 间接绝对跳：

```asm
FF 25 00 00 00 00
dq target
```

它支持任意距离的目标地址，入口需要容纳 14 字节完整指令。

普通 x64 inline hook 的入口选择顺序为：5 字节 `E9 rel32` 直跳、附近 relay stub、14 字节入口覆盖。`NoAbsoluteJump` 把可选范围限制为直跳和 relay。

### Syscall HookPort

实现位置：`LocaleEmulatorPlus/HookPort.cpp`。x86 和 x64 共用 typed wrapper、callback 表与注册逻辑。HookPort 枚举 ntdll 的 `Zw*` 导出，建立 hash/service id 索引，并在某个 syscall 第一次注册 callback 时 patch 对应的 `NtXxx`/`ZwXxx` 入口。

每个受支持的 syscall 都必须在 `FindWrapperByHash()` 中有一个参数类型和调用约定与原函数一致的 wrapper。调用过程在两种架构上相同：

1. 调用者照常调用 ntdll/win32u/gdi32 的 syscall stub，入口 patch 跳到对应 wrapper，原调用者的参数和返回地址保持正常函数调用布局。
2. wrapper 通过 hash 找到 `SYSCALL_INFO` 和保存的 original，然后由 `DispatchTypedFilter()` 按 `FilterBitmap` 从低位到高位调用已注册 callback。
3. callback 每次返回后立即检查 `FltInfo.Action`。`ContinueSystemCall` 进入下一个 callback；`BlockSystemCall` 结束循环，并将当前 callback 的普通返回值交给原调用者。
4. callback 链以 `ContinueSystemCall` 结束时，wrapper 调用 original，并把 syscall 返回值交给原调用者。

Action 语义：

| Action | callback 链 | original | 对原调用者可见的返回值 |
| --- | --- | --- | --- |
| `ContinueSystemCall` | 进入下一项；默认值 | 链结束后执行 | original 的返回值 |
| `BlockSystemCall` | 在当前项结束 | 跳过 | 当前 callback 的返回值 |

callback 可以先通过 `HpCallSysCall()` 或 `HpGetSystemCallOriginal()` 调用 original，再设置 `BlockSystemCall`，让这次 callback 的返回值成为最终结果。若保持默认的 `ContinueSystemCall`，wrapper 会在 callback 链结束后调用 original。

每个 syscall 的 callback 表最多容纳 32 项，`FilterBitmap` 同时表示槽位占用情况和调用顺序。项目中的注册点各占用一个槽位。x86 original trampoline 从入口覆盖范围之后恢复执行；x64 original 是独立的人工 syscall stub，两者都直接绕过 wrapper。callback 抛出的异常沿普通调用栈传播给调用者。

#### x86 入口与 original

x86 parser 要求 syscall stub 从 `mov eax, service_id` 开始。首次注册 callback 时，patch manager 在该 `NtXxx`/`ZwXxx` 入口安装普通 5 字节相对跳，并生成 trampoline；`SYSCALL_INFO.FunctionAddress` 随后指向该 trampoline。

trampoline 复制入口被覆盖的完整指令，末尾跳回原 stub 中未覆盖的位置。native x86 随后进入 `KiFastSystemCall`；WOW64 按系统 stub 的布局进入 `fs:[0xC0]` 或 `Wow64Transition`。系统调用完成后返回 wrapper，wrapper 再按 `stdcall` 返回原调用者。

#### x64 入口与 original

x64 HookPort 枚举 ntdll `Zw*` 导出，要求前 `0x20` 字节内包含 `mov r10, rcx`、`mov eax, service_id` 和 `syscall; ret`。它同时识别两种 stub：

```asm
4C 8B D1              ; mov r10, rcx
B8 xx xx xx xx        ; mov eax, service_id
0F 05                 ; syscall
C3                    ; ret
```

以及带运行时入口选择的现代形式：

```asm
4C 8B D1                         ; mov r10, rcx
B8 xx xx xx xx                   ; mov eax, service_id
F6 04 25 08 03 FE 7F 01          ; test byte ptr [0x7FFE0308], 1
75 03                            ; jne int2e_path
0F 05                            ; syscall
C3                               ; ret
CD 2E                            ; int 2e
C3                               ; ret
```

`0x7FFE0308` 是 `KUSER_SHARED_DATA.SystemCall`。parser 在 selector、`syscall; ret` 和 `int 2e; ret` 符合上述布局时记录 `SystemCallHasInt2ESelector`。安装 hook 时，HookPort 为该 syscall 生成一段可直接调用的最小 syscall stub，下文称为“人工原始调用 stub”；带 selector 的入口会生成同样的动态分支。

user32/win32u/gdi32 中找到的 syscall stub 通过 `HpAddSystemCallByRoutine()` 或 `HpAddSystemCallByRoutineRange()` 额外注册。

x64 HookPort 还有一个 syscall 专用的 12 字节绝对跳：

```asm
48 B8 imm64           ; mov rax, wrapper
FF E0                 ; jmp rax
```

这条跳转支持任意距离的 wrapper，并会改写 `rax`。syscall stub 本身用 `mov eax, service_id` 定义 `rax`，original 又由人工 stub 执行，因此入口的 `rax` 改写符合 wrapper 路径的寄存器约定。标准 x64 syscall stub 常见长度为 11 字节，12 字节覆盖也便于把边界限制在 `ret` 及其 padding 内。普通 inline hook 则使用寄存器透明的 14 字节 `FF 25 [rip+0]` 绝对跳。

x64 syscall 入口 patch 策略：

- 优先 5 字节 `E9 rel32` 直跳到 wrapper。
- wrapper 超出相对跳范围时，在 syscall stub 附近申请 relay，入口 `E9` 到 relay，relay 再跳 wrapper。
- relay 失败后，只在 `ret` 位于 12 字节覆盖范围末尾，或 `ret` 后面直到覆盖末尾全是 padding 时，才允许上面的 12 字节绝对跳。
- 其它布局返回不支持状态，确保覆盖范围止于当前 stub。

人工原始调用 stub 按解析结果生成独立代码，依次执行 `mov r10, rcx`、`mov eax, service_id` 和系统调用尾部。带 `KUSER_SHARED_DATA.SystemCall` selector 的版本按运行时状态选择 `syscall` 或 `int 2e`。内核返回后，人工 stub 的 `ret` 回到 wrapper，wrapper 再返回原调用者。

## 工作项

### 1. 初次进程注入：`LdrLoadDll` hook

时机：`LoaderDll` 创建挂起的目标进程时。

做法：从磁盘加载 `LocaleEmulatorPlus_*` 的本地镜像并重定位后写入目标进程；bootstrap 元数据保存目标 `LdrLoadDll` 原始字节、原页面保护和 Payload 偏移/大小。目标入口改写为跳转到镜像内的 `LoadFirstDll` 后，首次调用先恢复原字节和页面保护，再校验 Payload 并完成初始化，最后继续原始 `LdrLoadDll`。

作用：让 LEP 在 kernel32/kernelbase 初始化前取得执行权。

### 2. 子进程传播：`NtCreateUserProcess`

时机：`HookNtdllRoutines()` 始终安装 `NtCreateUserProcess` filter。真实 `NtCreateUserProcess` 成功后，`LepNtCreateUserProcess()` 调 `InjectSelfToChildProcess()`。

共同的创建时序：wrapper 先从调用者传入的 `ThreadFlags` 记下“原本是否要求挂起”，然后无论同架构还是跨架构，都把实际传给 `NtCreateUserProcess` 的标志强制改为挂起。系统调用成功后，父进程趁子线程尚未运行完成注入；随后按照刚才记下的原始标志决定是否调用 `NtResumeThread`。

父进程先根据自己的当前配置重新构建 `LEP_BOOTSTRAP_PAYLOAD`，再从磁盘读取对应位数的 `LocaleEmulatorPlus_*` 镜像。镜像按目标地址重定位后，与 Payload 一起写入子进程的一块连续内存；Payload 位于对齐后的 PE 镜像尾部。bootstrap 元数据同时写入 Payload 偏移/大小、目标 `LdrLoadDll` 原始字节和页面保护，再把目标入口 patch 到映像中的 `LoadFirstDll`（x86 使用 `E9 rel32`，x64 使用 `FF 25 [rip+0]` 绝对跳）。第一次调用恢复原入口、校验并读取 Payload、完成初始化；初始化后的子进程继续拥有 `NtCreateUserProcess` filter，因此它创建后代时会重复这套传播流程。

同架构传播：父子进程使用相同位数时，父进程可以直接按本架构格式操作目标进程。

跨架构传播：父进程不能把本架构的指针、Payload 内部地址或 DLL 镜像直接交给另一位数进程。父进程先用 `CreateFileMappingW(INVALID_HANDLE_VALUE, ...)` 创建一个按名称访问的、由系统分页文件提供后备存储的临时 section，再用 `MapViewOfFile` 写入 broker 配置。这个 `Local\LEP_BROKER_<childPid>_<parentPid>` 映射只是父进程和 broker 之间的传输邮包。

邮包中的 `LEP_BOOTSTRAP_PAYLOAD` 使用架构无关的固定宽度字段：环境块和 DLL 路径通过相对于 Payload 起点的偏移定位，内部环境使用 `LEP_RUNTIME_ENVIRONMENT`。父进程随后启动目标位数的 `rundll32.exe`，调用 `LoaderDll_*!LepBrokerEntry`；broker 校验邮包中的 Payload，从磁盘读取对应位数的 LEP DLL，在本地按目标进程中分配的映像基址重定位，并填充 `LEP_FIRST_DLL_BOOTSTRAP_DATA`。随后将镜像和 Payload 的原始字节写入目标进程，Payload 紧跟在对齐后的映像尾部，再 patch 子进程的 `LdrLoadDll`。Payload 不会在 broker 中重新序列化或解析时区；其内部偏移相对于自身起点，更换进程或放置地址不需要改写这些偏移。broker 将最终状态写回邮包的 `Result` 字段，父进程等待 broker 退出后读取结果。

broker 启动使用线程级的 `lep::ActiveFrame` 标记，其首成员是标准 `TEB_ACTIVE_FRAME`。父进程准备通过 `CreateProcessW` 启动 broker 时把类型为 `LEP_BROKER_LAUNCH_CONTEXT` 的 frame 压入当前线程，`CreateProcessW` 返回后弹出；因为父进程自己的 `NtCreateUserProcess` hook 也会在这条线程调用链上看到 broker 创建，`InjectSelfToChildProcess()` 发现该 frame 就跳过注入。这样不会影响其他线程同时创建的普通子进程，也只阻止“启动 broker → 又给 broker 注入 → broker 再启动 broker”的递归重入。

注意：代码直接使用当前进程解析出的 `LdrLoadDll` 地址写入 bootstrap 元数据。这隐含假设同架构子进程 ntdll 映射基址与当前进程一致，因此 `ntdll!LdrLoadDll` 地址一致；跨架构 broker 会在自身目标位数上下文中重新解析地址。

作用：让目标进程创建的子进程自动继承 LEP 环境。

### 3. `NtContinue`

时机：`HookNtdllRoutines()`。

x86：`SearchLdrInitNtContinue()` 扫描 `LdrInitializeThunk` 前 `0x5B` 字节，改写其中目标为 `NtContinue` 的 call site。`LepLdrInitNtContinue()` 在 loader 即将恢复线程初始 context 前，把当前线程的 `TEB.CurrentLocale` 设置为 `LEPB.LocaleId`，然后通过 `StubLdrInitNtContinue` 调用原 `NtContinue`。hook 范围限定在 loader 的这处调用。

x64：不再扫描 `LdrInitializeThunk`，也不安装 x86 使用的 loader call-site hook。HookPort 直接对 `NtContinue` syscall 入口安装 filter，覆盖经过该 ntdll stub 的调用。`LepNtContinue()` 设置当前线程的 `TEB.CurrentLocale`，保持 `CONTEXT`、`TestAlert` 和默认 `ContinueSystemCall` Action，随后由 wrapper 调用 original。

作用：`TEB.CurrentLocale` 是线程级状态。这个 hook 以 loader 结束线程初始化并通过 `NtContinue` 恢复用户执行现场的时刻为锚点，确保新线程进入用户入口前看到 LEP 的目标 locale。

### 4. ntdll locale/sysinfo syscall filter

时机：`HookNtdllRoutines()`，非 loader 进程路径。

函数：

- `NtQuerySystemInformation`：对 `SystemCurrentTimeZoneInformation` 返回配置快照中的完整时区数据。时区 ID 解析、东京默认名称和用户名称覆盖均在 Loader 入口完成，查询 hook 不再改写名称。
- `NtInitializeNlsFiles`：提供目标 NLS 文件映射和 locale 信息。
- `NtQueryDefaultLocale`：返回目标 LCID。

x86/x64：均走 HookPort filter。

作用：让查询 ntdll 层默认区域、时区和 NLS 文件的路径看到目标 locale。

### 5. 注册表重定向：`NtQueryValueKey`

配置：GUI/XML 的 `RegistryRedirectionMode` 默认值为 2，并原样传入 `LEPB.RegistryRedirectionMode`。

| 模式 | Core 构造的查询覆盖 |
|---|---|
| 0 | 3 项：HKLM NLS CodePage 的 `ACP`、`OEMCP`，以及 HKLM NLS Language 的 `Default`。 |
| 1 | 4 项：HKLM NLS CodePage 的 `InstallLanguage`、`Default`、`OEMCP`、`ACP`。 |
| 2 | 模式 1 的 4 项，再加 HKCU International 的 `Locale`、`LocaleName`，以及 Desktop 的 `PreferredUILanguages`、`MuiCached\\MachinePreferredUILanguages`，共 8 项。 |

时机：模式 0/1 的表在 Core 主初始化阶段生成。模式 2 的前 4 项也在此阶段生成；后 4 项需要先把 LCID 转成 culture name，因此被有意隔离到 `HookNtdllRoutines()` 中，并且只在 `NtInitializeNlsFiles`/`NtQueryDefaultLocale` 过滤器安装完成后追加。这样 `RtlLCIDToCultureName` 的惰性初始化看到的是目标 system locale，不会用宿主 LCID 提前填充 ntdll named-locale/NLS 缓存。

当 `RegistryRedirectionEntry` 非空或 UI-language 模式为 2 时安装 `NtQueryValueKey` filter；UI-language 模式 2 还借此处理虚拟 MUI 键，与上述 0/1/2 注册表模式相互独立。

x86/x64：均走 HookPort filter。

作用：把 NLS/codepage/language 相关注册表查询改写为由 `LEPB` 数值字段派生的目标值，让读注册表的路径都符合目标 locale。

### 6. 自定义代码页转换：`RtlCustomCPToUnicodeN`

时机：`HookNtdllRoutines()`。

状态：实现由 `LEP_ENABLE_CUSTOM_CP_TO_UNICODE_HOOK` 控制，当前值为 `0`，`HookNtdllRoutines()` 不安装该 hook。

启用时：inline hook `RtlCustomCPToUnicodeN` 为 `LepCustomCPToUnicodeN`。当调用者传入既不是目标代码页，也不是 UTF-8 的 CP table 时，先用目标 ANSI codepage table 重新初始化，再执行转换。

用途：强制显式传入的 custom CP table 使用目标 ACP。默认 ANSI/OEM 转换由基础 NLS 表同步负责（LE 遗留代码，LEP 目前测试下来应该是不需要）。

### 7. 基础 NLS 表和 PEB/TEB codepage

时机：`LepGlobalData::Initialize()` 和 `HackAnsiOemCodeHashNodes()`。

这两处会做一部分重复同步，原因是初始化阶段负责建立 LEP 自己的目标 NLS 表；而 `HackAnsiOemCodeHashNodes()` 发生在 kernel32/kernelbase 加载或通知阶段，需要把 ntdll、PEB/TEB、user32 等已经存在或刚建立的 codepage cache 再压回目标状态。

首次初始化时读取目标 ACP/OEMCP NLS 文件和 `l_intl.nls`，复制到 `CodePageMapView`，并写入 PEB 三个 NLS 表指针：

- `PEB.AnsiCodePageData`
- `PEB.OemCodePageData`
- `PEB.UnicodeCaseTableData`

然后用 `CodePageMapView` 调 `RtlInitNlsTables()` / `RtlResetRtlTranslations()`，随后调用 `LepSyncNtdllNlsGlobals()` 同步 ntdll 导出的全局变量 `NlsAnsiCodePage`、`NlsMbCodePageTag`、`NlsMbOemCodePageTag`。

后续 `HackAnsiOemCodeHashNodes()` 按同一套目标 NLS 表重同步运行时状态：

1. 写架构相关 ANSI/OEM codepage pair：x64 写 `PEB + 0x34C`，x86 写 `PEB + 0x228`。
2. x64 同步 user32 client codepage。
3. 用 `CodePageMapView` 里的目标 ACP/OEMCP/case table 调 `RtlInitNlsTables()`。
4. 把得到的 `NLSTABLEINFO` 传给 `RtlResetRtlTranslations()`，让 ntdll 默认转换状态切到目标表。
5. 调 `LepSyncNtdllNlsGlobals()` 写 ntdll 导出 NLS 全局变量。
6. 调私有 `RtlpInitCodePageTables` 做额外内部 cache refresh；失败只记录。

作用：让 ntdll/default ANSI 转换使用目标代码页数据。

### 8. x64 user32 client codepage 改写

时机：x64 的 `HackAnsiOemCodeHashNodes()` 中调用 `LepSyncUser32ClientCodePage()`。

x64：`LepGetWin32ClientInfo()` 使用 `TEB + 0x800`，把 `LEP_WIN32_CLIENT_INFO_CODE_PAGE_INDEX == 19` 这一项的低 16 位写为目标 ANSI codepage。

作用：修正 user32 client-side ANSI codepage cache。某些 user32 A/W 转换路径查 client info，而不是调用普通 ntdll 转换 API。

### 9. ntdll 私有 codepage table refresh

时机：`HackAnsiOemCodeHashNodes()`。

查找：`FindRtlpInitCodePageTables()` 先取 ntdll 的 `.text` 范围，再扫描所有 `call rel32` call-site；目标为导出 `RtlInitCodePageTable` 的 call-site 会被当作候选。候选 call-site 如果距离导出的 `RtlInitNlsTables` 小于 `0x80` 字节，则直接忽略。剩下的候选从首个 call-site 向前最多 `0x120` 字节找连续 `CC/90` padding 边界，以 padding 后第一个字节作为函数起点；再从这个首个 call-site 向后最多 `0x100` 字节找第二个 call 到 `RtlInitCodePageTable`，都能找到就将其视为 `RtlpInitCodePageTables` 函数地址。

调用：`LepInitNtdllCodePageTables(ansi, oem)` 调用找到的内部例程。

作用：刷新 `RtlResetRtlTranslations()` 之外的 ntdll 内部 codepage table/cache。

### 10. kernelbase ANSI/OEM code hash node refresh

时机：`HackAnsiOemCodeHashNodes()`。

状态：`FindSetupAnsiOemCodeHashNodes()` 和 `LepSetupAnsiOemCodeHashNodes()` 的实现保留，`HackAnsiOemCodeHashNodes()` 当前不在函数中调用。

启用时：Windows 10 build 19042+ 从 kernelbase 入口依次定位 `BaseNlsDllInitialize`、`NlsProcessInitialize` 和私有 `SetupAnsiOemCodeHashNodes`，再调用该私有例程重建 ANSI/OEM code hash nodes。

用途？：刷新 kernelbase 自己的 ANSI/OEM codepage cache。该私有例程布局随系统版本变化，当前由 PEB、ntdll 和 user32 的 codepage 同步覆盖正常转换路径。

PS: 这个我看了很久逆向信息和当时的 [PR](https://github.com/xupefei/Locale-Emulator-Core/pull/3)，但是依然搞不明白为什么当初会把找到的这个玩意叫 `SetupAnsiOemCodeHashNodes` 并使用它，
它在 ida 中的名字和实际功能都完全对不上原代码中似乎预期的作用。

### 11. kernelbase user-default NLS cache node 预热

时机：`HookKernel32Routines()` 处理 `KERNELBASE.dll` 时调用一次 `HackUserDefaultLCID2()`；随后处理 `KERNEL32.dll` 时不重复执行。

查找：直接解析 `KERNELBASE.dll!GetNamedLocaleHashNode` 导出。

做法：临时 hook `GetNamedLocaleHashNode` 为 `LepGetNamedLocaleHashNode`，调用 `GetUserDefaultLCID()` 触发 cache 初始化/更新后立即恢复。hook 只改写位于 KernelBase 映像内的内部 length-prefixed locale-name 缓冲区。

作用：让 user-default cache slot 指向目标 named-locale hash node。

### 12. UI-language 模式与 Win32 API

配置 `HookUILanguageMode` 的默认值为 1，传入 `LEPB.HookUILanguageMode`。

| 模式 | UI-language Hook |
|---|---|
| 0 | 不安装 UI-language 专用 Hook。 |
| 1 | 对 `kernel32!GetSystemDefaultUILanguage` 和 `GetUserDefaultUILanguage` 安装 Win32 inline Hook，返回目标 LANGID。 |
| 2 | 不安装上述 Win32 inline Hook，改用下节的 native UI API 与虚拟 MUI 安装状态 Hook。 |

模式 1 在 `HookKernel32Routines()` 处理 `KERNEL32.dll` 时安装，`UnHookKernel32Routines()` 中恢复。

### 13. install UI language 与虚拟 MUI 安装状态

时机：仅当 `HookUILanguageMode == 2` 时，由 `HookNtdllRoutines()` 初始化并安装。

入口：

- `NtQueryInstallUILanguage`：直接返回目标 `LEPB.LocaleId`。
- `NtQueryDefaultUILanguage`：直接返回目标 `LEPB.LocaleId`。
- `NtQueryLicenseValue("Kernel-MUI-Language-Allowed")`：返回目标 culture name。
- `NtOpenKey`、`NtClose`、`NtEnumerateKey`、`NtQueryKey`、`NtEnumerateValueKey`、`NtQueryValueKey`：在 native MUI loader 访问 `HKLM\System\CurrentControlSet\Control\MUI\UILanguages` 时构造目标语言项。

先安装 `NtInitializeNlsFiles`/`NtQueryDefaultLocale` 过滤器，再准备虚拟 MUI 文化名，避免该转换提前用真实系统 LCID 初始化 ntdll 的 NLS 映射。与注册表模式 2 的 culture-name 生成一样，这段代码被有意隔离在两个 locale filter 后面；区别是虚拟 MUI 还会在 native UI-language/注册表过滤器安装之前读取真实安装 UI language 和真实 `InstallLanguageFallback`。如对目标为 `ja-JP`、宿主为 `zh-CN` 的机器，虚拟状态的逻辑顺序是 `ja-JP -> zh-CN -> 宿主原 fallback`。

虚拟目标键提供 `DefaultFallback`、宿主语言名值、`LCID` 和 `Type`；对 NLS Language 键的 `InstallLanguageFallback` 查询返回宿主安装语言及其原 fallback。

### 14. `NtUserCreateWindowEx`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：有 win32u 时按名称取 `NtUserCreateWindowEx` 并 inline hook；无 win32u 的兼容布局使用 `FindNtUserCreateWindowEx(user32)`：查 `ntdll!RtlQueryInformationActiveActivationContext` 的 IAT slot，遍历 relocation 找 `FF 15 [slot]` 调用点，再向后最多 `0x150` 字节找 syscall stub。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserCreateWindowEx`，注册 HookPort filter。

x64 user32 兼容布局：先从 `CreateWindowExW/A` 找 internal `CreateWindowEx`；在其中最多 `0x2C0` 字节内找包含立即数 `0xC0000000` 的指令；之后第一个 call 到 user32 内部函数视为 `VerNtUserCreateWindowEx`；再在其中最多 `0x300` 字节找第一个 call 到 x64 syscall stub，作为 `NtUserCreateWindowEx`。

参数版本差异：

- Win7：15 参数，x64 末尾 activation context 句柄为 `ULONG_PTR`。
- Win8/8.1：16 参数，x64 末尾 activation context 句柄为 `ULONG_PTR`。
- Win10+：17 参数，x64 末尾 activation context 句柄为 `ULONG_PTR`。

版本选择由 `Nt_QueryOsVersion()` 的结果决定。

作用：把 ANSI 窗口/类名数据按目标代码页转换，包装 A 窗口过程，并让窗口创建走 native 路径。

### 15. `NtUserMessageCall`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：有 win32u 时按名称取 `NtUserMessageCall` 并 inline hook；无 win32u 的兼容布局使用 `FindNtUserMessageCall2(user32)`：查 `kernel32!GlobalLock`、`GlobalUnlock`、`GlobalFree` 的 IAT 项，遍历 user32 relocation table 找这三个导入指针连续出现的位置，再在前两个引用之间找 syscall stub。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserMessageCall`，注册 HookPort filter。

x64 user32 兼容布局：从 `SendNotifyMessageW/A` 开始，各扫 `0x30` 字节，第一个 call 到 x64 syscall stub 的目标视为 `NtUserMessageCall`。

作用：拦截 user32 消息派发底层路径，转换相关 ANSI 字符串消息并通过 W 语义派发。

### 16. ANSI 窗口过程包装及边界

LEP 为 ANSI 窗口安装一个 Unicode 包装过程：系统先调用 LEP 的 `WindowProcW()`，LEP 按目标代码页转换参数，再调用程序原来的 ANSI 窗口过程。这个包装在窗口创建期间由 `NtUserCreateWindowEx`/CBT 路径安装，也会在程序通过 SetWindowLongA/PtrA 安装 ANSI 窗口过程时安装。

#### 保存和替换窗口过程

原过程保存在该窗口的属性列表中，属性名为 `L"Asuna"`，属性值供 `CallWindowProcA()` 调用。LEP 内部的 `SetWindowDataA()` 封装 SetPropW 保存这个值，`GetWindowDataA()` 封装 GetPropW 读取这个值；`AtomAnsiProc` 是属性名对应的 atom 编号。

`InitUnicodeProc()` 用 SetWindowLongPtrW 把窗口的 `GWLP_WNDPROC` 替换成 `WindowProcW`。每次消息到达时，包装过程根据 hwnd 查出保存的原过程。程序以后通过 A 接口更换窗口过程，LEP 就更新这个属性；GetWindowLongA/PtrA hook 返回属性里的过程值，IsWindowUnicode hook 返回 FALSE，使程序继续按 ANSI 窗口使用它。

#### 向原 ANSI 过程交付字符

`CallAnsiCharacterProc()` 把 Unicode 字符编码为目标 ACP 的字节，再根据消息类型选择交付方式。下文统一使用 CP936 的“中”作为例子：Unicode 值为 `0x4E2D`，编码字节为 `D6 D0`。

**`WM_CHAR`：逐字节交付。** 双字节 WM_CHAR 交给普通窗口时，每个字节调用一次原过程，前一次返回后再调用下一次：

```cpp
CallWindowProcA(PrevProcA, hwnd, WM_CHAR, 0xD6, lParam);
CallWindowProcA(PrevProcA, hwnd, WM_CHAR, 0xD0, lParam);
```

双字节 WM_CHAR 交给 EDIT 控件时，EDIT 会在首字节回调期间取得尾字节。原生 USER32 为这类交付准备尾字节缓存；LEP 手工拆字节调用原过程后，由临时 Frame 补上这部分机制：

```cpp
CallWindowProcA(PrevProcA, hwnd, WM_CHAR, 0xD6, lParam);
if (!Pending.Consumed) // 首字节处理期间，尾字节还没被取走
    CallWindowProcA(PrevProcA, hwnd, WM_CHAR, 0xD0, lParam);
```

1. LEP 在线程栈上暂存尾字节 D0，并压入关联这份数据的 `lep::ActiveFrame`。
2. LEP 调用原 ANSI 过程，传入首字节 D6。EDIT 在处理期间调用 PeekMessageA 寻找尾字节。
3. `LepPeekMessageA()` 检查当前 Frame、窗口和消息范围，返回暂存的 D0。PM_NOREMOVE 可以重复查看，PM_REMOVE 标记为已取走。
4. 首字节回调返回后，LEP 弹出 Frame。尾字节已取走时交付完成；否则再调用一次原过程，传入 D0。

这个机制保持原生完整字符的交付效果，让 EDIT 配对本次字符的两个字节，保留真实队列中其他字符的顺序。应用安装的 ANSI 子类仍经过保存的原过程；嵌套调用使用各自的 Frame。

**`WM_IME_CHAR`、`WM_SYSCHAR`、`WM_DEADCHAR`、`WM_SYSDEADCHAR`：打包后交付。** 每条消息调用原过程一次，两个字节合成一个 `wParam` 字符值 `0xD6D0`。消息表内部的 `IMSG_INWPARAMCHAR` 类别也使用这条路径，处理 WM_IME_COMPOSITION、WM_MENUCHAR 等消息。

打包字符占低 16 位，首字节在第 8～15 位，尾字节在第 0～7 位。第 16 位及以上保持原值，例如 WM_MENUCHAR 在第 16～31 位保存菜单标志：

```text
wParam：[第16位及以上：原值] [第8～15位：D6] [第0～7位：D0]
                           └──── 字符值 D6D0 ────┘
```

上述字符交付均沿用原 lParam。

#### EDIT 的文本位置和长度

窗口包装也需要处理 EDIT 的位置单位：W 接口按 UTF-16 字符位置计数，A 接口按目标 ACP 的字节位置计数。例如 CP936 的“中A”占两个 UTF-16 码元、三个 ANSI 字节，A 所在的位置分别为 1 和 2。

LEP 为 EM_GETSEL、EM_SETSEL、EM_LINEINDEX、EM_LINELENGTH 转换输入位置和返回结果：

- `WindowProcW()` 进入 `UserfnEDIT_TEXT_POSITION()`：调用原 ANSI 过程前，把 W 字符位置换成 ANSI 字节位置；返回后，把字节位置或长度换回 W 单位。
- `LepNtUserMessageCall()` 处理发给已包装 EDIT 的 ANSI 消息时，进入 `KernelfnEDIT_TEXT_POSITION()`，按相反方向转换，使 A 接口的调用方使用字节单位。

这项处理与字符交付一样，用来补齐 LEP 窗口包装后的 A/W 语义，使选择范围、行起始位置和行内长度保持一致。

#### 程序通过 A 接口发送字符

这个方向把 ANSI 字符参数按目标 ACP 解码为 Unicode，再交给系统的 W 路径。对于上述打包格式，低 16 位从 `0xD6D0` 转为 `0x4E2D`，第 16 位及以上和 lParam 保持原值。

`LepNtUserMessageCall()` 在 `IMSG_INWPARAMCHAR` 分支执行这项转换。另外两个入口 hook 也为 WM_IME_CHAR、WM_IME_COMPOSITION 转换参数：

- SendMessageA：目标窗口属于当前线程且满足快速处理条件时，USER32 可以在用户态完成参数转换并直接调用窗口过程。入口 hook 先按目标 ACP 解码，再调用 SendMessageW，使这条直接调用路径也使用正确的 Unicode 参数。
- PostMessageA：USER32 处理字符参数后，通过 NtUserPostMessage 把消息投递到目标线程的队列。目标线程用 GetMessage/PeekMessage 取出，再由 DispatchMessage 派发到窗口过程。入口 hook 先解码参数再调用 PostMessageW，使入队的字符使用 Unicode 表示。

#### 原过程要求默认 IME 处理

原 ANSI 过程收到 IME 消息后，可以自行处理，也可以调用 DefWindowProcA 请求默认处理。对已包装窗口，LEP 的 DefWindowProcA hook 按消息选择处理方式：

- WM_IME_CHAR：将字符解码为 Unicode，调用 PostMessageW 投递一条 WM_CHAR；这条消息随后按前面的 WM_CHAR 流程交给原 ANSI 过程。
- WM_IME_COMPOSITION：将字符参数解码为 Unicode，调用 DefWindowProcW，由系统默认 IME 窗口处理组合状态。

#### USER32 的 DBCS 状态

`Hooks/User32Environment.cpp` 根据 CheckDBCSEnabledExt 的机器码操作定位服务信息指针。首次同步时，宿主 DBCS 模式与目标 NLS 模式一致就沿用原映射；不一致则复制原映射中可读的区域尾部（上限 1 MiB），修改副本第一 DWORD 的 DBCS 位，并让本进程 USER32 使用副本。标准 EDIT 内部也会读到目标模式。

GetSystemMetrics/CheckDBCSEnabledExt、窗口创建和相关设置消息会从原映射同步其他数据。卸载时，仍指向 LEP 副本的槽恢复为原指针。已发布的副本保留到进程退出，供并发原生读者继续访问。定位或映射验证失败时沿用原状态，并记录日志。

#### 标准对话框标题

`WindowProcW()` 对 `#32770` 类窗口的 WM_NCCREATE、WM_SETTEXT 调用 DefWindowProcW，使来自系统本地化资源的标题按 Unicode 处理。例如中文系统的“选择计算机”标题，按 CP932 转换可能产生问号，因此这两类消息直接交给 W 默认过程。

### 17. `NtUserDefSetText`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：有 win32u 时按名称取 `NtUserDefSetText` 并 inline hook；无 win32u 的兼容布局使用 `FindNtUserDefSetText(user32)`：查 `NotifyWinEvent` 导出，扫描 user32 text 中 `push EVENT_OBJECT_NAMECHANGE` 模式，确认附近 call `NotifyWinEvent`，再向前找 call 到 `DefSetText`，要求目标 prologue 为 `8B FF 55 8B`，最后在 `DefSetText` 中找 syscall stub。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserDefSetText`，注册 HookPort filter。

x64 user32 兼容布局：先按 `NtUserCreateWindowEx` 路径找到 internal `CreateWindowEx`，扫描到 `0xC0000000` 前的上一个 user32 call 作为内部 `RtlInitLargeUnicodeString`；再扫描 `.text` 中所有 call 到它的位置。对每个候选向后 `0x20` 字节，若恰好有一个 call 到 x64 syscall stub 且后面很快返回，则该 syscall 视为 `NtUserDefSetText`。

作用：保证默认窗口标题/文本设置路径遵循目标 ACP。

### 18. user32 DC / BeginPaint charset

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86 win32u 布局：优先 inline hook `win32u!NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`；只有 native 导出不完整时才回退 EAT inline hook `user32!GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint`。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`，验证为直接 x64 syscall stub，再注册 HookPort filter。

x64 user32 兼容布局：从 user32 导出 `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint` 取直接 syscall stub，验证后注册 HookPort filter。

作用：获取 DC 或 paint DC 后检查当前字体；只有当前字体仍是 Windows 原始 stock/default font 时，才选择对应的目标 charset 克隆。显式创建并选择的字体保持不变，DC codepage 继续由实际字体 charset 自然派生。

 Win10/Win11 的系统 EDIT 还有一条不经过 DC/GDI 查询的默认字体路径：创建时的 `ECSetFont(nullptr)` 从 user32 的 104 字节 DPI server-info 结构复制 `TEXTMETRICW`，并用其中的 `tmCharSet` 覆盖 EDIT 的 charset 缓存。x86/x64 现在都从消费者语义定位该 getter：要求同一内部函数连续三次提供 `+32` 宽度、`+36` 高度和 `+40` 起始的完整 `TEXTMETRICW`，且候选唯一。Win10 中目标是 `GetDPIServerInfo`，Win11 中目标是 `GetDpiServerInfoForCurrentThread`。hook 调用原函数后返回线程私有的 104 字节副本，仅把默认度量的 `tmCharSet` 改成目标 charset；系统共享页、其它字段和显式选中的字体不变。特征不匹配或候选不唯一时不安装。

 Win7 x86/x64 的 `ECSetFont(nullptr)` 直接读取 `gpsi` 中的默认度量，不调用上述 getter；Win7 x86 虽存在名为 `GetDPIServerInfo` 的内部函数，但 EDIT 不使用它。因此 Win10/Win11 的 getter matcher 在两份 Win7 样本上均不产生候选。Win7 改由第二套消费者语义定位：x86 匹配 `push 0Fh / add esi,gpsi+metric / rep movsd` 的 60 字节 `TEXTMETRICW` 复制，x64 匹配等价的 `memcpy(..., 0x3C)` 序列；再从同一 `ECSetFont` 内后续的 `tmCharSet` load/store 推导 EDIT 私有 charset 字段偏移。仅当函数和偏移都唯一且结构完整时才 hook。原函数完成 `Font == nullptr` 的默认字体初始化后，LEP 把该 EDIT 实例的 charset 缓存改成目标 charset。

### 19. `SetWindowLongA` / `GetWindowLongA` / PtrA 与 `IsWindowUnicode`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`IsWindowUnicode`。

x64：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`SetWindowLongPtrA`、`GetWindowLongPtrA`、`IsWindowUnicode`。

Win7 x64 特例：`SetWindowLongA` 和 `SetWindowLongPtrA` 使用 `LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP`，允许短跳和 relay，但禁止最后退回入口 14 字节绝对跳覆盖。原因是 Win7 x64 下这两个入口最终对应的 syscall stub 只有 11 字节，后面紧跟其它函数指令；写 14 字节 `OpJumpIndirect` 会跨过 stub 边界并覆盖后续函数开头。

作用：包装/恢复 A 窗口过程；`IsWindowUnicode` 对 tracked wrapped A window 返回 `FALSE`，保持外部观察到的 A 窗口语义。

### 20. 剪贴板 ANSI 数据

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86/x64：EAT inline hook `GetClipboardData`、`SetClipboardData`。

作用：让 `CF_TEXT` 和字符串数据按目标 ACP 转换。转换后的 CF_TEXT 使用固定、清零的 GPTR 缓冲，兼容将返回句柄直接当作指针的客户端，同时仍可用 GlobalLock 读取。这是经过运行验证的兼容行为；SetClipboardData 文档要求提交的内存带 GMEM_MOVEABLE，且固定分配不延长 CloseClipboard 之后的对象有效期。

### 21. `SystemParametersInfoA/W`

时机：`USER32.dll` 加载后，`HookUser32Routines()`，仅当目标 ANSI codepage 为 `932` 时安装。

x86/x64：EAT inline hook `SystemParametersInfoA/W`。hook 先调用对应的原函数；当调用成功、`uiAction == SPI_GETDEFAULTINPUTLANG` 且 `pvParam` 非空时，把输出的 `HKL` 改写为 `0x04110411`。

作用：让查询默认输入语言的 A/W 路径看到日文默认输入法布局，补齐 CP932 转区下程序读取输入语言状态的 user32 路径。

### 22. `NtGdiHfontCreate`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86：有 win32u 时按名称取 `NtGdiHfontCreate` 并 inline hook；无 win32u 的兼容布局从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，取第一个 call 到 `IsSystemCall()` stub 的目标。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtGdiHfontCreate`，验证为直接 x64 syscall stub，注册 HookPort filter。

x64 gdi32 兼容布局：从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，返回第一个 call 到直接 x64 syscall stub 的目标，注册 HookPort filter。

线程级重入保护：字体枚举 callback 调用 `AdjustFontData()` 校正字体名称和度量时，会通过 `CreateFontIndirectBypassW()` 创建临时字体；它把 `Kind` 为 `GDI_HOOK_BYPASS` 的 `lep::ActiveFrame` 压入当前线程，再调用 `CreateFontIndirectW()`。调用链到达 `LepNtGdiHfontCreateWorker()` 时，该标记使这次字体创建跳过 charset 二次改写。

作用：控制/记录字体 charset 创建路径，让字体选择符合目标 locale。

### 23. `QueryFontAssocStatus`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`，仅当目标 ANSI codepage 为 `932` 时安装。

x86/x64：对 `QueryFontAssocStatus` 使用普通 inline hook。查找时优先从已加载的 `gdi32full.dll` 导出表取地址，随后查询 `gdi32.dll` 导出表。`LepQueryFontAssocStatus()` 固定返回 `0`。

作用：模拟日文 Windows 上 GDI font association status 为 `0` 的状态，避免中文系统转日区时 GDI A 文本路径进入 `FontAssocHack`，把 `TextOutA`/度量路径中的单字节半角假名按 `1252` 而不是 `932` 解释。

### 24. gdi32 字体枚举和 DC/对象辅助 hook

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86：在 `gdi32.dll` hook `GetStockObject`、`DeleteObject`、`CreateCompatibleDC`；字体枚举函数优先 hook 已加载的 `gdi32full.dll`，不存在时回退 `gdi32.dll`。均为 inline hook。

x64：inline hook `gdi32.dll` 的 `GetStockObject`、`DeleteObject`、`CreateCompatibleDC`。字体枚举函数优先 hook `gdi32full.dll`，不存在时 hook `gdi32.dll`；枚举字体宏显式使用 `Mp::OpJumpIndirect`。

作用：调整字体枚举、stock font 和兼容 DC 路径中的 charset/font 行为。

### 25. DLL load notification

时机：`LepGlobalData::Initialize()` 注册 `LdrRegisterDllNotification()`。

处理：

- `USER32.dll` -> `HookUser32Routines()`
- `GDI32.dll` -> `HookGdi32Routines()`
- `KERNELBASE.dll` / `KERNEL32.dll` -> `HookKernel32Routines()`

作用：对后加载模块补装 hook 和 locale cache 同步。

### 26. `LoadMemoryDll()` shadow ntdll hook

时机：从内存加载 DLL 的辅助路径中。

做法：临时 hook shadow ntdll 中的 `NtQueryAttributesFile`、`NtOpenFile`、`NtCreateSection`、`NtMapViewOfSection`、`NtClose`、`NtQuerySection`。

作用：为内存 DLL 加载辅助逻辑模拟和接管文件、section 相关操作。

### 27. Heap corruption helper

时机：调试辅助功能启用时。

hook：`RtlAllocateHeap`、`RtlReAllocateHeap`、`RtlFreeHeap`、`RtlSizeHeap`。

作用：记录堆分配生命周期，辅助定位 heap corruption。
