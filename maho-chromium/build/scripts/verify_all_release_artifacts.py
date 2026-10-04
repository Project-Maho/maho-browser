#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_PUBKEY = os.path.join(_WORKSPACE_ROOT, 'maho-chromium', 'build', 'keys', 'maho-release-pubkey.asc')

EXPECTED_LINUX_FILES = [
    'maho_1.0.1_amd64.deb',
    'maho_1.0.1_amd64.deb.sig',
    'maho-1.0.1-1.x86_64.rpm',
    'maho-1.0.1-1.x86_64.rpm.sig',
    'maho-1.0.1-x86_64.tar.gz',
    'maho-1.0.1-x86_64.tar.gz.sig',
]


def verify_gpg_sig(file_path: str, sig_path: str) -> bool:
    if not os.path.isfile(_PUBKEY):
        print(f"Error: Public key missing at {_PUBKEY}", file=sys.stderr)
        return False
    with tempfile.TemporaryDirectory(prefix='maho-verify-gpg-') as tmpdir:
        import_res = subprocess.run(
            ['gpg', '--batch', '--homedir', tmpdir, '--import', _PUBKEY],
            capture_output=True,
            text=True,
        )
        if import_res.returncode != 0:
            print(f"Failed to import pubkey: {import_res.stderr}", file=sys.stderr)
            return False
        verify_res = subprocess.run(
            ['gpg', '--batch', '--homedir', tmpdir, '--verify', sig_path, file_path],
            capture_output=True,
            text=True,
        )
        if verify_res.returncode != 0:
            print(f"GPG verify failed for {file_path}:\n{verify_res.stderr}", file=sys.stderr)
            return False
    print(f"Verified GPG signature: {sig_path} -> {file_path}")
    return True


def audit_packages_dir(packages_dir: str) -> bool:
    print(f"Auditing release artifacts in {packages_dir}...")
    all_ok = True
    for name in EXPECTED_LINUX_FILES:
        path = os.path.join(packages_dir, name)
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            print(f"Missing or empty expected artifact: {path}", file=sys.stderr)
            all_ok = False
        else:
            print(f"Found artifact: {name} ({os.path.getsize(path):,} bytes)")

    for pkg_name in ['maho_1.0.1_amd64.deb', 'maho-1.0.1-1.x86_64.rpm', 'maho-1.0.1-x86_64.tar.gz']:
        pkg_path = os.path.join(packages_dir, pkg_name)
        sig_path = pkg_path + '.sig'
        if os.path.isfile(pkg_path) and os.path.isfile(sig_path):
            if not verify_gpg_sig(pkg_path, sig_path):
                all_ok = False
        else:
            all_ok = False

    return all_ok


if __name__ == '__main__':
    target_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(_WORKSPACE_ROOT, 'out', 'Release', 'packages')
    if not audit_packages_dir(target_dir):
        sys.exit(1)
    print("All release artifacts verified successfully.")
