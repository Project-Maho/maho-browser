# maho-chromium

Chromium overlay for Maho Browser. Brave-style chromium_src overrides.

Shared-artifact build commands use the workspace root `.build.lock` for the Rust prebuilt flow, so `python3 maho/build/scripts/build_maho_core.py` may pause with `Waiting for build lock: ...` if another build is already running.

## Structure

- `chromium_src/` — Upstream Chromium file overrides (#define + #include pattern)
- `browser/` — Maho C++ browser code (WebUI handlers, theme helpers)
- `components/` — Maho components (Mojo IDL, WebUI resources, constants)
- `build/` — Build scripts and GN configuration
- `third_party/maho/` — maho-core prebuilt static library + C++ bridge

## Setup

1. Symlink into Chromium source tree:
   ```
   ln -s ../../maho-chromium chromium/src/maho
   ```

2. Build maho-core:
   ```
   python3 maho/build/scripts/build_maho_core.py
   ```
   This uses the shared workspace `.build.lock` at the repo root.

3. Build Chromium with Maho:
   ```
   autoninja -C out/Default chrome
   ```
