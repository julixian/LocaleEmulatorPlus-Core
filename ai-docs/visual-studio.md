# 使用 Visual Studio 浏览代码

打开仓库根目录的 [`LocaleEmulatorPlus-Core.slnx`](../LocaleEmulatorPlus-Core.slnx)，可看到两个 C++ 项目：

- `LocaleEmulatorPlus`：Core、Hooks、Utility、完整 support 层，以及构建辅助代码和公开模块声明。
- `LoaderDll`：Loader 实现、公开加载器头、broker 协议和导出定义。

项目采用 Makefile 类型，仅供浏览、语义高亮、跳转和 IntelliSense。VS 中的生成、重新生成、清理和单文件编译均只显示提示，不编译或部署 DLL。实际构建继续使用 `build.py` / `build-nolog.py`，输出位置与 VS 编辑缓存分开。

## 选择要查看的代码分支

解决方案提供 `Log`、`NoLog` 两种配置，分别定义 `ENABLE_LOG=1`、`ENABLE_LOG=0`；`Win32` / `x64` 平台对应 32 位和 64 位条件编译。两种配置都定义 `NDEBUG`，与当前 Python 构建一致。Loader 项目另定义 `LEP_LOADER_DLL=1`，以便解析真实的启动和 broker 实现。

内部代码按 C++14 解析，公开的 `include/LocaleEmulatorPlus.ixx` 单独使用 C++20，32 位平台保留 `/Gr` 和 SSE 选项。x64 专用 CRT shim 在 Win32 配置中排除。

## IntelliSense 配置

[`ide/Editor.Configuration.props`](../ide/Editor.Configuration.props) 声明查看配置和目标 SDK。SDK 默认 `10.0.26100.0`，也接受启动 VS 前设置的 `SDK_VER`。C++ 工具集、CRT 和系统头文件路径由当前 VS 安装解析，不绑定本机的 VS 安装目录。

[`ide/Editor.IntelliSense.props`](../ide/Editor.IntelliSense.props) 集中定义宏、内部包含路径和编译器语义选项，额外加入 WDK 的 `km` 包含目录。底层 NT 声明继续通过 `support/Support.h` / `native/Declarations.h` 建立包含上下文，不修改实际源文件来迁就编辑器。

机器需安装 C++ 桌面开发组件及相应 SDK/WDK 头文件。Makefile 项目的 IntelliSense 设置方式见 [Microsoft 文档](https://learn.microsoft.com/en-us/cpp/build/reference/creating-a-makefile-project?view=msvc-170)。

项目中的文件清单是显式列表，`.vcxproj.filters` 按现有目录分组。以后新增、移动或删除源文件时，同步更新对应项目和 filters；这些清单只影响 VS 浏览，Python 构建仍使用自己的源文件选择规则。

## 验证范围

已通过本机 VS MSBuild 验证四种配置下的解决方案加载、两个项目的宏和包含路径，以及设计时 `GetClCommandLines`。构建命令只输出提示，未调用 C++ 编译器、链接器或部署脚本。VS 生成的状态、浏览数据库和中间目录分别位于已忽略的 `.vs/`、`out/ide/`。
