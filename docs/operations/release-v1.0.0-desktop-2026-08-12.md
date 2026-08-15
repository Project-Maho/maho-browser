# Desktop Release v1.0.0 — Cross-Platform Build & Publish Record

**Date:** 2026-08-12
**Release tag:** `v1.0.0` on `Project-Maho/release` (release-only public repo)
**Scope:** macOS (prior), Windows, Linux desktop builds + `maho` CLI bundling.

---

## Build mode — RELEASE / OFFICIAL (confirmed, not debug)

All release templates use the macOS-aligned reduced-symbol policy:

| arg | Windows | Linux |
|-----|---------|-------|
| `is_official_build` | `true` | `true` |
| `is_debug` | `false` | `false` |
| `symbol_level` | `1` | `1` |
| `blink_symbol_level` | `0` | `0` |
| `strip_debug_info` | `true` | `true` |
| `dcheck_always_on` | `false` | `false` |

`is_debug=false` + `is_official_build=true` = official release. The reduced
symbol policy retains function-level backtraces while omitting Blink debug
information and stripping shipped binaries; it does **not** make these debug
builds. The Windows and Linux artifacts listed below are being replaced from
the earlier full-symbol (`symbol_level=2`) build with fresh branded artifacts
using this policy.

---

## Published artifacts (`Project-Maho/release`, tag `v1.0.0`)

| Platform | Asset | Size | Signature |
|----------|-------|------|-----------|
| macOS | `Maho-1.0.0.dmg` | 65 MB | (prior session) |
| macOS | `appcast.xml` | — | Sparkle Ed |
| Windows | `MahoSetup-1.0.0.exe` | 130 MB | **dev self-signed** (see caveat) |
| Windows | `update-win.json` | — | — |
| Linux | `maho-1.0.0-x86_64.tar.gz` | pending branded rebuild | GPG `.tar.gz.sig` (`51C8C6C55603AFF0`) |
| Linux | `maho_1.0.0_amd64.deb` | pending branded rebuild | GPG `.deb.sig` (`51C8C6C55603AFF0`) |
| Linux | `maho-1.0.0-1.x86_64.rpm` | pending branded rebuild | GPG `.rpm.sig` |

Chrome binaries: Windows `chrome.exe` PE ProductVersion `149.0.7812.0`
(`chrome.dll` ~315 MB); Linux `chrome` ~1.73 GB. Bundled `maho` CLI: Windows
`maho.exe` ~21 MB, Linux `maho` ~28 MB.

Publishing tool: `maho-chromium/build/scripts/release_local.py` run per-host
(`--skip-build` on Windows and Linux to reuse the verified build output),
uploading to the single `v1.0.0` release with `gh release upload --clobber`.
It writes each update manifest into the selected `--out-dir`. Keep the release
draft while all three hosts upload their platform artifacts; invoke the last
host with `--finalize` only after every expected asset is present to mark it
public and latest:

```bash
python3 maho-chromium/build/scripts/release_local.py --skip-build --finalize
```

Before this command, verify the draft release contains the Windows installer
and manifest, macOS DMG and appcast, and all Linux packages and signatures.
The Windows installer manifest is reapplied by the tracked Chromium override
before every Windows build, so `maho.exe` remains an installer payload on a
fresh checkout.

---

## `maho` CLI bundling verification (Windows)

- `maho.exe` present in `out/Release` and inside the installer archive
  (`chrome.7z` → `Chrome-bin\maho.exe`).
- `signtool verify /pa maho.exe` passes (against the dev cert trusted on the host).
- `maho.exe tab list` with a headless browser running returns the tab list
  (exit 0); with an absent socket it prints `Error: Browser not running` (exit 1).

Linux CLI: `out/Release/maho` staged next to the browser; absent-socket case
prints `Error: Browser not running`. The CLI is now bundled into the Linux
packages (see packaging note below).

---

## Signing status & caveats

- **Windows is dev self-signed only.** The workspace has no CA/EV Authenticode
  certificate, so `MahoSetup-1.0.0.exe` is signed with a self-signed dev cert
  generated on the build host. End users will see an "unknown publisher" /
  SmartScreen warning. For a distributable signature, re-sign with a real
  Authenticode certificate via the same env
  (`MAHO_WIN_CERT_PFX`/`MAHO_WIN_CERT_PASSWORD` or `MAHO_WIN_CERT_SHA1`) and
  re-run `release_local.py`.
- **Linux tarball, `.deb`, and `.rpm` are GPG-signed** with key
  `51C8C6C55603AFF0`. Release packaging fails unless each detached signature
  verifies against the committed public key. The reduced-symbol RPM spec
  disables debuginfo output, so `maho-debuginfo` is not a release asset.
- On Linux, `maho` remains the bundled CLI and `maho-browser` is the browser
  launcher used by the desktop entry. This preserves CLI subcommands while URL
  activation opens the browser.
- Both builds were produced from the **uncommitted working tree** (multi-session
  WIP). Commit for reproducibility before treating v1.0.0 as final.

---

## Build-environment / graph blockers resolved

Windows and Linux native checkouts were incomplete/stale relative to the local
macOS working tree. Resolved, in order:

1. Missing overlay source (Peek module, translate popover, google-sign-in test,
   ~133 tracked files) synced; stale `maho/browser/BUILD.gn` refreshed.
2. Stale Rust FFI workspace + `maho_ffi.h` synced (adds
   `maho_core_auto_archive_conversations`); untracked `maho-core` modules
   `composer_draft.rs` / `routine_runs.rs` synced.
3. grit resource-id registration for `maho_translate_content_script` present in
   `tools/gritsettings/resource_ids.spec`.
4. Windows: `maho_mail/node_modules` recreated as a directory junction (was a
   broken file-symlink) so esbuild bundling works.
5. Windows: `bun` / `run_tailwind.py` — Linux needed `~/.bun/bin` on `PATH`.
6. Windows disk exhaustion during the official ThinLTO `chrome.dll` link — freed
   scratch (cleared `%TEMP%`).
7. Google API key (`.secrets/maho/google_chromium_api_key.env`) required for
   release builds synced to both hosts.
8. `maho-cli` Windows portability: `SpawnedBrowser::terminate()` used Unix-only
   `nix` unconditionally; gated `#[cfg(unix)]` (nix) / `#[cfg(not(unix))]`
   (`std::process::Child::kill()`).
9. `SidebarCacheInvalidation` became non-aggregate (user-declared constructors);
   `maho_sidebar_footer_view.cc` designated-init call converted to explicit
   construction (behavior-preserving).

---

## Remote host changes (reversible; local macOS untouched)

- Dev code-signing cert generated + trusted on the Windows host
  (`CurrentUser`/`LocalMachine` Root + TrustedPublisher).
- GPG signing key imported into WSL keyring; passphrase preset via gpg-agent.
- Release/API secrets and `GH_TOKEN` copied to the Windows and WSL checkouts.
- `out/Default` populated with the runtime for Linux packaging.
- Windows Defender real-time scanning excludes only
  `C:\Users\sook\chromium\src\out\Release`. This recovers from concurrent
  `clang-cl` temporary-object rename failures without excluding the source
  checkout, secret directories, installation paths, or user files.
