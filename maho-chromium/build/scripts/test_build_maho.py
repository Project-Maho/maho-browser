#!/usr/bin/env python3

import importlib.util
import os
import plistlib
import re
import subprocess
import sys
import tempfile
import types
import unittest
from contextlib import ExitStack
from typing import Any
from unittest.mock import MagicMock, patch


# ---------------------------------------------------------------------------
# Helpers to import build_maho without executing the real load_module calls
# that require the workspace tree to exist on disk.
# ---------------------------------------------------------------------------

def _load_build_maho() -> Any:
    _mod_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build_maho.py')
    _orig_spec = importlib.util.spec_from_file_location
    _orig_mfs = importlib.util.module_from_spec

    stub_lock_mod: Any = types.ModuleType('maho_build_lock')
    stub_lock_mod.build_lock = MagicMock()

    stub_core_mod: Any = types.ModuleType('maho_build_maho_core')
    stub_core_mod.build_rust_prebuilt = MagicMock()

    def _fake_spec(name: str, path: Any, **kw: Any) -> Any:
        if isinstance(path, str) and 'build_lock' in path:
            fake: Any = MagicMock()
            fake.loader.exec_module = lambda m: None
            return fake
        if isinstance(path, str) and 'build_maho_core' in path:
            fake = MagicMock()
            fake.loader.exec_module = lambda m: None
            return fake
        return _orig_spec(name, path, **kw)

    def _fake_mfs(spec: Any) -> Any:
        name = getattr(spec, 'name', '')
        if 'build_lock' in str(name):
            return stub_lock_mod
        if 'build_maho_core' in str(name):
            return stub_core_mod
        return _orig_mfs(spec)

    with patch('importlib.util.spec_from_file_location', side_effect=_fake_spec), \
         patch('importlib.util.module_from_spec', side_effect=_fake_mfs):
        real_spec = _orig_spec('build_maho_under_test', _mod_path)
        assert real_spec is not None
        mod: Any = _orig_mfs(real_spec)
        real_spec.loader.exec_module(mod)  # type: ignore[union-attr]

    mod.build_lock = stub_lock_mod.build_lock
    mod.build_maho_core = stub_core_mod
    return mod

_bm = _load_build_maho()
find_codesign_identity = _bm.find_codesign_identity
codesign_app = _bm.codesign_app
launch_app = _bm.launch_app
normalize_autoninja_args = _bm.normalize_autoninja_args
ensure_args_gn = _bm.ensure_args_gn
ensure_macos_sdk_path = _bm.ensure_macos_sdk_path


class TestSetAppProductVersion(unittest.TestCase):

    def test_updates_cfbundle_versions_to_product_version(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_path = os.path.join(temp_dir, 'Maho.app')
            contents_dir = os.path.join(app_path, 'Contents')
            os.makedirs(contents_dir)
            plist_path = os.path.join(contents_dir, 'Info.plist')
            initial_plist = {
                'CFBundleExecutable': 'Maho',
                'CFBundleShortVersionString': '149.0.7795.0',
                'CFBundleVersion': '7795.0',
            }
            with open(plist_path, 'wb') as fp:
                plistlib.dump(initial_plist, fp)

            _bm.set_bundle_product_version(app_path, '2026.9.27')

            with open(plist_path, 'rb') as fp:
                updated = plistlib.load(fp)

            self.assertEqual(updated.get('CFBundleShortVersionString'), '2026.9.27')
            self.assertEqual(updated.get('CFBundleVersion'), '2026.9.27')


class TestFindCodesignIdentity(unittest.TestCase):

    def _make_result(self, stdout):
        r = MagicMock()
        r.stdout = stdout
        r.returncode = 0
        return r

    def test_returns_sha_from_valid_line(self):
        output = (
            '  1) 083B7F04ED95D8E9B5B0249809819945A905C75B "Apple Development: Indo Yoon (X5A8FG8987)"\n'
            '     1 valid identities found\n'
        )
        with patch('subprocess.run', return_value=self._make_result(output)):
            result = find_codesign_identity()
        self.assertEqual(result, '083B7F04ED95D8E9B5B0249809819945A905C75B')

    def test_returns_first_when_multiple(self):
        output = (
            '  1) AAAA0000000000000000000000000000000000AA "Apple Development: First"\n'
            '  2) BBBB0000000000000000000000000000000000BB "Apple Development: Second"\n'
        )
        with patch('subprocess.run', return_value=self._make_result(output)):
            result = find_codesign_identity()
        self.assertEqual(result, 'AAAA0000000000000000000000000000000000AA')

    def test_release_prefers_developer_id_application(self):
        output = (
            '  1) AAAA0000000000000000000000000000000000AA "Apple Development: Test"\n'
            '  2) BBBB0000000000000000000000000000000000BB "Apple Distribution: Test"\n'
            '  3) CCCC0000000000000000000000000000000000CC "Developer ID Application: Test"\n'
        )
        with patch('subprocess.run', return_value=self._make_result(output)):
            result = find_codesign_identity(release=True)
        self.assertEqual(result, 'CCCC0000000000000000000000000000000000CC')

    def test_uppercases_sha(self):
        output = '  1) aaaa7f04ed95d8e9b5b0249809819945a905c75b "Apple Development: Test"\n'
        with patch('subprocess.run', return_value=self._make_result(output)):
            result = find_codesign_identity()
        self.assertEqual(result, 'AAAA7F04ED95D8E9B5B0249809819945A905C75B')

    def test_returns_none_when_no_valid_identities(self):
        output = '     0 valid identities found\n'
        with patch('subprocess.run', return_value=self._make_result(output)):
            result = find_codesign_identity()
        self.assertIsNone(result)

    def test_returns_none_on_file_not_found(self):
        with patch('subprocess.run', side_effect=FileNotFoundError):
            result = find_codesign_identity()
        self.assertIsNone(result)

    def test_returns_none_on_timeout(self):
        with patch('subprocess.run', side_effect=subprocess.TimeoutExpired(cmd='security', timeout=10)):
            result = find_codesign_identity()
        self.assertIsNone(result)

    def test_ignores_lines_without_matching_format(self):
        output = (
            'Policy: Code Signing\n'
            '  Matching identities\n'
            '     0 valid identities found\n'
        )
        with patch('subprocess.run', return_value=self._make_result(output)):
            result = find_codesign_identity()
        self.assertIsNone(result)


class TestCodesignApp(unittest.TestCase):

    def test_signs_existing_bundle_no_sparkle(self):
        def _isdir(path):
            if path == '/fake/Maho.app':
                return True
            return False

        with patch('os.path.isdir', side_effect=_isdir), \
             patch('os.path.isfile', return_value=False), \
             patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0)
            result = codesign_app('DEADBEEF', '/fake/Maho.app')
        self.assertTrue(result)
        mock_run.assert_called_once_with(
            ['codesign', '--force', '--sign', 'DEADBEEF', '--options', 'runtime', '/fake/Maho.app'],
            check=True,
            timeout=120,
        )

    def test_signs_top_level_crashpad_handler_for_notarization(self):
        crashpad_handler = '/fake/Maho.app/Contents/MacOS/chrome_crashpad_handler'

        with patch('os.path.isdir', side_effect=lambda path: path == '/fake/Maho.app'), \
             patch('os.path.isfile', side_effect=lambda path: path == crashpad_handler), \
             patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0)
            result = codesign_app('DEADBEEF', '/fake/Maho.app')

        self.assertTrue(result)
        self.assertIn(
            ['codesign', '--force', '--sign', 'DEADBEEF', '--options', 'runtime',
             '--timestamp', crashpad_handler],
            [call.args[0] for call in mock_run.call_args_list],
        )

    def test_signs_existing_bundle_with_sparkle(self):
        app_path = '/fake/Maho.app'
        framework_path = f'{app_path}/Contents/Frameworks/Maho Framework.framework'
        sparkle_path = f'{app_path}/Contents/Frameworks/Sparkle.framework'
        with patch(
            'os.path.isdir',
            side_effect=lambda path: path != (
                f'{framework_path}/Versions/Current/Libraries'
            ),
        ), \
             patch('os.path.isfile', return_value=True), \
             patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0)
            result = codesign_app('DEADBEEF', app_path)
        self.assertTrue(result)
        self.assertEqual(mock_run.call_count, 20)

    def test_signs_chromium_framework_before_outer_app(self):
        framework_path = (
            '/fake/Maho.app/Contents/Frameworks/Maho Framework.framework'
        )
        helper_path = (
            f'{framework_path}/Versions/Current/Helpers/Maho Helper.app'
        )

        with patch(
            'os.path.isdir',
            side_effect=lambda path: path in {
                '/fake/Maho.app',
                framework_path,
                helper_path,
            },
        ), \
             patch('os.path.isfile', return_value=False), \
             patch('subprocess.run') as mock_run:
            result = codesign_app('DEADBEEF', '/fake/Maho.app')

        self.assertTrue(result)
        self.assertEqual(
            [call.args[0][-1] for call in mock_run.call_args_list],
            [helper_path, framework_path, '/fake/Maho.app'],
        )
        helper_call = mock_run.call_args_list[0].args[0]
        self.assertTrue(
            helper_call[helper_call.index('--entitlements') + 1].endswith(
                'branding/mac/helper-entitlements.plist'
            )
        )

    def test_uses_separate_app_and_helper_entitlements(self):
        # Without a provisioning profile the app is signed with a stripped
        # temp copy of branding/mac/entitlements.plist (see
        # unrestricted_entitlements_path), so accept that temp path too.
        with patch.dict(os.environ, {}, clear=False), \
             patch(
                 'os.path.isdir',
                 side_effect=lambda path: path == '/fake/Maho.app',
             ), \
             patch(
                 'os.path.isfile',
                 side_effect=lambda path: path.endswith((
                     'maho_mail_helper',
                     'helper-entitlements.plist',
                     'entitlements.plist',
                 )) or os.path.basename(path).startswith('maho-entitlements-'),
             ), \
             patch('subprocess.run') as mock_run:
            os.environ.pop('MAHO_MAC_PROVISIONING_PROFILE', None)
            result = codesign_app('DEADBEEF', '/fake/Maho.app')

        self.assertTrue(result)
        helper_call, app_call = [call.args[0] for call in mock_run.call_args_list]
        self.assertTrue(
            helper_call[helper_call.index('--entitlements') + 1].endswith(
                'branding/mac/helper-entitlements.plist'
            )
        )
        app_entitlements = app_call[app_call.index('--entitlements') + 1]
        self.assertFalse(app_entitlements.endswith('helper-entitlements.plist'))
        if os.path.basename(app_entitlements).startswith('maho-entitlements-'):
            try:
                with open(app_entitlements, 'rb') as handle:
                    stripped = plistlib.load(handle)
            finally:
                os.remove(app_entitlements)
            self.assertNotIn('com.apple.application-identifier', stripped)
            self.assertNotIn('keychain-access-groups', stripped)
        else:
            self.assertTrue(
                app_entitlements.endswith('branding/mac/entitlements.plist'))

    def test_embeds_explicit_macos_provisioning_profile(self):
        with patch.dict(
            os.environ,
            {'MAHO_MAC_PROVISIONING_PROFILE': '/profiles/maho.provisionprofile'},
        ), \
             patch(
                 'os.path.isdir',
                 side_effect=lambda path: path == '/fake/Maho.app',
             ), \
             patch('os.path.isfile', return_value=True), \
             patch('shutil.copy2') as mock_copy, \
             patch('subprocess.run'):
            result = codesign_app('DEADBEEF', '/fake/Maho.app')

        self.assertTrue(result)
        mock_copy.assert_called_once_with(
            '/profiles/maho.provisionprofile',
            '/fake/Maho.app/Contents/embedded.provisionprofile',
        )

    def test_returns_false_when_bundle_missing(self):
        with patch('os.path.isdir', return_value=False):
            result = codesign_app('DEADBEEF', '/nonexistent/Maho.app')
        self.assertFalse(result)

    def test_returns_false_on_codesign_not_found(self):
        with patch('os.path.isdir', return_value=True), \
             patch('subprocess.run', side_effect=FileNotFoundError):
            result = codesign_app('DEADBEEF', '/fake/Maho.app')
        self.assertFalse(result)

    def test_returns_false_on_timeout(self):
        with patch('os.path.isdir', return_value=True), \
             patch('subprocess.run', side_effect=subprocess.TimeoutExpired(cmd='codesign', timeout=120)):
            result = codesign_app('DEADBEEF', '/fake/Maho.app')
        self.assertFalse(result)

    def test_returns_false_on_nonzero_exit(self):
        with patch('os.path.isdir', return_value=True), \
             patch('subprocess.run', side_effect=subprocess.CalledProcessError(1, 'codesign')):
            result = codesign_app('DEADBEEF', '/fake/Maho.app')
        self.assertFalse(result)

    def test_success_does_not_raise(self):
        with patch('os.path.isdir', side_effect=lambda path: path == '/fake/Maho.app'), \
             patch('os.path.isfile', return_value=False), \
             patch('subprocess.run', return_value=MagicMock(returncode=0)):
            self.assertTrue(codesign_app('MYIDENTITY', '/fake/Maho.app'))

    def test_signs_bundled_cli_before_outer_app(self):
        app_path = '/fake/Maho.app'
        cli_path = f'{app_path}/Contents/Helpers/maho'

        with patch('os.path.isdir', side_effect=lambda path: path == app_path), \
             patch('os.path.isfile', side_effect=lambda path: path == cli_path), \
             patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0)
            result = codesign_app('DEADBEEF', app_path)

        self.assertTrue(result)
        self.assertEqual(
            [call.args[0][-1] for call in mock_run.call_args_list],
            [cli_path, app_path],
        )
        self.assertIn(
            '--timestamp',
            mock_run.call_args_list[0].args[0],
        )


class TestEmbedMahoCli(unittest.TestCase):

    def test_copies_cli_to_helpers_directory_with_execute_permission(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = os.path.join(temp_dir, 'maho')
            app_path = os.path.join(temp_dir, 'Maho.app')
            with open(source, 'wb') as cli_file:
                cli_file.write(b'maho-cli')
            os.chmod(source, 0o755)

            self.assertTrue(_bm.embed_maho_cli(app_path, source))

            destination = os.path.join(
                app_path,
                'Contents',
                'Helpers',
                'maho',
            )
            self.assertTrue(os.path.isfile(destination))
            self.assertTrue(os.access(destination, os.X_OK))
            with open(destination, 'rb') as cli_file:
                self.assertEqual(cli_file.read(), b'maho-cli')

    def test_fails_closed_when_cli_artifact_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app_path = os.path.join(temp_dir, 'Maho.app')
            source = os.path.join(temp_dir, 'missing-maho')

            self.assertFalse(_bm.embed_maho_cli(app_path, source))
            self.assertFalse(os.path.exists(
                os.path.join(app_path, 'Contents', 'Helpers', 'maho'),
            ))


class TestVerifyOmoRuntime(unittest.TestCase):
    """The gate runs the real entry with Maho's RPC argv (python stands in for bun)."""

    def _entry(self, temp_dir: str, body: str) -> str:
        path = os.path.join(temp_dir, 'rpc-entry.py')
        with open(path, 'w') as f:
            f.write('import sys\n' + body)
        return path

    def test_accepts_runtime_that_reports_readiness(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            entry = self._entry(temp_dir, (
                "assert sys.argv[1] == '--listen'\n"
                "assert sys.argv[2].startswith('unix://')\n"
                "assert sys.argv[3] == '--no-builtin-tools'\n"
                "sys.stderr.write('senpi rpc listening on ' + sys.argv[2] + '\\n')\n"
                "sys.stderr.flush()\n"
                "sys.stdin.read()\n"))
            self.assertIsNone(_bm.verify_omo_runtime(sys.executable, entry))

    def test_rejects_runtime_that_exits_on_listen(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            entry = self._entry(temp_dir, (
                "sys.stderr.write('Error: Unknown option: --listen\\n')\n"
                "sys.exit(1)\n"))
            failure = _bm.verify_omo_runtime(sys.executable, entry)
            self.assertIsNotNone(failure)
            self.assertIn('status 1', failure)
            self.assertIn('Unknown option: --listen', failure)

    def test_embed_fails_closed_on_incompatible_package(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with patch.object(_bm.shutil, 'which', return_value='/usr/bin/bun'), \
                 patch.object(_bm, 'omo_runtime_source', return_value=temp_dir), \
                 patch.object(_bm.os.path, 'isfile', return_value=True), \
                 patch.object(_bm.os.path, 'getmtime', return_value=0), \
                 patch.object(_bm, 'verify_omo_runtime',
                              return_value='exited with status 1: x'), \
                 patch.object(_bm.shutil, 'copy2') as copy2:
                with self.assertRaises(RuntimeError):
                    _bm.embed_omo_runtime(
                        os.path.join(temp_dir, 'Maho.app'), 'out/Release')
                copy2.assert_not_called()

    def test_embed_stages_bridge_built_with_explicit_target_triple(self):
        # build_maho_cli() uses --target, so the only bridge on a release
        # builder lives under target/<triple>/release.
        with tempfile.TemporaryDirectory() as temp_dir:
            triple_dir = os.path.join(
                temp_dir, 'maho', 'target', 'aarch64-apple-darwin', 'release')
            os.makedirs(triple_dir)
            with open(os.path.join(triple_dir, 'maho-browser-mcp'), 'wb') as f:
                f.write(b'mcp')
            package = os.path.join(temp_dir, 'omo')
            os.makedirs(os.path.join(package, 'dist'))
            with open(os.path.join(package, 'dist', 'rpc-entry.js'), 'w') as f:
                f.write('')
            bun = os.path.join(temp_dir, 'bun')
            with open(bun, 'wb') as f:
                f.write(b'bun')
            app_path = os.path.join(temp_dir, 'Maho.app')
            with patch.object(_bm, '_WORKSPACE_ROOT', temp_dir), \
                 patch.object(_bm.shutil, 'which', return_value=bun), \
                 patch.object(_bm, 'omo_runtime_source', return_value=package), \
                 patch.object(_bm, 'verify_omo_runtime', return_value=None):
                self.assertTrue(_bm.embed_omo_runtime(app_path, 'out/Release'))
            with open(os.path.join(
                    app_path, 'Contents', 'MacOS', 'maho-browser-mcp'), 'rb') as f:
                self.assertEqual(f.read(), b'mcp')
            self.assertTrue(os.path.isfile(os.path.join(
                app_path, 'Contents', 'Resources', 'omo', 'dist', 'rpc-entry.js')))


class TestMahoCliBinaryName(unittest.TestCase):

    def test_windows_triple_gets_exe_suffix(self):
        self.assertEqual(_bm.maho_cli_binary_name('x86_64-pc-windows-msvc'), 'maho.exe')
        self.assertEqual(_bm.maho_cli_binary_name('aarch64-pc-windows-msvc'), 'maho.exe')

    def test_unix_triples_have_no_suffix(self):
        self.assertEqual(_bm.maho_cli_binary_name('aarch64-apple-darwin'), 'maho')
        self.assertEqual(_bm.maho_cli_binary_name('x86_64-unknown-linux-gnu'), 'maho')


class TestRequireWindowsMailHelper(unittest.TestCase):

    def test_returns_helper_beside_browser(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            helper = os.path.join(temp_dir, 'out', 'Default', 'maho_mail_helper.exe')
            os.makedirs(os.path.dirname(helper))
            with open(helper, 'wb') as helper_file:
                helper_file.write(b'helper')
            with patch.object(_bm, '_CHROMIUM_SRC_ROOT', temp_dir):
                self.assertEqual(
                    _bm.require_windows_mail_helper('out/Default'), helper)

    def test_fails_closed_when_helper_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir, \
             patch.object(_bm, '_CHROMIUM_SRC_ROOT', temp_dir):
            with self.assertRaisesRegex(RuntimeError, 'maho_mail_helper.exe'):
                _bm.require_windows_mail_helper('out/Default')


class TestStageMahoCliNextToBrowser(unittest.TestCase):

    def test_copies_cli_next_to_browser_preserving_exec_bit(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = os.path.join(temp_dir, 'maho')
            with open(source, 'wb') as cli_file:
                cli_file.write(b'maho-cli')
            os.chmod(source, 0o755)
            out_dir = 'out/Rel'
            with patch.object(_bm, '_CHROMIUM_SRC_ROOT', temp_dir):
                dest = _bm.stage_maho_cli_next_to_browser(out_dir, source)
            self.assertEqual(dest, os.path.join(temp_dir, out_dir, 'maho'))
            self.assertTrue(os.path.isfile(dest))
            self.assertTrue(os.access(dest, os.X_OK))

    def test_preserves_windows_exe_basename(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = os.path.join(temp_dir, 'maho.exe')
            with open(source, 'wb') as cli_file:
                cli_file.write(b'maho-cli')
            with patch.object(_bm, '_CHROMIUM_SRC_ROOT', temp_dir):
                dest = _bm.stage_maho_cli_next_to_browser('out/Default', source)
            self.assertTrue(dest.endswith('maho.exe'))
            self.assertTrue(os.path.isfile(dest))

    def test_fails_closed_when_artifact_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with patch.object(_bm, '_CHROMIUM_SRC_ROOT', temp_dir):
                self.assertIsNone(
                    _bm.stage_maho_cli_next_to_browser('out/Default',
                                                       os.path.join(temp_dir, 'missing')))


class TestMacBrowserResourceEntitlements(unittest.TestCase):

    def test_app_has_chromium_browser_resource_entitlements(self):
        maho_chromium_dir = os.path.abspath(
            os.path.join(os.path.dirname(__file__), '..', '..')
        )
        with open(os.path.join(maho_chromium_dir, 'branding', 'mac',
                               'entitlements.plist'), 'rb') as entitlements_file:
            app_entitlements = plistlib.load(entitlements_file)
        for resource in (
            'device.audio-input', 'device.bluetooth', 'device.camera',
            'device.print', 'device.usb', 'personal-information.location',
            'personal-information.photos-library',
        ):
            with self.subTest(resource=resource):
                self.assertIs(app_entitlements[f'com.apple.security.{resource}'], True)


class TestMacPasskeySigningContract(unittest.TestCase):

    def test_branding_and_entitlements_enable_local_passkeys(self):
        maho_chromium_dir = os.path.abspath(
            os.path.join(os.path.dirname(__file__), '..', '..')
        )
        branding_path = os.path.join(maho_chromium_dir, 'branding', 'BRANDING')
        app_entitlements_path = os.path.join(
            maho_chromium_dir, 'branding', 'mac', 'entitlements.plist'
        )
        helper_entitlements_path = os.path.join(
            maho_chromium_dir, 'branding', 'mac', 'helper-entitlements.plist'
        )

        with open(branding_path, encoding='utf-8') as branding_file:
            branding = dict(
                line.rstrip('\n').split('=', 1)
                for line in branding_file
                if '=' in line
            )
        with open(app_entitlements_path, 'rb') as entitlements_file:
            app_entitlements = plistlib.load(entitlements_file)
        with open(helper_entitlements_path, 'rb') as entitlements_file:
            helper_entitlements = plistlib.load(entitlements_file)

        team_id = branding['MAC_TEAM_ID']
        bundle_id = branding['MAC_BUNDLE_ID']
        self.assertTrue(team_id)
        self.assertEqual(
            app_entitlements['com.apple.application-identifier'],
            f'{team_id}.{bundle_id}',
        )
        self.assertEqual(
            app_entitlements['com.apple.developer.team-identifier'],
            team_id,
        )
        self.assertEqual(
            app_entitlements['keychain-access-groups'],
            [
                f'{team_id}.{bundle_id}.webauthn',
                f'{team_id}.{bundle_id}.webauthn-uvk',
            ],
        )
        self.assertNotIn(
            'com.apple.application-identifier', helper_entitlements
        )
        self.assertNotIn('keychain-access-groups', helper_entitlements)


class TestLaunchApp(unittest.TestCase):

    def test_launches_on_darwin(self):
        with patch.object(sys, 'platform', 'darwin'), \
             patch('os.path.isdir', return_value=True), \
             patch('subprocess.Popen') as mock_popen:
            launch_app('/fake/Maho.app')
        mock_popen.assert_called_once_with(['open', '/fake/Maho.app'])

    def test_no_launch_on_linux(self):
        with patch.object(sys, 'platform', 'linux'), \
             patch('subprocess.Popen') as mock_popen:
            launch_app('/fake/Maho.app')
        mock_popen.assert_not_called()

    def test_no_launch_on_win32(self):
        with patch.object(sys, 'platform', 'win32'), \
             patch('subprocess.Popen') as mock_popen:
            launch_app('/fake/Maho.app')
        mock_popen.assert_not_called()

    def test_skips_when_bundle_missing(self):
        with patch.object(sys, 'platform', 'darwin'), \
             patch('os.path.isdir', return_value=False), \
             patch('subprocess.Popen') as mock_popen:
            launch_app('/nonexistent/Maho.app')
        mock_popen.assert_not_called()


class TestNormalizeAutoninja(unittest.TestCase):

    def test_strips_leading_double_dash(self):
        self.assertEqual(normalize_autoninja_args(['--', '-j16']), ['-j16'])

    def test_passthrough_without_double_dash(self):
        self.assertEqual(normalize_autoninja_args(['-j16']), ['-j16'])

    def test_empty_list(self):
        self.assertEqual(normalize_autoninja_args([]), [])

    def test_none_passthrough(self):
        self.assertIsNone(normalize_autoninja_args(None))


class TestArgsTemplateSelection(unittest.TestCase):

    def test_release_templates_match_macos_symbol_policy(self):
        config_dir = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            'config',
        )
        for platform in ('mac', 'win', 'linux'):
            with self.subTest(platform=platform):
                config_path = os.path.join(
                    config_dir,
                    f'args_{platform}_release.gn',
                )
                with open(config_path, encoding='utf-8') as config_file:
                    config = config_file.read()
                self.assertIn('symbol_level = 1', config)
                self.assertIn('blink_symbol_level = 0', config)
                self.assertIn('strip_debug_info = true', config)

    def test_release_syncs_local_google_api_key_into_default_args(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            workspace = os.path.join(temp_dir, 'workspace')
            chromium_src = os.path.join(workspace, 'chromium', 'src')
            args_path = os.path.join(chromium_src, 'out', 'Default', 'args.gn')
            vault_path = os.path.join(
                workspace,
                '.secrets',
                'maho',
                'google_chromium_api_key.env',
            )
            os.makedirs(os.path.dirname(args_path))
            os.makedirs(os.path.dirname(vault_path))
            with open(args_path, 'w', encoding='utf-8') as args_file:
                args_file.write('is_official_build = true\n')
            with open(vault_path, 'w', encoding='utf-8') as vault_file:
                vault_file.write(
                    'GOOGLE_API_KEY=AIza12345678901234567890123456789012345\n'
                )

            _bm.sync_local_google_api_key(workspace, args_path, release=True)

            with open(args_path, encoding='utf-8') as args_file:
                args = args_file.read()
        self.assertIn(
            'google_api_key = "AIza12345678901234567890123456789012345"',
            args,
        )
        self.assertNotIn('google_default_client_secret', args)

    def test_non_release_syncs_local_google_api_key_when_present(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            workspace = os.path.join(temp_dir, 'workspace')
            args_path = os.path.join(workspace, 'chromium', 'src', 'out', 'Default', 'args.gn')
            vault_path = os.path.join(
                workspace,
                '.secrets',
                'maho',
                'google_chromium_api_key.env',
            )
            os.makedirs(os.path.dirname(args_path))
            os.makedirs(os.path.dirname(vault_path))
            with open(args_path, 'w', encoding='utf-8') as args_file:
                args_file.write('is_official_build = false\n')
            with open(vault_path, 'w', encoding='utf-8') as vault_file:
                vault_file.write(
                    'GOOGLE_API_KEY=AIza12345678901234567890123456789012345\n'
                )

            updated = _bm.sync_local_google_api_key(workspace, args_path, release=False)
            self.assertTrue(updated)

            with open(args_path, encoding='utf-8') as args_file:
                args = args_file.read()
        self.assertIn('google_api_key = "AIza12345678901234567890123456789012345"', args)

    def test_non_release_without_local_google_api_key_does_not_fail(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args_path = os.path.join(temp_dir, 'out', 'Default', 'args.gn')
            os.makedirs(os.path.dirname(args_path))
            with open(args_path, 'w', encoding='utf-8') as args_file:
                args_file.write('is_official_build = false\n')

            updated = _bm.sync_local_google_api_key(temp_dir, args_path, release=False)
            self.assertFalse(updated)

            with open(args_path, encoding='utf-8') as args_file:
                args = args_file.read()
        self.assertNotIn('google_api_key', args)

    def test_release_without_local_google_api_key_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args_path = os.path.join(temp_dir, 'out', 'Default', 'args.gn')
            os.makedirs(os.path.dirname(args_path))
            with open(args_path, 'w', encoding='utf-8') as args_file:
                args_file.write('is_official_build = true\n')

            with self.assertRaisesRegex(RuntimeError, 'requires local Google API key'):
                _bm.sync_local_google_api_key(temp_dir, args_path, release=True)

    def test_debug_uses_debug_template(self):
        with patch.object(_bm, '_BUILD_CONFIG_DIR', '/configs'), \
             patch('os.path.isfile', side_effect=lambda path: path == '/configs/args_win_debug.gn'), \
             patch('os.makedirs') as mock_makedirs, \
             patch('shutil.copy2') as mock_copy:
            ensure_args_gn('/chromium', 'out/Debug', 'win', True, debug=True)

        mock_makedirs.assert_called_once_with('/chromium/out/Debug', exist_ok=True)
        mock_copy.assert_called_once_with(
            '/configs/args_win_debug.gn',
            '/chromium/out/Debug/args.gn',
        )

    def test_windows_debug_template_disables_siso(self):
        config_path = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            'config',
            'args_win_debug.gn',
        )
        with open(config_path, encoding='utf-8') as config_file:
            self.assertIn('use_siso = false', config_file.read())

    def test_release_regenerates_when_existing_args_is_component_build(self):
        # A stale dev args.gn (component build) must NOT be reused for --release:
        # a component build is non-distributable (engine dylibs load from the
        # build dir via rpath, never bundled).
        with tempfile.TemporaryDirectory() as temp_dir:
            args_path = os.path.join(temp_dir, 'out', 'Default', 'args.gn')
            os.makedirs(os.path.dirname(args_path))
            with open(args_path, 'w', encoding='utf-8') as f:
                f.write('is_component_build = true\nis_debug = false\n')
            template = os.path.join(temp_dir, 'args_mac_release.gn')
            with open(template, 'w', encoding='utf-8') as f:
                f.write('is_official_build = true\n')
            with patch.object(_bm, '_BUILD_CONFIG_DIR', temp_dir):
                ensure_args_gn(temp_dir, 'out/Default', 'mac', False, release=True)
            with open(args_path, encoding='utf-8') as f:
                result = f.read()
        self.assertIn('is_official_build = true', result)
        self.assertNotIn('is_component_build = true', result)

    def test_release_regenerates_when_existing_args_lacks_official_flag(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            args_path = os.path.join(temp_dir, 'out', 'Default', 'args.gn')
            os.makedirs(os.path.dirname(args_path))
            with open(args_path, 'w', encoding='utf-8') as f:
                f.write('is_debug = false\ntarget_cpu = "arm64"\n')
            template = os.path.join(temp_dir, 'args_mac_release.gn')
            with open(template, 'w', encoding='utf-8') as f:
                f.write('is_official_build = true\n')
            with patch.object(_bm, '_BUILD_CONFIG_DIR', temp_dir):
                ensure_args_gn(temp_dir, 'out/Default', 'mac', False, release=True)
            with open(args_path, encoding='utf-8') as f:
                result = f.read()
        self.assertIn('is_official_build = true', result)

    def test_release_keeps_existing_official_noncomponent_args(self):
        # A valid official (non-component) args.gn is respected, not clobbered.
        with tempfile.TemporaryDirectory() as temp_dir:
            args_path = os.path.join(temp_dir, 'out', 'Default', 'args.gn')
            os.makedirs(os.path.dirname(args_path))
            sentinel = 'is_official_build = true\ngoogle_api_key = "LOCAL_SENTINEL"\n'
            with open(args_path, 'w', encoding='utf-8') as f:
                f.write(sentinel)
            template = os.path.join(temp_dir, 'args_mac_release.gn')
            with open(template, 'w', encoding='utf-8') as f:
                f.write('is_official_build = true\n')
            with patch.object(_bm, '_BUILD_CONFIG_DIR', temp_dir):
                ensure_args_gn(temp_dir, 'out/Default', 'mac', False, release=True)
            with open(args_path, encoding='utf-8') as f:
                result = f.read()
        # untouched -> local sentinel (e.g. synced google key) preserved
        self.assertIn('LOCAL_SENTINEL', result)


def _make_fake_staged_sdk(directory: str, name: str = 'MacOSX.sdk') -> str:
    sdk_path = os.path.join(directory, name)
    math_h = os.path.join(sdk_path, 'usr', 'include', 'math.h')
    os.makedirs(os.path.dirname(math_h), exist_ok=True)
    with open(math_h, 'w', encoding='utf-8') as f:
        f.write(
            '#   if 0 // disabled: prevents partial float.h inclusion from poisoning libc++ float.h\n'
            '#       define    NAN          __builtin_nanf("0x7fc00000")\n'
            '#       define    INFINITY     __builtin_inff()\n'
        )
    return sdk_path


class TestEnsureMacOsSdkPath(unittest.TestCase):

    def setUp(self):
        self._temp_dir = tempfile.TemporaryDirectory()
        self.temp_dir = self._temp_dir.name
        self.staged_sdk = _make_fake_staged_sdk(self.temp_dir, 'MacOSX.sdk')

        # Set up a fake Chromium source tree: <temp_dir>/chromium/src/.gn
        self.chromium_src = os.path.join(self.temp_dir, 'chromium', 'src')
        os.makedirs(self.chromium_src, exist_ok=True)
        with open(os.path.join(self.chromium_src, '.gn'), 'w', encoding='utf-8') as f:
            f.write('buildconfig = "//BUILDCONFIG.gn"\n')
        self.out_dir = os.path.join(self.chromium_src, 'out', 'Release')
        os.makedirs(self.out_dir, exist_ok=True)
        self.args_path = os.path.join(self.out_dir, 'args.gn')
        with open(self.args_path, 'w', encoding='utf-8') as f:
            f.write('is_official_build = true\n')

        # Mock stage_macos_sdk module
        self.mock_stage_mod: Any = types.ModuleType('maho_stage_macos_sdk')
        self.mock_stage_mod.DEFAULT_DEST = self.staged_sdk
        self.mock_stage_mod.find_source_sdk = MagicMock(return_value='/System/Library/SDKs/MacOSX.sdk')
        self.mock_stage_mod.stage = MagicMock(return_value=self.staged_sdk)

        def _check(sdk):
            math_path = os.path.join(sdk, 'usr', 'include', 'math.h')
            if not os.path.isfile(math_path):
                return 1
            with open(math_path, encoding='utf-8', errors='replace') as h:
                text = h.read()
            if 'INFINITY' in text and 'NAN' in text:
                return 0
            return 1

        self.mock_stage_mod.check = _check

        self._orig_load_module = _bm.load_module

        def _fake_load_module(name, path):
            if 'stage_macos_sdk' in name:
                return self.mock_stage_mod
            return self._orig_load_module(name, path)

        self._patcher = patch.object(_bm, 'load_module', side_effect=_fake_load_module)
        self._patcher.start()

    def tearDown(self):
        self._patcher.stop()
        self._temp_dir.cleanup()

    def test_creates_build_output_symlink_and_uses_gn_relative_path(self):
        result = ensure_macos_sdk_path(self.args_path)
        self.assertTrue(result)
        with open(self.args_path, encoding='utf-8') as f:
            content = f.read()

        # Must configure mac_sdk_path relative to GN build root, NOT raw external path
        self.assertIn('mac_sdk_path = "//out/Release/sdk/xcode_links/MacOSX.sdk"', content)
        self.assertNotIn(self.staged_sdk, content)

        # Build output symlink must exist and point to the staged SDK
        symlink_path = os.path.join(self.out_dir, 'sdk', 'xcode_links', 'MacOSX.sdk')
        self.assertTrue(os.path.islink(symlink_path))
        self.assertEqual(
            os.path.realpath(symlink_path),
            os.path.realpath(self.staged_sdk),
        )
        self.assertEqual(self.mock_stage_mod.check(symlink_path), 0)

    def test_migrates_existing_external_absolute_path_to_symlink(self):
        # Existing pre-154 args.gn has the external absolute path
        with open(self.args_path, 'w', encoding='utf-8') as f:
            f.write(f'is_official_build = true\nmac_sdk_path = "{self.staged_sdk}"\n')

        result = ensure_macos_sdk_path(self.args_path)
        self.assertTrue(result)
        with open(self.args_path, encoding='utf-8') as f:
            content = f.read()

        # The external absolute path must be replaced with the build-output symlink path
        self.assertNotIn(f'mac_sdk_path = "{self.staged_sdk}"', content)
        self.assertIn('mac_sdk_path = "//out/Release/sdk/xcode_links/MacOSX.sdk"', content)

        symlink_path = os.path.join(self.out_dir, 'sdk', 'xcode_links', 'MacOSX.sdk')
        self.assertTrue(os.path.islink(symlink_path))
        self.assertEqual(
            os.path.realpath(symlink_path),
            os.path.realpath(self.staged_sdk),
        )

    def test_idempotent_when_valid_symlink_already_configured(self):
        # Configure valid state
        symlink_dir = os.path.join(self.out_dir, 'sdk', 'xcode_links')
        os.makedirs(symlink_dir, exist_ok=True)
        symlink_path = os.path.join(symlink_dir, 'MacOSX.sdk')
        os.symlink(self.staged_sdk, symlink_path)

        with open(self.args_path, 'w', encoding='utf-8') as f:
            f.write('is_official_build = true\nmac_sdk_path = "//out/Release/sdk/xcode_links/MacOSX.sdk"\n')

        mtime_before = os.path.getmtime(self.args_path)
        result = ensure_macos_sdk_path(self.args_path)
        self.assertTrue(result)

        with open(self.args_path, encoding='utf-8') as f:
            content = f.read()

        self.assertIn('mac_sdk_path = "//out/Release/sdk/xcode_links/MacOSX.sdk"', content)
        # Content unchanged
        self.assertEqual(os.path.getmtime(self.args_path), mtime_before)
        # Symlink untouched and healthy
        self.assertTrue(os.path.islink(symlink_path))
        self.assertEqual(os.path.realpath(symlink_path), os.path.realpath(self.staged_sdk))

    def test_repairs_missing_or_broken_symlink_when_args_has_symlink_path(self):
        # args.gn specifies the symlink path, but symlink is missing
        with open(self.args_path, 'w', encoding='utf-8') as f:
            f.write('is_official_build = true\nmac_sdk_path = "//out/Release/sdk/xcode_links/MacOSX.sdk"\n')

        result = ensure_macos_sdk_path(self.args_path)
        self.assertTrue(result)

        symlink_path = os.path.join(self.out_dir, 'sdk', 'xcode_links', 'MacOSX.sdk')
        self.assertTrue(os.path.islink(symlink_path))
        self.assertEqual(os.path.realpath(symlink_path), os.path.realpath(self.staged_sdk))

    def test_preserves_existing_non_symlink_directory(self):
        sdk_path = os.path.join(self.out_dir, 'sdk', 'xcode_links', 'MacOSX.sdk')
        os.makedirs(sdk_path)
        sentinel = os.path.join(sdk_path, 'preserve.txt')
        with open(sentinel, 'w', encoding='utf-8') as f:
            f.write('existing data')
        with self.assertRaises(RuntimeError):
            ensure_macos_sdk_path(self.args_path)
        with open(sentinel, encoding='utf-8') as f:
            self.assertEqual(f.read(), 'existing data')

    def test_preserves_custom_valid_staged_sdk_when_migrating(self):
        custom_sdk = _make_fake_staged_sdk(self.temp_dir, 'CustomMacOSX.sdk')
        with open(self.args_path, 'w', encoding='utf-8') as f:
            f.write(f'is_official_build = true\nmac_sdk_path = "{custom_sdk}"\n')

        result = ensure_macos_sdk_path(self.args_path)
        self.assertTrue(result)

        with open(self.args_path, encoding='utf-8') as f:
            content = f.read()

        self.assertIn('mac_sdk_path = "//out/Release/sdk/xcode_links/CustomMacOSX.sdk"', content)
        symlink_path = os.path.join(self.out_dir, 'sdk', 'xcode_links', 'CustomMacOSX.sdk')
        self.assertTrue(os.path.islink(symlink_path))
        self.assertEqual(os.path.realpath(symlink_path), os.path.realpath(custom_sdk))

    def test_sdk_inputs_gn_action_accepts_configured_sdk_path(self):
        # Verify that Chromium 154's sdk_inputs outputs are accepted by GN
        # when configured by ensure_macos_sdk_path.
        result = ensure_macos_sdk_path(self.args_path)
        self.assertTrue(result)
        with open(self.args_path, encoding='utf-8') as f:
            content = f.read()

        match = re.search(r'^\s*mac_sdk_path\s*=\s*"([^"]+)"', content, re.MULTILINE)
        self.assertIsNotNone(match)
        assert match is not None
        configured_path = match.group(1)

        # 154 build/config/mac/BUILD.gn:114 action("sdk_inputs") outputs:
        # "$mac_sdk_path/usr/include/mach/exc.defs"
        # GN rule: action outputs must reside within root_build_dir.
        # Paths starting with '//' and rooted at the output directory satisfy this invariant.
        self.assertTrue(configured_path.startswith('//'), f'mac_sdk_path must start with //: {configured_path}')
        self.assertIn('/sdk/xcode_links/', configured_path)

        # Rebase to root_build_dir should be relative to output dir
        rel_to_build = os.path.relpath(
            os.path.join(self.chromium_src, configured_path[2:]),
            self.out_dir,
        )
        self.assertEqual(rel_to_build, 'sdk/xcode_links/MacOSX.sdk')


class TestChromiumSourceRoot(unittest.TestCase):

    def test_resolves_junctions_before_building(self):
        with patch('os.path.realpath', return_value='/physical/chromium/src') as mock_realpath:
            root = _bm.chromium_src_root('/workspace')

        self.assertEqual(root, '/physical/chromium/src')
        mock_realpath.assert_called_once_with('/workspace/chromium/src')


class TestApplyBranding(unittest.TestCase):

    def test_windows_brands_browser_installer_and_tiles(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            maho_dir = os.path.join(temp_dir, 'maho-chromium')
            branding_dir = os.path.join(maho_dir, 'branding')
            chromium_root = os.path.join(temp_dir, 'chromium', 'src')
            theme_dir = os.path.join(
                chromium_root, 'chrome', 'app', 'theme', 'chromium')
            os.makedirs(os.path.join(theme_dir, 'win', 'tiles'))
            os.makedirs(os.path.join(
                chromium_root, 'chrome', 'installer', 'mini_installer'))
            os.makedirs(os.path.join(
                chromium_root, 'chrome', 'installer', 'setup'))
            os.makedirs(branding_dir)

            with open(os.path.join(branding_dir, 'BRANDING'), 'w') as branding:
                branding.write('PRODUCT_FULLNAME=Maho\n')

            logos = {}
            for size in (16, 32, 48, 256):
                logo = (
                    b'\x89PNG\r\n\x1a\n'
                    + b'\0' * 8
                    + size.to_bytes(4, 'big') * 2
                )
                logos[size] = logo
                with open(
                    os.path.join(branding_dir, f'product_logo_{size}.png'),
                    'wb',
                ) as file:
                    file.write(logo)

            with patch.object(_bm, '_MAHO_CHROMIUM_DIR', maho_dir), \
                 patch.object(_bm, '_CHROMIUM_SRC_ROOT', chromium_root):
                _bm.apply_branding('win')

            expected_ico = _bm._build_windows_ico(branding_dir)
            self.assertIsNotNone(expected_ico)
            for path in (
                os.path.join(theme_dir, 'win', 'chromium.ico'),
                os.path.join(
                    chromium_root,
                    'chrome',
                    'installer',
                    'mini_installer',
                    'mini_installer.ico',
                ),
                os.path.join(
                    chromium_root,
                    'chrome',
                    'installer',
                    'setup',
                    'setup.ico',
                ),
            ):
                with open(path, 'rb') as file:
                    self.assertEqual(file.read(), expected_ico)

            with open(
                os.path.join(theme_dir, 'win', 'tiles', 'Logo.png'),
                'rb',
            ) as file:
                self.assertEqual(file.read(), logos[256])
            with open(
                os.path.join(theme_dir, 'win', 'tiles', 'SmallLogo.png'),
                'rb',
            ) as file:
                self.assertEqual(file.read(), logos[48])

            output_paths = (
                os.path.join(theme_dir, 'win', 'chromium.ico'),
                os.path.join(
                    chromium_root,
                    'chrome',
                    'installer',
                    'mini_installer',
                    'mini_installer.ico',
                ),
                os.path.join(
                    chromium_root,
                    'chrome',
                    'installer',
                    'setup',
                    'setup.ico',
                ),
                os.path.join(theme_dir, 'win', 'tiles', 'Logo.png'),
                os.path.join(theme_dir, 'win', 'tiles', 'SmallLogo.png'),
                os.path.join(theme_dir, 'BRANDING'),
            )
            first_outputs = {}
            for path in output_paths:
                with open(path, 'rb') as file:
                    first_outputs[path] = file.read()

            with patch.object(_bm, '_MAHO_CHROMIUM_DIR', maho_dir), \
                 patch.object(_bm, '_CHROMIUM_SRC_ROOT', chromium_root):
                _bm.apply_branding('win')

            for path, first_output in first_outputs.items():
                with open(path, 'rb') as file:
                    self.assertEqual(file.read(), first_output)


class TestVerifyRemoteSccache(unittest.TestCase):

    @patch('shutil.which', return_value='/opt/homebrew/bin/sccache')
    @patch('subprocess.run')
    @patch('socket.create_connection')
    def test_verify_remote_sccache_success(self, mock_socket, mock_run, mock_which):
        mock_run.return_value = MagicMock(
            stdout="Compile requests 100\nCache location s3, name: sccache, prefix: /maho/\n"
        )
        mock_sock = MagicMock()
        mock_socket.return_value = mock_sock

        with patch.dict('os.environ', {'SCCACHE_ENDPOINT': 'cache.internal.example:9000'}):
            _bm.verify_remote_sccache(skip=False)
        mock_socket.assert_called_once_with(('cache.internal.example', 9000), timeout=3.0)
        mock_sock.close.assert_called_once()

    def test_verify_remote_sccache_skip(self):
        with patch('shutil.which') as mock_which:
            _bm.verify_remote_sccache(skip=True)
            mock_which.assert_not_called()

    @patch.dict('os.environ', {}, clear=True)
    def test_verify_remote_sccache_no_endpoint_skips(self):
        # Third-party builders without SCCACHE_ENDPOINT just build locally.
        with patch('shutil.which') as mock_which:
            _bm.verify_remote_sccache(skip=False)
            mock_which.assert_not_called()

    @patch('shutil.which', return_value=None)
    @patch('os.path.exists', return_value=False)
    def test_verify_remote_sccache_missing_binary(self, mock_exists, mock_which):
        with patch.dict('os.environ', {'SCCACHE_ENDPOINT': 'cache.internal.example:9000'}):
            with self.assertRaises(SystemExit) as cm:
                _bm.verify_remote_sccache(skip=False)
        self.assertIn('sccache binary not found', str(cm.exception))

    @patch('shutil.which', return_value='/usr/bin/sccache')
    @patch('subprocess.run', side_effect=Exception('daemon dead'))
    def test_verify_remote_sccache_daemon_down(self, mock_run, mock_which):
        with patch.dict('os.environ', {'SCCACHE_ENDPOINT': 'cache.internal.example:9000'}):
            with self.assertRaises(SystemExit) as cm:
                _bm.verify_remote_sccache(skip=False)
        self.assertIn('cannot query sccache daemon', str(cm.exception))

    @patch('shutil.which', return_value='/usr/bin/sccache')
    @patch('subprocess.run')
    def test_verify_remote_sccache_local_disk_fails(self, mock_run, mock_which):
        mock_run.return_value = MagicMock(
            stdout="Compile requests 100\nCache location disk, path: /tmp/sccache\n"
        )
        with patch.dict('os.environ', {'SCCACHE_ENDPOINT': 'cache.internal.example:9000'}):
            with self.assertRaises(SystemExit) as cm:
                _bm.verify_remote_sccache(skip=False)
        self.assertIn('cache location is not S3', str(cm.exception))

    @patch('shutil.which', return_value='/usr/bin/sccache')
    @patch('subprocess.run')
    @patch('socket.create_connection', side_effect=OSError('network unreachable'))
    def test_verify_remote_sccache_unreachable_endpoint(self, mock_socket, mock_run, mock_which):
        mock_run.return_value = MagicMock(
            stdout="Compile requests 100\nCache location s3, name: sccache, prefix: /maho/\n"
        )
        with patch.dict('os.environ', {'SCCACHE_ENDPOINT': 'cache.internal.example:9000'}):
            with self.assertRaises(SystemExit) as cm:
                _bm.verify_remote_sccache(skip=False)
        self.assertIn('remote MinIO endpoint', str(cm.exception))


class TestMainFlags(unittest.TestCase):

    def setUp(self) -> None:
        for name in (
            'ensure_overlay_mount',
            'ensure_args_gn',
            'sync_local_google_api_key',
            'apply_tracked_chromium_overrides',
            'apply_branding',
            'run_lucide_icon_check',
            'verify_remote_sccache',
            'run_gn_gen',
            'embed_sparkle_framework',
            'embed_mail_helper',
            'embed_omo_runtime',
            'require_windows_mail_helper',
            'build_windows_installer',
            'sign_windows_installer',
            'find_codesign_identity_release_dist',
            'find_appstore_provisioning_profile',
        ):
            patcher = patch.object(_bm, name)
            patcher.start()
            self.addCleanup(patcher.stop)

    def _run_main(self, argv, platform='darwin'):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        # ExitStack instead of a multi-item `with`: 21 context managers exceed
        # CPython's statically-nested-block limit.
        with ExitStack() as stack:
            stack.enter_context(patch.object(sys, 'argv', ['build_maho.py'] + argv))
            stack.enter_context(patch.object(sys, 'platform', platform))
            for name in ('ensure_overlay_mount', 'ensure_args_gn',
                         'sync_local_google_api_key', 'apply_tracked_chromium_overrides',
                         'apply_branding', 'run_lucide_icon_check', 'verify_remote_sccache',
                         'run_gn_gen', 'embed_sparkle_framework', 'embed_mail_helper'):
                stack.enter_context(patch.object(_bm, name))
            stack.enter_context(
                patch.object(_bm, 'embed_omo_runtime', return_value=True))
            mock_build_chromium = stack.enter_context(
                patch.object(_bm, 'build_chromium_app'))
            mock_build_cli = stack.enter_context(
                patch.object(_bm, 'build_maho_cli', return_value='/fake/maho'))
            mock_embed_cli = stack.enter_context(
                patch.object(_bm, 'embed_maho_cli', return_value=True))
            mock_stage_cli = stack.enter_context(
                patch.object(_bm, 'stage_maho_cli_next_to_browser',
                             return_value='/fake/out/maho'))
            mock_sign_win = stack.enter_context(
                patch.object(_bm, 'sign_windows_binary', return_value=True))
            mock_find = stack.enter_context(
                patch.object(_bm, 'find_codesign_identity', return_value='FAKSHA'))
            mock_find_dist = stack.enter_context(
                patch.object(_bm, 'find_codesign_identity_release_dist',
                             return_value='FAKSHA'))
            mock_find_profile = stack.enter_context(
                patch.object(_bm, 'find_appstore_provisioning_profile',
                             return_value='/fake/profile.mobileprovision'))
            mock_sign = stack.enter_context(patch.object(_bm, 'codesign_app'))
            mock_launch = stack.enter_context(patch.object(_bm, 'launch_app'))
            _bm.main()

        self.mock_build_chromium = mock_build_chromium
        self.mock_find_dist = mock_find_dist
        self.mock_find_profile = mock_find_profile
        return mock_build_cli, mock_embed_cli, mock_find, mock_sign, mock_launch

    def test_default_darwin_codesigns_and_launches(self):
        self.addCleanup(os.environ.pop, 'MAHO_MAC_PROVISIONING_PROFILE', None)
        mock_build_cli, mock_embed_cli, mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust'])
        mock_build_cli.assert_called_once_with(None)
        mock_embed_cli.assert_called_once_with(_bm._MAHO_APP_PATH, '/fake/maho')
        # Debug policy: a stable Apple Distribution identity paired with the
        # App Store provisioning profile, never the generic auto-detected
        # identity (see find_codesign_identity_release_dist in build_maho.py).
        mock_find.assert_not_called()
        self.mock_find_dist.assert_called_once()
        self.mock_find_profile.assert_called_once_with('com.maho.browser')
        self.assertEqual(
            os.environ.get('MAHO_MAC_PROVISIONING_PROFILE'),
            '/fake/profile.mobileprovision')
        mock_sign.assert_called_once_with('FAKSHA', _bm._MAHO_APP_PATH)
        mock_launch.assert_called_once()

    def test_skip_codesign_skips_sign_but_still_launches(self):
        _, _, mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust', '--skip-codesign'])
        mock_find.assert_not_called()
        mock_sign.assert_not_called()
        mock_launch.assert_called_once()

    def test_no_launch_skips_launch_but_still_signs(self):
        _, _, _, mock_sign, mock_launch = self._run_main(['--skip-rust', '--no-launch'])
        mock_sign.assert_called_once()
        mock_launch.assert_not_called()

    def test_windows_release_includes_mini_installer(self):
        self._run_main(
            ['--skip-rust', '--release', '--no-launch'],
            platform='win32',
        )

        self.assertEqual(
            self.mock_build_chromium.call_args.args[1],
            ['chrome', 'maho_mail_helper', 'mini_installer'],
        )

    def test_codesign_identity_overrides_autodetect(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust', '--codesign-identity', 'MYID']), \
             patch.object(sys, 'platform', 'darwin'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'build_maho_cli', return_value='/fake/maho'), \
             patch.object(_bm, 'embed_maho_cli', return_value=True), \
             patch.object(_bm, 'find_codesign_identity') as mock_find, \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app'):
            _bm.main()

        mock_find.assert_not_called()
        mock_sign.assert_called_once_with('MYID', _bm._MAHO_APP_PATH)

    def test_skip_chromium_skips_codesign_and_launch(self):
        _, _, mock_find, mock_sign, mock_launch = self._run_main(['--skip-chromium'])
        mock_find.assert_not_called()
        mock_sign.assert_not_called()
        mock_launch.assert_not_called()

    def test_codesign_skipped_on_linux(self):
        _, _, mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust'], platform='linux')
        mock_find.assert_not_called()
        mock_sign.assert_not_called()
        mock_launch.assert_called_once()

    def test_linux_stages_cli_next_to_browser(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust']), \
             patch.object(sys, 'platform', 'linux'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'build_maho_cli', return_value='/fake/maho'), \
             patch.object(_bm, 'stage_maho_cli_next_to_browser', return_value='/fake/out/maho') as mock_stage, \
             patch.object(_bm, 'sign_windows_binary') as mock_sign_win, \
             patch.object(_bm, 'launch_app'):
            _bm.main()

        mock_stage.assert_called_once_with('out/Default', '/fake/maho')
        mock_sign_win.assert_not_called()

    def test_windows_stages_and_signs_cli_before_build(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()
        call_order = []

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust']), \
             patch.object(sys, 'platform', 'win32'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app', side_effect=lambda *a, **k: call_order.append('build')), \
             patch.object(_bm, 'build_maho_cli', return_value='/fake/maho.exe'), \
             patch.object(_bm, 'stage_maho_cli_next_to_browser', side_effect=lambda *a, **k: call_order.append('stage') or '/fake/out/maho.exe'), \
             patch.object(_bm, 'sign_windows_binary', side_effect=lambda *a, **k: call_order.append('sign')), \
             patch.object(_bm, 'launch_app'):
            _bm.main()

        self.assertEqual(call_order, ['stage', 'sign', 'build'])

    def test_windows_skip_codesign_stages_without_signing(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust', '--skip-codesign']), \
             patch.object(sys, 'platform', 'win32'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'build_maho_cli', return_value='/fake/maho.exe'), \
             patch.object(_bm, 'stage_maho_cli_next_to_browser', return_value='/fake/out/maho.exe') as mock_stage, \
             patch.object(_bm, 'sign_windows_binary') as mock_sign_win, \
             patch.object(_bm, 'launch_app'):
            _bm.main()

        mock_stage.assert_called_once()
        mock_sign_win.assert_not_called()

    def test_no_identity_prints_warning_does_not_crash(self):
        class _Err:
            def __init__(self):
                self.buf = []
            def write(self, s):
                self.buf.append(s)
            def getvalue(self):
                return ''.join(self.buf)

        err = _Err()
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust']), \
             patch.object(sys, 'platform', 'darwin'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'build_maho_cli', return_value='/fake/maho'), \
             patch.object(_bm, 'embed_maho_cli', return_value=True), \
             patch.object(_bm, 'find_codesign_identity_release_dist', return_value=None), \
             patch.object(_bm, 'find_appstore_provisioning_profile', return_value=None), \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app'), \
             patch.object(_bm.subprocess, 'run') as mock_run, \
             patch.object(_bm.sys, 'stderr', err):
            _bm.main()

        mock_sign.assert_not_called()
        self.assertIn('falling back to ad-hoc signing', err.getvalue())
        adhoc = [c for c in mock_run.call_args_list
                 if c.args and isinstance(c.args[0], list) and c.args[0][:1] == ['codesign']]
        self.assertEqual(len(adhoc), 1)
        cmd = adhoc[0].args[0]
        self.assertEqual(cmd[cmd.index('--sign') + 1], '-')
        self.assertEqual(cmd[cmd.index('--identifier') + 1], 'com.maho.browser')
        self.assertIn('-r=designated => identifier "com.maho.browser"', cmd)
        self.assertTrue(adhoc[0].kwargs.get('check'))

    def test_no_identity_adhoc_skipped_entirely_when_codesign_skipped(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust', '--skip-codesign']), \
             patch.object(sys, 'platform', 'darwin'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'build_maho_cli', return_value='/fake/maho'), \
             patch.object(_bm, 'embed_maho_cli', return_value=True), \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app'), \
             patch.object(_bm.subprocess, 'run') as mock_run:
            _bm.main()

        mock_sign.assert_not_called()
        codesign_runs = [c for c in mock_run.call_args_list
                         if c.args and isinstance(c.args[0], list) and c.args[0][:1] == ['codesign']]
        self.assertEqual(codesign_runs, [])

    def test_ninja_target_repeats(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust', '--ninja-target', 'target1', '--ninja-target', 'target2']), \
             patch.object(sys, 'platform', 'darwin'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app') as mock_build, \
             patch.object(_bm, 'find_codesign_identity', return_value='FAKSHA'), \
             patch.object(_bm, 'find_codesign_identity_release_dist', return_value='FAKSHA'), \
             patch.object(_bm, 'find_appstore_provisioning_profile', return_value='/fake/profile.mobileprovision'), \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app') as mock_launch:
            _bm.main()

        mock_build.assert_called_once_with('out/Default', ['target1', 'target2'], [])
        mock_sign.assert_not_called()
        mock_launch.assert_not_called()

    def test_ninja_target_clashes_with_remainder(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust', '--ninja-target', 'target1', '--', '-j16']), \
             patch.object(sys, 'platform', 'darwin'), \
             patch.object(sys, 'stderr'):
            with self.assertRaises(SystemExit):
                _bm.main()


class TestCreateDmgImage(unittest.TestCase):

    def test_synthesizes_udrw_attach_layout_convert_udzo_not_makehybrid(self):
        with patch('subprocess.run') as mock_run, \
             patch('tempfile.mkdtemp', return_value='/tmp/maho-dmg-layout-x'), \
             patch('os.path.exists', return_value=False), \
             patch('shutil.rmtree') as mock_rmtree, \
             patch.object(_bm, '_write_dmg_layout') as mock_layout:
            mock_run.return_value = MagicMock(returncode=0)
            _bm._create_dmg_image('/stage', '/out/Maho-1.0.0.dmg', 'Maho 1.0.0')
        commands = [c.args[0] for c in mock_run.call_args_list]
        create = commands[0]
        self.assertEqual(create[0:2], ['hdiutil', 'create'])
        self.assertIn('-srcfolder', create)
        self.assertEqual(create[create.index('-srcfolder') + 1], '/stage')
        self.assertIn('-volname', create)
        self.assertEqual(create[create.index('-volname') + 1], 'Maho 1.0.0')
        self.assertIn('-format', create)
        self.assertEqual(create[create.index('-format') + 1], 'UDRW')
        self.assertEqual(commands[1][0:2], ['hdiutil', 'attach'])
        self.assertIn('/tmp/maho-dmg-layout-x', commands[1])
        mock_layout.assert_called_once_with('/tmp/maho-dmg-layout-x')
        self.assertEqual(commands[2][0:2], ['hdiutil', 'detach'])
        convert = commands[3]
        self.assertEqual(convert[0:2], ['hdiutil', 'convert'])
        self.assertEqual(convert[convert.index('-format') + 1], 'UDZO')
        self.assertEqual(convert[-1], '/out/Maho-1.0.0.dmg')
        self.assertFalse(any('makehybrid' in str(command) for command in commands),
                         'must not use pkg-dmg/makehybrid')
        mock_rmtree.assert_called_once_with('/tmp/maho-dmg-layout-x', ignore_errors=True)


class TestWriteDmgLayout(unittest.TestCase):

    def test_writes_background_alias_window_bounds_and_icon_locations(self):
        """Alias.for_file is mocked because mac_alias cannot serialize APFS
        64-bit file IDs; the real pipeline only ever aliases the background
        inside the HFS+ image volume, where 32-bit CNIDs guarantee it works
        (covered by the integration run of build_dmg)."""
        from ds_store import DSStore

        stub_alias = MagicMock()
        stub_alias.to_bytes.return_value = b'maho-test-background-alias'
        with tempfile.TemporaryDirectory(prefix='maho-dmg-layout-test-') as staging:
            os.makedirs(os.path.join(staging, 'Maho.app'))
            os.symlink('/Applications', os.path.join(staging, 'Applications'))
            with patch('mac_alias.Alias.for_file', return_value=stub_alias):
                _bm._write_dmg_layout(staging)
            with DSStore.open(os.path.join(staging, '.DS_Store'), 'r') as store:
                s: Any = store
                bwsp = s['.']['bwsp']
                self.assertEqual(bwsp['WindowBounds'], '{{100, 100}, {664, 428}}')
                self.assertFalse(bwsp['ShowToolbar'])
                icvp = s['.']['icvp']
                self.assertEqual(icvp['backgroundType'], 2)
                self.assertEqual(icvp['iconSize'], 68.0)
                self.assertEqual(icvp['backgroundImageAlias'], b'maho-test-background-alias')
                self.assertEqual(s['Maho.app']['Iloc'], (181, 284))
                self.assertEqual(s['Applications']['Iloc'], (416, 284))
                self.assertEqual(s['.']['vSrn'], (b'long', 1))
            self.assertTrue(os.path.isfile(os.path.join(staging, '.background.png')))


class TestVerifyDmgAppSignature(unittest.TestCase):

    def _dispatch(self, codesign_rc=0, xattr_rc=0, xattr_stdout=''):
        def _run(cmd, *a, **k):
            if cmd[:2] == ['hdiutil', 'attach']:
                return MagicMock(returncode=0, stdout='', stderr='')
            if cmd[:1] == ['codesign']:
                return MagicMock(returncode=codesign_rc, stdout='', stderr='detritus' if codesign_rc else '')
            if cmd[:1] == ['xattr']:
                return MagicMock(returncode=xattr_rc, stdout=xattr_stdout, stderr='boom' if xattr_rc else '')
            if cmd[:2] == ['hdiutil', 'detach']:
                return MagicMock(returncode=0)
            return MagicMock(returncode=0, stdout='', stderr='')
        return _run

    def _run_verify(self, dispatch):
        with patch('tempfile.mkdtemp', return_value='/tmp/maho-dmg-verify-x'), \
             patch('os.path.isfile', return_value=True), \
             patch('os.path.isdir', return_value=True), \
             patch('os.rmdir'), \
             patch('subprocess.run', side_effect=dispatch) as mock_run:
            result = _bm.verify_dmg_app_signature('/out/Maho-1.0.0.dmg')
        detaches = [c for c in mock_run.call_args_list
                    if c.args[0][:2] == ['hdiutil', 'detach']]
        return result, detaches

    def test_passes_when_mounted_app_clean(self):
        result, detaches = self._run_verify(
            self._dispatch(xattr_stdout='some/path: com.apple.provenance\n'))
        self.assertTrue(result)
        self.assertEqual(len(detaches), 1)
        self.assertEqual(detaches[0].args[0][2], '/tmp/maho-dmg-verify-x')

    def test_fails_closed_and_detaches_when_finderinfo_present(self):
        result, detaches = self._run_verify(
            self._dispatch(xattr_stdout='a: com.apple.FinderInfo\nb: com.apple.FinderInfo\n'))
        self.assertFalse(result)
        self.assertEqual(len(detaches), 1, 'must detach in finally even on failure')

    def test_fails_closed_and_detaches_when_codesign_fails(self):
        result, detaches = self._run_verify(self._dispatch(codesign_rc=1))
        self.assertFalse(result)
        self.assertEqual(len(detaches), 1)

    def test_fails_closed_and_detaches_when_xattr_scan_errors(self):
        # A failed xattr scan must NOT be read as "0 FinderInfo" (fail-open).
        result, detaches = self._run_verify(self._dispatch(xattr_rc=1, xattr_stdout=''))
        self.assertFalse(result, 'xattr failure must fail closed, not pass')
        self.assertEqual(len(detaches), 1)

    def test_detaches_when_app_missing_after_attach(self):
        def _run(cmd, *a, **k):
            if cmd[:2] == ['hdiutil', 'attach']:
                return MagicMock(returncode=0, stdout='', stderr='')
            if cmd[:2] == ['hdiutil', 'detach']:
                return MagicMock(returncode=0)
            return MagicMock(returncode=0, stdout='', stderr='')
        with patch('tempfile.mkdtemp', return_value='/tmp/maho-dmg-verify-y'), \
             patch('os.path.isfile', return_value=True), \
             patch('os.path.isdir', return_value=False), \
             patch('os.rmdir'), \
             patch('subprocess.run', side_effect=_run) as mock_run:
            result = _bm.verify_dmg_app_signature('/out/Maho-1.0.0.dmg')
        self.assertFalse(result)
        detaches = [c for c in mock_run.call_args_list
                    if c.args[0][:2] == ['hdiutil', 'detach']]
        self.assertEqual(len(detaches), 1, 'attach succeeded so must detach')


class TestBuildDmg(unittest.TestCase):

    def _patches(self, run_side_effect, gate=True):
        return [
            patch('os.path.isdir', return_value=True),
            patch('os.path.exists', return_value=False),
            patch('os.makedirs'),
            patch('os.symlink'),
            patch('os.remove'),
            patch('shutil.rmtree'),
            patch('subprocess.run', side_effect=run_side_effect),
            patch.object(_bm, 'verify_dmg_app_signature', return_value=gate),
            patch.object(_bm, '_write_dmg_layout'),
        ]

    def test_synthesizes_via_hdiutil_create_not_pkgdmg(self):
        calls = []

        def _run(cmd, *a, **k):
            calls.append(cmd)
            return MagicMock(returncode=0, stdout='', stderr='')

        ctx = self._patches(_run, gate=True)
        for p in ctx:
            p.start()
        try:
            result = _bm.build_dmg('/out/Default/Maho.app', 'out/Default', '1.0.0', 'DEADBEEF')
        finally:
            for p in reversed(ctx):
                p.stop()

        self.assertTrue(result and result.endswith('Maho-1.0.0.dmg'))
        flat = [c[0] for c in calls]
        self.assertIn('ditto', flat)
        self.assertTrue(any(c[0] == 'hdiutil' and c[1] == 'create' and '-srcfolder' in c for c in calls),
                        'must synthesize the image with hdiutil create -srcfolder')
        self.assertFalse(any('pkg-dmg' in str(c) or 'makehybrid' in str(c) for c in calls),
                         'must not use pkg-dmg/makehybrid')

    def test_returns_none_and_removes_dmg_when_gate_fails(self):
        def _run(cmd, *a, **k):
            return MagicMock(returncode=0, stdout='', stderr='')

        removed = []
        ctx = [
            patch('os.path.isdir', return_value=True),
            patch('os.path.exists', return_value=True),
            patch('os.makedirs'),
            patch('os.symlink'),
            patch('os.remove', side_effect=lambda p: removed.append(p)),
            patch('shutil.rmtree'),
            patch('subprocess.run', side_effect=_run),
            patch.object(_bm, 'verify_dmg_app_signature', return_value=False),
            patch.object(_bm, '_write_dmg_layout'),
        ]
        for p in ctx:
            p.start()
        try:
            result = _bm.build_dmg('/out/Default/Maho.app', 'out/Default', '1.0.0', 'DEADBEEF')
        finally:
            for p in reversed(ctx):
                p.stop()

        self.assertIsNone(result)
        self.assertTrue(any(r.endswith('Maho-1.0.0.dmg') for r in removed),
                        'must delete the invalid dmg on gate failure')

    def test_returns_none_when_staged_app_fails_strict_verify(self):
        def _run(cmd, *a, **k):
            if cmd[:1] == ['codesign'] and '--verify' in cmd:
                return MagicMock(returncode=1, stdout='', stderr='nested code invalid')
            return MagicMock(returncode=0, stdout='', stderr='')

        ctx = self._patches(_run, gate=True)
        for p in ctx:
            p.start()
        try:
            result = _bm.build_dmg('/out/Default/Maho.app', 'out/Default', '1.0.0', 'DEADBEEF')
        finally:
            for p in reversed(ctx):
                p.stop()

        self.assertIsNone(result)


class TestNotarizeDirectExecution(unittest.TestCase):

    def setUp(self):
        self.env = {
            'APPLE_API_KEY': 'fake-key-content',
            'APPLE_API_KEY_ID': 'FAKEKEYID1',
            'APPLE_API_ISSUER': 'fake-issuer-uuid',
        }

    def test_notarize_app_missing_env_skips(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertFalse(_bm.notarize_app('/fake/Maho.app'))

    def test_notarize_app_executes_submit_staple_and_verify(self):
        calls = []

        def fake_run(cmd, *args, **kwargs):
            calls.append(cmd)
            if cmd[:3] == ['xcrun', 'stapler', 'validate']:
                # Not yet stapled, so notarize_app must not take its
                # already-notarized fast path.
                return subprocess.CompletedProcess(cmd, 65, stdout='', stderr='')
            return subprocess.CompletedProcess(cmd, 0, stdout='id: 1234\nstatus: Accepted\n', stderr='')

        with tempfile.TemporaryDirectory() as tmp:
            app = os.path.join(tmp, 'Maho.app')
            os.makedirs(app)
            with patch.dict(os.environ, self.env, clear=True), \
                 patch.object(_bm.subprocess, 'run', side_effect=fake_run):
                self.assertTrue(_bm.notarize_app(app))

        self.assertTrue(any(c[0] == 'ditto' for c in calls), 'must create zip via ditto')
        self.assertTrue(any(c[:3] == ['xcrun', 'notarytool', 'submit'] for c in calls), 'must submit with notarytool')
        self.assertTrue(any(c[:3] == ['xcrun', 'stapler', 'staple'] for c in calls), 'must staple app bundle')
        self.assertTrue(any(c[:4] == ['spctl', '-a', '-t', 'exec'] for c in calls), 'must verify with spctl exec')

    def test_notarize_app_retries_on_transient_network_timeout(self):
        submit_count = 0

        def fake_run(cmd, *args, **kwargs):
            nonlocal submit_count
            if cmd[:3] == ['xcrun', 'stapler', 'validate']:
                return subprocess.CompletedProcess(cmd, 65, stdout='', stderr='')
            if cmd[:3] == ['xcrun', 'notarytool', 'submit']:
                submit_count += 1
                if submit_count == 1:
                    return subprocess.CompletedProcess(cmd, 1, stdout='', stderr='HTTPClientError.connectTimeout')
                return subprocess.CompletedProcess(cmd, 0, stdout='status: Accepted\n', stderr='')
            return subprocess.CompletedProcess(cmd, 0, stdout='', stderr='')

        with tempfile.TemporaryDirectory() as tmp:
            app = os.path.join(tmp, 'Maho.app')
            os.makedirs(app)
            with patch.dict(os.environ, self.env, clear=True), \
                 patch('time.sleep'), \
                 patch.object(_bm.subprocess, 'run', side_effect=fake_run):
                self.assertTrue(_bm.notarize_app(app))

        self.assertEqual(submit_count, 2, 'must retry after transient connect timeout')

    def test_notarize_dmg_executes_submit_staple_and_verify(self):
        calls = []

        def fake_run(cmd, *args, **kwargs):
            calls.append(cmd)
            return subprocess.CompletedProcess(cmd, 0, stdout='id: 5678\nstatus: Accepted\n', stderr='')

        with tempfile.TemporaryDirectory() as tmp:
            dmg = os.path.join(tmp, 'Maho.dmg')
            with open(dmg, 'wb') as f:
                f.write(b'dmg-data')
            with patch.dict(os.environ, self.env, clear=True), \
                 patch.object(_bm.subprocess, 'run', side_effect=fake_run):
                self.assertTrue(_bm.notarize_dmg(dmg))

        self.assertTrue(any(c[:3] == ['xcrun', 'notarytool', 'submit'] for c in calls), 'must submit dmg with notarytool')
        self.assertTrue(any(c[:3] == ['xcrun', 'stapler', 'staple'] for c in calls), 'must staple dmg')
        self.assertTrue(any(c[:4] == ['spctl', '-a', '-t', 'open'] for c in calls), 'must verify dmg with spctl open')


class TestMainDmgFatalPaths(unittest.TestCase):

    def _run(self, argv, build_dmg_ret, notarize_ret=True, notarize_app_ret=True):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()
        with ExitStack() as stack:
            stack.enter_context(
                patch.object(sys, 'argv', ['build_maho.py'] + argv))
            stack.enter_context(patch.object(sys, 'platform', 'darwin'))
            # Guard against cross-test leakage of the debug-profile env var and
            # against a real ad-hoc codesign if the distribution finder paths
            # ever change again.
            stack.enter_context(patch.dict('os.environ', {}, clear=False))
            stack.enter_context(
                patch.object(_bm, 'find_codesign_identity_release_dist',
                             return_value='FAKSHA'))
            stack.enter_context(
                patch.object(_bm, 'find_appstore_provisioning_profile',
                             return_value='/fake/profile.mobileprovision'))
            stack.enter_context(patch.object(_bm.subprocess, 'run'))
            for name in (
                'ensure_overlay_mount',
                'apply_tracked_chromium_overrides',
                'apply_branding',
                'run_lucide_icon_check',
                'verify_remote_sccache',
                'build_chromium_app',
                'embed_sparkle_framework',
                'embed_mail_helper',
                'ensure_args_gn',
                'sync_cc_wrapper',
                'sync_media_codec_flags',
                'ensure_macos_sdk_path',
                'set_bundle_product_version',
                'sync_local_google_api_key',
                'run_gn_gen',
            ):
                stack.enter_context(patch.object(_bm, name))
            stack.enter_context(
                patch.object(_bm, 'embed_omo_runtime', return_value=True))
            stack.enter_context(
                patch.object(_bm, 'build_maho_cli', return_value='/fake/maho'))
            stack.enter_context(
                patch.object(_bm, 'embed_maho_cli', return_value=True))
            stack.enter_context(
                patch.object(_bm, 'find_codesign_identity', return_value='FAKSHA'))
            stack.enter_context(patch.object(_bm, 'codesign_app', return_value=True))
            stack.enter_context(
                patch.object(_bm, 'read_maho_version', return_value='1.0.0'))
            stack.enter_context(patch('os.path.isfile', return_value=True))
            mock_notarize_app = stack.enter_context(
                patch.object(_bm, 'notarize_app', return_value=notarize_app_ret))
            mock_dmg = stack.enter_context(
                patch.object(_bm, 'build_dmg', return_value=build_dmg_ret))
            mock_notarize = stack.enter_context(
                patch.object(_bm, 'notarize_dmg', return_value=notarize_ret))
            mock_launch = stack.enter_context(patch.object(_bm, 'launch_app'))
            try:
                _bm.main()
                exited = None
            except SystemExit as e:
                exited = e.code
        return mock_dmg, mock_notarize, mock_notarize_app, mock_launch, exited

    def test_exits_nonzero_when_dmg_gate_fails(self):
        mock_dmg, mock_notarize, mock_notarize_app, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg', '--no-launch'], build_dmg_ret=None)
        mock_dmg.assert_called_once()
        self.assertIsNotNone(exited)
        self.assertNotEqual(exited, 0)
        mock_notarize.assert_not_called()
        mock_notarize_app.assert_not_called()

    def test_exits_nonzero_when_notarize_app_fails(self):
        mock_dmg, mock_notarize, mock_notarize_app, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg', '--notarize', '--no-launch'],
            build_dmg_ret='/out/Maho-1.0.0.dmg', notarize_app_ret=False)
        mock_notarize_app.assert_called_once()
        mock_dmg.assert_not_called()
        mock_notarize.assert_not_called()
        self.assertIsNotNone(exited)
        self.assertNotEqual(exited, 0)

    def test_exits_nonzero_when_notarize_dmg_fails(self):
        mock_dmg, mock_notarize, mock_notarize_app, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg', '--notarize', '--no-launch'],
            build_dmg_ret='/out/Maho-1.0.0.dmg', notarize_ret=False)
        mock_notarize_app.assert_called_once()
        mock_dmg.assert_called_once()
        mock_notarize.assert_called_once()
        self.assertIsNotNone(exited)
        self.assertNotEqual(exited, 0)

    def test_notarize_calls_both_app_and_dmg_in_order(self):
        mock_dmg, mock_notarize, mock_notarize_app, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg', '--notarize', '--no-launch'],
            build_dmg_ret='/out/Maho-1.0.0.dmg')
        mock_notarize_app.assert_called_once()
        mock_dmg.assert_called_once()
        mock_notarize.assert_called_once()
        self.assertIsNone(exited)

    def test_no_exit_when_dmg_ok_and_no_notarize(self):
        mock_dmg, mock_notarize, mock_notarize_app, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg'], build_dmg_ret='/out/Maho-1.0.0.dmg')
        self.assertIsNone(exited)
        mock_notarize.assert_not_called()
        mock_notarize_app.assert_not_called()
        mock_launch.assert_called_once()


@unittest.skipUnless(sys.platform == 'darwin', 'macOS-only real image synthesis')
class TestRealDmgSynthesis(unittest.TestCase):

    def test_real_dmg_synthesis_has_no_finderinfo(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = os.path.join(tmp, 'stage')
            app = os.path.join(stage, 'Maho.app', 'Contents', 'MacOS')
            os.makedirs(app)
            with open(os.path.join(app, 'Maho'), 'wb') as f:
                f.write(b'\xcf\xfa\xed\xfe payload')
            os.symlink('/Applications', os.path.join(stage, 'Applications'))
            subprocess.run(['xattr', '-cr', stage], check=True)
            dmg = os.path.join(tmp, 'probe.dmg')
            _bm._create_dmg_image(stage, dmg, 'Probe')
            self.assertTrue(os.path.isfile(dmg))
            # Reuse the production gate's mount+FinderInfo check via a direct
            # mount (codesign on an unsigned fake app would fail, so assert the
            # image-synthesis property this fix guarantees: 0 FinderInfo).
            import plistlib
            at = subprocess.run(
                ['hdiutil', 'attach', '-nobrowse', '-readonly', '-plist', dmg],
                capture_output=True, check=True)
            info = plistlib.loads(at.stdout)
            mnt = next(e['mount-point'] for e in info['system-entities'] if e.get('mount-point'))
            dev = min((e['dev-entry'] for e in info['system-entities'] if e.get('dev-entry')), key=len)
            try:
                dump = subprocess.run(['xattr', '-lr', os.path.join(mnt, 'Maho.app')],
                                      capture_output=True, text=True)
                finderinfo = sum(1 for ln in dump.stdout.splitlines()
                                 if 'com.apple.FinderInfo' in ln)
                self.assertEqual(finderinfo, 0,
                                 'hdiutil create must not inject FinderInfo detritus')
            finally:
                subprocess.run(['hdiutil', 'detach', dev, '-quiet'], capture_output=True)


if __name__ == '__main__':
    unittest.main()
