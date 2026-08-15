#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path


_SCRIPT_DIR = Path(__file__).resolve().parent
_SCRIPT_PATH = _SCRIPT_DIR / "macos_vm_build_transport.py"
_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_build_transport_under_test", _SCRIPT_PATH
)
assert _SPEC and _SPEC.loader
transport = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = transport
_SPEC.loader.exec_module(transport)


class MacosVmBuildTransportTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_dir.cleanup)
        self.workspace = (Path(self.temporary_dir.name) / "maho-workspace").resolve()
        self.chromium_src = self.workspace / "chromium" / "src"
        self.out_dir = self.chromium_src / "out" / "Default"
        self.scripts_dir = self.workspace / "maho-chromium" / "build" / "scripts"
        self.out_dir.mkdir(parents=True)
        self.scripts_dir.mkdir(parents=True)

    def _write(self, relative_path: str, contents: bytes = b"fixture") -> Path:
        path = self.chromium_src / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        return path

    def test_canonical_build_argv_is_exact_and_lock_backed(self) -> None:
        self.assertEqual(
            transport.canonical_build_argv(self.workspace),
            (
                sys.executable,
                "maho-chromium/build/scripts/build_maho.py",
                "--skip-rust",
                "--out-dir",
                "out/Default",
                "--no-launch",
                "--ninja-target",
                "chrome",
                "--ninja-target",
                "maho_mail_helper",
                "--ninja-target",
                "interactive_ui_tests",
            ),
        )
        self.assertNotIn("autoninja", transport.canonical_build_argv(self.workspace))

    def test_output_validation_accepts_only_literal_out_default(self) -> None:
        self.assertEqual(
            transport.validate_output_dir(Path("out/Default")),
            Path("out/Default"),
        )
        for invalid in (
            Path("out/VM-E2E"),
            Path("out/Release"),
            Path("out/VM-E2E/../Default"),
            Path("out/Default/../Default"),
            Path("/tmp/out/Default"),
            Path("out/Default/child"),
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(transport.InvalidBuildTransportError):
                    transport.validate_output_dir(invalid)

    def test_output_validation_rejects_default_symlink_to_alternate_tree(self) -> None:
        self.out_dir.rmdir()
        alternate = self.chromium_src / "out" / "VM-E2E"
        alternate.mkdir()
        self.out_dir.symlink_to(alternate, target_is_directory=True)

        with self.assertRaises(transport.InvalidBuildTransportError):
            transport.validate_output_dir(
                Path("out/Default"), chromium_src=self.chromium_src
            )

    def test_runtime_deps_are_normalized_deduplicated_and_default_bounded(self) -> None:
        binary = self._write("out/Default/interactive_ui_tests")
        dylib = self._write("out/Default/libbase.dylib")
        app_binary = self._write("out/Default/Maho.app/Contents/MacOS/Maho")
        runtime_deps = "\n".join(
            (
                "./interactive_ui_tests",
                "libbase.dylib",
                "Maho.app/Contents/MacOS/Maho",
                "../../chrome/test/data/index.html",
                "./libbase.dylib",
                "",
            )
        )

        parsed = transport.parse_runtime_dependencies(
            runtime_deps,
            chromium_src=self.chromium_src,
            output_dir=Path("out/Default"),
        )

        source_file = self._write("chrome/test/data/index.html")
        self.assertEqual(parsed, (binary, dylib, app_binary, source_file))

        for invalid in (
            "/tmp/escape",
            "../../../outside",
            "../VM-E2E/stale",
            "../Release/stale",
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(transport.InvalidRuntimeDependencyError):
                    transport.parse_runtime_dependencies(
                        invalid,
                        chromium_src=self.chromium_src,
                        output_dir=Path("out/Default"),
                    )

    def test_runtime_deps_reject_symlink_resolved_alternate_output(self) -> None:
        alternate = self._write("out/VM-E2E/interactive_ui_tests")
        link = self.out_dir / "linked_test"
        link.symlink_to(alternate)

        with self.assertRaises(transport.InvalidRuntimeDependencyError):
            transport.parse_runtime_dependencies(
                "linked_test",
                chromium_src=self.chromium_src,
                output_dir=Path("out/Default"),
            )

    def test_read_only_mount_contract_preserves_exact_guest_workspace_path(self) -> None:
        contract = transport.read_only_mount_contract(
            self.workspace,
            Path("/Users/Shared/maho-workspace"),
        )

        self.assertEqual(contract.host_path, self.workspace.resolve())
        self.assertEqual(contract.share_name, "maho-workspace")
        self.assertEqual(
            contract.tart_argument,
            f"maho-workspace:{self.workspace.resolve()}:ro,tag=maho-workspace",
        )
        self.assertEqual(
            contract.guest_share_path,
            Path("/Volumes/My Shared Files/maho-workspace"),
        )
        self.assertEqual(
            contract.guest_workspace,
            Path("/Users/Shared/maho-workspace"),
        )
        self.assertEqual(
            contract.remount_argv,
            (
                "sudo",
                "mount",
                "-t",
                "virtiofs",
                "-o",
                "ro",
                "maho-workspace",
                "/Users/Shared/maho-workspace",
            ),
        )

    def test_fallback_plan_uses_relative_copy_and_hashes_exact_dependencies(self) -> None:
        binary = self._write("out/Default/interactive_ui_tests", b"binary")
        dylib = self._write("out/Default/libbase.dylib", b"dylib")

        plan = transport.plan_rsync_fallback(
            workspace_root=self.workspace,
            dependencies=(binary, dylib),
            guest_workspace=Path("/Users/Shared/maho-workspace"),

        )

        self.assertEqual(
            plan.rsync_argv,
            (
                "rsync",
                "--archive",
                "--recursive",
                "--relative",
                "--files-from=-",
                f"{self.workspace.resolve()}/./",
                "admin@guest:/Users/Shared/maho-workspace/",
            ),
        )
        self.assertEqual(
            plan.relative_paths,
            (
                "chromium/src/out/Default/interactive_ui_tests",
                "chromium/src/out/Default/libbase.dylib",
            ),
        )
        self.assertEqual(plan.manifest[0].sha256, hashlib.sha256(b"binary").hexdigest())
        self.assertEqual(plan.manifest[0].size_bytes, len(b"binary"))

    def test_fallback_accepts_source_tree_and_rejects_other_outputs(self) -> None:
        sibling = self._write("out/VM-E2E/interactive_ui_tests")
        source_file = self._write("chrome/test/data/index.html")
        linked = self.out_dir / "linked_test"
        linked.symlink_to(sibling)

        plan = transport.plan_rsync_fallback(
            workspace_root=self.workspace,
            dependencies=(source_file,),
        )
        self.assertEqual(
            plan.relative_paths,
            ("chromium/src/chrome/test/data/index.html",),
        )

        for invalid in (sibling, linked):
            with self.subTest(invalid=invalid):
                with self.assertRaises(transport.InvalidRuntimeDependencyError):
                    transport.plan_rsync_fallback(
                        workspace_root=self.workspace,
                        dependencies=(invalid,),
                    )

    def test_fallback_accepts_workspace_runner_as_extra_path(self) -> None:
        binary = self._write("out/Default/interactive_ui_tests", b"binary")
        script = self.workspace / "maho-chromium/build/scripts/e2e_chromium_ai.js"
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_text("console.log('fixture');", encoding="utf-8")

        plan = transport.plan_rsync_fallback(
            workspace_root=self.workspace,
            dependencies=(binary,),
            extra_paths=(script,),
        )

        self.assertEqual(
            plan.relative_paths,
            (
                "chromium/src/out/Default/interactive_ui_tests",
                "maho-chromium/build/scripts/e2e_chromium_ai.js",
            ),
        )

    def test_preflight_detects_missing_and_stale_outputs_before_execution(self) -> None:
        runtime_deps_path = self.out_dir / "interactive_ui_tests.runtime_deps"
        runtime_deps_path.write_text(
            "./interactive_ui_tests\n./libmissing.dylib\nMaho.app/\n",
            encoding="utf-8",
        )
        binary = self._write("out/Default/interactive_ui_tests")
        app = self.out_dir / "Maho.app"
        app.mkdir()
        os.utime(binary, (100, 100))
        os.utime(runtime_deps_path, (200, 200))

        result = transport.inspect_preflight(
            workspace_root=self.workspace,
            output_dir=Path("out/Default"),
            required_tests=("MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",),
            listed_tests=(),
        )

        self.assertFalse(result.ready)
        self.assertIn(
            self.out_dir / "libmissing.dylib",
            result.missing_paths,
        )
        self.assertIn(binary, result.stale_paths)
        self.assertEqual(
            result.missing_tests,
            ("MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",),
        )

    def test_preflight_rejects_vm_e2e_output_without_reading_it(self) -> None:
        with self.assertRaises(transport.InvalidBuildTransportError):
            transport.inspect_preflight(
                workspace_root=self.workspace,
                output_dir=Path("out/VM-E2E"),
                required_tests=(),
                listed_tests=(),
            )


if __name__ == "__main__":
    unittest.main()
