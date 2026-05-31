# PROJECT KNOWLEDGE BASE

**Generated:** 2026-05-15
**Commit:** e2e5fbf
**Branch:** main

## OVERVIEW
Maho Browser monorepo: Rust core crates + Chromium overlay fork + native shells (macOS/iOS/Android). Also includes Maho Mail (Tauri) and Maho Website (Astro). Brave-style architecture — `maho-chromium` patches upstream Chromium, `maho` crates provide backend logic via FFI bridge.

## STRUCTURE
```
maho-workspace/
├── maho/               # Rust workspace + native shells (see maho/AGENTS.md)
│   ├── crates/         # maho-core, maho-ffi, maho-storage, maho-types, maho-engine, maho-extension, maho-jni, maho-bench
│   ├── relay/          # Standalone sync relay server (separate Cargo workspace)
│   ├── macos-shell/    # AppKit shell (reference; active work in Chromium fork)
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
| Sync relay server | `maho/relay/` | Standalone workspace; do NOT add to root Cargo.toml |
| Build Chromium | `chromium/src/out/Default/` | Canonical: `python3 maho-chromium/build/scripts/build_maho.py` from repo root. Optional: `autoninja -C out/Default chrome` for narrower rebuilds. |
| Build maho-core lib | `maho-chromium/build/scripts/build_maho_core.py` | Produces prebuilt static lib |
| Regression tests | `maho/appium-tests/` | Appium runners |
| Icon system (canonical) | `docs/icon-system.md` + `maho/assets/icons/sf-to-lucide-mapping.json` | Lucide is the unified icon library across all surfaces. Per-platform: `lucide-react` (Mail/WebUI), vendored SVGs (`browser/resources/<feature>/icons/`), `aggregate_vector_icons` GN target at `maho-chromium/browser/ui/views/maho_lucide_icons/` (Native Views), `lucide-icons-swift` SwiftPM (macOS/iOS), AVDs at `maho/android-shell/app/src/main/res/drawable/ic_*.xml`. License: `LICENSES/lucide.txt` |

## CONVENTIONS
- `bun run` for repo scripts, `bunx` for one-off CLIs
- `bun run test` / `bun run typecheck` proxy to Cargo at maho/ root
- Repo-provided build/test wrappers serialize through the workspace root `.build.lock`, so `bun run test` and `bun run typecheck` may pause and print `Waiting for build lock: ...` when another lock-backed step is active
- Chromium C++: explicit includes, `DCHECK_CALLED_ON_VALID_SEQUENCE`, namespace-local helpers
- Mojo handlers are thin — delegate to helpers in `browser/ai/` or `browser/ui/`
- WebUI pattern: each feature has `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` + `resources/{feature}/app.ts`
- Subtree AGENTS.md files take precedence over this root guide

## ANTI-PATTERNS (THIS PROJECT)
- **No adding relay/ or mail/ crates to root workspace members** (libsqlite3-sys conflict)
- No type-safety suppression
- No domain types in `maho-core`; use `maho-types`
- No direct FFI/business-logic mixing in `maho-ffi`
- No direct Keychain/Security.framework use in macOS shell
- **NEVER delete `.build.lock`** — see BUILD LOCK below
- **빌드 실패 시 GN 파일 강제 수정 금지** — 빌드 (특히 `build_maho.py`) 가 GN regen 단계 또는 그 이후에서 실패하면 즉시 중단하고 사용자에게 보고. `chromium/src/**/BUILD.gn`, `maho-chromium/**/BUILD.gn`, `chromium/src/chrome/test/BUILD.gn` 등의 GN 파일을 우회 fix 목적으로 직접 수정하지 말 것. 다른 세션의 미완 작업이나 working tree dirty state 인 경우가 많고, 임의 cleanup 은 추후 충돌·revert·데이터 손실을 부른다. fix 가 필요해 보여도 **사용자 명시 승인 없이는 GN 편집·삭제 금지**.

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

# Chromium build
python3 maho-chromium/build/scripts/build_maho.py    # canonical repo-root build

# Chromium-only rebuilds, from chromium/src
autoninja -C out/Default chrome

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
