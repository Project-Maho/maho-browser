#!/usr/bin/env python3
"""Build maho-core Rust static library and copy to prebuilt directory."""

import argparse
import os
import platform
import shutil
import subprocess
import sys

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
MAHO_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..', 'maho'))
MAHO_CHROMIUM_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..'))
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


def main():
    parser = argparse.ArgumentParser(description='Build maho-core Rust static library.')
    parser.add_argument('--target', help='Explicit Rust target triple for cross-compilation.')
    args = parser.parse_args()

    rust_target, cpu_dir, lib_name = get_target_triple(args.target)
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
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')


if __name__ == '__main__':
    main()
