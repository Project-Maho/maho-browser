# Platform branding notes

This directory keeps platform-specific branding requirements documented for the Maho Chromium overlay.

## macOS

- Use the existing `branding/mac/` icon assets and `app.icns` packaging flow.
- The current overlay already ships macOS-specific icon resources.

## Windows

- Windows needs a proper `.ico` file for the application icon.
- `build/scripts/build_maho.py` generates it on every Windows Chromium build:
  it packs `product_logo_{16,32,48,256}.png` into a multi-size ICO and writes
  it before GN generation to the Chromium application, mini-installer, and
  setup-stub resource paths. It also copies Maho tile PNGs for Start Menu
  integration. No generated `.ico` is tracked in this directory.

## Linux

- Linux desktop integration uses `linux/maho.desktop` with the XDG entry name
  `maho.desktop`; `build_maho_linux_packages.py` includes it and
  `product_logo_256.png` in tarball, Debian, and RPM packages. Release
  packaging requires the emitted Chromium runtime files and directories rather
  than silently producing a partial package.

## General note

- Prefer keeping source assets as PNG/SVG templates and generate platform-native packaging artifacts during the build or release process.
