# Chromium fork build flow

This document records the standard local build flow for the Chromium overlay used by Maho. Active implementation lives in the Chromium overlay repo mounted at `chromium/src/maho`, backed by the sibling repo `/Users/indo/code/project/maho-workspace/maho-chromium`.

## Purpose and scope

Use this flow when you need to build the Chromium desktop app bundle or run the Chromium desktop macOS E2E harness from the local overlay checkout. Use `out/Default` as the standard build directory. Do not treat `out/Release` as the default path.

## Repo layout and working directories

- Chromium source tree root: `/Users/indo/code/project/maho-workspace/chromium/src`
- Overlay repo root: `/Users/indo/code/project/maho-workspace/maho-chromium`
- Rust workspace root for `maho-core`: `/Users/indo/code/project/maho-workspace/maho`
- The overlay is mounted into Chromium as `chromium/src/maho`

Run the Chromium overlay build and E2E commands from `chromium/src`.

### Overlay mount setup

If `chromium/src/maho` is missing, create the symlink from `/Users/indo/code/project/maho-workspace`:

```bash
ln -s ../../maho-chromium chromium/src/maho
```

This local workspace already has that mount in place.

## Standard build commands

1. Build the Rust prebuilt library:

   ```bash
   python3 maho/build/scripts/build_maho_core.py
   ```

   The checked-in helper runs `cargo build --release --target <triple> -p maho-ffi` in the sibling `maho` repo, producing `libmaho_ffi.a`, then copies it into the overlay prebuilt directory as `libmaho_core.a`.

2. Build Chromium with the Maho overlay:

   ```bash
   autoninja -C out/Default chrome
   ```

   The build wiring resolves the mac app bundle executable name to `Maho`, so this command produces `out/Default/Maho.app`.

## Rust prebuilt flow

- `chromium/src/chrome/app/theme/maho/BRANDING` sets `PRODUCT_FULLNAME=Maho` and `MAC_BUNDLE_ID=com.maho.browser`.
- `chromium/src/chrome/BUILD.gn` uses `output_name = chrome_product_full_name` for the mac app bundle target.
- `maho-chromium/README.md` uses `autoninja -C out/Default chrome` as the standard build command.
- `maho-chromium/third_party/maho/BUILD.gn` links the prebuilt library via `libs = [ "maho_core" ]` and exposes the FFI headers and bridge.
- `maho-chromium/build/scripts/e2e_selectors.py` defaults `MAHO_APP_PATH` to the `out/Default` app bundle.

## Artifact locations

- Chromium app bundle: `chromium/src/out/Default/Maho.app`
- Mounted overlay path used by Chromium build: `chromium/src/maho/third_party/maho/prebuilt/arm64/libmaho_core.a`
- Sibling repo path backing the same file: `maho-chromium/third_party/maho/prebuilt/arm64/libmaho_core.a`
- E2E artifacts: `/tmp/maho-chromium-e2e-spaces/<scenario-name>/`

The E2E harness also writes per-run files like `launch.log`, `before.png`, `after.png`, and failure diagnostics under each scenario directory.

## E2E entrypoints

From `chromium/src`:

```bash
python3 maho/build/scripts/e2e_chromium_sidebar.py
python3 maho/build/scripts/e2e_chromium_sidebar.py --list
python3 maho/build/scripts/e2e_chromium_sidebar.py --scenario launch_clean_profile
python3 maho/build/scripts/e2e_chromium_sidebar.py --scenario phase2_seeded_favorites_snapshot_assert --scenario phase2_folder_sibling_reorder_blocked
```

`MAHO_APP_PATH` defaults to the built app bundle in `out/Default`, and `MAHO_E2E_ARTIFACT_DIR` defaults to `/tmp/maho-chromium-e2e-spaces`.

## Current local assumptions and limitations

- The checked-in prebuilt directory is present for `arm64`.
- `x64` was not present in this local workspace.
