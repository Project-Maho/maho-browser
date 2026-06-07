# maho-chromium

Chromium overlay for Maho Browser. Brave-style chromium_src overrides.

The canonical Chromium build entrypoint is the lock-backed wrapper at `python3 build/scripts/build_maho.py` from the overlay repo root. It builds the Rust prebuilt and then runs Chromium in one command. Pass `--ninja-target <T>` (repeatable) to build a narrower target through the same wrapper, e.g. `--ninja-target unit_tests`. Use raw `autoninja -C out/Default <target>` only when you want to skip the wrapper entirely (no Rust prebuild, no overlay/override apply).

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

2. Build Chromium and the Rust prebuilt together:
    ```
    python3 build/scripts/build_maho.py
    ```
    This acquires the shared workspace `.build.lock` once, builds the Rust prebuilt if needed, and then runs `autoninja -C out/Default chrome` from `chromium/src` with `depot_tools` prepended to `PATH`.

3. Optional, for Chromium-only rebuilds:
    ```
    autoninja -C out/Default chrome
    ```

4. Narrower target through the wrapper (Rust prebuilt skipped):
    ```
    python3 build/scripts/build_maho.py --skip-rust --ninja-target unit_tests
    ```
