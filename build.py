#!/usr/bin/env python3
"""Build the native Core and Loader DLLs with MSVC and the patched linker.

Requires Windows and Python 3.10+. No third-party Python packages are needed.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
CORE_SOURCES = (
    "HandleTable.cpp",
    "Hooks/Gdi32Hook.cpp",
    "Hooks/Kernel32Hook.cpp",
    "Hooks/NtdllHook.cpp",
    "Hooks/User32Hook.cpp",
    "Hooks/User32Environment.cpp",
    "LocaleEmulatorPlus.cpp",
    "stdafx.cpp",
    "Utility/Utility.cpp",
    "HookPort.cpp",
)
SUPPORT_DIRECTORIES = ("runtime", "native", "pe", "patch")
SYSTEM_LIBRARIES = (
    "kernel32.lib", "user32.lib", "gdi32.lib", "dbghelp.lib",
    "libcmt.lib", "oldnames.lib", "libvcruntime.lib",
)


def parse_options(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", default="all", type=str.lower,
                        choices=("all", "x86", "x64"))
    parser.add_argument("--vs-root", type=Path,
                        default=os.environ.get("VS_ROOT") or r"D:\VisualStudio2026")
    parser.add_argument("--msvc-version", default=os.environ.get("MSVC_VER") or "14.44.35207")
    parser.add_argument("--sdk-root", type=Path,
                        default=os.environ.get("SDK_ROOT") or r"C:\Program Files (x86)\Windows Kits\10")
    parser.add_argument("--sdk-version", default=os.environ.get("SDK_VER") or "10.0.26100.0")
    parser.add_argument("--no-log", action="store_true", help="disable logging")
    parser.add_argument("--diag-init", action="store_true", help="show initialization diagnostic dialogs")
    parser.add_argument("--diag-input", action="store_true", help="enable PID-armed input diagnostics in existing hooks")
    parser.add_argument("--deploy-dir", type=Path,
                        default=os.environ.get("LEP_DEPLOY_DIR") or r"D:\GALGAME\GALGAMETOOLS\LocaleEmulatorPlus",
                        help="copy DLLs here only if this directory already exists")
    parser.add_argument("--no-deploy", action="store_true", help="leave all artifacts in out/")
    parser.add_argument("--dry-run", action="store_true", help="print commands without running or writing files")
    parser.add_argument("--verbose", action="store_true", help="print each command before running it")
    return parser.parse_args(argv)


@dataclass(frozen=True)
class BuildConfig:
    root: Path
    vc_tools: Path
    sdk_root: Path
    sdk_version: str
    log_enabled: bool
    deploy_dir: Path | None
    diag_init: bool = False
    diag_input: bool = False
    dry_run: bool = False
    verbose: bool = False

    @property
    def linker(self) -> Path:
        return self.root / "dep/tools/link.exe"

    @property
    def sdk_include(self) -> Path:
        return self.sdk_root / "Include" / self.sdk_version

    def preflight(self, architectures: tuple[str, ...]) -> None:
        """Check every selected toolchain before producing partial output."""
        files = [self.linker, self.sdk_include / "um/winnt.h"]
        directories = [self.vc_tools / "include"]
        directories += [self.sdk_include / name for name in ("ucrt", "shared", "um", "km")]
        for arch in architectures:
            host = self.vc_tools / "bin/Hostx64" / arch
            files += [host / "cl.exe", host / "lib.exe"]
            directories += [self.vc_tools / "lib" / arch]
            directories += [self.sdk_root / "Lib" / self.sdk_version / name / arch
                            for name in ("um", "ucrt")]
        missing = [path for path in files if not path.is_file()]
        missing += [path for path in directories if not path.is_dir()]
        if missing:
            raise FileNotFoundError("Missing build dependencies:\n  " + "\n  ".join(map(str, missing)))


class NativeBuild:
    def __init__(self, config: BuildConfig, arch: str):
        self.config = config
        self.arch = arch
        self.root = config.root
        self.core = self.root / "LocaleEmulatorPlus"
        self.host = config.vc_tools / "bin/Hostx64" / arch
        self.compiler = self.host / "cl.exe"
        self.librarian = self.host / "lib.exe"
        self.output = self.root / "out" / arch
        self.objects = self.root / "out/obj" / arch
        self.libraries = self.root / "out/libs" / arch
        self.environment = os.environ.copy()
        self.environment["PATH"] = os.pathsep.join((
            str(self.host), str(config.linker.parent), self.environment.get("PATH", "")))
        self.compiler_flags = [
            "/nologo", "/utf-8", "/c", "/O2", "/Ob1", "/GF", "/Gy", "/GR-", "/EHs-c-", "/GS-",
            *(["/Gr", "/arch:SSE", "/DWIN32"] if arch == "x86" else ["/DWIN64", "/D_WIN64"]),
            "/DNDEBUG", f"/DENABLE_LOG={int(config.log_enabled)}", f"/DLEP_DIAG_INIT={int(config.diag_init)}",
            f"/DLEP_DIAG_INPUT={int(config.diag_input)}",
            "/D_NO_CRT_STDIO_INLINE", "/Zc:wchar_t",
        ]
        self.includes = [f"/I{path}" for path in (
            self.core, config.vc_tools / "include",
            *(config.sdk_include / name for name in ("ucrt", "shared", "um", "km")),
        )]
        self.library_paths = [f"/libpath:{path}" for path in (
            config.sdk_root / "Lib" / config.sdk_version / "um" / arch,
            config.sdk_root / "Lib" / config.sdk_version / "ucrt" / arch,
            config.vc_tools / "lib" / arch,
        )]
        self.linker_flags = [
            "/nodefaultlib", "/debug:none", "/opt:ref", "/ignore:4254",
            *(["/safeseh:no"] if arch == "x86" else []),
            "/manifest:no", f"/machine:{arch}", "/subsystem:windows",
        ]

    def run(self, executable: Path, *arguments: str | Path, cwd: Path | None = None) -> None:
        command = [str(executable), *map(str, arguments)]
        if self.config.verbose or self.config.dry_run:
            print(subprocess.list2cmdline(command), flush=True)
        if not self.config.dry_run:
            # An argument list preserves spaces and Unicode paths. No cmd.exe,
            # shell expansion, or inherited VS environment setup is needed.
            subprocess.run(command, cwd=cwd or self.root, env=self.environment, check=True)

    def stage(self, number: int, description: str) -> None:
        print(f"[{self.arch} {number}/6] {description}", flush=True)

    def compile(self, source: Path, object_name: str, *flags: str) -> Path:
        target = self.objects / object_name
        self.run(self.compiler, *self.compiler_flags, *flags, f"/Fo{target}",
                 *self.includes, source, cwd=self.core)
        return target

    def archive(self, name: str, objects: list[Path]) -> None:
        self.run(self.librarian, "/nologo", f"/out:{self.libraries / name}", *objects)

    def import_aliases(self) -> list[str]:
        if self.arch != "x86":
            return []
        aliases = []
        for name, size in (("LdrInitializeThunk", 8), ("LdrRegisterDllNotification", 16),
                           ("LdrUnregisterDllNotification", 4), ("CreateProcessInternalW", 48)):
            for prefix in ("__imp__", "_"):
                aliases.append(f"/alternatename:{prefix}{name}@{size}={prefix}{name}")
        return aliases

    def link_libraries(self, *, core: bool) -> list[str | Path]:
        return [
            self.libraries / "LepSupport.lib", self.libraries / "LepMyLib.lib", "ntdll.lib",
            self.libraries / f"lep_ntdll_{self.arch}.lib",
            self.libraries / f"lep_k32_{self.arch}.lib",
            *(name for name in SYSTEM_LIBRARIES if core or name != "user32.lib"),
        ]

    def build(self) -> None:
        if not self.config.dry_run:
            for directory in (self.output, self.objects, self.libraries):
                directory.mkdir(parents=True, exist_ok=True)

        self.stage(1, "Generating import libraries")
        for name in ("ntdll", "k32"):
            stem = f"lep_{name}_{self.arch}"
            self.run(self.librarian, "/nologo", f"/machine:{self.arch}",
                     f"/def:{self.root / 'dep/libs' / (stem + '.def')}",
                     f"/out:{self.libraries / (stem + '.lib')}")

        self.stage(2, "Building LEP delay-load helper")
        helper_objects = [self.compile(self.root / "dep/libs/LepDelayLoad_x86_x64.cpp", "LepDelayLoad.obj")]
        if self.arch == "x64":
            helper_objects.append(self.compile(self.root / "dep/libs/LepCrtShim_x64.cpp", "LepCrtShim.obj", "/Oi-"))
        self.archive("LepMyLib.lib", helper_objects)

        self.stage(3, "Building native support library")
        support_objects = []
        for directory in SUPPORT_DIRECTORIES:
            sources = sorted((self.core / "support" / directory).glob("*.cpp"))
            for source in sources:
                support_objects.append(self.compile(source, f"Support_{directory}_{source.stem}.obj"))
        if not support_objects:
            raise FileNotFoundError(f"No support sources found in {self.core / 'support'}")
        self.archive("LepSupport.lib", support_objects)

        self.stage(4, "Compiling LocaleEmulatorPlus")
        core_objects = [self.compile(self.core / source, f"LEP_{Path(source).stem}.obj")
                        for source in CORE_SOURCES]

        core_name = f"LocaleEmulatorPlus_{self.arch}"
        self.stage(5, f"Linking {core_name}.dll")
        self.run(self.config.linker, "/nologo", "/dll", f"/out:{self.output / (core_name + '.dll')}",
                 f"/implib:{self.output / (core_name + '.lib')}", *core_objects,
                 *self.library_paths, *self.linker_flags, "/dynamicbase:no", "/nxcompat", "/entry:DllMain",
                 "/export:LoadFirstDll",
                 *(f"/delayload:{name}.dll" for name in ("KERNEL32", "USER32", "GDI32", "DBGHELP")),
                 *self.import_aliases(), *self.link_libraries(core=True))

        loader_name = f"LoaderDll_{self.arch}"
        self.stage(6, f"Building {loader_name}.dll")
        loader_object = self.compile(self.root / "LoaderDll/LoaderDll.cpp", "LoaderDll.obj", "/DLEP_LOADER_DLL=1")
        environment_object = self.compile(self.root / "LoaderDll/Environment.cpp", "Environment.obj", "/DLEP_LOADER_DLL=1")
        self.run(self.config.linker, "/nologo", "/dll", "/noentry",
                 f"/out:{self.output / (loader_name + '.dll')}",
                 f"/implib:{self.output / (loader_name + '.lib')}",
                 f"/def:{self.root / 'LoaderDll/LoaderDll.def'}", loader_object, environment_object,
                 *self.library_paths, *self.linker_flags,
                 *self.import_aliases(), *self.link_libraries(core=False))

        self.deploy((core_name + ".dll", loader_name + ".dll"))
        status = "Dry run completed" if self.config.dry_run else "Build succeeded"
        print(f"\n[{self.arch}] {status}.\n  {self.output / (core_name + '.dll')}\n"
              f"  {self.output / (loader_name + '.dll')}", flush=True)

    def deploy(self, names: tuple[str, str]) -> None:
        destination = self.config.deploy_dir
        if destination is None:
            print("Deployment disabled; artifacts remain in out/.", flush=True)
        elif not destination.is_dir():
            print(f'Deployment directory "{destination}" does not exist; skipped copy.', flush=True)
        elif self.config.dry_run:
            print(f'Would copy DLLs to "{destination}".', flush=True)
        else:
            for name in names:
                source, target = self.output / name, destination / name
                if source.resolve() != target.resolve():
                    shutil.copy2(source, target)
            print(f'Copied DLLs to "{destination}".', flush=True)


def main(argv: list[str] | None = None) -> int:
    options = parse_options(argv)
    if options.diag_input and options.no_log:
        print("--diag-input requires logging; remove --no-log.", file=sys.stderr)
        return 2
    if os.name != "nt":
        print("This build requires Windows and MSVC.", file=sys.stderr)
        return 1
    try:
        config = BuildConfig(
            root=ROOT,
            vc_tools=options.vs_root.resolve() / "VC/Tools/MSVC" / options.msvc_version,
            sdk_root=options.sdk_root.resolve(), sdk_version=options.sdk_version,
            log_enabled=not options.no_log,
            diag_init=options.diag_init,
            diag_input=options.diag_input,
            deploy_dir=None if options.no_deploy else options.deploy_dir.resolve(),
            dry_run=options.dry_run, verbose=options.verbose,
        )
        architectures = ("x86", "x64") if options.target == "all" else (options.target,)
        config.preflight(architectures)
        for arch in architectures:
            NativeBuild(config, arch).build()
    except subprocess.CalledProcessError as error:
        print(f"Build failed (exit {error.returncode}): {subprocess.list2cmdline(error.cmd)}", file=sys.stderr)
        return error.returncode if 0 < error.returncode < 256 else 1
    except OSError as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Build interrupted.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
