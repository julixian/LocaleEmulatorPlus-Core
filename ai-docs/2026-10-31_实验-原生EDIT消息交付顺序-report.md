# 原生 ANSI EDIT 的混合同步与异步字符交付实验

日期：2026-10-31。目的：验证普通窗口过程自行投递尾字节、同步调用首字节，是否能在未使用 LEP 的进程中拼错字符。此前对话中的队列示意是推演，不是该场景的实测记录；本实验补充直接证据。

## 环境和范围

- 本机原生 ACP936，SM_DBCSENABLED=1；USER32 文件版本 10.0.26100.8875。
- x86、x64 分别测试；每个用例使用全新进程，排除线程级字符缓存残留。
- 测试程序仅调用 Windows API，没有加载或调用 LEP。x64 EXE 的静态 DLL 依赖为 KERNEL32.dll 和 USER32.dll。
- 创建隐藏的控制窗口与真正的 ANSI EDIT 子窗口，IsWindowUnicode(edit)=FALSE。所有测试操作从控制窗口的窗口过程执行。EDIT 的 ANSI 子类只记录 WM_CHAR 后调用原过程。
- 不改变系统区域、输入法、用户剪贴板、交互窗口或 LEP 运行代码。

## Evidence

E-01：`out/native-dbcs-queue-probe/native-dbcs-queue-probe.cpp`，包含用例与控制窗口过程。E-02/E-03：同目录 `isolated-results-x86.txt`、`isolated-results-x64.txt`，两种架构的独立进程实测输出。早期 `results-*.txt` 在同一线程连续运行用例，会残留原生字符缓存，不用作结论依据。

复现：在 ACP936、DBCS 开启的宿主运行以下命令，参数 0 至 5 各自启动独立进程。源码会检查宿主条件，不匹配则返回 3。

```powershell
0..5 | ForEach-Object { & .\out\native-dbcs-queue-probe\x64.exe $_ }
0..5 | ForEach-Object { & .\out\native-dbcs-queue-probe\x86.exe $_ }
```

## Findings

以下操作均针对同一个 EDIT。中=U+4E2D，CP936 字节为 D6 D0；Z=0x5A。

| 用例 | 操作顺序 | x86/x64 结果 |
|---|---|---|
| 0 | PostMessageA(Z)、PostMessageA(D6)、PostMessageA(D0)，然后用 A 消息循环派发 | 文本 5A D6 D0，即 Z中 |
| 1 | PostMessageA(Z)、PostMessageA(D0)，然后 SendMessageA(D6) | 文本 D6 5A，即 U+8AFE；原来的 Z 被用作第二字节 |
| 2 | 同用例 1，但首字节通过 CallWindowProcA 交给当前 ANSI 子类过程 | 同样得到 D6 5A / U+8AFE |
| 3 | PostMessageA(Z)，然后 SendMessageW(完整的中) | 文本 D6 D0 / U+4E2D；Z 仍能从队列取出 |
| 4 | PostMessageA(Z)、PostMessageA(D6)、PostMessageA(D0)，然后额外 SendMessageA(D6) | 文本 D6 5A；另有 D6、D0 可被后续取出 |
| 5 | PostMessageA(Z)、PostMessageW(完整的中)，然后用 A 消息循环派发 | 文本 5A D6 D0，即 Z中 |

F-01（validated）：混用“异步投递尾字节”和“同步交付首字节”可在原生 EDIT 中复现错误配对，不依赖 LEP。E-01/E-02/E-03 支撑。

F-02（validated）：完整字符通过原生 Unicode 同步路径交付，以及完整字符按顺序异步交付，均有正常对照。不能把 F-01 推广为正常键盘/IME 输入自然就会产生该顺序。

## Path 和适用边界

P-01：普通控制窗口过程执行 PostMessageA(Z) → PostMessageA(D0) → SendMessageA(D6) → 原生 ANSI EDIT 处理首字节 → 最终文本为 D6 5A，说明 Z 参与了配对。用 CallWindowProcA 交付首字节也能复现。

该结果验证的是 API 操作组合及其输出，不是对内部队列逐项布局的直接取证。PostMessageA 本身也可能参与字符缓冲/转换，不能把每次成功调用都等同于队列里增加一个未经处理的裸字节。本文不声称 Windows 的正常输入法会自然产生此序列，也不证明 Frame 是唯一可行方案。

对 LEP 的意义：当前适配需要保持原来的完整 Unicode 字符交付效果。用 Frame 在同步回调内提供配对尾字节是一种实现；要改为真实队列方案，还需单独验证同步语义、消息顺序和原生 A/W 缓存，不能仅凭本实验否定所有队列方案。

## 完整 Unicode 字符的包装边界对照

为明确 LEP 的责任边界，新增用例 6：在原 ANSI EDIT 上通过 SetWindowLongPtrW 安装临时 Unicode 包装过程。包装过程把收到的完整 Unicode WM_CHAR 按 ACP936 编码，然后依次 CallWindowProcA 交付 D6、D0，不提供 Frame 或尾字节 Hook。测试后恢复原 ANSI 入口再查询文本。该用例只模拟 LEP 的窗口包装及手工拆字节边界，没有加载实际 LEP，也没有模拟它的其余功能。

E-04/E-05 为同目录 `unicode-boundary-results-x86.txt`、`unicode-boundary-results-x64.txt`。用例 3 与 6 分别在独立进程运行，程序发起的操作相同：先 PostMessageA(Z)，再 SendMessageW(完整的中)。差别仅为有无上述手工包装边界。

| 条件 | x86/x64 文本结果 | 原先排队的 Z |
|---|---|---|
| 原生 ANSI EDIT，无手工 Unicode 包装（用例 3） | D6 D0 / U+4E2D，中 | 仍能取出 0x5A |
| 安装 Unicode 包装，手工逐字节调用 A 原过程，无 Frame（用例 6） | D6 5A / U+8AFE，错误字符 | 被消费；D0 的第二次原过程调用也没有纠正文本 |

复现：分别运行 `out/native-dbcs-queue-probe/x64.exe 3`、`out/native-dbcs-queue-probe/x64.exe 6`，x86 同理。

F-03（validated）：原程序交付完整 Unicode 字符可以正常工作；引入手工 Unicode→ANSI 逐字节回调边界后，同样的正常调用产生错误。LEP 的尾字节适配需要补偿的是自己采用的包装机制带来的语义差异。此前原程序手工混合同步/异步字节的例子，不能单独证明此责任；本组对照直接验证了边界差异。Frame 并非唯一实现，但完整字符交付效果必须与原生路径一致。

## WM_SETTEXT 的 A/W 发送路径

补充探针 `out/title-path-probe/title_path_probe.cpp` 创建同线程的普通 A、W 窗口，分别通过 SendMessageA 发送 CP936 的中。E-06 为 `out/title-path-probe/trace-x64.txt`：CDB 在 win32u!NtUserMessageCall 上按 WM_SETTEXT 及测试阶段过滤，记录调用栈和窗口过程收到的参数。

| 目标过程 | 发送阶段的实测调用链 | 收到的文本 |
|---|---|---|
| Unicode | SendMessageA → SendMessageInternal → SendMessageWorker → NtUserMessageCall → _fnINSTRING → DispatchClientMessage → W 回调 | U+4E2D |
| ANSI | SendMessageA → SendMessageInternal → SendMessageWorker → UserCallWinProcCheckWow → A 回调 | D6 D0 |

两组最终标题均为 U+4E2D。ANSI 回调随后调用 DefWindowProcA 设置标题时，还会出现 RealDefWindowProcWorker → NtUserMessageCall；该栈属于默认处理阶段，与发送阶段的直接调用分开记录。

F-04（validated，本机 x64）：同线程发送 WM_SETTEXT 时，接收过程的 A/W 属性也影响分支选择。LEP 把实际入口设为 WindowProcW，因此 A→W 标题发送对应上表第一条路径。按源码，LepNtUserMessageCall 的 INSTRINGNULL 分支将 lParam 字符串按目标 ACP 解码，并以 W 标志调用原入口；WindowProcW 的 UserfnINSTRINGNULL 分支再编码后交给保存的 ANSI 原过程。标准对话框标题按第 16 节的 W 默认过程例外处理。

## SendMessageA / PostMessageA 入口转换对照

实验脚本：`out/ime-entry-ablation/run_ablation.py`。它使用现有 NativeBuild 的编译和链接参数，分别生成 baseline、no-send、no-post、no-send-post 四组 DLL。后三组把对应入口的转换条件置为 FALSE，继续调用保存的原入口；NtUserMessageCall 转换、窗口包装、DBCS 环境同步和 DefWindowProcA 适配保持启用。每组分别链接 x86/x64，使用现有 `tests/ime-probe.cpp` 的探针及独立子进程测试。

E-07 为 `out/ime-entry-ablation/run.log`，分组输出在各变体目录下的 `results-x86.txt`、`results-x64.txt`。宿主原生 ACP936、DBCS=1。

| 变体 | x64 CP936 | x64 CP932 | x86 结果 |
|---|---|---|---|
| baseline | 通过 | 通过；随后 CP1252 通过 | CP936/932/1252 均通过 |
| no-send | 通过 | 同步 SendMessageA 的四个检查失败 | CP936 通过，CP932 测试子进程退出 0xC000041D |
| no-post | 通过 | PostMessageA 的四个检查失败 | CP936 通过，CP932 测试子进程退出 0xC000041D |
| no-send-post | 通过 | 上述八个检查失败 | CP936 通过，CP932 测试子进程退出 0xC000041D |

CP932 中的“中”为 92 86；跳过相应入口转换后，A 回调观察到 3F，Unicode 接收窗口观察到 U+62DE，符合把 92 86 按 CP936 解码的结果。x64 分组分别定位了同步与异步路径的失败；x86 的异常退出单独保留为待解释现象。失败变体的现有父探针在 CP932 失败后结束，因此表中仅 baseline 覆盖到 CP1252。

F-05（validated）：在本机现有 LEP 架构下，修正 DBCS 标志之后，这两个 IME 入口转换仍分别有作用。CP936 和 CP932 的 DBCS 标志同为 1，标志相同并不能保证这些消息按目标代码页解码。该实验确认当前两个入口适配的必要用例；统一底层代码页状态仍可作为未来替代实现的研究方向。
