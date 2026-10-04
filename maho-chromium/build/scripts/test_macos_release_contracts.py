#!/usr/bin/env python3
import importlib.util
import os
import plistlib
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock, call, patch


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))


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


_verify = _load_module('verify_macos_release.py', 'verify_macos_release_under_test')
_release_local = _load_module('release_local.py', 'release_local_macos_under_test')


def _completed(
    command,
    *,
    returncode: int = 0,
    stdout: str = '',
    stderr: str = '',
):
    return subprocess.CompletedProcess(command, returncode, stdout, stderr)


class MacBundleFixture:
    def __init__(self, root: str, executable_mode: int = 0o755):
        self.app = Path(root) / 'Maho.app'
        macos = self.app / 'Contents' / 'MacOS'
        macos.mkdir(parents=True)
        self.executable = macos / 'Maho'
        self.executable.write_bytes(b'mach-o')
        self.executable.chmod(executable_mode)

        info = {
            'CFBundleExecutable': 'Maho',
            'CFBundleIdentifier': _verify.EXPECTED_BUNDLE_ID,
            'CFBundlePackageType': 'APPL',
        }
        with (self.app / 'Contents' / 'Info.plist').open('wb') as info_file:
            plistlib.dump(info, info_file)

        self.framework = (
            self.app
            / 'Contents'
            / 'Frameworks'
            / 'Maho Framework.framework'
            / 'Versions'
            / 'Current'
            / 'Maho Framework'
        )
        self.framework.parent.mkdir(parents=True)
        self.framework.write_bytes(
            b'framework ffmpeg_video_decoder ffmpeg_audio_decoder avcodec_open2'
        )
        self.framework.chmod(0o755)
        (self.app / 'Contents' / 'Frameworks' / 'Sparkle.framework').mkdir()


class TestMacBundleValidation(unittest.TestCase):

    def test_media_capabilities_rejects_missing_ffmpeg_markers(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            fixture.framework.write_bytes(b'stripped framework without ffmpeg')
            with self.assertRaisesRegex(
                _verify.VerificationError,
                'missing mandatory FFmpeg media decoder symbols',
            ):
                _verify._verify_media_capabilities(fixture.app)

    def test_metadata_requires_executable_permission(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir, executable_mode=0o644)
            with self.assertRaisesRegex(
                _verify.VerificationError,
                'bundle executable is not executable',
            ):
                _verify._verify_bundle_metadata(fixture.app, 'arm64')

    def test_metadata_requires_expected_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            with patch.object(
                _verify,
                '_run',
                return_value=_completed(['lipo'], stdout='x86_64\n'),
            ):
                with self.assertRaisesRegex(
                    _verify.VerificationError,
                    'does not contain arm64',
                ):
                    _verify._verify_bundle_metadata(fixture.app, 'arm64')

    def test_metadata_verifies_product_version(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            with patch.object(
                _verify,
                '_run',
                return_value=_completed(['lipo'], stdout='arm64\n'),
            ):
                with self.assertRaisesRegex(
                    _verify.VerificationError,
                    'unexpected CFBundleShortVersionString',
                ):
                    _verify._verify_bundle_metadata(fixture.app, 'arm64', expected_version='2026.9.27')

                with (fixture.app / 'Contents' / 'Info.plist').open('wb') as f:
                    plistlib.dump({
                        'CFBundleExecutable': 'Maho',
                        'CFBundleIdentifier': _verify.EXPECTED_BUNDLE_ID,
                        'CFBundlePackageType': 'APPL',
                        'CFBundleShortVersionString': '2026.9.27',
                        'CFBundleVersion': '2026.9.27',
                    }, f)
                _verify._verify_bundle_metadata(fixture.app, 'arm64', expected_version='2026.9.27')

    def test_signature_rejects_apple_development_identity(self) -> None:
        verify_result = _completed(['codesign'], stdout='', stderr='')
        details = _completed(
            ['codesign'],
            stderr=(
                'Identifier=com.maho.browser\n'
                'Authority=Apple Development: Example (TEAMID)\n'
                'TeamIdentifier=TEAMID\n'
            ),
        )
        with patch.object(_verify, '_run', side_effect=[verify_result, details]):
            with self.assertRaisesRegex(
                _verify.VerificationError,
                'Developer ID Application',
            ):
                _verify._verify_code_signature(Path('/fake/Maho.app'))

    def test_signature_accepts_developer_id_with_team_identifier(self) -> None:
        verify_result = _completed(['codesign'])
        details = _completed(
            ['codesign'],
            stderr=(
                'Identifier=com.maho.browser\n'
                'Authority=Developer ID Application: Example (TEAMID)\n'
                'Authority=Developer ID Certification Authority\n'
                'TeamIdentifier=TEAMID\n'
            ),
        )
        with patch.object(_verify, '_run', side_effect=[verify_result, details]):
            _verify._verify_code_signature(Path('/fake/Maho.app'))

    def test_runtime_dependencies_reject_build_tree_absolute_dylib(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            system_only = (
                f'{fixture.executable}:\n'
                '\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n'
            )
            build_tree = (
                f'{fixture.framework}:\n'
                '\t/Users/runner/chromium/src/out/Default/libbad.dylib '
                '(compatibility version 1.0.0)\n'
            )
            with patch.object(
                _verify,
                '_run',
                side_effect=[
                    _completed(['otool'], stdout=system_only),
                    _completed(['otool'], stdout=build_tree),
                ],
            ):
                with self.assertRaisesRegex(
                    _verify.VerificationError,
                    'non-system absolute dylib dependency',
                ):
                    _verify._verify_runtime_dependencies(
                        fixture.app,
                        fixture.executable,
                    )

    def test_runtime_dependencies_accept_system_and_bundled_sparkle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            main_output = (
                f'{fixture.executable}:\n'
                '\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n'
            )
            framework_output = (
                f'{fixture.framework}:\n'
                '\t@rpath/Sparkle.framework/Versions/B/Sparkle '
                '(compatibility version 1.6.0)\n'
                '\t/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit '
                '(compatibility version 45.0.0)\n'
            )
            with patch.object(
                _verify,
                '_run',
                side_effect=[
                    _completed(['otool'], stdout=main_output),
                    _completed(['otool'], stdout=framework_output),
                ],
            ):
                _verify._verify_runtime_dependencies(
                    fixture.app,
                    fixture.executable,
                )

    def test_quarantine_is_allowed_but_finderinfo_is_rejected(self) -> None:
        app = Path('/fake/Maho.app')
        with patch.object(
            _verify,
            '_run',
            return_value=_completed(
                ['xattr'],
                stdout='Maho.app: com.apple.quarantine: 0083;...\n',
            ),
        ):
            _verify._verify_xattrs(app)

        with patch.object(
            _verify,
            '_run',
            return_value=_completed(
                ['xattr'],
                stdout='Maho.app: com.apple.FinderInfo: 0000\n',
            ),
        ):
            with self.assertRaisesRegex(
                _verify.VerificationError,
                'com.apple.FinderInfo',
            ):
                _verify._verify_xattrs(app)

    def test_stapler_validation_accepts_stapled_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            with patch.object(_verify, '_verify_bundle_metadata', return_value=fixture.executable), \
                 patch.object(_verify, '_verify_code_signature'), \
                 patch.object(_verify, '_verify_runtime_dependencies'), \
                 patch.object(_verify, '_verify_xattrs'), \
                 patch.object(_verify, '_run') as mock_run:
                mock_run.return_value = _completed(['xcrun', 'stapler', 'validate'])
                _verify.verify_app_bundle(
                    fixture.app,
                    expected_arch='arm64',
                    require_gatekeeper=False,
                    require_stapled=True,
                )
                self.assertTrue(
                    any(c.args[0][:3] == ['xcrun', 'stapler', 'validate']
                        for c in mock_run.call_args_list)
                )

    def test_stapler_validation_rejects_unstapled_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            fixture = MacBundleFixture(temp_dir)
            with patch.object(_verify, '_verify_bundle_metadata', return_value=fixture.executable), \
                 patch.object(_verify, '_verify_code_signature'), \
                 patch.object(_verify, '_verify_runtime_dependencies'), \
                 patch.object(_verify, '_verify_xattrs'), \
                 patch.object(
                     _verify,
                     '_run',
                     side_effect=_verify.VerificationError('bundle does not have a ticket stapled to it'),
                 ):
                with self.assertRaisesRegex(
                    _verify.VerificationError,
                    'does not have a ticket stapled',
                ):
                    _verify.verify_app_bundle(
                        fixture.app,
                        expected_arch='arm64',
                        require_gatekeeper=False,
                        require_stapled=True,
                    )


class TestDmgReleaseValidation(unittest.TestCase):

    def test_detaches_dmg_when_mounted_app_validation_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            dmg = Path(temp_dir) / 'Maho-1.0.0.dmg'
            dmg.write_bytes(b'dmg')
            commands = []

            def fake_run(command, **kwargs):
                commands.append(command)
                return _completed(command)

            with patch.object(_verify, '_run', side_effect=fake_run), patch.object(
                _verify,
                'verify_app_bundle',
                side_effect=_verify.VerificationError('bad mounted app'),
            ):
                with self.assertRaisesRegex(
                    _verify.VerificationError,
                    'bad mounted app',
                ):
                    _verify.verify_release_dmg(dmg)

            self.assertTrue(
                any(command[:2] == ['hdiutil', 'detach'] for command in commands),
                'mounted release DMG must always detach on validation failure',
            )

    def test_gatekeeper_failure_is_fatal_before_mount(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            dmg = Path(temp_dir) / 'Maho-1.0.0.dmg'
            dmg.write_bytes(b'dmg')
            commands = []

            def fake_run(command, **kwargs):
                commands.append(command)
                if command[:4] == ['spctl', '-a', '-t', 'open']:
                    raise _verify.VerificationError('Gatekeeper rejected dmg')
                return _completed(command)

            with patch.object(_verify, '_run', side_effect=fake_run):
                with self.assertRaisesRegex(
                    _verify.VerificationError,
                    'Gatekeeper rejected dmg',
                ):
                    _verify.verify_release_dmg(dmg)

            self.assertFalse(
                any(command[:2] == ['hdiutil', 'attach'] for command in commands),
                'a DMG rejected by Gatekeeper must not advance to mounted-app validation',
            )


class TestMacReleaseEntrypoints(unittest.TestCase):

    def test_local_macos_release_always_notarizes_then_verifies(self) -> None:
        args = SimpleNamespace(
            skip_build=False,
            dry_run=True,
            out_dir='out/Release',
        )
        with patch.object(_release_local, 'run') as run, patch.object(
            _release_local,
            'verify_macos_release_artifact',
        ) as verify:
            _release_local.build_and_bundle_mac('1.0.0', args)

        build_command = run.call_args_list[0].args[0]
        self.assertIn('--release', build_command)
        self.assertIn('--notarize', build_command)
        self.assertIn('--no-launch', build_command)
        verify.assert_called_once_with(
            os.path.join(
                _release_local._CHROMIUM_SRC,
                'out/Release',
                'Maho-1.0.0.dmg',
            ),
            True,
        )
        self.assertEqual(
            run.call_args_list[1].args[0][1],
            _release_local._GEN_MANIFESTS,
            'manifest generation must happen only after the release verifier',
        )

    def test_skip_build_still_runs_release_verifier(self) -> None:
        args = SimpleNamespace(
            skip_build=True,
            dry_run=True,
            out_dir='out/Release',
        )
        order = []

        def record_run(*args, **kwargs):
            order.append('manifest')
            return 0

        def record_verify(*args, **kwargs):
            order.append('verify')
            return 0

        with patch.object(_release_local, 'run', side_effect=record_run), patch.object(
            _release_local,
            'verify_macos_release_artifact',
            side_effect=record_verify,
        ):
            _release_local.build_and_bundle_mac('1.0.0', args)

        self.assertEqual(order, ['verify', 'manifest'])

    def test_main_release_workflow_imports_developer_id_and_notarizes(self) -> None:
        workflow_path = os.path.join(
            _WORKSPACE_ROOT,
            '.github',
            'workflows',
            'release.yml',
        )
        with open(workflow_path, encoding='utf-8') as workflow_file:
            workflow = workflow_file.read()

        self.assertIn('Import Developer ID certificate', workflow)
        self.assertIn('MACOS_CERT_P12: ${{ secrets.MACOS_CERT_P12 }}', workflow)
        self.assertIn('MACOS_CERT_PASSWORD: ${{ secrets.MACOS_CERT_PASSWORD }}', workflow)
        self.assertIn('APPLE_API_KEY: ${{ secrets.APPLE_API_KEY }}', workflow)
        self.assertIn('APPLE_API_KEY_ID: ${{ secrets.APPLE_API_KEY_ID }}', workflow)
        self.assertIn('APPLE_API_ISSUER: ${{ secrets.APPLE_API_ISSUER }}', workflow)
        self.assertIn('--release --notarize --no-launch', workflow)
        self.assertIn('verify_macos_release.py', workflow)
        self.assertIn('Clean up signing keychain', workflow)


if __name__ == '__main__':
    unittest.main()
