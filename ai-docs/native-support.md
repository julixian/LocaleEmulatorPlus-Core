# 原生支持层维护

原 `ml.h`（32,946 行）和 `ml.cpp`（14,015 行）已拆到 `LocaleEmulatorPlus/support/`，并完成一轮未使用功能删除和依赖整理。支持层从第一轮的 81 个文件收敛到 56 个文件（41 个头文件、15 个实现文件），约 3.1 万行；最大文件仍为 `native/Process.h`，2,691 行。

实现独立编译为 `LepSupport.lib`，分别静态链接进同架构的 Core 和 Loader DLL。第三方继续使用 [`include/LocaleEmulatorPlus.ixx`](../include/LocaleEmulatorPlus.ixx)，公开导出和 LEPB 布局保持不变。

## 查找和修改代码

| 目录 | 内容 | 常用入口 |
| --- | --- | --- |
| `base/` | SDK/CRT 入口、平台宏、指针运算和字节序 | `Platform.h`、`Pointer.h` |
| `native/` | NT 类型和 API 声明、进程、内存、注册表、NLS、路径 | `Declarations.h`、`Helpers.h`、`Operations.h` |
| `runtime/` | 堆、字符串、容器、文件封装、诊断 | `Heap.h`、`String.h`、`File.h`、`Diagnostics.cpp` |
| `pe/` | PE 镜像、导入导出解析、导出名哈希 | `Image.h`、`Image.cpp`、`ExportHash.h` |
| `patch/` | 指令长度解码、指令遍历、跳板和内存补丁 | `Decoder.h`、`Decoder.cpp`、`MemoryPatch.cpp` |
| `search/` | 实际使用的 Sunday 和模式匹配 | `Sunday.h`、`Pattern.h` |

`Platform.h` 负责统一基础包含。容器及使用命名空间的原生封装头文件自己声明 `lep` 命名空间并包含所需依赖，调用者不应在命名空间内部包含它们。`Support.h` 是内部汇总入口；它仍为现有 Core 实现引入原生封装命名空间，便于逐步缩小旧调用处的依赖。`ml.h` 保留旧包含路径的薄转发。

主要可独立包含的入口由 [`tests/run-support-headers.ps1`](../tests/run-support-headers.ps1) 列出，共 22 个。`native/` 中其余底层 NT 声明片段仍由 `Declarations.h` 按依赖顺序组装，不承诺每个片段都能独立包含。

路径和注册表封装声明分别放在 `Path.h`、`RegistrySupport.h`，供字符串模块使用，避免它与 `Operations.h` 循环依赖。`NtHelpers.cpp` 实现旧 NT 辅助入口；`Path.cpp` 实现路径、环境和名称匹配。指令遍历及 `GetOpCodeSize*` 已从 PE/补丁接口归并到 `patch/Decoder.h`。

## 命名与所有权

内部命名空间已由 `ml` 改为 `lep`。内部 C++ 名称发生变化，不能把支持层类型当成第三方 ABI。

| 原名称 | 当前名称 |
| --- | --- |
| `MemoryAllocator` | `HeapAllocator` |
| `NtFileDisk` / `NtFileMemory` | `NativeFile` / `MemoryFile` |
| `GrowableArray` / `HashTableT` | `lep::DynamicArray` / `lep::HashTable` |
| `StringImplementT` | `lep::WideStringStorage` |
| `MlInitialize` / `MlUnInitialize` | `lep::InitializeSupportHeap` / `lep::UninitializeSupportHeap` |

堆生命周期实现与分配器归并在 `runtime/Heap.cpp`。控制台输出、崩溃转储、原生错误对话框和日志目录辅助函数归并在 `runtime/Diagnostics.*`。原 `Compatibility.cpp` 中多余的全局内存/资源转发入口已删除，内存调用直接使用原生命名空间的实现。

## 新增导出查找或 hook

使用 `lep::HashExportName("函数名")`，不再维护 `NTDLL_*`、`WIN32K_*` 等静态大表。例如：

```cpp
PVOID routine = LookupExportTable(module, lep::HashExportName("LdrLoadDll"));
```

哈希算法仍是逐字节左循环移位 13 位后异或，输入字节按无符号值处理。字符串字面量支持编译期计算，也可作为 `switch` 标签；运行期导出遍历共用同一实现。DLL 由查找参数确定，哈希只取决于导出名，原 hook 宏中的 DLL 前缀参数已移除。

替换时对旧表的全部 8,088 项分别执行了算法比较及 x86/x64 C++ `static_assert` 编译校验，结果一致。表文件已删除；布局测试保留关键函数名的固定哈希值回归。

## 已删除的旧功能

删除依据是仓库内调用和声明依赖检查，再通过两个架构的编译、真实进程启动测试和 DLL 契约对比验证。删除范围包括：

- 音频/WAVE、UCA 和图片/UCI 编解码辅助声明及封装。
- UCL 压缩解压、SHA-256 汇编和相关声明。
- AppCompat/SDB 数据库声明、LPC 通信类及 API 声明。
- Trie/Automaton 搜索、未使用的智能指针和虚函数回调容器。
- 未使用的线程 lambda、APC 和异步定时器封装、堆破坏诊断 hook。
- 旧目录遍历、CRT 命令行解析、时间戳伪随机数和 CPU 标志辅助函数。
- 没有调用者的旧远程 LoadLibrary 注入器、机器码及旧进程创建包装。
- 重复的 `Native::Str::UnicodeString` 类和无调用者的资源转发入口。

现用 Loader 的手工映射、`LdrLoadDll` bootstrap、跨架构 broker 和已创建进程句柄传递继续保留。也保留了字体 hook 实际使用的字节序转换、指令遍历和模式匹配。未根据单次运行覆盖结果删除其他兼容分支。

## 构建与验证

[`build.py`](../build.py) 从 `runtime/`、`native/`、`pe/`、`patch/` 编译 `.cpp`，对象名称包含子目录，静态库使用本次明确的对象列表，避免删除源文件后混入旧 `.obj`。输出为 `out/libs/<arch>/LepSupport.lib`，部署所需 DLL 数量不变。原支持层 `INCLUDE_LIB` pragma 和第三方库开关已移除，链接依赖由构建脚本指定。

构建入口使用 Python 3.10+ 标准库，按工具链配置、独立架构构建、编译/归档/链接/部署分开组织，使用参数列表直接运行工具。`all` 先检查两个架构的依赖，任何步骤失败立即返回非零状态。`--dry-run` 只打印命令，`--no-deploy` 直接禁用部署；工具链和额外编译参数支持命令行或原有环境变量配置，详见 [维护说明](../MAINTAINING.md)。

在仓库根目录运行以下 PowerShell 命令，构建产物会留在 `out/`。

```powershell
python .\build.py all --no-deploy
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& .\tests\run-support-headers.ps1
& .\tests\run-support-layout.ps1
& .\tests\run-lepb-compat.ps1
& .\tests\run-broker-handle.ps1
& .\tests\run-public-api.ps1
```

`run-support-layout.ps1` 对照拆分前测得的固定数值，各检查 x86/x64 的 26 项大小和偏移，包括 PEB、TEB、NLS、时区、文件及容器布局。类型改名只同步基线中的名称，未改变其数值；布局有意变化时应人工审查基线差异。

其余测试覆盖 34 个 LEPB/子进程场景、10 个 broker 句柄场景、两个架构的公开声明布局和实际第三方调用。关闭日志时使用 `python .\build.py all --no-log --no-deploy`。构建脚本的参数、失败传播、部署和预览行为通过 `python -m unittest discover -s tests -p test_build.py` 验证。Core 仍只能普通静态导入 `ntdll.dll`，其他依赖保留 delay-load；不得因整理头文件引入全局动态初始化或 TLS。

`build-nolog.py` 是强制追加 `--no-log` 的快捷入口，其余参数、构建逻辑和退出状态复用 `build.py`，例如 `python .\build-nolog.py all --no-deploy`。

本轮对开启、关闭日志两种配置分别构建并运行启动回归，与对应原版产物比较普通导入、延迟导入、导出名/序号及 TLS 目录。自动测试不能替代具体游戏的人工兼容性验证。
