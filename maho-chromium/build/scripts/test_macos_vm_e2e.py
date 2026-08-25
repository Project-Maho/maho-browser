#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import FrozenInstanceError
from pathlib import Path
from unittest.mock import patch


_SCRIPT_DIR = Path(__file__).resolve().parent
_SCRIPT_PATH = _SCRIPT_DIR / "macos_vm_e2e.py"
_MANIFEST_PATH = _SCRIPT_DIR / "macos_vm_e2e_manifest.json"
_SPEC = importlib.util.spec_from_file_location("macos_vm_e2e_under_test", _SCRIPT_PATH)
assert _SPEC and _SPEC.loader
macos_vm_e2e = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = macos_vm_e2e
_SPEC.loader.exec_module(macos_vm_e2e)


_EXPECTED_SUITES = ("interactive-smoke", "ai-mock", "all")
_EXPECTED_AI_SCENARIOS = (
    "ai_panel_renders",
    "ai_real_turn",
    "ai_mock_general_turn",
    "ai_mock_quick_action_context",
    "ai_mock_server_error",
    "ai_mock_key_cleared_no_request",
)


class MacosVmE2eManifestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest_data = json.loads(_MANIFEST_PATH.read_text(encoding="utf-8"))

    def _write_manifest(self, data: object, directory: str) -> Path:
        path = Path(directory) / "manifest.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def _assert_invalid_manifest(self, data: object, message: str) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            path = self._write_manifest(data, temporary_dir)
            with self.assertRaises(macos_vm_e2e.InvalidManifestError) as raised:
                macos_vm_e2e.load_manifest(path)
        self.assertEqual(raised.exception.code, "invalid_manifest")
        self.assertIn(message, str(raised.exception))

    def test_production_manifest_parses_into_frozen_typed_values(self) -> None:
        manifest = macos_vm_e2e.load_manifest(_MANIFEST_PATH)

        self.assertEqual(manifest.base_image, "ghcr.io/cirruslabs/macos-tahoe-base:latest")
        self.assertEqual(manifest.golden_image, "maho-e2e-golden")
        self.assertEqual(
            manifest.guest_workspace,
            Path("/Users/Shared/maho-workspace"),
        )
        self.assertEqual(manifest.output_dir, Path("out/Default"))
        self.assertEqual(manifest.cdp_address, "127.0.0.1")
        self.assertEqual(manifest.suite_names, _EXPECTED_SUITES)
        self.assertEqual(manifest.ai_scenarios, _EXPECTED_AI_SCENARIOS)
        self.assertIn(
            "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",
            manifest.suite("interactive-smoke").gtest_filter,
        )
        self.assertIn(
            "MahoCommandIncognitoInteractiveUiTest.AllowlistHappyPath",
            manifest.suite("interactive-smoke").gtest_filter,
        )
        self.assertIn(
            "MahoCommandColdStartInteractiveUiTest.AutoPresentsVisiblePaletteWithActionResults",
            manifest.suite("interactive-smoke").gtest_filter,
        )
        with self.assertRaises(FrozenInstanceError):
            setattr(manifest, "golden_image", "changed")

    def test_selected_gtest_narrows_preflight_inventory(self) -> None:
        manifest = macos_vm_e2e.load_manifest(_MANIFEST_PATH)
        selected = "MahoPeekInteractiveUiTest.RoutesPopupToForegroundTabPreservingOpenerMessaging"

        self.assertEqual(
            macos_vm_e2e._required_gtests(manifest, selected_gtest=selected),
            (selected,),
        )

    def test_guest_preflight_requires_only_native_test_artifacts(self) -> None:
        manifest = macos_vm_e2e.load_manifest(_MANIFEST_PATH)
        command = macos_vm_e2e._guest_suite_preflight_argv(
            manifest, ("ssh", "admin@example.test")
        )

        remote_command = command[-1]
        self.assertNotIn("process.versions.node", remote_command)
        self.assertIn("interactive_ui_tests", remote_command)
        self.assertIn("Maho.app", remote_command)
        self.assertNotIn("--gtest_list_tests", remote_command)

    def test_list_json_prints_only_manifest_suites_and_ai_scenarios(self) -> None:
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            result = macos_vm_e2e.main(["list", "--json"])

        self.assertEqual(result, 0)
        self.assertEqual(
            json.loads(stdout.getvalue()),
            {
                "suites": list(_EXPECTED_SUITES),
                "ai_scenarios": list(_EXPECTED_AI_SCENARIOS),
            },
        )

    def test_list_plain_prints_exact_inventory_names(self) -> None:
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            result = macos_vm_e2e.main(["list"])

        self.assertEqual(result, 0)
        self.assertEqual(
            tuple(stdout.getvalue().splitlines()),
            _EXPECTED_SUITES + _EXPECTED_AI_SCENARIOS,
        )

    def test_output_path_traversal_is_typed_and_starts_no_subprocess(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["output_dir"] = "out/Default/../VM-E2E"

        with tempfile.TemporaryDirectory() as temporary_dir:
            path = self._write_manifest(data, temporary_dir)
            stderr = io.StringIO()
            with patch.object(macos_vm_e2e.subprocess, "run") as run:
                with redirect_stderr(stderr):
                    result = macos_vm_e2e.main(
                        ["--manifest", str(path), "list", "--json"]
                    )

        self.assertEqual(result, 2)
        self.assertEqual(
            json.loads(stderr.getvalue())["error"]["code"],
            "invalid_manifest",
        )
        run.assert_not_called()

    def test_rejects_non_absolute_guest_workspace(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["guest_workspace"] = "Users/Shared/maho-workspace"
        self._assert_invalid_manifest(data, "guest_workspace")

    def test_task_owned_sources_and_docs_do_not_expose_developer_home(self) -> None:
        workspace_root = _SCRIPT_DIR.parents[2]
        owned_paths = {
            path
            for pattern in ("macos_vm_*", "test_macos_vm_*.py")
            for path in _SCRIPT_DIR.glob(pattern)
            if path.is_file()
        }
        owned_paths.update(
            workspace_root / relative_path
            for relative_path in (
                "AGENTS.md",
                "docs/operations/macos-vm-e2e.md",
                "maho/docs/chromium-desktop-e2e.md",
                "maho/docs/chromium-fork-build.md",
                "maho/docs/test-matrix.md",
                "maho-chromium/docs/chromium-desktop-e2e.md",
                "maho-chromium/docs/chromium-sidebar-dnd-reference.md",
                ".omo/plans/macos-vm-isolated-interactive-e2e.md",
            )
        )
        developer_home = "/Users/" + "indo"
        for path in sorted(owned_paths):
            with self.subTest(path=path.relative_to(workspace_root)):
                self.assertNotIn(
                    developer_home,
                    path.read_text(encoding="utf-8"),
                )

    def test_rejects_output_other_than_default_directory(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["output_dir"] = "out/VM-E2E"
        self._assert_invalid_manifest(data, "out/Default")

    def test_rejects_empty_gtest_filter(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["suites"][0]["gtest_filter"] = []
        self._assert_invalid_manifest(data, "gtest_filter")

    def test_rejects_duplicate_ai_scenario_names(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["suites"][1]["scenarios"].append(
            data["suites"][1]["scenarios"][0]
        )
        self._assert_invalid_manifest(data, "duplicate")

    def test_rejects_non_loopback_cdp_address(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["cdp_address"] = "0.0.0.0"
        self._assert_invalid_manifest(data, "loopback")

    def test_rejects_aggregate_member_that_is_not_a_manifest_suite(self) -> None:
        data = copy.deepcopy(self.manifest_data)
        data["suites"][2]["members"].append("missing-suite")
        self._assert_invalid_manifest(data, "missing-suite")

    def test_rejects_unknown_requested_suite(self) -> None:
        manifest = macos_vm_e2e.load_manifest(_MANIFEST_PATH)
        with self.assertRaises(macos_vm_e2e.UnknownSuiteError) as raised:
            manifest.suite("missing-suite")
        self.assertEqual(raised.exception.code, "unknown_suite")


if __name__ == "__main__":
    unittest.main()
