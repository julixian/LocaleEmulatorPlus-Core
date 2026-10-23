# Issue #2：SkyProtect 启动链的 LEP 注入失败定位

分析日期：2026-10-24。对象：[LocaleEmulatorPlus issue #2](https://github.com/julixian/LocaleEmulatorPlus/issues/2)。

## 结论

本机本次运行已确认失败点在 `SPM.exe`（x64）创建 `trgame.exe`（x86）后的 broker 注入阶段，返回 `STATUS_ACCESS_DENIED`（0xC0000022）。父进程 hook 命中、728 字节配置序列化及 broker 启动均成功。现有证据不支持将问题简单归因为“payload 丢失”。

目标进程内存操作受到保护限制是优先排查方向，但尚未确定返回拒绝的是哪一次 API，也未直接证明拒绝来源一定是 SkyProtect 驱动。

## Scope

- auth：用户请求判断 issue 原因，提供游戏目录 `F:\cjpp`，并运行游戏供读取日志。
- in_scope：Core/Loader 源码、游戏 PE 架构和导入表、现有 LEP 日志；未修改保护组件或游戏文件。
- network_profile：只读取公开 issue。未登录游戏或提交 issue 评论。

## Evidence

### E-001：本地架构与配置

- source_ref：`F:\cjpp\Launcher.exe`、`talesrunner.exe`、`SPL.exe`、`SPM.exe`、`trgame.exe` 的 PE Machine；`Launcher.exe.lep.config`。
- content_hash：n/a。
- observed：Launcher/talesrunner/SPL/trgame 为 x86，SPM 为 x64；配置 Location=zh-CN、Timezone=China Standard Time、RegistryRedirectionMode=2、HookUILanguageMode=1、RunAsAdmin=true。
- repro_command：使用 PE 工具读取文件 Machine 字段；本机通过 Python pefile 的 `FILE_HEADER.Machine` 验证，0x14C 为 x86，0x8664 为 x64。

### E-002：前序注入成功

- source_ref：`D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus\Log\SPL.exe.6E98.log.txt`、`LocaleEmulatorPlus-broker-12112.log`、`SPM.exe.5D7C.log.txt`。
- content_hash：n/a。
- observed：SPL 的 x86→x64 broker 返回 00000000；SPM PID=23932 初始化完成，Payload=728、Environment=460、locale=0804、代码页=936。

### E-003：游戏子进程注入失败

- source_ref：`SPM.exe.5D7C.log.txt:100`、`:117`、`:119`；`LocaleEmulatorPlus-broker-4140.log`。
- content_hash：n/a。
- raw_excerpt：

```text
cross-arch begin pid=24760 currentWow64=0 targetWow64=1 flags=00000005
cross-arch payload serialized bytes=728
cross-arch broker finished pid=4140 processExit=00000000 result=C0000022
create edge creatorPid=23932 creatorTid=22932 childPid=24760 childTid=1968 createStatus=00000000 injectStatus=C0000022 originalSuspended=1
[4140] finish status=C0000022
```

- repro_command：

```powershell
$logRoot = 'D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus\Log'
Select-String -LiteralPath "$logRoot\SPM.exe.5D7C.log.txt" -Pattern 'init complete|cross-arch begin|cross-arch payload serialized|cross-arch broker finished|childPid=24760'
Get-Content -LiteralPath "$logRoot\LocaleEmulatorPlus-broker-4140.log" -Encoding Unicode
```

broker 日志为无 BOM 的 UTF-16LE；SPM 日志曾暂时显示为零字节，稍后写出完整内容，因此不能用当时文件大小判定 SPM 未初始化。

### E-004：状态回传及失败处理

- source_ref：`LoaderDll/LoaderDll.cpp:224`、`:241`、`:257`、`:268`、`:281`、`:284`、`:312`、`:341`；`LocaleEmulatorPlus/Hooks/NtdllHook.cpp:1507`、`:1528`。
- content_hash：n/a。
- observed：broker 先 OpenProcess，再打开共享映射并验证 Payload，随后加载本地 Core 镜像、分配目标内存、读写和修改目标内存保护。清理路径将真实状态写回 Config->Result。创建 hook 用 Status2 保存注入结果，但最终返回原进程创建结果 Status。
- repro_command：阅读上述函数 `LepBrokerInject` 和 `LepNtCreateUserProcess`。运行目录中四个 Loader/Core DLL 的 SHA256 均与当前 out/x86、out/x64 构建一致。

## Findings

- F-001：severity=n/a_re；evidence_ids=E-001,E-002,E-003；confidence=high；location=SPM(x64)→broker(x86)→trgame(x86)；status=validated。失败是明确的跨位数注入访问拒绝，不是 GUI 未传入简体中文配置或子进程 hook 未命中。
- F-002：severity=n/a_re；evidence_ids=E-003,E-004；confidence=medium；location=LepBrokerInject 内存操作阶段；status=candidate。保护对内存访问或实际授予句柄权限的限制最可疑；当前日志缺少分阶段状态，不能区分 LoadPeImage、分配、读取、保护修改或镜像/Payload 写入。broker 未记录末尾的 metadata injection status，错误发生在该日志之前。
- F-003：severity=n/a_re；evidence_ids=E-003,E-004；confidence=high；location=LepNtCreateUserProcess；status=validated。注入失败未成为进程创建失败，因此游戏继续启动但缺少 LEP 环境。

父进程读取到共享 Config->Result=C0000022，结合当前代码顺序，可以推断 broker 已经成功获得进程句柄和共享映射：若 OpenProcess 在最前面直接失败，父进程的共享 Result 会保留初始 STATUS_UNSUCCESSFUL。Payload 格式错误则应返回 STATUS_INVALID_PARAMETER。不能仅凭访问拒绝就断言 OpenProcess 本身失败。

## Path

P-001，path_type=callflow：LEPProc_x86(17796) → Launcher_x86(17516) → talesrunner_x86(3464) → SPL_x86(28312) → broker_x64(12112) → SPM_x64(23932，初始化成功) → 创建 trgame_x86(24760) → broker_x86(4140，C0000022) → hook 返回原创建成功状态 → 游戏继续运行但注入失败。

## 下一步

初次定位建议补上分阶段状态和实际句柄权限。用户随后要求修改代码并自行复测，候选修复与测试结果如下。

## 候选修复与测试包

2026-10-24 已修改 Core 和 Loader，公开 LEPB/API 保持原样：

1. 跨位数注入接收创建游戏时已有的进程句柄。先挂起创建 broker，再用 DuplicateHandle/DUPLICATE_SAME_ACCESS 把句柄复制到 broker，发布其本地句柄值后恢复 broker 线程，避免读取竞态。
2. 复制失败时记录错误，回退到最小权限的 OpenProcess。所需权限为 PROCESS_VM_OPERATION、PROCESS_VM_READ、PROCESS_VM_WRITE、PROCESS_QUERY_LIMITED_INFORMATION（0x1038）。
3. broker 检查实际权限和目标 PID，权限不足时记录 granted/required/missing。对配置、句柄、权限、本地镜像加载、目标内存分配、Ldr 读取/保护修改、镜像/Payload 写入和 Ldr patch 分别记录 stage 与 NTSTATUS。
4. 内部 broker 协议更新为 v5，采用固定宽度的句柄、阶段和权限字段。两种位数的 Payload 偏移均为 48，四个 Loader/Core DLL 需配套替换。
5. 进程创建返回策略保持原样。候选改动不保证目标保护策略允许所需访问，实际游戏兼容性等待用户复测。

### 验证证据

- E-005：source_ref=out/issue2-broker-build.log；content_hash=n/a；x86/x64 日志版构建成功，无编译警告。
- E-006：source_ref=tests/broker-handle-probe.cpp、tests/run-broker-handle.ps1、out/issue2-broker-handles.log；content_hash=n/a；两种位数共 10 组测试通过。原句柄与最小权限回退均成功；缺少 VM 权限返回 C0000022/stage=4；错误 PID 在 stage=5 拒绝；无效 PID 的打开错误在 stage=3 写回。
- E-007：source_ref=out/issue2-broker-compat.log、out/issue2-public-api.log；content_hash=n/a；原有 34 组 v2/v3、同位数和跨位数子进程测试，以及公开声明的 x86/x64 布局及真实调用测试通过。跨位数日志确认 source=created-handle，最后 stage=complete。
- repro_command（在 Core 仓库构建后运行）：

```powershell
.\tests\run-broker-handle.ps1
.\tests\run-lepb-compat.ps1
.\tests\run-public-api.ps1
```

F-004：severity=n/a_re；evidence_ids=E-005,E-006,E-007；confidence=high；location=候选修复的通用子进程路径；status=validated。通用测试通过，实际 SkyProtect 游戏仍待用户验证。

测试 DLL 已替换 `D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus` 中对应的四个文件，并与构建哈希核对一致。原 DLL 备份在 `D:\VSProj\LocaleEmulatorPlus-Core\out\backups\issue2-before-handle-fix-20261024-013607`。

复测后读取同目录 Log 下的新 SPM 和 broker 日志。broker 日志为无 BOM 的 UTF-16LE，使用 Get-Content -Encoding Unicode。成功应出现 source=created-handle、missing=00000000、stage=complete；失败时记录对应 stage/status 和 granted/required/missing 即可定位，无需公开完整游戏参数。

## Timeline

1. 读取公开 issue，确认报告的 x86→x64→x86 启动链。
2. 核对 F:\cjpp 的 PE 架构及 zh-CN 配置。
3. 读取用户本次运行日志，确认前序注入及 SPM 初始化成功。
4. 等待缓冲日志写出后读取 SPM，定位 broker C0000022；对照共享结果回传和进程创建返回逻辑。
5. 实现句柄传递和分阶段诊断，完成测试并备份、替换日志版 DLL，等待用户复测。
6. 用户复测后，确认 SPM→trgame 的 broker 全阶段成功，游戏进程 LEP 初始化完成并应用代码页 936。

## 实际游戏复测：成功

2026-10-24 01:38 用户按原配置启动后，SPM PID=8592，trgame PID=24556。最后一跳使用复制的创建句柄：

```text
source=created-handle granted=001FFFFF required=00001038 missing=00000000
stage=allocate-target-memory status=00000000
stage=write-payload status=00000000
stage=patch-ldr status=00000000
injection result=00000000 stage=complete targetPid=24556 granted=001FFFFF
```

游戏自身日志确认配置已被消费、初始化完成：

```text
initialize entry loader=0 bootstrapPayloadPresent=1 payloadSize=728
init stage bootstrap payload size=728 environment=460 registryMode=2
init complete pid=24556 tid=2004 processOnly=0 nlsApply=1 status=00000000
nls sync after peb=936/936 ntdll=936 mb=1 oemmb=1 ntdllStatus=00000000 status=00000000
```

E-008：source_ref=`D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus\Log\LocaleEmulatorPlus-broker-24460.log`、`SPM.exe.2190.log.txt`、`trgame.exe.5FEC.log.txt`；content_hash=n/a；repro_command：broker 文件用 Get-Content -Encoding Unicode，其余文件筛选 init complete、cross-arch、nls sync。父进程日志同样显示 result=00000000、stage=complete。

F-005：severity=n/a_re；evidence_ids=E-008；confidence=high；location=SPM→trgame 实际游戏链路；status=validated。复用创建句柄的方案解决本机本次复现，配置不再中断于最后一跳。旧版没有逐阶段日志，仍不能追溯确认原先具体哪一次内存 API 返回拒绝，或推断所有保护版本均兼容。
