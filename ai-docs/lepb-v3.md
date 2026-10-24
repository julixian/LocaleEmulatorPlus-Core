# 原生调用与 LEPB

Loader 提供 `LepCreateProcess`、`LepCreateProcess2` 两个进程启动接口，使用 NTSTATUS 返回执行结果。第三方声明位于 [`include/LocaleEmulatorPlus.ixx`](../include/LocaleEmulatorPlus.ixx)，完整示例位于 [`examples/lep-launch.cpp`](../examples/lep-launch.cpp)。

## 配置布局

LEPB v3 在 x86、x64 上均为 460 字节，不含指针。

| 字段 | 偏移 | 大小 |
| --- | ---: | ---: |
| Size、Version、代码页、区域和模式字段 | 0 | 32 字节 |
| RTL_TIME_ZONE_INFORMATION Timezone | 32 | 172 字节 |
| WCHAR TimeZoneId[128] | 204 | 256 字节 |

`TimeZoneId` 是以零结束的 UTF-16 数组，最多容纳 127 个 WCHAR 的 ID。Loader 接受的格式为：

| Version | Size | 行为 |
| ---: | ---: | --- |
| 3 | 460 | 读取完整配置和可选 TimeZoneId |
| 2 | 204 | 不读取 TimeZoneId，直接使用 Timezone 数据 |
| 其他组合 | 任意 | 返回 STATUS_INVALID_PARAMETER |

`Size` 与 `Version` 必须匹配，传入缓冲区必须至少覆盖 `Size` 字节。

## C++ 调用

独立声明只依赖 Windows SDK，包含配置结构、进程信息和两个 WINAPI 函数指针类型，无需 Core 私有头文件或导入库。以 C++20 模块形式使用：

```cpp
#include <Windows.h>
import LocaleEmulatorPlus;

LEP::LEPB environment{};
environment.Size = sizeof(environment);
environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
environment.AnsiCodePage = 932;
environment.OemCodePage = 932;
environment.LocaleID = 0x411;
environment.DefaultCharset = 128;
environment.RegistryRedirectionMode = 2;
environment.HookUILanguageMode = 1;
environment.Timezone.Bias = -540;
static const WCHAR id[] = L"Tokyo Standard Time";
CopyMemory(environment.TimeZoneId, id, sizeof(id));
```

将 `LocaleEmulatorPlus.ixx` 作为模块接口加入构建，通过 `LoadLibraryW` 加载对应架构的 Loader，然后使用 `GetProcAddress` 取得 `LepCreateProcess2` 并转换为 `LEP::LepCreateProcess2_t`。调用时检查 NTSTATUS，并关闭返回的进程、线程句柄。具体参数和完整错误处理参照启动示例。

需要普通头文件时，可复制 ixx 内容，移除 `module;` 和 `export module LocaleEmulatorPlus;`，将 `export namespace LEP` 改为 `namespace LEP`，加上 `#pragma once`。保留 includes、pack push/pop、类型声明和 static_assert；其中 inline constexpr 需要 C++17。

## 时区数据

`TimeZoneId` 使用 Windows 固定时区 ID，例如 `Tokyo Standard Time`，不是本地化的 DisplayName 或 StandardName。.NET 调用者可从 `TimeZoneInfo.GetSystemTimeZones()` 中取得对象的 `Id`。

ID 用于标识调用方提供的 `Timezone`，不会自动重建时差、转换日期或修改 Windows 系统时区。调用方负责填写 Bias、StandardBias、DaylightBias 及日期字段。

- 非空 ID 必须存在于本机 Windows 时区注册表，Loader 以只读方式校验。
- 未终止或包含路径分隔符的 ID 会被拒绝。
- `Tokyo Standard Time` 比较忽略大小写，要求 Bias 为 -540、StandardBias 为 0。
- 东京 ID 的查询结果中，StandardName、DaylightName 统一为 `東京標準時`，其他数值和日期使用传入配置。
- 空 ID、其他合法 ID 和 v2 配置直接使用传入名称。

格式错误返回 `STATUS_INVALID_PARAMETER`（0xC000000D）；不存在的 ID 通常返回 `STATUS_OBJECT_NAME_NOT_FOUND`（0xC0000034）。返回值是 NTSTATUS，不是 `GetLastError()`。

## .NET 封送

时区 ID 必须内联存储，不能使用封送为指针的普通字符串：

```csharp
[MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
internal byte[] TimeZoneId;
```

GUI 的 `LoaderWrapper` 创建 256 字节零数组，再复制 ID 的 UTF-16LE 内容；`Size` 取 `Marshal.SizeOf`，`Version` 设为 3。

## 分发与传播

Loader 和 Core DLL 必须配套分发。跨架构子进程注入需要 x86/x64 两组 DLL 位于同一运行目录。

内部 bootstrap payload 保存配置的相对偏移和长度。校验器核对实际环境长度与 LEPB.Size，复制完整配置；同架构传播及跨架构 broker 均传递时区 ID。

## 验证

构建 DLL 后在仓库根目录运行：

```powershell
.\tests\run-lepb-compat.ps1
.\tests\run-public-api.ps1
```

第一个脚本覆盖配置格式、保护页边界、参数错误、ID 校验和子进程传播。第二个脚本分别编译 x86/x64 公开模块，对照 Core 的结构大小和字段偏移，并运行只依赖公开声明的调用者。工具链可通过脚本的 `VcTools`、`SdkRoot`、`SdkVersion` 参数指定，测试产物位于 `out/`。
