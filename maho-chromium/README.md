# maho-chromium

Chromium overlay for Maho Browser. Brave-style chromium_src overrides.

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

3. Build Chromium with Maho:
   ```
   autoninja -C out/Release chrome
   ```
