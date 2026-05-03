# Cross-platform build setup

This repository is an overlay around Chromium, plus a Rust core that is consumed through a thin C/C++ bridge.

## Workspace structure

- `chromium/` — upstream Chromium checkout. Treat this as read-only and never modify it directly.
- `maho-chromium/` — this overlay repository. All Chromium integration changes live here.
- `maho/` — Rust core workspace used to build `maho-ffi` and the underlying `maho-core` logic.

The build scripts in this overlay assume that `maho/` and `maho-chromium/` live next to each other in the workspace layout used by the existing tooling.

## Supported targets

The Rust core can be built for these targets:

- macOS: `aarch64-apple-darwin`, `x86_64-apple-darwin`
- Windows: `x86_64-pc-windows-msvc`, `aarch64-pc-windows-msvc`
- Linux: `x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu`

## GN arg templates

Use the platform templates in `build/config/` as a starting point:

- `args_mac.gn`
- `args_win.gn`
- `args_linux.gn`

Each template sets the common release-style values used by this overlay:

- `is_official_build = false`
- `is_debug = false`
- `symbol_level = 0`

## Rust core build script

The `build/scripts/build_maho_core.py` helper can detect the host platform automatically, or you can pass an explicit target:

```bash
python build/scripts/build_maho_core.py
python build/scripts/build_maho_core.py --target=x86_64-pc-windows-msvc
python build/scripts/build_maho_core.py --target=aarch64-unknown-linux-gnu
```

The script copies the Rust static library into `third_party/maho/prebuilt/<cpu>/` and selects the correct library extension for the target platform.

## Platform-specific build notes

### macOS

- Use an Apple toolchain and the existing macOS icon packaging flow.
- `maho_core` is linked as a static library with the current macOS framework dependencies.

### Windows

Required tooling:

- Rust target support for MSVC
- Visual Studio Build Tools or a full Visual Studio install
- Windows SDK

Notes:

- The Rust static library is expected as `maho_ffi.lib`.
- The Chromium overlay should keep Windows-specific code behind `is_win` / `BUILDFLAG(IS_WIN)` checks.

### Linux

Required tooling:

- Rust target support for GNU Linux
- A Linux sysroot or equivalent Chromium-supported sysroot setup
- Standard GNU build tools

Notes:

- The Rust static library is expected as `libmaho_ffi.a`.
- The GN config template enables `use_sysroot = true` for Chromium-compatible builds.

## Chromium overlay rules

- Never edit files under `chromium/` directly.
- Put integration changes in `maho-chromium/` only.
- Keep public Rust FFI headers platform-agnostic; platform selection should happen in the build system and bridge glue.
