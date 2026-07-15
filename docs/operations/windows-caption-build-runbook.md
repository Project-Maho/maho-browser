# Windows Caption Feature — Build & Remote-Access Runbook

**Last updated:** 2026-07-15
**Owner branch/commit:** `main @ be9e4b69` (green / buildable)

This runbook documents (1) the Windows caption feature work, (2) how to build it
per platform, and (3) how to reach the Windows builder machine remotely. It exists
because this work spans macOS (dev), a Windows builder, and WSL, and the connection
setup is non-obvious.

---

## 1. Feature summary

Replace the Mac-style in-sidebar caption dots on **Windows** with:

- **Top-left app menu** — an ellipsis (`⋯`) button in the sidebar top bar that opens
  the standard Chromium `AppMenu` (`AppMenuModel`). Keyboard route:
  `BrowserView::ShowAppMenu()` → `MahoSidebarContainerView::ShowAppMenu()` →
  `MahoSidebarTopBarView::ShowAppMenu()`.
- **Hover-revealed native caption controls (WCO)** — the native Windows caption
  buttons (min/max/close) stay hidden at rest and reveal on hover from a small
  top-right trigger zone (96×8 dp), with a 500 ms delayed hide. Snap Layout HT
  codes preserved. Implemented by overlaying Chromium's Window Controls Overlay on
  the frameless Arc window.

### Key source locations
| What | Where |
|------|-------|
| App-menu button + AppMenu wiring | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc/.h` |
| Removed caption-dot layout tokens | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h` |
| `ShowAppMenu()` forwarder | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc/.h` |
| WCO hover-reveal patches (upstream overlay) | `maho-chromium/build/scripts/apply_chromium_src_overrides.py` → `chrome/browser/ui/views/frame/browser_caption_button_container_win.cc/.h` + `browser_view.cc` |

### Verification available on macOS
The caption C++ is `#if BUILDFLAG(IS_WIN)` / win-only files, so it **cannot be
compiled on macOS**. The only local check is that the override patches apply cleanly:

```bash
python3 maho-chromium/build/scripts/apply_chromium_src_overrides.py --platform win --dry-run
# EXIT=0, no "Missing expected anchor" → patches match current upstream
```

Actual compile + runtime verification requires a **Windows** build.

---

## 2. Build per platform

`build_maho.py` builds the Rust prebuilt (`maho-core`) first, then Chromium.
If `maho-core` does not compile, **no platform builds**. Use `--skip-rust` to reuse
an existing prebuilt static lib and bypass a broken Rust tree.

### Native Windows — the ONLY build that shows the caption feature
Requires Visual Studio + Windows SDK + `depot_tools` on the Windows machine.
```
git pull                                   # main @ be9e4b69 or later
gclient sync                               # ~121 GB on first sync
python3 maho-chromium/build/scripts/build_maho.py
chromium\src\out\Default\chrome.exe
```
Capture: top-left `⋯` menu, and hover the top-right edge to reveal caption controls.

### WSL / Linux — GUI visible, but NOT the Windows caption feature
- **WSLg** (Windows 11) renders Linux GUI apps on the Windows desktop, so a Linux
  Maho window **is viewable/screenshotable** from WSL.
- **However** the caption controls are `#if IS_WIN` — a Linux build uses the Linux
  frame and will **not** show the hover WCO caption / app-menu feature.
- Use this only to sanity-check the general build, not the Windows caption UI.
```
git pull
gclient sync
python3 maho-chromium/build/scripts/build_maho.py
./chromium/src/out/Default/chrome           # displays via WSLg
```

### macOS (local dev machine)
```
python3 maho-chromium/build/scripts/build_maho.py
```
Win caption code is excluded from the macOS build.

### CI
`.github/workflows/build-matrix.yml` builds `macos-arm64 / linux-x64 / linux-arm64 /
windows-x64` on self-hosted runners, triggered on push to `main` and PRs touching
`maho/**` or `maho-chromium/**`. It runs the full `build_maho.py` (no `--skip-rust`),
so `maho-core` must compile.

---

## 3. Remote access to the Windows builder (`DESKTOP-1LAPJMP`)

### Network — Tailscale
- Node: `desktop-1lapjmp` → `100.126.171.58` / `desktop-1lapjmp.taila0cef2.ts.net`
- Tailscale provides the **network path only**. On macOS the CLI ships inside the app:
  `/Applications/Tailscale.app/Contents/MacOS/Tailscale`.

### Auth — regular Windows OpenSSH (NOT Tailscale SSH)
- **Tailscale SSH server is not supported on Windows** (`SSH_HostKeys=False` for the
  node). So `tailscale ssh` cannot authenticate it; a normal `ssh` hits Windows
  OpenSSH and requires a key or password.
- **`sook` is a local Administrator.** Windows OpenSSH therefore reads
  `C:\ProgramData\ssh\administrators_authorized_keys` and **ignores**
  `%USERPROFILE%\.ssh\authorized_keys`.
- An existing authorized key labelled **`maho-win-builder`** is already present in
  `administrators_authorized_keys` (its private key was created on another machine —
  it is NOT on the current macOS dev box).

### Authorize a NEW client (e.g. this macOS box)
Run in an **Administrator PowerShell** on the Windows machine (ascii, no BOM, correct ACL):
```powershell
$key = '<paste client id_ed25519.pub one line>'
$adminKeys = "$env:ProgramData\ssh\administrators_authorized_keys"
Add-Content -Path $adminKeys -Value $key -Encoding ascii
icacls $adminKeys /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F"
Restart-Service sshd
```
Then from the client: `ssh sook@100.126.171.58 "ver"` should connect passwordless.

> Pitfalls that cause "Permission denied (publickey,...)" even after adding a key:
> - key added to per-user file while account is admin (server ignores it),
> - wrong ACL on `administrators_authorized_keys` (must be Administrators + SYSTEM only),
> - UTF-16/BOM in the file (`Add-Content -Encoding ascii` avoids it),
> - sshd not restarted.

### Credentials note
- **WSL Ubuntu** login: `sook` / `1234` — this is the **WSL** password only.
- **Windows account** `sook` login password is **separate** and unknown here; that is
  why `1234` is rejected by the SSH (Windows OpenSSH) server.

---

## 4. Gotchas / history

- macOS **cannot** build or run Windows Chromium, and cannot run WSL (WSL is
  Windows-only). Windows caption verification must happen on Windows hardware.
- A working-tree snapshot committed to `main` (`a6f4d1f3`) initially **broke the build**
  because it froze an unrelated in-flight `maho-core` Rust breakage (autofill/sync WIP)
  from concurrent sessions. Re-syncing the now-green tree (`be9e4b69`) fixed it.
- Secrets and junk are **excluded** from commits: `maho/relay/.dev.vars`, `**/node_modules`,
  `.tmp/`, `.opencode/`, `mail/` (embedded repo), build logs, and `*-snapshot.md`.
- This repo is **closed-source**; do not add OSS-claim language or public source links.
