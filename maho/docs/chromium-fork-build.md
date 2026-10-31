# Chromium fork build flow

This document records the standard local build flow for the Chromium overlay used by Maho. Active implementation lives in the Chromium overlay repo mounted at `chromium/src/maho`, backed by the sibling `maho-chromium` directory at the workspace root.

## Purpose and scope

Use this flow when you need to build the Chromium desktop app bundle from the local overlay checkout. `out/Default` is the only active Chromium build directory. Do not create or use `out/Release` or any other alternate output. All recommended builds go through the canonical `python3 maho-chromium/build/scripts/build_maho.py` wrapper from the workspace root.

## Repo layout and working directories

- Chromium source tree root: `chromium/src` relative to the workspace root
- Overlay repo root: `maho-chromium` relative to the workspace root
- Rust workspace root for `maho-core`: `maho` relative to the workspace root
- The overlay is mounted into Chromium as `chromium/src/maho`

Run the build wrapper from the workspace root.

### Overlay mount setup

If `chromium/src/maho` is missing, create the symlink from the workspace root:

```bash
ln -s ../../maho-chromium chromium/src/maho
```

This local workspace already has that mount in place.

## Standard build commands

Build the Rust prebuilt library and Chromium app together from the workspace root:

```bash
python3 maho-chromium/build/scripts/build_maho.py
```

The wrapper owns the shared build lock and coordinates the Rust prebuilt and
Chromium build steps. For a Chromium-only rebuild, keep the same wrapper and
skip only the Rust step:

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust
```

The build wiring resolves the mac app bundle executable name to `Maho`.

## Build-time branding and Rust prebuilt flow

- `maho-chromium/branding/BRANDING` is the source of the Maho product identity.
  Before every Chromium build, `build_maho.py` overlays it into Chromium's active
  `chrome/app/theme/chromium/BRANDING` file; it also copies `product_logo_*`
  assets and generates `win/chromium.ico` from the 16/32/48/256 PNGs for
  Windows builds. Do not hand-edit files under `chromium/`.
- `chromium/src/chrome/BUILD.gn` uses `output_name = chrome_product_full_name` for the mac app bundle target.
- `maho-chromium/README.md` and this runbook point to `build_maho.py` as the canonical build entrypoint, including targeted rebuilds in `out/Default`.
- `maho-chromium/third_party/maho/BUILD.gn` links the prebuilt library via `libs = [ "maho_ffi" ]` and exposes the FFI headers and bridge.

## Release symbol policy

All platform release templates (`args_mac_release.gn`,
`args_win_release.gn`, and `args_linux_release.gn`) use the same policy:

```gn
is_official_build = true
is_debug = false
symbol_level = 1
blink_symbol_level = 0
strip_debug_info = true
dcheck_always_on = false
```

This is an optimized official release, not a debug build. It retains
function-level symbols for actionable crash backtraces but removes Blink's
largest debug-information payload and strips shipped binaries. When changing a
release template, force the existing output directory to adopt it:

```bash
python3 maho-chromium/build/scripts/build_maho.py \
  --release --force-args-template --force-gn-gen
```

## Artifact locations

- Chromium app bundle: `chromium/src/out/Default/Maho.app`
- Chromium output: `chromium/src/out/Default` only. During active development,
  another session may temporarily own the build lock or leave this tree
  inconsistent between edits. Wait for that session, then validate or
  incrementally rebuild the same output with `build_maho.py`. Never create a
  clean or dedicated Chromium output directory to avoid waiting.
- Do not use raw Ninja, GN/build-flag changes, or an alternate output directory
  to avoid the shared build lock.
- Mounted overlay path used by Chromium build: `chromium/src/maho/third_party/maho/prebuilt/arm64/libmaho_ffi.a`
- Sibling repo path backing the same file: `maho-chromium/third_party/maho/prebuilt/arm64/libmaho_ffi.a`

## Desktop E2E

VM 기반 데스크톱 E2E는 2026-09-18 완전 삭제되었습니다(VM 테스트 전면 금지, 사용자 지시).
데스크톱 E2E/검증은 omarchy(100.91.254.71)에서 실행합니다. 로컬 AI 패널 CDP 명령은
`docs/chromium-desktop-e2e.md`에 문서화되어 있습니다.

## Current local assumptions and limitations

- The checked-in prebuilt directory is present for `arm64`.
- `x64` was not present in this local workspace.
- Mobile Appium remains a separate simulator/emulator or device test layer.
