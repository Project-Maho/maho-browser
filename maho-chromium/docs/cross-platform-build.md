# Cross-platform build setup

**Last updated:** 2026-07-10
**Companion documents:**
- `.omo/plans/cross-platform-completion-plan-2026-07-10.md` — active completion plan
- `docs/operations/ci-infrastructure.md` — CI hardware layout and runner setup
- `docs/operations/linux-packaging-pipeline.md` — Linux packaging runbook

---

## Overview

This repository is an overlay around Chromium, plus a Rust core that is consumed through a thin C/C++ bridge. The build system supports macOS, Windows, and Linux at the infrastructure level. Feature completeness and packaging maturity vary by platform — see the status matrix below.

## Workspace structure

- `chromium/` — upstream Chromium checkout. Treat this as read-only and never modify it directly.
- `maho-chromium/` — this overlay repository. All Chromium integration changes live here.
- `maho/` — Rust core workspace used to build `maho-ffi` and the underlying `maho-core` logic.

The build scripts in this overlay assume that `maho/` and `maho-chromium/` live next to each other in the workspace layout used by the existing tooling.

---

## Platform Status Matrix (as of 2026-07-10)

### Build infrastructure (all platforms fully supported)

| Component | macOS | Windows | Linux |
|---|---|---|---|
| `build_maho.py` platform detection | ✅ | ✅ | ✅ |
| Overlay mount mechanism | ✅ symlink | ✅ `mklink /J` junction | ✅ symlink |
| `gn` / `autoninja` binary switching | ✅ | ✅ `.bat` variants | ✅ |
| `apply_chromium_src_overrides.py` filtering | ✅ | ✅ via `FILE_PLATFORMS` | ✅ via `FILE_PLATFORMS` |
| GN args template | ✅ `args_mac.gn` | ✅ `args_win.gn` | ✅ `args_linux.gn` |
| Rust FFI cross-compile (`build_maho_core.py`) | ✅ | ✅ | ✅ |
| FFI linker config in `third_party/maho/BUILD.gn` | ✅ | ✅ | ✅ |
| `.build.lock` mechanism (POSIX `fcntl.flock`) | ✅ | ⚠️ Unix-only; Windows path unverified | ✅ |

### Rust workspace (all core crates buildable on all platforms)

| Feature | macOS | Windows | Linux |
|---|---|---|---|
| Core crates (`maho-core`, `maho-types`, `maho-storage`, `maho-ffi`) | ✅ | ✅ | ✅ |
| Zero platform-locked deps at workspace level | ✅ | ✅ | ✅ |
| No `todo!()` / `unimplemented!()` panics | ✅ | ✅ | ✅ |
| No `_mac.rs` / `_win.rs` / `_linux.rs` files — all `#[cfg]` inline | ✅ | ✅ | ✅ |
| Platform-locked deps (conditional-compile only) | `security-framework` | `windows-sys` | none |

### Native features (implementation gaps by platform)

| Feature | macOS | Windows | Linux |
|---|---|---|---|
| Command palette vibrancy | ✅ NSVisualEffectView | ✅ DWM/Win11 acrylic (Win11 22H2+) | ⚪ no-op stub |
| Utility panel vibrancy | ✅ NSVisualEffectView | ⚪ no-op stub | ⚪ no-op stub |
| Toast vibrancy | ✅ NSVisualEffectView | ⚪ no-op stub | ⚪ no-op stub |
| Traffic light geometry (sidebar top bar) | ✅ | ⚪ returns `std::nullopt` | ⚪ returns `std::nullopt` |
| Maho Mini window (pip/popout) | ✅ full impl | ❌ empty BUILD.gn stub | ❌ empty BUILD.gn stub |
| Native notification share | ✅ NSUserNotification | ❌ file absent | ❌ file absent |
| os_crypt file-backed key provider | ✅ Keychain bypass | ⚪ Chromium DPAPI fallback | ⚪ Chromium kwallet fallback |
| MCP IPC transport | ✅ Unix domain socket | ✅ Named pipe + DPAPI session token (C++ side) | ✅ Unix domain socket |
| MCP client Rust transport | ✅ `tokio::net::UnixStream` | ⚠️ **Path resolves but transport not wired to named pipe** | ✅ `tokio::net::UnixStream` |
| Chromium cookie decryption (v10) | ✅ via Keychain | ❌ not implemented | ❌ not implemented |
| Sync `DeviceType` enum variant | ✅ `Mac` | ⚠️ `Unknown` (no variant) | ⚠️ `Unknown` (no variant) |
| Hostname resolution | ✅ | ✅ | ⚠️ `"Unknown Device"` on non-mac/linux fallback |
| CLI browser launcher (`maho-cli`) | ✅ `open -a Maho` | ⚪ TODO | ⚪ TODO |
| File permission enforcement (`0o600`) | ✅ `#[cfg(unix)]` | ⚠️ no ACL/DACL equivalent | ✅ `#[cfg(unix)]` |

### Auto-update subsystem

| Platform | Compiled delegate | Status |
|---|---|---|
| macOS | `maho_update_delegate_mac.mm` | ✅ Sparkle 2 backed |
| Windows | `maho_update_delegate_win.cc` (~14.8KB) | ✅ **Full WinRT implementation** with JSON manifest download, signature verification |
| Linux | `maho_update_delegate_linux.cc` | ✅ Signed-manifest check, one factory for all shipped formats. Dispatches on `linux_install_kind.cc`: an install under `/usr/lib/maho` (deb/rpm) gets the apt/dnf banner, any other directory (tar.gz) gets download guidance |

`maho_update_delegate_stub.cc` still sits on disk and defines a fourth
`CreatePlatformUpdaterDelegate()`, but is referenced by no `BUILD.gn` and is
never compiled.

### Chromium source overrides

The `chromium_src/` directory holds tracked source overrides. Platform-neutral
files apply on all platforms; platform-specific files are gated by the
`FILE_PLATFORMS` map in `build/scripts/apply_chromium_src_overrides.py`:

- `chrome/app/app-Info.plist` — mac only
- `chrome/browser/app_controller_mac.mm` — mac only
- `components/os_crypt/common/keychain_password_mac.mm` — mac only
- `chrome/installer/mini_installer/chrome.release` — Windows only; adds
  `maho.exe` to the installer payload so a fresh checkout bundles the CLI.

All other overrides apply cross-platform.

### Branding assets

| Platform | Status |
|---|---|
| macOS | ✅ `branding/mac/` has full `.icns` + xcassets |
| Windows | ✅ `build_maho.py` generates Maho icons for `chrome.exe`, `mini_installer.exe`, extracted `setup.exe`, and Start Menu tiles from Maho `product_logo_*` assets before GN generation |
| Linux | ✅ `branding/linux/maho.desktop` and `product_logo_256.png` are packaged into tarball, Debian, and RPM artifacts; release packaging rejects missing required runtime files |

---

## Supported target triples

The Rust core is buildable for these targets. `build_maho_core.py` accepts `--target=<triple>` or auto-detects the host.

| Platform | Architecture | Rust triple | Output artifact | Ship status |
|---|---|---|---|---|
| macOS | arm64 | `aarch64-apple-darwin` | `libmaho_ffi.a` | ✅ Primary |
| macOS | x86_64 | `x86_64-apple-darwin` | `libmaho_ffi.a` | ⚪ Rust builds; no macOS x64 GN preset |
| Windows | x64 | `x86_64-pc-windows-msvc` | `maho_ffi.lib` | 🟡 Primary target for Windows |
| Windows | arm64 | `aarch64-pc-windows-msvc` | `maho_ffi.lib` | ⚪ Rust builds; no arm64 GN preset |
| Linux | x64 | `x86_64-unknown-linux-gnu` | `libmaho_ffi.a` | 🟡 Primary target for Linux |
| Linux | arm64 | `aarch64-unknown-linux-gnu` | `libmaho_ffi.a` | ⚪ Rust builds; no arm64 GN preset; not in packaging pipeline |

**Note:** GN args presets exist only for macOS arm64, Windows x64, and Linux x64. arm64 support for Windows/Linux at the Chromium build level requires creating `args_{platform}_arm64.gn` and adding an `--arch` flag to `build_maho.py`.

---

## GN arg templates

Use the platform templates in `build/config/` as a starting point:

| File | `target_os` | `target_cpu` | Notes |
|---|---|---|---|
| `args_mac.gn` | `mac` | `arm64` | Includes `enable_traffic_annotation_auditor = true` |
| `args_win.gn` | `win` | `x64` | Minimal |
| `args_linux.gn` | `linux` | `x64` | Includes `use_sysroot = true` |

Common values across all templates:
- `is_official_build = false`
- `is_debug = false`
- `symbol_level = 0`
- `maho_root = "//maho"`

Official release templates (`args_{mac,win,linux}_release.gn`) instead use
the shared reduced-symbol policy:

```gn
is_official_build = true
is_debug = false
symbol_level = 1
blink_symbol_level = 0
strip_debug_info = true
dcheck_always_on = false
```

This keeps function-level crash symbols while dropping Blink debug information
and stripping shipped binaries. Regenerate the output directory with
`--force-args-template --force-gn-gen` after changing a release template.

---

## Rust core build script

The `build/scripts/build_maho_core.py` helper detects the host platform automatically, or you can pass an explicit target:

```bash
python3 build/scripts/build_maho_core.py
python3 build/scripts/build_maho_core.py --target=x86_64-pc-windows-msvc
python3 build/scripts/build_maho_core.py --target=aarch64-unknown-linux-gnu
```

The script:
1. Runs `cargo build --release --target <triple>` in `maho/crates/maho-ffi/`
2. Copies the resulting static lib into `third_party/maho/prebuilt/<cpu>/`
3. Selects the correct library extension (`.a` for POSIX, `.lib` for Windows)
4. Runs `ranlib -c` on macOS

---

## Platform-specific build notes

### macOS

**Required tooling:**
- Xcode Command Line Tools + Apple toolchain
- Rust target: `aarch64-apple-darwin` (default on M-series) or `x86_64-apple-darwin`

**Notes:**
- `maho_core` links against `CoreFoundation`, `Security`, `SystemConfiguration`, `resolv`, `c++`.
- Existing macOS icon packaging flow at `branding/mac/` produces `.icns` bundle resources.
- **Sparkle 2 auto-updater**: The real delegate `maho_update_delegate_mac.mm` exists but is not compiled. See `.omo/plans/cross-platform-completion-plan-2026-07-10.md` Phase A.4.

### Windows

**Required tooling:**
- Visual Studio Build Tools 2022 or a full Visual Studio install
- Windows SDK 10.0.22000.0 or later
- Rust targets: `x86_64-pc-windows-msvc` (primary), optionally `aarch64-pc-windows-msvc`
- `depot_tools` must be on `PATH`

**Notes:**
- The Rust static library is emitted as `maho_ffi.lib`.
- `third_party/maho/BUILD.gn` links `ws2_32.lib`, `userenv.lib`, `ntdll.lib`, `iphlpapi.lib`, `bcrypt.lib` on Windows.
- Windows-specific product code must be gated behind `is_win` / `BUILDFLAG(IS_WIN)`.
- **Known gaps** (see completion plan):
  - MCP client Rust transport in `maho-browser-mcp` uses `tokio::net::UnixStream`; a named pipe transport must be wired for Windows to function.
  - Maho Mini feature is an empty stub block in `browser/BUILD.gn`.
  - `installer/README.md` (referenced by `maho_update_delegate_win.cc:119`) does not exist.
- `.build.lock` mechanism uses `fcntl.flock` which is Unix-only; Windows compatibility has not been verified. If Windows build entrypoints fail at lock acquisition, the lock helper needs a Windows abstraction (msvcrt `_locking` or `LockFileEx`).

### Linux

**Required tooling:**
- A Linux sysroot or equivalent Chromium-supported sysroot setup (Chromium supplies these automatically when `use_sysroot=true`)
- Standard GNU build tools + clang + lld
- Rust target: `x86_64-unknown-linux-gnu` (primary), optionally `aarch64-unknown-linux-gnu`

**Notes:**
- The Rust static library is emitted as `libmaho_ffi.a`.
- `third_party/maho/BUILD.gn` links `dl`, `rt`, `pthread`, `m` on Linux.
- The GN config template enables `use_sysroot = true` for Chromium-compatible builds.
- **Update delegates**: Linux compiles exactly one delegate covering all three shipped formats — deb/rpm get the apt/dnf banner, tar.gz gets download guidance. The AppImage, Flatpak, and noop delegates were deleted; Maho ships none of those formats.
- **Known gaps** (see completion plan):
  - Chromium cookie decryption (v10) requires libsecret/kwallet integration; currently returns `None` on non-mac.

---

## Chromium overlay rules

- Never edit files under `chromium/` directly.
- Put integration changes in `maho-chromium/` only.
- Keep public Rust FFI headers platform-agnostic; platform selection should happen in the build system and bridge glue.
- Do not add platform-suffixed Rust files (`_win.rs`, `_linux.rs`, etc.); use inline `#[cfg]` attributes instead. All existing Rust code follows this pattern.
- When a Maho-specific native feature (e.g. vibrancy, notifications, Maho Mini) does not have an implementation on a target platform, add a `_stub.cc` file that no-ops gracefully rather than blocking compilation. See `browser/ui/views/command/maho_command_overlay_vibrancy_stub.cc` as the reference pattern.

---

## Verifying a cross-platform build

Once builds succeed on a target platform:

1. Run the Rust workspace tests: `cd maho && bun run test`
2. Run relevant Chromium unit tests: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests`
3. Manual smoke: launch the built binary and verify a browser window opens.
4. For release-oriented validation, follow the platform-specific runbook:
   - macOS: (existing manual test flow)
   - Linux: `docs/operations/linux-packaging-pipeline.md`
   - Windows: TBD — no packaging runbook exists yet

Full CI infrastructure and runner registration is described in `docs/operations/ci-infrastructure.md`.
