# Issue 3 EDIT 边界适配范围

- auth：用户要求在本机复现、尝试修复输入乱码，并指出修复应遵循现有两处 A/W 边界；包含必要的本地只读诊断和回归测试。
- in_scope：本仓库 user32 字符消息适配、EDIT 输入及选择位置、测试程序；只读分析本机 `C:\Windows\SysWOW64\user32.dll` 的相关函数。
- out_of_scope：修改 Windows 系统文件、修改游戏、向 GitHub 发布消息、部署或发布版本。
- network_profile：本机 IDA MCP HTTP；符号解析可使用公开符号来源，不测试远程目标。
- artifacts：测试和二进制分析输出在 `out/input-path/`；正式分析说明在 `ai-docs/`。
- completion：确认输入失败机制，移除整类 Unicode 旁路，验证保留原 ANSI 回调的修复；不把本机用例通过等同于 QQ 飞车 issue 关闭。
