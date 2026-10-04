#!/usr/bin/env python3
"""Stage a patched macOS SDK so Chromium builds against Xcode 27's SDK.

Why this exists
---------------
The pinned Chromium revision's clang-module maps predate Xcode 27's SDK, and
SDK 27's `usr/include/math.h` pulls in `<float.h>` through a `__need_*` protocol
that no longer terminates once clang modules are disabled:

  * modules ON  -> `module 'std_float_h' is needed but has not been provided`
  * modules OFF -> `<cfloat>` resolves to clang's own float.h shim, the partial
                   `__need_infinity_nan` include poisons it, and `FLT_EPSILON`
                   / `INFINITY` end up undefined (protobuf fails first).

Fixing the module maps in-tree is not possible for this revision (see
`docs/operations/macos-xcode27-sdk-build.md`), so the working configuration is
`use_clang_modules = false` **plus** a staged SDK whose `math.h` skips that
partial include and defines `INFINITY` directly.

This script produces exactly that staged SDK, reproducibly, without ever
modifying the SDK inside Xcode.app: it copies the source SDK to a stable
location and patches the copy.

Usage
-----
    python3 stage_macos_sdk.py                 # stage into ~/SDKs/MacOSX.sdk
    python3 stage_macos_sdk.py --check         # verify an existing staged SDK
    python3 stage_macos_sdk.py --force         # re-stage from scratch

Then point the machine-local args.gn at the staged copy:

    mac_sdk_path = "/Users/<you>/SDKs/MacOSX.sdk"

`use_clang_modules = false` already ships in `maho-chromium/build/config/args_mac*.gn`.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

# The two edits that make SDK 27's math.h usable with clang modules disabled.
# Each entry is (description, before, after); `before` must appear exactly once.
PATCHES = (
    (
        'skip the partial <float.h> include',
        '#   if defined(__has_feature) && __has_feature(modules) && '
        'defined(__has_include) && __has_include(<float.h>)',
        '#   if 0 // disabled: prevents partial float.h inclusion from poisoning '
        'libc++ float.h',
    ),
    (
        'define INFINITY directly (the skipped include used to provide it)',
        '#       define    NAN          __builtin_nanf("0x7fc00000")',
        '#       define    NAN          __builtin_nanf("0x7fc00000")\n'
        '#       define    INFINITY     __builtin_inff()',
    ),
)

DEFAULT_DEST = os.path.expanduser('~/SDKs/MacOSX.sdk')
_XCODE_SDK_DIRS = (
    '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/'
    'Developer/SDKs',
    '/Library/Developer/CommandLineTools/SDKs',
)


def find_source_sdk() -> str:
    """Newest MacOSX<major>.sdk, Xcode platform SDK preferred over CommandLineTools."""
    candidates: list[tuple[int, str]] = []
    for root in _XCODE_SDK_DIRS:
        if not os.path.isdir(root):
            continue
        for name in os.listdir(root):
            if not name.startswith('MacOSX') or not name.endswith('.sdk'):
                continue
            middle = name[len('MacOSX'):-len('.sdk')]
            major = middle.split('.')[0]
            if not major.isdigit():
                continue
            path = os.path.join(root, name)
            if os.path.isdir(path):
                candidates.append((int(major), path))
    if not candidates:
        sys.exit('error: no MacOSX*.sdk found under Xcode or CommandLineTools')
    candidates.sort()
    return candidates[-1][1]


def math_h_path(sdk: str) -> str:
    return os.path.join(sdk, 'usr', 'include', 'math.h')


def check(sdk: str) -> int:
    """0 when `sdk` is staged+patched, 1 otherwise; never exits."""
    path = math_h_path(sdk)
    if not os.path.isfile(path):
        print(f'error: not an SDK (no usr/include/math.h): {sdk}')
        return 1
    with open(path, encoding='utf-8', errors='replace') as handle:
        text = handle.read()
    missing = [desc for desc, _, after in PATCHES if after not in text]
    if missing:
        print(f'not staged: {sdk}')
        for desc in missing:
            print(f'  missing patch: {desc}')
        return 1
    print(f'staged and patched: {sdk}')
    return 0


def stage(source: str, dest: str, force: bool) -> str:
    """Stage `source` at `dest`, returning the staged SDK path (exits on failure)."""
    if os.path.abspath(source) == os.path.abspath(dest):
        sys.exit(f'error: source and destination are the same SDK: {source}')

    if os.path.isdir(dest):
        if check(dest) == 0 and not force:
            print('already staged; nothing to do (pass --force to re-stage)')
            return dest
        print(f'removing stale staging dir: {dest}')
        shutil.rmtree(dest)

    os.makedirs(os.path.dirname(dest), exist_ok=True)
    print(f'copying SDK {source} -> {dest}')
    # ditto preserves the SDK's symlinks, xattrs, and permissions exactly.
    if shutil.which('ditto'):
        subprocess.run(['ditto', source, dest], check=True)
    else:
        shutil.copytree(source, dest, symlinks=True)

    path = math_h_path(dest)
    if not os.path.isfile(path):
        sys.exit(f'error: staged SDK has no usr/include/math.h: {path}')
    with open(path, encoding='utf-8') as handle:
        text = handle.read()

    for desc, before, after in PATCHES:
        occurrences = text.count(before)
        if occurrences != 1:
            sys.exit(
                f'error: anchor for "{desc}" matched {occurrences} times '
                f'(expected exactly 1) in {path}. The SDK layout changed — '
                're-derive the patch before continuing.'
            )
        text = text.replace(before, after)
        print(f'  patched: {desc}')

    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(text)

    if check(dest) != 0:
        sys.exit(f'error: staging finished but {dest} does not verify')
    print(f'\nstaged SDK ready: {dest}')
    print(f'set in args.gn:  mac_sdk_path = "{dest}"')
    return dest


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument('--source', default=None,
                        help='source SDK to stage (default: newest installed)')
    parser.add_argument('--dest', default=DEFAULT_DEST,
                        help=f'staging destination (default: {DEFAULT_DEST})')
    parser.add_argument('--check', action='store_true',
                        help='only verify an existing staged SDK')
    parser.add_argument('--force', action='store_true',
                        help='re-stage even when the destination already verifies')
    args = parser.parse_args()

    if args.check:
        return check(args.dest)
    source = args.source or find_source_sdk()
    stage(source, args.dest, args.force)
    return 0


if __name__ == '__main__':
    sys.exit(main())
