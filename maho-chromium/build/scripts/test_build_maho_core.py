#!/usr/bin/env python3

import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch


def _load_module():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build_maho_core.py')
    spec = importlib.util.spec_from_file_location('build_maho_core_under_test', path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_module = _load_module()


class TestMailOAuthBuildEnvironment(unittest.TestCase):
    def test_uses_public_default_when_environment_is_empty(self):
        with patch.dict(os.environ, {}, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(
            env['GMAIL_CLIENT_ID'],
            '650357306759-c6tn05kg1l60ved2ne6cf9jp33moanh0.apps.googleusercontent.com',
        )

    def test_uses_maho_google_client_id_for_gmail_when_not_overridden(self):
        with patch.dict(os.environ, {
            'MAHO_GOOGLE_CLIENT_ID': 'maho-public-client-id',
        }, clear=True):
            env = _module.mail_oauth_build_env()

        self.assertEqual(env['GMAIL_CLIENT_ID'], 'maho-public-client-id')

    def test_explicit_gmail_client_id_wins(self):
        with patch.dict(os.environ, {
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

    def test_mail_only_build_skips_main_rust_prebuilt(self):
        with patch.object(_module, 'build_mail_ffi_prebuilt') as build_mail:
            with patch.object(_module, 'build_rust_prebuilt') as build_main:
                _module.run_build(target=None, mail_only=True)

        build_mail.assert_called_once_with(
            'aarch64-apple-darwin',
            'arm64',
        )
        build_main.assert_not_called()

    def test_mail_helper_sandbox_allows_chromium_network_socket_requirements(self):
        helper_main = Path(__file__).parents[2] / 'browser' / 'mail_helper' / 'maho_mail_helper_main.cc'
        profile = helper_main.read_text(encoding='utf-8')

        self.assertIn('(control-name "com.apple.netsrc")', profile)
        self.assertIn('(remote udp)', profile)
        self.assertIn('(remote tcp)', profile)


if __name__ == '__main__':
    unittest.main()
