# Chromium fork build flow

This document records the standard local build flow for the Chromium overlay used by Maho. Active implementation lives in the Chromium overlay repo mounted at `chromium/src/maho`, backed by the sibling `maho-chromium` directory at the workspace root.

## Purpose and scope

Use this flow when you need to build the Chromium desktop app bundle from the local overlay checkout. `out/Default` is the only active Chromium build directory, including for isolated macOS VM E2E. Do not create or use `out/Release`, a dedicated VM output, or any other alternate output. All recommended builds go through the canonical `python3 maho-chromium/build/scripts/build_maho.py` wrapper from the workspace root.

## Repo layout and working directories

- Chromium source tree root: `chromium/src` relative to the workspace root
- Overlay repo root: `maho-chromium` relative to the workspace root
- Rust workspace root for `maho-core`: `maho` relative to the workspace root
- The overlay is mounted into Chromium as `chromium/src/maho`

Run the build wrapper and the VM inventory CLI from the workspace root. The approved VM lifecycle is documented in the workspace-level `docs/operations/macos-vm-e2e.md` runbook.

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

For the isolated VM targets, use the same shared `out/Default` build:

```bash
python3 maho-chromium/build/scripts/build_maho.py \
  --no-launch \
  --ninja-target chrome \
  --ninja-target interactive_ui_tests
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
- `maho-chromium/build/scripts/macos_vm_e2e_manifest.json` fixes the host build directory for VM runs at `out/Default`; VM evidence is exported separately from build output.

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
- VM E2E output: `chromium/src/out/Default` only. During active development,
  another session may temporarily own the build lock or leave this tree
  inconsistent between edits. Wait for that session, then validate or
  incrementally rebuild the same output with `build_maho.py`. Never create a
  clean or dedicated Chromium output directory to avoid waiting.
- Historical `chromium/src/out/VM-E2E` data and its external sparse-image mount
  are not supported build inputs and are not citable VM evidence. Preserve
  them until an explicitly approved cleanup, but do not build from them.
- External APFS storage remains for Tart images and lifecycle state only, not
  Chromium build output. Do not use raw Ninja, GN/build-flag changes, or an
  alternate output directory to avoid the shared build lock.
- Mounted overlay path used by Chromium build: `chromium/src/maho/third_party/maho/prebuilt/arm64/libmaho_ffi.a`
- Sibling repo path backing the same file: `maho-chromium/third_party/maho/prebuilt/arm64/libmaho_ffi.a`
- VM manifest: `maho-chromium/build/scripts/macos_vm_e2e_manifest.json`

`out/Default` is the only host build output the guest may consume read-only;
it is not the host proof package. The VM runbook defines the privacy, artifact,
and failure contracts. In particular, browser profiles, VM disks, keychains,
provider credentials, and raw environment dumps are not artifacts.

## Desktop VM E2E entrypoint

From the workspace root, the shipped lifecycle CLI exposes preflight,
golden-image preparation, inventory, and suite execution:

```bash
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py doctor \
  --state-dir /Volumes/maho-build/maho-vm-state \
  --identity ~/.cache/maho-macos-vm/id_ed25519
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py prepare \
  --state-dir /Volumes/maho-build/maho-vm-state \
  --identity ~/.cache/maho-macos-vm/id_ed25519
python3 maho-chromium/build/scripts/macos_vm_e2e.py list --json
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py run \
  --suite all \
  --golden maho-e2e-golden \
  --identity ~/.cache/maho-macos-vm/id_ed25519 \
  --state-dir /Volumes/maho-build/maho-vm-state
```

This workstation keeps Tart's images and the lifecycle ownership state on the external `/Volumes/maho-build` APFS volume. That volume is for Tart state only, not Chromium build output. Tart inherits `TART_HOME` from the environment, while `--state-dir` (or `MAHO_MACOS_VM_STATE`) governs the CLI's ownership state. `list` reads only the checked-in manifest and takes neither.

The commands above pass `--identity ~/.cache/maho-macos-vm/id_ed25519`, an explicit custom path outside the state directory. That's a deliberate choice on this workstation, not the default. Without `--identity`, the CLI uses `<state-dir>/id_ed25519`, and `prepare` creates that keypair when it's missing. With `--identity`, `prepare` creates the key at the path you name if it isn't there; `doctor` and `run` only read it, and `run` fails with a CLI error when the private key is absent. Pick one convention and keep it on all three commands, because a key created under the default path won't be found by a later command that passes `--identity`.

`maho-e2e-golden` is derived locally from the approved public base image and stays local and private. Never push, `tart push`, publish, or otherwise upload it to a registry, artifact store, or shared host: it is machine-local build state, and a leaked guest image is a credential and data exposure. Pulling the approved public base `ghcr.io/cirruslabs/macos-tahoe-base:latest` is fine and is what `prepare` does.

`doctor`, `prepare`, and `list` have reviewed receipts in the workspace-level VM
runbook; the live `run all` receipt remains pending there. The historical
Accessibility sidebar harness is unavailable and must not be used as a
substitute. Local AI panel CDP commands are documented separately in
`docs/chromium-desktop-e2e.md`; they do not by themselves constitute isolated
VM proof.

## Current local assumptions and limitations

- The checked-in prebuilt directory is present for `arm64`.
- `x64` was not present in this local workspace.
- The desktop VM contract targets a disposable macOS guest and the canonical host `out/Default` build shared into that guest read-only. Wait for the active shared build owner and lock; never create or use an alternate output. Mobile Appium remains a separate simulator/emulator or device test layer.
