#!/usr/bin/env python3
"""Build the Rust prebuilt and Chromium app under one shared lock."""

import argparse
import importlib.util
import os
import subprocess

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_MAHO_SCRIPTS_DIR = os.path.join(_WORKSPACE_ROOT, 'maho', 'scripts')
_CHROMIUM_SRC_ROOT = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_DEPOT_TOOLS_DIR = os.path.join(_CHROMIUM_SRC_ROOT, 'third_party', 'depot_tools')


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


def build_chromium_app(extra_args):
    env = os.environ.copy()
    path = env.get('PATH', '')
    env['PATH'] = os.pathsep.join([_DEPOT_TOOLS_DIR, path]) if path else _DEPOT_TOOLS_DIR
    command = ['autoninja', '-C', 'out/Default', 'chrome', *extra_args]
    print('Building Chromium app with autoninja...')
    subprocess.run(command, cwd=_CHROMIUM_SRC_ROOT, check=True, env=env)


def main():
    parser = argparse.ArgumentParser(description='Build maho-core and Chromium with one shared lock.')
    parser.add_argument('--target', help='Explicit Rust target triple for the prebuilt build.')
    parser.add_argument('--skip-rust', action='store_true', help='Skip the Rust prebuilt step.')
    parser.add_argument('--skip-chromium', action='store_true', help='Skip the Chromium app build step.')
    parser.add_argument('autoninja_args', nargs=argparse.REMAINDER, help='Extra args passed to autoninja after --.')
    args = parser.parse_args()

    extra_args = normalize_autoninja_args(args.autoninja_args)
    if args.skip_rust and args.skip_chromium:
        parser.error('nothing to do; remove one of --skip-rust or --skip-chromium')

    with build_lock():
        if not args.skip_rust:
            build_maho_core.build_rust_prebuilt(args.target)
        if not args.skip_chromium:
            build_chromium_app(extra_args)


if __name__ == '__main__':
    main()
