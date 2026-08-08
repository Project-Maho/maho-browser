#!/usr/bin/env python3
"""Build maho-core Rust static library and copy to prebuilt directory."""

import argparse
import glob
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import importlib.util

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_MAHO_SCRIPTS_DIR = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..', 'maho', 'scripts'))
_DEFAULT_GOOGLE_CLIENT_ID = (
    '650357306759-c6tn05kg1l60ved2ne6cf9jp33moanh0.apps.googleusercontent.com'
)


def mail_oauth_build_env():
    cargo_env = os.environ.copy()
    google_client_id = cargo_env.get(
        'MAHO_GOOGLE_CLIENT_ID', _DEFAULT_GOOGLE_CLIENT_ID)
    cargo_env.setdefault('MAHO_GOOGLE_CLIENT_ID', google_client_id)
    cargo_env.setdefault('GMAIL_CLIENT_ID', google_client_id)
    google_client_secret = cargo_env.get(
        'MAHO_GOOGLE_CLIENT_SECRET', cargo_env.get('GMAIL_CLIENT_SECRET'))
    if google_client_secret:
        cargo_env['GMAIL_CLIENT_SECRET'] = google_client_secret
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

WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
MAHO_ROOT = os.path.join(WORKSPACE_ROOT, 'maho')
MAHO_CHROMIUM_ROOT = os.path.join(WORKSPACE_ROOT, 'maho-chromium')
PREBUILT_DIR = os.path.join(MAHO_CHROMIUM_ROOT, 'third_party', 'maho', 'prebuilt')


def _atomic_place_lib(src_lib, dst_lib, release_dir=None, private_prefix=None):
    # Atomic install: a concurrent build's linker must never read a half-written
    # lib. Copy to a temp, ranlib the temp, then os.replace (atomic rename).
    tmp = dst_lib + '.tmp'
    try:
        shutil.copy2(src_lib, tmp)
        if private_prefix:
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
    subprocess.run(
        ['cargo', 'build', '--release', '--target', rust_target,
         '-p', 'maho-stt-ffi'],
        cwd=MAHO_ROOT,
        check=True,
    )

    release_dir = os.path.join(MAHO_ROOT, 'target', rust_target, 'release')
    src_lib = os.path.join(release_dir, lib_name)
    dst_lib = os.path.join(output_dir, lib_name)
    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')
    _atomic_place_lib(
        src_lib, dst_lib, release_dir,
        'mahostt' if 'windows' in rust_target or 'linux' in rust_target else None)
    print(f'Copied {src_lib} -> {dst_lib}')
    print(f'Size: {os.path.getsize(dst_lib) / 1024 / 1024:.1f} MB')


def build_mail_ffi_prebuilt(rust_target, cpu_dir):
    """Build the standalone maho-mail-ffi static lib and copy it to prebuilt/.

    maho-mail-ffi (crate dir maho/mail-core) is its own cargo workspace (NOT a
    member of maho/ — avoids the libsqlite3-sys/bundled-sqlcipher conflict), so
    it is built from its crate dir. Its build.rs regenerates the C header via
    cbindgen; that header is copied into third_party/maho/ below.
    """
    lib_name = 'maho_mail_ffi.lib' if sys.platform == 'win32' else 'libmaho_mail_ffi.a'
    crate_dir = os.path.join(MAHO_ROOT, 'mail-core')
    output_dir = os.path.join(PREBUILT_DIR, cpu_dir)

    if not os.path.isdir(crate_dir):
        print(f'Skipping maho-mail-ffi: {crate_dir} not found (Mail deferred)')
        return
    os.makedirs(output_dir, exist_ok=True)

    print(f'Building maho-mail-ffi for {rust_target}...')
    subprocess.run(
        ['cargo', 'build', '--release', '--target', rust_target],
        cwd=crate_dir,
        check=True,
        env=mail_oauth_build_env(),
    )

    src_lib = os.path.join(crate_dir, 'target', rust_target, 'release', lib_name)
    dst_lib = os.path.join(output_dir, lib_name)
    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')
    _atomic_place_lib(
        src_lib, dst_lib, os.path.dirname(src_lib),
        'mahomail' if 'windows' in rust_target or 'linux' in rust_target else None)
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
    if symbol == 'rust_eh_personality' or '___rustc' in symbol:
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
    return any(marker in symbol for marker in (
        'std::', 'core::', 'alloc::',
        'std..', 'core..', 'alloc..',
        '_ZN3std', '_ZN4core', '_ZN5alloc',
        'rustc_demangle', 'addr2line', 'gimli', 'miniz_oxide',
        'panic_unwind', 'unwind::', 'object::',
    ))


def privatize_rust_staticlib(lib_path, release_dir, prefix):
    """Rename embedded Rust runtime symbols to a per-library private prefix.

    Rust staticlibs bundle a full runtime. Chromium also links Rust std, so
    symbols such as `rust_eh_personality` collide at the final link. objcopy
    rewrites definitions and references within each member consistently.
    Windows additionally privatizes bundled OpenSSL symbols, while Linux only
    requires the Rust runtime rewrite.
    """
    nm, ar, objcopy = _find_llvm_tools()
    ossl_libs = (
        glob.glob(os.path.join(release_dir, 'build', 'openssl-sys-*', 'out',
                               '**', 'libcrypto.lib'), recursive=True)
        + glob.glob(os.path.join(release_dir, 'build', 'openssl-sys-*', 'out',
                                 '**', 'libssl.lib'), recursive=True))
    rename = {s: f'{prefix}_{s}' for s in _defined_extern_symbols(nm, ossl_libs)}
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
    linux_sysroots = {
        'x86_64-unknown-linux-gnu': 'debian_bullseye_amd64-sysroot',
        'aarch64-unknown-linux-gnu': 'debian_bullseye_arm64-sysroot',
    }
    if rust_target in linux_sysroots:
        chromium_src = os.path.join(WORKSPACE_ROOT, 'chromium', 'src')
        clang = os.path.join(
            chromium_src, 'third_party', 'llvm-build', 'Release+Asserts',
            'bin', 'clang')
        sysroot = os.path.join(
            chromium_src, 'build', 'linux', linux_sysroots[rust_target])
        if os.path.isfile(clang) and os.path.isdir(sysroot):
            target_env = rust_target.replace('-', '_')
            cargo_env[f'CC_{target_env}'] = clang
            existing_cflags = cargo_env.get(f'CFLAGS_{target_env}', '')
            cargo_env[f'CFLAGS_{target_env}'] = ' '.join(filter(None, (
                existing_cflags,
                f'--target={rust_target}',
                f'--sysroot={sysroot}',
            )))
        else:
            print('Chromium Linux toolchain unavailable; using host C toolchain.')
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
    subprocess.run(
        ['cargo', 'build', '--release', '--target', rust_target, '-p', 'maho-ffi'],
        cwd=MAHO_ROOT,
        check=True,
        env=cargo_env,
    )

    src_lib = os.path.join(MAHO_ROOT, 'target', rust_target, 'release', lib_name)
    dst_lib = os.path.join(output_dir, lib_name)

    if not os.path.exists(src_lib):
        sys.exit(f'Build artifact not found: {src_lib}')

    _atomic_place_lib(
        src_lib, dst_lib, os.path.dirname(src_lib),
        'mahoffi' if 'windows' in rust_target or 'linux' in rust_target else None)
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
