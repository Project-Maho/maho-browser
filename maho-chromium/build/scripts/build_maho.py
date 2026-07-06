#!/usr/bin/env python3
"""Build the Rust prebuilt and Chromium app under one shared lock."""

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_MAHO_CHROMIUM_DIR = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..'))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_MAHO_SCRIPTS_DIR = os.path.join(_WORKSPACE_ROOT, 'maho', 'scripts')
_CHROMIUM_SRC_ROOT = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_DEPOT_TOOLS_DIR = os.path.join(_CHROMIUM_SRC_ROOT, 'third_party', 'depot_tools')
_APPLY_OVERRIDES_SCRIPT = os.path.join(_SCRIPT_DIR, 'apply_chromium_src_overrides.py')
_BUILD_CONFIG_DIR = os.path.join(_MAHO_CHROMIUM_DIR, 'build', 'config')


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


def ensure_args_gn(chromium_src, out_dir, platform, force):
    args_gn_path = os.path.join(chromium_src, out_dir, 'args.gn')
    template_path = os.path.join(_BUILD_CONFIG_DIR, f'args_{platform}.gn')

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
            ensure_args_gn(_CHROMIUM_SRC_ROOT, out_dir, platform, args.force_args_template)
            apply_tracked_chromium_overrides(platform)
            build_ninja_path = os.path.join(_CHROMIUM_SRC_ROOT, out_dir, 'build.ninja')
            if not os.path.isfile(build_ninja_path) or args.force_gn_gen:
                run_gn_gen(_CHROMIUM_SRC_ROOT, out_dir, _DEPOT_TOOLS_DIR)
            build_chromium_app(out_dir, extra_args)


if __name__ == '__main__':
    main()
