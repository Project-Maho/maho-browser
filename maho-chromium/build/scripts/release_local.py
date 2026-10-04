#!/usr/bin/env python3
"""Local per-machine release: build, bundle, manifest, and publish.

Each of the three build machines runs THIS script on its own OS. It detects the
host platform, produces only that platform's release artifacts, generates the
matching update manifest, and idempotently uploads them to the single GitHub
release on the release-only repo. Run it on all three (mac / windows / linux in
WSL) and the one release accumulates every artifact.

  macOS   -> Maho-<v>.dmg + appcast.xml + release-notes.html
  Windows -> MahoSetup-<v>.exe + update-win.json
  Linux   -> maho-<v>-x86_64.tar.gz + maho_<v>_amd64.deb + maho-<v>-1.*.rpm (+ .sig)

The release is created on first run and reused afterwards, so machine order does
not matter. Uploads use --clobber so re-runs replace prior assets. Keep the
release draft while every platform uploads; run the final platform invocation
with --finalize only after all expected assets have uploaded.

Prerequisites per machine:
  - A completed toolchain (depot_tools, gclient sync) for the Chromium build.
  - `gh` authenticated locally (gh auth status) with push access to the repo.
  - macOS: Developer ID Application signing identity +
    APPLE_API_KEY/_KEY_ID/_ISSUER (notarization is mandatory),
    SPARKLE_ED_PRIVATE_KEY (for the signed appcast).
  - Windows: MAHO_WIN_CERT_PFX(+_PASSWORD) or MAHO_WIN_CERT_SHA1 for signtool.
  - Linux: dpkg-deb + rpmbuild; gpg key 51C8C6C55603AFF0 for package .sig files
    (public key committed at maho-chromium/build/keys/maho-release-pubkey.asc;
    override id via MAHO_GPG_KEY_ID).
"""

import argparse
import base64
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from email.utils import formatdate
from typing import Optional, Sequence

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_CHROMIUM_SRC = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_VERSION_TXT = os.path.join(_WORKSPACE_ROOT, 'maho', 'version.txt')

_BUILD_MAHO = os.path.join(_SCRIPT_DIR, 'build_maho.py')
_BUILD_LINUX = os.path.join(_SCRIPT_DIR, 'build_maho_linux_packages.py')
_GEN_MANIFESTS = os.path.join(_SCRIPT_DIR, 'generate_update_manifests.py')
_PACKAGE_MSIX = os.path.join(_SCRIPT_DIR, 'package_windows_msix.py')
_GEN_CHANGELOG = os.path.join(_SCRIPT_DIR, 'generate_changelog_json.py')
_VERIFY_MACOS_RELEASE = os.path.join(_SCRIPT_DIR, 'verify_macos_release.py')

DEFAULT_REPO = os.environ.get('MAHO_RELEASE_REPO', 'Project-Maho/release')
DEFAULT_RELAY_URL = os.environ.get(
    'MAHO_RELAY_URL', 'https://relay.mahobrowser.com/updates/release'
)
MANIFEST_PUBKEY_HEX = (
    '8b076b75cb7896810997c59b1ec7e184730d714ca0679e0deae22f7aba0d4dbc'
)
MANIFEST_PUBKEY_BYTES = bytes.fromhex(MANIFEST_PUBKEY_HEX)

_FINAL_RELEASE_ASSETS = (
    'Maho-{version}.dmg',
    'appcast.xml',
    'release-notes.html',
    'MahoSetup-{version}.exe',
    'update-win.json',
    'MahoBrowser_{version}.0_x64.msix',
    'maho-{version}-x86_64.tar.gz',
    'maho-{version}-x86_64.tar.gz.sig',
    'maho_{version}_amd64.deb',
    'maho_{version}_amd64.deb.sig',
    'maho-{version}-1.x86_64.rpm',
    'maho-{version}-1.x86_64.rpm.sig',
)


class ManifestVerificationError(Exception):
    """Raised when an update manifest does not match actual release artifacts."""
    pass


def host_platform() -> str:
    if sys.platform == 'darwin':
        return 'mac'
    if sys.platform == 'win32':
        return 'win'
    return 'linux'


def read_version() -> str:
    with open(_VERSION_TXT) as f:
        return f.read().strip()


def version_to_code(version_str: str) -> int:
    """Derives a monotonic numeric version code from semver string for SQL ordering."""
    parts = []
    v = version_str.lstrip('v').strip()
    for part in v.split('.')[:3]:
        m = re.match(r'^(\d+)', part)
        if m:
            parts.append(int(m.group(1)))
        else:
            parts.append(0)
    while len(parts) < 3:
        parts.append(0)
    return parts[0] * 1_000_000 + parts[1] * 1_000 + parts[2]


def release_output_dir(out_dir: str) -> str:
    return os.path.join(_CHROMIUM_SRC, out_dir)


def run(cmd, dry_run, **kwargs):
    print('+ ' + ' '.join(cmd))
    if dry_run:
        return 0
    return subprocess.run(cmd, check=True, **kwargs).returncode


def require(path: str) -> str:
    if not os.path.exists(path):
        sys.exit(f'error: expected artifact missing: {path}')
    return path


def verify_macos_release_artifact(dmg: str, dry_run: bool) -> int:
    """Verify the exact DMG before generating an appcast or publishing it.

    This intentionally does not use run(): existing release-contract tests mock
    run() to inspect manifest/publish calls, while this is a separate mandatory
    distribution gate. In dry-run mode the command is only printed.
    """
    cmd = [
        sys.executable,
        _VERIFY_MACOS_RELEASE,
        '--dmg',
        dmg,
        '--expected-arch',
        'arm64',
    ]
    print('+ ' + ' '.join(cmd))
    if dry_run:
        return 0
    return subprocess.run(cmd, check=True, cwd=_WORKSPACE_ROOT).returncode


def generate_release_notes(version: str, output_path: str, dry_run: bool) -> int:
    """Generate standalone HTML release notes using generate_changelog_json.py."""
    cmd = [
        sys.executable,
        _GEN_CHANGELOG,
        '--mode',
        'notes',
        '--version',
        version,
        '--out',
        output_path,
    ]
    print('+ ' + ' '.join(cmd))
    if dry_run:
        return 0
    return subprocess.run(cmd, check=True, cwd=_WORKSPACE_ROOT).returncode


def _verify_appcast_manifest(
    appcast_path: str,
    dmg_path: str,
    pubkey_bytes: bytes = MANIFEST_PUBKEY_BYTES,
) -> None:
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        from cryptography.exceptions import InvalidSignature
    except ImportError:
        sys.exit('error: the cryptography package is required for manifest verification')

    with open(appcast_path, 'r', encoding='utf-8') as f:
        xml_content = f.read()

    length_val = None
    ed_sig_val = None

    try:
        root = ET.fromstring(xml_content)
        enclosures = root.findall('.//enclosure')
        if enclosures:
            enc = enclosures[0]
            length_val = enc.attrib.get('length')
            for k, v in enc.attrib.items():
                if k.endswith('edSignature'):
                    ed_sig_val = v
                    break
    except Exception:
        pass

    if length_val is None:
        m = re.search(r'length="(\d+)"', xml_content)
        if m:
            length_val = m.group(1)

    if ed_sig_val is None:
        m = re.search(r'(?:sparkle:)?edSignature="([^"]+)"', xml_content)
        if m:
            ed_sig_val = m.group(1)

    if length_val is None:
        raise ManifestVerificationError(f'{appcast_path}: enclosure length attribute missing')
    if ed_sig_val is None:
        raise ManifestVerificationError(f'{appcast_path}: sparkle:edSignature attribute missing')

    actual_dmg_size = os.path.getsize(dmg_path)
    enclosure_len = int(length_val)
    if enclosure_len != actual_dmg_size:
        raise ManifestVerificationError(
            f'appcast enclosure length mismatch: manifest says {enclosure_len}, '
            f'actual DMG byte size is {actual_dmg_size}'
        )

    with open(dmg_path, 'rb') as f:
        dmg_data = f.read()

    try:
        sig_bytes = base64.b64decode(ed_sig_val)
    except Exception as e:
        raise ManifestVerificationError(f'appcast edSignature is not valid base64: {e}')

    if len(sig_bytes) != 64:
        raise ManifestVerificationError(
            f'appcast edSignature must be 64 bytes (got {len(sig_bytes)})'
        )

    vk = Ed25519PublicKey.from_public_bytes(pubkey_bytes)
    try:
        vk.verify(sig_bytes, dmg_data)
    except InvalidSignature:
        raise ManifestVerificationError(
            f'appcast edSignature verification failed against public key {pubkey_bytes.hex()}'
        )
    print(
        f'  [verified] {os.path.basename(appcast_path)}: enclosure length={enclosure_len} '
        f'and edSignature valid against {os.path.basename(dmg_path)}'
    )


def _verify_win_manifest(
    win_manifest_path: str,
    exe_path: str,
    pubkey_bytes: bytes = MANIFEST_PUBKEY_BYTES,
) -> None:
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        from cryptography.exceptions import InvalidSignature
    except ImportError:
        sys.exit('error: the cryptography package is required for manifest verification')

    with open(win_manifest_path, 'r', encoding='utf-8') as f:
        manifest_raw = f.read()

    try:
        envelope = json.loads(manifest_raw)
    except Exception as e:
        raise ManifestVerificationError(f'{win_manifest_path} is not valid JSON: {e}')

    if not isinstance(envelope, dict):
        raise ManifestVerificationError(f'{win_manifest_path} envelope is not a JSON object')

    sig_hex = envelope.get('signature')
    payload_str = envelope.get('payload')

    if not sig_hex or not isinstance(sig_hex, str):
        raise ManifestVerificationError(f'{win_manifest_path} missing valid signature field')
    if payload_str is None or not isinstance(payload_str, str):
        raise ManifestVerificationError(f'{win_manifest_path} missing valid payload string')

    try:
        sig_bytes = bytes.fromhex(sig_hex)
    except Exception as e:
        raise ManifestVerificationError(f'{win_manifest_path} signature is not valid hex: {e}')

    if len(sig_bytes) != 64:
        raise ManifestVerificationError(
            f'{win_manifest_path} signature must be 64 bytes (got {len(sig_bytes)})'
        )

    vk = Ed25519PublicKey.from_public_bytes(pubkey_bytes)
    try:
        vk.verify(sig_bytes, payload_str.encode('utf-8'))
    except InvalidSignature:
        raise ManifestVerificationError(
            f'update-win.json envelope signature verification failed against public key {pubkey_bytes.hex()}'
        )

    try:
        payload = json.loads(payload_str)
    except Exception as e:
        raise ManifestVerificationError(f'{win_manifest_path} payload is not valid JSON: {e}')

    if not isinstance(payload, dict):
        raise ManifestVerificationError(f'{win_manifest_path} payload is not a JSON object')

    expected_sha256 = payload.get('installer_sha256')
    if not expected_sha256:
        raise ManifestVerificationError(f'{win_manifest_path} payload missing installer_sha256')

    h = hashlib.sha256()
    with open(exe_path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    actual_sha256 = h.hexdigest().lower()

    if expected_sha256.lower() != actual_sha256:
        raise ManifestVerificationError(
            f'update-win.json installer_sha256 mismatch: manifest says {expected_sha256}, '
            f'actual EXE sha256 is {actual_sha256}'
        )
    print(
        f'  [verified] {os.path.basename(win_manifest_path)}: installer_sha256={actual_sha256} '
        f'and envelope signature valid against {os.path.basename(exe_path)}'
    )


def verify_manifest_matches_artifacts(
    artifacts: Sequence[str],
    dry_run: bool = False,
    pubkey_bytes: bytes = MANIFEST_PUBKEY_BYTES,
    expected_version: Optional[str] = None,
) -> None:
    """Verify manifests against real artifact bytes before publishing.

    Checks:
      1. macOS appcast.xml:
         - Enclosure length == actual DMG byte size
         - Sparkle edSignature verifies against MANIFEST_PUBKEY_BYTES
      2. Windows update-win.json:
         - Envelope signature verifies against MANIFEST_PUBKEY_BYTES
         - Payload installer_sha256 == actual EXE sha256

    Exits nonzero on any mismatch.
    """
    print('+ verify manifest matches artifacts')
    if dry_run:
        return

    appcast_files = [a for a in artifacts if a.endswith('.xml') or 'appcast' in os.path.basename(a)]
    dmg_files = [a for a in artifacts if a.endswith('.dmg')]

    for appcast_path in appcast_files:
        if not os.path.exists(appcast_path):
            raise ManifestVerificationError(f'appcast manifest missing: {appcast_path}')
        matching_dmg = None
        if dmg_files:
            matching_dmg = dmg_files[0]
        else:
            cand = glob.glob(os.path.join(os.path.dirname(appcast_path), '*.dmg'))
            if cand:
                matching_dmg = cand[0]
        if not matching_dmg or not os.path.exists(matching_dmg):
            raise ManifestVerificationError(f'DMG artifact missing for {appcast_path}')
        _verify_appcast_manifest(appcast_path, matching_dmg, pubkey_bytes)

    win_manifest_files = [a for a in artifacts if os.path.basename(a) == 'update-win.json']
    exe_files = [a for a in artifacts if a.endswith('.exe') and 'MahoSetup' in os.path.basename(a)]
    if not exe_files:
        exe_files = [a for a in artifacts if a.endswith('.exe')]

    for win_manifest_path in win_manifest_files:
        if not os.path.exists(win_manifest_path):
            raise ManifestVerificationError(f'update-win manifest missing: {win_manifest_path}')
        matching_exe = None
        if exe_files:
            matching_exe = exe_files[0]
        else:
            cand = glob.glob(os.path.join(os.path.dirname(win_manifest_path), 'MahoSetup-*.exe'))
            if cand:
                matching_exe = cand[0]
        if not matching_exe or not os.path.exists(matching_exe):
            raise ManifestVerificationError(f'EXE artifact missing for {win_manifest_path}')
        _verify_win_manifest(win_manifest_path, matching_exe, pubkey_bytes)

    for linux_manifest_path in [a for a in artifacts if os.path.basename(a) == 'update-linux.json']:
        _verify_linux_manifest(linux_manifest_path, expected_version, pubkey_bytes)


def _verify_linux_manifest(
    linux_manifest_path: str,
    expected_version: Optional[str],
    pubkey_bytes: bytes = MANIFEST_PUBKEY_BYTES,
) -> None:
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        from cryptography.exceptions import InvalidSignature
    except ImportError:
        sys.exit('error: the cryptography package is required for manifest verification')

    if not os.path.exists(linux_manifest_path):
        raise ManifestVerificationError(f'update-linux manifest missing: {linux_manifest_path}')
    with open(linux_manifest_path, 'r', encoding='utf-8') as f:
        try:
            envelope = json.load(f)
        except Exception as e:
            raise ManifestVerificationError(f'{linux_manifest_path} is not valid JSON: {e}')
    if not isinstance(envelope, dict):
        raise ManifestVerificationError(f'{linux_manifest_path} envelope is not a JSON object')
    sig_hex = envelope.get('signature')
    payload_str = envelope.get('payload')
    if not isinstance(sig_hex, str) or not isinstance(payload_str, str):
        raise ManifestVerificationError(f'{linux_manifest_path} missing signature/payload')
    try:
        sig_bytes = bytes.fromhex(sig_hex)
    except ValueError as e:
        raise ManifestVerificationError(f'{linux_manifest_path} signature is not valid hex: {e}')
    try:
        Ed25519PublicKey.from_public_bytes(pubkey_bytes).verify(sig_bytes, payload_str.encode('utf-8'))
    except InvalidSignature:
        raise ManifestVerificationError(
            f'update-linux.json envelope signature verification failed against public key {pubkey_bytes.hex()}'
        )
    try:
        payload = json.loads(payload_str)
    except Exception as e:
        raise ManifestVerificationError(f'{linux_manifest_path} payload is not valid JSON: {e}')
    if not isinstance(payload, dict) or not isinstance(payload.get('version'), str):
        raise ManifestVerificationError(f'{linux_manifest_path} payload missing version')
    if expected_version is not None and payload['version'] != expected_version:
        raise ManifestVerificationError(
            f'update-linux.json version mismatch: manifest says {payload["version"]}, '
            f'release is {expected_version}'
        )
    print(f'  [verified] update-linux.json: version={payload["version"]} and envelope signature valid')


def build_and_bundle_mac(version, args):
    if not args.skip_build:
        # A public macOS artifact is never allowed to take the unsigned or
        # unstapled path. build_maho.py fails if notarization fails, and the
        # artifact verifier below independently checks Developer ID, stapling,
        # Gatekeeper, bundle metadata/permissions, dylib paths, and xattrs.
        run([
            sys.executable,
            _BUILD_MAHO,
            '--release',
            '--notarize',
            '--no-launch',
            '--out-dir',
            args.out_dir,
        ], args.dry_run)
    output_dir = release_output_dir(args.out_dir)
    dmg = os.path.join(output_dir, f'Maho-{version}.dmg')
    if not args.dry_run:
        require(dmg)
    verify_macos_release_artifact(dmg, args.dry_run)
    run([sys.executable, _GEN_MANIFESTS, '--version', version, '--dmg', dmg,
         '--output-dir', output_dir],
        args.dry_run, cwd=_WORKSPACE_ROOT)
    notes_html = os.path.join(output_dir, 'release-notes.html')
    generate_release_notes(version, notes_html, args.dry_run)
    if not args.dry_run:
        require(notes_html)
    return [dmg, os.path.join(output_dir, 'appcast.xml'), notes_html]


def build_and_bundle_win(version, args):
    if not args.skip_build:
        run([
            sys.executable,
            _BUILD_MAHO,
            '--release',
            '--platform',
            'win',
            '--no-launch',
            '--out-dir',
            args.out_dir,
        ], args.dry_run)
    output_dir = release_output_dir(args.out_dir)
    exe = os.path.join(output_dir, f'MahoSetup-{version}.exe')
    if not args.dry_run:
        require(exe)
        require(os.path.join(output_dir, 'maho.exe'))
        require(os.path.join(output_dir, 'maho_mail_helper.exe'))
    run([sys.executable, _GEN_MANIFESTS, '--version', version, '--win-exe', exe,
         '--output-dir', output_dir],
        args.dry_run, cwd=_WORKSPACE_ROOT)
    # Store package: Windows ships through the Microsoft Store, which signs the
    # MSIX at ingestion, so it is packed unsigned here.
    msix = os.path.join(output_dir, f'MahoBrowser_{version}.0_x64.msix')
    run([sys.executable, _PACKAGE_MSIX, '--input-dir', output_dir,
         '--output', msix, '--version', version],
        args.dry_run, cwd=_WORKSPACE_ROOT)
    if not args.dry_run:
        require(msix)
    return [exe, os.path.join(output_dir, 'update-win.json'), msix]


def build_and_bundle_linux(version, args):
    if not args.skip_build:
        run([
            sys.executable,
            _BUILD_LINUX,
            '--release',
            '--format',
            'all',
            '--out-dir',
            args.out_dir,
        ], args.dry_run)
    deb = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-deb', f'maho_{version}_amd64.deb')
    tarball = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-tarball', f'maho-{version}-x86_64.tar.gz')
    rpms = glob.glob(os.path.join(_CHROMIUM_SRC, 'out', 'Linux-rpm', 'rpmbuild', 'RPMS', '*', 'maho-*.rpm'))
    if not args.dry_run:
        require(deb)
        require(tarball)
        if not rpms:
            sys.exit('error: no rpm produced under out/Linux-rpm/rpmbuild/RPMS')
    artifacts = [tarball, deb] + rpms
    if not args.dry_run:
        for artifact in artifacts:
            require(artifact + '.sig')
    signed = artifacts + [artifact + '.sig' for artifact in artifacts]
    output_dir = release_output_dir(args.out_dir)
    run([sys.executable, _GEN_MANIFESTS, '--version', version, '--linux',
         '--output-dir', output_dir],
        args.dry_run, cwd=_WORKSPACE_ROOT)
    return signed + [os.path.join(output_dir, 'update-linux.json')]


def ensure_release(tag, repo, version, dry_run):
    exists = subprocess.run(['gh', 'release', 'view', tag, '--repo', repo],
                            capture_output=True, text=True).returncode == 0
    if exists:
        print(f'release {tag} already exists on {repo}; reusing')
        return
    run(['gh', 'release', 'create', tag, '--repo', repo,
         '--title', f'Maho {version}', '--draft',
         '--notes', f'Maho {version} release.'], dry_run)


def publish(tag, repo, artifacts, dry_run):
    artifacts = [a for a in artifacts if a]
    if not dry_run:
        artifacts = [a for a in artifacts if require(a)]
    run(['gh', 'release', 'upload', tag, '--repo', repo, '--clobber', *artifacts], dry_run)


def finalize_release(tag, repo, dry_run):
    if not dry_run:
        version = tag.removeprefix('v')
        result = subprocess.run(
            [
                'gh',
                'release',
                'view',
                tag,
                '--repo',
                repo,
                '--json',
                'assets',
                '--jq',
                '.assets[].name',
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        uploaded_assets = set(result.stdout.splitlines())
        required_assets = {
            asset.format(version=version) for asset in _FINAL_RELEASE_ASSETS
        }
        missing_assets = sorted(required_assets - uploaded_assets)
        if missing_assets:
            sys.exit(
                'error: cannot finalize release; missing required release assets: '
                + ', '.join(missing_assets)
            )
    run(['gh', 'release', 'edit', tag, '--repo', repo, '--draft=false', '--latest'], dry_run)


def register_release_to_relay(
    platform: str,
    channel: str,
    version: str,
    version_code: int,
    rollout_threshold: int,
    artifacts: Sequence[str],
    repo: str,
    relay_url: str = DEFAULT_RELAY_URL,
    dry_run: bool = False,
) -> None:
    """Register the uploaded release payload to the Maho update relay."""
    db_platform = {
        'mac': 'macos',
        'darwin': 'macos',
        'macos': 'macos',
        'win': 'windows',
        'win32': 'windows',
        'windows': 'windows',
        'linux': 'linux',
    }.get(platform, platform)

    appcast_xml: Optional[str] = None
    win_envelope: Optional[str] = None
    linux_envelope: Optional[str] = None

    for a in artifacts:
        if os.path.basename(a) == 'appcast.xml' and os.path.isfile(a):
            with open(a, 'r', encoding='utf-8') as f:
                appcast_xml = f.read()
        elif os.path.basename(a) == 'update-win.json' and os.path.isfile(a):
            with open(a, 'r', encoding='utf-8') as f:
                win_envelope = f.read()
        elif os.path.basename(a) == 'update-linux.json' and os.path.isfile(a):
            with open(a, 'r', encoding='utf-8') as f:
                linux_envelope = f.read()

    notes_url = f'https://github.com/{repo}/releases/download/v{version}/release-notes.html'
    pub_date = formatdate(usegmt=True)

    payload = {
        'platform': db_platform,
        'channel': channel,
        'version': version,
        'version_code': version_code,
        'rollout_threshold': rollout_threshold,
        'appcast_xml': appcast_xml,
        'win_envelope': win_envelope,
        'linux_envelope': linux_envelope,
        'notes_url': notes_url,
        'pub_date': pub_date,
    }

    if dry_run:
        print(f'+ POST {relay_url}')
        print('  Headers: Content-Type: application/json, X-Release-Admin-Token: [REDACTED]')
        print('  Payload:')
        print(json.dumps(payload, indent=2))
        return

    token = os.environ.get('RELEASE_ADMIN_TOKEN', '').strip()
    if not token:
        sys.exit(
            'error: RELEASE_ADMIN_TOKEN environment variable is required to register release with relay'
        )

    req_data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(
        relay_url,
        data=req_data,
        headers={
            'Content-Type': 'application/json',
            'X-Release-Admin-Token': token,
            # Cloudflare fronts the relay and blocks the default
            # `Python-urllib/3.x` agent with HTTP 403 (error 1010), so the
            # release registration must identify itself as a normal client.
            'User-Agent': 'Maho-Release/1.0 (+https://mahobrowser.com)',
        },
        method='POST',
    )
    try:
        with urllib.request.urlopen(req) as resp:
            body = resp.read().decode('utf-8', errors='replace')
            print(f'Relay registration succeeded ({resp.status}): {body}')
    except urllib.error.HTTPError as e:
        err_body = e.read().decode('utf-8', errors='replace')
        sys.exit(f'error: relay registration failed with HTTP {e.code}: {err_body}')
    except Exception as e:
        sys.exit(f'error: relay registration failed: {e}')


def run_self_test() -> int:
    """Run verification self-test against valid and tampered manifests/artifacts."""
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    print('== Running release_local manifest verification self-test ==')
    test_key = Ed25519PrivateKey.generate()
    test_pub_bytes = test_key.public_key().public_bytes_raw()

    with tempfile.TemporaryDirectory() as tmp_dir:
        # 1. Valid macOS fixture
        dmg_path = os.path.join(tmp_dir, 'Maho-1.0.0.dmg')
        dmg_bytes = b'sample-macos-dmg-content-for-selftest-1234567890'
        with open(dmg_path, 'wb') as f:
            f.write(dmg_bytes)

        sig_b64 = base64.b64encode(test_key.sign(dmg_bytes)).decode()
        appcast_path = os.path.join(tmp_dir, 'appcast.xml')
        appcast_xml = f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Maho Browser</title>
    <item>
      <title>Version 1.0.0</title>
      <pubDate>Mon, 31 Aug 2026 12:00:00 GMT</pubDate>
      <sparkle:releaseNotesLink>https://github.com/Project-Maho/release/releases/latest/download/release-notes.html</sparkle:releaseNotesLink>
      <enclosure url="https://github.com/Project-Maho/release/releases/latest/download/Maho-1.0.0.dmg"
                 sparkle:version="1.0.0"
                 sparkle:shortVersionString="1.0.0"
                 type="application/octet-stream"
                 length="{len(dmg_bytes)}"
                 sparkle:edSignature="{sig_b64}" />
    </item>
  </channel>
</rss>"""
        with open(appcast_path, 'w', encoding='utf-8') as f:
            f.write(appcast_xml)

        verify_manifest_matches_artifacts([dmg_path, appcast_path], pubkey_bytes=test_pub_bytes)
        print('  [PASS] Valid macOS appcast verification passed')

        # 2. Known v1.0.0 defect: enclosure length 65355735 vs real DMG 197337667
        tampered_appcast_path = os.path.join(tmp_dir, 'appcast-tampered-length.xml')
        tampered_length_xml = appcast_xml.replace(
            f'length="{len(dmg_bytes)}"',
            'length="65355735"'
        )
        with open(tampered_appcast_path, 'w', encoding='utf-8') as f:
            f.write(tampered_length_xml)

        caught_defect = False
        try:
            verify_manifest_matches_artifacts([dmg_path, tampered_appcast_path], pubkey_bytes=test_pub_bytes)
        except ManifestVerificationError as e:
            caught_defect = True
            assert 'enclosure length mismatch' in str(e), f'Unexpected error message: {e}'
            assert '65355735' in str(e), f'Expected defect length in message: {e}'
            print(f'  [PASS] Successfully caught known v1.0.0 enclosure length mismatch defect: {e}')

        if not caught_defect:
            sys.exit('error: self-test failed to catch tampered appcast enclosure length')

        # 3. Tampered edSignature on macOS appcast
        tampered_sig_path = os.path.join(tmp_dir, 'appcast-tampered-sig.xml')
        bad_sig_b64 = base64.b64encode(b'A' * 64).decode()
        tampered_sig_xml = appcast_xml.replace(sig_b64, bad_sig_b64)
        with open(tampered_sig_path, 'w', encoding='utf-8') as f:
            f.write(tampered_sig_xml)

        caught_bad_sig = False
        try:
            verify_manifest_matches_artifacts([dmg_path, tampered_sig_path], pubkey_bytes=test_pub_bytes)
        except ManifestVerificationError as e:
            caught_bad_sig = True
            assert 'edSignature verification failed' in str(e), f'Unexpected error message: {e}'
            print(f'  [PASS] Successfully caught invalid macOS edSignature: {e}')

        if not caught_bad_sig:
            sys.exit('error: self-test failed to catch tampered appcast edSignature')

        # 4. Valid Windows fixture
        exe_path = os.path.join(tmp_dir, 'MahoSetup-1.0.0.exe')
        exe_bytes = b'sample-windows-setup-executable-bytes-9876543210'
        with open(exe_path, 'wb') as f:
            f.write(exe_bytes)

        exe_sha256 = hashlib.sha256(exe_bytes).hexdigest()
        win_payload_obj = {
            'version': '1.0.0',
            'installer_url': 'https://github.com/Project-Maho/release/releases/latest/download/MahoSetup-1.0.0.exe',
            'installer_sha256': exe_sha256,
            'installer_signer_cn': 'Maho Browser',
            'rollout_bucket': 100,
        }
        win_payload_str = json.dumps(win_payload_obj, separators=(',', ':'))
        win_sig_hex = test_key.sign(win_payload_str.encode('utf-8')).hex().lower()
        win_manifest_path = os.path.join(tmp_dir, 'update-win.json')
        win_envelope = {
            'signature': win_sig_hex,
            'payload': win_payload_str,
        }
        with open(win_manifest_path, 'w', encoding='utf-8') as f:
            json.dump(win_envelope, f, indent=2)

        verify_manifest_matches_artifacts([exe_path, win_manifest_path], pubkey_bytes=test_pub_bytes)
        print('  [PASS] Valid Windows manifest verification passed')

        # 5. Tampered Windows installer_sha256
        bad_sha_payload_obj = dict(win_payload_obj)
        bad_sha_payload_obj['installer_sha256'] = '0' * 64
        bad_sha_payload_str = json.dumps(bad_sha_payload_obj, separators=(',', ':'))
        bad_sha_sig = test_key.sign(bad_sha_payload_str.encode('utf-8')).hex().lower()
        tampered_win_path = os.path.join(tmp_dir, 'update-win-bad-sha.json')
        with open(tampered_win_path, 'w', encoding='utf-8') as f:
            json.dump({'signature': bad_sha_sig, 'payload': bad_sha_payload_str}, f, indent=2)

        caught_bad_sha = False
        try:
            verify_manifest_matches_artifacts([exe_path, tampered_win_path], pubkey_bytes=test_pub_bytes)
        except ManifestVerificationError as e:
            caught_bad_sha = True
            assert 'installer_sha256 mismatch' in str(e), f'Unexpected error message: {e}'
            print(f'  [PASS] Successfully caught Windows installer_sha256 mismatch: {e}')

        if not caught_bad_sha:
            sys.exit('error: self-test failed to catch tampered Windows installer_sha256')

        # 6. Tampered Windows envelope signature
        tampered_env_sig_path = os.path.join(tmp_dir, 'update-win-bad-sig.json')
        with open(tampered_env_sig_path, 'w', encoding='utf-8') as f:
            json.dump({'signature': 'ff' * 64, 'payload': win_payload_str}, f, indent=2)

        caught_bad_env_sig = False
        try:
            verify_manifest_matches_artifacts([exe_path, tampered_env_sig_path], pubkey_bytes=test_pub_bytes)
        except ManifestVerificationError as e:
            caught_bad_env_sig = True
            assert 'signature verification failed' in str(e), f'Unexpected error message: {e}'
            print(f'  [PASS] Successfully caught invalid Windows envelope signature: {e}')

        if not caught_bad_env_sig:
            sys.exit('error: self-test failed to catch tampered Windows envelope signature')

    # 7. Production public key check
    assert MANIFEST_PUBKEY_HEX == '8b076b75cb7896810997c59b1ec7e184730d714ca0679e0deae22f7aba0d4dbc', (
        f'Mismatch in MANIFEST_PUBKEY_HEX: {MANIFEST_PUBKEY_HEX}'
    )
    assert len(MANIFEST_PUBKEY_BYTES) == 32, f'Expected 32 bytes for MANIFEST_PUBKEY_BYTES, got {len(MANIFEST_PUBKEY_BYTES)}'

    print('Self-test passed: all manifest validations and defect catches verified.')
    return 0


def main():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument('--repo', default=DEFAULT_REPO, help='release-only repo (owner/name).')
    p.add_argument(
        '--out-dir',
        default='out/Default',
        help='Chromium output directory relative to chromium/src.',
    )
    p.add_argument(
        '--notarize',
        action='store_true',
        help='Deprecated compatibility flag; macOS releases are always notarized.',
    )
    p.add_argument(
        '--skip-build',
        action='store_true',
        help='Reuse an already-built bundle; only verify + manifest + publish.',
    )
    p.add_argument(
        '--no-publish',
        action='store_true',
        help='Build + bundle + manifest, but do not touch GitHub or update relay.',
    )
    p.add_argument(
        '--finalize',
        action='store_true',
        help='Make the draft release public/latest after all platform assets upload.',
    )
    p.add_argument(
        '--dry-run',
        action='store_true',
        help='Print every command and network payload without executing or sending.',
    )
    p.add_argument(
        '--channel',
        default='stable',
        help='Release channel for update relay (default: stable).',
    )
    p.add_argument(
        '--rollout-threshold',
        type=int,
        default=10,
        help='Relay rollout threshold percentage (0-100, default: 10).',
    )
    p.add_argument(
        '--version-code',
        type=int,
        default=None,
        help='Explicit version code override for relay ordering.',
    )
    p.add_argument(
        '--relay-url',
        default=DEFAULT_RELAY_URL,
        help='Relay endpoint for release registration.',
    )
    p.add_argument(
        '--self-test',
        action='store_true',
        help='Run manifest verification and tampered fixture self-test suite and exit.',
    )
    args = p.parse_args()

    if args.self_test:
        return run_self_test()

    platform = host_platform()
    version = read_version()
    tag = f'v{version}'
    print(
        f'== Maho local release: platform={platform} version={version} '
        f'tag={tag} repo={args.repo} =='
    )

    builder = {
        'mac': build_and_bundle_mac,
        'win': build_and_bundle_win,
        'linux': build_and_bundle_linux,
    }[platform]
    artifacts = builder(version, args)

    print('Artifacts:')
    for a in artifacts:
        print(f'  {a}')

    try:
        verify_manifest_matches_artifacts(artifacts, dry_run=args.dry_run, expected_version=version)
    except ManifestVerificationError as e:
        sys.exit(f'error: manifest verification failed: {e}')

    if args.no_publish:
        print('--no-publish set; skipping GitHub release and relay registration.')
        return 0

    ensure_release(tag, args.repo, version, args.dry_run)
    publish(tag, args.repo, artifacts, args.dry_run)
    if args.finalize:
        finalize_release(tag, args.repo, args.dry_run)

    v_code = args.version_code if args.version_code is not None else version_to_code(version)
    register_release_to_relay(
        platform=platform,
        channel=args.channel,
        version=version,
        version_code=v_code,
        rollout_threshold=args.rollout_threshold,
        artifacts=artifacts,
        repo=args.repo,
        relay_url=args.relay_url,
        dry_run=args.dry_run,
    )
    print(f'Published {platform} artifacts to {args.repo} {tag}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
