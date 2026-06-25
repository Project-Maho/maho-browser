# Maho Mobile + Agent Framework Unified Plan (v3)

*Hyperplan v3: integrated mobile parity + agent framework decision. Plan agent output.*

## Decision Lock (immutable)

| ID | Decision | Rationale |
|----|----------|-----------|
| Q1 | **b — Unified agent runtime** | Same Rust `maho-agent` crate on desktop + mobile. No opencode subprocess. Built on swiftide + rmcp. |
| Q2 | **BYOK via SecureStorage trait** | Platform backends: iOS Keychain, Android Keystore, libsecret/DPAPI. XPC/Binder isolation where available. |
| Q3 | **Web-AI bridge phased** | Phase A (protocol versioning) DONE. Phase B (Chat/ConvoList/SpaceConfig) + C (Privacy/Agent/Pinch) pending. |
| Q4 | **A — iOS restraint kept** | No native AI UI beyond web-ai WebView. Home empty-state cancelled. Privacy via web-ai. |
| Q5 | **Kill criterion: compile spike** | If swiftide fails aarch64-apple-ios-sim OR aarch64-linux-android within W0, fall back to minimal custom runtime (no swiftide dep). |

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  maho-chromium (Desktop)                                │
│  Mojo IPC ──► maho-ffi ──► maho-core ──► maho-agent    │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│  iOS/Android Shell                                      │
│  WebView ──► JS bridge ──► maho-ffi ──► maho-core ──►  │
│              maho-agent (same binary)                   │
└─────────────────────────────────────────────────────────┘

Crate layout:
  maho/crates/maho-agent/     # NEW — AgentRuntime trait + swiftide impl + tool registry
  maho/crates/maho-agent/src/
    lib.rs                    # pub trait AgentRuntime (decouple C5)
    swiftide_backend.rs       # swiftide + rmcp integration
    tool_registry.rs          # platform-divergent tool catalog (C3)
    permission.rs             # ApprovalRequired gate → platform UX callback (C7)
    runtime_isolation.rs      # dedicated tokio::Runtime + per-tool timeout (C1)
```

## Wave 0 — P0 Spikes + Repo Fixups (1-2 weeks, all parallel-safe)

### W0-S1 — swiftide iOS compile spike

- **Goal**: Prove swiftide v0.32 + rmcp v1.7 compile for `aarch64-apple-ios-sim`
- **Bash/command**:
  ```bash
  mkdir -p /tmp/spike-swiftide-ios && cd /tmp/spike-swiftide-ios
  cargo init --lib && cat >> Cargo.toml <<'EOF'
  [lib]
  crate-type = ["staticlib"]
  [dependencies]
  swiftide-agents = "0.32"
  rmcp = { version = "1.7", features = ["client"] }
  tokio = { version = "1", features = ["rt-multi-thread"] }
  EOF
  rustup target add aarch64-apple-ios-sim
  cargo build --target aarch64-apple-ios-sim --release 2>&1 | tee build.log
  ```
- **Success criterion**: `build.log` contains `Finished` with exit code 0
- **Kill criterion**: Linker/compile errors in swiftide or rmcp that require upstream patches → fall back to minimal custom runtime
- **Wall-clock**: 2 days

### W0-S2 — swiftide Android compile spike

- **Goal**: Prove swiftide + rmcp compile for `aarch64-linux-android` via NDK
- **Bash/command**:
  ```bash
  mkdir -p /tmp/spike-swiftide-android && cd /tmp/spike-swiftide-android
  cargo init --lib && cat >> Cargo.toml <<'EOF'
  [lib]
  crate-type = ["cdylib"]
  [dependencies]
  swiftide-agents = "0.32"
  rmcp = { version = "1.7", features = ["client"] }
  tokio = { version = "1", features = ["rt-multi-thread"] }
  EOF
  export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/$(ls $HOME/Library/Android/sdk/ndk | tail -1)"
  cargo ndk -t arm64-v8a build --release 2>&1 | tee build.log
  ```
- **Success criterion**: `build.log` contains `Finished` with exit code 0
- **Kill criterion**: Same as W0-S1
- **Wall-clock**: 2 days

### W0-S3 — AgentRuntime trait abstraction seam (C5 mitigation)

- **Goal**: Design `trait AgentRuntime` interface file so that if W0-S1/S2 fail, we swap implementation without touching callers
- **Bash/command**:
  ```bash
  cd maho && cargo init --lib crates/maho-agent
  # Then write trait with: run_turn, cancel, list_tools, set_permission_callback
  cd maho && cargo check -p maho-agent
  ```
- **Success criterion**: `cargo check -p maho-agent` passes with trait + mock impl, zero coupling to swiftide
- **Kill criterion**: N/A (pure abstraction)
- **Wall-clock**: 1 day

### W0-Q1 — Repo health restoration (current breakages from prior session)

| Fix | Command to verify | Success criterion |
|-----|-------------------|-------------------|
| Add `#include <stdbool.h>` to maho_ffi.h | `cd maho/ios-shell && xcodebuild -scheme Maho -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPhone 17 Pro Max' build 2>&1 \| grep -c error` | Output = `0` |
| Re-export BYOK + Conversation FFI symbols in maho-ffi | `cd maho && cargo build -p maho-ffi --target aarch64-apple-ios-sim` | exit 0 |
| Fix CameraManager.kt camera dependency | `cd maho/android-shell && ./gradlew assembleDebug 2>&1 \| grep -c 'BUILD SUCCESSFUL'` | Output = `1` |
| Add iOS `ChatSessionRegistry.shared` singleton | `grep 'static let shared' maho/ios-shell/Bridge/ChatSessionRegistry.swift` | Match found |
| Write maho-storage conversation round-trip tests | `cd maho && cargo test -p maho-storage -- conversation` | ≥3 tests pass |
| Apply `ffi_catch_unwind!` macro to all 243 exports | `grep -rc 'ffi_catch_unwind\\|ffi_safe!' maho/crates/maho-ffi/src/` | Output ≥ 243 |

**Wall-clock**: 3-5 days (parallelizable across 3 engineers)

## Wave 1 — Core Agent Impl (4-6 weeks)

### W1-T1 — maho-agent crate: SwiftideBackend implementation
- **Goal**: Implement `AgentRuntime` trait backed by swiftide-agents (assumes W0-S1/S2 passed)
- **Depends**: W0-S1, W0-S2, W0-S3
- **TDD**: Write integration test that instantiates runtime, registers a dummy tool, runs one inference turn with a mock LLM provider → asserts tool call + response
- **Success criterion**: `cargo test -p maho-agent -- --include-ignored integration` passes
- **Wall-clock**: 2 weeks

### W1-T2 — Runtime isolation (C1)
- **Goal**: Dedicated `tokio::Runtime` spawned per agent session. Per-tool timeout (30s default, configurable). Panic in tool → graceful error, not process abort.
- **Depends**: W1-T1
- **TDD**: Test that spawns agent, registers a tool that panics → asserts session returns error, process stays alive
- **Success criterion**: `cargo test -p maho-agent -- isolation` all green
- **Wall-clock**: 1 week

### W1-T3 — Platform tool catalog divergence (C3)
- **Goal**: `ToolRegistry` supports `#[cfg(target_os)]` gated tool sets. Desktop gets filesystem/shell tools. Mobile gets camera/photo-library/clipboard only.
- **Depends**: W1-T1
- **TDD**: Compile for ios-sim target → assert `list_tools()` excludes `fs_read`, `shell_exec`
- **Success criterion**: Cross-compile check + unit test passes on both targets
- **Wall-clock**: 1 week

### W1-T4 — Permission callback (C7)
- **Goal**: `set_permission_callback(Box<dyn Fn(PermissionRequest) -> Future<PermissionDecision>>)` that bridges to platform-native UX (Chromium dialog / UIKit sheet / Android bottom-sheet)
- **Depends**: W1-T1
- **TDD**: Mock callback that auto-approves → test passes. Mock that denies → agent turn stops with `PermissionDenied`.
- **Success criterion**: Unit tests pass; FFI export for callback registration compiles for all 3 targets
- **Wall-clock**: 1 week

### W1-T5 — BYOK SecureStorage integration with agent (C2)
- **Goal**: Agent reads API keys via `SecureStorage` trait at inference time. Never holds key in memory longer than one request.
- **Depends**: W0-Q1 (BYOK FFI re-exported), W1-T1
- **TDD**: Test with mock SecureStorage → agent retrieves key, makes inference call, key dropped before next yield
- **Success criterion**: `cargo test -p maho-agent -- byok` passes; ASAN run shows no key retained post-call
- **Wall-clock**: 1 week

### W1-T6 — Conversation history integration (C6)
- **Goal**: maho-agent reads/writes conversation history via maho-storage. Single schema, migration from 3 existing schemas.
- **Depends**: W0-Q1 (conversation tests written), W1-T1
- **TDD**: Migration test: create DB with v1/v2/v3 schema → run migrator → all rows readable via new API
- **Success criterion**: `cargo test -p maho-storage -- migration` passes for all 3 legacy schemas
- **Wall-clock**: 2 weeks

## Wave 2 — Integration + Desktop Migration (4-6 weeks)

### W2-T1 — maho-ffi agent exports
- **Goal**: Expose `maho_agent_create_session`, `maho_agent_send_message`, `maho_agent_cancel`, `maho_agent_list_tools` through FFI with `ffi_catch_unwind!`
- **Depends**: W1-T1, W1-T2
- **TDD**: C-callable integration test (cbindgen-generated header + minimal C driver)
- **Success criterion**: `cargo test -p maho-ffi -- agent` passes; cbindgen generates header without warnings
- **Wall-clock**: 1 week

### W2-T2 — Desktop Mojo bridge swap (C8)
- **Goal**: Replace opencode subprocess calls in `maho-chromium/browser/ai/` with calls through existing Mojo bridge → new maho-ffi agent exports. Keep existing Mojo IDL, change only handler impls (~1556 LOC touched, not deleted).
- **Depends**: W2-T1
- **TDD**: `autoninja -C out/Default unit_tests && out/Default/unit_tests --gtest_filter="MahoAgent*"` passes
- **Success criterion**: Desktop agent chat works end-to-end via Mojo; opencode binary no longer spawned
- **Wall-clock**: 3 weeks

### W2-T3 — Web-AI Phase B (Chat + ConvoList + SpaceConfig)
- **Goal**: Implement T12/T13/T14 from v1 plan. WebView-based AI UI for mobile.
- **Depends**: W0-Q1 (web-ai typecheck baseline), W2-T1 (agent FFI available)
- **TDD**: `cd maho/web-ai && bun run typecheck` passes; Playwright smoke test loads chat screen
- **Success criterion**: TypeScript typecheck 0 errors; manual verification on iOS Simulator shows chat rendering
- **Wall-clock**: 3 weeks

### W2-T4 — Web-AI Phase C (Privacy + Agent Config + Pinch Summary)
- **Goal**: Implement T22 from v1 plan. Privacy dashboard, agent configuration, pinch-to-summarize.
- **Depends**: W2-T3
- **TDD**: Same typecheck gate + Playwright test for privacy panel
- **Success criterion**: Typecheck clean; privacy panel renders user's stored keys (redacted) and active permissions
- **Wall-clock**: 2 weeks

### W2-T5 — iOS + Android native bridge wiring
- **Goal**: Wire maho-ffi agent exports into iOS WebView bridge handlers and Android JNI. Mobile shells can invoke agent.
- **Depends**: W2-T1, W0-Q1 (shells build)
- **TDD**: iOS: `xcodebuild test` with XCTest calling agent create/send. Android: `./gradlew connectedAndroidTest` with agent round-trip.
- **Success criterion**: Platform tests pass on simulator/emulator
- **Wall-clock**: 2 weeks

## Wave 3 — Cutover + Sunset (2-4 weeks)

### W3-T1 — Feature-flag rollout
- **Goal**: Agent runtime behind `maho::features::kMahoAgentV2` flag. Default OFF in first build, ON after 1 week internal dogfood.
- **Depends**: W2-T2, W2-T5
- **Success criterion**: Flag OFF → old behavior (no-op on mobile, opencode on desktop). Flag ON → new unified runtime.
- **Wall-clock**: 2 days

### W3-T2 — Remove opencode subprocess code
- **Goal**: Delete opencode process spawn, IPC parsing, and related test fixtures from maho-chromium.
- **Depends**: W3-T1 (flag ON validated)
- **Success criterion**: `grep -r "opencode" maho-chromium/browser/ai/` returns 0 hits (excluding comments/docs)
- **Wall-clock**: 3 days

### W3-T3 — TestFlight + Play Internal submission
- **Goal**: Submit builds with agent runtime (flag ON) to TestFlight and Play Internal Testing track.
- **Depends**: W2-T5, W3-T1
- **Success criterion**: TestFlight build visible in App Store Connect; Play Internal track shows new version
- **Wall-clock**: 2 days

### W3-T4 — Native-vs-Web boundary rule doc (T5 from v1)
- **Goal**: Document which features are native UI vs web-ai WebView, rationale, and policy for new features.
- **Depends**: W2-T4 (all web-ai phases done)
- **Success criterion**: `docs/architecture/native-vs-web-boundary.md` exists, reviewed, ≥500 words
- **Wall-clock**: 1 day

### W3-T5 — Canonical streaming event shape doc (T15 from v1)
- **Goal**: Document the streaming event protocol between maho-agent → FFI → WebView for real-time token display.
- **Depends**: W2-T3
- **Success criterion**: `docs/architecture/streaming-event-protocol.md` exists with JSON schema + sequence diagram
- **Wall-clock**: 1 day

## Atomic Commit Strategy

| Rule | Description |
|------|-------------|
| One crate per commit | New crate creation = 1 commit (e.g., `feat(maho-agent): scaffold AgentRuntime trait`) |
| Tests before impl | Test file committed first (red), then impl (green), then refactor — may squash to single commit |
| FFI boundary = own commit | Any change to maho-ffi exports gets isolated commit for easy revert |
| Platform shell changes = per-platform | iOS fix ≠ Android fix; never combined |
| Web-AI phases = per-screen | Each screen (chat/convolist/config/privacy) = own commit |
| Flag changes = own commit | Feature flag toggle is always atomic and revertable |
| Docs = own commit | Never bundled with code changes |

Commit message format: `<type>(<scope>): <description>` where type ∈ {feat, fix, test, docs, refactor, chore}.

## Total Estimate

- **Optimistic**: 4 months (aggressive parallelism, spikes pass first try)
- **Realistic**: 6 months (one spike fails → custom runtime fallback adds 3 weeks)
- **Pessimistic**: 8 months (swiftide upstream issues + conversation migration edge cases)

**Kill criteria (abort entire agent effort)**:
1. Both W0-S1 AND W0-S2 fail AND custom minimal runtime exceeds 4 weeks to stabilize
2. swiftide pre-1.0 breaking changes exceed 2 within the Wave 1 window
3. Memory overhead of agent runtime on mobile exceeds 50MB RSS idle

## Open Risks Not Yet Mitigated

1. **swiftide pre-1.0 API churn** — Trait seam (C5) limits blast radius, but major version bump mid-Wave-1 could force 2-week rework
2. **MCP server ecosystem assumes stdio** — Mobile tool catalog is custom-only until rmcp adds in-process transport (tracked: rmcp#247)
3. **Conversation history migration data loss** — 3 schemas with potentially incompatible attachment references; need user-facing "migration failed" recovery path
4. **CI matrix explosion** — Adding aarch64-apple-ios-sim + aarch64-linux-android to CI doubles build time; may need tiered CI (fast=host-only, nightly=all-targets)
5. **Permission UX latency** — Native permission dialog round-trip (FFI→UI→FFI) adds ~200-500ms to first tool call; may feel sluggish vs desktop instant-approve

## TODO List (for execution by next session)

### Wave 0 (Start Immediately - No Dependencies, All Parallel)

- [ ] **W0-S1**: swiftide iOS compile spike — depends none, blocks W1-T1..T5, category=`deep`
- [ ] **W0-S2**: swiftide Android compile spike — depends none, blocks W1-T1..T5, category=`deep`
- [ ] **W0-S3**: AgentRuntime trait abstraction — depends none, blocks W1-T1, category=`small-impl`
- [ ] **W0-Q1a**: Fix maho_ffi.h stdbool include — depends none, blocks W2-T5, category=`quick`
- [ ] **W0-Q1b**: Re-export BYOK + Conversation FFI symbols — depends none, blocks W1-T5, W1-T6, W2-T5, category=`small-impl`
- [ ] **W0-Q1c**: Fix Android CameraManager.kt — depends none, blocks W2-T5, category=`quick`
- [ ] **W0-Q1d**: iOS ChatSessionRegistry.shared singleton — depends none, blocks W2-T5, category=`quick`
- [ ] **W0-Q1e**: maho-storage conversation tests — depends none, blocks W1-T6, category=`small-impl`
- [ ] **W0-Q1f**: Apply ffi_catch_unwind! macro to all 243 exports — depends none, blocks W2-T1, category=`opus-impl`

### Wave 1 (After Wave 0 Spikes Pass)

- [ ] **W1-T1**: SwiftideBackend implementation — depends W0-S1+S2+S3, blocks W1-T2..T5+W2-T1, category=`opus-impl`
- [ ] **W1-T2**: Runtime isolation — depends W1-T1, blocks W2-T1, category=`deep`
- [ ] **W1-T3**: Platform tool catalog — depends W1-T1, blocks W2-T1, category=`small-impl`
- [ ] **W1-T4**: Permission callback — depends W1-T1, blocks W2-T2+T5, category=`small-impl`
- [ ] **W1-T5**: BYOK SecureStorage agent integration — depends W0-Q1b+W1-T1, blocks W2-T2, category=`small-impl`
- [ ] **W1-T6**: Conversation history migration — depends W0-Q1e+W1-T1, blocks W2-T3, category=`deep`

### Wave 2 (After Wave 1 Core)

- [ ] **W2-T1**: maho-ffi agent exports — depends W1-T1+T2+T3, blocks W2-T2+T3+T5, category=`small-impl`
- [ ] **W2-T2**: Desktop Mojo bridge swap — depends W2-T1+W1-T4+T5, blocks W3-T1+T2, category=`opus-impl`
- [ ] **W2-T3**: Web-AI Phase B — depends W2-T1+W1-T6, blocks W2-T4+W3-T5, category=`opus-impl`, skills=[`frontend-ui-ux`]
- [ ] **W2-T4**: Web-AI Phase C — depends W2-T3, blocks W3-T4, category=`opus-impl`, skills=[`frontend-ui-ux`]
- [ ] **W2-T5**: iOS + Android native bridge wiring — depends W2-T1+W0-Q1a+Q1c+Q1d, blocks W3-T1+T3, category=`opus-impl`

### Wave 3 (Cutover)

- [ ] **W3-T1**: Feature-flag rollout — depends W2-T2+T5, blocks W3-T2+T3, category=`quick`
- [ ] **W3-T2**: Remove opencode subprocess — depends W3-T1, category=`small-impl`, skills=[`git-master`]
- [ ] **W3-T3**: TestFlight + Play Internal submission — depends W3-T1, category=`quick`
- [ ] **W3-T4**: Native-vs-Web boundary doc — depends W2-T4, category=`writing`
- [ ] **W3-T5**: Streaming event protocol doc — depends W2-T3, category=`writing`

## Execution Instructions

1. **Wave 0**: Fire ALL 9 tasks in parallel (no dependencies between them)
2. **Gate check**: If W0-S1 OR W0-S2 fails → trigger kill criterion evaluation. If both fail → abort swiftide, redesign with custom runtime (add 3-week buffer)
3. **Wave 1**: After Wave 0 passes, fire W1-T1 first (all others depend on it), then W1-T2/T3/T4/T5/T6 in parallel
4. **Wave 2**: W2-T1 first, then W2-T2/T3/T5 in parallel, then W2-T4
5. **Wave 3**: W3-T1 first, then W3-T2/T3/T4/T5 in parallel
6. **Final QA**: All success criteria verified + `cargo check --workspace` clean + both shells build

---

*Source: Hyperplan v3 plan agent dispatch on session 2026-06-21. Integrates Hyperplan v1 mobile parity plan (T0-T22) and Hyperplan v2 agent framework decision (Q1=b unified runtime). All Hard Constraints C1-C8 and User Gates Q1-Q5 honored.*
