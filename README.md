# LocaleEmulatorPlus-Core

[中文](#中文) | [English](#english)

## 中文

本仓库存放 LocaleEmulatorPlus 的原生核心，负责创建目标进程、注入和区域环境模拟。GUI、CLI 与右键菜单扩展在 [julixian/LocaleEmulatorPlus](https://github.com/julixian/LocaleEmulatorPlus) 单独维护。

如果你不是开发者，而是普通用户，请前往 GUI 仓库下载发行版，不需要直接使用本 Core 仓库。

实现原理和维护说明请看 [MAINTAINING.md](MAINTAINING.md)。
Windows 上使用 Python 3.10+ 构建：`python build.py all --no-deploy`。

第三方原生调用可直接使用或复制 [LocaleEmulatorPlus.ixx](include/LocaleEmulatorPlus.ixx) 中的独立声明，参考[启动示例](examples/lep-launch.cpp)与 [ABI 说明](ai-docs/lepb-v4.md)。

本项目源自 [xupefei/Locale-Emulator-Core](https://github.com/xupefei/Locale-Emulator-Core)，采用 [GNU GPL v3](LICENSE.txt) 许可证发布。

## English

This repository contains LocaleEmulatorPlus's native core for process creation, injection, and locale emulation. The GUI, CLI, and shell extension are maintained at [julixian/LocaleEmulatorPlus](https://github.com/julixian/LocaleEmulatorPlus).

If you are an end user rather than a developer, download releases from the GUI repository instead of using this core repository directly.

For implementation notes and maintenance details, see [MAINTAINING.md](MAINTAINING.md).
Build with Python 3.10+ on Windows: `python build.py all --no-deploy`.

Third-party native callers can use the standalone declarations in [LocaleEmulatorPlus.ixx](include/LocaleEmulatorPlus.ixx), with a [launch example](examples/lep-launch.cpp) and [ABI notes](ai-docs/lepb-v4.md).

Derived from [xupefei/Locale-Emulator-Core](https://github.com/xupefei/Locale-Emulator-Core) and licensed under the [GNU GPL v3](LICENSE.txt).
