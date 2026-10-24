# 原生支持层

`LocaleEmulatorPlus/support/` 为 Core 和 Loader 提供 NT API、内存、容器、文件、PE 和指令补丁支持。实现编译为 `LepSupport.lib`，分别静态链接进两个 DLL。

第三方调用使用 [`include/LocaleEmulatorPlus.ixx`](../include/LocaleEmulatorPlus.ixx)。支持层属于内部实现，不作为公开 ABI。

## 模块职责

| 目录 | 职责 | 入口 |
| --- | --- | --- |
| `base/` | 平台宏、SDK/CRT 包含、指针运算和字节序 | `Platform.h`、`Pointer.h` |
| `native/` | NT 类型和 API、进程、内存、注册表、NLS、路径 | `Declarations.h`、`Helpers.h`、`Operations.h` |
| `runtime/` | 堆、字符串、容器、文件和诊断 | `Heap.h`、`String.h`、`File.h`、`Diagnostics.h` |
| `pe/` | PE 镜像、导入导出解析、导出名哈希 | `Image.h`、`ExportHash.h` |
| `patch/` | 指令长度、指令遍历、跳板和内存补丁 | `Decoder.h`、`MemoryPatch.h` |
| `search/` | Sunday 搜索和字节模式匹配 | `Sunday.h`、`Pattern.h` |

`Support.h` 是汇总入口，提供完整的声明上下文。容器和使用命名空间的封装头文件自行声明命名空间并包含依赖，不能在外层命名空间内包含它们。底层 NT 声明片段由 `native/Declarations.h` 按顺序组装。

`native/Path.h` 和 `native/RegistrySupport.h` 分别提供路径、注册表封装声明，供字符串模块使用。`native/NtHelpers.cpp` 实现 NT 辅助入口，`native/Path.cpp` 实现路径、环境和名称匹配。指令遍历和 `GetOpCodeSize*` 位于 `patch/Decoder.h`。

## 类型与内存所有权

| 类型或接口 | 用途 |
| --- | --- |
| `HeapAllocator` | 管理共享支持堆和私有堆分配 |
| `NativeFile` | NT 文件句柄、读写、目录和文件信息 |
| `MemoryFile` | 内存缓冲区中的文件内容 |
| `lep::DynamicArray` | 动态数组 |
| `lep::HashTable` | 键值缓存 |
| `lep::String`、`lep::WideStringStorage` | 宽字符串及其存储 |
| `lep::InitializeSupportHeap`、`lep::UninitializeSupportHeap` | 支持堆的生命周期 |

堆生命周期和分配实现位于 `runtime/Heap.cpp`。控制台输出、崩溃转储、原生错误对话框和日志目录辅助函数位于 `runtime/Diagnostics.*`。原生内存操作使用 `lep::Native::Mm`。

## 导出查找与 hook

使用 `lep::HashExportName` 计算导出名哈希：

```cpp
PVOID routine = LookupExportTable(module, lep::HashExportName("LdrLoadDll"));
```

算法对每个无符号字节执行 32 位左循环移位 13 位后异或。字面量支持编译期计算，可用于常量初始化和 `switch` 标签；运行期导出遍历使用同一实现。模块由查找参数指定，哈希只取决于函数名。

Hook 入口和具体改写行为见 [HOOK_INVENTORY.md](../HOOK_INVENTORY.md)。

## 构建约束

[`build.py`](../build.py) 编译 `runtime/`、`native/`、`pe/`、`patch/` 中的 `.cpp`，对象名称包含子目录，并使用本次生成的明确对象列表归档。静态库输出位于 `out/libs/<arch>/LepSupport.lib`，最终 DLL 输出位于 `out/<arch>/`。

链接依赖由构建脚本指定。Core 只能普通静态导入 `ntdll.dll`，其他系统依赖使用 delay-load。支持层不能引入需要常规 CRT 启动的全局动态初始化或 TLS，以免破坏 Core 的早期注入流程。工具链和部署参数见 [MAINTAINING.md](../MAINTAINING.md)。

```powershell
python .\build.py all --no-deploy
python .\build-nolog.py all --no-deploy
python .\build.py --diag-init --no-deploy
```

日志由 `--no-log` 控制，初始化弹框由 `--diag-init` 控制。`build-nolog.py` 默认禁用日志，其余行为与 `build.py` 相同。

## 验证

在仓库根目录运行：

```powershell
python -m unittest discover -s tests -p test_build.py
.\tests\run-support-headers.ps1
.\tests\run-support-layout.ps1
.\tests\run-lepb-compat.ps1
.\tests\run-broker-handle.ps1
.\tests\run-public-api.ps1
```

- `run-support-headers.ps1` 检查 22 个入口头文件能独立、重复包含。
- `run-support-layout.ps1` 检查 x86/x64 各 26 项大小和偏移，以及固定导出哈希值。布局有意变化时应人工审查基线。
- `run-lepb-compat.ps1` 覆盖 LEPB 格式、参数校验及同架构、跨架构子进程传播。
- `run-broker-handle.ps1` 检查 broker 句柄传递、权限和 PID 校验。
- `run-public-api.ps1` 对照公开声明与 Core 布局，并运行第三方调用示例。
