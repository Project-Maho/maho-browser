#!/usr/bin/env python3

import importlib.util
import os
import plistlib
import subprocess
import sys
import types
import unittest
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

    def test_signs_existing_bundle_with_sparkle(self):
        with patch('os.path.isdir', return_value=True), \
             patch('os.path.isfile', return_value=True), \
             patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0)
            result = codesign_app('DEADBEEF', '/fake/Maho.app')
        self.assertTrue(result)
        self.assertEqual(mock_run.call_count, 12)

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
             patch('os.path.isfile', return_value=True), \
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
        with patch('os.path.isdir', return_value=True), \
             patch('subprocess.run', return_value=MagicMock(returncode=0)):
            self.assertTrue(codesign_app('MYIDENTITY', '/fake/Maho.app'))


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


class TestChromiumSourceRoot(unittest.TestCase):

    def test_resolves_junctions_before_building(self):
        with patch('os.path.realpath', return_value='/physical/chromium/src') as mock_realpath:
            root = _bm.chromium_src_root('/workspace')

        self.assertEqual(root, '/physical/chromium/src')
        mock_realpath.assert_called_once_with('/workspace/chromium/src')


class TestMainFlags(unittest.TestCase):

    def _run_main(self, argv, platform='darwin'):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py'] + argv), \
             patch.object(sys, 'platform', platform), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'find_codesign_identity', return_value='FAKSHA') as mock_find, \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app') as mock_launch:
            _bm.main()

        return mock_find, mock_sign, mock_launch

    def test_default_darwin_codesigns_and_launches(self):
        mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust'])
        mock_find.assert_called_once()
        mock_sign.assert_called_once()
        mock_launch.assert_called_once()

    def test_skip_codesign_skips_sign_but_still_launches(self):
        mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust', '--skip-codesign'])
        mock_find.assert_not_called()
        mock_sign.assert_not_called()
        mock_launch.assert_called_once()

    def test_no_launch_skips_launch_but_still_signs(self):
        mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust', '--no-launch'])
        mock_sign.assert_called_once()
        mock_launch.assert_not_called()

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
             patch.object(_bm, 'find_codesign_identity') as mock_find, \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app'):
            _bm.main()

        mock_find.assert_not_called()
        mock_sign.assert_called_once_with('MYID', _bm._MAHO_APP_PATH)

    def test_skip_chromium_skips_codesign_and_launch(self):
        mock_find, mock_sign, mock_launch = self._run_main(['--skip-chromium'])
        mock_find.assert_not_called()
        mock_sign.assert_not_called()
        mock_launch.assert_not_called()

    def test_codesign_skipped_on_linux(self):
        mock_find, mock_sign, mock_launch = self._run_main(['--skip-rust'], platform='linux')
        mock_find.assert_not_called()
        mock_sign.assert_not_called()
        mock_launch.assert_called_once()

    def test_no_identity_prints_warning_does_not_crash(self):
        mock_lock_cm = MagicMock()
        mock_lock_cm.__enter__ = MagicMock(return_value=None)
        mock_lock_cm.__exit__ = MagicMock(return_value=False)
        _bm.build_lock = MagicMock(return_value=mock_lock_cm)
        _bm.build_maho_core.build_rust_prebuilt = MagicMock()

        with patch.object(sys, 'argv', ['build_maho.py', '--skip-rust']), \
             patch.object(sys, 'platform', 'darwin'), \
             patch.object(_bm, 'apply_tracked_chromium_overrides'), \
             patch.object(_bm, 'build_chromium_app'), \
             patch.object(_bm, 'find_codesign_identity', return_value=None), \
             patch.object(_bm, 'codesign_app') as mock_sign, \
             patch.object(_bm, 'launch_app'):
            _bm.main()

        mock_sign.assert_not_called()

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


if __name__ == '__main__':
    unittest.main()
