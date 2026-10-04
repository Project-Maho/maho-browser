#!/usr/bin/env python3

import importlib.util
import json
import os
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import call, patch


def _load_module():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build_maho_core.py')
    spec = importlib.util.spec_from_file_location('build_maho_core_under_test', path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_module = _load_module()


class TestMailOAuthBuildEnvironment(unittest.TestCase):
    @patch.object(_module.subprocess, 'run')
    def test_tool_probe_rejects_incompatible_host_binary(self, run):
        run.side_effect = OSError(8, 'Exec format error')

        self.assertFalse(_module._tool_runs('/path/to/wrong-arch-clang'))

    @patch.object(_module.subprocess, 'run')
    def test_tool_probe_accepts_runnable_host_binary(self, run):
        run.return_value.returncode = 0

        self.assertTrue(_module._tool_runs('/path/to/native-clang'))
        run.assert_called_once_with(
            ['/path/to/native-clang', '--version'],
            stdout=_module.subprocess.DEVNULL,
            stderr=_module.subprocess.DEVNULL,
            check=False,
        )

    def test_fails_closed_when_web_client_has_no_secret(self):
        with tempfile.TemporaryDirectory() as temp_dir, \
             patch.object(_module, '_WORKSPACE_ROOT', temp_dir), \
             patch.dict(os.environ, {}, clear=True):
            with self.assertRaises(RuntimeError) as raised:
                _module.mail_oauth_build_env()

        message = str(raised.exception)
        self.assertIn('GMAIL_CLIENT_SECRET', message)
        self.assertIn(
            '650357306759-c6tn05kg1l60ved2ne6cf9jp33moanh0.apps.googleusercontent.com',
            message,
        )

    def test_desktop_client_override_needs_no_secret(self):
        with tempfile.TemporaryDirectory() as temp_dir, \
             patch.object(_module, '_WORKSPACE_ROOT', temp_dir), \
             patch.dict(os.environ, {
                 'MAHO_GOOGLE_CLIENT_ID': '1234-abcd.apps.googleusercontent.com',
             }, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(
            env['GMAIL_CLIENT_ID'], '1234-abcd.apps.googleusercontent.com')
        self.assertNotIn('GMAIL_CLIENT_SECRET', env)

    def test_vault_client_secret_file_supplies_secret(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            vault_dir = Path(temp_dir) / '.secrets' / 'maho'
            vault_dir.mkdir(parents=True)
            vault_file = vault_dir / 'google_desktop_oauth.json'
            vault_file.write_text(json.dumps({
                'installed': {
                    'client_id': (
                        '650357306759-c6tn05kg1l60ved2ne6cf9jp33moanh0'
                        '.apps.googleusercontent.com'),
                    'client_secret': 'vault-supplied-secret',
                },
            }), encoding='utf-8')
            with patch.object(_module, '_WORKSPACE_ROOT', temp_dir), \
                 patch.dict(os.environ, {}, clear=True):
                env = _module.mail_oauth_build_env()

        self.assertEqual(env['GMAIL_CLIENT_SECRET'], 'vault-supplied-secret')

    def test_vault_client_id_mismatch_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            vault_dir = Path(temp_dir) / '.secrets' / 'maho'
            vault_dir.mkdir(parents=True)
            vault_file = vault_dir / 'google_desktop_oauth.json'
            vault_file.write_text(json.dumps({
                'installed': {
                    'client_id': '9999-other.apps.googleusercontent.com',
                    'client_secret': 'other-secret',
                },
            }), encoding='utf-8')
            with patch.object(_module, '_WORKSPACE_ROOT', temp_dir), \
                 patch.dict(os.environ, {}, clear=True):
                with self.assertRaises(RuntimeError) as raised:
                    _module.mail_oauth_build_env()

        self.assertIn('9999-other.apps.googleusercontent.com',
                      str(raised.exception))

    def test_uses_maho_google_client_id_for_gmail_when_not_overridden(self):
        with tempfile.TemporaryDirectory() as temp_dir, \
             patch.object(_module, '_WORKSPACE_ROOT', temp_dir), \
             patch.dict(os.environ, {
                 'MAHO_GOOGLE_CLIENT_ID': 'maho-public-client-id',
             }, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(env['GMAIL_CLIENT_ID'], 'maho-public-client-id')

    def test_explicit_gmail_client_id_wins(self):
        with tempfile.TemporaryDirectory() as temp_dir, \
             patch.object(_module, '_WORKSPACE_ROOT', temp_dir), \
             patch.dict(os.environ, {
                 'MAHO_GOOGLE_CLIENT_ID': 'maho-public-client-id',
                 'GMAIL_CLIENT_ID': 'mail-specific-client-id',
             }, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(env['GMAIL_CLIENT_ID'], 'mail-specific-client-id')

    def test_preserves_original_tauri_gmail_client_secret(self):
        with patch.dict(os.environ, {
            'GMAIL_CLIENT_SECRET': 'original-tauri-secret',
        }, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(env['GMAIL_CLIENT_SECRET'], 'original-tauri-secret')

    def test_platform_specific_env_wins_over_generic(self):
        plat = _module._platform_name().upper()
        with patch.dict(os.environ, {
            f'MAHO_GOOGLE_CLIENT_ID_{plat}': 'platform-specific-id',
            f'MAHO_GOOGLE_CLIENT_SECRET_{plat}': 'platform-specific-secret',
            'MAHO_GOOGLE_CLIENT_ID': 'generic-id',
            'MAHO_GOOGLE_CLIENT_SECRET': 'generic-secret',
        }, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(env['GMAIL_CLIENT_ID'], 'platform-specific-id')
        self.assertEqual(env['GMAIL_CLIENT_SECRET'], 'platform-specific-secret')

    def test_linux_chromium_toolchain_env_covers_c_cxx_ar_and_sysroot(self):
        chromium_src = os.path.join(_module.WORKSPACE_ROOT, 'chromium', 'src')
        clang_dir = os.path.join(
            chromium_src, 'third_party', 'llvm-build', 'Release+Asserts', 'bin')
        sysroot = os.path.join(
            chromium_src, 'build', 'linux',
            'debian_bullseye_arm64-sysroot')
        target = 'aarch64-unknown-linux-gnu'
        target_env = target.replace('-', '_')

        with patch.object(_module.os.path, 'isfile', return_value=True), \
             patch.object(_module.os.path, 'isdir', return_value=True), \
             patch.object(_module, '_tool_runs', return_value=True):
            env = _module.chromium_linux_toolchain_env(
                target,
                {
                    f'CFLAGS_{target_env}': '-O2',
                    f'CXXFLAGS_{target_env}': '-stdlib=libc++',
                },
            )

        self.assertEqual(env[f'CC_{target_env}'], os.path.join(clang_dir, 'clang'))
        self.assertEqual(env[f'CXX_{target_env}'], os.path.join(clang_dir, 'clang++'))
        self.assertEqual(env[f'AR_{target_env}'], os.path.join(clang_dir, 'llvm-ar'))
        for flags_name in (f'CFLAGS_{target_env}', f'CXXFLAGS_{target_env}'):
            self.assertIn(f'--target={target}', env[flags_name])
            self.assertIn(f'--sysroot={sysroot}', env[flags_name])
        self.assertIn('-O2', env[f'CFLAGS_{target_env}'])
        self.assertIn('-stdlib=libc++', env[f'CXXFLAGS_{target_env}'])

    def test_mail_prebuilt_uses_linux_chromium_toolchain_and_privatization(self):
        target = 'aarch64-unknown-linux-gnu'
        toolchain_env = {'CC_aarch64_unknown_linux_gnu': '/chromium/clang'}
        with patch.object(
                 _module, 'mail_oauth_build_env', return_value={'oauth': 'configured'}), \
             patch.object(
                 _module, 'chromium_linux_toolchain_env',
                 return_value=toolchain_env) as linux_env, \
             patch.object(_module, '_cargo_build') as cargo_build, \
             patch.object(_module.os.path, 'isdir', return_value=True), \
             patch.object(_module.os.path, 'exists', return_value=False):
            with self.assertRaises(SystemExit):
                _module.build_mail_ffi_prebuilt(target, 'arm64')

        linux_env.assert_called_once_with(target, {'oauth': 'configured'})
        cargo_build.assert_called_once_with(
            ['cargo'],
            os.path.join(_module.MAHO_ROOT, 'mail-core'),
            target,
            env=toolchain_env,
        )

    def test_mail_prebuilt_preserves_symbol_privatization(self):
        target = 'aarch64-unknown-linux-gnu'
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            maho_root = root / 'maho'
            crate_dir = maho_root / 'mail-core'
            release_dir = crate_dir / 'target' / target / 'release'
            release_dir.mkdir(parents=True)
            source = release_dir / 'libmaho_mail_ffi.a'
            source.write_bytes(b'archive')
            prebuilt_dir = root / 'prebuilt'

            def place(source_path, destination_path, *_args):
                Path(destination_path).write_bytes(Path(source_path).read_bytes())

            with patch.object(_module, 'MAHO_ROOT', str(maho_root)), \
                 patch.object(_module, 'PREBUILT_DIR', str(prebuilt_dir)), \
                 patch.object(_module, '_cargo_build'), \
                 patch.object(
                     _module, 'mail_oauth_build_env', return_value={}), \
                 patch.object(
                     _module, 'chromium_linux_toolchain_env',
                     return_value={}), \
                 patch.object(
                     _module, '_atomic_place_lib', side_effect=place) as place_lib:
                _module.build_mail_ffi_prebuilt(target, 'arm64')

        place_lib.assert_called_once_with(
            str(source),
            str(prebuilt_dir / 'arm64' / 'libmaho_mail_ffi.a'),
            str(release_dir),
            'mahomail',
        )

    def test_main_prebuilt_uses_shared_linux_chromium_toolchain_env(self):
        target = 'x86_64-unknown-linux-gnu'
        toolchain_env = {'CC_x86_64_unknown_linux_gnu': '/chromium/clang'}
        with patch.object(
                 _module, 'get_target_triple',
                 return_value=(target, 'x64', 'libmaho_ffi.a')), \
             patch.object(
                 _module, 'chromium_linux_toolchain_env',
                 return_value=toolchain_env) as linux_env, \
             patch.object(_module, '_cargo_build') as cargo_build, \
             patch.object(_module.os.path, 'exists', return_value=False):
            with self.assertRaises(SystemExit):
                _module.build_rust_prebuilt(target)

        linux_env.assert_called_once()
        self.assertEqual(linux_env.call_args.args[0], target)
        cargo_build.assert_called_once_with(
            ['cargo'], _module.MAHO_ROOT, target, 'maho-ffi', toolchain_env)

    def test_mail_only_build_skips_main_rust_prebuilt(self):
        with patch.object(_module, 'build_mail_ffi_prebuilt') as build_mail:
            with patch.object(_module, 'build_rust_prebuilt') as build_main:
                _module.run_build(target=None, mail_only=True)

        build_mail.assert_called_once_with(
            'aarch64-apple-darwin',
            'arm64',
        )
        build_main.assert_not_called()

    def test_windows_mail_prebuilt_uses_coff_library_name(self):
        with patch.object(_module, '_cargo_build'), \
             patch.object(_module, 'mail_oauth_build_env', return_value={}), \
             patch.object(_module.os.path, 'isdir', return_value=True), \
             patch.object(_module.os.path, 'exists', return_value=False):
            with self.assertRaises(SystemExit) as error:
                _module.build_mail_ffi_prebuilt(
                    'x86_64-pc-windows-msvc', 'x64')

        self.assertIn('maho_mail_ffi.lib', str(error.exception))

    def test_bundled_c_staticlibs_include_openssl_and_sqlcipher(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            release = Path(temp_dir)
            libraries = (
                release / 'build' / 'openssl-sys-test' / 'out' / 'build'
                / 'lib'
            )
            libraries.mkdir(parents=True)
            crypto = libraries / 'libcrypto.a'
            ssl = libraries / 'libssl.a'
            sqlcipher = (
                release / 'build' / 'libsqlite3-sys-test' / 'out'
                / 'libsqlcipher.a'
            )
            sqlcipher_coff = (
                release / 'build' / 'libsqlite3-sys-test' / 'out'
                / 'sqlcipher.lib'
            )
            sqlcipher.parent.mkdir(parents=True)
            for library in (crypto, ssl, sqlcipher, sqlcipher_coff):
                library.touch()

            discovered = {
                Path(path) for path in _module._bundled_c_staticlibs(
                    str(release))
            }

        self.assertEqual(
            discovered,
            {crypto, ssl, sqlcipher, sqlcipher_coff},
        )

    def test_darwin_placement_privatizes_all_bundled_c_symbols(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / 'source.a'
            destination = root / 'destination.a'
            source.write_bytes(b'archive')
            with patch.object(_module.sys, 'platform', 'darwin'), \
                 patch.object(
                     _module, '_find_llvm_tools',
                     return_value=('llvm-nm', 'llvm-ar', 'llvm-objcopy'),
                 ), \
                 patch.object(
                     _module, '_bundled_c_staticlibs',
                     return_value=['libcrypto.a', 'libsqlcipher.a'],
                 ) as bundled_libraries, \
                 patch.object(
                     _module, '_defined_extern_symbols',
                     return_value={'ASN1_TYPE_get', 'sqlite3_open'},
                 ), \
                 patch.object(_module, '_weaken_macho_symbols'), \
                 patch.object(_module.subprocess, 'run') as run:
                _module._atomic_place_lib(
                    str(source),
                    str(destination),
                    str(root),
                    'mahomail',
                )

        bundled_libraries.assert_called_once_with(str(root))
        redefine_call = run.call_args_list[0]
        self.assertEqual(redefine_call.kwargs, {'check': True})
        self.assertEqual(redefine_call.args[0][0], 'llvm-objcopy')
        self.assertTrue(redefine_call.args[0][1].startswith('--redefine-syms='))
        self.assertEqual(run.call_args_list[1], call(
            ['ranlib', '-c', str(destination) + '.tmp'],
            check=True,
        ))

    def test_linux_mail_helper_denies_arbitrary_unix_ipc_without_secret_service(self):
        overlay = Path(__file__).parents[2]
        helper_dir = overlay / 'browser' / 'mail_helper'
        build_gn = (
            overlay / 'third_party' / 'maho' / 'BUILD.gn'
        ).read_text(encoding='utf-8')
        mail_config = build_gn.split(
            'config("maho_mail_ffi_prebuilt_config")', 1
        )[1].split('source_set("maho_mail_ffi")', 1)[0]
        helper_build = (helper_dir / 'BUILD.gn').read_text(encoding='utf-8')
        sandbox = (
            helper_dir / 'maho_mail_helper_sandbox_linux.cc'
        ).read_text(encoding='utf-8')
        sandbox_tests = (
            helper_dir / 'maho_mail_helper_sandbox_linux_unittest.cc'
        ).read_text(encoding='utf-8')
        mail_root = Path(__file__).parents[3] / 'maho' / 'mail-core'
        core_cargo = (
            mail_root / 'vendor' / 'maho-core' / 'Cargo.toml'
        ).read_text(encoding='utf-8')
        core_lib = (
            mail_root / 'vendor' / 'maho-core' / 'src' / 'lib.rs'
        ).read_text(encoding='utf-8')
        runtime_export = (
            mail_root / 'vendor' / 'maho-core' / 'src' / 'transfer'
            / 'runtime_export.rs'
        ).read_text(encoding='utf-8')
        io_api = (
            mail_root / 'src' / 'ffi' / 'io_api.rs'
        ).read_text(encoding='utf-8')

        self.assertNotIn('"dbus-1"', mail_config)
        self.assertNotIn('"dbus-1"', helper_build)
        self.assertNotIn('Secret Service', helper_build)
        self.assertIn('.Case(AF_UNIX, Error(EPERM))', sandbox)
        self.assertIn('case __NR_socketpair:', sandbox)
        self.assertIn('RestrictMailSocketPair()', sandbox)
        self.assertIn(
            'DeniesArbitraryUnixSocketTransportWithEperm',
            sandbox_tests,
        )
        self.assertIn(
            'AllowsConnectedUnixStreamSocketPairsForMojoOnly',
            sandbox_tests,
        )
        self.assertNotIn(
            'AllowsSecretServiceDbusSocketTransport',
            sandbox_tests,
        )
        self.assertIn('legacy-ffi = ["dep:keyring"]', core_cargo)
        self.assertIn('optional = true', core_cargo)
        self.assertIn(
            '#[cfg(feature = "legacy-ffi")]\npub mod credential_store;',
            core_lib,
        )
        self.assertNotIn('keyring_password(', runtime_export)
        self.assertIn('sqlcipher_key: &str', runtime_export)
        self.assertIn('credential_key: &[u8; 32]', runtime_export)
        self.assertIn('&ctx.sqlcipher_key', io_api)
        self.assertIn('&ctx.credential_key', io_api)

    def test_windows_smime_backend_is_native_and_openssl_is_target_scoped(self):
        mail_root = Path(__file__).parents[3] / 'maho' / 'mail-core'
        cargo = (mail_root / 'Cargo.toml').read_text(encoding='utf-8')
        windows = (
            mail_root / 'src' / 'ffi' / 'crypto_api' / 'smime' / 'windows.rs'
        ).read_text(encoding='utf-8')

        self.assertIn("[target.'cfg(not(windows))'.dependencies]", cargo)
        self.assertIn("[target.'cfg(windows)'.dependencies]", cargo)
        self.assertNotIn('\nopenssl =', cargo.split(
            "[target.'cfg(not(windows))'.dependencies]", 1)[0])
        for api in (
            'PFXImportCertStore',
            'CryptSignMessage',
            'CryptEncryptMessage',
            'CryptDecryptMessage',
            'NCryptImportKey',
            'open_named_key(&provider_name, &key_name)',
            'NCryptOpenKey',
            'NCryptDeleteKey',
            'NCRYPT_EXPORT_POLICY_PROPERTY',
        ):
            self.assertIn(api, windows)
        import_body = windows.split('fn import_pkcs12', 1)[1].split(
            'fn sign', 1)[0]
        self.assertLess(
            import_body.index('canonical_profile_hash(scope.profile_path)?'),
            import_body.index('PFXImportCertStore'),
        )
        self.assertLess(
            import_body.index('account_hash(scope.account_id)?'),
            import_body.index('PFXImportCertStore'),
        )
        self.assertGreaterEqual(
            windows.count('let mut buffer = vec![0usize;'),
            2,
        )
        self.assertIn(
            'wide_ptr_to_string(info.pwszProvName, &buffer, size as usize)',
            windows,
        )
        self.assertIn('address < start', windows)
        self.assertIn('.checked_add(valid_size)', windows)
        self.assertIn(
            '(size as usize) > std::mem::size_of_val(buffer.as_slice())',
            windows,
        )
        self.assertIn('if size != 32', windows)
        self.assertIn('size as usize != bytes.len()', windows)
        self.assertNotIn('&(*(*cert).pCertInfo)', windows)
        self.assertIn('checked_add(word_size - 1)', windows)
        self.assertIn('info.PublicKey.cbData == 0', windows)
        self.assertIn('blob.cbData == 0 || blob.pbData.is_null()', windows)
        self.assertIn('u32::try_from(der.len())', windows)
        self.assertIn('written <= 1 || written > size', windows)
        self.assertIn('size as usize != size_of::<u32>()', windows)
        self.assertNotIn('len() as u32', windows)
        self.assertIn('fn win32_len(', windows)
        self.assertIn('(size as usize) > output.len()', windows)
        self.assertIn('if duplicate.is_null()', windows)
        self.assertIn('CryptReleaseContext(handle as usize, 0)', windows)
        self.assertIn('fn fail_after_delete<T>', windows)
        self.assertIn('CertFreeCertificateContext(signer)', windows)

    def test_mail_helper_is_in_all_desktop_build_and_windows_package_contracts(self):
        overlay = Path(__file__).parents[2]
        browser_gn = (overlay / 'browser' / 'BUILD.gn').read_text(encoding='utf-8')
        overrides = (
            overlay / 'build' / 'scripts' / 'apply_chromium_src_overrides.py'
        ).read_text(encoding='utf-8')
        launcher = (
            overlay / 'browser' / 'mail_helper' / 'maho_mail_helper_launcher.cc'
        ).read_text(encoding='utf-8')

        self.assertIn(
            'deps += [ "//maho/browser/mail_helper:maho_mail_helper" ]',
            browser_gn,
        )
        self.assertNotIn('if (!is_win)', browser_gn)
        self.assertIn('maho_mail_helper.exe: %(ChromeDir)s', overrides)
        self.assertIn('base::DIR_EXE', launcher)
        self.assertIn('maho_mail_helper.exe', launcher)

    def test_mail_helper_crashpad_startup_is_best_effort_when_handler_missing(self):
        helper_main = (
            Path(__file__).parents[2] / 'browser' / 'mail_helper'
            / 'maho_mail_helper_main.cc'
        ).read_text(encoding='utf-8')

        self.assertIn(
            'if (!base::PathExists(handler_path))',
            helper_main,
        )
        self.assertIn(
            'continuing without crash reporting.',
            helper_main,
        )

    def test_mail_helper_generic_backend_denies_pgp_export_before_ffi(self):
        helper_main = (
            Path(__file__).parents[2] / 'browser' / 'mail_helper'
            / 'maho_mail_helper_main.cc'
        ).read_text(encoding='utf-8')
        generic_dispatch = helper_main.split(
            'void CallBackend(const std::string& command,', 1
        )[1]
        export_branch = generic_dispatch.split(
            'if (command == "ExportPgpKey") {', 1
        )[1].split('if (command == "ListPgpKeys") {', 1)[0]

        self.assertIn('generic PGP export is not permitted', export_branch)
        self.assertNotIn('MahoMailExportPgpKey', export_branch)
        self.assertNotIn('MahoMailReadBridge::Start', export_branch)

    def test_desktop_agent_reauthorizes_mail_reads_at_completion(self):
        browser_dir = Path(__file__).parents[2] / 'browser'
        executor = (
            browser_dir / 'ai' / 'maho_browser_tool_executor.cc'
        ).read_text(encoding='utf-8')
        authorization = (
            browser_dir / 'ai' / 'maho_mail_tool_authorization.h'
        ).read_text(encoding='utf-8')
        tests = (
            browser_dir / 'ai' / 'maho_browser_tool_executor_unittest.cc'
        ).read_text(encoding='utf-8')
        completion = executor.split(
            'void MahoBrowserToolExecutor::OnMailToolResult(', 1
        )[1].split(
            'void MahoBrowserToolExecutor::ExecuteBrowserAction(', 1
        )[0]

        self.assertIn('GetMailAuthorizationContext()', completion)
        self.assertIn('AuthorizeMailTool(tool_name, context)', completion)
        self.assertIn('mail_helper_generation_changed', completion)
        self.assertLess(
            completion.index('AuthorizeMailTool(tool_name, context)'),
            completion.index('base::JSONReader::Read(result_json'),
        )
        self.assertIn('uint64_t helper_generation = 0;', authorization)
        for probe in (
            'InFlightMailReadFailsClosedAfterConsentRevocation',
            'InFlightMailReadFailsClosedAfterFeatureDisable',
            'InFlightMailReadFailsClosedAfterHelperGenerationChange',
            'InFlightMailReadFailsClosedAfterGlobalPolicyRevocation',
            'MailCompletionRecheckInventoryIsReadOnly',
        ):
            self.assertIn(probe, tests)

    def test_mail_helper_sandbox_allows_chromium_network_socket_requirements(self):
        helper_main = Path(__file__).parents[2] / 'browser' / 'mail_helper' / 'maho_mail_helper_main.cc'
        profile = helper_main.read_text(encoding='utf-8')

        self.assertIn('(control-name "com.apple.netsrc")', profile)
        self.assertIn('(remote udp)', profile)
        self.assertIn('(remote tcp)', profile)

    def test_macos_mail_helper_sandbox_has_no_global_temp_access(self):
        helper_main = (
            Path(__file__).parents[2] / 'browser' / 'mail_helper'
            / 'maho_mail_helper_main.cc'
        ).read_text(encoding='utf-8')

        self.assertNotIn('(param "TEMP_PATH")', helper_main)
        self.assertNotIn('"TEMP_PATH"', helper_main)
        self.assertNotIn('base::GetTempDir(&temp_dir)', helper_main)
        self.assertIn(
            'mail_root.AppendASCII("Crashpad")',
            helper_main,
        )

    def test_linux_mail_sandbox_allows_nss_runtime_reads(self):
        helper_dir = Path(__file__).parents[2] / 'browser' / 'mail_helper'
        build_gn = (
            helper_dir / 'BUILD.gn'
        ).read_text(encoding='utf-8')
        sandbox = (
            helper_dir / 'maho_mail_helper_sandbox_linux.cc'
        ).read_text(encoding='utf-8')
        tests = (
            helper_dir / 'maho_mail_helper_sandbox_linux_unittest.cc'
        ).read_text(encoding='utf-8')

        self.assertIn('FILE_PATH_LITERAL("libnss_*.so*")', sandbox)
        self.assertIn(
            'base::FileEnumerator::FolderSearchPolicy::ALL',
            sandbox,
        )
        self.assertIn('"/etc/ld.so.cache"', sandbox)
        self.assertIn(
            'LoadsNssModuleAfterSandboxEngagement',
            tests,
        )
        self.assertIn('CloseSuperfluousFds(preserved_fds)', sandbox)
        self.assertIn('ClosesUnlistedInheritedDescriptors', tests)
        self.assertIn('MailRootHasNoExternalLinks(canonical_root)', sandbox)
        self.assertIn('SymlinkInsideMailRootFailsClosed', tests)
        self.assertIn('HardLinkInsideMailRootFailsClosed', tests)
        self.assertIn('option == IP_RECVERR', sandbox)
        self.assertIn(
            'AllowsResolverErrorQueueOptionsAfterSandboxEngagement',
            tests,
        )
        self.assertIn(
            'RestrictCloneToThreadsAndEPERMFork()',
            sandbox,
        )
        self.assertIn(
            'AllowsThreadCreationAfterSandboxEngagement',
            tests,
        )
        self.assertIn('option == IP_RECVERR', sandbox)
        self.assertIn(
            'AllowsResolverErrorQueueOptionsAfterSandboxEngagement',
            tests,
        )
        sandbox_target = build_gn.split(
            'test("maho_mail_helper_sandbox_test")', 1
        )[1]
        self.assertIn('libs = [ "dl" ]', sandbox_target)

    def test_desktop_mail_notification_permission_provider_is_installed(
            self) -> None:
        overlay = Path(__file__).parents[2]
        browser_root = Path(__file__).parents[2] / 'browser'
        main_parts = (
            browser_root / 'maho_browser_main_extra_parts.cc'
        ).read_text(encoding='utf-8')
        generic = (
            browser_root / 'mail_helper' /
            'maho_mail_notification_permission.cc'
        ).read_text(encoding='utf-8')
        mac = (
            browser_root / 'mail_helper' /
            'maho_mail_notification_permission_mac.mm'
        ).read_text(encoding='utf-8')
        windows = (
            browser_root / 'mail_helper' /
            'maho_mail_notification_permission_win.cc'
        ).read_text(encoding='utf-8')
        override_script = (
            overlay / 'build' / 'scripts' /
            'apply_chromium_src_overrides.py'
        ).read_text(encoding='utf-8')

        self.assertIn(
            'InstallMahoMailNotificationPermissionCallbacks();',
            main_parts,
        )
        self.assertIn(
            'MailOsNotificationPermission::kGranted',
            generic,
        )
        self.assertIn('RoGetActivationFactory', windows)
        self.assertIn('get_Setting', windows)
        # The toast notifier must be created with Chromium's registered
        # browser model ID. The per-process explicit AUMID is unset in the
        # browser process, so using it yields an unregistered app id and
        # Windows silently drops every Maho Mail toast.
        self.assertIn(
            'ShellUtil::GetBrowserModelId(InstallUtil::IsPerUserInstall())',
            windows,
        )
        self.assertIn(
            '#include "chrome/installer/util/install_util.h"',
            windows,
        )
        self.assertIn(
            '#include "chrome/installer/util/shell_util.h"',
            windows,
        )
        self.assertNotIn(
            'GetCurrentProcessExplicitAppUserModelID',
            windows,
        )
        mail_build_gn = (
            browser_root / 'mail_helper' / 'BUILD.gn'
        ).read_text(encoding='utf-8')
        windows_target = mail_build_gn.split(
            'sources += [ "maho_mail_notification_permission_win.cc" ]', 1
        )[1]
        self.assertIn('"//chrome/installer/util:with_no_strings"', windows_target)
        self.assertIn(
            'getNotificationSettingsWithCompletionHandler',
            mac,
        )
        self.assertIn(
            'requestAuthorizationWithOptions',
            mac,
        )
        self.assertIn(
            'UNAuthorizationStatusDenied',
            mac,
        )
        self.assertIn(
            '"    MAHO_MAIL = 12,\\n"',
            override_script,
        )
        self.assertIn(
            '"    MAHO_MAIL = 5,\\n"',
            override_script,
        )
        self.assertIn(
            '"    MAX = MAHO_MAIL,\\n"',
            override_script,
        )
        self.assertIn(
            'NotificationHandler::Type::MAHO_MAIL',
            main_parts,
        )
        self.assertIn(
            'static_assert(NotificationHandler::Type::MAHO_MAIL ==',
            main_parts,
        )
        self.assertIn('NotificationHandler::Type::MAX);', main_parts)
        self.assertIn(
            'static_cast<int>(NotificationHandler::Type::EXTENSION_REQUEST)',
            main_parts,
        )
        self.assertNotIn(
            'Display(NotificationHandler::Type::TRANSIENT',
            main_parts,
        )
        self.assertIn('settings_query_pending = false', mac)
        self.assertIn('request_pending = false', mac)
        self.assertIn('pending_status_callbacks', mac)
        self.assertIn('pending_request_callbacks', mac)
        self.assertIn('RefreshSettings', mac)
        self.assertNotIn('g_authorization_status', mac)

    def test_desktop_mail_lifecycle_barrier_drives_the_real_service_test(self) -> None:
        mail_helper = Path(__file__).parents[2] / 'browser' / 'mail_helper'
        service_test = (
            mail_helper / 'maho_mail_service_unittest.cc'
        ).read_text(encoding='utf-8')
        support_header = (
            mail_helper / 'maho_mail_integration_test_support.h'
        ).read_text(encoding='utf-8')

        self.assertIn(
            'base::test::TaskEnvironment::TimeSource::MOCK_TIME',
            service_test,
        )
        prepare_replies = re.findall(
            r'task_environment_\.RunUntilIdle\(\);'
            r'(?:\n.*){0,8}\n\s*helper\.ReplyPrepareShutdown\(\);',
            service_test,
        )
        self.assertEqual(len(prepare_replies), 3)
        self.assertIn('test::MailReplyBarrier barrier;', service_test)
        self.assertIn('barrier.Park(', service_test)
        self.assertIn('barrier.Release();', service_test)
        self.assertNotIn('MailEventDriver', support_header)
        self.assertNotIn('GetFixedMailFixture', support_header)

    def test_desktop_mail_sync_interval_is_canonical_minutes(self) -> None:
        root = Path(__file__).parents[3]
        settings = (
            root / 'maho-chromium/browser/resources/maho_settings/react/'
            'mail_behavior_prefs.ts'
        ).read_text(encoding='utf-8')
        mail_settings = (
            root / 'maho-chromium/browser/resources/maho_mail/react/hooks/'
            'useSettings.ts'
        ).read_text(encoding='utf-8')
        mail_api = (
            root / 'maho-chromium/browser/resources/maho_mail/react/api/'
            'index.ts'
        ).read_text(encoding='utf-8')
        helper = (
            root / 'maho-chromium/browser/mail_helper/'
            'maho_mail_helper_main.cc'
        ).read_text(encoding='utf-8')
        sync = (root / 'maho/mail-core/src/sync.rs').read_text(
            encoding='utf-8'
        )

        for source in (settings, mail_settings):
            self.assertIn('sync_interval: 15', source)
        self.assertIn('interval_minutes: intervalMinutes', mail_api)
        self.assertIn('get_int("interval_minutes", 15)', helper)
        self.assertIn('MahoMailSetSyncIntervalMinutes', helper)
        self.assertIn('SYNC_INTERVAL_MINUTES.load', sync)
        self.assertIn('matches!(minutes, 5 | 15 | 30 | 60)', sync)

    def test_macos_attachment_launch_verifies_validated_inode(self) -> None:
        root = Path(__file__).parents[3]
        launcher = (
            root / 'maho-chromium/browser/ui/webui/maho_mail/'
            'maho_mail_attachment_launcher_mac.mm'
        ).read_text(encoding='utf-8')

        self.assertIn('fstat(launch_file.GetPlatformFile()', launcher)
        self.assertIn('stat(reference_url.fileSystemRepresentation', launcher)
        self.assertIn('descriptor_stat.st_dev == reference_stat.st_dev', launcher)
        self.assertIn('descriptor_stat.st_ino == reference_stat.st_ino', launcher)
        self.assertIn('!ReferenceMatchesLaunchFile(reference_url, launch_file)', launcher)


if __name__ == '__main__':
    unittest.main()
