"""Package the opt-in diagnostic build and a reproducible handoff patch."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=root / 'out/qqspeed-input-handoff.zip')
    output = parser.parse_args().output.resolve()
    tracked = [
        'build.py', 'LocaleEmulatorPlus/LocaleEmulatorPlus.h',
        'LocaleEmulatorPlus/Hooks/User32Hook.cpp',
        'ai-docs/2026-10-28_逆向-USER32-DBCS跨版本-report.md',
    ]
    added = [
        'LocaleEmulatorPlus/Hooks/InputDiagnostics.h',
        'tests/set-input-trace.ps1', 'tests/collect-qqspeed-input.ps1',
        'tests/write-input-breakpoints.ps1', 'tests/input-diagnostics-probe.cpp',
        'tests/run-input-diagnostics.ps1', 'tests/package-input-diagnostics.py',
        'ai-docs/qqspeed-input-handoff.md',
        'ai-docs/2026-10-29_逆向-宿主DBCS模式与QQ飞车输入-report.md',
    ]
    patch = subprocess.run(['git', 'diff', '--binary', 'HEAD', '--', *tracked],
                           cwd=root, check=True, capture_output=True).stdout
    for name in added:
        exists = subprocess.run(['git', 'cat-file', '-e', f'HEAD:{name}'],
                                cwd=root, capture_output=True).returncode == 0
        if exists:
            patch += subprocess.run(['git', 'diff', '--binary', 'HEAD', '--', name],
                                    cwd=root, check=True, capture_output=True).stdout
            continue
        # --no-index represents new files without modifying the user's index.
        result = subprocess.run(['git', 'diff', '--no-index', '--binary', '--', '/dev/null', name],
                                cwd=root, capture_output=True)
        if result.returncode not in (0, 1):
            raise RuntimeError(result.stderr.decode(errors='replace'))
        patch += result.stdout
    files: dict[str, bytes] = {'changes.patch': patch}
    docs = [
        'ai-docs/qqspeed-input-handoff.md',
        'ai-docs/2026-10-28_逆向-EDIT双字节边界-report.md',
        'ai-docs/2026-10-28_逆向-USER32-DBCS跨版本-report.md',
        'ai-docs/2026-10-29_逆向-宿主DBCS模式与QQ飞车输入-report.md',
        'ai-docs/issue3-edit-scope.md',
    ]
    for name in docs + [x for x in added if x.startswith('tests/')]:
        files[name] = (root / name).read_bytes()
    for arch in ('x86', 'x64'):
        directory = root / 'out/input-diagnostics' / arch
        for stem in ('LoaderDll', 'LocaleEmulatorPlus'):
            name = f'{stem}_{arch}.dll'
            files[f'bin/{name}'] = (directory / name).read_bytes()
        files[f'bin/input-diagnostics-probe_{arch}.exe'] = (directory / 'input-diagnostics-probe.exe').read_bytes()
        files[f'validation/selftest-{arch}.log'] = (directory / 'selftest.log').read_bytes()
    files['START-HERE.md'] = (
        '# QQ 飞车输入诊断交接包\n\n'
        '先读 [完整交接文档](ai-docs/qqspeed-input-handoff.md)。\n\n'
        '`bin/` 是匹配的四个诊断 DLL 和双架构自检程序；'
        '`tests/` 是采集、开关和调试命令生成脚本；'
        '`changes.patch` 用于基线仓库上的重建。\n\n'
        '本包没有修改宿主 DBCS 标志，没有添加 API Hook。'
        '完成登录后才按实际游戏 PID 开启采样。\n'
    ).encode('utf-8')
    base = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, check=True,
                          capture_output=True, text=True).stdout.strip()
    manifest = {
        'format': 1, 'baseCommit': base, 'uncommittedPatch': bool(patch), 'patchRequired': bool(patch),
        'buildCommand': 'python build.py all --diag-input --no-deploy',
        'runtimeArm': 'DLL_DIRECTORY/Log/input-trace.DECIMAL_PID.enable',
        'eventLimitPerProcess': 2048, 'gameValidated': False,
        'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())},
    }
    files['manifest.json'] = json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf-8')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    print(output)
    print('SHA256', hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
