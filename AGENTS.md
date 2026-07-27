# PROJECT KNOWLEDGE BASE

**Generated:** 2026-07-14
**Commit:** 4fd48142
**Branch:** main

## OVERVIEW
Maho Browser monorepo: Rust core crates + Chromium overlay fork + native shells (macOS/iOS/Android). Also includes Maho Mail (Tauri) and Maho Website (Astro). Brave-style architecture — `maho-chromium` patches upstream Chromium, `maho` crates provide backend logic via FFI bridge.

## COMMUNICATION (MANDATORY)
- **한국어로 답할 때는 항상 존댓말로, 공손하게 답한다.** 반말 금지. 사용자를 친구처럼 대하지 말 것.
- When replying in Korean, ALWAYS use polite/formal speech (존댓말). Never use casual/informal speech (반말).
- Keep a respectful, professional tone at all times.

## LICENSING & DISTRIBUTION
- **Maho is CLOSED-SOURCE / PROPRIETARY.** This monorepo (browser, mail, relay, native shells) is NOT open source. Do NOT introduce OSS-claim language ("open source", "MIT licensed", "auditable source", "View on GitHub for source code", etc.) into marketing copy, docs, or WebUI strings.
- **Public source repositories do NOT exist for this codebase.** Never link to a public source repo URL for Maho itself. Any `github.com/*/maho*` link outside of upstream dependency references is wrong by default.
- **Downloads are distributed via a separate PUBLIC GitHub RELEASE-ONLY repository** (release binaries + release notes only, no source). This repo hosts `.dmg` / `.exe` / `.AppImage` / `.apk` and equivalent artifacts. When wiring download URLs (e.g. `website/src/pages/download.astro`), point to the release-only repo's `/releases` path, NOT to any main-source-tree URL.
- Upstream dependency GitHub links (Chromium, Lucide, Fontsource, etc.) are fine — they refer to third-party OSS, not to Maho's source.

## STRUCTURE
```
maho-workspace/
├── maho/               # Rust workspace + native shells (see maho/AGENTS.md)
│   ├── crates/         # maho-core, maho-ffi, maho-storage, maho-types, maho-engine, maho-import, maho-agent, maho-browser-mcp, maho-cli, maho-jni, maho-bench
│   ├── relay/          # Standalone sync relay server (separate Cargo workspace; see maho/relay/AGENTS.md)
│   ├── web-ai/         # Unified AI WebView bundle (Preact, iOS + Android + Desktop; see maho/web-ai/AGENTS.md)
│   ├── ios-shell/      # iOS shell
│   ├── android-shell/  # Android Gradle project
│   └── appium-tests/   # Regression automation
├── maho-chromium/      # Chromium overlay (see maho-chromium/AGENTS.md)
│   ├── chromium_src/   # Upstream file overrides (#define + #include pattern)
│   ├── browser/        # Maho C++ browser code + WebUI resources
│   ├── components/     # Mojo IDL, shared constants
│   ├── third_party/maho/ # maho-core prebuilt static lib + C++ bridge
│   └── build/          # GN config + build scripts
├── mail/               # Maho Mail (Tauri v2 + React 19; separate Cargo workspace)
├── website/            # Maho Website (Astro 6 + Starlight)
├── chromium/           # Upstream Chromium source (gitignored, ~121GB, gclient-managed)
└── docs/               # Design docs
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Rust backend logic | `maho/crates/maho-core/` | Manager-per-concern architecture |
| Shared types | `maho/crates/maho-types/` | Contract types only |
| FFI bridge (Rust→C++) | `maho/crates/maho-ffi/` → `maho-chromium/third_party/maho/` | |
| Chromium UI features | `maho-chromium/browser/ui/` | Views + WebUI surfaces |
| WebUI resources (TS) | `maho-chromium/browser/resources/` | Per-feature app.ts + HTML |
| Maho Mail (Frontend) | `mail/src/` | React 19 + Vite |
| Maho Mail (Rust) | `mail/src-tauri/` | Tauri v2 commands/state |
| Maho Website | `website/src/` | Astro pages & Starlight docs |
| Upstream overrides | `maho-chromium/chromium_src/` | Brave-style #define + #include |
| Sync relay server | `maho/relay/` | Standalone workspace; do NOT add to root Cargo.toml. See `maho/relay/AGENTS.md` |
| AI WebView bundle (iOS/Android/Desktop) | `maho/web-ai/` | Preact SPA loaded by WKWebView/WebView; see `maho/web-ai/AGENTS.md` |
| Build Chromium | `chromium/src/out/Default/` | Canonical: `python3 maho-chromium/build/scripts/build_maho.py` from repo root. It holds the shared `.build.lock`, so builds SERIALIZE — this is intentional: concurrent Chromium builds exhaust RAM and swap-thrash, making all builds slower. Pass `--ninja-target <T>` (repeatable) to build a narrower target through the same lock-backed wrapper. Build the NARROWEST target that verifies your change; avoid full `unit_tests`/`interactive_ui_tests`/`browser_tests` unless you must run that suite. Do NOT run raw `autoninja -C out/Default` on the shared machine — it bypasses the lock; if unavoidable, wrap it: `python3 maho/scripts/build_lock.py -- autoninja -C out/Default <target>`. |
| Build maho-core lib | `maho-chromium/build/scripts/build_maho_core.py` | Produces prebuilt static lib |
| Regression tests | `maho/appium-tests/` | Appium runners |
| Icon system (canonical) | `docs/icon-system.md` + `maho/assets/icons/sf-to-lucide-mapping.json` | Lucide is the unified icon library across all surfaces. Per-platform: `lucide-react` (Mail; canonical for new WebUI surfaces, see `maho-chromium/docs/webui-shadcn-conventions.md`), legacy vendored SVGs at `browser/resources/<feature>/icons/` for pre-shadcn surfaces, `aggregate_vector_icons` GN target at `maho-chromium/browser/ui/views/maho_lucide_icons/` (Native Views; auto-generated from `manifest.json` via `maho-chromium/build/scripts/lucide_icons/generate.py`), `lucide-icons-swift` SwiftPM (macOS/iOS), AVDs at `maho/android-shell/app/src/main/res/drawable/ic_*.xml`. License: `LICENSES/lucide.txt` |
| Reference screenshots (Arc/design target) | `maho/reference_screenshots/` | Ground-truth UI targets. e.g. `arc-library-space.jpeg` = Arc's Spaces library view reference for Maho's Archive-Spaces overlay. ALWAYS consult before claiming visual parity. |

## CONVENTIONS
- `bun run` for repo scripts, `bunx` for one-off CLIs
- `bun run test` / `bun run typecheck` proxy to Cargo at maho/ root
- Repo-provided build/test wrappers serialize through the workspace root `.build.lock`, so `bun run test` and `bun run typecheck` may pause and print `Waiting for build lock: ...` when another lock-backed step is active
- Chromium C++: explicit includes, `DCHECK_CALLED_ON_VALID_SEQUENCE`, namespace-local helpers
- Mojo handlers are thin — delegate to helpers in `browser/ai/` or `browser/ui/`
- WebUI pattern: each feature has `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` + `resources/{feature}/app.ts`
- `plan/` is local planning scratch, not canonical project documentation. Keep it gitignored; durable decisions and ship-state notes belong in AGENTS.md, ADRs, or committed docs.
- Subtree AGENTS.md files take precedence over this root guide

## CURRENT SHIP STATUS
- **Maho Mail is deferred from the current ship.** Do not expose or reactivate Mail without explicit user approval and a completed security/integrity remediation pass.
- Runtime defer gates are intentionally left in place while the bundle may still build: sidebar Mail entry hidden in `maho_sidebar_top_bar_view.cc`, `chrome://maho-mail` disabled via `MahoMailUIConfig::IsWebUIEnabled()`, helper launch blocked by `kMahoMailDeferred`, and Maho Mail settings panes hidden by `MAIL_DEFERRED`.
- Keeping the WebUI/resources/helper targets in the bundle is acceptable for now; they are unreachable dead weight. Removing them from GN/build scripts is a separate cleanup requiring explicit approval because it touches shared build infrastructure.

## ANTI-PATTERNS (THIS PROJECT)
- **No adding relay/ or mail/ crates to root workspace members** (libsqlite3-sys conflict)
- No type-safety suppression
- No domain types in `maho-core`; use `maho-types`
- No direct FFI/business-logic mixing in `maho-ffi`
- **NEVER delete `.build.lock`** — see BUILD LOCK below
- **빌드 실패 시 GN 파일 강제 수정 금지** — 빌드 (특히 `build_maho.py`) 가 GN regen 단계 또는 그 이후에서 실패하면 즉시 중단하고 사용자에게 보고. `chromium/src/**/BUILD.gn`, `maho-chromium/**/BUILD.gn`, `chromium/src/chrome/test/BUILD.gn` 등의 GN 파일을 우회 fix 목적으로 직접 수정하지 말 것. 다른 세션의 미완 작업이나 working tree dirty state 인 경우가 많고, 임의 cleanup 은 추후 충돌·revert·데이터 손실을 부른다. fix 가 필요해 보여도 **사용자 명시 승인 없이는 GN 편집·삭제 금지**.
- **Chromium/GN/build feature flags 또는 build flags 추가·변경 전에는 반드시 사용자 명시 승인을 받을 것.**

## BUILD LOCK — `.build.lock`

The workspace root `.build.lock` serializes all build/test entrypoints (`build_maho.py`, `bun run test`, `bun run typecheck`) via `fcntl.flock`. **The lock lives on the file descriptor, not the file path.**

**NEVER `rm .build.lock`:**
- The holding process still locks the deleted inode (fd stays open)
- A new process creates a fresh file (new inode) and locks it immediately
- Both builds run concurrently → corrupted output, partial writes, linker failures

**If a build times out waiting for the lock:**
```bash
# 1. Find the process holding the lock
lsof .build.lock

# 2. If a build IS running → wait for it, or kill the PROCESS (not the file)
kill <PID>

# 3. If no process holds the file → the lock was already released; just retry
python3 maho-chromium/build/scripts/build_maho.py
```

The lock is **automatically released** when the holding process exits (normal exit, crash, or SIGKILL). There is no scenario where deleting the file is the correct action.

## COMMANDS
```bash
# Rust workspace (Browser)
cd maho && bun run typecheck          # cargo check --workspace
cd maho && bun run test               # cargo test --workspace

# Maho Mail
cd mail && bun run tauri dev          # Run Tauri dev
cd mail && bun run test               # vitest (frontend)
cd mail && cargo test                 # Rust tests (run inside mail/ folder)

# Maho Website
cd website && bun run dev             # Astro dev (port 5555)
cd website && bun run build           # Build site

# Chromium build — ALWAYS via build_maho.py; it holds the shared .build.lock so builds
# SERIALIZE. Concurrent Chromium builds on one machine exhaust RAM (→ tens of GB of swap)
# and make ALL builds crawl. One heavy build at a time finishes faster than three thrashing.
python3 maho-chromium/build/scripts/build_maho.py    # canonical, builds chrome target
# Fast locked incremental rebuild of a NARROW target (skip rust + lucide, keep the lock):
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target <target>

# Build the NARROWEST target that verifies your change. Do NOT build full `unit_tests`,
# `interactive_ui_tests`, or `browser_tests` (they compile every test TU — huge) unless you
# actually need to run that whole suite. To just check a change compiles, build `chrome` or
# the single affected target.

# DO NOT run raw `autoninja -C out/Default ...` on the shared build machine — it BYPASSES the
# build lock, so parallel agents build at once, blow past RAM, and everything swap-thrashes.
# If you truly need raw ninja, wrap it in the lock:
#   python3 maho/scripts/build_lock.py -- autoninja -C out/Default <target>

# Build maho-core static lib for Chromium
python3 maho-chromium/build/scripts/build_maho_core.py

# Relay server
cd maho/relay && cargo test && cargo clippy -- -D warnings
```

## NOTES
- `chromium/` is gitignored (121GB upstream); managed by gclient, not this repo's git
- **Never use `git sparse-checkout`** in this workspace. `chromium/` is already gitignored and gclient-managed, so there is nothing to sparse-checkout. Do not infer sparse-checkout from the repo size unless the user explicitly requests it.
- Symlink setup: `ln -s ../../maho-chromium chromium/src/maho`
- **Three Cargo workspaces**: `maho/Cargo.toml` (main), `maho/relay/Cargo.toml` (isolated), and `mail/Cargo.toml` (isolated).
- See `maho/AGENTS.md` for crate-level detail; `maho-chromium/AGENTS.md` for overlay detail
- New subdirectory AGENTS.md added: `maho/relay/`, `maho-chromium/browser/ai/`, `maho-chromium/browser/ui/views/`, `maho-chromium/browser/ui/webui/`, `maho-chromium/browser/resources/`
