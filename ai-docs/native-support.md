# 原生支持层维护

原 `LocaleEmulatorPlus/ml.h`（32,946 行）和 `ml.cpp`（14,015 行）已按职责拆到 `LocaleEmulatorPlus/support/`。声明通过 `Support.h` 汇总，24 个实现文件独立编译为 `LepSupport.lib`，分别静态链接进同架构的 Core 和 Loader DLL。

## 查找和修改代码

| 目录 | 内容 | 常用入口 |
| --- | --- | --- |
| `base/` | 平台宏、编译器适配、常量表达式 | `Macros.h`、`Constexpr.h` |
| `native/` | NT 类型和 API 声明、进程、内存、注册表、NLS、文件、加载器 | `Declarations.h`、`Process.h`、`ProcessSupport.cpp` |
| `runtime/` | 堆、字符串、容器、文件封装、诊断和旧辅助算法 | `Heap.h`、`String.cpp`、`File.cpp` |
| `pe/` | PE 类型、镜像映射和导出解析 | `ImageTypes.h`、`Image.cpp` |
| `patch/` | 指令长度解码、跳板和内存补丁 | `Decoder.cpp`、`MemoryPatch.cpp` |
| `search/` | 字节模式匹配 | `Sunday.h`、`Automaton.h`、`Pattern.h` |
| `generated/` | 按 10 个 DLL 分组的导出名哈希常量 | `ExportHashes.h`、`NTDLL.h` |

当前共有 57 个头文件、24 个实现文件，最大文件为 `native/Process.h`，2,691 行。所有 8,088 个导出名哈希常量保留原名和原值；可选的旧辅助功能也保留，尚未根据调用覆盖删除代码。

内部消费者包含 `support/Support.h`，支持层实现包含 `../Support.h`。`ml.h` 只保留旧路径的兼容转发；`ml.cpp` 已移除，Loader 不再包含实现文件。

`Support.h` 仍负责旧声明的包含顺序、编译条件和命名空间环境。子头文件是有序声明片段，部分容器声明在 `ml` 命名空间中包含，因此不能一律当成可单独包含的公共头文件。这一轮先建立可导航、可独立编译的实现边界，后续再逐个收窄头文件依赖。

第三方调用者继续使用 [`include/LocaleEmulatorPlus.ixx`](../include/LocaleEmulatorPlus.ixx)，无需依赖内部支持层。

## 构建约束

[`build.bat`](../build.bat) 从 `runtime/`、`native/`、`pe/`、`patch/` 编译 `.cpp`，对象名称包含子目录名，避免同名的 `File.cpp` 等互相覆盖。静态库使用本次编译的明确对象列表，删除源文件后不会把旧 `.obj` 带入新库。产物位于 `out/libs/<arch>/LepSupport.lib`，不会增加部署所需 DLL。

Core 和 Loader 使用相同架构、宏和编译选项构建支持库。Core 仍只能普通静态导入 `ntdll.dll`，保留现有 delay-load、CRT shim 和 patched linker 约束；不能为了拆分方便引入全局动态初始化或 TLS。

为支持独立编译，`NtFileDisk` 的两个简单构造函数和 `HANDLE` 转换操作的原函数体移入头文件，保证 `ForceInline` 定义对调用者可见。NT 基础类型的 `pack(push)` 与原 `pack(pop)` 收拢到 `native/Types.h`，保持原有 x86 四字节、x64 八字节对齐。

## 验证改动

在仓库根目录运行以下 PowerShell 命令。这里把部署目录指向不存在的位置，使构建产物只留在 `out/`。

```powershell
$env:LEP_DEPLOY_DIR = Join-Path $PWD 'out\no-deployment'
& .\build.bat all
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& .\tests\run-support-layout.ps1
& .\tests\run-lepb-compat.ps1
& .\tests\run-broker-handle.ps1
& .\tests\run-public-api.ps1
```

`run-support-layout.ps1` 对照拆分前头文件生成的 x86/x64 固定基线，各检查 26 项内部结构大小和偏移，包括 PEB、TEB、NLS、时区及容器布局。不要为了让测试通过而直接更新基线；需要有意修改布局时，可以用 `-BaselineHeader <对照头文件> -UpdateBaseline` 显式重建并审查差异。

其余测试分别覆盖 LEPB v2/v3 与子进程传播的 34 个场景、broker 句柄的 10 个场景，以及两个架构的公开声明布局和真实调用者。`build-nolog.bat all` 使用同一构建流程验证关闭日志的版本。

本轮重构在开启、关闭日志两种配置下均完成上述启动回归；并与相同日志配置的原版 DLL 对比，四个 DLL 的普通导入、延迟导入、导出名和序号均相同，均没有 TLS 目录。拆分不改变公开 ABI、注入算法及 broker 的已有句柄传递修复。自动测试不能替代具体游戏的人工兼容性验证。
