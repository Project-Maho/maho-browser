#!/usr/bin/env python3
"""Build the Rust prebuilt and Chromium app under one shared lock."""

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
from subprocess import SubprocessError

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_MAHO_CHROMIUM_DIR = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..'))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_MAHO_SCRIPTS_DIR = os.path.join(_WORKSPACE_ROOT, 'maho', 'scripts')
_CHROMIUM_SRC_ROOT = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_DEPOT_TOOLS_DIR = os.path.join(_CHROMIUM_SRC_ROOT, 'third_party', 'depot_tools')
_APPLY_OVERRIDES_SCRIPT = os.path.join(_SCRIPT_DIR, 'apply_chromium_src_overrides.py')
_BUILD_CONFIG_DIR = os.path.join(_MAHO_CHROMIUM_DIR, 'build', 'config')
_MAHO_APP_PATH = os.path.join(_CHROMIUM_SRC_ROOT, 'out', 'Default', 'Maho.app')


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


build_lock = load_module('maho_build_lock', os.path.join(_MAHO_SCRIPTS_DIR, 'build_lock.py')).build_lock
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


def find_codesign_identity() -> str | None:
    try:
        res = subprocess.run(
            ['security', 'find-identity', '-v', '-p', 'codesigning'],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if res.returncode != 0:
            return None
        sha_re = re.compile(r'^\s*\d+\)\s+([a-fA-F0-9]{40})\s+"')
        for line in res.stdout.splitlines():
            m = sha_re.match(line)
            if m:
                return m.group(1).upper()
        return None
    except Exception:
        return None


def codesign_app(identity: str, app_path: str) -> bool:
    if not os.path.isdir(app_path):
        print(f"error: app bundle not found: {app_path}", file=sys.stderr)
        return False
    try:
        frameworks_dir = os.path.join(app_path, 'Contents', 'Frameworks')
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

        entitlements_path = os.path.join(_MAHO_CHROMIUM_DIR, 'branding', 'mac', 'entitlements.plist')
        cmd = ['codesign', '--force', '--sign', identity, '--options', 'runtime']
        if os.path.isfile(entitlements_path):
            cmd += ['--entitlements', entitlements_path]
        cmd += [app_path]
        
        print(f"Signing app bundle: {app_path}")
        subprocess.run(cmd, check=True, timeout=120)
        return True
    except (SubprocessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"codesign failed: {e}", file=sys.stderr)
        return False


def notarize_app(app_path: str) -> bool:
    import tempfile
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
            
        print("Submitting to Apple Notary Service...")
        cmd = [
            'xcrun', 'notarytool', 'submit', zip_path,
            '--key-id', key_id,
            '--issuer', issuer_id,
            '--key', key_path,
            '--wait'
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        print(res.stdout)
        if res.returncode != 0:
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


def launch_app(app_path: str) -> None:
    if sys.platform != 'darwin':
        return
    if not os.path.isdir(app_path):
        print(f"warning: app bundle not found: {app_path}; skipping launch", file=sys.stderr)
        return
    print(f"Launching app: {app_path}")
    subprocess.Popen(['open', app_path])


def ensure_args_gn(chromium_src, out_dir, platform, force, release=False):
    args_gn_path = os.path.join(chromium_src, out_dir, 'args.gn')
    suffix = '_release' if release else ''
    template_path = os.path.join(_BUILD_CONFIG_DIR, f'args_{platform}{suffix}.gn')

    if not os.path.isfile(template_path):
        print(
            f'warning: args template not found: {template_path}; skipping args.gn bootstrap',
        )
        return

    if os.path.isfile(args_gn_path) and not force:
        print(f'args.gn already exists: {args_gn_path} (use --force-args-template to overwrite)')
        return

    out_dir_abs = os.path.join(chromium_src, out_dir)
    os.makedirs(out_dir_abs, exist_ok=True)
    already_existed = os.path.isfile(args_gn_path)
    shutil.copy2(template_path, args_gn_path)
    action = 'Overwrote' if already_existed else 'Created'
    print(f'{action} args.gn from {template_path}')


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


def build_chromium_app(out_dir, extra_args):
    autoninja_bin = 'autoninja.bat' if host_platform() == 'win' else 'autoninja'
    env = os.environ.copy()
    path = env.get('PATH', '')
    env['PATH'] = os.pathsep.join([_DEPOT_TOOLS_DIR, path]) if path else _DEPOT_TOOLS_DIR
    command = [autoninja_bin, '-C', out_dir, 'chrome', *extra_args]
    print('Building Chromium app with autoninja...')
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
    parser.add_argument('--notarize', action='store_true', help='Notarize the built app on macOS.')
    parser.add_argument('--no-launch', action='store_true', help='Do not launch the built app on macOS.')
    parser.add_argument('--codesign-identity', help='Explicit codesigning identity to use.')
    parser.add_argument('--release', action='store_true', help='Build in production release mode.')
    parser.add_argument('autoninja_args', nargs=argparse.REMAINDER, help='Extra args passed to autoninja after --.')
    args = parser.parse_args()

    extra_args = normalize_autoninja_args(args.autoninja_args)
    if args.skip_rust and args.skip_chromium:
        parser.error('nothing to do; remove one of --skip-rust or --skip-chromium')

    platform = args.platform
    out_dir = args.out_dir
    with build_lock():
        run_lucide_icon_check(args.skip_lucide_check)
        if not args.skip_rust:
            build_maho_core.build_rust_prebuilt(args.target)
        if not args.skip_chromium:
            ensure_overlay_mount(_CHROMIUM_SRC_ROOT, args.skip_overlay_mount)
            ensure_args_gn(_CHROMIUM_SRC_ROOT, out_dir, platform, args.force_args_template, args.release)
            apply_tracked_chromium_overrides(platform)
            build_ninja_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'build.ninja')
            if not os.path.isfile(build_ninja_path) or args.force_gn_gen:
                run_gn_gen(_CHROMIUM_SRC_ROOT, out_dir, _DEPOT_TOOLS_DIR)
            build_chromium_app(out_dir, extra_args)

            app_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'Maho.app')
            if platform == 'mac':
                embed_sparkle_framework(app_path)
            if platform == 'mac' and not args.skip_codesign:
                identity = args.codesign_identity
                if not identity:
                    identity = find_codesign_identity()
                if identity:
                    if codesign_app(identity, app_path):
                        if args.notarize:
                            notarize_app(app_path)
                else:
                    print("warning: no valid code signing identity found; built app will not be signed", file=sys.stderr)
            
            if not args.no_launch:
                launch_app(app_path)


if __name__ == '__main__':
    main()
