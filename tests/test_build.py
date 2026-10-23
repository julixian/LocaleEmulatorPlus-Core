"""Build orchestration checks; run with python -m unittest discover -s tests."""

from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build


@unittest.skipUnless(os.name == "nt", "Windows build driver")
class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="LEP build 测试 ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = build.BuildConfig(
            root=self.root, vc_tools=self.root / "VS tools", sdk_root=self.root / "Windows SDK",
            sdk_version="test", extra_cl=("/DENABLE_LOG=1",), deploy_dir=None,
        )
        self.output = io.StringIO()
        self.stdout = redirect_stdout(self.output)
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)

    def test_windows_quoting_round_trip_without_shell_expansion(self):
        arguments = [r'/IC:\SDK path\include', '/DNAME="value with spaces"',
                     '/DROOT=C:\\trailing path\\', '/DSYNTAX=%TEMP%&$(command)', '/D名称=值']
        self.assertEqual(build.split_windows_arguments(subprocess.list2cmdline(arguments)), arguments)
        self.assertEqual(build.split_windows_arguments("  "), [])

    def test_dry_run_does_not_create_outputs_run_tools_or_modify_environment(self):
        source = self.root / "LocaleEmulatorPlus/support/runtime/Test.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("", encoding="utf-8")
        config = replace(self.config, dry_run=True)
        original_environment = os.environ.copy()
        with patch.object(build.subprocess, "run") as run:
            build.NativeBuild(config, "x86").build()
        run.assert_not_called()
        self.assertFalse((self.root / "out").exists())
        self.assertEqual(dict(os.environ), original_environment)
        self.assertIn("Dry run completed", self.output.getvalue())

    def test_tool_failure_stops_before_compilation_linking_or_deployment(self):
        with patch.object(build.subprocess, "run", side_effect=subprocess.CalledProcessError(7, ["lib.exe"])) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                build.NativeBuild(self.config, "x86").build()
        self.assertEqual(run.call_count, 1)
        self.assertNotIn("Build succeeded", self.output.getvalue())

    def test_all_target_stops_on_first_failure_and_returns_tool_exit_code(self):
        with patch.object(build.BuildConfig, "preflight"), \
             patch.object(build.NativeBuild, "build", side_effect=subprocess.CalledProcessError(7, ["cl.exe"])) as run, \
             redirect_stderr(io.StringIO()):
            result = build.main(["all", "--no-deploy"])
        self.assertEqual(result, 7)
        self.assertEqual(run.call_count, 1)

    def test_missing_dependencies_fail_before_creating_artifacts(self):
        with self.assertRaises(FileNotFoundError):
            self.config.preflight(("x86", "x64"))
        self.assertFalse((self.root / "out").exists())

    def test_existing_deploy_directory_receives_both_dlls(self):
        destination = self.root / "部署 path"
        destination.mkdir()
        driver = build.NativeBuild(replace(self.config, deploy_dir=destination), "x64")
        driver.output.mkdir(parents=True)
        names = ("LocaleEmulatorPlus_x64.dll", "LoaderDll_x64.dll")
        for name in names:
            (driver.output / name).write_bytes(name.encode())
        driver.deploy(names)
        for name in names:
            self.assertEqual((destination / name).read_bytes(), name.encode())

    def test_absent_deploy_directory_is_not_created(self):
        destination = self.root / "missing deployment"
        driver = build.NativeBuild(replace(self.config, deploy_dir=destination), "x64")
        driver.deploy(("Core.dll", "Loader.dll"))
        self.assertFalse(destination.exists())

    def test_no_deploy_overrides_existing_destination_and_no_log_preserves_extra_flags(self):
        with patch.object(build.BuildConfig, "preflight"), patch.object(build, "NativeBuild") as driver:
            self.assertEqual(build.main(["x64", "--no-deploy", "--no-log",
                                         "--deploy-dir", str(self.root),
                                         "--extra-cl=/DLEP_DIAG_INIT=1 /DENABLE_LOG=1"]), 0)
        config = driver.call_args.args[0]
        self.assertIsNone(config.deploy_dir)
        self.assertIn("/DLEP_DIAG_INIT=1", config.extra_cl)
        self.assertEqual(config.extra_cl[-1], "/DENABLE_LOG=0")

    def test_cli_paths_override_legacy_environment(self):
        with patch.dict(os.environ, {"VS_ROOT": "old vs", "SDK_VER": "old sdk"}):
            options = build.parse_options(["X86", "--vs-root", "new vs", "--sdk-version", "new sdk"])
        self.assertEqual(options.target, "x86")
        self.assertEqual(options.vs_root, Path("new vs"))
        self.assertEqual(options.sdk_version, "new sdk")


if __name__ == "__main__":
    unittest.main()
