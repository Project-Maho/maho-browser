# PROJECT KNOWLEDGE BASE

**Generated:** 2026-05-05
**Commit:** e2e5fbf
**Branch:** main

## OVERVIEW
Maho Browser monorepo: Rust core crates + Chromium overlay fork + native shells (macOS/iOS/Android). Brave-style architecture — `maho-chromium` patches upstream Chromium, `maho` crates provide backend logic via FFI bridge.

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
| Upstream overrides | `maho-chromium/chromium_src/` | Brave-style #define + #include |
| Sync relay server | `maho/relay/` | Standalone workspace; do NOT add to root Cargo.toml |
| Build Chromium | `chromium/src/out/Default/` | `autoninja -C out/Default chrome` → `Maho.app` |
| Build maho-core lib | `maho-chromium/build/scripts/build_maho_core.py` | Produces prebuilt static lib |
| Regression tests | `maho/appium-tests/` | Appium runners |
| Native shells (ref) | `maho/macos-shell/`, `maho/ios-shell/` | Reference only; active work in Chromium fork |

## CONVENTIONS
- `bun run` for repo scripts, `bunx` for one-off CLIs
- `bun run test` / `bun run typecheck` proxy to Cargo at maho/ root
- Repo-provided build/test wrappers serialize through the workspace root `.build.lock`, so `bun run test` and `bun run typecheck` may pause and print `Waiting for build lock: ...` when another lock-backed step is active
- Chromium C++: explicit includes, `DCHECK_CALLED_ON_VALID_SEQUENCE`, namespace-local helpers
- Mojo handlers are thin — delegate to helpers in `browser/ai/` or `browser/ui/`
- WebUI pattern: each feature has `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` + `resources/{feature}/app.ts`
- Subtree AGENTS.md files take precedence over this root guide

## ANTI-PATTERNS (THIS PROJECT)
- No adding relay/ crates to root workspace members (libsqlite3-sys conflict)
- No type-safety suppression
- No domain types in `maho-core`; use `maho-types`
- No direct FFI/business-logic mixing in `maho-ffi`
- No direct Keychain/Security.framework use in macOS shell

## COMMANDS
```bash
# Rust workspace
cd maho && bun run typecheck          # cargo check --workspace
cd maho && bun run test               # cargo test --workspace

# Chromium build
autoninja -C chromium/src/out/Default chrome    # → Maho.app
# If autoninja not on PATH:
PATH="chromium/src/third_party/depot_tools:$PATH" autoninja -C chromium/src/out/Default chrome

# Build maho-core static lib for Chromium
python3 maho-chromium/build/scripts/build_maho_core.py

# Relay server
cd maho/relay && cargo test && cargo clippy -- -D warnings

# Native shells
cd maho/macos-shell && make app
cd maho/appium-tests && bun run test
```

## NOTES
- `chromium/` is gitignored (121GB upstream); managed by gclient, not this repo's git
- Symlink setup: `ln -s ../../maho-chromium chromium/src/maho`
- Two Cargo workspaces: `maho/Cargo.toml` (main) and `maho/relay/Cargo.toml` (isolated)
- See `maho/AGENTS.md` for crate-level detail; `maho-chromium/AGENTS.md` for overlay detail
