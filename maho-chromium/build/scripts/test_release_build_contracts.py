#!/usr/bin/env python3
import importlib.util
import os
import sys
import tempfile
import types
import unittest
from types import SimpleNamespace
from typing import Any
from unittest.mock import call, patch


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def _load_module(filename: str, module_name: str) -> types.ModuleType:
    spec = importlib.util.spec_from_file_location(
        module_name,
        os.path.join(_SCRIPT_DIR, filename),
    )
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


_release_local = _load_module("release_local.py", "release_local_under_test")
_linux_packages = _load_module(
    "build_maho_linux_packages.py",
    "build_maho_linux_packages_under_test",
)
_overrides = _load_module(
    "apply_chromium_src_overrides.py",
    "apply_chromium_src_overrides_under_test",
)


class TestReleaseArtifactPaths(unittest.TestCase):

    def test_windows_release_uses_release_output_directory(self) -> None:
        args = SimpleNamespace(
            skip_build=True,
            dry_run=True,
            out_dir="out/Release",
        )

        artifacts = _release_local.build_and_bundle_win("1.0.0", args)

        output_dir = os.path.join(
            _release_local._CHROMIUM_SRC,
            "out",
            "Release",
        )
        self.assertEqual(
            artifacts,
            [
                os.path.join(output_dir, "MahoSetup-1.0.0.exe"),
                os.path.join(output_dir, "update-win.json"),
            ],
        )

    def test_manifests_are_written_to_selected_release_output(self) -> None:
        args = SimpleNamespace(
            skip_build=True,
            dry_run=True,
            out_dir="out/Release",
        )
        output_dir = _release_local.release_output_dir(args.out_dir)

        with patch.object(_release_local, "run") as run:
            _release_local.build_and_bundle_mac("1.0.0", args)
            _release_local.build_and_bundle_win("1.0.0", args)

        self.assertEqual(
            run.call_args_list[0].args[0][-2:],
            ["--output-dir", output_dir],
        )
        self.assertEqual(
            run.call_args_list[1].args[0][-2:],
            ["--output-dir", output_dir],
        )

    def test_publish_uploads_without_making_release_public(self) -> None:
        with patch.object(_release_local, "run") as run:
            _release_local.publish(
                "v1.0.0",
                "Project-Maho/release",
                ["/tmp/MahoSetup-1.0.0.exe"],
                dry_run=True,
            )

        self.assertEqual(
            run.call_args_list,
            [
                call(
                    [
                        "gh",
                        "release",
                        "upload",
                        "v1.0.0",
                        "--repo",
                        "Project-Maho/release",
                        "--clobber",
                        "/tmp/MahoSetup-1.0.0.exe",
                    ],
                    True,
                ),
            ],
        )

    def test_finalize_makes_uploaded_release_public_and_latest(self) -> None:
        with patch.object(_release_local, "run") as run:
            _release_local.finalize_release(
                "v1.0.0",
                "Project-Maho/release",
                dry_run=True,
            )

        self.assertEqual(
            run.call_args_list,
            [
                call(
                    [
                        "gh",
                        "release",
                        "edit",
                        "v1.0.0",
                        "--repo",
                        "Project-Maho/release",
                        "--draft=false",
                        "--latest",
                    ],
                    True,
                ),
            ],
        )

    def test_finalize_requires_every_platform_asset(self) -> None:
        with patch.object(
            _release_local.subprocess,
            "run",
            return_value=SimpleNamespace(stdout="MahoSetup-1.0.0.exe\n"),
        ), patch.object(_release_local, "run"):
            with self.assertRaisesRegex(
                SystemExit,
                "missing required release assets.*Maho-1.0.0.dmg",
            ):
                _release_local.finalize_release(
                    "v1.0.0",
                    "Project-Maho/release",
                    dry_run=False,
                )

    def test_finalize_accepts_complete_cross_platform_release(self) -> None:
        asset_names = "\n".join(
            asset.format(version="1.0.0")
            for asset in _release_local._FINAL_RELEASE_ASSETS
        )
        with patch.object(
            _release_local.subprocess,
            "run",
            return_value=SimpleNamespace(stdout=asset_names),
        ) as view, patch.object(_release_local, "run") as run:
            _release_local.finalize_release(
                "v1.0.0",
                "Project-Maho/release",
                dry_run=False,
            )

        self.assertEqual(
            view.call_args.args[0][-3:],
            ["assets", "--jq", ".assets[].name"],
        )
        run.assert_called_once_with(
            [
                "gh",
                "release",
                "edit",
                "v1.0.0",
                "--repo",
                "Project-Maho/release",
                "--draft=false",
                "--latest",
            ],
            False,
        )

    def test_windows_bundle_requires_staged_cli(self) -> None:
        args = SimpleNamespace(
            skip_build=True,
            dry_run=False,
            out_dir="out/Release",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            output_dir = os.path.join(temp_dir, "out", "Release")
            os.makedirs(output_dir)
            with open(
                os.path.join(output_dir, "MahoSetup-1.0.0.exe"),
                "wb",
            ):
                pass
            with patch.object(_release_local, "_CHROMIUM_SRC", temp_dir):
                with self.assertRaisesRegex(SystemExit, "maho.exe"):
                    _release_local.build_and_bundle_win("1.0.0", args)


class TestWindowsInstallerOverlay(unittest.TestCase):

    def test_overlay_tracks_maho_cli_and_mail_helper_installer_entries(self) -> None:
        relative_path = "chrome/installer/mini_installer/chrome.release"
        original = (
            "chrome.exe: %(ChromeDir)s\\\n"
            "chrome_proxy.exe: %(ChromeDir)s\\\n"
            "*.*.*.*.manifest: %(VersionDir)s\\\n"
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            target = os.path.join(temp_dir, "chrome.release")
            with open(target, "w", encoding="utf-8") as file:
                file.write(original)

            changed, _ = _overrides.apply_replacements(
                _overrides.Path(target),
                _overrides.REPLACEMENTS[relative_path],
                relative_path=relative_path,
            )

            self.assertTrue(changed)
            with open(target, encoding="utf-8") as file:
                release = file.read()
                self.assertIn("maho.exe: %(ChromeDir)s\\\n", release)
                self.assertIn(
                    "maho_mail_helper.exe: %(ChromeDir)s\\\n", release)


class TestLinuxRuntimeStaging(unittest.TestCase):

    def test_release_build_forwards_selected_output_directory(self) -> None:
        with patch.object(_linux_packages.subprocess, "run") as run:
            _linux_packages.run_build(
                skip_rust=True,
                skip_chromium=False,
                release=True,
                out_dir="out/Default",
            )

        self.assertEqual(
            run.call_args.args[0],
            [
                _linux_packages.sys.executable,
                _linux_packages._BUILD_MAHO_PY,
                "--platform",
                "linux",
                "--out-dir",
                "out/Default",
                "--skip-rust",
                "--release",
                "--force-args-template",
            ],
        )

    def test_copies_emitted_runtime_libraries_and_data(self) -> None:
        required_files = [
            "chrome",
            "maho_mail_helper",
            "maho",
            "resources.pak",
            "chrome_100_percent.pak",
            "chrome_200_percent.pak",
            "icudtl.dat",
            "v8_context_snapshot.bin",
            "chrome_crashpad_handler",
            "chrome_management_service",
            "chrome_sandbox",
            "libEGL.so",
            "libGLESv2.so",
            "libvulkan.so.1",
            "libvk_swiftshader.so",
            "vk_swiftshader_icd.json",
        ]
        directories = [
            "locales",
            "MEIPreload",
            "PrivacySandboxAttestationsPreloaded",
        ]

        with tempfile.TemporaryDirectory() as temp_dir:
            source_dir = os.path.join(temp_dir, "source")
            destination_dir = os.path.join(temp_dir, "destination")
            os.makedirs(source_dir)
            for filename in required_files:
                with open(os.path.join(source_dir, filename), "wb") as file:
                    file.write(filename.encode("utf-8"))
            for dirname in directories:
                directory = os.path.join(source_dir, dirname)
                os.makedirs(directory)
                with open(os.path.join(directory, "sentinel"), "w") as file:
                    file.write(dirname)

            _linux_packages.copy_browser_files(destination_dir, source_dir)

            for filename in required_files:
                expected_name = (
                    "chrome-sandbox"
                    if filename == "chrome_sandbox"
                    else filename
                )
                self.assertTrue(
                    os.path.isfile(os.path.join(destination_dir, expected_name)),
                    filename,
                )
            helper_path = os.path.join(destination_dir, "maho_mail_helper")
            self.assertTrue(os.access(helper_path, os.X_OK))
            for dirname in directories:
                self.assertTrue(
                    os.path.isfile(os.path.join(destination_dir, dirname, "sentinel")),
                    dirname,
                )

    def test_package_launchers_keep_cli_and_browser_commands_distinct(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            bin_dir = os.path.join(temp_dir, "usr", "bin")
            runtime_dir = os.path.join(temp_dir, "usr", "lib", "maho")
            os.makedirs(runtime_dir)
            with open(os.path.join(runtime_dir, "maho"), "wb") as file:
                file.write(b"cli")

            _linux_packages.write_package_launchers(bin_dir, runtime_dir)

            cli_launcher = os.path.join(bin_dir, "maho")
            browser_launcher = os.path.join(bin_dir, "maho-browser")
            self.assertTrue(os.path.samefile(cli_launcher, os.path.join(runtime_dir, "maho")))
            with open(browser_launcher, encoding="utf-8") as file:
                self.assertEqual(
                    file.read(),
                    "#!/bin/sh\nexec /usr/lib/maho/chrome \"$@\"\n",
                )

    def test_tarball_browser_launcher_executes_its_sibling_chrome(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            _linux_packages.write_tarball_browser_launcher(temp_dir)

            with open(
                os.path.join(temp_dir, "maho-browser"), encoding="utf-8"
            ) as file:
                self.assertEqual(
                    file.read(),
                    "#!/bin/sh\nexec \"$(dirname \"$0\")/chrome\" \"$@\"\n",
                )

    @patch.object(_linux_packages.subprocess, "run")
    @patch.object(_linux_packages.shutil, "which", return_value="gpg")
    def test_release_signature_is_verified_against_committed_public_key(
        self, which, run
    ) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact = os.path.join(temp_dir, "maho_1.0.0_amd64.deb")
            with open(artifact, "wb") as file:
                file.write(b"package")

            self.assertTrue(_linux_packages.sign_file(artifact, required=True))

        commands = [call.args[0] for call in run.call_args_list]
        self.assertTrue(any("--import" in command for command in commands))
        self.assertTrue(any("--verify" in command for command in commands))

    @patch.object(_linux_packages.shutil, "which", return_value=None)
    def test_release_signature_fails_closed_without_gpg(self, which) -> None:
        with self.assertRaisesRegex(RuntimeError, "gpg.*cannot sign"):
            _linux_packages.sign_file("/tmp/maho_1.0.0_amd64.deb", required=True)

    def test_final_release_requires_signed_linux_tarball(self) -> None:
        self.assertIn(
            "maho-{version}-x86_64.tar.gz.sig",
            _release_local._FINAL_RELEASE_ASSETS,
        )

    def test_release_workflow_packages_x64_linux_release_outputs(self) -> None:
        workflow_path = os.path.join(
            _linux_packages._WORKSPACE_ROOT,
            ".github",
            "workflows",
            "release.yml",
        )
        with open(workflow_path, encoding="utf-8") as file:
            workflow = file.read()

        self.assertIn("--format all --release", workflow)
        self.assertIn("chromium/src/out/Linux-tarball/*.tar.gz", workflow)
        self.assertIn("chromium/src/out/Linux-deb/*.deb.sig", workflow)
        self.assertNotIn("linux-arm64", workflow)


if __name__ == "__main__":
    unittest.main()
