# SiglusEngine 转区启动时区弹窗分析

分析日期：2026-10-06。结论：游戏比较时区名称的文字前缀；截图返回的简体中文“东京标准时间”不符合日文“東京”或大写英文“TOKYO”的条件。

## 范围

用户授权检查 `G:\GalCharsStatistics\SAGA\#フローラル・フローラブ\SiglusEngine.exe`、同目录 `SiglusEngine.exe_export_for_ai` 和当前 LEP Core 源码，以解释启动弹窗。后续追踪了相邻 GUI 源码的时区配置构造，并通过已安装的 LoaderDll 启动独立探针验证 x86/x64 返回值。未运行游戏、修改游戏文件或系统时区。network_profile：offline。

目标 SHA256：`7fb849aa5258ad948fda1b2741376dc89f8be3b633f53170a6decf4465fb4494`；大小：8549376 字节。

## Evidence

### E-001：时区查询与名称检查

- source_ref：导出目录 `decompile/6002D0.c`、`600380.c`、`5117F0.c`、`517C10.c`、`600470.c`。
- content_hash：n/a，反编译结果交叉核对原始 PE 常量。
- repro_command：

```powershell
$exportRoot = 'G:\GalCharsStatistics\SAGA\#フローラル・フローラブ\SiglusEngine.exe_export_for_ai'
Get-Content -LiteralPath "$exportRoot\decompile\6002D0.c", "$exportRoot\decompile\600380.c", "$exportRoot\decompile\5117F0.c", "$exportRoot\decompile\600470.c"
```

`sub_6002D0` 调用 `GetTimeZoneInformation`，复制 `TIME_ZONE_INFORMATION.StandardName`。`sub_600380` 取前 2 个 UTF-16 字符与 `word_A3A114` 比较，失败后取前 5 个字符与 `L"TOKYO"` 比较。`sub_5117F0` 按字符精确比较，未进行大小写或简繁转换。

### E-002：原始 PE 常量

- source_ref：原始 `SiglusEngine.exe`，VA `0xA3A114`，文件偏移 `0x638714`。
- content_hash：上述目标 SHA256。
- raw_excerpt：`71 67 AC 4E 00 00`，UTF-16LE 解码为 `東京`；紧邻后面的常量为 `TOKYO`。
- repro_command：

```powershell
$targetPath = 'G:\GalCharsStatistics\SAGA\#フローラル・フローラブ\SiglusEngine.exe'
$targetBytes = [IO.File]::ReadAllBytes($targetPath)
[Text.Encoding]::Unicode.GetString($targetBytes, 0x638714, 4)
[Text.Encoding]::Unicode.GetString($targetBytes, 0x63871C, 10)
Get-FileHash -LiteralPath $targetPath -Algorithm SHA256
```

### E-003：截图与 Core 时区处理

- source_ref：用户附图；`LocaleEmulatorPlus/Hooks/NtdllHook.cpp:1175`；`LocaleEmulatorPlus/Hooks/Kernel32Hook.cpp`。
- content_hash：n/a。
- repro_command：

```powershell
Select-String -LiteralPath 'D:\VSProj\LocaleEmulatorPlus-Core\LocaleEmulatorPlus\Hooks\NtdllHook.cpp' -Pattern 'SystemCurrentTimeZoneInformation' -Context 0,15
```

截图显示当前名称为“东京标准时间”。Core 对 `SystemCurrentTimeZoneInformation` 直接返回配置中的 `Timezone`，包括 `StandardName`；当前 Kernel32 hook 未实现 `GetTimeZoneInformation` 的独立替换。静态证据不足以判断截图中的中文名称是配置传入，还是某条查询路径未被 hook 覆盖。

### E-004：启动器构造中文时区名称

- source_ref：`D:\VSProj\LocaleEmulatorPlus\LEPProc\LoaderWrapper.cs:137`，`LocaleEmulatorPlus/LocaleEmulatorPlus.h:467`，`LocaleEmulatorPlus/Hooks/NtdllHook.cpp:1182`。
- content_hash：n/a。
- repro_command：

```powershell
[TimeZoneInfo]::FindSystemTimeZoneById('Tokyo Standard Time') | Format-List Id,StandardName,DaylightName,BaseUtcOffset
Get-Content -LiteralPath 'D:\VSProj\LocaleEmulatorPlus\LEPProc\LoaderWrapper.cs' | Select-Object -Skip 125 -First 25
```

启动器使用 `TimeZoneInfo.FindSystemTimeZoneById(value)`，将 `tzi.StandardName` 写入 `LEPB.Timezone.StandardName` 和 `DaylightName`。本机 `Tokyo Standard Time` 的 `StandardName` 为“东京标准时间”。Loader 构造 Payload 时复制整个 Environment，Core 查询 hook 原样返回该字段。

### E-005：真实 Loader/Core 的 x86/x64 独立进程验证

- source_ref：`out/timezone-probe.cpp`、`out/timezone-x86-chinese.txt`、`out/timezone-x86-japanese.txt`、`out/timezone-x64-chinese.txt`、`out/timezone-x64-japanese.txt`。
- content_hash：n/a，生成的诊断产物。
- repro_command（探针已编译；源文件在 out 中，未纳入 Git）：

```powershell
Set-Location 'D:\VSProj\LocaleEmulatorPlus-Core'
foreach ($probeArch in @('x86','x64')) {
    foreach ($probeName in @('chinese','japanese')) {
        $probeOutput = Join-Path $PWD "out\timezone-$probeArch-$probeName.txt"
        & ".\out\timezone-probe-$probeArch.exe" "D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus\LoaderDll_$probeArch.dll" $probeName $probeOutput
        Get-Content -LiteralPath $probeOutput
    }
}
```

两种位数均得到：

```text
create status=00000000
child exit=0
result=0 ACP=932 Bias=-540 StandardName=东京标准时间 accepted=0
create status=00000000
child exit=0
result=0 ACP=932 Bias=-540 StandardName=東京標準時 accepted=1
```

探针通过 `LepCreateProcess2` 启动独立子进程，子进程调用 `GetTimeZoneInformation` 并按已还原的游戏条件比较。此验证证明已安装 Core 的时区 hook 可工作，中文名称来自传入配置；日文名称足以通过同样的时区条件。未验证整个游戏启动流程。

## Findings

### F-001：名称文字导致日本时区判定失败

- severity：n/a_re。
- evidence_ids：E-001、E-002、E-003。
- confidence：high。
- location：`sub_600380 @ 0x600380`。
- status：validated（静态条件、截图及 x86/x64 独立探针一致，未做游戏启动复测）。

该检查等价于：

```cpp
bool accepted = standardName.substr(0, 2) == L"東京"
             || standardName.substr(0, 5) == L"TOKYO";
```

“东京标准时间”的首字符为 `东`（U+4E1C），要求的是 `東`（U+6771）。两者不同，因此比较失败。此检查不读取 `Bias`；仅把偏移设为 UTC+9 无法保证通过。

截图中的时区错误分支位于 `sub_600470`：前面的 `sub_600210` 区域检查通过后，`sub_600380` 返回 false，程序重新读取时区名称，填入弹窗并返回失败。`sub_600210` 查询 `GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_ILANGUAGE, ...)`，要求前四个字符为 `0411`。

## Path

P-001，path_type=callflow：启动函数 `sub_5FE7D0` → `sub_600470` → `sub_600210` 区域检查 → `sub_600380` → `sub_6002D0` → `GetTimeZoneInformation().StandardName` → 与 `東京`/`TOKYO` 比较失败 → MessageBoxW → 启动失败。

## 初次修正方向与接口限制

转区进程返回的日本时区名称应使用 `東京標準時`，并让日本时区其余字段保持一致。E-004、E-005 已将初次静态分析中的来源疑问收敛到启动器：修正位置是 GUI 仓库 `LEPProc/LoaderWrapper.cs` 的 `Timezone` setter，按时区 ID `Tokyo Standard Time` 提供稳定的日文名称，避免直接继承宿主显示语言的 `tzi.StandardName`。其他时区继续采用其现有配置，偏移和日期字段保留原来的读取方式。

Core 没有时区 ID 字段，只有传入的时区数据；在 Core 里仅凭 LCID 或 -540 偏移把名称强行替换，可能覆盖用户选择的其他 UTC+9 时区。后续按用户要求，将处理移入 Core，采用明确的名称匹配加偏移检查。

## 初次 GUI 修复与 2.0.3.0 验证

按用户后续要求，已修改 GUI 仓库 `LEPProc/LoaderWrapper.cs`：仅在时区 ID 为 `Tokyo Standard Time` 时，将 StandardName、DaylightName 设置为 `東京標準時`；其他时区保留原逻辑。时差、日期字段不变。

六个项目的 AssemblyVersion、AssemblyFileVersion，以及 LEPVersion.xml、VersionInfo.xml 和两个 ClickOnce ApplicationVersion 字段均更新为 `2.0.3.0`。VersionInfo 日期为 `20261006`。完整 GUI Release 构建成功，输出在 `D:\VSProj\LocaleEmulatorPlus\Build\Release`。

### E-006：新构建启动器配置及真实子进程验证

- source_ref：`out/verify-timezone-fix.ps1`、`out/timezone-fixed-x86.txt`、`out/timezone-fixed-x64.txt`、`out/gui-2.0.3-build.log`。
- content_hash：n/a，生成的诊断产物。
- repro_command：

```powershell
Set-Location 'D:\VSProj\LocaleEmulatorPlus-Core'
& 'C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -ExecutionPolicy Bypass -File 'out\verify-timezone-fix.ps1' -Architecture x86
& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -ExecutionPolicy Bypass -File 'out\verify-timezone-fix.ps1' -Architecture x64
```

脚本通过反射调用新构建 LEPProc 中的真实 LoaderWrapper，检查日本默认名称、切换至韩国时区后的名称，再调用真实 Start 方法启动独立原生探针：

```text
x86 version=2.0.3.0 other-zone=preserved
result=0 ACP=932 Bias=-540 StandardName=東京標準時 accepted=1
x64 version=2.0.3.0 other-zone=preserved
result=0 ACP=932 Bias=-540 StandardName=東京標準時 accepted=1
```

F-002：severity=n/a_re，evidence_ids=E-006，confidence=high，location=GUI LoaderWrapper.Timezone，status=validated。修复后的真实启动器在两种位数下返回正确日文时区名称，其他时区未被改写。仍未做完整游戏启动复测。构建产物未覆盖已安装目录。

## 中间方案：DLL 根据本地化名称匹配（已替换）

2026-10-07 按用户要求撤回 GUI 的名称特殊处理，将修复放入 `LocaleEmulatorPlus/Hooks/NtdllHook.cpp` 的 `LepNtQuerySystemInformation`。复制时区配置到返回缓冲区后，调用 `LepNormalizeTokyoTimeZoneName`。

识别条件为 Bias=-540、StandardBias=0，并且 StandardName 精确匹配以下名称之一（英文字母比较忽略大小写）：`Tokyo Standard Time`、`东京标准时间`、`東京標準時間`、`東京標準時`。匹配后仅将返回的 StandardName 和 DaylightName 改为 `東京標準時`；配置 Payload、数值和转换日期不变。不能仅凭 UTC+9 推断时区，其他或未知名称保留原样。支持这些明确识别的名称，不声称覆盖所有 Windows 显示语言。

LEPB ABI 不变，第三方通过现有 LoaderDll 创建进程时也使用相同 Core hook，因此无需在调用侧实现这一名称兼容处理。

### E-007：DLL 直接调用验证及最终 GUI 回归

- source_ref：`out/timezone-probe.cpp`、`out/timezone-core-*-*.txt`、`out/core-timezone-build.log`、`out/gui-core-timezone-build.log`、`out/verify-timezone-fix.ps1`。
- content_hash：n/a，生成的诊断产物。
- repro_command：

```powershell
Set-Location 'D:\VSProj\LocaleEmulatorPlus-Core'
foreach ($probeArch in @('x86','x64')) {
    foreach ($probeName in @('chinese','traditional','english','uppercase','japanese','korean','custom','different-bias')) {
        $probeOutput = Join-Path $PWD "out\timezone-core-$probeArch-$probeName.txt"
        & ".\out\timezone-probe-$probeArch.exe" "$PWD\out\$probeArch\LoaderDll_$probeArch.dll" $probeName $probeOutput
        Get-Content -LiteralPath $probeOutput
    }
}
```

两种位数的五组东京名称均返回 StandardName=DaylightName=`東京標準時`，Bias=-540，ACP=932，accepted=1。韩国、自定义 UTC+9、名称为东京但 Bias=-480 的三组输入均保留原配置。总计 16 组真实子进程直接调用 DLL 验证通过。

原生 Core x86/x64 及完整 GUI Release 均构建成功。最终 GUI 验证脚本先确认 LoaderWrapper 原样保留宿主本地化名称，再由真实 Start 方法调用新 DLL；子进程仍返回日文名称并通过游戏条件。版本保留 2.0.3.0。未进行完整游戏启动复测，未覆盖现有安装目录。

F-003：severity=n/a_re，evidence_ids=E-007，confidence=high，location=LepNtQuerySystemInformation，status=validated。兼容逻辑集中到 LEP DLL，明确识别东京名称，不误改其他 UTC+9 配置。

## 最终方案：LEPB v3 的稳定时区 ID

2026-10-07 用户指出枚举宿主语言名称无法覆盖其他系统语言，并批准在原 LEPB 尾部增加可选时区 ID。最终实现撤掉名称枚举；LEPB v3 总大小为 460 字节，原 204 字节前缀不变，尾部为 WCHAR TimeZoneId[128]。旧 v2/204 调用继续支持。

GUI 仅填入 Windows 时区 ID 和原有时区数据，不实现名称修复。Core 只在 v3 ID 为 Tokyo Standard Time 时统一返回日文名称；其他 ID、空 ID 和 v2 配置保留原名称。Loader 只读校验非空 ID 的注册表键，拒绝格式错误或与东京标准偏移明显冲突的配置。完整 ABI、迁移和复现说明见 [lepb-v3.md](lepb-v3.md)。

E-008：source_ref=tests/lepb-compat-probe.cpp、tests/run-lepb-compat.ps1、out/lepb-compat-tests.log、out/core-lepb-v3-build.log、out/gui-lepb-v3-build.log；content_hash=n/a；repro_command=`.\tests\run-lepb-compat.ps1`，在 Core 仓库构建后运行。两种位数共 34 组测试通过，包括保护页上的 v2/v3 缓冲区、同位数和跨位数子进程传递、参数错误处理。额外验证已安装旧 DLL 明确拒绝 v3，以及 GUI 真实调用的 v3/460 封送和东京名称返回值。

F-004：severity=n/a_re，evidence_ids=E-008，confidence=high，location=LEPB v3 / LepCreateProcess2 / LepNtQuerySystemInformation，status=validated。按稳定 ID 修复名称，避免宿主语言名称枚举；v2 调用保持原 ABI，但无 ID 时不获得新的名称修复。

P-002，path_type=callflow：调用方 v3/460 + TimeZoneId → Loader 校验并按 Size 序列化 → Core 接受 v2/v3 Payload → 按 ID 返回日文东京名称 → 子进程及跨位数 broker 按 Size 保留完整配置。

## Timeline

1. 从导出导入表及反编译结果定位唯一 `GetTimeZoneInformation` 调用。
2. 追踪名称比较与弹窗分支，确认区域检查、时区检查的先后关系。
3. 从原始 PE 读取日文常量，确认是 `東京` 而非简体 `东京`。
4. 对照 Core 时区 hook，记录配置名称和 API 路径的待验证项。
5. 追踪 GUI `Timezone` setter，确认宿主本地化名称被写入 LEPB。
6. 通过已安装 Loader/Core 完成 x86/x64 中文、日文名称四组验证，确认无需增加 Core hook。
7. 按用户要求修复 GUI 时区名称，统一版本为 2.0.3.0，完成 Release 构建与真实 LoaderWrapper 的 x86/x64 回归验证。
8. 2026-10-07 按用户澄清调整文档目录：给 AI 的指令集中在 ai-docs/AGENTS.md，根目录 AGENTS.md 仅保留入口；维护说明和 hook 清单恢复原位，本报告保存在 docs/。
9. 2026-10-07 按用户要求将修复移入 LEP DLL，撤回 GUI 特例，完成两种位数 16 组直接调用验证及 GUI 回归。
10. 2026-10-07 扩展 LEPB v3/460，按可选时区 ID 修复名称并保留 v2/204 兼容；完成 34 组 ABI/子进程测试、旧 DLL 拒绝测试和 GUI 回归。
