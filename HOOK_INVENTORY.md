# LocaleEmulatorPlus Hook 与状态改写清单

本文档按“工作项”组织当前 hook、模式查找和非 hook 状态改写。公共 hook 机制只在前面说明一次；每个工作项分别说明 hook/改写时机、x86/x64 差异、查找方式和作用。

## 公共机制

### 初始化顺序

`LocaleEmulatorPlus` 初始化时先建立 `LEPPEB`/`LEPB` 配置，再初始化 HookPort，然后安装 ntdll hook，随后注册 `LdrRegisterDllNotification()` 处理后加载模块。若 kernel32/kernelbase 已经加载，会主动调用 `HookKernel32Routines()`；USER32/GDI32 则通常由 DLL notification 触发。

### Inline Hook

宏定义在 `LocaleEmulatorPlus/LocaleEmulatorPlus.h`。

- `LepHookFromEAT(base, prefix, name)`：从导出表找 `prefix_name`，入口跳到 `LepName`，原 trampoline 存到 `HookStub.StubName`。
- `LepHookFromEATOp(base, prefix, name, op)`：同上，但显式指定 patch op。
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

它不受 +/-2GB 限制，但入口必须能安全覆盖 14 字节完整指令。

当前普通 x64 inline hook 默认使用 `OpJumpIndirect`，但 patch manager 会先尝试 5 字节 `E9 rel32`；直跳不够距时尝试附近 relay stub，入口 `E9` 到 relay，relay 再用 14 字节 `OpJumpIndirect` 到真实目标；短跳和 relay 都失败时才退回入口 14 字节覆盖。如果设置 `NoAbsoluteJump`，最后这步会失败。

### Syscall HookPort

实现位置：`LocaleEmulatorPlus/HookPort.cpp`，x86 和 x64 编译同一份 typed wrapper、callback 表与注册逻辑。HookPort 枚举 ntdll 的 `Zw*` 导出并建立 hash/service id 索引，但只在某个 syscall 第一次注册 callback 时 patch 该 syscall 自己的入口，不再 patch x86/WOW64 的共享系统调用 gateway。

每个受支持的 syscall 都必须在 `FindWrapperByHash()` 中有一个参数类型和调用约定与原函数一致的 wrapper。调用过程在两种架构上相同：

1. 调用者照常调用 ntdll/win32u/gdi32 的 syscall stub，入口 patch 跳到对应 wrapper，原调用者的参数和返回地址保持正常函数调用布局。
2. wrapper 通过 hash 找到 `SYSCALL_INFO` 和保存的 original，然后由 `DispatchTypedFilter()` 按 `FilterBitmap` 从低位到高位调用已注册 callback。
3. callback 每次返回后立即检查 `FltInfo.Action`。`ContinueSystemCall` 继续下一个 callback；`BlockSystemCall` 立即停止循环，并将当前 callback 的普通返回值交给原调用者。
4. 所有 callback 都以 `ContinueSystemCall` 返回后，wrapper 调用 original，并把真实 syscall 的返回值交给原调用者。之前 callback 的普通返回值不会保留。

当前只有两种 Action：

| Action | 是否继续后续 callback | HookPort 是否自动执行 original | 对原调用者可见的返回值 |
| --- | --- | --- | --- |
| `ContinueSystemCall` | 是；也是默认值 | callback 全部结束后执行 | original 的返回值 |
| `BlockSystemCall` | 否 | 不执行 | 当前 callback 的返回值 |

因此 `BlockSystemCall` 的准确含义是“停止 callback 链，并阻止 wrapper 再自动调用 original”。它不是权限判断，也不妨碍 callback 自己先通过 `HpCallSysCall()` 或 `HpGetSystemCallOriginal()` 调用 original。callback 自己调用过 original 时必须设置 `BlockSystemCall`，否则 callback 返回后 wrapper 会再调用一次。

callback 表仍支持一个 syscall 注册多个 callback，虽然当前项目每个 syscall 实际只注册一个。全局 filter、`PermitSystemCall` 和与共享 dispatcher 配套的线程级 bypass frame 已删除；两种架构的 original 都不会重新进入被 patch 的 syscall 入口，不需要用线程状态避免递归。`DispatchTypedFilter()` 不用 SEH 包裹 callback，callback 抛出的异常会继续向外传播。

#### x86 入口与 original

x86 parser 要求 syscall stub 从 `mov eax, service_id` 开始。首次注册 callback 时，patch manager 在该 `NtXxx`/`ZwXxx` 入口安装普通 5 字节相对跳，并生成 trampoline；`SYSCALL_INFO.FunctionAddress` 随后指向该 trampoline。

trampoline 复制入口被覆盖的完整指令，末尾跳回原 stub 中未覆盖的位置，所以 native x86 会继续走原有 `KiFastSystemCall` 路径，WOW64 会继续走系统本来选择的 `fs:[0xC0]` 或 `Wow64Transition` 路径。HookPort 不再解析、patch 或模拟这些 transition，也不需要改写 EDX 或人工调整返回地址。original 执行结束后先返回 wrapper，wrapper 再按普通 `stdcall` 返回原调用者。

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
F6 04 25 08 03 FE 7F 01         ; test byte ptr [0x7FFE0308], 1
75 03                            ; jne int2e_path
0F 05                            ; syscall
C3                               ; ret
CD 2E                            ; int 2e
C3                               ; ret
```

`0x7FFE0308` 是 `KUSER_SHARED_DATA.SystemCall`。parser 只有在 selector、`syscall; ret` 和 `int 2e; ret` 都符合上述布局时才记录 `SystemCallHasInt2ESelector`。安装 hook 时，HookPort 会为被 patch 的 syscall 重新生成一段可直接调用的最小 syscall stub，下文统一称为“人工原始调用 stub”；它会保留同一段动态判断，而不是固定强制走 `syscall`。

user32/win32u/gdi32 中找到的 syscall stub 通过 `HpAddSystemCallByRoutine()` 或 `HpAddSystemCallByRoutineRange()` 额外注册。每个被 hook 的 syscall 必须在 `FindWrapperByHash()` 中有强类型 wrapper。

x64 HookPort 还有一个 syscall 专用的 12 字节绝对跳：

```asm
48 B8 imm64           ; mov rax, wrapper
FF E0                 ; jmp rax
```

它和普通 inline hook 的 14 字节 `OpJumpIndirect` 不是同一套取舍。12 字节跳转的优点是更短，不需要目标在 +/-2GB 内；缺点是会改写 `rax`，不如 `FF 25 [rip+0]` 那种 14 字节间接跳转“寄存器透明”。因此它不适合作为普通 inline hook 的默认绝对跳：普通 hook 要面对任意函数入口或 call-site 场景，最好不要在进入 hook 目标前无条件破坏寄存器状态。

syscall 入口可以选择这条路，是因为 HookPort 已经先解析了 syscall stub 并记录 service id，原 syscall 由上文定义的人工原始调用 stub 执行，被 patch 的入口不会作为 trampoline 继续执行。入口 patch 的唯一职责是跳到强类型 wrapper，`rax` 原本也会在 syscall stub 中被 `mov eax, service_id` 覆盖，所以这里使用 `mov rax; jmp rax` 的寄存器破坏可以接受。更重要的是，标准 x64 syscall stub 常见长度是 11 字节，12 字节绝对跳比 14 字节更可能只覆盖到 `ret` 后的 padding，而不是跨进下一个函数。

x64 syscall 入口 patch 策略：

- 优先 5 字节 `E9 rel32` 直跳到 wrapper。
- 不够距时，在 syscall stub 附近申请 relay，入口 `E9` 到 relay，relay 再跳 wrapper。
- relay 失败后，只在 `ret` 位于 12 字节覆盖范围末尾，或 `ret` 后面直到覆盖末尾全是 padding 时，才允许上面的 12 字节绝对跳。
- 否则失败，避免覆盖后续仍会执行的真实指令。

人工原始调用 stub 不是从被覆盖入口继续执行的 trampoline，而是按解析结果重新生成的独立代码。它重新执行 `mov r10, rcx`、`mov eax, service_id` 和系统调用尾部；原 stub 带 `KUSER_SHARED_DATA.SystemCall` 判断时，人工 stub 也按运行时状态选择 `syscall` 或 `int 2e`，否则直接执行 `syscall`。内核返回后，人工 stub 的 `ret` 回到 wrapper，wrapper 再返回原调用者。

#### 仍保留的线程级保护

GDI 字体路径另有一套与 HookPort 无关的线程级保护。`CreateFontIndirectBypassA/W()` 压入 Context 为 `GDI_HOOK_BYPASS` 的 `TEB_ACTIVE_FRAME`，再调用公开的 `CreateFontIndirectA/W()`；这些 API 会在更深处到达被 hook 的 `NtGdiHfontCreate`，`LepNtGdiHfontCreateWorker()` 发现该标记后跳过 charset 二次改写。这个保护在 x86/x64 都使用，作用域仅限当前线程的这次字体创建调用。

NtUser 不使用 frame。x86 对找到的 `NtUserCreateWindowEx`、`NtUserMessageCall` 和 `NtUserDefSetText` 做普通 inline hook，原调用通过 `HookStub.StubNtUser*` trampoline 执行。x64 的 NtUser 由 HookPort patch syscall stub，原调用通过 `HpGetSystemCallOriginal(hash)` 查询进程级人工原始调用 stub；查询失败表示对应 syscall hook 没有成功建立。

强类型 wrapper 当前覆盖：

- ntdll：`NtCreateUserProcess`、`NtQuerySystemInformation`、`NtQueryValueKey`、`NtInitializeNlsFiles`、`NtQueryDefaultLocale`、`NtQueryDefaultUILanguage`、`NtQueryInstallUILanguage`、`NtContinue`
- user32/win32u：`NtUserCreateWindowEx`、`NtUserMessageCall`、`NtUserDefSetText`、`NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`
- gdi/win32u：`NtGdiHfontCreate`

### 通用查找约定

x64 syscall stub 检查要求前 `0x20` 字节内能识别 `mov r10, rcx`、`mov eax, service_id`、`syscall; ret`。x86 HookPort 要求 ntdll syscall stub 的入口是 `mov eax, service_id`；其它 x86 NtUser/GDI 私有 stub 仍使用各自工作项说明的扫描逻辑。

涉及 user32/kernelbase/ntdll 私有例程的查找都属于版本敏感逻辑；若系统 DLL 更新后失败，优先检查对应扫描规则和强类型 wrapper 签名。

## 工作项

### 1. 初次进程注入：`LdrLoadDll` 调试断点

时机：`LoaderDll` 调用 `CreateProcessWithDll(CPWD_BEFORE_KERNEL32)` 创建目标进程时。

做法：`InjectDllBeforeKernel32Loaded()` 加载一份干净 ntdll 镜像，通过导出表找 `LdrLoadDll`，映射回目标进程地址，把远程入口临时写成 2 字节 `UD2`。调试器等到第一次 loader 侧 `LdrLoadDll` 调用触发非法指令后，恢复原字节，记录第一次 call site，再把 `LocaleEmulatorPlus_*` 注入到目标进程。

作用：让 LEP 在 kernel32/kernelbase 初始化前取得执行权，并拿到目标进程真实 loader 调用现场。

### 2. 子进程传播：`NtCreateUserProcess` 与 shadow `LoadFirstDll`

时机：`HookNtdllRoutines()` 始终安装 `NtCreateUserProcess` filter。真实 `NtCreateUserProcess` 成功后，`LepNtCreateUserProcess()` 调 `InjectSelfToChildProcess()`。

x86/x64：`NtCreateUserProcess` 本身走 HookPort syscall filter。子进程 `LdrLoadDll` 入口 patch x86 用 `E9 rel32`，x64 用 `FF 25 [rip+0]` 加绝对目标地址。

做法：把当前 LEP DLL 手工 shadow 到子进程，重定位到远程地址，然后把子进程 `ntdll!LdrLoadDll` 入口改成跳到 shadow 里的 `LoadFirstDll`。`LoadFirstDll()` 进入后恢复 `LdrLoadDll` 原字节，初始化 LEP，再调用真实 `LdrLoadDll`。

注意：当前代码直接使用已保存的绝对地址 `LEPPEB->LdrLoadDllAddress`。这隐含假设同架构子进程 ntdll 映射基址与当前进程一致，因此 `ntdll!LdrLoadDll` 地址一致。

作用：让目标进程创建的子进程自动继承 LEP 环境。

### 3. `NtContinue`

时机：`HookNtdllRoutines()`。

x86：`SearchLdrInitNtContinue()` 扫描 `LdrInitializeThunk` 前 `0x5B` 字节，只改写其中目标为 `NtContinue` 的 call site，不 hook `NtContinue` 函数入口。`LepLdrInitNtContinue()` 在 loader 即将恢复线程初始 context 前，把当前线程的 `TEB.CurrentLocale` 设置为 `LEPB.LocaleID`，然后通过 `StubLdrInitNtContinue` 调用原 `NtContinue`。因此异常恢复、APC 等其它 x86 `NtContinue` 调用不受影响。

x64：`SearchLdrInitNtContinue()` 的结果只用于兼容性检查和日志；实际通过 HookPort 对 `NtContinue` syscall 入口安装 filter。`LepNtContinue()` 同样只设置当前线程的 `TEB.CurrentLocale`，不修改传入的 `CONTEXT` 和 `TestAlert`，也不设置 `BlockSystemCall`，所以 filter 返回后仍调用原 `NtContinue`。这会覆盖所有经过该 ntdll syscall stub 的 x64 `NtContinue`，范围比 x86 的 loader call-site hook 更广。

作用：`TEB.CurrentLocale` 是线程级状态。这个 hook 以 loader 结束线程初始化并通过 `NtContinue` 恢复用户执行现场的时刻为锚点，确保新线程进入用户入口前看到 LEP 的目标 locale；它不改写即将恢复的 CPU context。

### 4. ntdll locale/sysinfo syscall filter

时机：`HookNtdllRoutines()`，非 loader 进程路径。

函数：

- `NtQuerySystemInformation`：改写系统时区信息。
- `NtInitializeNlsFiles`：提供目标 NLS 文件映射和 locale 信息。
- `NtQueryDefaultLocale`：返回目标 LCID。
- `NtQueryDefaultUILanguage`：返回目标 UI language。

x86/x64：均走 HookPort filter。

作用：让直接查询 ntdll 层默认区域、UI language、时区和 NLS 文件的路径看到目标 locale。

### 5. 注册表重定向：`NtQueryValueKey`

时机：`HookNtdllRoutines()` 中，仅当 `RegistryRedirectionEntry` 非空时安装。

x86/x64：均走 HookPort filter。

作用：把 NLS/codepage/language 相关注册表查询重定向到 `LEPB` 中保存的值，让直接读注册表的 API 也符合目标 locale。

### 6. 异常与自定义代码页：`RtlKnownExceptionFilter` / `RtlCustomCPToUnicodeN`

时机：`HookNtdllRoutines()`。

做法：两个函数都使用普通 inline hook。

- `RtlKnownExceptionFilter` -> `LepKnownExceptionFilter`：在转交原 filter 前生成 minidump。感觉没什么用，暂时跳过了，只保留实现。
- `RtlCustomCPToUnicodeN` -> `LepCustomCPToUnicodeN`：当调用者传入非目标、非 UTF-8 的 CP table 时，先用目标 ANSI codepage table 重新初始化，再执行转换。

作用：前者用于诊断，后者修正部分绕过默认 ACP 的转换路径。

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
2. x64 额外同步 user32 client codepage；x86 当前没有对应 user32 client-info 写入。
3. 用 `CodePageMapView` 里的目标 ACP/OEMCP/case table 调 `RtlInitNlsTables()`。
4. 把得到的 `NLSTABLEINFO` 传给 `RtlResetRtlTranslations()`，让 ntdll 默认转换状态切到目标表。
5. 调 `LepSyncNtdllNlsGlobals()` 写 ntdll 导出 NLS 全局变量。
6. 调私有 `RtlpInitCodePageTables` 做额外内部 cache refresh；失败只记录。

作用：让 ntdll/default ANSI 转换使用目标代码页数据。

### 8. user32 client codepage 改写

时机：x64 的 `HackAnsiOemCodeHashNodes()` 中调用 `LepSyncUser32ClientCodePage()`。

x64：`LepGetWin32ClientInfo()` 使用 `TEB + 0x800`，当前写 `LEP_WIN32_CLIENT_INFO_CODE_PAGE_INDEX == 19` 这一项的低 16 位为目标 ANSI codepage。

x86：`LepGetWin32ClientInfo()` 使用 `TEB.User32Reserved`，但 `LepSyncUser32ClientCodePage()` 当前函数体在 x86 下为空；也就是说这个同步目前是 x64 路径独有。

作用：修正 user32 client-side ANSI codepage cache。某些 user32 A/W 转换路径查 client info，而不是调用普通 ntdll 转换 API。

### 9. ntdll 私有 codepage table refresh

时机：`HackAnsiOemCodeHashNodes()`。

查找：`FindRtlpInitCodePageTables()` 先取 ntdll 的 `.text` 范围，再扫描所有 `call rel32` call-site；目标为导出 `RtlInitCodePageTable` 的 call-site 会被当作候选。候选 call-site 如果距离导出的 `RtlInitNlsTables` 小于 `0x80` 字节，则直接忽略。剩下的候选从首个 call-site 向前最多 `0x120` 字节找连续 `CC/90` padding 边界，以 padding 后第一个字节作为函数起点；再从这个首个 call-site 向后最多 `0x100` 字节找第二个 call 到 `RtlInitCodePageTable`，都能找到就将其视为 `RtlpInitCodePageTables` 函数地址。

调用：`LepInitNtdllCodePageTables(ansi, oem)` 调用找到的内部例程。

作用：刷新 `RtlResetRtlTranslations()` 之外的 ntdll 内部 codepage table/cache。

### 10. kernelbase ANSI/OEM code hash node refresh

时机：`HookKernel32Routines()` 调 `HackAnsiOemCodeHashNodes()`。

x64/x86：当前都跳过 kernelbase ANSI/OEM code hash node refresh，只保留 `LepSetupAnsiOemCodeHashNodes()` 实现。原因是当前已经完成 process/ntdll/user32 侧 cache sync，观察上这个 kernelbase 私有刷新不是必要条件，而它的私有查找跨 build 比较脆。

查找：`FindSetupAnsiOemCodeHashNodes(kernelbase)` 从 kernelbase entry point 进入初始化链：

- entry 后 `0x40` 内第一个 call -> `KernelBaseBaseDllInitialize`
- 其后 `0x100` 内第二个 call/jump -> 内部 `_KernelBaseBaseDllInitialize`
- 在内部 initializer 最多 `0x800` 字节内找 `mov eax, 0x190`(在部分版本上还会变成 `mov ecx, 0x190`)
- 从该点开始依次找 `BaseNlsDllInitialize`、`NlsProcessInitialize`、第三个 call 作为 `SetupAnsiOemCodeHashNodes`

作用：替换进程 ACP/OEMCP 后重建 kernelbase 的 ANSI/OEM code hash/cache nodes。

PS: 这个我看了很久逆向信息和当时的 [PR](https://github.com/xupefei/Locale-Emulator-Core/pull/3)，但是依然搞不明白为什么当初会把找到的这个玩意叫 `SetupAnsiOemCodeHashNodes` 并使用它，
它在 ida 中的名字和实际功能都完全对不上原代码中似乎预期的作用。

### 11. kernelbase named-locale cache 预热

时机：`HookKernel32Routines()` 调 `HackUserDefaultLCID2()`。

查找：`FindGetNamedLocaleHashNode(GetNLSVersionEx)` 从 `KERNELBASE.dll` 导出 `GetNLSVersionEx` 开始扫 call；新布局看到立即数 `0x8001` 时扩展到 `0x60` 字节，并选择前面近处有 `xor edx, edx` 的 call。

做法：临时 hook 找到的内部 `GetNamedLocaleHashNode` 为 `LepGetNamedLocaleHashNode`，调用 `GetUserDefaultLCID()` 预热/刷新后恢复。

作用：让 kernelbase named-locale cache 以目标 LCID/locale name 填充。

### 12. `GetSystemDefaultUILanguage` / `GetUserDefaultUILanguage`

时机：`HookKernel32Routines()` 处理 `KERNEL32.dll` 时，仅当 `HookUILanguageApi` 启用才安装。

x86/x64：统一 inline hook `kernel32!GetSystemDefaultUILanguage` 和 `kernel32!GetUserDefaultUILanguage`，返回 `LEPB.LocaleID`。

作用：让查询系统/用户默认 UI language 的公开 kernel32 API 看到目标 LCID，同时不破坏 shell/common dialog 等系统组件对 install UI language 和 MUI 资源安装状态的假设。

### 13. `NtUserCreateWindowEx`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：没有 win32u 路径。`FindNtUserCreateWindowEx(user32)` 查 `ntdll!RtlQueryInformationActiveActivationContext` 的 IAT slot，遍历 relocation 找 `FF 15 [slot]` 调用点，再向后最多 `0x150` 字节找 call 到 syscall stub。找到后用普通函数入口 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserCreateWindowEx`，注册到 HookPort，再安装 syscall filter。

x64 无 win32u：先从 `CreateWindowExW/A` 找 internal `CreateWindowEx`；在其中最多 `0x2C0` 字节内找包含立即数 `0xC0000000` 的指令；之后第一个 call 到非 syscall 的 user32 内部函数视为 `VerNtUserCreateWindowEx`；再在其中最多 `0x300` 字节找第一个 call 到 x64 syscall stub，作为 `NtUserCreateWindowEx`。

x64 参数版本差异：

- Win7：15 参数，末尾 activation context 句柄为 `ULONG_PTR`。
- Win8/8.1：16 参数，末尾 activation context 句柄为 `ULONG_PTR`。
- Win10+：17 参数，末尾 activation context 句柄为 `ULONG_PTR`。

版本选择使用 `Nt_QueryOsVersion()`，不使用 PEB 版本字段。

作用：把 ANSI 窗口/类名数据按目标代码页转换，包装 A 窗口过程，并让窗口创建走 native 路径。

### 14. `NtUserMessageCall`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：当前使用 `FindNtUserMessageCall2(user32)`。它查 `kernel32!GlobalLock`、`GlobalUnlock`、`GlobalFree` 的 IAT 项，遍历 user32 relocation table 找这三个导入指针连续出现的位置，再在前两个引用之间找 call 到 syscall stub。找到后普通 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserMessageCall`，注册 HookPort syscall filter。

x64 无 win32u：从 `SendNotifyMessageW/A` 开始，各扫 `0x30` 字节，第一个 call 到 x64 syscall stub 的目标视为 `NtUserMessageCall`。

作用：拦截 user32 消息派发底层路径，转换相关 ANSI 字符串消息并通过 W 语义派发。

### 15. ANSI 窗口过程包装及边界

时机：`NtUserCreateWindowEx`/CBT 包装 A 创建路径，或 `SetWindowLongA` / `SetWindowLongPtrA` 安装 A 窗口过程时生效。

做法：把原 ANSI 窗口过程保存到 `GetWindowDataA(hwnd)` 对应数据里，并把窗口过程替换为 `WindowProcW()`。`IsWindowUnicode` 对这些 tracked wrapped A window 返回 `FALSE`，让外部仍看到 A 窗口语义。

标准对话框标题例外：`WindowProcW()` 对 `#32770` 类窗口的 `WM_SETTEXT` 直接调用 `DefWindowProcW()`。原因是 shell/common dialog 可能通过 A 创建路径得到 ANSI 标志窗口，于是被 LEP 包装；但后续标题可能来自系统自己的 Unicode 本地化资源，例如中文 Windows 上的“选择计算机”。如果继续走 `WindowProcW -> CallWindowProcA(PrevProcA)`，这些系统 Unicode 文本会被按目标 ACP 压成 ANSI，CP932 等目标代码页无法表示的本机字符会变成 `?`。该例外只覆盖标准对话框标题，不全局拦截所有 `WM_SETTEXT`，以免绕过自定义 A 窗口过程内部状态。后续如果有其它标准窗口有同样的问题可以再加白名单。

作用：定义 LEP 自己制造的 A/W 窗口过程边界。EDIT 文本位置 thunk、标准对话框标题例外都依赖这个边界。

### 16. EDIT A/W 文本位置消息 thunk

时机：`USER32.dll` 加载并安装 user32 hook 后，在两条 LEP 自己制造的 A/W 边界上生效。

路径一：`WindowProcW()` 收到消息后，如果窗口类是 `Edit`，且消息是 `EM_GETSEL`、`EM_SETSEL`、`EM_LINEINDEX`、`EM_LINELENGTH`，则不走通用 `MessageTable` 分派，而是进入 `UserfnEDIT_TEXT_POSITION()`。这是 W 语义消息转发到保存的原 ANSI 窗口过程 `PrevProcA` 的路径。

路径二：`LepNtUserMessageCall()` 拦到 `WINDOW_FLAG_ANSI` 消息时，如果窗口类是 `Edit` 且窗口已经被 LEP 包装过，即 `GetWindowDataA(hwnd)` 中保存了原 ANSI proc，则进入 `KernelfnEDIT_TEXT_POSITION()`。这是 ANSI API 消息要送入已包装 W 窗口的路径。

转换规则：

- `EM_SETSEL`：W->A 时把字符 offset 转成目标 ACP 下的 ANSI byte offset；A->W 时反向把 ANSI byte offset 转成字符 offset。
- `EM_GETSEL`：W->A 返回后把 ANSI byte offset 转成字符 offset；A->W 返回后把字符 offset 转成 ANSI byte offset。
- `EM_LINEINDEX`：按同样方向转换行起始位置。
- `EM_LINELENGTH`：按同样方向转换行内长度。

作用：补回 LEP 包装 ANSI 窗口过程时绕过的 user32 `SendMessageWorker`/EDIT 系统类 A/W 位置语义。原生 EDITA 在 DBCS codepage 下会维护自己的字符边界；但 `WindowProcW -> CallWindowProcA(PrevProcA)` 不是原生 `SendMessageA -> SendMessageWorker -> EDITA` 路径，所以必须在 LEP 的边界处显式转换 offset/length。该逻辑只覆盖 `Edit` 控件和已包装窗口，避免影响普通原生 ANSI EDIT 或自定义同号消息。

### 17. `NtUserDefSetText`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：`FindNtUserDefSetText(user32)` 查 `NotifyWinEvent` 导出，扫描 user32 text 中 `push EVENT_OBJECT_NAMECHANGE` 模式，确认附近 call `NotifyWinEvent`，再向前找 call 到 `DefSetText`，要求目标 prologue 为 `8B FF 55 8B`，最后在 `DefSetText` 中找第一个 call 到 syscall stub。找到后普通 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserDefSetText`，注册 HookPort syscall filter。

x64 无 win32u：先按 `NtUserCreateWindowEx` 路径找到 internal `CreateWindowEx`，扫描到 `0xC0000000` 前的上一个非 syscall user32 call 作为内部 `RtlInitLargeUnicodeString`；再扫描 `.text` 中所有 call 到它的位置。对每个候选向后 `0x20` 字节，若恰好有一个 call 到 x64 syscall stub 且后面很快返回，则该 syscall 视为 `NtUserDefSetText`。

作用：保证默认窗口标题/文本设置路径遵循目标 ACP。

### 18. user32 DC / BeginPaint charset

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint`。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`，要求都是直接 x64 syscall stub，注册 HookPort filter。

x64 无 win32u：从 user32 导出 `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint` 取入口，并要求导出本身是直接 x64 syscall stub；不满足就失败。

作用：获取 DC 或 paint DC 后重置/检查 DC charset，避免绘制路径沿用本机区域字符集状态。

### 19. `SetWindowLongA` / `GetWindowLongA` / PtrA 与 `IsWindowUnicode`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`IsWindowUnicode`。

x64：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`SetWindowLongPtrA`、`GetWindowLongPtrA`、`IsWindowUnicode`。PtrA 是独立 hook，不是 LongA 的别名。

Win7 x64 特例：`SetWindowLongA` 和 `SetWindowLongPtrA` 使用 `LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP`，允许短跳和 relay，但禁止最后退回入口 14 字节绝对跳覆盖。原因是 Win7 x64 下这两个入口最终对应的 syscall stub 只有 11 字节，后面紧跟其它函数指令；写 14 字节 `OpJumpIndirect` 会跨过 stub 边界并覆盖后续函数开头。

作用：包装/恢复 A 窗口过程；`IsWindowUnicode` 对 tracked wrapped A window 返回 `FALSE`，保持外部观察到的 A 窗口语义。

### 20. 剪贴板 ANSI 数据

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86/x64：EAT inline hook `GetClipboardData`、`SetClipboardData`。

作用：让 `CF_TEXT` 和字符串数据按目标 ACP 转换。

### 21. `SystemParametersInfoA`

时机：`USER32.dll` 加载后，`HookUser32Routines()`，仅当目标 ANSI codepage 为 `932` 时安装。

x86/x64：EAT inline hook `SystemParametersInfoA`。`LepSystemParametersInfoA()` 先调用原函数；当调用成功、`uiAction == SPI_GETDEFAULTINPUTLANG` 且 `pvParam` 非空时，把输出的 `HKL` 改写为 `0x04110411`。

作用：让查询默认输入语言的 A 路径看到日文默认输入法布局，补齐 CP932 转区下部分程序读取输入语言状态的 user32 路径。

### 22. `NtGdiHfontCreate`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86：没有 win32u 时从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，返回第一个 call 到 `IsSystemCall()` stub 的目标，随后用普通函数入口 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtGdiHfontCreate`，验证为直接 x64 syscall stub，注册 HookPort filter。

x64 无 win32u：从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，返回第一个 call 到直接 x64 syscall stub 的目标，注册 HookPort filter。

作用：控制/记录字体 charset 创建路径，让字体选择符合目标 locale。

### 23. `QueryFontAssocStatus`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`，仅当目标 ANSI codepage 为 `932` 时安装。

x86/x64：统一使用普通 inline hook，不走 `NtGdiQueryFontAssocInfo` syscall hook。查找时优先从已加载的 `gdi32full.dll` 导出表取 `QueryFontAssocStatus`，找不到则退回当前 `gdi32.dll` 导出表。hook 后 `LepQueryFontAssocStatus()` 固定返回 `0`。

作用：模拟日文 Windows 上 GDI font association status 为 `0` 的状态，避免中文系统转日区时 GDI A 文本路径进入 `FontAssocHack`，把 `TextOutA`/度量路径中的单字节半角假名按 `1252` 而不是 `932` 解释。

### 24. gdi32 字体枚举和 DC/对象辅助 hook

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86：EAT inline hook `GetStockObject`、`DeleteObject`、`CreateCompatibleDC`、`EnumFontsW/A`、`EnumFontFamiliesA/W`、`EnumFontFamiliesExA/W`。

x64：EAT inline hook `GetStockObject`、`DeleteObject`、`CreateCompatibleDC`。字体枚举函数优先 hook `gdi32full.dll`，不存在时 hook `gdi32.dll`；枚举字体宏显式使用 `Mp::OpJumpIndirect`。

作用：调整字体枚举、stock font、兼容 DC 等路径中的 charset/font 行为。

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

作用：让内存 DLL 加载辅助逻辑能模拟/接管文件与 section 相关操作。它不是普通运行时 locale hook。

### 27. Heap corruption helper

时机：调试辅助功能启用时。

hook：`RtlAllocateHeap`、`RtlReAllocateHeap`、`RtlFreeHeap`、`RtlSizeHeap`。

作用：辅助定位 heap corruption，不属于转区必需路径。

### 28. 当前存在但未接入主路径的 helper

`FindNtUserMessageCall(user32)`：旧版 x86 helper，从 `SendNotifyMessageW` 最多扫 `0x40` 字节，找第一个 call 到 syscall stub；当前 `HookUser32Routines()` 使用的是 `FindNtUserMessageCall2()`。

`FindSendMessageWorker(user32)`：动态 probe helper，创建 button、subclass 后发送 `WM_SETTEXT`，probe callback 遍历栈帧返回 callback 前 call 目标；当前未被 `HookUser32Routines()` 使用。
