# QQ 飞车实际进程输入诊断交接

目标：在 issue #3 的英语宿主、LEP 简中配置下，定位文字在哪一步丢失。先收集实际游戏证据，再决定修复位置。当前阶段没有改写 USER32 的 DBCS 标志，也没有新增 API Hook。

接手 agent：先读本文的“快速开始”和“结果判读”。现有修复基线为 `46007213cb73722600716247ea6d5c469f64bcb6`；本诊断增量由交接包的 `changes.patch` 提供，具体二进制指纹在 `manifest.json`。QQ 飞车实际进程的结果尚待收集，不能把下面的本机实验当作游戏确诊。

如果已经从 Git 同步了包含本文的诊断提交，代码已经齐全，直接构建和自检即可；不要再重复应用旧交接包的补丁。`changes.patch` 用于旧基线仓库与独立 ZIP 的交接。

## 快速开始

### 1. 准备诊断版和明确实际游戏 PID

交接 ZIP 中 `bin/` 有匹配的四个诊断 DLL，以及双架构自检程序。把四个 DLL 放到专用 LEP 测试安装目录，沿用正常 UI/启动配置启动游戏；先保留该目录原 DLL。所有 Launcher、broker 和实际游戏都使用这一组版本，避免只替换一个 Core。

配置沿用 [issue #3](https://github.com/julixian/LocaleEmulatorPlus/issues/3)：英语显示语言、非 Unicode 程序语言为英语、UTF-8 Beta 关闭；LEP `zh-CN`、中国时区、注册表模式 2、UI 语言模式 2。记录实际 Windows build、游戏版本、输入法名称和两项系统语言设置。

从正常、未转区的 64 位 PowerShell 观察进程。先启动游戏并完成登录，找到拥有聊天/输入窗口的实际游戏 PID，不能用登录器 PID 代替。可先列出候选：

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'QQSpeed|Speed|TCLS|WeGame' } |
    Select-Object Id, ProcessName, Path
```

下面的脚本既可从仓库根目录运行，也可从解压后的交接包根目录运行。按实际环境设置三个变量，之后保持同一个 PowerShell 会话：

```powershell
$gameId = [int](Read-Host '实际游戏 PID')
$dllRoot = (Resolve-Path -LiteralPath (Read-Host '四个诊断 DLL 所在目录')).Path
$capture = Join-Path (Get-Location) ('out\qqspeed-capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\collect-qqspeed-input.ps1 -ProcessId $gameId -DllDirectory $dllRoot -OutputDirectory $capture -CaseLabel before
```

`collect-qqspeed-input.ps1` 只观察窗口、进程、选定模块和 DLL 指纹，并复制该 PID 的现有日志；不注入进程，不读取窗口标题或剪贴板。进程权限不足时模块清单可能不完整，要保留 `ModuleReadError`。

### 2. 核实进程内状态，再开启输入采样

诊断日志在 DLL 目录的 `Log/` 下，文件名为 `进程名.PID十六进制.log.txt`。PID 开关文件使用十进制，两者故意不同。

先找实际游戏日志里的：

```text
init complete pid=...
observed ntdllAcp=936 nlsApply=1
inputdiag-ready version=1 ... targetAcp=936 observedAcp=936 ntdllAcp=936 nlsDbcs=1 smDbcs=... checkDbcs=... arm=...
```

`inputdiag-ready` 在首次成功创建窗口后输出一次，附该线程 HKL、窗口类名和完整开关路径。它不一定对应聊天窗口；后续 `event=message` 才能关联实际输入窗口。游戏的 Core 可能是手动映射的，普通模块列表里没有 LEP 不等于注入失败；以实际 PID 的初始化日志为依据。没有 ready 行时，检查 DLL 指纹、构建开关、实际 PID、窗口创建 Hook 和日志是否可写，不要直接认定没有输入。

完成登录并聚焦非敏感测试输入框后开启：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\set-input-trace.ps1 -ProcessId $gameId -DllDirectory $dllRoot
```

开启后最多记录 2,048 个事件；达到上限需要重启进程。开关只按 PID 控制当前进程。字符日志包含字符码和转换字节，因此只在下述固定测试阶段开启；不要在账号或密码输入阶段开启。

### 3. 执行最小测试矩阵

每次只测一种输入方式，清空测试框后再换下一项。只使用 `A1`、`中`、`中A1` 三个测试串。聊天框优先；另一个非敏感输入框可作对照，不要假定两个框使用相同实现。

| Case | 操作 | 必须记录 |
|---|---|---|
| ascii-type | 英文模式键入 `A1` | 字符是否出现；焦点窗口 |
| pinyin-commit | 微软拼音输入并选定“中” | 候选窗是否出现；选词后是否显示、乱码或完全无字 |
| mixed-type | 输入 `中A1` | 丢的是汉字、后续 ASCII，还是全部 |
| ascii-paste | 从记事本手动复制 `A1`，Ctrl+V | 是否出现；是否命中剪贴板日志 |
| chinese-paste | 手动复制 `中A1`，Ctrl+V | 插入结果；读取格式及句柄结果 |
| paste-menu | 仅当游戏提供粘贴菜单时，使用菜单粘贴同串 | 与 Ctrl+V 是否不同 |

不要由采集代码自动清空、替换、备份或恢复用户剪贴板。人手复制测试串即可。Ctrl+V 也不一定产生 `WM_PASTE`，游戏可能直接处理按键并调用剪贴板 API。

每项完成后记录时间和肉眼结果，并采集一个带 case 名的快照。例如第一项：

```powershell
[pscustomobject]@{case='ascii-type';utc=[DateTime]::UtcNow.ToString('o');sample='A1';observed=(Read-Host '实际现象')} |
    ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $capture 'observations.jsonl') -Encoding UTF8
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\collect-qqspeed-input.ps1 -ProcessId $gameId -DllDirectory $dllRoot -OutputDirectory $capture -CaseLabel ascii-type
```

其它项替换 case、sample 和观察结果。快照不切换游戏焦点，也不主动向游戏发送字符。切回 PowerShell可能改变焦点，观察后要重新聚焦游戏。窗口快照里的 `focusedWindow` 是采集时状态，不保证等于刚才按键时状态。

### 4. 停止并保存

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\set-input-trace.ps1 -ProcessId $gameId -DllDirectory $dllRoot -Disable
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\collect-qqspeed-input.ps1 -ProcessId $gameId -DllDirectory $dllRoot -OutputDirectory $capture -CaseLabel after
```

退出游戏后再保存一次完整 PID 日志。运行中的日志复制只是时间点快照，最后一行可能尚未完成。若进程已经退出，直接复制对应 `Log` 文件；采集脚本需要活进程才能取得窗口信息。

## 日志覆盖范围与结果判读

所有观测位于现有 Hook 中，日志代码保存并恢复 Win32 LastError，并有线程重入保护。没有改变真实消息队列、剪贴板数据、IME 组合状态或 USER32 私有标志。

| 字段或事件 | 能说明什么 | 不能直接说明什么 |
|---|---|---|
| `targetAcp/observedAcp/ntdllAcp/nlsDbcs` | 当前被采样游戏进程的配置和部分有效 NLS 状态 | 不表示 USER32 已进入目标 DBCS 模式 |
| `smDbcs/checkDbcs` | 游戏进程内的 USER32 DBCS 查询结果 | 值为 0 还不等于游戏必然因此失败 |
| `event=peek-native` | 原 `PeekMessageA` 返回的输入相关消息，在包装层转换前记录 | 不覆盖 GetMessageA/W、PeekMessageW 或游戏自己的队列 |
| `event=message` | 输入相关消息已经进入 LEP 的包装窗口过程 | 不覆盖未包装的 Unicode 窗口、TSF 或全部输入路径 |
| `event=convert` | Unicode 字符转换后的目标 ACP 字节 | 不保证系统 EDIT 后续接受该字符 |
| `event=peek-trail` | 临时尾字节匹配成功；`caller` 可帮助定位消费方 | 不代表真实队列中的所有字符都正确 |
| `event=edit-trail consumed=0/1` | 首字节回调返回时，临时尾字节是否被匹配的 PM_REMOVE 消费 | 未消费有多种原因，不能单凭它认定 gpsi 分支 |
| `event=clipboard` | 请求格式、Unicode 来源、CF_TEXT 回写及返回句柄 | 不记录正文，不直接证明返回字节正确 |

消息编号：`WM_CHAR=0102`、`WM_IME_STARTCOMPOSITION=010D`、`WM_IME_ENDCOMPOSITION=010E`、`WM_IME_COMPOSITION=010F`、`WM_IME_CHAR=0286`、`WM_PASTE=0302`。`GCS_RESULTSTR=0800` 是组合消息 lParam 中的标志。剪贴板 `CF_TEXT=1`、`CF_UNICODETEXT=13`。

只有 API 返回失败时 LastError 才可能有诊断意义，成功时可能是旧值。`text-publish handle=0` 是 CF_TEXT 回写失败的证据；不能只看非零的旧 LastError 就宣称失败。

外部 JSON 的 `HostObserver` 来自观察 PowerShell，不是游戏进程。外部 `nativeUnicode=true` 也不能单独证明应用回调是 Unicode：LEP 包装的 ANSI 回调会让原生窗口标志变为 Unicode，`trackedA` 才说明是否保存了原 ANSI 过程。JSON 的 `parent` 表示枚举的顶层根窗口，不是直接父窗口。

优先顺序：

1. 没有实际游戏 PID 的成功初始化日志：先定位启动链和配置传播，不继续修改字符转换。
2. ACP 936、NLS DBCS 为 1，但 `smDbcs=0`：确认宿主模式依赖；结合窗口类、消息和 consumed 判断它是否处于实际失败路径。
3. `peek-native` 已返回 `D0D6`，而游戏没有继续 dispatch 到包装过程：调查游戏直接消费 ANSI 队列的逻辑。
4. 有正确 `convert ... unicode=4E2D hex=D6 D0`，但 EDIT 无文字且 consumed=0：检查原 EDIT 的 DBCS 分支和子类处理。不要直接把缓存置位实验当作生产补丁。
5. `WM_IME_COMPOSITION` 到达，但缺少字符提交：进入下一节，确认是否由游戏直接读取组合串。没有 WM_CHAR 本身也可能是正常设计。
6. 粘贴命中剪贴板 Hook：检查格式、来源句柄、回写和最终句柄；成功句柄还需要确认游戏实际读到的字节以及后续过滤。

## 第二阶段：正常调试条件下观察 IME 和返回字节

第一阶段没有新增 IMM32 Hook。若日志仍不能区分原因，可在实际游戏完成登录后，用匹配架构的 WinDbg/CDB 正常附加，生成入口观察命令：

```powershell
$architecture = Read-Host '实际游戏架构，x86 或 x64'
$commands = Join-Path $capture ('breakpoints-' + $architecture + '.txt')
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\write-input-breakpoints.ps1 -Architecture $architecture -OutputFile $commands
```

在调试器里执行生成文件：使用 `$$><` 后跟文件实际绝对路径。先 `bl` 保存原有断点列表；仅删除本轮添加的断点。命令文件只记录入口参数和短调用栈并继续运行，不会自动保存返回字节。系统 DLL 的标准导出即可定位入口，Core 可以没有 PDB。

观察的函数是 `ImmGetCompositionStringA/W`、`GetClipboardData`、`GetSystemMetrics(SM_DBCSENABLED)` 和 `CheckDBCSEnabledExt`。诊断代码本身也会查询 DBCS 状态，要从栈中排除这些调用，辨认游戏或其输入模块的调用。若正常调试无法附加，记录限制并继续使用第一阶段证据，不改动游戏保护组件。

要取某一次 IME 返回值，暂时停用自动继续的入口断点，改为普通入口断点。只选 `index=0800` 且输出缓冲非空的读取调用；先用 `kn` 确认调用方属于游戏。单次观察时避免其它线程的入口命中覆盖调试器临时寄存器。

| 项目 | x86 | x64 |
|---|---|---|
| HIMC / index | `[esp+4]` / `[esp+8]` | `rcx` / `rdx` |
| 输出指针 / 容量 | `[esp+0C]` / `[esp+10]` | `r8` / `r9` |
| 返回值 | `eax`，有符号 LONG | `eax`，有符号 LONG |

保存输出指针和容量后再执行 `gu` 到返回处。例如 x86 的保存命令是 `r @$t0=poi(@esp+0xc)`、`r @$t1=poi(@esp+0x10)`；x64 是 `r @$t0=@r8`、`r @$t1=@r9`。返回值为负是错误，0 是空或所请求项没有数据。只读取实际返回字节，且不超过调用容量和 16 字节。例如返回 4 且容量至少 4 时用 `db @$t0 L4`。不要假定字符串有 NUL 结尾。

对于测试串 `中A1`，CP936 字节应为 `D6 D0 41 31`；UTF-16LE 应为 `2D 4E 41 00 31 00`。A/W 的长度都是字节数，不能把 W 返回长度当字符数。一次 size-query 返回长度并不证明后面的实际读取正确。

剪贴板 `GetClipboardData` 返回的是 HANDLE，不是字符串指针。要看正文，在游戏自己调用 `GlobalLock` 返回后观察其缓冲，或在游戏确实把数据视为指针的位置验证；不能直接对返回 HANDLE 执行 `db`。同时确认它把内容按哪种编码解析。

## 在另一台电脑重建代码

交接包包含三个被修改的源文件、新增诊断头和测试脚本的统一补丁。先在匹配基线、无冲突的仓库执行：

若 manifest 的 `patchRequired` 为 false，基线本身已包含代码，可跳过两条 `git apply` 命令。

从 Git 取得完整诊断提交时，同样直接跳过两条 `git apply` 命令；仓库中无需存在 `changes.patch`。

```powershell
git apply --check changes.patch
git apply changes.patch
python build.py all --diag-input --no-deploy
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-input-diagnostics.ps1
```

若编译器路径不同，使用 `build.py --vs-root --msvc-version --sdk-root --sdk-version` 和自检脚本对应的 `-VcTools -SdkRoot -SdkVersion` 参数。这些参数只涉及本机工具链。

`--diag-input` 默认关闭，必须保留日志，不能与 `--no-log` 合用。普通构建 `python build.py all --no-deploy` 不启用这些观测。

自检只在自己创建的隐藏窗口发送合成消息；剪贴板测试故意不调用 OpenClipboard，预期错误 1418，不读取或改变剪贴板。自检通过表示日志、开关、字符路径和 LastError 验证通过，不表示真实 IME 或 QQ 飞车已通过。完整输入回归是 `tests/run-input-path.ps1`；英语宿主上的失败结果也需要保留，不能为了通过回归先改宿主模式。

不要把 `out/input-path/versions/host-dbcs-probe.cpp` 的固定地址私有副本实验移植成通用运行补丁。它仅用于指纹匹配的本机 USER32，交接包运行工具没有使用那些地址。

## 已知证据与交回材料

| Evidence | Finding | 后续验证路径 |
|---|---|---|
| 简中宿主上的 x86/x64 字符回归 | 当前临时尾字节修复对这些用例有效 | 保留为基线，见首轮报告 |
| 本机保持 ACP936、关闭 USER32 私有副本 DBCS 位后 EDIT 字符为空 | USER32 宿主模式是独立依赖 | 在游戏进程确认状态、调用方及是否处于失败路径 |
| 同一实验中 SetWindowTextW 和 EM_REPLACESEL 仍正常 | 字符失败不能直接解释全部粘贴现象 | 单独追踪格式、返回字节和游戏过滤 |
| CP936 不安装日文默认输入语言 Hook | 日文 HKL 怀疑已排除 | 不为本 issue 修改该 CP932 策略 |

历史报告：[EDIT 边界](2026-10-28_逆向-EDIT双字节边界-report.md)、[USER32 跨版本](2026-10-28_逆向-USER32-DBCS跨版本-report.md)、[宿主 DBCS 实验](2026-10-29_逆向-宿主DBCS模式与QQ飞车输入-report.md)。

交回材料：manifest 和四个实际 DLL 指纹；实际游戏 PID/架构/版本；系统与非 Unicode 语言；before/after JSON 和完整 PID 日志；每项 case 的时间、样本和肉眼现象；若进行第二阶段，附调用栈、A/W、返回长度和仅测试串的字节。说明未能采到的项目，区分观察、推断和仍未验证项。

现有初始化日志会记录启动命令行；分享日志前检查并删去其中的账号凭据或令牌，保留本次测试需要的进程、状态和事件信息。

## 本机准备验证

已完成 x86/x64 诊断构建与自检：开启前的 B、关闭后的 C 未进入字符日志；开启期间的字符、原生 Peek、EDIT 尾字节和剪贴板失败路径均被记录；原始错误 1418 保持不变；结束后无开关残留。原有 x86/x64、原生及 LEP CP936/932/1252 输入回归通过。普通非诊断构建也已编译通过。

外部采集脚本已在隐藏 STATIC 窗口上验证；断点生成脚本已分别由 x86/x64 CDB 在自有测试程序上命中五个目标入口，检查了参数和栈输出。这些检查不包含实际 QQ 飞车、真实输入法组合串或游戏剪贴板读取。

本轮只准备和验证诊断工具。游戏启动、登录和实际输入由用户与另一台电脑的 agent 完成；本文没有授权向其它聊天自动发消息、改变系统区域设置或发布游戏修复。
