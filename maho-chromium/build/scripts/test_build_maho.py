#!/usr/bin/env python3

import importlib.util
import os
import plistlib
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
        self.assertEqual(mock_run.call_count, 17)

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
        with patch(
            'os.path.isdir',
            side_effect=lambda path: path == '/fake/Maho.app',
        ), \
             patch(
                 'os.path.isfile',
                 side_effect=lambda path: path.endswith((
                     'maho_mail_helper',
                     'helper-entitlements.plist',
                     'entitlements.plist',
                 )),
             ), \
             patch('subprocess.run') as mock_run:
            result = codesign_app('DEADBEEF', '/fake/Maho.app')

        self.assertTrue(result)
        helper_call, app_call = [call.args[0] for call in mock_run.call_args_list]
        self.assertTrue(
            helper_call[helper_call.index('--entitlements') + 1].endswith(
                'branding/mac/helper-entitlements.plist'
            )
        )
        self.assertTrue(
            app_call[app_call.index('--entitlements') + 1].endswith(
                'branding/mac/entitlements.plist'
            )
        )

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

    def test_non_release_does_not_sync_local_google_api_key(self):
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

            _bm.sync_local_google_api_key(workspace, args_path, release=False)

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


class TestMainFlags(unittest.TestCase):

    def setUp(self) -> None:
        for name in (
            'ensure_overlay_mount',
            'ensure_args_gn',
            'sync_local_google_api_key',
            'apply_tracked_chromium_overrides',
            'apply_branding',
            'run_lucide_icon_check',
            'run_gn_gen',
            'embed_sparkle_framework',
            'embed_mail_helper',
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
                         'apply_branding', 'run_lucide_icon_check', 'run_gn_gen',
                         'embed_sparkle_framework', 'embed_mail_helper'):
                stack.enter_context(patch.object(_bm, name))
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

    def test_uses_hdiutil_create_srcfolder_udzo_not_makehybrid(self):
        with patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0)
            _bm._create_dmg_image('/stage', '/out/Maho-1.0.0.dmg', 'Maho 1.0.0')
        argv = mock_run.call_args_list[0].args[0]
        self.assertEqual(argv[0], 'hdiutil')
        self.assertEqual(argv[1], 'create')
        self.assertIn('-srcfolder', argv)
        self.assertEqual(argv[argv.index('-srcfolder') + 1], '/stage')
        self.assertIn('-format', argv)
        self.assertEqual(argv[argv.index('-format') + 1], 'UDZO')
        self.assertIn('-volname', argv)
        self.assertEqual(argv[argv.index('-volname') + 1], 'Maho 1.0.0')
        self.assertEqual(argv[-1], '/out/Maho-1.0.0.dmg')
        self.assertNotIn('makehybrid', argv)


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


class TestMainDmgFatalPaths(unittest.TestCase):

    def _run(self, argv, build_dmg_ret, notarize_ret=True):
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
                'build_chromium_app',
                'embed_sparkle_framework',
                'embed_mail_helper',
                'ensure_args_gn',
                'sync_local_google_api_key',
                'run_gn_gen',
            ):
                stack.enter_context(patch.object(_bm, name))
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
        return mock_dmg, mock_notarize, mock_launch, exited

    def test_exits_nonzero_when_dmg_gate_fails(self):
        mock_dmg, mock_notarize, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg', '--no-launch'], build_dmg_ret=None)
        mock_dmg.assert_called_once()
        self.assertIsNotNone(exited)
        self.assertNotEqual(exited, 0)
        mock_notarize.assert_not_called()

    def test_exits_nonzero_when_notarize_fails(self):
        mock_dmg, mock_notarize, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg', '--notarize', '--no-launch'],
            build_dmg_ret='/out/Maho-1.0.0.dmg', notarize_ret=False)
        mock_notarize.assert_called_once()
        self.assertIsNotNone(exited)
        self.assertNotEqual(exited, 0)

    def test_no_exit_when_dmg_ok_and_no_notarize(self):
        mock_dmg, mock_notarize, mock_launch, exited = self._run(
            ['--skip-rust', '--dmg'], build_dmg_ret='/out/Maho-1.0.0.dmg')
        self.assertIsNone(exited)
        mock_notarize.assert_not_called()
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
