# Issue 3 输入兼容性分析范围

- auth：用户要求接续另一台电脑的提交，编译部署诊断版本测试 issue #3，并在提供乱码结果后要求“查，找出原因”。
- in_scope：本仓库的字符消息与剪贴板适配；实际游戏 PID 的诊断日志；普通只读调试附加；只读分析本机 x86/x64 USER32 和 QQ 飞车输入相关模块；自有隐藏窗口及独立窗口站中的对照实验。
- out_of_scope：修改 Windows 系统文件、修改游戏及保护组件、改变系统区域设置、自动登录、向 GitHub 发布消息或发布版本。
- network_profile：本机 IDA MCP HTTP；CDB 符号解析可使用公开符号来源，不测试远程游戏服务。
- artifacts：输入回归位于 `out/input-path/`；实际游戏采集与机制实验位于 `out/qqspeed-capture-20261030-english/`；正式分析说明位于 `ai-docs/`。
- completion：区分实际观察、可重复的机制与未验证的调用路径；说明输入和粘贴各自的失败原因。诊断完成不等于游戏修复已通过回归。
