#!/usr/bin/env python3
"""Build maho-core Rust static library and copy to prebuilt directory."""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import importlib.util

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_MAHO_SCRIPTS_DIR = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..', 'maho', 'scripts'))


def load_build_lock():
    module_path = os.path.join(_MAHO_SCRIPTS_DIR, 'build_lock.py')
    spec = importlib.util.spec_from_file_location('maho_build_lock', module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f'Unable to load build lock helper: {module_path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build_lock


build_lock = load_build_lock()

WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
MAHO_ROOT = os.path.join(WORKSPACE_ROOT, 'maho')
MAHO_CHROMIUM_ROOT = os.path.join(WORKSPACE_ROOT, 'maho-chromium')
PREBUILT_DIR = os.path.join(MAHO_CHROMIUM_ROOT, 'third_party', 'maho', 'prebuilt')


def get_target_triple(target=None):
    machine = platform.machine().lower()
    if machine in ('arm64', 'aarch64'):
        arch = 'aarch64'
        cpu_dir = 'arm64'
    elif machine in ('x86_64', 'amd64'):
        arch = 'x86_64'
        cpu_dir = 'x64'
    else:
        sys.exit(f'Unsupported architecture: {machine}')

    if target:
        if target.startswith(('x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc')):
            return target, 'x64' if target.startswith('x86_64-') else 'arm64', 'maho_ffi.lib'
        if target.startswith(('x86_64-unknown-linux-gnu', 'aarch64-unknown-linux-gnu')):
            return target, 'x64' if target.startswith('x86_64-') else 'arm64', 'libmaho_ffi.a'
        if target.startswith(('x86_64-apple-darwin', 'aarch64-apple-darwin')):
            return target, 'x64' if target.startswith('x86_64-') else 'arm64', 'libmaho_ffi.a'
        sys.exit(f'Unsupported target triple: {target}')

    if sys.platform == 'darwin':
        return f'{arch}-apple-darwin', cpu_dir, 'libmaho_ffi.a'
    if sys.platform == 'win32':
        return f'{arch}-pc-windows-msvc', cpu_dir, 'maho_ffi.lib'
    if sys.platform.startswith('linux'):
        return f'{arch}-unknown-linux-gnu', cpu_dir, 'libmaho_ffi.a'

    sys.exit(f'Unsupported platform: {sys.platform}')


def build_stt_ffi_prebuilt(rust_target, cpu_dir):
    """Build the standalone maho-stt-ffi static lib and copy it to prebuilt/.

    maho-stt-ffi is its own cargo workspace (not a member of maho/), so it is
    built from its crate dir rather than via `-p` from MAHO_ROOT.
    """
    lib_name = 'maho_stt_ffi.lib' if sys.platform == 'win32' else 'libmaho_stt_ffi.a'
    crate_dir = os.path.join(MAHO_ROOT, 'crates', 'maho-stt-ffi')
    output_dir = os.path.join(PREBUILT_DIR, cpu_dir)
    os.makedirs(output_dir, exist_ok=True)

    print(f'Building maho-stt-ffi for {rust_target}...')
    subprocess.run(
        ['cargo', 'build', '--release', '--target', rust_target],
        cwd=crate_dir,
        check=True,
    )

    src_lib = os.path.join(crate_dir, 'target', rust_target, 'release', lib_name)
    dst_lib = os.path.join(output_dir, lib_name)
    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')
    shutil.copy2(src_lib, dst_lib)
    if sys.platform == 'darwin':
        subprocess.run(['ranlib', '-c', dst_lib], check=True)
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')


def build_rust_prebuilt(target=None):
    rust_target, cpu_dir, lib_name = get_target_triple(target)
    output_dir = os.path.join(PREBUILT_DIR, cpu_dir)
    os.makedirs(output_dir, exist_ok=True)

    print(f'Building maho-core for {rust_target}...')
    subprocess.run(
        ['cargo', 'build', '--release', '--target', rust_target, '-p', 'maho-ffi'],
        cwd=MAHO_ROOT,
        check=True,
    )

    src_lib = os.path.join(MAHO_ROOT, 'target', rust_target, 'release', lib_name)
    dst_lib = os.path.join(output_dir, lib_name)

    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')

    shutil.copy2(src_lib, dst_lib)
    if sys.platform == 'darwin':
        subprocess.run(['ranlib', '-c', dst_lib], check=True)
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')

    build_stt_ffi_prebuilt(rust_target, cpu_dir)

    # Run cbindgen to update the FFI header
    print('Generating maho_ffi.h using cbindgen...')
    header_path = os.path.normpath(os.path.join(MAHO_CHROMIUM_ROOT, 'third_party', 'maho', 'maho_ffi.h'))

    cargo_target_dir = os.environ.get('CARGO_TARGET_DIR')
    if cargo_target_dir:
        cbindgen_dir = os.path.join(cargo_target_dir, '.maho-cbindgen')
    else:
        cbindgen_dir = os.path.join(MAHO_ROOT, 'target', '.maho-cbindgen')

    os.makedirs(cbindgen_dir, exist_ok=True)
    temp_header_path = os.path.join(cbindgen_dir, 'maho_ffi.h.tmp')

    cbindgen_config = os.path.join(MAHO_ROOT, 'crates', 'maho-ffi', 'cbindgen.toml')
    subprocess.run(
        ['cbindgen', '--config', cbindgen_config, '--crate', 'maho-ffi', '--output', temp_header_path],
        cwd=os.path.join(MAHO_ROOT, 'crates', 'maho-ffi'),
        check=True,
    )

    new_content = b''
    if os.path.exists(temp_header_path):
        with open(temp_header_path, 'rb') as f:
            new_content = f.read()

    old_content = b''
    if os.path.exists(header_path):
        with open(header_path, 'rb') as f:
            old_content = f.read()

    if new_content != old_content:
        active_task = os.environ.get('ACTIVE_TASK') or os.environ.get('TASK_ID')

        # Dynamically find the active attempt directory (highest numbered folder)
        evidence_root = os.path.join(WORKSPACE_ROOT, '.omo', 'evidence', 'arc-parity-incognito-desktop')
        attempt_dir = None
        if os.path.exists(evidence_root):
            attempts = [d for d in os.listdir(evidence_root) if d.startswith('attempt-') and os.path.isdir(os.path.join(evidence_root, d))]
            if attempts:
                attempts.sort()
                attempt_dir = os.path.join(evidence_root, attempts[-1])

        tsv_path = os.path.join(attempt_dir, 'preimage', 'owned-product-paths.tsv') if attempt_dir else None
        rel_header_path = os.path.relpath(header_path, WORKSPACE_ROOT)

        owned = False
        if active_task and tsv_path and os.path.exists(tsv_path):
            with open(tsv_path, 'r') as f:
                for line in f:
                    parts = line.strip().split('|')
                    if len(parts) >= 3:
                        task_id, mode, path = parts[0], parts[1], parts[2]
                        if task_id == str(active_task) and path == rel_header_path:
                            owned = True
                            break

        if owned:
            try:
                os.replace(temp_header_path, header_path)
            except OSError:
                shutil.copy2(temp_header_path, header_path)
                os.remove(temp_header_path)
            print(f'Replaced tracked header: {header_path}')
        else:
            print(f'Active task {active_task} does not own {rel_header_path}. Skip replace.')
            if os.path.exists(temp_header_path):
                os.remove(temp_header_path)
    else:
        print('Header content unchanged. Skip replace.')
        if os.path.exists(temp_header_path):
            os.remove(temp_header_path)


def main():
    parser = argparse.ArgumentParser(description='Build maho-core Rust static library.')
    parser.add_argument('--target', help='Explicit Rust target triple for cross-compilation.')
    args = parser.parse_args()

    with build_lock():
        build_rust_prebuilt(args.target)


if __name__ == '__main__':
    main()
