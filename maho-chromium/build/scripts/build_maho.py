#!/usr/bin/env python3
"""Build the Rust prebuilt and Chromium app under one shared lock."""

from __future__ import annotations

import argparse
import glob
import importlib.util
import os
import plistlib
from datetime import datetime, timezone
import re
import shutil
import subprocess
import sys
import tempfile
import time
from subprocess import SubprocessError

# Bootstrap developer toolpaths onto PATH up front so all child processes
# (ninja, siso, python scripts, node scripts, subshells) find bun, sccache, cargo, etc.
_extra_tool_dirs = [
    os.path.expanduser('~/.bun/bin'),
    os.path.expanduser('~/.local/bin'),
    os.path.expanduser('~/.cargo/bin'),
    os.path.expanduser('~/depot_tools'),
    '/opt/homebrew/bin',
    '/opt/homebrew/sbin',
    '/usr/local/bin',
]
_current_paths = os.environ.get('PATH', '').split(os.pathsep)
for _d in _extra_tool_dirs:
    if os.path.isdir(_d) and _d not in _current_paths:
        _current_paths.insert(0, _d)
os.environ['PATH'] = os.pathsep.join(_current_paths)

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_MAHO_CHROMIUM_DIR = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..'))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
if os.path.basename(_WORKSPACE_ROOT) == 'src' and os.path.basename(os.path.dirname(_WORKSPACE_ROOT)) == 'chromium':
    _WORKSPACE_ROOT = os.path.normpath(os.path.join(_WORKSPACE_ROOT, '..', '..'))
_MAHO_SCRIPTS_DIR = os.path.join(_WORKSPACE_ROOT, 'maho', 'scripts')


def chromium_src_root(workspace_root):
    return os.path.realpath(os.path.join(workspace_root, 'chromium', 'src'))


_CHROMIUM_SRC_ROOT = chromium_src_root(_WORKSPACE_ROOT)
_DEPOT_TOOLS_DIR = os.path.join(_CHROMIUM_SRC_ROOT, 'third_party', 'depot_tools')
_APPLY_OVERRIDES_SCRIPT = os.path.join(_SCRIPT_DIR, 'apply_chromium_src_overrides.py')
_BUILD_CONFIG_DIR = os.path.join(_MAHO_CHROMIUM_DIR, 'build', 'config')
_MAHO_APP_PATH = os.path.join(_CHROMIUM_SRC_ROOT, 'out', 'Default', 'Maho.app')
_PKG_DMG = os.path.join(_CHROMIUM_SRC_ROOT, 'chrome', 'installer', 'mac', 'pkg-dmg')
_VERSION_TXT = os.path.join(_WORKSPACE_ROOT, 'maho', 'version.txt')
_DMG_BACKGROUND = os.path.join(_MAHO_CHROMIUM_DIR, 'branding', 'mac', 'dmg-background.png')


def host_platform() -> str:
    """Return the canonical host platform string: 'mac', 'win', or 'linux'."""
    if sys.platform == 'darwin':
        return 'mac'
    if sys.platform == 'win32':
        return 'win'
    return 'linux'


def load_module(module_name, module_path):
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f'Unable to load module: {module_path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_build_lock_mod = load_module('maho_build_lock', os.path.join(_MAHO_SCRIPTS_DIR, 'build_lock.py'))
build_lock = _build_lock_mod.build_lock
classify_targets = _build_lock_mod.classify_targets
build_maho_core = load_module(
    'maho_build_maho_core', os.path.join(_SCRIPT_DIR, 'build_maho_core.py')
)


def normalize_autoninja_args(extra_args):
    if extra_args and extra_args[0] == '--':
        return extra_args[1:]
    return extra_args


def ensure_overlay_mount(chromium_src, skip):
    """Ensure chromium/src/maho exists and points to the maho-chromium overlay.

    On POSIX creates a relative symlink.  On Windows creates a directory
    junction via cmd /c mklink /J.  If the path already exists and points to
    the correct target, this is a no-op.  If it exists but points somewhere
    else, raises SystemExit with a clear error — the caller must not delete or
    overwrite an unexpected path automatically.
    """
    if skip:
        return

    mount_path = os.path.join(chromium_src, 'maho')

    if os.path.islink(mount_path):
        current_target = os.readlink(mount_path)
        resolved = os.path.realpath(mount_path)
        expected = os.path.realpath(_MAHO_CHROMIUM_DIR)
        if resolved == expected:
            print(f'Overlay mount already exists: {mount_path} -> {current_target}')
            return
        print(
            f'error: {mount_path} already exists but points to {current_target!r} '
            f'(resolved: {resolved}); expected {expected}',
            file=sys.stderr,
        )
        sys.exit(1)

    if os.path.exists(mount_path) or os.path.isdir(mount_path):
        resolved = os.path.realpath(mount_path)
        expected = os.path.realpath(_MAHO_CHROMIUM_DIR)
        if resolved == expected:
            print(f'Overlay mount already exists: {mount_path}')
            return
        print(
            f'error: {mount_path} already exists (not a symlink) and does not resolve '
            f'to {expected}; refusing to modify it',
            file=sys.stderr,
        )
        sys.exit(1)

    if sys.platform == 'win32':
        target_abs = os.path.realpath(_MAHO_CHROMIUM_DIR)
        print(f'Creating junction: {mount_path} -> {target_abs}')
        subprocess.run(
            ['cmd', '/c', 'mklink', '/J', mount_path, target_abs],
            check=True,
        )
    else:
        real_chromium_src = os.path.realpath(chromium_src)
        real_maho_chromium = os.path.realpath(_MAHO_CHROMIUM_DIR)
        rel_target = os.path.relpath(real_maho_chromium, real_chromium_src)
        print(f'Creating symlink: {mount_path} -> {rel_target}')
        os.symlink(rel_target, mount_path)


def embed_sparkle_framework(app_path: str) -> bool:
    """Copy the vendored Sparkle.framework into the app's Frameworks dir.

    The GN build links against Sparkle.framework but does not stage it into the
    bundle; the Sparkle-backed updater delegate resolves it from
    Contents/Frameworks/ at runtime. Copies with symlinks preserved so the
    framework's Versions/Current structure and code signatures stay valid.
    """
    src = os.path.join(_MAHO_CHROMIUM_DIR, 'third_party', 'sparkle',
                       'Sparkle.framework')
    if not os.path.isdir(src):
        print(f"warning: vendored Sparkle framework not found: {src}; "
              "skipping embed", file=sys.stderr)
        return False
    frameworks_dir = os.path.join(app_path, 'Contents', 'Frameworks')
    os.makedirs(frameworks_dir, exist_ok=True)
    dest = os.path.join(frameworks_dir, 'Sparkle.framework')
    if os.path.islink(dest) or os.path.isfile(dest):
        os.remove(dest)
    elif os.path.isdir(dest):
        shutil.rmtree(dest)
    print(f"Embedding Sparkle framework into {frameworks_dir}")
    shutil.copytree(src, dest, symlinks=True)
    return True


def embed_mail_helper(app_path: str, out_dir: str) -> bool:
    """Copy the maho_mail_helper executable into the app's MacOS dir.

    The GN build produces maho_mail_helper as a standalone executable but does
    not stage it into the bundle; MahoMailHelperLauncher resolves it from
    <DIR_EXE>/maho_mail_helper (i.e. Contents/MacOS/) at runtime. Copies with
    shutil.copy2 so the exec bit and the linker-signed adhoc signature are
    preserved. Does NOT fabricate the binary — a missing source is a hard error.
    """
    src = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'maho_mail_helper')
    if not os.path.isfile(src):
        print(f"error: maho_mail_helper executable not found: {src}; "
              "cannot embed into app bundle", file=sys.stderr)
        return False
    macos_dir = os.path.join(app_path, 'Contents', 'MacOS')
    os.makedirs(macos_dir, exist_ok=True)
    dest = os.path.join(macos_dir, 'maho_mail_helper')
    if os.path.islink(dest) or os.path.isfile(dest):
        os.remove(dest)
    print(f"Embedding maho_mail_helper into {macos_dir}")
    shutil.copy2(src, dest)
    return True


def omo_runtime_source():
    """Directory holding the omo package to stage, or None when unavailable.

    Override with MAHO_OMO_PACKAGE_DIR. The directory must contain
    dist/rpc-entry.js; anything else is rejected rather than silently staged.
    """
    override = os.environ.get('MAHO_OMO_PACKAGE_DIR')
    candidates = [override] if override else [
        os.path.expanduser(
            '~/.bun/install/global/node_modules/@code-yeongyu/senpi'),
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(
                os.path.join(candidate, 'dist', 'rpc-entry.js')):
            return candidate
    return None


# Must match rpc_argv() and READY_MARKER in
# maho/crates/maho-agent/src/omo/process.rs: the gate launches the package
# exactly the way OmoBackend will at runtime.
_OMO_READY_MARKER = 'rpc listening on'
_OMO_PROBE_TIMEOUT_SECONDS = 30


def verify_omo_runtime(bun: str, rpc_entry: str) -> str | None:
    """Launch the omo runtime with Maho's RPC argv; return None when ready.

    Returns the runtime's stderr tail (or a timeout note) when it exits or
    stalls before reporting readiness. A package that only has
    dist/rpc-entry.js is not enough: senpi 2026.8.x rejects `--listen` and
    exits 1, which shipped as "omo runtime exited before it became ready".
    """
    import selectors
    probe_dir = tempfile.mkdtemp(prefix='omo-probe-', dir='/tmp')
    socket_path = os.path.join(probe_dir, 'rpc.sock')
    proc = subprocess.Popen(
        [bun, rpc_entry, '--listen', f'unix://{socket_path}',
         '--no-builtin-tools'],
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    seen: list[str] = []
    deadline = time.monotonic() + _OMO_PROBE_TIMEOUT_SECONDS
    try:
        with selectors.DefaultSelector() as sel:
            sel.register(proc.stderr, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return (f'no readiness within {_OMO_PROBE_TIMEOUT_SECONDS}s; '
                            + ' | '.join(seen[-5:]))
                if not sel.select(remaining):
                    continue
                line = proc.stderr.readline()
                if not line:
                    proc.wait(timeout=5)
                    return (f'exited with status {proc.returncode}: '
                            + ' | '.join(seen[-5:]))
                if _OMO_READY_MARKER in line:
                    return None
                seen.append(line.strip())
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        proc.stdin.close()
        proc.stderr.close()
        shutil.rmtree(probe_dir, ignore_errors=True)


def embed_omo_runtime(app_path: str, out_dir: str) -> bool:
    """Stage the omo runtime, Bun, and the browser MCP bridge into the bundle.

    OmoBackend resolves all three from <DIR_EXE> (Contents/MacOS), the same
    convention MahoMailHelperLauncher uses, so a shipped browser never falls back
    to a developer's own omo or Bun install.

    A dangling symlink inside the staged tree fails the later
    codesign --verify --deep --strict gate (the verifier resolves the link
    target and dies with ENOENT), so dangling links are pruned after the
    copy; a dangling link is a no-op at runtime, so pruning changes no
    behavior.

    Returns False when a piece is missing so the caller can decide; the agent
    panel simply stays on its existing backend in that case.
    """
    macos_dir = os.path.join(app_path, 'Contents', 'MacOS')
    os.makedirs(macos_dir, exist_ok=True)

    bun = shutil.which('bun')
    if not bun:
        print('warning: bun not found on PATH; skipping omo runtime staging',
              file=sys.stderr)
        return False

    package = omo_runtime_source()
    if not package:
        print('warning: omo package not found (set MAHO_OMO_PACKAGE_DIR); '
              'skipping omo runtime staging', file=sys.stderr)
        return False

    # build_maho_cli() builds the bridge with an explicit --target, which puts
    # it under target/<triple>/release; plain cargo builds land in
    # target/{release,debug}. Stage the newest one.
    target_root = os.path.join(_WORKSPACE_ROOT, 'maho', 'target')
    candidates = [
        path for path in (
            glob.glob(os.path.join(target_root, '*', 'release', 'maho-browser-mcp'))
            + [os.path.join(target_root, 'release', 'maho-browser-mcp'),
               os.path.join(target_root, 'debug', 'maho-browser-mcp')])
        if os.path.isfile(path)
    ]
    if not candidates:
        print('warning: maho-browser-mcp not built; skipping omo runtime '
              'staging', file=sys.stderr)
        return False
    mcp = max(candidates, key=os.path.getmtime)

    failure = verify_omo_runtime(
        bun, os.path.join(package, 'dist', 'rpc-entry.js'))
    if failure:
        raise RuntimeError(
            f'omo package at {package} cannot start with Maho\'s RPC argv '
            f'({failure}); update it (bun add -g @code-yeongyu/senpi) or '
            'point MAHO_OMO_PACKAGE_DIR at a compatible package')

    bun_dest = os.path.join(macos_dir, 'bun')
    if os.path.islink(bun_dest) or os.path.isfile(bun_dest):
        os.remove(bun_dest)
    print(f'Embedding bun into {macos_dir}')
    shutil.copy2(bun, bun_dest)

    mcp_dest = os.path.join(macos_dir, 'maho-browser-mcp')
    if os.path.islink(mcp_dest) or os.path.isfile(mcp_dest):
        os.remove(mcp_dest)
    print(f'Embedding maho-browser-mcp into {macos_dir}')
    shutil.copy2(mcp, mcp_dest)

    # Also stage into Contents/Helpers where IsTrustedMahoPeer and
    # IsTrustedMahoExecutablePath expect it.
    helpers_dir = os.path.join(app_path, 'Contents', 'Helpers')
    os.makedirs(helpers_dir, exist_ok=True)
    helpers_mcp = os.path.join(helpers_dir, 'maho-browser-mcp')
    if os.path.islink(helpers_mcp) or os.path.isfile(helpers_mcp):
        os.remove(helpers_mcp)
    print(f'Embedding maho-browser-mcp into {helpers_dir}')
    shutil.copy2(mcp, helpers_mcp)

    # omo runtime files (JS/JSON/etc.) belong in Contents/Resources/omo so that
    # Apple's codesign seals them as resources rather than rejecting uncompiled
    # .d.ts/.js files in Contents/MacOS.
    resources_dir = os.path.join(app_path, 'Contents', 'Resources')
    os.makedirs(resources_dir, exist_ok=True)
    omo_dest = os.path.join(resources_dir, 'omo')
    if os.path.isdir(omo_dest):
        shutil.rmtree(omo_dest)
    print(f'Embedding omo runtime into {omo_dest}')
    shutil.copytree(package, omo_dest, symlinks=True)
    pruned = 0
    for root, dirs, files in os.walk(omo_dest, topdown=True, followlinks=False):
        for name in dirs + files:
            path = os.path.join(root, name)
            if not os.path.islink(path):
                continue
            target = os.readlink(path)
            if os.path.isabs(target):
                resolved = target
            else:
                resolved = os.path.normpath(os.path.join(root, target))
            if not os.path.lexists(resolved):
                os.remove(path)
                pruned += 1
                print(f'warning: pruned dangling symlink {path} -> {target}',
                      file=sys.stderr)
    if pruned:
        print(f'pruned {pruned} dangling symlink(s) from {omo_dest}')
    return True


def require_windows_mail_helper(out_dir: str) -> str:
    """Return the packaged-layout helper path or fail closed when absent."""
    helper = os.path.join(
        _CHROMIUM_SRC_ROOT, out_dir, 'maho_mail_helper.exe')
    if not os.path.isfile(helper):
        raise RuntimeError(
            f'maho_mail_helper.exe is missing from the Windows output: {helper}')
    return helper


def maho_cli_binary_name(rust_target: str) -> str:
    return 'maho.exe' if 'windows' in rust_target else 'maho'


def build_maho_cli(target: str | None) -> str:
    rust_target, _, _ = build_maho_core.get_target_triple(target)
    print(f"Building maho CLI for {rust_target}...")
    subprocess.run(
        [
            'cargo',
            'build',
            '--release',
            '--target',
            rust_target,
            '--package',
            'maho-cli',
            '--bin',
            'maho',
            # The omo runtime's browser MCP bridge ships in the same bundle.
            '--package',
            'maho-browser-mcp',
            '--bin',
            'maho-browser-mcp',
        ],
        cwd=os.path.join(_WORKSPACE_ROOT, 'maho'),
        check=True,
    )
    cli_path = os.path.join(
        _WORKSPACE_ROOT,
        'maho',
        'target',
        rust_target,
        'release',
        maho_cli_binary_name(rust_target),
    )
    if not os.path.isfile(cli_path):
        raise RuntimeError(f'maho CLI build artifact not found: {cli_path}')
    return cli_path


def embed_maho_cli(app_path: str, cli_path: str) -> bool:
    if not os.path.isfile(cli_path):
        print(
            f"error: maho CLI executable not found: {cli_path}; "
            "cannot embed into app bundle",
            file=sys.stderr,
        )
        return False
    helpers_dir = os.path.join(app_path, 'Contents', 'Helpers')
    os.makedirs(helpers_dir, exist_ok=True)
    destination = os.path.join(helpers_dir, 'maho')
    if os.path.islink(destination) or os.path.isfile(destination):
        os.remove(destination)
    print(f"Embedding maho CLI into {helpers_dir}")
    shutil.copy2(cli_path, destination)
    return True


def stage_maho_cli_next_to_browser(out_dir: str, cli_path: str) -> str | None:
    if not os.path.isfile(cli_path):
        print(
            f"error: maho CLI executable not found: {cli_path}; "
            "cannot stage next to browser",
            file=sys.stderr,
        )
        return None
    out_root = os.path.join(_CHROMIUM_SRC_ROOT, out_dir)
    os.makedirs(out_root, exist_ok=True)
    destination = os.path.join(out_root, os.path.basename(cli_path))
    if os.path.islink(destination) or os.path.isfile(destination):
        os.remove(destination)
    print(f"Staging maho CLI at {destination}")
    shutil.copy2(cli_path, destination)
    return destination


def set_bundle_product_version(app_path: str, version: str) -> bool:
    info_path = os.path.join(app_path, 'Contents', 'Info.plist')
    if not os.path.isfile(info_path):
        return False
    with open(info_path, 'rb') as fp:
        plist = plistlib.load(fp)
    plist['CFBundleShortVersionString'] = version
    plist['CFBundleVersion'] = version
    with open(info_path, 'wb') as fp:
        plistlib.dump(plist, fp)
    return True


def find_codesign_identity(release: bool = False) -> str | None:
    """Pick a codesigning identity deterministically.

    TCC/Keychain grants are keyed by the designated requirement, which embeds
    the signing identity — so flipping identities between builds resets every
    permission grant and re-triggers "Maho wants to access..." prompts. The
    search order below is fixed so consecutive builds sign with the same
    identity. (Previously the first match from `security find-identity` was
    used, whose order is not guaranteed to be stable.)
    """
    try:
        res = subprocess.run(
            ['security', 'find-identity', '-v', '-p', 'codesigning'],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if res.returncode != 0:
            return None
        sha_re = re.compile(r'^\s*\d+\)\s+([a-fA-F0-9]{40})\s+"([^"]+)"')
        by_name = {}
        for line in res.stdout.splitlines():
            m = sha_re.match(line)
            if m:
                by_name.setdefault(m.group(2), m.group(1).upper())
        if release:
            preferred = ('Developer ID Application:', 'Apple Distribution:')
        else:
            preferred = ('Apple Development:', 'Developer ID Application:')
        for prefix in preferred:
            for name, identity in sorted(by_name.items()):
                if name.startswith(prefix):
                    return identity
        return next(iter(by_name.values()), None)
    except Exception:
        return None


def find_codesign_identity_release_dist() -> str | None:
    """Return the Apple Distribution identity (stable team-based DR)."""
    try:
        res = subprocess.run(
            ['security', 'find-identity', '-v', '-p', 'codesigning'],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if res.returncode != 0:
            return None
        sha_re = re.compile(r'^\s*\d+\)\s+([a-fA-F0-9]{40})\s+"([^"]+)"')
        for line in res.stdout.splitlines():
            m = sha_re.match(line)
            if m and m.group(2).startswith('Apple Distribution:'):
                return m.group(1).upper()
        return None
    except Exception:
        return None


def find_appstore_provisioning_profile(bundle_id: str) -> str | None:
    """Locate an installed, unexpired App Store profile for `bundle_id`.

    App Store distribution profiles pair with the Apple Distribution
    certificate, which carries a stable team-based designated requirement —
    exactly what macOS TCC needs to keep granted permissions across rebuilds.
    """
    profiles_dir = os.path.expanduser(
        '~/Library/MobileDevice/Provisioning Profiles')
    if not os.path.isdir(profiles_dir):
        return None
    now = datetime.now(timezone.utc)
    for name in sorted(os.listdir(profiles_dir)):
        if not name.endswith('.mobileprovision'):
            continue
        path = os.path.join(profiles_dir, name)
        try:
            res = subprocess.run(
                ['security', 'cms', '-D', '-i', path],
                capture_output=True, timeout=15)
            if res.returncode != 0:
                continue
            plist = plistlib.loads(res.stdout)
        except Exception:
            continue
        app_id = (plist.get('Entitlements') or {}).get(
            'application-identifier', '')
        if not app_id.endswith('.' + bundle_id):
            continue
        expiration = plist.get('ExpirationDate')
        if expiration is not None and expiration.replace(
                tzinfo=timezone.utc) <= now:
            continue
        # Ensure profile platform is compatible with macOS. iOS/visionOS/xrOS
        # profiles for com.maho.browser must not be embedded in the macOS app.
        platforms = plist.get('Platform') or []
        if platforms and 'OSX' not in platforms and 'macOS' not in platforms:
            continue
        # Skip development profiles: they are pinned to other certificates and
        # would not satisfy a distribution-signed build anyway.
        certs = plist.get('DeveloperCertificates') or []
        if not certs:
            continue
        return path
    return None


# Entitlements that only take effect when a matching provisioning profile is
# embedded. Signing an app with these but no profile makes AMFI refuse to launch
# it (AppleMobileFileIntegrityError -413 "No matching profile found"), so the
# unrestricted subset must be used whenever no profile ships with the bundle.
_RESTRICTED_ENTITLEMENT_KEYS = (
    'com.apple.application-identifier',
    'com.apple.developer.team-identifier',
    'keychain-access-groups',
)


def unrestricted_entitlements_path(source_path: str) -> str:
    """Write a copy of `source_path` without profile-gated entitlements.

    Returns the original path when nothing needed stripping.
    """
    try:
        with open(source_path, 'rb') as handle:
            data = plistlib.load(handle)
    except (OSError, plistlib.InvalidFileException):
        return source_path
    stripped = {k: v for k, v in data.items()
                if k not in _RESTRICTED_ENTITLEMENT_KEYS}
    if len(stripped) == len(data):
        return source_path
    handle, temp_path = tempfile.mkstemp(
        prefix='maho-entitlements-', suffix='.plist')
    with os.fdopen(handle, 'wb') as out:
        plistlib.dump(stripped, out)
    return temp_path


def codesign_app(identity: str, app_path: str) -> bool:
    if not os.path.isdir(app_path):
        print(f"error: app bundle not found: {app_path}", file=sys.stderr)
        return False
    try:
        provisioning_profile = os.environ.get('MAHO_MAC_PROVISIONING_PROFILE')
        embedded_profile = os.path.join(
            app_path, 'Contents', 'embedded.provisionprofile'
        )
        if provisioning_profile:
            if not os.path.isfile(provisioning_profile):
                print(
                    'codesign failed: MAHO_MAC_PROVISIONING_PROFILE does not '
                    f'exist: {provisioning_profile}',
                    file=sys.stderr,
                )
                return False
            print(f"Embedding macOS provisioning profile: {provisioning_profile}")
            shutil.copy2(provisioning_profile, embedded_profile)
        elif os.path.isfile(embedded_profile):
            try:
                os.remove(embedded_profile)
            except OSError:
                pass

        frameworks_dir = os.path.join(app_path, 'Contents', 'Frameworks')
        entitlements_path = os.path.join(
            _MAHO_CHROMIUM_DIR, 'branding', 'mac', 'entitlements.plist'
        )
        helper_entitlements_path = os.path.join(
            _MAHO_CHROMIUM_DIR,
            'branding',
            'mac',
            'helper-entitlements.plist',
        )
        # Restricted entitlements need an embedded, matching profile. Without
        # one (Developer ID signing without a Developer ID profile) AMFI kills
        # the process at launch, so sign the unrestricted subset instead of
        # shipping a bundle that cannot start.
        if provisioning_profile:
            app_entitlements_path = entitlements_path
        else:
            app_entitlements_path = unrestricted_entitlements_path(
                entitlements_path)
            if app_entitlements_path != entitlements_path:
                print(
                    'No provisioning profile embedded: signing without '
                    'restricted entitlements (application-identifier, '
                    'keychain-access-groups).'
                )
        maho_framework_path = os.path.join(
            frameworks_dir, 'Maho Framework.framework'
        )
        if os.path.isdir(maho_framework_path):
            helpers_dir = os.path.join(
                maho_framework_path, 'Versions', 'Current', 'Helpers'
            )
            for helper_name in (
                'Maho Helper.app',
                'Maho Helper (Alerts).app',
                'Maho Helper (GPU).app',
                'Maho Helper (Renderer).app',
            ):
                helper_path = os.path.join(helpers_dir, helper_name)
                if os.path.isdir(helper_path):
                    subprocess.run(
                        [
                            'codesign',
                            '--force',
                            '--sign',
                            identity,
                            '--options',
                            'runtime',
                            '--entitlements',
                            helper_entitlements_path,
                            helper_path,
                        ],
                        check=True,
                        timeout=120,
                    )

            # Chromium also ships bare Mach-O tools + ANGLE/SwiftShader dylibs
            # inside the framework. codesign does NOT recurse into these, so sign
            # each explicitly (Developer ID + hardened runtime + secure
            # timestamp) BEFORE sealing the framework, or notarization rejects
            # them ("not signed with a valid Developer ID" / "no secure
            # timestamp" / "hardened runtime not enabled").
            libraries_dir = os.path.join(
                maho_framework_path, 'Versions', 'Current', 'Libraries'
            )
            if os.path.isdir(libraries_dir):
                for lib_name in sorted(os.listdir(libraries_dir)):
                    if lib_name.endswith('.dylib'):
                        subprocess.run(
                            ['codesign', '--force', '--sign', identity,
                             '--options', 'runtime', '--timestamp',
                             os.path.join(libraries_dir, lib_name)],
                            check=True, timeout=180,
                        )
            for tool_name in (
                'app_mode_loader',
                'chrome_crashpad_handler',
                'web_app_shortcut_copier',
            ):
                tool_path = os.path.join(helpers_dir, tool_name)
                if os.path.isfile(tool_path):
                    subprocess.run(
                        ['codesign', '--force', '--sign', identity,
                         '--options', 'runtime', '--timestamp', tool_path],
                        check=True, timeout=180,
                    )

            print("Signing Maho Framework...")
            subprocess.run(
                [
                    'codesign',
                    '--force',
                    '--sign',
                    identity,
                    '--options',
                    'runtime',
                    maho_framework_path,
                ],
                check=True,
                timeout=120,
            )

        sparkle_path = os.path.join(frameworks_dir, 'Sparkle.framework')

        if os.path.isdir(sparkle_path):
            print("Signing Sparkle framework components...")
            installer_xpc = os.path.join(sparkle_path, 'Versions', 'B', 'XPCServices', 'Installer.xpc')
            if os.path.isdir(installer_xpc):
                subprocess.run(['codesign', '--force', '--sign', identity, '--options', 'runtime', installer_xpc], check=True, timeout=120)

            downloader_xpc = os.path.join(sparkle_path, 'Versions', 'B', 'XPCServices', 'Downloader.xpc')
            if os.path.isdir(downloader_xpc):
                subprocess.run(['codesign', '--force', '--sign', identity, '--options', 'runtime', '--preserve-metadata=entitlements', downloader_xpc], check=True, timeout=120)

            autoupdate = os.path.join(sparkle_path, 'Versions', 'B', 'Autoupdate')
            if os.path.isfile(autoupdate):
                subprocess.run(['codesign', '--force', '--sign', identity, '--options', 'runtime', autoupdate], check=True, timeout=120)

            updater_app = os.path.join(sparkle_path, 'Versions', 'B', 'Updater.app')
            if os.path.isdir(updater_app):
                subprocess.run(['codesign', '--force', '--sign', identity, '--options', 'runtime', updater_app], check=True, timeout=120)

            subprocess.run(['codesign', '--force', '--sign', identity, '--options', 'runtime', sparkle_path], check=True, timeout=120)

        # Sign the embedded mail helper before the outer bundle is sealed
        # (inside-out order, same as the Sparkle components above).
        mail_helper = os.path.join(app_path, 'Contents', 'MacOS', 'maho_mail_helper')
        if os.path.isfile(mail_helper):
            print("Signing maho_mail_helper...")
            helper_cmd = ['codesign', '--force', '--sign', identity, '--options', 'runtime']
            if os.path.isfile(helper_entitlements_path):
                helper_cmd += ['--entitlements', helper_entitlements_path]
            helper_cmd += [mail_helper]
            subprocess.run(helper_cmd, check=True, timeout=120)

        # Sign the staged omo runtime pieces before the outer bundle is sealed.
        # bun and maho-browser-mcp need the helper entitlements (allow-jit,
        # allow-unsigned-executable-memory, disable-library-validation) or AMFI
        # kills them.
        for staged_rel in (
            ('Contents', 'MacOS', 'bun'),
            ('Contents', 'MacOS', 'maho-browser-mcp'),
            ('Contents', 'Helpers', 'maho-browser-mcp'),
        ):
            staged_path = os.path.join(app_path, *staged_rel)
            if not os.path.isfile(staged_path):
                continue
            print(f"Signing staged {os.path.basename(staged_path)} in {staged_rel[1]}...")
            staged_cmd = [
                'codesign', '--force', '--sign', identity, '--options', 'runtime']
            if os.path.isfile(helper_entitlements_path):
                staged_cmd += ['--entitlements', helper_entitlements_path]
            staged_cmd += [staged_path]
            subprocess.run(staged_cmd, check=True, timeout=300)

        maho_cli = os.path.join(app_path, 'Contents', 'Helpers', 'maho')
        if os.path.isfile(maho_cli):
            print("Signing bundled maho CLI...")
            subprocess.run(
                [
                    'codesign',
                    '--force',
                    '--sign',
                    identity,
                    '--options',
                    'runtime',
                    '--timestamp',
                    maho_cli,
                ],
                check=True,
                timeout=180,
            )

        crashpad_handler = os.path.join(
            app_path, 'Contents', 'MacOS', 'chrome_crashpad_handler')
        if os.path.isfile(crashpad_handler):
            subprocess.run(
                ['codesign', '--force', '--sign', identity, '--options', 'runtime',
                 '--timestamp', crashpad_handler],
                check=True,
                timeout=180,
            )

        # Notarization audits every Mach-O in the bundle, including native
        # modules shipped inside the omo runtime's node_modules (e.g. pi-pty,
        # pi-tui prebuilds). codesign --deep seals them as resources but the
        # notary service rejects unsigned executables, so each one is signed
        # here with the Developer ID identity and a secure timestamp.
        omo_dir = os.path.join(app_path, 'Contents', 'Resources', 'omo')
        if os.path.isdir(omo_dir):
            signed_count = 0
            for root, dirs, files in os.walk(omo_dir, followlinks=False):
                for name in files:
                    path = os.path.join(root, name)
                    try:
                        with open(path, 'rb') as f:
                            magic = f.read(4)
                    except OSError:
                        continue
                    if magic not in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
                        continue
                    subprocess.run(
                        ['codesign', '--force', '--sign', identity,
                         '--options', 'runtime', '--timestamp', path],
                        check=True,
                        timeout=180,
                    )
                    signed_count += 1
            if signed_count:
                print(f"Signed {signed_count} native module(s) in the omo runtime")

        cmd = ['codesign', '--force', '--sign', identity, '--options', 'runtime']
        if os.path.isfile(app_entitlements_path):
            cmd += ['--entitlements', app_entitlements_path]
        cmd += [app_path]

        print(f"Signing app bundle: {app_path}")
        subprocess.run(cmd, check=True, timeout=120)
        return True
    except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"codesign failed: {e}", file=sys.stderr)
        return False


def _load_apple_notarization_env():
    """Load APPLE_API_KEY, APPLE_API_KEY_ID, APPLE_API_ISSUER from .secrets vault if missing."""
    import json
    if os.environ.get('APPLE_API_KEY') and os.environ.get('APPLE_API_KEY_ID') and os.environ.get('APPLE_API_ISSUER'):
        return

    # Check .secrets/maho/app-store-connect-api-key.json
    vault_paths = [
        os.path.join(_WORKSPACE_ROOT, '.secrets', 'maho', 'app-store-connect-api-key.json'),
        os.path.join(_WORKSPACE_ROOT, '..', '.secrets', 'maho', 'app-store-connect-api-key.json'),
    ]
    for p in vault_paths:
        if os.path.isfile(p):
            try:
                with open(p, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                key_id = data.get('key_id') or data.get('keyId')
                issuer_id = data.get('issuer_id') or data.get('issuerId')
                key_content = data.get('key')
                if key_id and issuer_id and key_content:
                    if not os.environ.get('APPLE_API_KEY_ID'):
                        os.environ['APPLE_API_KEY_ID'] = key_id
                    if not os.environ.get('APPLE_API_ISSUER'):
                        os.environ['APPLE_API_ISSUER'] = issuer_id
                    if not os.environ.get('APPLE_API_KEY'):
                        os.environ['APPLE_API_KEY'] = key_content
                    return
            except Exception as e:
                print(f"warning: failed to parse {p}: {e}", file=sys.stderr)


def notarize_app(app_path: str) -> bool:
    import tempfile

    # Fast path: if the app is already stapled and accepted by Gatekeeper, avoid redundant upload
    try:
        staple_check = subprocess.run(
            ['xcrun', 'stapler', 'validate', app_path],
            capture_output=True, text=True
        )
        spctl_check = subprocess.run(
            ['spctl', '-a', '-t', 'exec', app_path],
            capture_output=True, text=True
        )
        if staple_check.returncode == 0 and spctl_check.returncode == 0:
            print(f"{app_path} is already notarized, stapled, and accepted by Gatekeeper.")
            return True
    except Exception:
        pass

    _load_apple_notarization_env()
    api_key = os.environ.get('APPLE_API_KEY')
    key_id = os.environ.get('APPLE_API_KEY_ID')
    issuer_id = os.environ.get('APPLE_API_ISSUER')

    if not (api_key and key_id and issuer_id):
        print("warning: APPLE_API_KEY, APPLE_API_KEY_ID, or APPLE_API_ISSUER environment variables are missing; skipping notarization", file=sys.stderr)
        return False

    print("Preparing app bundle for notarization...")
    zip_path = app_path + '.zip'
    if os.path.exists(zip_path):
        os.remove(zip_path)

    try:
        subprocess.run(['ditto', '-c', '-k', '--keepParent', app_path, zip_path], check=True)
    except Exception as e:
        print(f"error: failed to zip app bundle: {e}", file=sys.stderr)
        return False

    key_path = None
    try:
        # Write API key to temp file
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.p8') as f:
            f.write(api_key)
            key_path = f.name

        cmd = [
            'xcrun', 'notarytool', 'submit', zip_path,
            '--key-id', key_id,
            '--issuer', issuer_id,
            '--key', key_path,
            '--wait',
        ]

        max_attempts = 3
        res = None
        for attempt in range(1, max_attempts + 1):
            print(f"Submitting to Apple Notary Service (attempt {attempt}/{max_attempts})...")
            res = subprocess.run(cmd, capture_output=True, text=True)
            print(res.stdout)
            if res.returncode == 0:
                break
            err_msg = (res.stderr or "") + (res.stdout or "")
            if attempt < max_attempts and any(w in err_msg.lower() for w in ("timeout", "network", "connect")):
                print(f"warning: transient notary submission error, retrying in 5s:\n{res.stderr}", file=sys.stderr)
                time.sleep(5)
            else:
                print(f"error: notarization submission failed:\n{res.stderr}", file=sys.stderr)
                return False

        print("Stapling notarization ticket to app bundle...")
        subprocess.run(['xcrun', 'stapler', 'staple', app_path], check=True)

        print("Verifying gatekeeper status...")
        subprocess.run(['spctl', '-a', '-t', 'exec', '-vv', app_path], check=True)
        print("Notarization and verification succeeded!")
        return True
    except Exception as e:
        print(f"error: notarization/stapling failed: {e}", file=sys.stderr)
        return False
    finally:
        if key_path and os.path.exists(key_path):
            os.remove(key_path)
        if os.path.exists(zip_path):
            os.remove(zip_path)


def read_maho_version() -> str:
    """Return the canonical product version from maho/version.txt.

    This is the single authored product-version source; artifact names and
    release manifests derive from it. A missing file is a hard error.
    """
    with open(_VERSION_TXT) as f:
        return f.read().strip()


_DMG_CONTENT_SIZE = (664, 400)
_DMG_TITLE_BAR_HEIGHT = 28
_DMG_WINDOW_XY = (100, 100)
_DMG_ICON_SIZE_PT = 68
_DMG_APP_ICON_CENTER = (215, 318)
_DMG_APPLICATIONS_CENTER = (450, 318)


def _write_dmg_layout(mount_point: str) -> None:
    """Write the branded Finder layout (.DS_Store) onto the mounted volume.

    Mirrors dmgbuild's mechanism: a picture background recorded via an alias
    to `.background.png` inside the image, explicit icon coordinates, and
    window bounds, all written as .DS_Store records with the ds_store package.
    No AppleScript or Finder interaction is involved. Coordinates are in
    points and match the approved 1328x800 (664x400pt @2x) background art in
    branding/mac/dmg-background.png.
    """
    try:
        from ds_store import DSStore
        from mac_alias import Alias
    except ImportError as e:
        raise RuntimeError("dmg layout requires the ds_store/mac_alias packages "
                           "(pip3 install dmgbuild)") from e

    background_in_image = os.path.join(mount_point, '.background.png')
    shutil.copyfile(_DMG_BACKGROUND, background_in_image)
    alias = Alias.for_file(background_in_image)

    window_left, window_top = _DMG_WINDOW_XY
    content_width, content_height = _DMG_CONTENT_SIZE
    bwsp = {
        'ShowStatusBar': False,
        'WindowBounds': f'{{{{{window_left}, {window_top}}}, {{{content_width}, '
                        f'{content_height + _DMG_TITLE_BAR_HEIGHT}}}}}',
        'ContainerShowSidebar': False,
        'PreviewPaneVisibility': False,
        'SidebarWidth': 180,
        'ShowTabView': False,
        'ShowToolbar': False,
        'ShowPathbar': False,
        'ShowSidebar': False,
    }
    icvp = {
        'viewOptionsVersion': 1,
        'backgroundType': 2,
        'backgroundColorRed': 1.0,
        'backgroundColorGreen': 1.0,
        'backgroundColorBlue': 1.0,
        'gridOffsetX': 0.0,
        'gridOffsetY': 0.0,
        'gridSpacing': 100.0,
        'arrangeBy': 'none',
        'showIconPreview': False,
        'showItemInfo': False,
        'labelOnBottom': True,
        'textSize': 16.0,
        'iconSize': float(_DMG_ICON_SIZE_PT),
        'scrollPositionX': 0.0,
        'scrollPositionY': 0.0,
        'backgroundImageAlias': alias.to_bytes(),
    }
    with DSStore.open(os.path.join(mount_point, '.DS_Store'), 'w+') as store:
        store['.']['vSrn'] = ('long', 1)
        store['.']['bwsp'] = bwsp
        store['.']['icvp'] = icvp
        store['.']['icvl'] = (b'type', 'icnv')
        for item, center in (('Maho.app', _DMG_APP_ICON_CENTER),
                             ('Applications', _DMG_APPLICATIONS_CENTER)):
            store[item]['Iloc'] = (
                center[0] - _DMG_ICON_SIZE_PT // 2,
                center[1] - _DMG_ICON_SIZE_PT // 2,
            )


def _create_dmg_image(staging_dir: str, dmg_path: str, volname: str) -> None:
    """Synthesize a read-only compressed DMG carrying the branded layout.

    Flow: `hdiutil create -srcfolder ... -format UDRW`, attach read-write,
    write the Finder layout (.DS_Store + background alias) on the mounted
    volume, detach, then convert to UDZO. Writing the layout on the mounted
    image (never via AppleScript/Finder) records the background alias against
    the image's own volume, exactly as dmgbuild does. This still deliberately
    avoids Chromium's pkg-dmg, whose read-only path runs `hdiutil makehybrid
    -hfs`; makehybrid synthesizes `com.apple.FinderInfo` on every catalog
    entry of the resulting HFS filesystem even when the source tree is
    completely clean, which makes the embedded app fail
    `codesign --verify --deep --strict` ("resource fork, Finder information,
    or similar detritus not allowed"). Raises on failure (subprocess
    check=True).
    """
    import tempfile

    rw_dmg = dmg_path + '.rw.dmg'
    if os.path.exists(rw_dmg):
        os.remove(rw_dmg)
    mount_point = tempfile.mkdtemp(prefix='maho-dmg-layout-')
    try:
        subprocess.run(
            ['hdiutil', 'create', '-ov', '-srcfolder', staging_dir,
             '-volname', volname, '-fs', 'HFS+', '-format', 'UDRW', rw_dmg],
            check=True, timeout=600,
        )
        subprocess.run(
            ['hdiutil', 'attach', '-nobrowse', '-readwrite',
             '-mountpoint', mount_point, rw_dmg],
            check=True, capture_output=True, text=True, timeout=120,
        )
        try:
            _write_dmg_layout(mount_point)
        finally:
            subprocess.run(['hdiutil', 'detach', '-force', mount_point],
                           check=True, timeout=120)
        subprocess.run(
            ['hdiutil', 'convert', rw_dmg, '-format', 'UDZO', '-o', dmg_path],
            check=True, timeout=600,
        )
    finally:
        if os.path.exists(rw_dmg):
            os.remove(rw_dmg)
        shutil.rmtree(mount_point, ignore_errors=True)


def verify_dmg_app_signature(dmg_path: str) -> bool:
    """Fail-closed gate: mount the DMG read-only and verify the embedded app.

    Attaches the image read-only at a private temporary mount point (never
    guesses `/Volumes/...`), runs `codesign --verify --deep --strict` on the
    embedded Maho.app, and requires ZERO `com.apple.FinderInfo` extended
    attributes anywhere in the bundle. The mount point is fixed up front so the
    volume is ALWAYS detached in the finally block, even if a later step fails.
    A failed `xattr` scan is treated as failure (fail-closed), never as "0
    FinderInfo". Returns True only when the mounted app is strict-clean.
    """
    import tempfile
    if not os.path.isfile(dmg_path):
        print(f"error: dmg not found for verification: {dmg_path}", file=sys.stderr)
        return False
    mount_point = tempfile.mkdtemp(prefix='maho-dmg-verify-')
    attached = False
    try:
        try:
            attach = subprocess.run(
                ['hdiutil', 'attach', '-nobrowse', '-readonly',
                 '-mountpoint', mount_point, dmg_path],
                capture_output=True, text=True, timeout=120,
            )
        except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
            print(f"error: hdiutil attach failed: {e}", file=sys.stderr)
            return False
        if attach.returncode != 0:
            print(f"error: hdiutil attach failed: {attach.stderr!r}", file=sys.stderr)
            return False
        attached = True

        app = os.path.join(mount_point, 'Maho.app')
        if not os.path.isdir(app):
            print(f"error: Maho.app missing from mounted dmg: {app}", file=sys.stderr)
            return False
        verify = subprocess.run(
            ['codesign', '--verify', '--deep', '--strict', '--verbose=2', app],
            capture_output=True, text=True, timeout=300,
        )
        if verify.returncode != 0:
            print(f"error: mounted dmg app failed strict codesign:\n{verify.stderr}",
                  file=sys.stderr)
            return False
        dump = subprocess.run(['xattr', '-lr', app], capture_output=True, text=True, timeout=120)
        if dump.returncode != 0:
            # A failed scan must NOT be read as "0 FinderInfo" — fail closed.
            print(f"error: xattr scan failed (rc={dump.returncode}): {dump.stderr!r}",
                  file=sys.stderr)
            return False
        finderinfo = sum(
            1 for line in dump.stdout.splitlines() if 'com.apple.FinderInfo' in line
        )
        if finderinfo != 0:
            print(f"error: mounted dmg app has {finderinfo} com.apple.FinderInfo xattrs",
                  file=sys.stderr)
            return False
        print("Mounted dmg app passed strict codesign with 0 FinderInfo xattrs.")
        return True
    finally:
        if attached:
            subprocess.run(['hdiutil', 'detach', mount_point, '-quiet'],
                           capture_output=True, timeout=120)
        try:
            os.rmdir(mount_point)
        except OSError:
            pass


def build_dmg(app_path: str, out_dir: str, version: str, identity: str) -> str | None:
    """Package a signed Maho.app into out/<out_dir>/Maho-<version>.dmg.

    Stages a clean copy of the signed app plus an /Applications symlink, then
    synthesizes the image with `hdiutil create -srcfolder` (NOT pkg-dmg, whose
    makehybrid path injects FinderInfo detritus). The disk image is
    code-signed so Gatekeeper can validate it and it can be notarized+stapled.
    A fail-closed gate then mounts the finished dmg and verifies the embedded
    app is deep-strict clean before returning; on any failure the invalid dmg
    is deleted and None is returned. Returns the dmg path, or None on failure.
    """
    if not os.path.isdir(app_path):
        print(f"error: app bundle not found: {app_path}; cannot build dmg", file=sys.stderr)
        return None
    dmg_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, f'Maho-{version}.dmg')
    if os.path.exists(dmg_path):
        os.remove(dmg_path)

    # Build a staging root containing ONLY the app + an /Applications symlink.
    # hdiutil create -srcfolder images exactly this tree, giving the familiar
    # drag-install layout without pkg-dmg's makehybrid FinderInfo injection.
    staging_dir = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, '.maho_dmg_stage')
    if os.path.isdir(staging_dir):
        shutil.rmtree(staging_dir)
    os.makedirs(staging_dir)
    staged_app = os.path.join(staging_dir, 'Maho.app')
    try:
        try:
            # ditto preserves the bundle bytes, symlinks, and code signature exactly.
            subprocess.run(['ditto', app_path, staged_app], check=True, timeout=600)
        except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
            print(f"error: staging app copy failed: {e}", file=sys.stderr)
            return None
        os.symlink('/Applications', os.path.join(staging_dir, 'Applications'))

        # The staged app must itself be deep-strict clean before we image it; a
        # drifted or invalid source is a hard error (do not ship it).
        staged_verify = subprocess.run(
            ['codesign', '--verify', '--deep', '--strict', staged_app],
            capture_output=True, text=True, timeout=300,
        )
        if staged_verify.returncode != 0:
            print(f"error: staged app failed strict codesign; not packaging:\n"
                  f"{staged_verify.stderr}", file=sys.stderr)
            return None

        print(f"Building disk image: {dmg_path}")
        try:
            _create_dmg_image(staging_dir, dmg_path, f'Maho {version}')
        except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
            print(f"error: hdiutil create failed: {e}", file=sys.stderr)
            return None

        # Fail-closed: the embedded app must be deep-strict clean in the finished
        # image BEFORE we sign/notarize. Delete the invalid artifact so no later
        # step (release publish) can upload it.
        if not verify_dmg_app_signature(dmg_path):
            print("error: dmg embedded app failed verification gate; deleting artifact",
                  file=sys.stderr)
            if os.path.exists(dmg_path):
                os.remove(dmg_path)
            return None

        try:
            # Sign the disk image itself (codesign contacts Apple's timestamp
            # server by default, which notarization requires).
            print(f"Signing disk image: {dmg_path}")
            subprocess.run(['codesign', '--force', '--sign', identity, dmg_path], check=True, timeout=300)
        except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
            print(f"error: signing dmg failed: {e}", file=sys.stderr)
            if os.path.exists(dmg_path):
                os.remove(dmg_path)
            return None
        return dmg_path
    finally:
        # Never leave the staging tree behind (success or failure).
        shutil.rmtree(staging_dir, ignore_errors=True)


def notarize_dmg(dmg_path: str) -> bool:
    """Submit the signed .dmg to Apple's notary service and staple the ticket.

    Notarizing the disk image covers the hardened-runtime-signed app inside it,
    so a stapled .dmg passes Gatekeeper offline. Requires the same
    APPLE_API_KEY/_KEY_ID/_ISSUER credentials as notarize_app.
    """
    import tempfile
    _load_apple_notarization_env()
    api_key = os.environ.get('APPLE_API_KEY')
    key_id = os.environ.get('APPLE_API_KEY_ID')
    issuer_id = os.environ.get('APPLE_API_ISSUER')
    if not (api_key and key_id and issuer_id):
        print("warning: APPLE_API_KEY, APPLE_API_KEY_ID, or APPLE_API_ISSUER environment variables are missing; skipping notarization", file=sys.stderr)
        return False
    if not os.path.isfile(dmg_path):
        print(f"error: dmg not found: {dmg_path}", file=sys.stderr)
        return False
    key_path = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.p8') as f:
            f.write(api_key)
            key_path = f.name
        max_attempts = 3
        res = None
        for attempt in range(1, max_attempts + 1):
            print(f"Submitting disk image to Apple Notary Service (attempt {attempt}/{max_attempts})...")
            res = subprocess.run(
                ['xcrun', 'notarytool', 'submit', dmg_path,
                 '--key-id', key_id, '--issuer', issuer_id, '--key', key_path, '--wait'],
                capture_output=True, text=True,
            )
            print(res.stdout)
            if res.returncode == 0:
                break
            err_msg = (res.stderr or "") + (res.stdout or "")
            if attempt < max_attempts and any(w in err_msg.lower() for w in ("timeout", "network", "connect")):
                print(f"warning: transient notary submission error, retrying in 5s:\n{res.stderr}", file=sys.stderr)
                time.sleep(5)
            else:
                print(f"error: notarization submission failed:\n{res.stderr}", file=sys.stderr)
                return False
        print("Stapling notarization ticket to disk image...")
        subprocess.run(['xcrun', 'stapler', 'staple', dmg_path], check=True)
        subprocess.run(['spctl', '-a', '-t', 'open', '--context', 'context:primary-signature', '-vv', dmg_path], check=True)
        print("DMG notarization and stapling succeeded!")
        return True
    except Exception as e:
        print(f"error: dmg notarization/stapling failed: {e}", file=sys.stderr)
        return False
    finally:
        if key_path and os.path.exists(key_path):
            os.remove(key_path)


def build_windows_installer(out_dir: str, version: str) -> str | None:
    """Copy the built mini_installer.exe to out/<out_dir>/MahoSetup-<version>.exe.

    The mini_installer ninja target produces mini_installer.exe in the output
    root; the release artifact name is the version-stamped MahoSetup exe that
    the download page and Windows update manifest reference. Returns the setup
    exe path, or None on failure.
    """
    src = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'mini_installer.exe')
    if not os.path.isfile(src):
        print(f"error: mini_installer.exe not found: {src}; "
              "build the 'mini_installer' target first", file=sys.stderr)
        return None
    setup_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, f'MahoSetup-{version}.exe')
    print(f"Packaging Windows installer: {setup_path}")
    shutil.copy2(src, setup_path)
    return setup_path


def sign_windows_binary(path: str) -> bool:
    """Authenticode-sign a Windows binary with signtool.

    Configured via environment: MAHO_SIGNTOOL (signtool.exe path; falls back to
    signtool on PATH), MAHO_WIN_CERT_PFX + MAHO_WIN_CERT_PASSWORD (pfx signing),
    or MAHO_WIN_CERT_SHA1 (thumbprint of a cert already in the Windows store).
    MAHO_WIN_TIMESTAMP_URL overrides the RFC-3161 timestamp server. Missing
    credentials skip signing with a warning (mirrors macOS notarization).
    """
    signtool = os.environ.get('MAHO_SIGNTOOL') or shutil.which('signtool') or shutil.which('signtool.exe')
    if not signtool:
        print("warning: signtool not found (set MAHO_SIGNTOOL); skipping Authenticode signing", file=sys.stderr)
        return False
    pfx = os.environ.get('MAHO_WIN_CERT_PFX')
    pfx_pw = os.environ.get('MAHO_WIN_CERT_PASSWORD')
    sha1 = os.environ.get('MAHO_WIN_CERT_SHA1')
    timestamp = os.environ.get('MAHO_WIN_TIMESTAMP_URL', 'http://timestamp.digicert.com')
    cmd = [signtool, 'sign', '/fd', 'SHA256', '/tr', timestamp, '/td', 'SHA256']
    if pfx:
        cmd += ['/f', pfx]
        if pfx_pw:
            cmd += ['/p', pfx_pw]
    elif sha1:
        cmd += ['/sha1', sha1]
    else:
        print("warning: no Windows signing cert configured "
              "(set MAHO_WIN_CERT_PFX[/_PASSWORD] or MAHO_WIN_CERT_SHA1); "
              "skipping Authenticode signing", file=sys.stderr)
        return False
    cmd.append(path)
    try:
        print(f"Authenticode-signing: {path}")
        subprocess.run(cmd, check=True, timeout=300)
        subprocess.run([signtool, 'verify', '/pa', path], check=True, timeout=120)
        return True
    except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"error: Authenticode signing failed: {e}", file=sys.stderr)
        return False


def sign_windows_installer(exe_path: str) -> bool:
    """Authenticode-sign the Windows installer with signtool."""
    return sign_windows_binary(exe_path)


def launch_app(app_path: str) -> None:
    if sys.platform != 'darwin':
        return
    if not os.path.isdir(app_path):
        print(f"warning: app bundle not found: {app_path}; skipping launch", file=sys.stderr)
        return
    print(f"Launching app: {app_path}")
    subprocess.Popen(['open', app_path])


def ensure_args_gn(chromium_src, out_dir, platform, force, release=False, debug=False):
    args_gn_path = os.path.join(chromium_src, out_dir, 'args.gn')
    suffix = '_release' if release else '_debug' if debug else ''
    template_path = os.path.join(_BUILD_CONFIG_DIR, f'args_{platform}{suffix}.gn')

    if not os.path.isfile(template_path):
        print(
            f'warning: args template not found: {template_path}; skipping args.gn bootstrap',
        )
        return

    if os.path.isfile(args_gn_path) and not force:
        # A release build must not silently reuse a stale/dev args.gn: a
        # component build (or one lacking is_official_build=true) produces a
        # NON-distributable app (engine dylibs load from the build dir via
        # rpath and are never bundled, so installed/DMG copies fail to launch).
        # Regenerate from the release template in that case; otherwise respect
        # the existing args.gn (preserves locally-synced values).
        stale_release = False
        if release:
            with open(args_gn_path, encoding='utf-8') as existing:
                current = existing.read()
            if 'is_component_build = true' in current or 'is_official_build = true' not in current:
                stale_release = True
                print(
                    f'warning: existing {args_gn_path} is not an official '
                    'non-component release config; regenerating from '
                    f'{template_path}',
                    file=sys.stderr,
                )
        if not stale_release:
            print(f'args.gn already exists: {args_gn_path} (use --force-args-template to overwrite)')
            sync_cc_wrapper(args_gn_path, platform)
            sync_media_codec_flags(args_gn_path)
            return

    out_dir_abs = os.path.join(chromium_src, out_dir)
    os.makedirs(out_dir_abs, exist_ok=True)
    already_existed = os.path.isfile(args_gn_path)
    shutil.copy2(template_path, args_gn_path)
    action = 'Overwrote' if already_existed else 'Created'
    print(f'{action} args.gn from {template_path}')
    sync_cc_wrapper(args_gn_path, platform)
    sync_media_codec_flags(args_gn_path)


def ensure_macos_sdk_path(args_gn_path: str) -> bool:
    """Point a macOS args.gn at the staged SDK via a build-output symlink.

    `use_clang_modules = false` (in the args templates) only compiles against
    SDK 27 when the sysroot is the *staged* SDK whose math.h patch keeps
    `FLT_EPSILON`/`INFINITY` defined; against the SDK inside Xcode.app the
    build dies in protobuf with `use of undeclared identifier 'FLT_EPSILON'`.
    The staged copy lives per-machine, so it cannot ship in the templates —
    stage it on demand and record the path here.

    Chromium 154's `sdk_inputs` action lists `$mac_sdk_path/usr/include/mach/exc.defs`
    as an action output, requiring `mac_sdk_path` to reside within the build output
    directory (e.g. `//out/Release/sdk/xcode_links/MacOSX.sdk`). An external
    absolute path like `/Users/.../SDKs/MacOSX.sdk` is rejected by GN with:
    `File is not inside output directory`.

    Consistent with upstream Chromium's `//build/config/apple/sdk_info.py` convention,
    this helper creates a symlink at `<out_dir>/sdk/xcode_links/<sdk_name>` pointing to
    the verified staged SDK and configures `mac_sdk_path = "//{rel_out}/sdk/xcode_links/{sdk_name}"`.

    Returns True when args.gn already carried a usable path or was successfully updated.
    """
    # Loaded lazily: this runs only on macOS, and a missing helper must never
    # break the import of build_maho.py on the Linux/Windows builders.
    stage_macos_sdk = load_module(
        'maho_stage_macos_sdk', os.path.join(_SCRIPT_DIR, 'stage_macos_sdk.py')
    )
    if not os.path.isfile(args_gn_path):
        return False
    with open(args_gn_path, encoding='utf-8') as f:
        content = f.read()

    out_dir_path = os.path.dirname(os.path.abspath(args_gn_path))

    # Determine the Chromium source root and the output directory relative to it.
    src_root = None
    cur = out_dir_path
    while cur and cur != os.path.dirname(cur):
        if os.path.isfile(os.path.join(cur, '.gn')):
            src_root = cur
            break
        cur = os.path.dirname(cur)

    if src_root:
        rel_out = os.path.relpath(out_dir_path, src_root)
    else:
        try:
            rel = os.path.relpath(out_dir_path, _CHROMIUM_SRC_ROOT)
            if not rel.startswith('..'):
                rel_out = rel
            else:
                raise ValueError()
        except Exception:
            parts = out_dir_path.replace('\\', '/').split('/')
            if 'out' in parts:
                idx = len(parts) - 1 - parts[::-1].index('out')
                rel_out = '/'.join(parts[idx:])
            else:
                rel_out = f"out/{os.path.basename(out_dir_path)}"
    rel_out = rel_out.replace('\\', '/').strip('/')

    existing = re.search(r'^\s*mac_sdk_path\s*=\s*"([^"]+)"', content,
                         re.MULTILINE)

    def _resolve_real_sdk(preferred_path: str | None = None) -> str:
        if preferred_path and stage_macos_sdk.check(preferred_path) == 0:
            return preferred_path
        default_dest = getattr(stage_macos_sdk, 'DEFAULT_DEST', os.path.expanduser('~/SDKs/MacOSX.sdk'))
        if stage_macos_sdk.check(default_dest) == 0:
            return default_dest
        return stage_macos_sdk.stage(
            stage_macos_sdk.find_source_sdk(), default_dest,
            force=False,
        )

    def _ensure_symlink(target: str, link_path: str) -> None:
        target_abs = os.path.abspath(target)
        if os.path.islink(link_path):
            try:
                cur_target = os.readlink(link_path)
                if not os.path.isabs(cur_target):
                    cur_target = os.path.normpath(os.path.join(os.path.dirname(link_path), cur_target))
                if os.path.abspath(cur_target) == target_abs and os.path.exists(link_path):
                    return
            except OSError:
                pass
            os.unlink(link_path)
        elif os.path.lexists(link_path):
            raise RuntimeError(
                f'Refusing to replace existing non-symlink SDK path: {link_path}')

        os.symlink(target, link_path)

    # Check if args.gn already specifies a build-output relative symlink path (starts with //)
    if existing and existing.group(1).startswith('//'):
        gn_val = existing.group(1)
        subpath = gn_val[2:].replace('/', os.sep)
        if src_root:
            fs_sdk_path = os.path.join(src_root, subpath)
        elif out_dir_path.replace('\\', '/').endswith(rel_out):
            base_dir = out_dir_path[:-len(rel_out)].rstrip('/\\')
            fs_sdk_path = os.path.join(base_dir, subpath)
        else:
            fs_sdk_path = os.path.join(out_dir_path, 'sdk', 'xcode_links', os.path.basename(subpath))

        # If the symlink already exists and points to a healthy staged SDK, we are idempotent.
        if os.path.islink(fs_sdk_path):
            try:
                link_dest = os.readlink(fs_sdk_path)
                if not os.path.isabs(link_dest):
                    link_dest = os.path.normpath(os.path.join(os.path.dirname(fs_sdk_path), link_dest))
                if stage_macos_sdk.check(link_dest) == 0:
                    return True
            except OSError:
                pass

        # If missing or pointing to invalid target, repair symlink
        real_sdk = _resolve_real_sdk()
        os.makedirs(os.path.dirname(fs_sdk_path), exist_ok=True)
        _ensure_symlink(real_sdk, fs_sdk_path)
        return True

    # If args.gn had an existing external path, check if it's already a valid staged SDK
    candidate_real = existing.group(1) if existing else None
    real_sdk = _resolve_real_sdk(candidate_real)

    sdk_name = os.path.basename(real_sdk.rstrip('/\\')) or 'MacOSX.sdk'
    symlink_dir = os.path.join(out_dir_path, 'sdk', 'xcode_links')
    os.makedirs(symlink_dir, exist_ok=True)
    symlink_path = os.path.join(symlink_dir, sdk_name)
    _ensure_symlink(real_sdk, symlink_path)

    gn_sdk_path = f"//{rel_out}/sdk/xcode_links/{sdk_name}"
    assignment = f'mac_sdk_path = "{gn_sdk_path}"'

    if existing:
        content = content.replace(existing.group(0), assignment)
        print(f'  mac_sdk_path -> {gn_sdk_path} (migrated from {existing.group(1)})')
    else:
        content = content.rstrip('\n') + f'\n{assignment}\n'
        print(f'  mac_sdk_path -> {gn_sdk_path}')

    with open(args_gn_path, 'w', encoding='utf-8') as f:
        f.write(content)
    return True


def sync_media_codec_flags(args_gn_path: str) -> bool:
    """Ensure mandatory media codecs and Widevine DRM flags are present in args.gn."""
    if not os.path.isfile(args_gn_path):
        return False
    with open(args_gn_path, encoding='utf-8') as f:
        content = f.read()

    required_flags = [
        ('proprietary_codecs', 'proprietary_codecs = true'),
        ('ffmpeg_branding', 'ffmpeg_branding = "Chrome"'),
        ('enable_widevine', 'enable_widevine = true'),
        ('ignore_missing_widevine_signing_cert', 'ignore_missing_widevine_signing_cert = true'),
    ]

    missing = []
    for key, assignment in required_flags:
        if not re.search(rf'^\s*{key}\s*=', content, re.MULTILINE):
            missing.append(assignment)

    if not missing:
        return False

    updated = content.rstrip() + '\n' + '\n'.join(missing) + '\n'
    with open(args_gn_path, 'w', encoding='utf-8') as f:
        f.write(updated)
    print(f'Synchronized mandatory media flags into {args_gn_path}: {", ".join(m.split()[0] for m in missing)}')
    return True


def sync_cc_wrapper(args_gn_path: str, platform: str) -> bool:
    """Ensure cc_wrapper is configured in args.gn so builds always use sccache."""
    if not os.path.isfile(args_gn_path):
        return False
    with open(args_gn_path, encoding='utf-8') as f:
        content = f.read()
    if re.search(r'^\s*cc_wrapper\s*=', content, re.MULTILINE):
        return False

    if platform == 'mac':
        wrapper = '/opt/homebrew/bin/sccache' if os.path.exists('/opt/homebrew/bin/sccache') else 'sccache'
    elif platform == 'win':
        wrapper = 'sccache.exe'
    else:
        wrapper = 'sccache'

    assignment = f'cc_wrapper = "{wrapper}"'
    updated = f"{content.rstrip()}\n{assignment}\n"
    with open(args_gn_path, 'w', encoding='utf-8') as f:
        f.write(updated)
    print(f'Synchronized mandatory cc_wrapper = "{wrapper}" into {args_gn_path}')
    return True


def ensure_webui_dependencies(workspace_root: str) -> None:
    """Verify that WebUI dependencies and Tailwind CLI are installed and functional.
    Auto-bootstraps missing website/node_modules and @tailwindcss/cli so builds never
    break mid-flight during the long Chromium compile phase."""
    website_dir = os.path.join(workspace_root, 'website')
    if not os.path.isdir(website_dir):
        return

    node_modules = os.path.join(website_dir, 'node_modules')
    tailwind_bin = os.path.join(node_modules, '.bin', 'tailwindcss')
    esbuild_pkg = os.path.join(node_modules, 'esbuild')

    needs_install = (
        not os.path.isdir(node_modules)
        or not os.path.isfile(tailwind_bin)
        or not os.path.isdir(esbuild_pkg)
    )

    if needs_install:
        bun_bin = shutil.which('bun') or (
            os.path.expanduser('~/.bun/bin/bun')
            if os.path.exists(os.path.expanduser('~/.bun/bin/bun'))
            else None
        )
        if bun_bin:
            print('Preflight: bootstrapping WebUI dependencies in website/ via bun install...')
            subprocess.run([bun_bin, 'install'], cwd=website_dir, check=True)
            if not os.path.isfile(tailwind_bin):
                print('Preflight: installing @tailwindcss/cli in website/...')
                subprocess.run([bun_bin, 'add', '@tailwindcss/cli'], cwd=website_dir, check=True)
        else:
            npm_bin = shutil.which('npm')
            if npm_bin:
                print('warning: bun not found; installing website dependencies via npm...')
                subprocess.run([npm_bin, 'install'], cwd=website_dir, check=True)
            else:
                print('warning: neither bun nor npm found on PATH; skipping WebUI dependency bootstrap')


def sync_local_google_api_key(workspace_root, args_gn_path, release):
    vault_path = os.path.join(
        workspace_root,
        '.secrets',
        'maho',
        'google_chromium_api_key.env',
    )
    if not os.path.isfile(vault_path):
        if release:
            raise RuntimeError(
                'release build requires local Google API key: '
                f'{vault_path}'
            )
        return False

    with open(vault_path, encoding='utf-8') as vault_file:
        key_lines = [
            line.rstrip('\n').removeprefix('GOOGLE_API_KEY=')
            for line in vault_file
            if line.startswith('GOOGLE_API_KEY=')
        ]
    if len(key_lines) != 1 or not re.fullmatch(r'AIza[0-9A-Za-z_-]{35}', key_lines[0]):
        raise RuntimeError(f'invalid local Google API key file: {vault_path}')

    with open(args_gn_path, encoding='utf-8') as args_file:
        args = args_file.read()
    assignment = f'google_api_key = "{key_lines[0]}"'
    api_key_pattern = re.compile(r'^google_api_key\s*=\s*"[^"]*"\s*$', re.MULTILINE)
    if api_key_pattern.search(args):
        updated_args = api_key_pattern.sub(assignment, args, count=1)
    else:
        updated_args = f'{args.rstrip()}\n{assignment}\n'
    if updated_args != args:
        with open(args_gn_path, 'w', encoding='utf-8') as args_file:
            args_file.write(updated_args)
        print(f'Synchronized local Google API key into {args_gn_path}')
        return True
    return False


def run_gn_gen(chromium_src, out_dir, depot_tools_dir):
    gn_bin = 'gn.bat' if host_platform() == 'win' else 'gn'
    gn_cmd = shutil.which(gn_bin) or os.path.join(depot_tools_dir, gn_bin)
    env = os.environ.copy()
    path = env.get('PATH', '')
    env['PATH'] = os.pathsep.join([depot_tools_dir, path]) if path else depot_tools_dir
    print(f'Running gn gen for {out_dir}...')
    subprocess.run(
        [gn_cmd, 'gen', out_dir],
        cwd=chromium_src,
        check=True,
        env=env,
    )


def build_chromium_app(out_dir, targets, extra_args):
    env = os.environ.copy()
    if sys.platform == 'win32':
        # Ensure sccache server is active on port 4228 (avoiding WSL port 4226 collision)
        env['SCCACHE_SERVER_PORT'] = '4228'
        env['SCCACHE_IDLE_TIMEOUT'] = '0'
        try:
            subprocess.run(['sccache', '--start-server'], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        except Exception:
            pass
    path = env.get('PATH', '')
    env['PATH'] = os.pathsep.join([_DEPOT_TOOLS_DIR, path]) if path else _DEPOT_TOOLS_DIR
    # Keep the default low enough to avoid exhausting memory on local builds.
    # Windows is the exception: the maho-win builder (6C/12T, 32 GB) sat at ~20%
    # CPU on 4 jobs and crawled through a full Chromium build, so it always runs
    # 12. MAHO_NINJA_JOBS and a caller-supplied -j continue to override it.
    default_jobs = '12' if sys.platform == 'win32' else '4'
    jobs = os.environ.get('MAHO_NINJA_JOBS', default_jobs)
    caller_set_j = any(a == '-j' or a.startswith('-j') for a in extra_args)
    if not targets:
        # Never fall through to a bare autoninja: with no target ninja builds
        # the default (all), which pulls in the whole test tree.
        print('warning: no ninja targets requested; skipping chromium build')
        return

    # When switching to or using siso, lingering .ninja_* files cause siso to fail.
    # Clean them up safely before invoking autoninja.
    out_dir_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir)
    if os.path.exists(out_dir_path):
        for ninja_file in ('.ninja_log', '.ninja_deps', '.ninja_lock'):
            target_file = os.path.join(out_dir_path, ninja_file)
            if os.path.exists(target_file):
                try:
                    os.unlink(target_file)
                except OSError:
                    pass

    # One invocation for every target. ninja reloads the entire build graph per
    # run, and that fixed cost measures ~25s on out/Default (68% .ninja parse,
    # 27% node stat). Passing the targets together pays it once instead of once
    # per target, and lets ninja schedule them in a single parallel pass.
    if host_platform() == 'win':
        env['PYTHONUTF8'] = '0'
        command = [sys.executable, os.path.join(_DEPOT_TOOLS_DIR, 'autoninja.py'), '-C', out_dir]
    else:
        command = ['autoninja', '-C', out_dir]
    if jobs and not caller_set_j:
        command += ['-j', jobs]
    command += [*targets, *extra_args]
    print(f"Building Chromium targets {' '.join(targets)} with autoninja...")
    subprocess.run(command, cwd=_CHROMIUM_SRC_ROOT, check=True, env=env)


def apply_tracked_chromium_overrides(platform):
    print('Applying tracked Chromium overrides...')
    subprocess.run(
        [
            sys.executable,
            _APPLY_OVERRIDES_SCRIPT,
            '--chromium-src', _CHROMIUM_SRC_ROOT,
            '--platform', platform,
        ],
        cwd=_WORKSPACE_ROOT,
        check=True,
    )


def _build_windows_ico(branding_dir):
    """Pack a multi-size Windows .ico (16/32/48/256) from Maho product logos."""
    import struct
    png_data = []
    for size in (16, 32, 48, 256):
        path = os.path.join(branding_dir, f'product_logo_{size}.png')
        if not os.path.isfile(path):
            return None
        with open(path, 'rb') as handle:
            png_data.append(handle.read())
    header = struct.pack('<HHH', 0, 1, len(png_data))
    entries = []
    offset = 6 + len(png_data) * 16
    for data in png_data:
        width, height = struct.unpack('>II', data[16:24])
        entries.append(struct.pack(
            '<BBBBHHII',
            0 if width >= 256 else width,
            0 if height >= 256 else height,
            0, 0, 1, 32, len(data), offset))
        offset += len(data)
    return header + b''.join(entries) + b''.join(png_data)


def apply_branding(platform):
    """Overlay Maho product name + icons into the chromium theme dir.

    is_chrome_branded stays false, so Chromium reads its product name (BRANDING)
    and app icons (product_logo_*, win/chromium.ico) from
    chrome/app/theme/chromium/. Copy Maho's assets over that dir so the built
    browser identifies as Maho with Maho icons. Runs before gn gen; idempotent.
    """
    branding_dir = os.path.join(_MAHO_CHROMIUM_DIR, 'branding')
    theme_dir = os.path.join(
        _CHROMIUM_SRC_ROOT, 'chrome', 'app', 'theme', 'chromium')
    if not os.path.isdir(branding_dir):
        raise RuntimeError(f'Maho branding directory is missing: {branding_dir}')
    if not os.path.isdir(theme_dir):
        raise RuntimeError(f'Chromium theme directory is missing: {theme_dir}')
    print('Applying Maho branding (product name + icons)...')
    applied = 0
    branding_file = os.path.join(branding_dir, 'BRANDING')
    if not os.path.isfile(branding_file):
        raise RuntimeError(f'Maho BRANDING file is missing: {branding_file}')
    shutil.copyfile(branding_file, os.path.join(theme_dir, 'BRANDING'))
    applied += 1
    for name in sorted(os.listdir(branding_dir)):
        if name.startswith('product_logo') and name.endswith(('.png', '.svg')):
            shutil.copyfile(
                os.path.join(branding_dir, name), os.path.join(theme_dir, name))
            applied += 1
    if platform == 'win':
        win_dir = os.path.join(theme_dir, 'win')
        ico_bytes = _build_windows_ico(branding_dir)
        if ico_bytes is None:
            raise RuntimeError('Maho Windows icon source PNGs are incomplete')
        ico_paths = [
            os.path.join(win_dir, 'chromium.ico'),
            os.path.join(
                _CHROMIUM_SRC_ROOT,
                'chrome',
                'installer',
                'mini_installer',
                'mini_installer.ico',
            ),
            os.path.join(
                _CHROMIUM_SRC_ROOT,
                'chrome',
                'installer',
                'setup',
                'setup.ico',
            ),
        ]
        for ico_path in ico_paths:
            ico_dir = os.path.dirname(ico_path)
            if not os.path.isdir(ico_dir):
                raise RuntimeError(f'Windows icon directory is missing: {ico_dir}')
            with open(ico_path, 'wb') as handle:
                handle.write(ico_bytes)
            applied += 1
        tiles_dir = os.path.join(win_dir, 'tiles')
        if not os.path.isdir(tiles_dir):
            raise RuntimeError(f'Windows tiles directory is missing: {tiles_dir}')
        for source_name, destination_name in (
            ('product_logo_256.png', 'Logo.png'),
            ('product_logo_48.png', 'SmallLogo.png'),
        ):
            source = os.path.join(branding_dir, source_name)
            if not os.path.isfile(source):
                raise RuntimeError(f'Maho Windows tile source is missing: {source}')
            shutil.copyfile(source, os.path.join(tiles_dir, destination_name))
            applied += 1
    if platform == 'mac':
        # chrome/BUILD.gn bundles app/theme/$branding_path_component/mac/{app.icns,
        # Assets.car} as the app icon. macOS prefers the asset-catalog AppIcon in
        # Assets.car over app.icns, so BOTH must be replaced — copying app.icns
        # alone leaves the Chromium icon rendering in the Dock.
        mac_theme_dir = os.path.join(theme_dir, 'mac')
        for name in ('app.icns', 'Assets.car'):
            source = os.path.join(branding_dir, 'mac', name)
            if not os.path.isfile(source):
                raise RuntimeError(f'Maho macOS branding asset is missing: {source}')
            shutil.copyfile(source, os.path.join(mac_theme_dir, name))
            applied += 1
    print(f'  Applied {applied} branding files to {theme_dir}')


_LUCIDE_GENERATE_SCRIPT = os.path.join(_SCRIPT_DIR, 'lucide_icons', 'generate.py')


def run_lucide_icon_check(skip):
    """Run the Lucide icon consistency check (generate.py --check).

    Ensures .icon files match the manifest. Aborts build on drift.
    """
    if skip:
        print('warning: Lucide icon check skipped (--skip-lucide-check)')
        return

    if not os.path.isfile(_LUCIDE_GENERATE_SCRIPT):
        print('warning: Lucide icon generator not found, skipping check')
        return

    print('Checking Lucide icon consistency...')
    result = subprocess.run(
        [sys.executable, _LUCIDE_GENERATE_SCRIPT, '--check'],
        cwd=_WORKSPACE_ROOT,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print('error: Lucide icon files have drifted from manifest.', file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        print(
            '\nTo fix: run `python3 maho-chromium/build/scripts/lucide_icons/generate.py --write`',
            file=sys.stderr,
        )
        sys.exit(1)
    print('  Lucide icons OK.')


def verify_remote_sccache(skip: bool = False) -> None:
    """Verify that sccache is running and backed by the remote shared S3 cache.

    Fails closed: if sccache is not installed, the daemon is not running,
    the cache location is not remote S3, or the remote endpoint is unreachable,
    aborts the build immediately with a clear error and resolution hints.
    """
    if skip:
        print('warning: remote sccache check skipped (--skip-sccache-check)')
        return

    sccache_bin = shutil.which('sccache') or (
        '/opt/homebrew/bin/sccache' if os.path.exists('/opt/homebrew/bin/sccache') else None
    )
    if not sccache_bin:
        sys.exit(
            'error: remote sccache verification failed: sccache binary not found on PATH '
            'or /opt/homebrew/bin/sccache. Install sccache to build.'
        )

    try:
        proc = subprocess.run(
            [sccache_bin, '--show-stats'],
            capture_output=True,
            text=True,
            timeout=10,
            check=True,
        )
        stats_output = proc.stdout if isinstance(proc.stdout, str) else ''
    except Exception as e:
        sys.exit(
            f'error: remote sccache verification failed: cannot query sccache daemon ({e}).\n'
            'Start or restart the sccache broker via launchctl:\n'
            '  launchctl kickstart -k gui/$(id -u)/com.maho.sccache'
        )

    cache_location_match = re.search(r'Cache location\s+(.+)', stats_output, re.IGNORECASE)
    if not cache_location_match:
        sys.exit(
            'error: remote sccache verification failed: "Cache location" not found in sccache stats.\n'
            f'sccache output:\n{stats_output}'
        )

    location_str = cache_location_match.group(1).strip()
    if not location_str.lower().startswith('s3'):
        sys.exit(
            f'error: remote sccache verification failed: cache location is not S3 ({location_str!r}).\n'
            'A stray local server may have started on disk cache. Stop servers and restart the broker:\n'
            '  sccache --stop-server\n'
            '  launchctl kickstart -k gui/$(id -u)/com.maho.sccache'
        )

    endpoint = os.environ.get('SCCACHE_ENDPOINT', '100.126.171.58:9000')
    if ':' in endpoint:
        endpoint_host, port_str = endpoint.split(':', 1)
        endpoint_port = int(port_str)
    else:
        endpoint_host = endpoint
        endpoint_port = 9000

    import socket
    try:
        sock = socket.create_connection((endpoint_host, endpoint_port), timeout=3.0)
        sock.close()
    except Exception as e:
        sys.exit(
            f'error: remote sccache verification failed: remote MinIO endpoint '
            f'{endpoint_host}:{endpoint_port} is unreachable ({e}).\n'
            'Ensure the remote host is reachable over Tailscale and the MahoMinio service is running.'
        )

    print(f'  Remote sccache OK: {location_str} ({endpoint_host}:{endpoint_port} reachable).')


def main():
    parser = argparse.ArgumentParser(description='Build maho-core and Chromium with one shared lock.')
    parser.add_argument('--target', help='Explicit Rust target triple for the prebuilt build.')
    parser.add_argument('--skip-rust', action='store_true', help='Skip the Rust prebuilt step.')
    parser.add_argument('--skip-chromium', action='store_true', help='Skip the Chromium app build step.')
    parser.add_argument(
        '--skip-lucide-check',
        action='store_true',
        help='Skip the Lucide icon consistency check (emergency bypass).',
    )
    parser.add_argument(
        '--skip-sccache-check',
        action='store_true',
        help='Skip the remote sccache verification check (emergency bypass).',
    )
    parser.add_argument(
        '--platform',
        choices=('mac', 'win', 'linux'),
        default=host_platform(),
        help='Target platform for args.gn template selection and per-file override filtering (default: auto-detected host)',
    )
    parser.add_argument(
        '--out-dir',
        default='out/Default',
        metavar='OUT_DIR',
        help='Build output directory relative to chromium/src (default: out/Default)',
    )
    parser.add_argument(
        '--skip-overlay-mount',
        action='store_true',
        help='Skip ensuring the chromium/src/maho overlay mount exists.',
    )
    parser.add_argument(
        '--force-args-template',
        action='store_true',
        help='Overwrite <out-dir>/args.gn even if it already exists.',
    )
    parser.add_argument(
        '--force-gn-gen',
        action='store_true',
        help='Run gn gen even if <out-dir>/build.ninja already exists.',
    )
    parser.add_argument('--skip-codesign', action='store_true', help='Skip codesigning on macOS.')
    parser.add_argument('--notarize', action='store_true',
                        help='Deprecated compatibility flag; macOS --release builds are notarized by default.')
    parser.add_argument('--no-notarize', action='store_true',
                        help='Skip notarization for a macOS --release build; the artifact '
                             'will be rejected by Gatekeeper (local-only use).')
    parser.add_argument('--dmg', action='store_true', help='Force macOS dmg packaging even without --release.')
    parser.add_argument('--no-dmg', action='store_true', help='Skip macOS dmg packaging even for a --release build.')
    parser.add_argument('--no-launch', action='store_true', help='Do not launch the built app on macOS.')
    parser.add_argument('--codesign-identity', help='Explicit codesigning identity to use.')
    parser.add_argument('--release', action='store_true', help='Build in production release mode.')
    parser.add_argument('--debug', action='store_true', help='Build with debug GN settings.')
    parser.add_argument(
        '--ninja-target',
        action='append',
        default=[],
        help='Repeatable ninja target to build. All targets are passed to a single '
             'autoninja invocation. Cannot be used with legacy remainder args.',
    )
    parser.add_argument('autoninja_args', nargs=argparse.REMAINDER, help='Extra args passed to autoninja after --.')
    args = parser.parse_args()

    extra_args = normalize_autoninja_args(args.autoninja_args)
    if args.ninja_target and extra_args:
        parser.error('cannot specify --ninja-target alongside legacy remainder args')

    if args.skip_rust and args.skip_chromium:
        parser.error('nothing to do; remove one of --skip-rust or --skip-chromium')
    if args.release and args.debug:
        parser.error('cannot specify both --release and --debug')

    platform = args.platform
    # Route the default out dir through the build queue: the daemon assigns
    # out/Default for full builds or a narrow out dir for single-target builds,
    # bounding concurrency. An explicit --out-dir opts out (legacy global lock).
    use_queue = args.out_dir == 'out/Default'
    build_class = classify_targets(args.ninja_target) if use_queue else None
    with build_lock(build_class) as assigned_out_dir:
        out_dir = assigned_out_dir or args.out_dir
        verify_remote_sccache(args.skip_sccache_check)
        run_lucide_icon_check(args.skip_lucide_check)
        ensure_webui_dependencies(_WORKSPACE_ROOT)
        # Narrow builds link the existing Rust prebuilt; only full builds rebuild it.
        if not args.skip_rust and build_class != 'narrow':
            build_maho_core.build_rust_prebuilt(args.target, incremental=not args.release)
        if not args.skip_chromium:
            ensure_overlay_mount(_CHROMIUM_SRC_ROOT, args.skip_overlay_mount)
            ensure_args_gn(
                _CHROMIUM_SRC_ROOT,
                out_dir,
                platform,
                args.force_args_template,
                args.release,
                args.debug,
            )
            args_gn_file = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'args.gn')
            cc_wrapper_synced = sync_cc_wrapper(args_gn_file, platform)
            media_synced = sync_media_codec_flags(args_gn_file)
            if platform == 'mac':
                ensure_macos_sdk_path(args_gn_file)
            key_synced = sync_local_google_api_key(
                _WORKSPACE_ROOT,
                args_gn_file,
                args.release,
            )
            apply_tracked_chromium_overrides(platform)
            apply_branding(platform)
            build_ninja_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'build.ninja')
            if (not os.path.isfile(build_ninja_path)
                    or args.force_gn_gen or args.release or key_synced or cc_wrapper_synced or media_synced):
                run_gn_gen(_CHROMIUM_SRC_ROOT, out_dir, _DEPOT_TOOLS_DIR)

            targets = list(args.ninja_target) if args.ninja_target else ['chrome']
            if not args.ninja_target and 'chrome' in targets:
                if platform in ('mac', 'win') and 'maho_mail_helper' not in targets:
                    targets.append('maho_mail_helper')
                # A release build IS the bundle: the Windows installer artifact
                # is the mini_installer target, so build it in the same pass.
                if (platform == 'win' and (args.release or args.dmg)
                        and 'mini_installer' not in targets):
                    targets.append('mini_installer')
            maho_cli_path = None
            if 'chrome' in targets and platform in ('mac', 'win', 'linux'):
                maho_cli_path = build_maho_cli(args.target)
            # Windows: stage (and sign) the CLI into the output dir BEFORE the
            # build so the mini_installer target packages it alongside the
            # browser (chrome.release lists maho.exe, so it must exist here
            # before that edge runs).
            if platform == 'win' and maho_cli_path:
                staged_win_cli = stage_maho_cli_next_to_browser(out_dir, maho_cli_path)
                if staged_win_cli is None:
                    raise RuntimeError('failed to stage maho CLI for Windows')
                if not args.skip_codesign:
                    sign_windows_binary(staged_win_cli)
            build_chromium_app(out_dir, targets, extra_args)

            if platform == 'win' and 'chrome' in targets:
                require_windows_mail_helper(out_dir)

            app_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'Maho.app')
            if 'chrome' in targets:
                if platform == 'mac':
                    embed_sparkle_framework(app_path)
                    embed_mail_helper(app_path, out_dir)
                    # A skipped stage leaves whatever omo tree an earlier
                    # build left in the bundle, which is how a runtime that
                    # rejects --listen shipped; fail instead.
                    if not embed_omo_runtime(app_path, out_dir):
                        raise RuntimeError('failed to embed omo runtime into app bundle')
                    if maho_cli_path is None or not embed_maho_cli(
                            app_path, maho_cli_path):
                        raise RuntimeError('failed to embed maho CLI into app bundle')
                    set_bundle_product_version(app_path, read_maho_version())
                if platform == 'mac' and not args.skip_codesign:
                    identity = args.codesign_identity
                    if not identity:
                        if args.release:
                            identity = find_codesign_identity(True)
                        else:
                            # Debug builds carry the same restricted
                            # entitlements as release (application-identifier,
                            # keychain-access-groups). A development
                            # certificate cannot satisfy them without a
                            # matching development profile, and AMFI then
                            # SIGKILLs the browser at launch. Sign with Apple
                            # Distribution plus the App Store provisioning
                            # profile instead: the profile authorizes the
                            # entitlements, and the certificate's team-based
                            # designated requirement stays identical across
                            # builds so macOS TCC grants (removable-volume
                            # access etc.) persist instead of re-prompting
                            # after every rebuild.
                            identity = find_codesign_identity_release_dist()
                            profile = find_appstore_provisioning_profile(
                                'com.maho.browser')
                            if identity and profile:
                                os.environ['MAHO_MAC_PROVISIONING_PROFILE'] = (
                                    profile)
                            else:
                                identity = None
                                print(
                                    'warning: no Apple Distribution identity '
                                    'or App Store profile for com.maho.browser '
                                    '— falling back to ad-hoc signing without '
                                    'entitlements', file=sys.stderr)
                    if identity:
                        if codesign_app(identity, app_path):
                            # A release build IS the bundle: package the signed
                            # app into a dmg unless explicitly opted out.
                            want_dmg = (args.release or args.dmg) and not args.no_dmg
                            # macOS --release defaults to notarizing app and
                            # dmg: a Developer ID-signed but unnotarized
                            # artifact is rejected by Gatekeeper on user
                            # machines, and the only path that ever shipped a
                            # good release (release_local.py) always passes
                            # --notarize. Explicit --no-notarize opts out for
                            # local-only artifacts.
                            do_notarize = (platform == 'mac'
                                           and not args.no_notarize
                                           and (args.release or args.notarize))
                            if do_notarize:
                                # When notarizing, notarize and staple the app
                                # bundle FIRST so the ticket is baked into the
                                # staged app before it is imaged into the read-only DMG.
                                if not notarize_app(app_path):
                                    print("error: app notarization/stapling failed; aborting",
                                          file=sys.stderr)
                                    sys.exit(1)
                            elif args.release and platform == 'mac':
                                print(
                                    "warning: --release on macOS explicitly opted out of "
                                    "notarization (--no-notarize). The resulting app/dmg "
                                    "is not notarized and will fail Gatekeeper on user "
                                    "machines!",
                                    file=sys.stderr,
                                )
                            if want_dmg:
                                version = read_maho_version()
                                dmg_path = build_dmg(app_path, out_dir, version, identity)
                                # Fail closed: a failed packaging/verification
                                # gate must abort the whole release with a
                                # nonzero exit so release_local.py (which runs
                                # this via check=True) never uploads a bad dmg.
                                if dmg_path is None:
                                    print("error: dmg packaging/verification failed; aborting",
                                          file=sys.stderr)
                                    sys.exit(1)
                                if do_notarize and not notarize_dmg(dmg_path):
                                    print("error: dmg notarization/stapling failed; aborting",
                                          file=sys.stderr)
                                    sys.exit(1)
                    else:
                        if platform == 'mac':
                            embedded_profile = os.path.join(
                                app_path, 'Contents', 'embedded.provisionprofile')
                            if os.path.isfile(embedded_profile):
                                try:
                                    os.remove(embedded_profile)
                                except OSError:
                                    pass
                            # Ad-hoc fallback with a STABLE designated
                            # requirement. TCC keys permission grants on the
                            # DR; the default ad-hoc DR embeds the cdhash,
                            # which changes on every build and re-triggers
                            # "Maho wants to access..." prompts. Pinning the
                            # DR to the bundle identifier alone keeps grants
                            # valid across rebuilds.
                            subprocess.run(
                                [
                                    'codesign', '--force', '--deep', '--sign',
                                    '-', '--identifier', 'com.maho.browser',
                                    '-r=designated => identifier "com.maho.browser"',
                                    # Permissive hardened-runtime exceptions
                                    # (allow-jit etc.) are required — V8 needs
                                    # JIT on Apple Silicon and the WebUIs break
                                    # without them. This plist must stay free
                                    # of restricted entitlements
                                    # (application-identifier,
                                    # keychain-access-groups): ad-hoc signing
                                    # cannot satisfy those and AMFI then
                                    # SIGKILLs the browser at launch.
                                    '--entitlements',
                                    os.path.join(
                                        _MAHO_CHROMIUM_DIR, 'branding', 'mac',
                                        'helper-entitlements.plist'),
                                    app_path,
                                ],
                                check=True,
                            )
                            print(
                                'Signed app bundle ad-hoc with stable '
                                'designated requirement')
                        else:
                            print(
                                'warning: no valid code signing identity '
                                'found; built app will not be signed',
                                file=sys.stderr)

                if platform == 'linux' and maho_cli_path:
                    if stage_maho_cli_next_to_browser(out_dir, maho_cli_path) is None:
                        raise RuntimeError('failed to stage maho CLI for Linux')

                if not args.no_launch:
                    launch_app(app_path)

            if 'mini_installer' in targets and platform == 'win':
                want_pkg = (args.release or args.dmg) and not args.no_dmg
                if want_pkg:
                    version = read_maho_version()
                    setup_path = build_windows_installer(out_dir, version)
                    if setup_path:
                        sign_windows_installer(setup_path)


if __name__ == '__main__':
    # print the machine-checkable result sentinel so callers that pipe the
    # output (e.g. `python3 build_maho.py | tee log`, which reports tee's
    # exit status) can still verify the real outcome from the log tail.
    try:
        main()
        print('MAHO BUILD RESULT: SUCCESS')
    except SystemExit as e:
        if e.code not in (None, 0):
            print('MAHO BUILD RESULT: FAILED', file=sys.stderr)
        raise
    sys.exit(0)
