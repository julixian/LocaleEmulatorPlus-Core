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

32 位 HookPort 从干净 ntdll 镜像枚举 `Zw*` 导出，解析 `mov eax, service_id` 形式的 syscall stub，并建立 service id、参数长度、返回指令地址和 filter callback 表。native x86 和 WOW64 共用这张表以及 `HpUserModeDispatcher()`，但入口 patch 点不同：

- native x86 patch `ntdll!KiFastSystemCall`。Win8+ 还会由 `PatchNtdllSysCallStub()` 把 ntdll `Zw*` stub 内部 call site 重新指向该入口。
- WOW64 由 `GetWow64SyscallJumpStub()` 扫描 `NtCancelTimer`，识别 Win7 常见的 `call dword ptr fs:[0xC0]`、较新系统常见的 `mov edx, address; call edx`，以及兼容形式 `mov edx, address; call [edx]`。`NtCancelTimer` 只是用来发现所有 32 位 syscall 共用的代码入口，本身不会被修改。

WOW64 的实际 patch 地址取决于 ntdll stub 的形式：

- 现代 `mov edx, address; call edx` 中的 `address` 通常指向 ntdll 内的 32 位共享 gateway，例如内容为 `jmp dword ptr [Wow64Transition]` 的 `Wow64SystemServiceCall`。此时 patch 的就是这个 gateway，例如调试器里看到的 `77345150: jmp dword ptr [Wow64Transition]`。
- Win7 `call dword ptr fs:[0xC0]` 会通过 `TEB32.WOW32Reserved` 直接取得目标。该目标通常已经是以 far jump 开头的 32/64 位切换入口，因此 patch 的是这个目标地址，不是 ntdll 中的 `call fs:[0xC0]` 指令，也不存在上面那层 ntdll gateway。

安装 WOW64 hook 时，`CopyOneOpCode()` 把 patch 地址处的第一条完整指令复制到 `StubSysEnter`。现代 gateway 对应的副本通常是 `jmp dword ptr [Wow64Transition]`；Win7 目标对应的副本通常是 far jump。随后 patch 地址的开头被改成 `push HookRoutine; ret`。下文中的 `StubSysEnter` 专指这段“被覆盖首指令的可执行副本”，不是一个按 C 调用约定调用的原函数。

Win7 syscall stub 会在 `call fs:[0xC0]` 前执行 `lea edx, [esp+4]`，进入 hook 时 `EDX` 已指向第一个 syscall 参数。现代 `mov edx, address; call edx` 进入 hook 时 `EDX` 仍是 gateway 地址，所以 `HookSysEnter_Wow64_Win8Plus` 用 `lea edx, [esp+8]` 重建第一个参数的地址，再进入公共汇编入口。这个改写只用于满足 HookPort 把 `EDX` 作为 `HpUserModeDispatcher()` 的 `Arguments` 参数这一内部约定；现代 Windows 自己的 WOW64 切换代码不要求这个值。

#### `HpUserModeDispatcher()`

`HookSysEnter_Wow64` 保存 EFLAGS 和通用寄存器后只调用一次 `HpUserModeDispatcher()`。它不执行 syscall，职责是运行 callback 链并产生最终 filter 决策：

1. 从 `EAX` 取得 service id，并拆分 service table index 和 table 内 index。
2. 在 syscall 表中找到 `SYSCALL_INFO`，取得参数长度、filter callback 和 ntdll syscall stub 中的 `ret` 地址。
3. 检查当前线程是否通过 `HookPortBypassFilter` 要求跳过这个 syscall 的 filter。命中 bypass 时不调用任何 callback，`Action` 保持默认的 `ContinueSystemCall`，随后仍执行原 syscall；bypass 不是阻止 syscall。
4. `HpServiceDispatcherInternal()` 按 `FilterBitmap` 顺序调用 callback。每个 callback 通过普通 C 返回值产生候选 syscall 结果，通过 `FltInfo.Action` 产生控制动作；callback 返回后立即检查 Action。`ContinueSystemCall` 才调用下一个 callback，`PermitSystemCall` 或 `BlockSystemCall` 都立即结束 callback 循环。每次普通返回值都会覆盖前一个，所以循环结束时只保留最后一个实际执行的 callback 返回值。callback 抛出的用户态异常由 dispatcher 的 SEH 转成异常状态值。
5. callback 循环结束后，`HpUserModeDispatcher()` 只对最终的 `BlockSystemCall` 做返回地址处理：把原 `call` 留在栈上的返回地址改成 ntdll syscall stub 中的 `ret` 地址。随后它把最后保留的 callback 返回值作为自己的 C 返回值，并把最终 Action 留在汇编入口传入的 `FltInfo` 中。

这里有两层不同的 Action 检查，不能混为一次“放行/阻止选择”：`HpServiceDispatcherInternal()` 中的检查只决定是否继续调用下一个 callback，不会执行或跳过 syscall；`HookSysEnter_Wow64` 在 dispatcher 整体返回后读取最终 Action，才决定恢复寄存器并执行原 syscall，还是采用 callback 返回值并跳过原 syscall。例如 callback 1 返回 `ContinueSystemCall` 时只会进入 callback 2；callback 2 返回 `BlockSystemCall` 后循环停止，最终由汇编入口执行阻止路径。

代码保留了 x86 全局 filter 接口 `HpSetGlobalFilter()`，但当前项目没有调用它安装全局 filter，因此正常运行不会进入该分支，下面的运行流程也不把它列为可用步骤。该分支本身也没有形成可靠闭环，所以 `GlobalFilterHandled` 和 `GlobalFilterModified` 都不是当前项目可依赖的 Action；实际支持的是以下 per-syscall Action：

| Action | 是否继续后续 callback | HookPort 是否自动执行原 syscall | 对调用者可见的返回值 |
| --- | --- | --- | --- |
| `ContinueSystemCall` | 是；也是默认值 | 所有 callback 结束后执行 | Windows syscall 返回值；callback 返回值被忽略 |
| `PermitSystemCall` | 否 | 执行 | Windows syscall 返回值；callback 返回值被忽略 |
| `BlockSystemCall` | 否 | 不执行 | 当前 callback 返回值 |

当前项目没有显式设置 `PermitSystemCall` 的调用点。未设置 Action 的 callback 保持 `ContinueSystemCall`；需要完全替代 syscall，或者已经在 callback 内显式调用过原 syscall、不希望 HookPort 再调用一次时，callback 设置 `BlockSystemCall`。因此表中的“不执行”只表示 callback 返回后 HookPort 不再自动执行；callback 本身仍可通过 `HpCallSysCall()` 调用原 syscall。

#### WOW64 运行时返回路径

为区分栈上的三个控制流地址，记：A 是 ntdll syscall stub 中原 `call edx`/`call fs:[0xC0]` 后面的地址，B 是 `HookSysEnter_Wow64` 中 `call HpUserModeDispatcher` 后面的地址，C 是 HookPort 人工压栈的 `StubSysEnter` 地址。

1. ntdll 执行原 `call` 时压入 A。patch 地址执行 `push HookRoutine; ret` 只模拟一次跳转，进入 `HookRoutine` 后 A 仍在栈顶。
2. `HookSysEnter_Wow64` 先压入 C，再保存 EFLAGS 和通用寄存器，然后调用 `HpUserModeDispatcher()`。该 C/C++ 调用临时压入 B；dispatcher 正常返回 B 后，B 和它的调用参数已经清理，A 与 C 仍在更深的栈中。
3. `ContinueSystemCall` 和 `PermitSystemCall` 都执行放行路径：恢复寄存器和 EFLAGS，然后 `ret` 到 C。`StubSysEnter` 执行安装期复制的首指令。现代路径由复制的 `jmp [Wow64Transition]` 跳到 far jump；Win7 路径直接执行复制的 far jump。两者随后都进入 Windows 原有的 WOW64 系统调用处理，完成后使用仍在栈上的 A 返回 ntdll syscall stub，再由该 stub 返回应用程序。
4. `BlockSystemCall` 执行阻止路径：dispatcher 先把栈中的 A 改成 ntdll syscall stub 的 `ret` 地址；汇编入口保留 callback 返回值在 `EAX`，丢弃已保存的寄存器区并跳过 C，最后 `ret` 到改写后的 A。这样会直接执行 ntdll stub 的 `ret` 并返回应用程序，不执行 `StubSysEnter`，也不由 HookPort 再触发 WOW64 模式切换和原 syscall。callback 如果此前自行调用过原 syscall，那次调用已经完成，不受这里的跳过影响。

这里的“放行”仅表示 HookPort 在 callback 之后继续自动执行 Windows 原 syscall；“阻止”表示 HookPort 直接采用 callback 返回值并跳过这次自动调用，不是安全权限判定，也不限制 callback 自己调用原 syscall。

### x64 HookPort

实现位置：`LocaleEmulatorPlus/HookPortStub.cpp`。

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

#### x64 filter 与返回路径

x64 不使用 `HpUserModeDispatcher()`，也不 patch WOW64 共享 gateway。每个受支持的 syscall 都有一个参数类型与原 syscall 相同的 wrapper，运行流程是：

1. 调用者照常调用 ntdll/win32u/gdi32 的 syscall stub；该 stub 的入口 patch 直接跳到对应 wrapper，调用者原先压入的返回地址和 x64 ABI 参数保持不变。
2. wrapper 通过 hash 找到 `SYSCALL_INFO` 和安装时生成的人工原始调用 stub，再由 `DispatchTypedFilter()` 运行该 syscall 的 callback。命中 `HookPortBypassFilter`、没有 callback 或 filter 未启用时，wrapper 直接调用人工 stub。
3. `ContinueSystemCall` 继续调用剩余 callback；全部 callback 返回后，wrapper 调用人工 stub。`PermitSystemCall` 停止调用剩余 callback，然后同样调用人工 stub。`BlockSystemCall` 停止调用剩余 callback，跳过这次自动调用，直接把当前 callback 的返回值返回给原调用者。
4. 人工 stub 重新执行 `mov r10, rcx`、`mov eax, service_id` 和系统调用尾部。原 stub 带 `KUSER_SHARED_DATA.SystemCall` 判断时，人工 stub 也按运行时状态选择 `syscall` 或 `int 2e`；否则直接执行 `syscall`。内核返回后，人工 stub 的 `ret` 回到 wrapper，wrapper 再按普通 x64 函数调用返回原调用者。

人工原始调用 stub 不是从被覆盖入口继续执行的 trampoline，而是按解析结果重新生成的独立代码。callback 若通过 `HpGetSystemCallOriginal()` 自己调用它，通常还要设置 `BlockSystemCall`，否则 callback 返回后 `DispatchTypedFilter()` 会再自动调用一次。x64 没有全局 filter 流程：`HpSetGlobalFilter()` 固定返回 `STATUS_NOT_SUPPORTED`；`GlobalFilterHandled` 和 `GlobalFilterModified` 也不是 x64 per-syscall callback 支持的 Action。若错误地设置其中之一，callback 循环会因为 Action 不再是 `ContinueSystemCall` 而停止，但最终仍走人工原始调用 stub，并不会执行名字所暗示的全局处理。另一个与 x86 的差异是，`DispatchTypedFilter()` 没有用 SEH 包裹 callback；callback 抛出的异常会继续向外传播。

#### 原调用与线程级防重入

`TEB_ACTIVE_FRAME::Push()` 所说的“压入”不是向 CPU 栈压一个返回地址，而是用 `RtlPushFrame()` 把带 Context 标记的记录挂到当前线程 TEB active-frame 链表头。`FindThreadFrame(Context)` 只能在当前线程的链表中找到它；记录析构或显式 `Pop()` 后恢复前一个链表头，因此其它线程不可见，并且支持嵌套调用。

HookPort 的通用防重入记录是 `SYSCALL_FILTER_SKIP_INFO`，Context 为 `SYSCALL_SKIP_MAGIC`，并保存要跳过 filter 的 service id。`HpCallSysCall()` 和 `CallSysCall()` 都通过临时 `HookPortAutoBypassFilter` 在调用期间压入该记录；嵌套调用再次进入 HookPort 时，`HpIsCurrentCallSkip()` 发现 service id 匹配便跳过 callback，但仍执行原 syscall。x86 和 x64 编译的是同一套 `HookPortAutoBypassFilter`，所以两边调用这两个宏时都会压 frame，区别在于实际必要性：

- x86 的 `SYSCALL_INFO.FunctionAddress` 是 ntdll syscall stub。callback 调它以后会再次经过已经 patch 的共享 syscall 入口，所以必须依靠 skip frame 防止同一个 filter 递归。
- x64 安装 hook 后，`SYSCALL_INFO.FunctionAddress` 已改为人工原始调用 stub。调用它直接执行 `syscall`/`int 2e`，不会再次进入被 patch 的 wrapper；当前 `HpCallSysCall()` 仍因共用宏而压相同的 frame，但对这条直接人工-stub 路径实际上不需要它。x64 的 `DispatchTypedFilter()` 仍保留 `HpIsCurrentCallSkip()` 检查，供确实再次进入 wrapper 的调用使用。

GDI 字体路径另有一套与 HookPort 无关的线程级保护。`CreateFontIndirectBypassA/W()` 压入 Context 为 `GDI_HOOK_BYPASS` 的 `TEB_ACTIVE_FRAME`，再调用公开的 `CreateFontIndirectA/W()`；这些 API 会在更深处到达被 hook 的 `NtGdiHfontCreate`，`LepNtGdiHfontCreateWorker()` 发现该标记后跳过 charset 二次改写。这个保护在 x86/x64 都使用，作用域仅限当前线程的这次字体创建调用。

NtUser 不使用上述两种 frame。x86 对找到的 `NtUserCreateWindowEx`、`NtUserMessageCall` 和 `NtUserDefSetText` 做普通 inline hook，原调用通过 `HookStub.StubNtUser*` trampoline 执行，trampoline 从被覆盖指令后继续，因此不会重新进入函数开头的 hook。x64 由 HookPort patch syscall stub，原调用统一通过 `HpGetSystemCallOriginal(hash)` 查询进程级人工原始调用 stub；该地址由所有线程共享，直接调用也不会重新进入 wrapper。查询失败表示对应 x64 syscall hook 没有成功建立，不会回退到 x86 使用的 `HookStub`。

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
