# LocaleEmulatorPlus-Core 维护说明

本文说明 Core 的模块职责、转区流程、构建方式和工具链约束。

## 总体结构

本仓库提供两组原生 DLL：

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

`LocaleEmulatorPlus/support/` 提供 NT API、内存、字符串、PE 和指令补丁等支持代码，编译为 `LepSupport.lib`，供 Core 和 Loader 分别静态链接。模块划分和验证方式见 [原生支持层维护](ai-docs/native-support.md)。

## 转区原理

LEP 的核心是在目标进程内尽早建立一套“伪区域环境”，让目标进程看到指定的 ACP/OEMCP/LCID/UI language/timezone/注册表值。

主要流程：

1. 调用方填写 `LOCALE_EMULATOR_PLUS_ENVIRONMENT_BLOCK` (LEPB)，通过 Loader 的启动接口传入代码页、区域、时区 ID 和可选名称覆盖。
2. `LoaderDll` 在入口将公开配置解析为 `LEP_RUNTIME_ENVIRONMENT`，补全时差、切换日期和返回名称，再与 `LocaleEmulatorPlus_*` 的路径序列化为 `LEP_BOOTSTRAP_PAYLOAD`。Payload 只保存相对偏移；子进程继承解析后的配置快照。公开 ABI 详见 [原生调用说明](ai-docs/lepb-v4.md)。
3. `LepCreateProcess2` 调用非公开的 `CreateProcessInternalW`，创建挂起且未加载 kernel32.dll/kernelbase.dll 的目标进程。
4. `LoaderDll` 从磁盘读取对应位数的 `LocaleEmulatorPlus_*`，根据 `SizeOfImage` 和 Payload 大小在目标进程中申请一块连续区域。
5. `LoaderDll` 根据申请的确定的远程地址在本地重定位 `LocaleEmulatorPlus_*` 镜像后，把镜像和 Payload 写入目标进程；Payload 紧跟在 PE `SizeOfImage` 对齐后的尾部。
6. `LoaderDll` 把目标 `ntdll!LdrLoadDll` 的原始字节、原页面保护以及 Payload 偏移/大小写入 shadow 的 bootstrap 元数据，然后 patch 目标 `ntdll!LdrLoadDll` 到 `LocaleEmulatorPlus_*!LoadFirstDll`。
7. `LocaleEmulatorPlus` 侧 `LdrLoadDll` 调用命中 `LoadFirstDll` 后，先恢复原入口并恢复页面保护，再校验 Payload，调用 LEP 初始化，最后转回原始 `LdrLoadDll`。
8. `LocaleEmulatorPlus` 从已校验的 Payload 读取静态配置；运行时获取的原始 locale、字体 charset 和 script name 保存在进程内的 `LEP_RUNTIME_STATE` 中。初始化完成后，目标进程调用 NLS、locale、窗口文字、字体、剪贴板、注册表、时区等相关 API 时，会被 LEP 的 hook 改写结果。

`LepCreateProcess` 路径（当前进程直接加载 `LocaleEmulatorPlus_*`）无法把 Payload 放在 PE 镜像尾部，因此使用 `TEB_ACTIVE_FRAME::Data` 传递指针。Payload 由进程堆分配：Loader 初始拥有它，LEP 接受后清空 frame 中的指针并负责释放；如果 LEP 没有接受，Loader 在返回前释放。

关键点是“早”：如果目标进程在 LEP 初始化前已经加载并初始化 kernel32/kernelbase，NLS 缓存已经按本机区域建立，后续再改 ACP/LCID 往往无效。因此 `LocaleEmulatorPlus.dll` 自身必须尽量只静态依赖 `ntdll.dll`；其他 DLL 只能 delay-load，且 delay helper 也不能依赖 kernel32。

具体 构建/Hook 工作详见[HOOK_INVENTORY.md](HOOK_INVENTORY.md)。

## LoaderDll 和 LocaleEmulatorPlus 的依赖约束

`LoaderDll` 可以依赖 kernel32，因为它运行在启动器/broker 进程里，负责创建和注入目标进程，不需要作为目标进程中的第一个用户 DLL。

`LocaleEmulatorPlus` 不应普通静态导入 kernel32/ucrt/vcruntime。原因：

- 普通 import 会让 Windows loader 在调用 `LocaleEmulatorPlus!DllMain` 前先加载依赖 DLL。
- 如果 kernel32/kernelbase 先加载，LEP 的“第一 DLL”假设会被破坏。

当前构建中：

- `LocaleEmulatorPlus_x86.dll` 普通 import 只应有 `ntdll.dll`；`KERNEL32.dll`/`USER32.dll`/`GDI32.dll`/`DBGHELP.dll` 是 delay-load。
- `LocaleEmulatorPlus_x64.dll` 普通 import 只应有 `ntdll.dll`；`USER32.dll`/`GDI32.dll`/`DBGHELP.dll` 是 delay-load。

## 构建

构建入口为 [`build.py`](build.py)，需要 Windows、Python 3.10+，只使用 Python 标准库。默认构建两个架构，也可以单独选择：

```powershell
python .\build.py
python .\build.py x86
python .\build.py x64
```

通过命令行指定本机工具链，无需修改脚本：

```powershell
python .\build.py x86 --vs-root D:\VisualStudio2026 --msvc-version 14.44.35207 --sdk-version 10.0.26100.0
```

| 参数 | 对应环境变量 | 默认值/行为 |
| --- | --- | --- |
| `--vs-root` | `VS_ROOT` | `D:\VisualStudio2026` |
| `--msvc-version` | `MSVC_VER` | `14.44.35207` |
| `--sdk-root` | `SDK_ROOT` | `C:\Program Files (x86)\Windows Kits\10` |
| `--sdk-version` | `SDK_VER` | `10.0.26100.0` |
| `--deploy-dir` | `LEP_DEPLOY_DIR` | `D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus` |

显式工具链和部署路径参数优先于环境变量。工具执行使用参数列表，不经过 shell 展开。

默认仅向已经存在的部署目录复制 DLL，目录不存在则保留 `out/` 产物并跳过复制。传入 `--no-deploy` 可直接禁用部署。日志默认开启，通过 `--no-log` 关闭：

```powershell
python .\build.py all --no-log --no-deploy
```

也可以使用快捷入口 `python .\build-nolog.py all --no-deploy`；它复用 `build.py`，强制关闭日志，其余参数和退出状态保持一致。省略架构时同样构建 x86/x64。

`ENABLE_LOG=1` 会启用 `WriteLog`。日志位于 LEP DLL 同目录下的 `Log` 子目录，普通运行日志形如 `<目标进程模块名>.<pid>.log.txt`，broker 日志记录跨架构注入阶段。日志会改变早期执行路径和时序，定位完问题后可关闭。

如果目标进程在日志建立前失败，可以用 `--diag-init` 启用初始化阶段弹框诊断：

```powershell
python .\build.py --diag-init --no-deploy
```

`--dry-run` 校验工具链并打印命令，不执行工具、不写文件，也不部署；`--verbose` 在真实构建时打印命令。运行 `python .\build.py --help` 查看全部参数。所有选定架构的工具链会先检查，任何工具失败立即返回非零状态，停止后续构建。

`LEP_DIAG_INIT=1` 会在 `LocaleEmulatorPlus` 初始化关键阶段弹出 `LEP modern init diag` 消息框，适合确认崩溃卡在哪一步；它会严重打断目标进程启动，只用于本机调试。

构建脚本使用：

- `cl.exe` / `lib.exe`：来自 `%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%`
- `link.exe`：来自 `dep\tools\link.exe`，这是打过补丁的 linker，下文会介绍
- SDK headers/libs：来自 `C:\Program Files (x86)\Windows Kits\10\Include|Lib\%SDK_VER%`

- 编译和生成 import lib 使用本机安装的 MSVC：`%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%`。
- 链接最终 DLL 使用 `dep\tools\link.exe`，因为它放宽了 delay-load 检查。
- 头文件和系统库仍依赖本机 Windows SDK/WDK 与 MSVC include/lib 目录。

其他开发者要编译时，推荐流程是：

1. 安装对应的 VS/MSVC、Windows SDK 和 WDK。
2. 复制自己本机 MSVC toolset 中与 `cl.exe` / `lib.exe` 配套的 linker 目录，例如
   `%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%\bin\Hostx64\x86`，作为新的 `dep\tools`
   基础。
3. 在这份本机工具链副本上 patch `link.exe`。 patch 方法详见下文。
4. 通过 `build.py` 的工具链参数或 `VS_ROOT`、`MSVC_VER`、`SDK_ROOT`、`SDK_VER` 环境变量指定自己的安装路径。

原则上 `cl.exe`、`lib.exe`、`link.exe` 应属于同一 MSVC toolset，版本应该一致。
当前仓库中的 `dep\tools` 只是演示用 patched linker 及其环境。不应混用不同版本的 compiler/linker。

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

代价也很明确：它破坏现代 W^X 习惯，安全性和可审计性不如 `.text` 只执行、`.rdata` 只读、`.data` 可写的普通布局。LNK4254 warning 正是 linker 在提示“把不同属性的 section 合并了”。当前 `build.py` 用 `/ignore:4254` 隐藏这个已知 warning；这只是不再输出噪声，不会改变 section 合并行为。

## 私有 import lib 和 delay-load helper

延迟加载辅助代码、CRT shim 和系统导入定义均位于 `dep/libs/`：

- `dep\libs\LepDelayLoad_x86_x64.cpp`
  - 自定义 delay-load helper。
  - 使用 `LdrLoadDll` 和 `LdrGetProcedureAddress`。
  - 避免依赖 kernel32。
- `dep\libs\LepCrtShim_x64.cpp`
  - x64 下极小的 runtime 兜底，只提供 `abort`、`terminate`、`calloc`、`free`。
  - `calloc`/`free` 直接走进程堆和 `RtlAllocateHeap`/`RtlFreeHeap`，避免引入普通 ucrt/vcruntime import。
- `dep\libs\lep_ntdll_x86.def`
  - x86：把 `_LdrInitializeThunk@8` 等 stdcall 修饰名映射到 ntdll 无修饰导出。
  - 同时导出 `_stricmp`、`_vscwprintf`、`_vsnprintf`、`_vsnwprintf`、`_wcsicmp` 等 ntdll 里的 CRT 风格字符串处理函数。
- `dep\libs\lep_ntdll_x64.def`
  - x64：导出 `LdrInitializeThunk`、`LdrRegisterDllNotification`、`LdrUnregisterDllNotification`。
  - 同时导出 `_vsnprintf`、`_vsnwprintf` 等 ntdll 里的 CRT 风格字符串处理函数。
- `dep\libs\lep_k32_x86.def`
  - x86：`CreateProcessInternalW=_CreateProcessInternalW@48`
- `dep\libs\lep_k32_x64.def`
  - x64：`CreateProcessInternalW`
注意不要在核心 DLL 里重新引入 `swprintf`、`_snwprintf` 这类高层 CRT printf 包装器。当前工具链可能把它们降成 `_vswprintf` 一类调用，但 ntdll 导出的 `_vswprintf` 语义/参数布局和 CRT 包装器预期不一致；确实需要格式化时优先使用已有的手写整数格式化，或直接调用已核对签名的 `_vsnwprintf` / `_vscwprintf`。

构建脚本每次会用当前工具链的 `lib.exe /def` 重新生成这些 import lib，避免复用旧二进制 lib 带来的兼容性问题。

## patched link.exe

构建使用仓库内的 patched MSVC linker：

```text
dep\tools\link.exe
```

作用：放宽 linker 对 delay-load DLL 的非法名单检查，让 `LocaleEmulatorPlus` 可以把特定系统 DLL 放进 delay import，而不是被 linker 拒绝或被迫变成普通 import。相关逻辑可在 IDA 导出的 `CheckInvalidDelayLoadDlls` / `FInvalidDelayLoadDll` 附近看到。

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
7. 重新运行 `python .\build.py`。
8. 用 `dep\tools\dumpbin.exe` 检查 `LocaleEmulatorPlus_*`：普通 imports 中不得出现 kernel32/ucrt/vcruntime。

## 更换/升级 VS/SDK/WDK 时的检查清单

1. 通过 `build.py` 命令行参数或 `VS_ROOT`、`MSVC_VER`、`SDK_ROOT`、`SDK_VER` 环境变量选择新工具链。
2. `SDK_VER` 应指向实际存在的 Windows Kits 版本。
3. 确认存在 `%VC_TOOLS%\include`、`%VC_TOOLS%\lib\x86`、`%VC_TOOLS%\lib\x64`、`%SDK_INC%\um`、`%SDK_INC%\shared`、`%SDK_INC%\km`。
4. `km` 目录通常来自 WDK。若找不到 WDK/内核头，确认 Visual Studio Installer 已安装 WDK/Windows Driver Kit 组件，且已安装与当前 Windows SDK 版本对应的 [WDK](https://learn.microsoft.com/zh-cn/windows-hardware/drivers/other-wdk-downloads)。
5. 更新 `dep\tools` 时要成套复制工具链文件，尤其是 linker 旁边的依赖 DLL、PDB 服务相关工具、资源目录等。
6. 重新生成 import lib，不要复用旧 `.lib`。构建脚本会自动生成 `out\libs\x86\*.lib` 和 `out\libs\x64\*.lib`。
7. 构建后检查 import table。
