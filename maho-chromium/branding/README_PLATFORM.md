# Platform branding notes

This directory keeps platform-specific branding requirements documented for the Maho Chromium overlay.

## macOS

- Use the existing `branding/mac/` icon assets and `app.icns` packaging flow.
- The current overlay already ships macOS-specific icon resources.

## Windows

- Windows needs a proper `.ico` file for the application icon.
- The overlay currently does **not** generate a binary `.ico` asset here.
- If you need a placeholder for local build plumbing, convert `branding/product_logo_32.png` into a multi-size `.ico` with standard Windows icon sizes (16, 32, 48, 256).
- Keep the actual conversion in a build step or asset pipeline, not in source control.

## Linux

- Linux desktop integration should use a standard XDG desktop entry.
- Suggested desktop file name: `maho.desktop`.
- Recommended fields:
  - `Type=Application`
  - `Name=Maho`
  - `Exec=maho`
  - `Icon=maho`
  - `Categories=Network;Productivity;`

## General note

- Prefer keeping source assets as PNG/SVG templates and generate platform-native packaging artifacts during the build or release process.
