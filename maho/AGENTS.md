# PROJECT KNOWLEDGE BASE

**Generated:** 2026-07-14
**Commit:** 4fd48142
**Branch:** main

## OVERVIEW
Hybrid repo: Rust workspace plus native shells and Appium test harnesses. Backend logic lives in Rust crates; platform UI lives in macOS/iOS/Android shell trees. Swift code under the shell directories is reference-only for now; active implementation work is happening in the Chromium fork.

## STRUCTURE
- `crates/` — Rust workspace members (`maho-core`, `maho-ffi`, `maho-storage`, `maho-types`, `maho-extension`, `maho-engine`, `maho-jni`, `maho-bench`)
- `relay/` — Standalone Rust workspace for sync relay server (Cloudflare Workers WASM; see `relay/AGENTS.md`)
- `web-ai/` — Unified AI WebView bundle (Preact SPA for iOS + Android + Desktop; see `web-ai/AGENTS.md`)
- `ios-shell/` — iOS shell target and views bridge
- `android-shell/` — Android shell Gradle project
- `appium-tests/` — standalone Appium regression suite
- `docs/` — test matrix and parity docs

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Backend browser behavior | `crates/maho-core` | Manager-per-concern architecture |
| Shared types/events | `crates/maho-types` | Contract types only |
| Sync relay server | `relay/` | Standalone workspace; do NOT add to root Cargo.toml members. See `relay/AGENTS.md` |
| Swift shell work | `ios-shell/` | Reference-only Swift code; active implementation work is in the Chromium fork |
| Android shell work | `android-shell/` | Gradle-based native app |
| Regression automation | `appium-tests/` | Canonical Appium runners |
| Test order / commands | `docs/test-matrix.md` | CI-style run sequence |
| Chromium overlay build / E2E flow | `docs/chromium-fork-build.md` | Standard Chromium overlay setup, build, app bundle, and desktop E2E entrypoints; canonical build wrapper is `python3 maho-chromium/build/scripts/build_maho.py` from the workspace root |

## CONVENTIONS
- Use `bun run` for repo scripts and local source files, and `bunx` for one-off package executables or CLIs.
- Rust workspace commands at repo root: `bun run test` and `bun run typecheck` proxy to Cargo.
- These shared-artifact entrypoints use the workspace root `.build.lock`, so `bun run test` and `bun run typecheck` may wait and print `Waiting for build lock: ...`.
- Shells are platform-separated; do not assume shared UI code across macOS/iOS/Android.
- Appium tests are repo-local and separate from product code; excluded from `bun run test` via `bunfig.toml`.
- **No `.unwrap()` / `.expect()` in non-test Rust code** — `clippy.toml` denies `unwrap`/`expect` workspace-wide.
- `swiftide-core` is patched to a local vendor at `third_party/swiftide-core/` — do NOT point to crates.io.
- `.cargo/config.toml` has machine-local Android NDK linker paths — do not commit path changes.
- Existing subtree AGENTS.md files take precedence over this root guide.

## ANTI-PATTERNS (THIS PROJECT)
- No type-safety suppression.
- No direct FFI/business-logic mixing in `maho-ffi`.
- No domain types invented in `maho-core`; use `maho-types`.
- No adding relay/ crates to root workspace members (libsqlite3-sys conflict).
- No test-matrix rewrites that hide real failures.

## COMMANDS
```bash
bun run typecheck
bun run test
cd ios-shell && make test
cd android-shell && ./gradlew app:testDebugUnitTest
cd appium-tests && bun run test
cd relay && cargo test
cd relay && cargo clippy -- -D warnings
cd relay && cargo build --release
```

## CHROMIUM BUILD

Use the Chromium overlay from `chromium/src`.

Standard build from the workspace root:

```bash
python3 maho-chromium/build/scripts/build_maho.py
```

This produces `chromium/src/out/Default/Maho.app`.

`build_maho.py` holds the shared `.build.lock`, so builds SERIALIZE across agents/sessions.
This is intentional: concurrent Chromium builds on one machine exhaust RAM (tens of GB of
swap) and make ALL builds slower — one heavy build at a time is faster than several thrashing.

For a narrower/faster rebuild, keep the lock and pass a narrow target (do NOT run raw
`autoninja -C out/Default`, which bypasses the lock):

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target <target>
```

Build the NARROWEST target that verifies your change — avoid full `unit_tests` /
`interactive_ui_tests` / `browser_tests` unless you must run that whole suite.

## NOTES
- Prefer the nearest AGENTS.md when working inside a subtree.
- `docs/test-matrix.md` is the canonical execution order.
- `relay/` is a separate Cargo workspace (Cloudflare Workers WASM); `relay/AGENTS.md` covers routes, auth, billing, sync, and deploy.
- `web-ai/` is a Preact SPA bundle loaded by `WKWebView` (iOS), `WebView` (Android), and `chrome://maho-ai` (Desktop); `web-ai/AGENTS.md` covers screens, bridge protocol, and build.
