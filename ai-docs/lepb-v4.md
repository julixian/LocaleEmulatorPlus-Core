# 原生调用与时区配置

调用方在 LEPB v4 中填写 Windows 时区 ID，Loader 负责解析完整时区配置。需要控制程序看到的名称时，单独设置标准时间、夏令时名称覆盖。

独立声明位于 [LocaleEmulatorPlus.ixx](../include/LocaleEmulatorPlus.ixx)，完整启动示例位于 [lep-launch.cpp](../examples/lep-launch.cpp)。声明只依赖 Windows SDK，可作为 C++20 模块使用，也可复制其中的声明用于动态调用。

## 填写配置

```cpp
#include <Windows.h>
import LocaleEmulatorPlus;

LEP::LEPB environment{};
environment.Size = sizeof(environment);
environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
environment.AnsiCodePage = environment.OemCodePage = 932;
environment.LocaleID = 0x411;
environment.DefaultCharset = SHIFTJIS_CHARSET;
environment.RegistryRedirectionMode = 2;
environment.HookUILanguageMode = 1;
wcscpy_s(environment.TimeZoneId, L"Tokyo Standard Time");
```

通过 `LoadLibraryW` 加载对应架构的 Loader，以 `GetProcAddress` 获取 `LepCreateProcess2` 并转换为 `LEP::LepCreateProcess2_t`。该接口创建并配置目标进程。`LepCreateProcess` 使用相同签名，但会在调用进程中加载 Core，使其子进程继承配置。

两个接口均返回有符号 NTSTATUS；失败值小于零。进程信息输出参数统一使用 Windows 标准 `PROCESS_INFORMATION`：x86 为 16 字节，x64 为 24 字节，包含进程句柄、线程句柄、进程 ID 和线程 ID。成功后需要关闭返回的两个句柄。

## 时区 ID 和名称

`TimeZoneId` 必填，例如 `Tokyo Standard Time`、`Pacific Standard Time`、`AUS Eastern Standard Time`。它是 Windows 固定 ID，不是本地化显示名。.NET 可从 `TimeZoneInfo.GetSystemTimeZones()` 返回对象的 `Id` 获取候选值。

Loader 只读访问本机的 `SOFTWARE\Microsoft\Windows NT\CurrentVersion\Time Zones\<ID>`：

- 从 `TZI` 读取 `Bias`、`StandardBias`、`DaylightBias` 和两套切换日期，按字段转换 Win32 `SYSTEMTIME` 与 NT `TIME_FIELDS`。
- 从 `Std`、`Dlt` 读取默认返回名称。
- ID 为 `Tokyo Standard Time` 时，两个默认名称均为 `東京標準時`，ID 比较忽略大小写。
- 最后分别应用非空的 `StandardNameOverride`、`DaylightNameOverride`。覆盖名称优先于东京默认名称，不影响时差或切换日期。

例如，在东京规则下自定义两个名称：

```cpp
wcscpy_s(environment.StandardNameOverride, L"Custom standard time");
wcscpy_s(environment.DaylightNameOverride, L"Custom daylight time");
```

两个覆盖字段互相独立。空字符串表示使用默认名称。更换 ID 时，同一结构体中已填写的覆盖名称仍然有效，需要恢复默认时应清空相应字段。

解析得到的数据用于 Core 的 `SystemCurrentTimeZoneInformation` 查询结果，也供 `GetTimeZoneInformation` 等调用路径使用；不会修改 Windows 系统时区。此配置使用时区键的 `TZI` 规则。

## ABI 布局和输入校验

LEPB v4 不含指针，x86/x64 均为 416 字节。

| 字段 | 偏移 | 大小 |
| --- | ---: | ---: |
| Size、Version、代码页、区域和模式字段 | 0 | 32 字节 |
| WCHAR TimeZoneId[128] | 32 | 256 字节 |
| WCHAR StandardNameOverride[32] | 288 | 64 字节 |
| WCHAR DaylightNameOverride[32] | 352 | 64 字节 |

设置 `Version = 4`、`Size = sizeof(LEPB)`。所有字符串均为以 NUL 结束的 UTF-16 内联数组：ID 最多 127 个 WCHAR，覆盖名称各最多 31 个 WCHAR。ID 不允许为空或包含路径分隔符。模式字段合法范围均为 0–2。

Loader 不修改或保留调用方缓冲区；缓冲区可以只读，并可在调用返回后释放。长度和版本不匹配、字符串格式错误、模式超出范围时返回 `STATUS_INVALID_PARAMETER`（0xC000000D）。未知 ID 通常返回 `STATUS_OBJECT_NAME_NOT_FOUND`（0xC0000034）。注册表读取失败或时区值类型、长度不合法时返回对应 NTSTATUS。

## .NET 封送

指定 Unicode 字符集和 `ByValTStr`，将字符串内联封送；普通 `string` 字段会产生指针布局，不能直接使用。

```csharp
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
internal struct LEPB
{
    internal uint Size, Version;
    internal uint AnsiCodePage, OemCodePage, LocaleID, DefaultCharset;
    internal uint RegistryRedirectionMode, HookUILanguageMode;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string TimeZoneId;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] internal string StandardNameOverride;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] internal string DaylightNameOverride;
}
```

`Size` 取 `Marshal.SizeOf(typeof(LEPB))`，`Version` 设为 4，覆盖名称默认设为空字符串。GUI 的 `LoaderWrapper` 使用该布局，提供时区 ID 和两个名称覆盖属性。

## 兼容和进程传播

Loader 同时接受 v2/204、v3/460 输入，仅在入口转换为内部配置：

- v2 使用调用方提供的完整时区数据。
- v3 保留调用方提供的数值、日期及名称；非空 ID 必须存在。东京 ID 要求 `Bias = -540`、`StandardBias = 0`，并将两个名称设为 `東京標準時`。

公开 `.ixx` 只声明 v4。Core 不解析公开版本，也不在查询时重新处理名称。Loader 将解析结果保存为 `LEP_RUNTIME_ENVIRONMENT`，通过 bootstrap payload 传递；同架构子进程和跨架构 broker 始终继承该快照，不重新读取时区注册表。

Loader 与 Core 必须配套分发。跨架构传播需要两种架构的四个 DLL 位于同一运行目录。

## 验证

构建后，在仓库根目录运行：

```powershell
.\tests\run-public-api.ps1
.\tests\run-timezone.ps1
.\tests\run-lepb-compat.ps1
.\tests\run-broker-handle.ps1
```

这些脚本分别验证公开 ABI 与实际调用、v4 默认值及名称覆盖和夏令时规则、v2/v3 输入兼容，以及 broker 句柄传递。时区测试覆盖两个导出、只读保护页输入、两种架构和多代子进程传播。
