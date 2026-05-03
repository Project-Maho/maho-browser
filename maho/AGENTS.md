# PROJECT KNOWLEDGE BASE

**Generated:** 2026-04-21
**Commit:** unknown
**Branch:** unknown

## OVERVIEW
Hybrid repo: Rust workspace plus native shells and Appium test harnesses. Backend logic lives in Rust crates; platform UI lives in macOS/iOS/Android shell trees. Swift code under the shell directories is reference-only for now; active implementation work is happening in the Chromium fork.

## STRUCTURE
- `crates/` — Rust workspace members (`maho-core`, `maho-ffi`, `maho-storage`, `maho-types`, `maho-extension`, `maho-engine`, `maho-jni`, `maho-bench`)
- `relay/` — Standalone Rust workspace for sync relay server (server, entity, migration crates)
- `macos-shell/` — AppKit shell; existing detailed AGENTS.md already covers local rules
- `ios-shell/` — iOS shell target and views bridge
- `android-shell/` — Android shell Gradle project
- `appium-tests/` — standalone Appium regression suite
- `docs/` — test matrix and parity docs

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Backend browser behavior | `crates/maho-core` | Manager-per-concern architecture |
| Shared types/events | `crates/maho-types` | Contract types only |
| Sync relay server | `relay/` | Standalone workspace; do NOT add to root Cargo.toml members |
| Swift shell work | `macos-shell/`, `ios-shell/` | Reference-only Swift code; active implementation work is in the Chromium fork |
| Android shell work | `android-shell/` | Gradle-based native app |
| Regression automation | `appium-tests/` | Canonical Appium runners |
| Test order / commands | `docs/test-matrix.md` | CI-style run sequence |
| Chromium overlay build / E2E flow | `docs/chromium-fork-build.md` | Verified Chromium overlay setup, build, app bundle, and desktop E2E entrypoints |

## CONVENTIONS
- Use `bun run` for repo scripts and local source files, and `bunx` for one-off package executables or CLIs.
- Rust workspace commands at repo root: `bun run test` and `bun run typecheck` proxy to Cargo.
- Shells are platform-separated; do not assume shared UI code across macOS/iOS/Android.
- Appium tests are repo-local and separate from product code.
- Existing subtree AGENTS.md files take precedence over this root guide.

## ANTI-PATTERNS (THIS PROJECT)
- No type-safety suppression.
- No direct Keychain/Security.framework use in macOS shell code.
- No direct FFI/business-logic mixing in `maho-ffi`.
- No domain types invented in `maho-core`; use `maho-types`.
- No adding relay/ crates to root workspace members (libsqlite3-sys conflict).
- No test-matrix rewrites that hide real failures.

## COMMANDS
```bash
bun run typecheck
bun run test
cd macos-shell && make app
cd macos-shell && make test
cd ios-shell && make test
cd android-shell && ./gradlew app:testDebugUnitTest
cd appium-tests && bun run test
cd relay && cargo test
cd relay && cargo clippy -- -D warnings
cd relay && cargo build --release
```

## NOTES
- `macos-shell/AGENTS.md` already contains the most specific shell rules.
- Prefer the nearest AGENTS.md when working inside a subtree.
- `docs/test-matrix.md` is the canonical execution order.
