# LocaleEmulatorPlus-Core 维护说明

本文记录 Core 的转区原理、x86/x64 实现差异、现代工具链构建方式、patched linker 注意事项，以及当前依赖的非公开 API。它不是用户手册，而是之后升级 VS、SDK、WDK 或继续补 x64 功能时用的维护笔记。

## 总体结构

本仓库只保留 Core 侧两组 DLL：

- `LoaderDll_x86.dll` / `LoaderDll_x64.dll`
- `LocaleEmulatorPlus_x86.dll` / `LocaleEmulatorPlus_x64.dll`

GUI 仓库根据目标 EXE 的 PE machine 选择 `LEPProc_x86.exe` 或 `LEPProc_x64.exe`。`LEPProc_*` 再 P/Invoke 对应位数的 `LoaderDll_*`，由 Loader 创建目标进程，并让同位数的 `LocaleEmulatorPlus_*` 尽早进入目标进程。

构建输出：

```text
out\x86\LoaderDll_x86.dll
out\x86\LocaleEmulatorPlus_x86.dll
out\x64\LoaderDll_x64.dll
out\x64\LocaleEmulatorPlus_x64.dll
```

## 转区原理

LEP 的核心不是修改系统区域，而是在目标进程内尽早建立一套“伪区域环境”，让目标进程看到指定的 ACP/OEMCP/LCID/UI language/timezone/注册表值。

主要流程：

1. `LEPProc_*` 把 GUI 配置转换成 `LOCALEP_EMULATOR_PLUS_ENVIRONMENT_BLOCK`，调用 `LoaderDll_*!LepCreateProcess`。
2. `LoaderDll` 用非公开的 `CreateProcessInternalW` 创建目标进程。
3. `LoaderDll` 生成或打开按 PID 命名的共享 section：`Local\LOCALEP_EMULATOR_PLUS_PROCESS_ENVIRONMENT_BLOCK_SECTION_<pid>`。
4. 共享 section 内放 `LEPPEB`，包含目标 ACP/OEMCP/LCID、时区、注册表重定向项、LEP DLL 路径、`LdrLoadDll` 备份字节等。
5. `LoaderDll` 在目标进程早期 loader 路径中 patch `ntdll!LdrLoadDll`，让 `LocaleEmulatorPlus_*` 在 kernel32 初始化前取得执行权。
6. `LocaleEmulatorPlus` 初始化后恢复 `LdrLoadDll` 原字节，读取 `LEPPEB`，安装 ntdll/kernelbase/user32/gdi32 等 hook。
7. 之后目标进程调用 NLS、locale、窗口文字、字体、剪贴板、注册表、时区等相关 API 时，会被 LEP 的 hook 改写结果。

关键点是“早”：如果目标进程在 LEP 初始化前已经加载并初始化 kernel32/kernelbase，NLS 缓存已经按本机区域建立，后续再改 ACP/LCID 往往无效。因此 `LocaleEmulatorPlus.dll` 自身必须尽量只静态依赖 `ntdll.dll`；其他 DLL 只能 delay-load，且 delay helper 也不能依赖 kernel32。

## LoaderDll 和 LocaleEmulatorPlus 的依赖约束

`LoaderDll` 可以依赖 kernel32，因为它运行在启动器进程里，负责创建和注入目标进程，不需要作为目标进程中的第一个用户 DLL。

`LocaleEmulatorPlus` 不应普通静态导入 kernel32/ucrt/vcruntime。原因：

- 普通 import 会让 Windows loader 在调用 `LocaleEmulatorPlus!DllMain` 前先加载依赖 DLL。
- 如果 kernel32/kernelbase 先加载，LEP 的“第一 DLL”假设会被破坏。

当前构建中：

- `LocaleEmulatorPlus_x86.dll` 普通 import 只应有 `ntdll.dll`；`KERNEL32.dll`/`USER32.dll`/`GDI32.dll`/`DBGHELP.dll` 是 delay-load。
- `LocaleEmulatorPlus_x64.dll` 普通 import 只应有 `ntdll.dll`；`USER32.dll`/`GDI32.dll`/`DBGHELP.dll` 是 delay-load。当前 x64 代码路径没有产生 kernel32 delay import。
- `LoaderDll_*` 对 kernel32 的约束没有 `LocaleEmulatorPlus_*` 那么严格。

## x86 和 x64 的主要差异

### 入口跳转编码

x86 下无论 syscall、LdrLoadDll 还是普通 inline hook，均使用 5 字节相对跳转：

```asm
E9 xx xx xx xx
```

x86 的 `LDR_LOAD_DLL_BACKUP_SIZE` 是 5。

PS: `LdrLoadDll` 早期注入 patch 和普通 inline hook 的风险模型不同。它是对子进程
loader 早期路径的临时入口覆盖：第一次进入 `LoadFirstDll` 后会先恢复
`LdrLoadDll` 原始入口字节，再调用真正的 `LdrLoadDll`。因此
`LDR_LOAD_DLL_BACKUP_SIZE` 只表示这条专用 patch 固定备份/恢复多少字节；
它不做完整指令分析，也不生成 trampoline，更不会执行被覆盖的原始指令片段。
普通 inline hook 则必须按完整指令覆盖并生成 trampoline，否则原函数回跳路径会
执行到半条指令。

x64 下不能简单用一个长度概括所有 hook。当前有两种主要入口 patch 编码。

LdrLoadDll 和普通 x64 inline hook 当前默认使用 `OpJumpIndirect`，写入 14 字节：

```asm
FF 25 00 00 00 00
dq target
```

也就是 `jmp qword ptr [rip+0]` 后面跟 8 字节目标地址。它不占用 `rax/r10/r11`
等通用寄存器。对普通 inline hook 来说，入口处必须能覆盖至少 14 字节完整指令；
对 `LdrLoadDll` 早期注入 patch 来说，则只是固定覆盖 14 字节并在真正调用前恢复。

x64 的 `LDR_LOAD_DLL_BACKUP_SIZE` 是 14。

x64 HookPort/syscall wrapper 使用 12 字节 `mov rax, imm64; jmp rax`。这个
长度在 `HookPortStub.cpp` 的 `X64_SYSCALL_PATCH_SIZE = 12`，实际写入代码在
`WriteAbsoluteJump()`：

```asm
48 B8 imm64
FF E0
```

它只用于 `HookPortStub.cpp` 里被 typed wrapper 支持的 ntdll `Zw*` syscall stub。

HookPort/syscall wrapper 可以使用 12 字节 `mov rax; jmp rax`，是因为它不依赖
通用 inline hook 的 trampoline。x64 路径会先枚举 ntdll 导出的 `Zw*` stub，
解析 service index，然后为原函数人工构造一个最小 syscall stub：

```asm
4C 8B D1              ; mov r10, rcx
B8 service_index      ; mov eax, service_index
0F 05                 ; syscall
C3                    ; ret
```

typed wrapper 需要调用原函数时，会调用这个人工 stub，而不是执行被覆盖入口处的
原始指令。因此它不用和普通 inline hook 一样生成通用 trampoline。这里仍保留
12 字节 `mov rax; jmp rax`，主要是这条 syscall wrapper 路径的历史实现，并没有太多深意。

### HookPort

x86 保留原版 HookPort 的思路，更接近通用 syscall dispatcher。原版会定位/复制内核 syscall 入口附近的 hook port，并用统一 dispatcher 处理更多系统调用形态。

x64 当前实现是 `HookPortStub.cpp`，属于 typed syscall wrapper：

- 枚举 ntdll 导出的 `Zw*` syscall stub。
- 解析 x64 syscall stub 中的 service index。
- 对需要过滤的 syscall 函数入口写入 12 字节 `mov rax, imm64; jmp rax`。
- 每个被支持的 syscall 有一个显式 wrapper，例如 `HpNtCreateUserProcess`、`HpNtQueryValueKey`。
- wrapper 按真实函数签名调用 filter，再调用人工生成的原始 syscall stub。

这让 x64 路径更明确，但只支持 `FindWrapperByHash()` 中列出的 syscall。新增 filter 时必须补 wrapper，否则会返回 `STATUS_HOOK_PORT_UNSUPPORTED_SYSTEM`。

### ACP/OEMCP 生效点

x86 原版会写 TEB 私有字段：

- `TEB + 0x228`：ACP
- `TEB + 0x22A`：OEMCP

x64 当前写 PEB 私有字段：

- `PEB + 0x34C`：ACP
- `PEB + 0x34E`：OEMCP

随后 `LepSetupAnsiOemCodeHashNodes()` 会重新调用 kernelbase 内部 NLS 初始化链，
让 ANSI/OEM codepage hash/cache 按 LEP 写入的 ACP/OEMCP 重建。当前 x64
`KernelBase_x64.dll` 中观察到的链路是：

```text
kernelbase!KernelBaseDllInitialize
  +0x9A0E8: E8 -> kernelbase+0x9A250  ; KernelBaseBaseDllInitialize wrapper

kernelbase+0x9A250
  +0x9A294: E9 -> kernelbase+0x29FC0  ; _KernelBaseBaseDllInitialize/internal body

kernelbase+0x29FC0
  +0x2A495: B8 90 01 00 00            ; mov eax, 190h
  +0x2A4A1: E8 -> kernelbase+0x2910C  ; BaseNlsDllInitialize

kernelbase+0x2910C
  +0x29123: E8 -> kernelbase+0x28FB0  ; NlsProcessInitialize

kernelbase+0x28FB0
  +0x28FEB: E8 -> kernelbase+0x288F8  ; SetupAnsiOemCodeHashNodes
```

这里的 `_KernelBaseBaseDllInitialize` 不是公开导出名，而是 IDA/反编译导出中给
内部实现起的名字。导出的 `KernelBaseBaseDllInitialize` 是外层 wrapper；不同架构
或 build 里进入内部实现的第二个控制转移可能是 `E8 call`，也可能是 tail `E9 jmp`。
当前代码会把 `E8`/`E9` 一起计数，取第 2 个目标。

随后通过模式匹配调用 kernelbase 内部 NLS 初始化链中的 `SetupAnsiOemCodeHashNodes`，让 kernelbase 的 ANSI/OEM codepage hash/cache 重新按目标代码页建立。这部分和 Windows build 相关，系统升级后如果 `GetACP()` 不再返回目标代码页，应优先检查 `LepSetupAnsiOemCodeHashNodes()`。

### user32 / win32u

较新 Windows 中 user32 的底层调用在 `win32u.dll`。当前代码在 `HasWin32U` 时直接从 `win32u.dll` hook：

- `NtUserCreateWindowEx`
- `NtUserMessageCall`
- `NtUserDefSetText`

## 构建

默认构建：

```bat
build.bat
```

单独构建：

```bat
build.bat x86
build.bat x64
```

可覆盖的环境变量：

```bat
set VS_ROOT=D:\VisualStudio2026
set MSVC_VER=14.44.35207
set SDK_VER=10.0.26100.0
build.bat x86
```

构建脚本使用：

- `cl.exe` / `lib.exe`：来自 `%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%`
- `link.exe`：来自 `dep\tools\link.exe`，这是打过补丁的 linker
- SDK headers/libs：来自 `C:\Program Files (x86)\Windows Kits\10\Include|Lib\%SDK_VER%`

- 编译和生成 import lib 使用本机安装的 MSVC：`%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%`。
- 链接最终 DLL 使用 `dep\tools\link.exe`，因为它放宽了 delay-load 检查。
- 头文件和系统库仍依赖本机 Windows SDK/WDK 与 MSVC include/lib 目录。

其他开发者要编译时，推荐流程是：

1. 安装对应的 VS/MSVC、Windows SDK 和 WDK。
2. 复制自己本机 MSVC toolset 中与 `cl.exe` / `lib.exe` 配套的 linker 目录，例如
   `%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%\bin\Hostx64\x86`，作为新的 `dep\tools`
   基础。
3. 在这份本机工具链副本上 patch `link.exe`。
4. 用 `build.bat` 顶部的 `VS_ROOT`、`MSVC_VER`、`SDK_VER` 指向自己的安装路径。

原则上 `cl.exe`、`lib.exe`、`link.exe` 属于同一 MSVC toolset，最稳妥的
做法是三者版本一致。当前仓库中的 `dep\tools` 只是演示当前需要 patch 哪一类
linker 以及需要成套保留哪些相邻文件；升级或换机器时，不应长期混用差异过大的
compiler/linker。

目录职责：

- `dep\tools`：保留 patched linker 及其运行所需的一整套相邻工具链文件。
- `dep\libs`：保留自定义 delay-load helper、CRT shim 和 import-lib `.def` 源文件。
- `out\obj\<arch>`：编译中间 `.obj`。
- `out\libs\<arch>`：每次构建重新生成的 import `.lib` 和 helper `.lib`。
- `out\<arch>`：最终 DLL 输出。

主要编译/链接约束：

- `/nodefaultlib`
- `/entry:DllMain`
- `/manifest:no`
- `LocaleEmulatorPlus_*` 必须检查 import table，确认没有普通 kernel32/ucrt/vcruntime import。

## section 合并行为

`LocaleEmulatorPlus.cpp` 里有这两行 linker pragma：

```cpp
#pragma comment(linker, "/SECTION:.text,ERW /MERGE:.rdata=.text /MERGE:.data=.text")
#pragma comment(linker, "/SECTION:.Asuna,ERW /MERGE:.text=.Asuna")
```

作用是把 `.rdata`、`.data` 和 `.text` 最终合并进一个可读、可写、可执行的代码 section。后面又把 `.text` 合并进 `.Asuna`，所以最终镜像里核心代码和数据集中在一个 ERW section 中。

在这个项目里它主要服务于早期注入和 hook：

- LEP 要在目标进程 loader 极早期运行，少依赖常规 CRT/loader 设施；合并 section 可以减少 PE section/protection 组合的复杂度。
- hook/trampoline/self-patching 逻辑需要写代码页，ERW section 避免反复处理自身代码和相邻常量/全局数据的保护属性。
- 某些逻辑默认代码、常量、运行期数据离得很近且都可访问；保留这个布局能降低移植时引入额外初始化问题的概率。

代价也很明确：它破坏现代 W^X 习惯，安全性和可审计性不如 `.text` 只执行、`.rdata` 只读、`.data` 可写的普通布局。LNK4254 warning 正是 linker 在提示“把不同属性的 section 合并了”。当前 `build.bat` 用 `/ignore:4254` 隐藏这个已知 warning；这只是不再输出噪声，不会改变 section 合并行为。

## 私有 import lib 和 delay-load helper

当前不再依赖[原仓库](https://github.com/xupefei/Locale-Emulator-Core/tree/master/LocaleEmulator) `_Libs` 中的闭源 `MyLib.lib`、旧 `DelayImp.lib`、`undoc_ntdll.lib`、`undoc_k32.lib`。

替代方式：

- `dep\libs\LepDelayLoad.cpp`
  - 自定义 delay-load helper。
  - 使用 `LdrLoadDll` 和 `LdrGetProcedureAddress`。
  - 避免依赖 kernel32。
- `dep\libs\LepCrtShim.cpp`
  - x64 下补少量 CRT intrinsic/string helper，避免带入 ucrt/vcruntime。
- `dep\libs\lep_ntdll_alias.def`
  - x86：把 `_LdrInitializeThunk@8` 等 stdcall 修饰名映射到 ntdll 无修饰导出。
  - 同时导出 `_stricmp`、`_vscwprintf`、`_vsnwprintf`、`_vswprintf`、`_wcsicmp` 等 ntdll 里的 CRT 风格函数。
- `dep\libs\lep_ntdll_x64.def`
  - x64：导出 `LdrInitializeThunk`、`LdrRegisterDllNotification`、`LdrUnregisterDllNotification`。
- `dep\libs\lep_k32_alias.def`
  - x86：`CreateProcessInternalW=_CreateProcessInternalW@48`
- `dep\libs\lep_k32_x64.def`
  - x64：`CreateProcessInternalW`
- `dep\libs\ntdll_vsnprintf.def`
  - 暴露 ntdll 中可用的格式化函数。

构建脚本每次会用当前工具链的 `lib.exe /def` 重新生成这些 import lib，避免复用旧二进制 lib 带来的兼容性问题。

## patched link.exe

当前补丁对象是现代 MSVC linker。本仓库中带有一份示例：

```text
dep\tools\link.exe
```

作用：放宽 linker 对 delay-load DLL 的非法名单检查，让 `LocaleEmulatorPlus` 可以把特定系统 DLL 放进 delay import，而不是被 linker 拒绝或被迫变成普通 import。旧 VS2015 linker 中相同逻辑可在 IDA 导出的 `CheckInvalidDelayLoadDlls` / `FInvalidDelayLoadDll` 附近看到。

为什么需要它：

- MSVC linker 对部分 DLL 的 `/delayload` 有硬编码限制。
- LEP 需要 delay-load kernel32/user32/gdi32/dbghelp，尤其不能让 `LocaleEmulatorPlus` 普通静态依赖 kernel32。
- 只替换 delay helper 不够；如果 linker 自己拒绝生成 delay import，必须先绕过 linker 检查。

更换/升级 linker 时的处理流程：

1. 复制本机新工具链中与 `cl.exe` / `lib.exe` 配套的 linker 目录，例如 `Hostx64\x86`。
2. 用原版新 linker 构建，确认失败点是否仍是 invalid delay-load DLL 或错误 import table。
3. 用 IDA/Ghidra 搜索 delay-load 非法 DLL 检查相关函数。关键词包括 `delayload`、`kernel32.dll`、`user32.dll`、`gdi32.dll`。
4. 定位硬编码 DLL 分类/掩码检查，将禁止 kernel32 等 delay-load 的 bitmask 或分支放宽。
5. 对比原版和补丁版，应尽量只有极少字节差异。
6. 用这份 patched 工具链目录更新 `dep\tools`，要成套保留 linker 旁边的依赖 DLL、PDB 服务相关工具、资源目录等。
7. 重新运行 `build.bat`。
8. 用 `dep\tools\dumpbin.exe` 检查 `LocaleEmulatorPlus_*`：普通 imports 中不得出现 kernel32/ucrt/vcruntime。

## 更换/升级 VS/SDK/WDK 时的检查清单

1. 修改 `build.bat` 顶部：`VS_ROOT`、`MSVC_VER`、`SDK_VER`。
2. `SDK_VER` 应指向实际存在的 Windows Kits 版本。
3. 确认存在 `%VC_TOOLS%\include`、`%VC_TOOLS%\lib\x86`、`%VC_TOOLS%\lib\x64`、`%SDK_INC%\um`、`%SDK_INC%\shared`、`%SDK_INC%\km`。
4. `km` 目录通常来自 WDK。若找不到 WDK/内核头，确认 Visual Studio Installer 已安装 WDK/Windows Driver Kit 组件，且已安装与当前 Windows SDK 版本对应的 [WDK](https://learn.microsoft.com/zh-cn/windows-hardware/drivers/download-the-wdk)。
5. 更新 `dep\tools` 时要成套复制工具链文件，尤其是 linker 旁边的依赖 DLL、PDB 服务相关工具、资源目录等。
6. 重新生成 import lib，不要复用旧 `.lib`。构建脚本会自动生成 `out\libs\x86\*.lib` 和 `out\libs\x64\*.lib`。
7. 构建后检查 import table。
8. x86 运行确认：32 位游戏能启动，`GetACP()`/标题/字体/注册表重定向符合预期。
9. x64 运行确认：64 位目标能启动，`GetACP()` 返回目标 ACP，例如日文 932；子进程继承转区，注册表重定向和时区正常。
10. 如出现 `0xC0000142`，优先检查 `LocaleEmulatorPlus` 是否普通导入了 kernel32/ucrt/vcruntime，以及 kernel32/kernelbase 是否在 LEP 初始化前已经加载。
11. 如出现 `0xC0000005`，优先检查 x64 inline hook 覆盖长度、绝对跳转写入、原始 stub 生成和 restore 逻辑。

## 非公开 API 和脆弱点

这里的“非公开”包括未正式文档化的 ntdll 导出、kernel32 内部导出、win32u syscall、PEB/TEB 私有字段，以及通过模式匹配调用的 kernelbase 内部例程。

运行时查找或模式匹配：

- `KERNELBASE.dll` loader/NLS 初始化链：`KernelBaseDllInitialize`、`KernelBaseBaseDllInitialize` wrapper、内部 `_KernelBaseBaseDllInitialize`、`BaseNlsDllInitialize`、`NlsProcessInitialize`、`SetupAnsiOemCodeHashNodes`。
- `gNlsProcessLocalCache`：旧逻辑中通过 relocation 扫描定位，目前主要保留作参考。

公开但被 hook 或特殊处理的 API：

- user32：`SetWindowLongA`、`GetWindowLongA`、`IsWindowUnicode`、`SendMessageA`、`SetWindowTextA`、窗口创建和消息调用相关入口。
- gdi32：字体枚举、字体创建、charset/text metric 等相关入口，见 `Hooks\Gdi32Hook.cpp`。
- ntdll/kernelbase：NLS、locale、注册表和进程创建相关入口。

私有结构和偏移：

- `TEB + 0x228`：x86 ACP
- `TEB + 0x22A`：x86 OEMCP
- `PEB + 0x34C`：x64 ACP
- `PEB + 0x34E`：x64 OEMCP
- `LdrInitializeThunk` 内部调用 `NtContinue` 的位置，x86 路径通过扫描定位。
- `LdrLoadDll` 入口会被短跳或绝对跳修改。

这些偏移和内部调用点均可能随 Windows build 改变。若系统升级后转区失败，优先检查 PEB/TEB 偏移、kernelbase NLS pattern、win32u 导出、ntdll syscall stub 格式。
