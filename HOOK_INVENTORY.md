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

### x86 HookPort

实现位置：`LocaleEmulatorPlus/HookPort.cpp`。

32 位 HookPort 从干净 ntdll 镜像枚举 `Zw*` 导出，解析 `mov eax, service_id` 形式的 syscall stub，建立 syscall/filter 表。native x86 和 WOW64 共用 filter 表和 `HpUserModeDispatcher()`，区别只在入口拦截点：

- native x86：patch `ntdll!KiFastSystemCall`；Win8+ 还会用 `PatchNtdllSysCallStub()` 把 ntdll `Zw*` stub 内部 call site 重新指向 `KiFastSystemCall`。
- WOW64：通过 `NtCancelTimer` stub 找 WOW64 syscall jump stub，通常是 `fs:[0xC0]` 路径，然后 patch 这个 jump stub；按系统版本选择 `HookSysEnter_Wow64` / `_Win8` / `_Win10`。

### x64 HookPort

实现位置：`LocaleEmulatorPlus/HookPortStub.cpp`。

x64 HookPort 枚举 ntdll `Zw*` 导出并验证 syscall stub：

```asm
4C 8B D1              ; mov r10, rcx
B8 xx xx xx xx        ; mov eax, service_id
0F 05                 ; syscall
C3                    ; ret
```

user32/win32u/gdi32 中找到的 syscall stub 通过 `HpAddSystemCallByRoutine()` 或 `HpAddSystemCallByRoutineRange()` 额外注册。每个被 hook 的 syscall 必须在 `FindWrapperByHash()` 中有强类型 wrapper。

x64 HookPort 还有一个 syscall 专用的 12 字节绝对跳：

```asm
48 B8 imm64           ; mov rax, wrapper
FF E0                 ; jmp rax
```

它和普通 inline hook 的 14 字节 `OpJumpIndirect` 不是同一套取舍。12 字节跳转的优点是更短，不需要目标在 +/-2GB 内；缺点是会改写 `rax`，不如 `FF 25 [rip+0]` 那种 14 字节间接跳转“寄存器透明”。因此它不适合作为普通 inline hook 的默认绝对跳：普通 hook 要面对任意函数入口或 call-site 场景，最好不要在进入 hook 目标前无条件破坏寄存器状态。

syscall 入口可以选择这条路，是因为 HookPort 已经先解析了 syscall stub，记录 service id，并为“原函数”人工构造最小 syscall stub；被 patch 的原入口不会作为 trampoline 执行。入口 patch 的唯一职责是跳到强类型 wrapper，`rax` 原本也会在 syscall stub 中被 `mov eax, service_id` 覆盖，所以这里使用 `mov rax; jmp rax` 的寄存器破坏可以接受。更重要的是，标准 x64 syscall stub 常见长度是 11 字节，12 字节绝对跳比 14 字节更可能只覆盖到 `ret` 后的 padding，而不是跨进下一个函数。

x64 syscall 入口 patch 策略：

- 优先 5 字节 `E9 rel32` 直跳到 wrapper。
- 不够距时，在 syscall stub 附近申请 relay，入口 `E9` 到 relay，relay 再跳 wrapper。
- relay 失败后，只在 `ret` 位于 12 字节覆盖范围末尾，或 `ret` 后面直到覆盖末尾全是 padding 时，才允许上面的 12 字节绝对跳。
- 否则失败，避免覆盖后续仍会执行的真实指令。

强类型 wrapper 当前覆盖：

- ntdll：`NtCreateUserProcess`、`NtQuerySystemInformation`、`NtQueryValueKey`、`NtInitializeNlsFiles`、`NtQueryDefaultLocale`、`NtQueryDefaultUILanguage`、`NtQueryInstallUILanguage`、`NtContinue`
- user32/win32u：`NtUserCreateWindowEx`、`NtUserMessageCall`、`NtUserDefSetText`、`NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`
- gdi/win32u：`NtGdiHfontCreate`

### 通用查找约定

x64 syscall stub 检查要求前 `0x20` 字节内能识别 `mov r10, rcx`、`mov eax, service_id`、`syscall; ret`。x86 内部 stub 查找沿用 `IsSystemCall()` / syscall-entry 逻辑。

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

x86：先用 `SearchLdrInitNtContinue()` 扫描 `LdrInitializeThunk` 前 `0x5B` 字节，查找目标为 `NtContinue` 的 call site，然后用 `LepFunctionCall` 改写到 `LepLdrInitNtContinue`。

x64：当前只记录 `SearchLdrInitNtContinue()` 的查找结果；实际直接对 `NtContinue` 安装 HookPort syscall filter。

作用：拦截 loader/system callback 附近的 continuation 路径，让早期 hook 有机会接入后续初始化流程。

### 4. ntdll locale/sysinfo syscall filter

时机：`HookNtdllRoutines()`，非 loader 进程路径。

函数：

- `NtQuerySystemInformation`：改写系统时区信息。
- `NtInitializeNlsFiles`：提供目标 NLS 文件映射和 locale 信息。
- `NtQueryDefaultLocale`：返回目标 LCID。
- `NtQueryDefaultUILanguage`：返回目标 UI language。
- `NtQueryInstallUILanguage`：仅 `HookUILanguageApi` 启用时安装，返回目标 install UI language。

x86/x64：均走 HookPort filter；x64 需要对应强类型 wrapper。

作用：让直接查询 ntdll 层默认区域、UI language、时区和 NLS 文件的路径看到目标 locale。

### 5. 注册表重定向：`NtQueryValueKey`

时机：`HookNtdllRoutines()` 中，仅当 `RegistryRedirectionEntry` 非空时安装。

x86/x64：走 HookPort filter。

作用：把 NLS/codepage/language 相关注册表查询重定向到 `LEPB` 中保存的值，让直接读注册表的 API 也符合目标 locale。

### 6. 异常与自定义代码页：`RtlKnownExceptionFilter` / `RtlCustomCPToUnicodeN`

时机：`HookNtdllRoutines()`。

做法：两个函数都使用普通 inline hook。

- `RtlKnownExceptionFilter` -> `LepKnownExceptionFilter`：在转交原 filter 前生成 minidump。
- `RtlCustomCPToUnicodeN` -> `LepCustomCPToUnicodeN`：当调用者传入非目标、非 UTF-8 的 CP table 时，先用目标 ANSI codepage table 重新初始化，再执行转换。

作用：前者用于诊断，后者修正部分绕过默认 ACP 的转换路径。

### 7. 基础 NLS 表和 PEB/TEB codepage pair

时机：`LepGlobalData::Initialize()` 和 `HackAnsiOemCodeHashNodes()`。

这两处会做一部分重复同步，原因是初始化阶段负责建立 LEP 自己的目标 NLS 表；而 `HackAnsiOemCodeHashNodes()` 发生在 kernel32/kernelbase 加载或通知阶段，需要把 ntdll、PEB/TEB、user32 等已经存在或刚建立的 codepage cache 再压回目标状态。

首次初始化时读取目标 ACP/OEMCP NLS 文件和 `l_intl.nls`，复制到 `CodePageMapView`，并写入 PEB 三个 NLS 表指针：

- `PEB.AnsiCodePageData`
- `PEB.OemCodePageData`
- `PEB.UnicodeCaseTableData`

然后用 `CodePageMapView` 调 `RtlInitNlsTables()` / `RtlResetRtlTranslations()`，随后调用 `LepSyncNtdllNlsGlobals()` 同步 ntdll 导出的全局变量 `NlsAnsiCodePage`、`NlsMbCodePageTag`、`NlsMbOemCodePageTag`。

后续 `HackAnsiOemCodeHashNodes()` 按同一套目标 NLS 表重同步运行时状态：

1. 写架构相关 ANSI/OEM codepage pair：x64 写 `PEB + 0x34C`，x86 写 `TEB + 0x228`。
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
- 在内部 initializer 最多 `0x800` 字节内找 `mov eax, 0x190`
- 从该点开始依次找 `BaseNlsDllInitialize`、`NlsProcessInitialize`、第三个 call 作为 `SetupAnsiOemCodeHashNodes`

作用：替换进程 ACP/OEMCP 后重建 kernelbase 的 ANSI/OEM code hash/cache nodes。

### 11. kernelbase named-locale cache 预热

时机：`HookKernel32Routines()` 调 `HackUserDefaultLCID2()`。

查找：`FindGetNamedLocaleHashNode(GetNLSVersionEx)` 从 `KERNELBASE.dll` 导出 `GetNLSVersionEx` 开始扫 call；新布局看到立即数 `0x8001` 时扩展到 `0x60` 字节，并选择前面近处有 `xor edx, edx` 的 call。

做法：临时 hook 找到的内部 `GetNamedLocaleHashNode` 为 `LepGetNamedLocaleHashNode`，调用 `GetUserDefaultLCID()` 预热/刷新后恢复。

作用：让 kernelbase named-locale cache 以目标 LCID/locale name 填充。

### 12. `NtUserCreateWindowEx`

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

### 13. `NtUserMessageCall`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：当前使用 `FindNtUserMessageCall2(user32)`。它查 `kernel32!GlobalLock`、`GlobalUnlock`、`GlobalFree` 的 IAT 项，遍历 user32 relocation table 找这三个导入指针连续出现的位置，再在前两个引用之间找 call 到 syscall stub。找到后普通 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserMessageCall`，注册 HookPort syscall filter。

x64 无 win32u：从 `SendNotifyMessageW/A` 开始，各扫 `0x30` 字节，第一个 call 到 x64 syscall stub 的目标视为 `NtUserMessageCall`。

作用：拦截 user32 消息派发底层路径，转换相关 ANSI 字符串消息并通过 W 语义派发。

### 14. `NtUserDefSetText`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：`FindNtUserDefSetText(user32)` 查 `NotifyWinEvent` 导出，扫描 user32 text 中 `push EVENT_OBJECT_NAMECHANGE` 模式，确认附近 call `NotifyWinEvent`，再向前找 call 到 `DefSetText`，要求目标 prologue 为 `8B FF 55 8B`，最后在 `DefSetText` 中找第一个 call 到 syscall stub。找到后普通 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserDefSetText`，注册 HookPort syscall filter。

x64 无 win32u：先按 `NtUserCreateWindowEx` 路径找到 internal `CreateWindowEx`，扫描到 `0xC0000000` 前的上一个非 syscall user32 call 作为内部 `RtlInitLargeUnicodeString`；再扫描 `.text` 中所有 call 到它的位置。对每个候选向后 `0x20` 字节，若恰好有一个 call 到 x64 syscall stub 且后面很快返回，则该 syscall 视为 `NtUserDefSetText`。

作用：保证默认窗口标题/文本设置路径遵循目标 ACP。

### 15. user32 DC / BeginPaint charset

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint`。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`，要求都是直接 x64 syscall stub，注册 HookPort filter。

x64 无 win32u：从 user32 导出 `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint` 取入口，并要求导出本身是直接 x64 syscall stub；不满足就失败。

作用：获取 DC 或 paint DC 后重置/检查 DC charset，避免绘制路径沿用本机区域字符集状态。

### 16. `SetWindowLongA` / `GetWindowLongA` / PtrA 与 `IsWindowUnicode`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`IsWindowUnicode`。

x64：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`SetWindowLongPtrA`、`GetWindowLongPtrA`、`IsWindowUnicode`。PtrA 是独立 hook，不是 LongA 的别名。

Win7 x64 特例：`SetWindowLongA` 和 `SetWindowLongPtrA` 使用 `LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP`，允许短跳和 relay，但禁止最后退回入口 14 字节绝对跳覆盖。原因是 Win7 x64 下这两个入口最终对应的 syscall stub 只有 11 字节，后面紧跟其它函数指令；写 14 字节 `OpJumpIndirect` 会跨过 stub 边界并覆盖后续函数开头。

作用：包装/恢复 A 窗口过程；`IsWindowUnicode` 对 tracked wrapped A window 返回 `FALSE`，保持外部观察到的 A 窗口语义。

### 17. 剪贴板 ANSI 数据

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86/x64：EAT inline hook `GetClipboardData`、`SetClipboardData`。

作用：让 `CF_TEXT` 和字符串数据按目标 ACP 转换。

### 18. `NtGdiHfontCreate`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86：没有 win32u 时从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，返回第一个 call 到 `IsSystemCall()` stub 的目标，随后用普通函数入口 inline hook。

x64 有 win32u：从 `win32u.dll` 按名称取 `NtGdiHfontCreate`，验证为直接 x64 syscall stub，注册 HookPort filter。

x64 无 win32u：从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，返回第一个 call 到直接 x64 syscall stub 的目标，注册 HookPort filter。

作用：控制/记录字体 charset 创建路径，让字体选择符合目标 locale。

### 19. gdi32 字体枚举和 DC/对象辅助 hook

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86：EAT inline hook `GetStockObject`、`DeleteObject`、`CreateCompatibleDC`、`EnumFontsW/A`、`EnumFontFamiliesA/W`、`EnumFontFamiliesExA/W`。

x64：EAT inline hook `GetStockObject`、`DeleteObject`、`CreateCompatibleDC`。字体枚举函数优先 hook `gdi32full.dll`，不存在时 hook `gdi32.dll`；枚举字体宏显式使用 `Mp::OpJumpIndirect`。

作用：调整字体枚举、stock font、兼容 DC 等路径中的 charset/font 行为。

### 20. DLL load notification

时机：`LepGlobalData::Initialize()` 注册 `LdrRegisterDllNotification()`。

处理：

- `USER32.dll` -> `HookUser32Routines()`
- `GDI32.dll` -> `HookGdi32Routines()`
- `KERNELBASE.dll` / `KERNEL32.dll` -> `HookKernel32Routines()`

作用：对后加载模块补装 hook 和 locale cache 同步。

### 21. `LoadMemoryDll()` shadow ntdll hook

时机：从内存加载 DLL 的辅助路径中。

做法：临时 hook shadow ntdll 中的 `NtQueryAttributesFile`、`NtOpenFile`、`NtCreateSection`、`NtMapViewOfSection`、`NtClose`、`NtQuerySection`。

作用：让内存 DLL 加载辅助逻辑能模拟/接管文件与 section 相关操作。它不是普通运行时 locale hook。

### 22. Heap corruption helper

时机：调试辅助功能启用时。

hook：`RtlAllocateHeap`、`RtlReAllocateHeap`、`RtlFreeHeap`、`RtlSizeHeap`。

作用：辅助定位 heap corruption，不属于转区必需路径。

### 23. 当前存在但未接入主路径的 helper

`FindNtUserMessageCall(user32)`：旧版 x86 helper，从 `SendNotifyMessageW` 最多扫 `0x40` 字节，找第一个 call 到 syscall stub；当前 `HookUser32Routines()` 使用的是 `FindNtUserMessageCall2()`。

`FindSendMessageWorker(user32)`：动态 probe helper，创建 button、subclass 后发送 `WM_SETTEXT`，probe callback 遍历栈帧返回 callback 前 call 目标；当前未被 `HookUser32Routines()` 使用。
