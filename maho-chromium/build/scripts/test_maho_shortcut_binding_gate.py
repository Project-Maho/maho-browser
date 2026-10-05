"""Regression coverage for the Maho intercepted-key binding gate.

The gate itself lives in `apply_chromium_src_overrides.verify_maho_intercepted_bindings`
and runs on every build. These tests exercise that production function directly:
it must accept the applied tree and reject the pre-fix dispatch, so a future
upstream Ctrl+Tab command-id rename (the Chromium 154
IDC_SELECT_NEXT_TAB -> IDC_CYCLE_TO_NEXT_TAB change that bypassed the MRU
overlay) fails the build instead of shipping silently.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from apply_chromium_src_overrides import (
    _UPDATED154_REVISION,
    default_chromium_src,
    repo_root,
    verify_maho_intercepted_bindings,
)

_BROWSER_COMMAND_CONTROLLER = "chrome/browser/ui/browser_command_controller.cc"


def _git_init(path):
    subprocess.run(["git", "init", "--quiet", str(path)],
                   check=True, capture_output=True)


class MahoInterceptedBindingGateTest(unittest.TestCase):
    def setUp(self):
        override = os.environ.get("MAHO_CHROMIUM_SRC")
        self.upstream = Path(override) if override else default_chromium_src()

    def upstream_text(self, relative_path):
        return subprocess.run(
            ["git", "-C", str(self.upstream), "show",
             f"{_UPDATED154_REVISION}:{relative_path}"],
            check=True, capture_output=True).stdout.decode()

    def canonical_patch(self):
        patch = (repo_root() / "build" / "chromium_base_patches" /
                 _UPDATED154_REVISION / f"{_BROWSER_COMMAND_CONTROLLER}.patch")
        self.assertTrue(patch.exists(), f"canonical base patch missing: {patch}")
        return patch

    def stage(self, dispatch_text):
        root = Path(tempfile.mkdtemp(prefix="maho-binding-gate-"))
        _git_init(root)
        for relative_path in ("chrome/browser/ui/cocoa/accelerators_cocoa.mm",
                              "chrome/browser/ui/accelerator_table.cc"):
            target = root / relative_path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(self.upstream_text(relative_path))
        target = root / _BROWSER_COMMAND_CONTROLLER
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(dispatch_text)
        return root

    def applied_dispatch_text(self):
        root = self.stage(self.upstream_text(_BROWSER_COMMAND_CONTROLLER))
        subprocess.run(["git", "-C", str(root), "apply",
                        str(self.canonical_patch())],
                       check=True, capture_output=True)
        return (root / _BROWSER_COMMAND_CONTROLLER).read_text()

    def pre_fix_dispatch_text(self):
        root = self.stage(self.applied_dispatch_text())
        migration = (repo_root() / "build" / "chromium_base_migrations" /
                     _UPDATED154_REVISION / f"{_BROWSER_COMMAND_CONTROLLER}.patch")
        subprocess.run(["git", "-C", str(root), "apply", "--reverse",
                        str(migration)],
                       check=True, capture_output=True)
        return (root / _BROWSER_COMMAND_CONTROLLER).read_text()

    def test_applied_tree_passes_the_gate(self):
        root = self.stage(self.applied_dispatch_text())
        self.assertEqual(verify_maho_intercepted_bindings(root), [])

    def test_pre_fix_dispatch_fails_the_gate_for_ctrl_tab(self):
        root = self.stage(self.pre_fix_dispatch_text())
        violations = verify_maho_intercepted_bindings(root)
        self.assertTrue(violations, "the gate accepted a dispatch that bypasses "
                                    "the MRU switcher for Ctrl+Tab")
        joined = "\n".join(violations)
        for platform in ("mac", "win", "linux"):
            self.assertIn(f"{platform}: Ctrl+Tab binds", joined)

    def test_missing_handler_is_reported_not_silently_passed(self):
        root = self.stage("void f() { switch (id) { case IDC_NEW_TAB: break; } }")
        violations = verify_maho_intercepted_bindings(root)
        self.assertTrue(violations)
        self.assertIn("MaybeHandleMruTabSwitch", violations[0])


if __name__ == "__main__":
    unittest.main()
