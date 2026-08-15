#!/usr/bin/env python3

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch


_SCRIPT_DIR = Path(__file__).resolve().parent
_SCRIPT_PATH = _SCRIPT_DIR / "package_windows_msix.py"
_SPEC = importlib.util.spec_from_file_location("package_windows_msix", _SCRIPT_PATH)
assert _SPEC and _SPEC.loader
msix = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(msix)


class PackageWindowsMsixTest(unittest.TestCase):
    def test_normalized_msix_version_adds_missing_components(self) -> None:
        self.assertEqual(msix.normalized_msix_version("1.2.3"), "1.2.3.0")
        self.assertEqual(msix.normalized_msix_version("7"), "7.0.0.0")
        with self.assertRaises(ValueError):
            msix.normalized_msix_version("1.beta")

    def test_stage_runtime_files_uses_chrome_release_destinations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            input_dir = root / "out"
            input_dir.mkdir()
            (input_dir / "chrome.exe").write_bytes(b"chrome")
            (input_dir / "maho.exe").write_bytes(b"maho")
            (input_dir / "maho_mail_helper.exe").write_bytes(b"helper")
            (input_dir / "chrome.dll").write_bytes(b"dll")
            (input_dir / "resources").mkdir()
            (input_dir / "resources" / "maho.pak").write_bytes(b"resource")
            release_file = root / "chrome.release"
            release_file.write_text(
                "\n".join(
                    (
                        "[GENERAL]",
                        "chrome.exe: %(ChromeDir)s\\",
                        "maho.exe: %(ChromeDir)s\\",
                        "maho_mail_helper.exe: %(ChromeDir)s\\",
                        "chrome.dll: %(VersionDir)s\\",
                        "resources\\*.pak: %(ChromeDir)s\\resources\\",
                    )
                ),
                encoding="utf-8",
            )
            staging_dir = root / "stage"
            copied = msix.stage_runtime_files(input_dir, release_file, staging_dir)

            self.assertEqual(len(copied), 5)
            self.assertEqual((staging_dir / "chrome.exe").read_bytes(), b"chrome")
            self.assertEqual((staging_dir / "maho.exe").read_bytes(), b"maho")
            self.assertEqual(
                (staging_dir / "maho_mail_helper.exe").read_bytes(), b"helper"
            )
            self.assertEqual((staging_dir / "chrome.dll").read_bytes(), b"dll")
            self.assertEqual(
                (staging_dir / "resources" / "maho.pak").read_bytes(), b"resource"
            )

    def test_stage_runtime_files_requires_maho_cli(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            input_dir = root / "out"
            input_dir.mkdir()
            (input_dir / "chrome.exe").write_bytes(b"chrome")
            release_file = root / "chrome.release"
            release_file.write_text(
                "[GENERAL]\nchrome.exe: %(ChromeDir)s\\\n", encoding="utf-8"
            )

            with self.assertRaises(FileNotFoundError):
                msix.stage_runtime_files(input_dir, release_file, root / "stage")

    def test_stage_runtime_files_requires_mail_helper(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            input_dir = root / "out"
            input_dir.mkdir()
            (input_dir / "chrome.exe").write_bytes(b"chrome")
            (input_dir / "maho.exe").write_bytes(b"maho")
            (input_dir / "chrome.dll").write_bytes(b"dll")
            release_file = root / "chrome.release"
            release_file.write_text(
                "\n".join(
                    (
                        "[GENERAL]",
                        "chrome.exe: %(ChromeDir)s\\",
                        "maho.exe: %(ChromeDir)s\\",
                        "chrome.dll: %(VersionDir)s\\",
                    )
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(FileNotFoundError, "maho_mail_helper.exe"):
                msix.stage_runtime_files(input_dir, release_file, root / "stage")

    def test_stage_runtime_files_supplies_chrome_alias_for_runtime_lookups(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            input_dir = root / "out"
            input_dir.mkdir()
            (input_dir / "maho.exe").write_bytes(b"maho")
            (input_dir / "maho_mail_helper.exe").write_bytes(b"helper")
            (input_dir / "chrome.dll").write_bytes(b"dll")
            release_file = root / "chrome.release"
            release_file.write_text(
                "\n".join(
                    (
                        "[GENERAL]",
                        "maho.exe: %(ChromeDir)s\\",
                        "maho_mail_helper.exe: %(ChromeDir)s\\",
                        "chrome.dll: %(VersionDir)s\\",
                    )
                ),
                encoding="utf-8",
            )

            copied = msix.stage_runtime_files(input_dir, release_file, root / "stage")

            self.assertEqual(len(copied), 4)
            self.assertEqual((root / "stage" / "chrome.exe").read_bytes(), b"maho")

    def test_parse_release_entries_excludes_optional_sections(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            release_file = Path(temporary_dir) / "chrome.release"
            release_file.write_text(
                "\n".join(
                    (
                        "[GENERAL]",
                        "maho.exe: %(ChromeDir)s\\",
                        "[HIDPI]",
                        "chrome_200_percent.pak: %(VersionDir)s\\",
                    )
                ),
                encoding="utf-8",
            )

            self.assertEqual(
                msix.parse_release_entries(release_file),
                [
                    ("maho.exe", "%(ChromeDir)s\\", True),
                    ("chrome_200_percent.pak", "%(VersionDir)s\\", False),
                ],
            )

    def test_stage_runtime_files_ignores_absent_optional_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            input_dir = root / "out"
            input_dir.mkdir()
            (input_dir / "maho.exe").write_bytes(b"maho")
            (input_dir / "maho_mail_helper.exe").write_bytes(b"helper")
            (input_dir / "chrome.dll").write_bytes(b"dll")
            release_file = root / "chrome.release"
            release_file.write_text(
                "\n".join(
                    (
                        "[GENERAL]",
                        "maho.exe: %(ChromeDir)s\\",
                        "maho_mail_helper.exe: %(ChromeDir)s\\",
                        "chrome.dll: %(VersionDir)s\\",
                        "[HIDPI]",
                        "chrome_200_percent.pak: %(VersionDir)s\\",
                    )
                ),
                encoding="utf-8",
            )

            msix.stage_runtime_files(input_dir, release_file, root / "stage")

            self.assertFalse(
                (root / "stage" / "chrome_200_percent.pak").exists()
            )

    @patch.dict("sys.modules", {"PIL": MagicMock()})
    def test_stage_visual_assets_generates_required_manifest_assets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            msix.stage_visual_assets(Path(temporary_dir))

        pil_image = sys.modules["PIL"].Image
        self.assertEqual(pil_image.new.call_count, 4)

    @patch.object(msix, "run")
    @patch.object(msix.shutil, "which", return_value="powershell.exe")
    @patch.object(msix, "is_windows_host", return_value=True)
    @patch.dict("sys.modules", {"PIL": None})
    def test_stage_visual_assets_uses_windows_native_resizer_without_pillow(
        self, is_windows_host, which, run
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            msix.stage_visual_assets(Path(temporary_dir))

        self.assertEqual(run.call_count, 4)
        self.assertIn("System.Drawing", run.call_args.args[0][-1])

    def test_write_manifest_uses_full_trust_chrome_entry_point(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_dir:
            manifest = msix.write_manifest(
                Path(temporary_dir),
                package_name="MahoBrowser",
                publisher="CN=Maho Development",
                version="1.0.0",
                architecture="x64",
            )
            content = manifest.read_text(encoding="utf-8")

            self.assertIn('Executable="chrome.exe"', content)
            self.assertIn('EntryPoint="Windows.FullTrustApplication"', content)
            self.assertIn('Name="runFullTrust"', content)
            self.assertIn('Version="1.0.0.0"', content)

    @patch.object(msix, "run")
    @patch.object(msix, "find_windows_sdk_tool")
    def test_pack_sign_and_install_signs_before_install(
        self, find_tool, run
    ) -> None:
        find_tool.side_effect = ("makeappx.exe", "signtool.exe")
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir)
            msix.pack_sign_and_install(
                staging_dir=root / "stage",
                output=root / "Maho.msix",
                certificate=root / "Maho.pfx",
                certificate_password="secret",
                install=False,
            )

        self.assertEqual(run.call_args_list[0].args[0][1], "pack")
        self.assertEqual(run.call_args_list[1].args[0][1], "sign")

    @patch.object(msix, "run")
    @patch.object(msix.shutil, "which", return_value="powershell.exe")
    def test_verify_installed_package_subscribes_before_activation(
        self, which, run
    ) -> None:
        msix.verify_installed_package(
            package_name="MahoBrowser",
            expected_executable="chrome.exe",
            launch=True,
        )

        command = run.call_args.args[0]
        script = command[-1]
        self.assertEqual(command[0], "powershell.exe")
        self.assertIn("Register-WmiEvent", script)
        self.assertLess(
            script.index("Register-WmiEvent"),
            script.index("Start-Process explorer.exe"),
        )
        self.assertIn("Wait-Event -SourceIdentifier MahoChromeStarted", script)
        self.assertIn("Unregister-Event", script)
        self.assertIn("Get-CimInstance Win32_Process", script)
        self.assertIn("$package.InstallLocation", script)


if __name__ == "__main__":
    unittest.main()
