# LEPB v3：可选 Windows 时区 ID

`LepCreateProcess`、`LepCreateProcess2` 的名称和函数参数保持不变。LEPB 在原布局尾部追加时区 ID，兼容旧 v2 调用方。

## ABI

| 字段 | 偏移 | 大小 |
|---|---:|---:|
| Size、Version、代码页、区域和模式字段 | 0 | 32 字节 |
| RTL_TIME_ZONE_INFORMATION Timezone | 32 | 172 字节 |
| WCHAR TimeZoneId[128]（v3 新增） | 204 | 256 字节 |

x86、x64 均使用相同布局，不含指针。UTF-16 ID 最长 127 个 WCHAR，必须以零结束。

| Version | Size | 行为 |
|---:|---:|---|
| 2 | 204 | 接受旧配置，不读取尾部 ID |
| 3 | 460 | 接受扩展配置，ID 为空时保留原名称 |
| 其他组合 | 任意 | STATUS_INVALID_PARAMETER |

现有前 204 字节的字段、偏移和含义保持不变。不要将 v2 的 Version 与 v3 的 Size 混用。

## 调用方设置

维护的独立声明在 [include/LocaleEmulatorPlus.ixx](../include/LocaleEmulatorPlus.ixx)，只包含当前版本的 TIME_FIELDS、RTL_TIME_ZONE_INFORMATION、LEPB v3、ML_PROCESS_INFORMATION 和两个导出函数的 WINAPI 函数指针类型。它只依赖 Windows SDK，无需 Core 私有头文件或 import lib。

C++20 项目将该文件作为模块接口加入构建后使用：

```cpp
#include <Windows.h>
import LocaleEmulatorPlus;

LEP::LEPB environment{};
environment.Size = sizeof(environment);
environment.Version = LEP::LEP_ENVIRONMENT_VERSION;
// 其余字段按下述约定填写。
auto create = reinterpret_cast<LEP::LepCreateProcess2_t>(
    GetProcAddress(loader, "LepCreateProcess2")); // loader 为已加载的 HMODULE
```

不使用 C++20 模块时，可将 ixx 内容复制为普通头文件：移除 `module;` 和 `export module LocaleEmulatorPlus;`，将 `export namespace LEP` 改为 `namespace LEP`，加上 `#pragma once`；保留 includes、pack push/pop、类型声明和 static_assert。其中 inline constexpr 需要 C++17。

完整调用示例见 [examples/lep-launch.cpp](../examples/lep-launch.cpp)。示例设置目标 EXE 所在目录为工作目录，检查 NTSTATUS 并关闭返回的进程/线程句柄。公开声明不再提供旧版本类型；DLL 的 v2 兼容仍保留。

使用当前原生头文件中的 LEPB 类型，初始化方式如下；省略的既有参数仍按应用原来的启动逻辑设置：

```cpp
LEPB environment = {};
environment.Size = sizeof(environment); // 460
environment.Version = LEP_ENVIRONMENT_VERSION; // 3
environment.AnsiCodePage = 932;
environment.OemCodePage = 932;
environment.LocaleID = 0x411;
environment.DefaultCharset = 128;
environment.RegistryRedirectionMode = 2;
environment.HookUILanguageMode = 1;
environment.Timezone.Bias = -540;
static const WCHAR id[] = L"Tokyo Standard Time";
CopyMemory(environment.TimeZoneId, id, sizeof(id));
// 仍通过原有 LepCreateProcess2 的首个参数传入 &environment。
```

`TimeZoneId` 是 Windows 时区的固定 ID，不是本地化的 DisplayName 或 StandardName。可以从 `TimeZoneInfo.GetSystemTimeZones()` 中获得所选对象的 `Id`。

C# 对应的新增字段使用内联 256 字节数组；不能使用会被封送为指针的普通字符串：

```csharp
[MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
internal byte[] TimeZoneId;
```

GUI 的 LoaderWrapper 先将字段初始化为 256 个零字节，再复制 ID 的 UTF-16LE 内容；Size 取 `Marshal.SizeOf`，Version 设为 3。

## ID 的含义和校验

ID 用于稳定标识调用方提供的 `Timezone`；它不会自动重建时差或转换日期，也不修改 Windows 系统时区。调用方仍负责提供对应时区的数值数据。

- v3 非空 ID 必须在本机 Windows 时区注册表中存在。Loader 在公共 API 边界以只读方式校验。
- 不接受未终止的 ID、带路径分隔符的 ID 或不匹配的版本/大小。
- `Tokyo Standard Time` 比较忽略大小写。其 Bias 必须为 -540、StandardBias 为 0，否则返回参数错误。
- Core 查询返回值中的 StandardName、DaylightName 统一为 `東京標準時`；不依赖输入名称属于哪一种语言。其他数值与日期保留原配置。
- v2、v3 空 ID、其他合法 ID 保留传入名称。已撤掉枚举本地化东京名称的处理。

典型错误：结构、终止符或东京偏移冲突返回 `STATUS_INVALID_PARAMETER`（0xC000000D）；不存在的 ID 通常返回注册表 `STATUS_OBJECT_NAME_NOT_FOUND`（0xC0000034）。公共 API 返回 NTSTATUS，不是 Win32 GetLastError 值。

## 兼容与分发

旧调用方向新 DLL 传入原来的 v2/204 配置即可，无需改动；因为没有提供 ID，旧调用不会自动获得基于 ID 的东京名称修复。

新 v3 调用方向旧 DLL 传参，会被旧的版本/大小校验拒绝。当前 GUI 要求配套的新 DLL，不会自动退回 v2。需要支持旧 DLL 的第三方可自行选择在明确的兼容性错误后使用旧结构；退回 v2 会失去 ID 功能。

应配套分发 LoaderDll 和 LocaleEmulatorPlus DLL。跨位数子进程注入时，x86/x64 两组 DLL 要位于同一运行目录。

内部 Bootstrap Payload 的头部格式及版本保持不变；EnvironmentSize 从固定值改为已校验的 LEPB.Size。校验器同时核对 Payload 的实际环境长度与 LEPB.Size，复制时保留整个 v3 扩展。相同位数和跨位数 broker 路径均保留 ID。

## 验证

构建原生 DLL 后执行：

```powershell
Set-Location 'D:\VSProj\LocaleEmulatorPlus-Core'
.\tests\run-lepb-compat.ps1
```

脚本默认使用仓库 build.bat 对应的 VS/SDK，可通过 VcTools、SdkRoot、SdkVersion 参数调整。产物和临时运行目录在 out/，不会覆盖已安装目录。

2026-10-07：两种位数共 34 组测试通过，包括 v2/v3、空 ID、其他 UTC+9 ID、ID 大小写、无效参数和注册表 ID、同位数及跨位数子进程。输入缓冲区放在 PAGE_NOACCESS 页前，验证不读取或复制超出传入大小的数据。完整 GUI Release 构建以及真实 LoaderWrapper 的 v3 封送、启动和返回值测试通过。产品版本仍为 2.0.3.0。

本次未复测完整游戏启动。

## 声明维护验证

修改公开声明或 Core ABI 后运行：

```powershell
.\tests\run-public-api.ps1
```

该脚本分别编译 x86/x64 模块，逐项对照 Core 真实头文件中的版本、结构大小和字段偏移，并运行只使用公开声明的第三方启动示例，验证代码页与时区返回值。2026-10-07 两种位数均通过。
