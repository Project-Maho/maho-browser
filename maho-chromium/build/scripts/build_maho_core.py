#!/usr/bin/env python3
"""Build maho-core Rust static library and copy to prebuilt directory."""

import argparse
import glob
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import importlib.util

_cargo_bin = os.path.expanduser('~/.cargo/bin')
if os.path.isdir(_cargo_bin) and _cargo_bin not in os.environ.get('PATH', ''):
    os.environ['PATH'] = f"{_cargo_bin}{os.pathsep}{os.environ.get('PATH', '')}"

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
if os.path.basename(_WORKSPACE_ROOT) == 'src' and os.path.basename(os.path.dirname(_WORKSPACE_ROOT)) == 'chromium':
    _WORKSPACE_ROOT = os.path.normpath(os.path.join(_WORKSPACE_ROOT, '..', '..'))
_MAHO_SCRIPTS_DIR = os.path.normpath(os.path.join(_WORKSPACE_ROOT, 'maho', 'scripts'))
_DEFAULT_GOOGLE_CLIENT_ID = (
    '650357306759-c6tn05kg1l60ved2ne6cf9jp33moanh0.apps.googleusercontent.com'
)

# Google token endpoints for these clients reject secretless PKCE exchanges with
# 400 invalid_request "client_secret is missing" (verified live 2026-09-15;
# evidence: a1/d6-token-400-root-cause.md). Building the mail FFI against such a
# client without GMAIL_CLIENT_SECRET ships a broken Gmail OAuth, so fail closed.
_WEB_CLIENT_IDS_REQUIRING_SECRET = frozenset({_DEFAULT_GOOGLE_CLIENT_ID})
_GOOGLE_DESKTOP_OAUTH_VAULT = os.path.join(
    '.secrets', 'maho', 'google_desktop_oauth.json')


def _platform_name():
    if sys.platform == 'darwin':
        return 'mac'
    if sys.platform == 'win32':
        return 'windows'
    if sys.platform.startswith('linux'):
        return 'linux'
    return sys.platform


def _vault_google_desktop_oauth_client():
    """Read the owner-supplied Google OAuth client JSON from the vault.

    Checks platform-specific vault files first (e.g.
    .secrets/maho/google_windows_oauth.json, google_mac_oauth.json, etc.),
    then falls back to .secrets/maho/google_desktop_oauth.json.

    Returns (client_id, client_secret), or None when absent/malformed.
    """
    plat = _platform_name()
    candidate_relpaths = [
        os.path.join('.secrets', 'maho', f'google_{plat}_oauth.json'),
        os.path.join('.secrets', 'maho', 'google_desktop_oauth.json'),
    ]
    for relpath in candidate_relpaths:
        path = os.path.join(_WORKSPACE_ROOT, relpath)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, encoding='utf-8') as handle:
                data = json.load(handle)
            entry = data.get('installed') or data.get('web') or {}
            client_id = entry.get('client_id')
            secret = entry.get('client_secret')
            if isinstance(client_id, str) and client_id and isinstance(secret, str) and secret:
                return client_id, secret
        except Exception:
            continue
    return None

# Pin the macOS deployment target for the prebuilt Rust static libs to match
# Chromium's mac_deployment_target (chromium/src/build/config/mac/mac_sdk.gni).
# cc-rs otherwise stamps vendored C objects (sqlcipher / OpenSSL libcrypto) with
# the host SDK version, and the official `chrome` link then rejects them with
# "has version <host>, which is newer than target minimum of 12.0.0". Set as a
# default so an explicit environment override still wins.
if sys.platform == 'darwin':
    os.environ.setdefault('MACOSX_DEPLOYMENT_TARGET', '12.0')


def mail_oauth_build_env():
    cargo_env = os.environ.copy()
    plat = _platform_name().upper()
    google_client_id = (
        cargo_env.get(f'MAHO_GOOGLE_CLIENT_ID_{plat}')
        or cargo_env.get('MAHO_GOOGLE_CLIENT_ID')
        or cargo_env.get('GMAIL_CLIENT_ID')
        or _DEFAULT_GOOGLE_CLIENT_ID
    )
    cargo_env.setdefault('MAHO_GOOGLE_CLIENT_ID', google_client_id)
    cargo_env.setdefault('GMAIL_CLIENT_ID', google_client_id)
    google_client_secret = (
        cargo_env.get(f'MAHO_GOOGLE_CLIENT_SECRET_{plat}')
        or cargo_env.get('MAHO_GOOGLE_CLIENT_SECRET')
        or cargo_env.get('GMAIL_CLIENT_SECRET')
    )
    vault = _vault_google_desktop_oauth_client()
    if not google_client_secret and vault:
        vault_id, vault_secret = vault
        if vault_id == google_client_id:
            google_client_secret = vault_secret
        else:
            raise RuntimeError(
                f'{_GOOGLE_DESKTOP_OAUTH_VAULT} holds client {vault_id} '
                f'but this build bakes {google_client_id}; refresh the '
                'vault file or override GMAIL_CLIENT_ID to match.')
    if google_client_secret:
        cargo_env['GMAIL_CLIENT_SECRET'] = google_client_secret
    elif google_client_id in _WEB_CLIENT_IDS_REQUIRING_SECRET:
        raise RuntimeError(
            'GMAIL_CLIENT_SECRET is required: Google OAuth client '
            f'{google_client_id} rejects secretless token exchanges (400 '
            'invalid_request, "client_secret is missing."). Set '
            f'MAHO_GOOGLE_CLIENT_SECRET_{plat}, MAHO_GOOGLE_CLIENT_SECRET, or '
            'GMAIL_CLIENT_SECRET in the build environment, or place the '
            f'client JSON in .secrets/maho/, or bake a desktop-type PKCE-only '
            'client id via MAHO_GOOGLE_CLIENT_ID / GMAIL_CLIENT_ID.')
    return cargo_env


def load_build_lock():
    module_path = os.path.join(_MAHO_SCRIPTS_DIR, 'build_lock.py')
    spec = importlib.util.spec_from_file_location('maho_build_lock', module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f'Unable to load build lock helper: {module_path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build_lock


build_lock = load_build_lock()

WORKSPACE_ROOT = _WORKSPACE_ROOT
MAHO_ROOT = os.path.join(WORKSPACE_ROOT, 'maho')
MAHO_CHROMIUM_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..'))
PREBUILT_DIR = os.path.join(MAHO_CHROMIUM_ROOT, 'third_party', 'maho', 'prebuilt')


def _is_apple_target(rust_target):
    return rust_target.endswith('-apple-darwin')


def _weaken_macho_symbols(lib_path, symbols):
    """Set N_WEAK_DEF on a defined symbol without touching Mach-O relocations.

    llvm-objcopy cannot weaken rust_eh_personality because compact-unwind pins
    it, while ld -r corrupts the archive's __eh_frame relocations. The Mach-O
    operation itself is only an nlist_64 n_desc bit (N_WEAK_DEF = 0x0080), so
    apply that bit directly and leave all code/unwind bytes unchanged.
    """
    with open(lib_path, 'rb') as f:
        data = bytearray(f.read())
    if not data.startswith(b'!<arch>\n'):
        raise RuntimeError(f'not a static archive: {lib_path}')

    symbols = set(symbols)
    changed = set()
    offset = 8
    while offset + 60 <= len(data):
        header = data[offset:offset + 60]
        if header[58:60] != b'`\n':
            raise RuntimeError(f'invalid archive member header in {lib_path}')
        try:
            member_size = int(header[48:58].decode('ascii').strip())
        except ValueError as exc:
            raise RuntimeError(f'invalid archive member size in {lib_path}') from exc
        member = offset + 60
        object_offset = member
        name = header[:16].decode('ascii', errors='replace').strip()
        if name.startswith('#1/'):
            object_offset += int(name[3:])
        object_end = member + member_size

        # MH_MAGIC_64, little endian. Rust's Apple staticlibs are thin archives
        # of native-architecture Mach-O objects.
        if object_offset + 32 <= object_end and data[object_offset:object_offset + 4] == b'\xcf\xfa\xed\xfe':
            ncmds = struct.unpack_from('<I', data, object_offset + 16)[0]
            command = object_offset + 32
            symoff = None
            nsyms = 0
            stroff = 0
            strsize = 0
            for _ in range(ncmds):
                cmd, cmdsize = struct.unpack_from('<II', data, command)
                if cmd == 0x2:  # LC_SYMTAB
                    symoff, nsyms, stroff, strsize = struct.unpack_from(
                        '<IIII', data, command + 8)
                    break
                command += cmdsize
            if symoff is not None:
                strings = object_offset + stroff
                for index in range(nsyms):
                    entry = object_offset + symoff + index * 16
                    strx = struct.unpack_from('<I', data, entry)[0]
                    if not strx or strx >= strsize:
                        continue
                    start = strings + strx
                    end = data.find(b'\0', start, strings + strsize)
                    if end < 0:
                        continue
                    found = data[start:end].decode('utf-8', errors='replace')
                    if found in symbols:
                        n_type = data[entry + 4]
                        n_desc = struct.unpack_from('<H', data, entry + 6)[0]
                        if n_type & 0x0e and not n_desc & 0x0080:
                            struct.pack_into('<H', data, entry + 6,
                                             n_desc | 0x0080)
                            changed.add(found)
        offset = object_end + (member_size & 1)

    missing = symbols - changed
    if missing:
        raise RuntimeError(
            f'failed to weaken symbols in {lib_path}: {sorted(missing)}')
    with open(lib_path, 'wb') as f:
        f.write(data)
    print(f'  weakened {len(changed)} symbols in {os.path.basename(lib_path)}')


def _cargo_build(command, cwd, rust_target, package=None, env=None):
    cargo_env = (env or os.environ).copy()
    args = ['build', '--release', '--target', rust_target]
    if package:
        args += ['-p', package]
    if _is_apple_target(rust_target):
        rustflags = cargo_env.get('RUSTFLAGS', '')
        cargo_env['RUSTFLAGS'] = f'{rustflags} -Cpanic=abort'.strip()
        args += ['-Zbuild-std=std,panic_abort']
        command = ['rustup', 'run', 'nightly', 'cargo']
    subprocess.run(command + args, cwd=cwd, check=True, env=cargo_env)


def _tool_runs(path):
    try:
        return subprocess.run(
            [path, '--version'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode == 0
    except OSError:
        return False


def chromium_linux_toolchain_env(rust_target, env=None):
    cargo_env = (env or os.environ).copy()
    linux_sysroots = {
        'x86_64-unknown-linux-gnu': 'debian_bullseye_amd64-sysroot',
        'aarch64-unknown-linux-gnu': 'debian_bullseye_arm64-sysroot',
    }
    sysroot_name = linux_sysroots.get(rust_target)
    if sysroot_name is None:
        return cargo_env

    chromium_src = os.path.join(WORKSPACE_ROOT, 'chromium', 'src')
    toolchain_dir = os.path.join(
        chromium_src, 'third_party', 'llvm-build', 'Release+Asserts', 'bin')
    clang = os.path.join(toolchain_dir, 'clang')
    clangxx = os.path.join(toolchain_dir, 'clang++')
    llvm_ar = os.path.join(toolchain_dir, 'llvm-ar')
    sysroot = os.path.join(
        chromium_src, 'build', 'linux', sysroot_name)
    tools = (clang, clangxx, llvm_ar)
    if (all(os.path.isfile(tool) and _tool_runs(tool) for tool in tools)
            and os.path.isdir(sysroot)):
        target_env = rust_target.replace('-', '_')
        cargo_env[f'CC_{target_env}'] = clang
        cargo_env[f'CXX_{target_env}'] = clangxx
        cargo_env[f'AR_{target_env}'] = llvm_ar
        target_flags = (
            f'--target={rust_target}',
            f'--sysroot={sysroot}',
        )
        for variable in ('CFLAGS', 'CXXFLAGS'):
            key = f'{variable}_{target_env}'
            cargo_env[key] = ' '.join(filter(None, (
                cargo_env.get(key, ''),
                *target_flags,
            )))
    else:
        print('Chromium Linux toolchain unavailable; using host C toolchain.')
    return cargo_env


def _atomic_place_lib(src_lib, dst_lib, release_dir=None, private_prefix=None):
    # Atomic install: a concurrent build's linker must never read a half-written
    # lib. Copy to a temp, ranlib the temp, then os.replace (atomic rename).
    tmp = dst_lib + '.tmp'
    try:
        shutil.copy2(src_lib, tmp)
        if sys.platform == 'darwin':
            if release_dir is None or private_prefix is None:
                raise ValueError(
                    'Darwin staticlib placement requires release_dir and '
                    'private_prefix')
            _, _, objcopy = _find_llvm_tools()
            bundled_c_symbols = _defined_extern_symbols(
                _find_llvm_tools()[0],
                _bundled_c_staticlibs(release_dir))
            if bundled_c_symbols:
                workdir = tempfile.mkdtemp(prefix='maho-ossl-')
                try:
                    map_path = os.path.join(workdir, 'redefine.txt')
                    with open(map_path, 'w') as f:
                        f.write('\n'.join(
                            f'{symbol} {private_prefix}_{symbol}'
                            for symbol in sorted(bundled_c_symbols)))
                    subprocess.run(
                        [objcopy, f'--redefine-syms={map_path}', tmp],
                        check=True)
                finally:
                    shutil.rmtree(workdir, ignore_errors=True)
            _weaken_macho_symbols(tmp, {'_rust_eh_personality'})
        elif private_prefix:
            privatize_rust_staticlib(tmp, release_dir, private_prefix)
        if sys.platform == 'darwin':
            subprocess.run(['ranlib', '-c', tmp], check=True)
        os.replace(tmp, dst_lib)
    except Exception:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


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


def effective_cargo_target_dir(default_crate_dir, rust_target) -> str:
    target_dir = os.environ.get('CARGO_TARGET_DIR')
    if target_dir:
        return os.path.join(target_dir, rust_target)
    return os.path.join(default_crate_dir, 'target', rust_target)


def build_stt_ffi_prebuilt(rust_target, cpu_dir):
    """Build the maho-stt-ffi static lib and copy it to prebuilt/.

    maho-stt-ffi is a member of the maho/ workspace, so it shares one Rust
    runtime with maho-ffi. Building it standalone would embed a second copy and
    the two staticlibs would then define the same std symbols (Arc::drop_slow,
    thread-local shims, __rust_no_alloc_shim_is_unstable), which lld-link
    rejects when linking chrome.dll.
    """
    lib_name = 'maho_stt_ffi.lib' if sys.platform == 'win32' else 'libmaho_stt_ffi.a'
    output_dir = os.path.join(PREBUILT_DIR, cpu_dir)
    os.makedirs(output_dir, exist_ok=True)

    print(f'Building maho-stt-ffi for {rust_target}...')
    _cargo_build(['cargo'], MAHO_ROOT, rust_target, 'maho-stt-ffi')

    target_dir = effective_cargo_target_dir(MAHO_ROOT, rust_target)
    release_dir = os.path.join(target_dir, 'release')
    src_lib = os.path.join(release_dir, lib_name)
    dst_lib = os.path.join(output_dir, lib_name)
    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')
    # ELF/COFF can rewrite every bundled runtime symbol. Mach-O instead uses
    # the weak personality emitted by _mac_abort_std(); objcopy cannot rewrite
    # that symbol because compact-unwind relocations pin it.
    private_prefix = 'mahostt'
    _atomic_place_lib(src_lib, dst_lib, release_dir, private_prefix)
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')


def build_mail_ffi_prebuilt(rust_target, cpu_dir):
    """Build the standalone maho-mail-ffi static lib and copy it to prebuilt/.

    maho-mail-ffi (crate dir maho/mail-core) is its own cargo workspace (NOT a
    member of maho/ — avoids the libsqlite3-sys/bundled-sqlcipher conflict), so
    it is built from its crate dir. Its build.rs regenerates the C header via
    cbindgen; that header is copied into third_party/maho/ below.
    """
    lib_name = ('maho_mail_ffi.lib'
                if 'windows' in rust_target else 'libmaho_mail_ffi.a')
    crate_dir = os.path.join(MAHO_ROOT, 'mail-core')
    output_dir = os.path.join(PREBUILT_DIR, cpu_dir)

    if not os.path.isdir(crate_dir):
        print(f'Skipping maho-mail-ffi: {crate_dir} not found (Mail deferred)')
        return
    os.makedirs(output_dir, exist_ok=True)

    print(f'Building maho-mail-ffi for {rust_target}...')
    cargo_env = chromium_linux_toolchain_env(
        rust_target, mail_oauth_build_env())
    if 'windows' in rust_target:
        cargo_env['RUSTFLAGS'] = (
            cargo_env.get('RUSTFLAGS', '') + ' -C target-feature=+crt-static'
        ).strip()
    _cargo_build(
        ['cargo'], crate_dir, rust_target, env=cargo_env)

    target_dir = effective_cargo_target_dir(crate_dir, rust_target)
    src_lib = os.path.join(target_dir, 'release', lib_name)
    dst_lib = os.path.join(output_dir, lib_name)
    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')
    private_prefix = 'mahomail'
    _atomic_place_lib(
        src_lib, dst_lib, os.path.dirname(src_lib), private_prefix)
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')

    src_header = os.path.join(crate_dir, 'generated', 'maho_mail_ffi.h')
    dst_header = os.path.normpath(
        os.path.join(MAHO_CHROMIUM_ROOT, 'third_party', 'maho', 'maho_mail_ffi.h'))
    if os.path.exists(src_header):
        shutil.copy2(src_header, dst_header)
        print(f'Copied {src_header} -> {dst_header}')


def _find_llvm_tools() -> tuple[str, str, str]:
    """Locate llvm-nm/llvm-ar/llvm-objcopy from an installed rustup toolchain
    (the llvm-tools component ships them under a toolchain's sysroot).

    The active toolchain's sysroot is tried first, then every installed
    toolchain under RUSTUP_HOME is scanned as a fallback. This fallback is
    required because a pinned rust-toolchain.toml may select a minimal-profile
    toolchain that omits llvm-tools, and in detached build environments
    (e.g. a scheduled task) `rustc --print sysroot` may not resolve at all."""
    exe = '.exe' if sys.platform == 'win32' else ''
    names = ('llvm-nm', 'llvm-ar', 'llvm-objcopy')
    bases = []
    try:
        sysroot = subprocess.run(['rustc', '--print', 'sysroot'],
                                 capture_output=True, text=True, check=True).stdout.strip()
        if sysroot:
            bases += glob.glob(os.path.join(sysroot, 'lib', 'rustlib', '*', 'bin'))
    except Exception:
        pass
    rustup_home = os.environ.get('RUSTUP_HOME') or os.path.join(
        os.path.expanduser('~'), '.rustup')
    bases += glob.glob(os.path.join(rustup_home, 'toolchains', '*',
                                    'lib', 'rustlib', '*', 'bin'))
    for base in bases:
        tools = [os.path.join(base, n + exe) for n in names]
        if all(os.path.exists(p) for p in tools):
            return tools[0], tools[1], tools[2]
    chromium_llvm = os.path.join(
        WORKSPACE_ROOT, 'chromium', 'src', 'third_party', 'llvm-build',
        'Release+Asserts', 'bin')
    tools = [os.path.join(chromium_llvm, name) for name in names]
    if all(os.path.exists(path) for path in tools):
        return tools[0], tools[1], tools[2]
    if sys.platform.startswith('linux'):
        nm = shutil.which('nm')
        ar = shutil.which('ar')
        objcopy = shutil.which('objcopy')
        if nm and ar and objcopy:
            return nm, ar, objcopy
    sys.exit('Rust staticlib privatization needs llvm-nm/llvm-ar/llvm-objcopy; '
             'install the rustup llvm-tools component: rustup component add llvm-tools')


def _defined_extern_symbols(nm, libs):
    syms = set()
    for lib in libs:
        if not os.path.exists(lib):
            continue
        out = subprocess.run([nm, '--defined-only', '--extern-only', lib],
                             capture_output=True, text=True, errors='replace').stdout
        for line in out.splitlines():
            line = line.rstrip()
            if not line or line.endswith(':'):
                continue
            syms.add(line.split()[-1])
    return syms


def _is_private_rust_runtime_symbol(symbol):
    """True when this defined symbol is a bundled Rust runtime copy.

    Each Rust staticlib embeds its own std/alloc/core, so linking two of them
    into chrome.dll makes these definitions collide (LNK2005). Renaming them
    per-lib keeps each runtime self-contained. `___rust_alloc` and friends stay
    untouched: those are *undefined* here and must still bind to Chromium's
    shared PartitionAlloc shim.
    """
    # nm prints Mach-O symbols with a leading underscore (`_rust_eh_personality`)
    # but ELF/most others without, so normalize before the exact-name check;
    # otherwise macOS misses this symbol and it duplicate-collides with
    # Chromium's own Rust std at the monolithic link.
    if symbol.lstrip('_') == 'rust_eh_personality' or '___rustc' in symbol:
        return True
    # Matched anywhere in the name, not as a prefix: the alloc shim flag also
    # appears as a dllimport thunk (`__imp___rust_no_alloc_shim_is_unstable`).
    if 'rust_no_alloc_shim' in symbol:
        return True
    # Monomorphized generics, trait impls and thread-local shims all carry the
    # defining crate path in the mangled name, either as `core::` / `_ZN4core`
    # or, for `impl` blocks, mangled inside the symbol as `core..` after a
    # leading `_$LT$` (`<`). The alloc shim entry points match none of these.
    # rustc_demangle / addr2line / gimli / miniz_oxide are pulled in by std's
    # backtrace support, so they ship inside the bundled runtime too.
    #
    # MSVC (x86_64-pc-windows-msvc) uses the v0 Rust mangling, where the crate
    # path is encoded as `3std` / `4core` / `5alloc` segments after a crate
    # disambiguator, e.g. `_RNvMNtNtCsaZOKLwlFm27_3std4time7instant...`. Those
    # never contain `_ZN3std` or `std::`, so without the segment markers every
    # bundled std/core/alloc symbol stayed unprefixed and two staticlibs
    # collided at the chrome.dll link (`duplicate symbol
    # <std::time::Instant>::now`).
    if any(seg in symbol for seg in ('_3std', '_4core', '_5alloc')):
        return True
    return any(marker in symbol for marker in (
        'std::', 'core::', 'alloc::',
        'std..', 'core..', 'alloc..',
        '_ZN3std', '_ZN4core', '_ZN5alloc',
        'rustc_demangle', 'addr2line', 'gimli', 'miniz_oxide',
        'panic_unwind', 'unwind::', 'object::',
    ))


def _bundled_c_staticlibs(release_dir):
    libraries = []
    for name in ('libcrypto.lib', 'libssl.lib', 'libcrypto.a', 'libssl.a'):
        libraries += glob.glob(
            os.path.join(release_dir, 'build', 'openssl-sys-*', 'out',
                         '**', name),
            recursive=True)
    for pattern in (
            '*sqlcipher*.a', '*sqlcipher*.lib',
            '*sqlite3*.a', '*sqlite3*.lib'):
        libraries += glob.glob(
            os.path.join(release_dir, 'build', 'libsqlite3-sys-*', 'out',
                         pattern))
    return libraries


def privatize_rust_staticlib(lib_path, release_dir, prefix):
    """Rename embedded Rust runtime symbols to a per-library private prefix.

    Rust staticlibs bundle a full runtime. Chromium also links Rust std, so
    symbols such as `rust_eh_personality` collide at the final link. objcopy
    rewrites definitions and references within each member consistently.
    Bundled OpenSSL and SQLCipher symbols are also private because Chromium
    links BoringSSL and SQLite into the same final binary.
    """
    nm, ar, objcopy = _find_llvm_tools()
    rename = {
        s: f'{prefix}_{s}'
        for s in _defined_extern_symbols(
            nm, _bundled_c_staticlibs(release_dir))
    }
    for s in _defined_extern_symbols(nm, [lib_path]):
        if _is_private_rust_runtime_symbol(s):
            rename[s] = f'{prefix}_{s}'
    if not rename:
        return
    print(f'  privatizing {len(rename)} symbols in {os.path.basename(lib_path)} '
          f'(prefix {prefix}_)')
    workdir = tempfile.mkdtemp(prefix='maho-priv-')
    try:
        map_path = os.path.join(workdir, 'redefine.txt')
        with open(map_path, 'w') as f:
            f.write('\n'.join(f'{k} {v}' for k, v in rename.items()))
        extdir = os.path.join(workdir, 'ext')
        os.makedirs(extdir)
        subprocess.run([ar, 'x', lib_path], cwd=extdir, check=True)
        objs = [os.path.join(extdir, n) for n in os.listdir(extdir)
                if n.lower().endswith(('.o', '.obj'))]
        done = []
        for o in objs:
            subprocess.run([objcopy, f'--redefine-syms={map_path}', o],
                           check=True, capture_output=True, text=True)
            done.append(o)
        for i in range(0, len(done), 200):
            subprocess.run([ar, 'r', lib_path] + done[i:i + 200],
                           check=True, capture_output=True)
        print(f'  spliced {len(done)} rewritten members back into '
              f'{os.path.basename(lib_path)}')
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def build_rust_prebuilt(target=None, incremental=False):
    rust_target, cpu_dir, lib_name = get_target_triple(target)
    output_dir = os.path.join(PREBUILT_DIR, cpu_dir)
    os.makedirs(output_dir, exist_ok=True)

    print(f'Building maho-core for {rust_target}...')
    cargo_env = os.environ.copy()
    # Google Sign-In client id, read by maho-core via option_env!. Public
    # identifier, not a secret; unset simply leaves Google sign-in unconfigured
    # (resolve_client_id then errors instead of building a bad auth URL).
    cargo_env.setdefault('MAHO_GOOGLE_CLIENT_ID',
                         _DEFAULT_GOOGLE_CLIENT_ID)
    cargo_env = chromium_linux_toolchain_env(rust_target, cargo_env)
    if incremental:
        # Local dev only. maho/.cargo/config.toml disables cargo's incremental
        # compilation so the sccache rustc-wrapper can cache units, but that
        # cache measures 1.21% hits (9 of 745), so the trade is a loss for the
        # edit-rebuild loop. Release builds keep the non-incremental path:
        # incremental widens codegen-unit boundaries and costs cross-CGU
        # optimization in the lib that ships linked into the browser.
        cargo_env['CARGO_INCREMENTAL'] = '1'
        cargo_env['RUSTC_WRAPPER'] = ''  # empty overrides the config wrapper
    if 'windows' in rust_target:
        win32_syms = [
            'sqlite3_win32_is_nt', 'sqlite3_win32_sleep',
            'sqlite3_win32_write_debug', 'sqlite3_win32_utf8_to_unicode',
            'sqlite3_win32_unicode_to_utf8', 'sqlite3_win32_mbcs_to_utf8',
            'sqlite3_win32_mbcs_to_utf8_v2', 'sqlite3_win32_utf8_to_mbcs',
            'sqlite3_win32_utf8_to_mbcs_v2',
        ]
        rename_flags = ' '.join(f'-D{sym}=maho_{sym}' for sym in win32_syms)
        existing = cargo_env.get('LIBSQLITE3_FLAGS', '')
        cargo_env['LIBSQLITE3_FLAGS'] = f'{existing} {rename_flags}'.strip()
        rustflags = cargo_env.get('RUSTFLAGS', '')
        cargo_env['RUSTFLAGS'] = f'{rustflags} -C target-feature=+crt-static'.strip()
        for _tool in ('perl', 'nasm'):
            if shutil.which(_tool) is None:
                print(f'WARNING: {_tool} not found on PATH; the vendored OpenSSL '
                      'build (bundled-sqlcipher-vendored-openssl) will fail. '
                      'Install it (e.g. Strawberry Perl / NASM).')
    _cargo_build(['cargo'], MAHO_ROOT, rust_target, 'maho-ffi', cargo_env)

    target_dir = effective_cargo_target_dir(MAHO_ROOT, rust_target)
    src_lib = os.path.join(target_dir, 'release', lib_name)
    dst_lib = os.path.join(output_dir, lib_name)

    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')

    private_prefix = 'mahoffi'
    _atomic_place_lib(
        src_lib, dst_lib, os.path.dirname(src_lib), private_prefix)
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')

    build_stt_ffi_prebuilt(rust_target, cpu_dir)
    build_mail_ffi_prebuilt(rust_target, cpu_dir)

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
    cbindgen_bin = shutil.which('cbindgen') or os.path.expanduser('~/.cargo/bin/cbindgen')
    subprocess.run(
        [cbindgen_bin, '--config', cbindgen_config, '--crate', 'maho-ffi', '--output', temp_header_path],
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
        # Generated output, never hand-authored: it must always track maho-ffi's
        # exports, or C++ compiles against a header the static lib disagrees with.
        try:
            os.replace(temp_header_path, header_path)
        except OSError:
            shutil.copy2(temp_header_path, header_path)
            os.remove(temp_header_path)
        print(f'Updated generated header: {header_path}')
    else:
        print('Header content unchanged. Skip replace.')
        if os.path.exists(temp_header_path):
            os.remove(temp_header_path)


def main():
    parser = argparse.ArgumentParser(description='Build maho-core Rust static library.')
    parser.add_argument('--target', help='Explicit Rust target triple for cross-compilation.')
    parser.add_argument('--mail-only', action='store_true',
                        help='Build only the standalone Mail FFI prebuilt.')
    args = parser.parse_args()

    with build_lock():
        run_build(args.target, args.mail_only)


def run_build(target, mail_only):
    if not mail_only:
        build_rust_prebuilt(target)
        return

    rust_target, cpu_dir, _ = get_target_triple(target)
    build_mail_ffi_prebuilt(rust_target, cpu_dir)


if __name__ == '__main__':
    main()
