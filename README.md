# LocaleEmulatorPlus-Core

[中文](#中文) | [English](#english)

## 中文

本仓库存放 LocaleEmulatorPlus 的原生核心，负责创建目标进程、注入和区域环境模拟。GUI 与启动器在
[julixian/LocaleEmulatorPlus](https://github.com/julixian/LocaleEmulatorPlus)
单独维护。

如果你不是开发者，而是普通用户，请前往 GUI 分支下载 LEP，不需要直接使用本
Core 仓库。

实现原理和维护说明请看 [MAINTAINING.md](MAINTAINING.md)。
Windows 上使用 Python 3.10+ 构建：`python build.py all --no-deploy`。

第三方原生调用可直接使用或复制 [LocaleEmulatorPlus.ixx](include/LocaleEmulatorPlus.ixx)
中的独立声明，参考[启动示例](examples/lep-launch.cpp)与 [ABI 说明](ai-docs/lepb-v3.md)。

原始上游许可证说明和第三方来源请见 `README_ORG.md`。

## English

This repository contains LocaleEmulatorPlus's native core for process creation,
injection, and locale emulation. The GUI and launchers are maintained at
[julixian/LocaleEmulatorPlus](https://github.com/julixian/LocaleEmulatorPlus).

If you are an end user rather than a developer, download LEP from the GUI
repository instead of using this core repository directly.

For implementation notes and maintenance details, see [MAINTAINING.md](MAINTAINING.md).
Build with Python 3.10+ on Windows: `python build.py all --no-deploy`.

Third-party native callers can use the standalone declarations in
[LocaleEmulatorPlus.ixx](include/LocaleEmulatorPlus.ixx), with a
[launch example](examples/lep-launch.cpp) and [ABI notes](ai-docs/lepb-v3.md).

See `README_ORG.md` for the original upstream license notice and third-party attribution.
