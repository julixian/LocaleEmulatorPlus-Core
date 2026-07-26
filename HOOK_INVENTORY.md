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
F6 04 25 08 03 FE 7F 01         ; test byte ptr [0x7FFE0308], 1
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

### 1. 初次进程注入：`LdrLoadDll` 调试断点

时机：`LoaderDll` 调用 `CreateProcessWithDll(CPWD_BEFORE_KERNEL32)` 创建目标进程时。

做法：`InjectDllBeforeKernel32Loaded()` 加载一份干净 ntdll 镜像，通过导出表找 `LdrLoadDll`，映射回目标进程地址，把远程入口临时写成 2 字节 `UD2`。调试器等到第一次 loader 侧 `LdrLoadDll` 调用触发非法指令后，恢复原字节，记录第一次 call site，再把 `LocaleEmulatorPlus_*` 注入到目标进程。

作用：让 LEP 在 kernel32/kernelbase 初始化前取得执行权，并拿到目标进程真实 loader 调用现场。

### 2. 子进程传播：`NtCreateUserProcess` 与 shadow `LoadFirstDll`

时机：`HookNtdllRoutines()` 始终安装 `NtCreateUserProcess` filter。真实 `NtCreateUserProcess` 成功后，`LepNtCreateUserProcess()` 调 `InjectSelfToChildProcess()`。

x86/x64：`NtCreateUserProcess` 本身走 HookPort filter。子进程 `LdrLoadDll` 入口 patch x86 用 `E9 rel32`，x64 用 `FF 25 [rip+0]` 加绝对目标地址。

做法：把当前 LEP DLL 手工 shadow 到子进程，重定位到远程地址，然后把子进程 `ntdll!LdrLoadDll` 入口改成跳到 shadow 里的 `LoadFirstDll`。`LoadFirstDll()` 进入后恢复 `LdrLoadDll` 原字节，初始化 LEP，再调用真实 `LdrLoadDll`。

注意：代码直接使用已保存的绝对地址 `LEPPEB->LdrLoadDllAddress`。这隐含假设同架构子进程 ntdll 映射基址与当前进程一致，因此 `ntdll!LdrLoadDll` 地址一致。

作用：让目标进程创建的子进程自动继承 LEP 环境。

### 3. `NtContinue`

时机：`HookNtdllRoutines()`。

x86：`SearchLdrInitNtContinue()` 扫描 `LdrInitializeThunk` 前 `0x5B` 字节，改写其中目标为 `NtContinue` 的 call site。`LepLdrInitNtContinue()` 在 loader 即将恢复线程初始 context 前，把当前线程的 `TEB.CurrentLocale` 设置为 `LEPB.LocaleID`，然后通过 `StubLdrInitNtContinue` 调用原 `NtContinue`。hook 范围限定在 loader 的这处调用。

x64：HookPort 对 `NtContinue` syscall 入口安装 filter，覆盖经过该 ntdll stub 的调用。`LepNtContinue()` 设置当前线程的 `TEB.CurrentLocale`，保持 `CONTEXT`、`TestAlert` 和默认 `ContinueSystemCall` Action，随后由 wrapper 调用 original。`SearchLdrInitNtContinue()` 的扫描结果用于兼容性检查和日志。

作用：`TEB.CurrentLocale` 是线程级状态。这个 hook 以 loader 结束线程初始化并通过 `NtContinue` 恢复用户执行现场的时刻为锚点，确保新线程进入用户入口前看到 LEP 的目标 locale。

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

### 6. 自定义代码页转换：`RtlCustomCPToUnicodeN`

时机：`HookNtdllRoutines()`。

状态：实现由 `LEP_ENABLE_CUSTOM_CP_TO_UNICODE_HOOK` 控制，当前值为 `0`，`HookNtdllRoutines()` 不安装该 hook。

启用时：inline hook `RtlCustomCPToUnicodeN` 为 `LepCustomCPToUnicodeN`。当调用者传入既不是目标代码页，也不是 UTF-8 的 CP table 时，先用目标 ANSI codepage table 重新初始化，再执行转换。

用途：强制显式传入的 custom CP table 使用目标 ACP。默认 ANSI/OEM 转换由基础 NLS 表同步负责。

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

x86 user32 布局：`FindNtUserCreateWindowEx(user32)` 查 `ntdll!RtlQueryInformationActiveActivationContext` 的 IAT slot，遍历 relocation 找 `FF 15 [slot]` 调用点，再向后最多 `0x150` 字节找 syscall stub，随后注册 HookPort filter。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserCreateWindowEx`，注册 HookPort filter。

x64 user32 兼容布局：先从 `CreateWindowExW/A` 找 internal `CreateWindowEx`；在其中最多 `0x2C0` 字节内找包含立即数 `0xC0000000` 的指令；之后第一个 call 到 user32 内部函数视为 `VerNtUserCreateWindowEx`；再在其中最多 `0x300` 字节找第一个 call 到 x64 syscall stub，作为 `NtUserCreateWindowEx`。

参数版本差异：

- Win7：15 参数，x64 末尾 activation context 句柄为 `ULONG_PTR`。
- Win8/8.1：16 参数，x64 末尾 activation context 句柄为 `ULONG_PTR`。
- Win10+：17 参数，x64 末尾 activation context 句柄为 `ULONG_PTR`。

版本选择由 `Nt_QueryOsVersion()` 的结果决定。

作用：把 ANSI 窗口/类名数据按目标代码页转换，包装 A 窗口过程，并让窗口创建走 native 路径。

### 14. `NtUserMessageCall`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86 user32 布局：`FindNtUserMessageCall2(user32)` 查 `kernel32!GlobalLock`、`GlobalUnlock`、`GlobalFree` 的 IAT 项，遍历 user32 relocation table 找这三个导入指针连续出现的位置，再在前两个引用之间找 syscall stub，随后注册 HookPort filter。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserMessageCall`，注册 HookPort filter。

x64 user32 兼容布局：从 `SendNotifyMessageW/A` 开始，各扫 `0x30` 字节，第一个 call 到 x64 syscall stub 的目标视为 `NtUserMessageCall`。

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

x86 user32 布局：`FindNtUserDefSetText(user32)` 查 `NotifyWinEvent` 导出，扫描 user32 text 中 `push EVENT_OBJECT_NAMECHANGE` 模式，确认附近 call `NotifyWinEvent`，再向前找 call 到 `DefSetText`，要求目标 prologue 为 `8B FF 55 8B`，最后在 `DefSetText` 中找 syscall stub，随后注册 HookPort filter。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserDefSetText`，注册 HookPort filter。

x64 user32 兼容布局：先按 `NtUserCreateWindowEx` 路径找到 internal `CreateWindowEx`，扫描到 `0xC0000000` 前的上一个 user32 call 作为内部 `RtlInitLargeUnicodeString`；再扫描 `.text` 中所有 call 到它的位置。对每个候选向后 `0x20` 字节，若恰好有一个 call 到 x64 syscall stub 且后面很快返回，则该 syscall 视为 `NtUserDefSetText`。

作用：保证默认窗口标题/文本设置路径遵循目标 ACP。

### 18. user32 DC / BeginPaint charset

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint`。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtUserGetDC`、`NtUserGetDCEx`、`NtUserGetWindowDC`、`NtUserBeginPaint`，验证为直接 x64 syscall stub，再注册 HookPort filter。

x64 user32 兼容布局：从 user32 导出 `GetDC`、`GetDCEx`、`GetWindowDC`、`BeginPaint` 取直接 syscall stub，验证后注册 HookPort filter。

作用：获取 DC 或 paint DC 后重置/检查 DC charset，避免绘制路径沿用本机区域字符集状态。

### 19. `SetWindowLongA` / `GetWindowLongA` / PtrA 与 `IsWindowUnicode`

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`IsWindowUnicode`。

x64：EAT inline hook `SetWindowLongA`、`GetWindowLongA`、`SetWindowLongPtrA`、`GetWindowLongPtrA`、`IsWindowUnicode`。

Win7 x64 特例：`SetWindowLongA` 和 `SetWindowLongPtrA` 使用 `LEP_FUNCTION_NO_ABSOLUTE_JUMP_OP`，允许短跳和 relay，但禁止最后退回入口 14 字节绝对跳覆盖。原因是 Win7 x64 下这两个入口最终对应的 syscall stub 只有 11 字节，后面紧跟其它函数指令；写 14 字节 `OpJumpIndirect` 会跨过 stub 边界并覆盖后续函数开头。

作用：包装/恢复 A 窗口过程；`IsWindowUnicode` 对 tracked wrapped A window 返回 `FALSE`，保持外部观察到的 A 窗口语义。

### 20. 剪贴板 ANSI 数据

时机：`USER32.dll` 加载后，`HookUser32Routines()`。

x86/x64：EAT inline hook `GetClipboardData`、`SetClipboardData`。

作用：让 `CF_TEXT` 和字符串数据按目标 ACP 转换。

### 21. `SystemParametersInfoA/W`

时机：`USER32.dll` 加载后，`HookUser32Routines()`，仅当目标 ANSI codepage 为 `932` 时安装。

x86/x64：EAT inline hook `SystemParametersInfoA/W`。hook 先调用对应的原函数；当调用成功、`uiAction == SPI_GETDEFAULTINPUTLANG` 且 `pvParam` 非空时，把输出的 `HKL` 改写为 `0x04110411`。

作用：让查询默认输入语言的 A/W 路径看到日文默认输入法布局，补齐 CP932 转区下程序读取输入语言状态的 user32 路径。

### 22. `NtGdiHfontCreate`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`。

x86 gdi32 布局：从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，取第一个 call 到 `IsSystemCall()` stub 的目标，随后注册 HookPort filter。

x64 win32u 布局：从 `win32u.dll` 按名称取 `NtGdiHfontCreate`，验证为直接 x64 syscall stub，注册 HookPort filter。

x64 gdi32 兼容布局：从 `gdi32!CreateFontIndirectExW` 开始最多扫 `0xA0` 字节，返回第一个 call 到直接 x64 syscall stub 的目标，注册 HookPort filter。

线程级重入保护：字体枚举 callback 调用 `AdjustFontData()` 校正字体名称和度量时，会通过 `CreateFontIndirectBypassW()` 创建临时字体；它把 Context 为 `GDI_HOOK_BYPASS` 的 `TEB_ACTIVE_FRAME` 压入当前线程，再调用 `CreateFontIndirectW()`。调用链到达 `LepNtGdiHfontCreateWorker()` 时，该标记使这次字体创建跳过 charset 二次改写。`CreateFontIndirectBypassA()` 当前没有调用点。

作用：控制/记录字体 charset 创建路径，让字体选择符合目标 locale。

### 23. `QueryFontAssocStatus`

时机：`GDI32.dll` 加载后，`HookGdi32Routines()`，仅当目标 ANSI codepage 为 `932` 时安装。

x86/x64：对 `QueryFontAssocStatus` 使用普通 inline hook。查找时优先从已加载的 `gdi32full.dll` 导出表取地址，随后查询 `gdi32.dll` 导出表。`LepQueryFontAssocStatus()` 固定返回 `0`。

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

作用：为内存 DLL 加载辅助逻辑模拟和接管文件、section 相关操作。

### 27. Heap corruption helper

时机：调试辅助功能启用时。

hook：`RtlAllocateHeap`、`RtlReAllocateHeap`、`RtlFreeHeap`、`RtlSizeHeap`。

作用：记录堆分配生命周期，辅助定位 heap corruption。
