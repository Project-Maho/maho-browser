# Maho AI: ACP Migration Plan (Rust-First)

**Goal:** Replace the current `opencode serve` HTTP+SSE integration with `opencode acp` (Agent Client Protocol over stdio JSON-RPC) as the runtime transport for the Maho AI side panel, with **all ACP wire logic implemented in a new Rust crate** exposed via `maho-ffi`. C++ in the Chromium browser process is reduced to a thin shim that bridges the Mojo seam to the Rust crate.

**Status:** Revised plan. Supersedes the previous C++-adapter plan revision. The previously landed C++ Phase A skeleton (`maho_acp_transport`, `maho_acp_jsonrpc`, `maho_opencode_acp_runtime_service`) is **scheduled for retirement** under this plan; Phase 0 below covers that.

---

## 1. Objective

Switch the Maho AI runtime adapter from HTTP+SSE against `opencode serve` to nd-JSON JSON-RPC against `opencode acp`. The user-facing experience (compact React panel, sessions, streaming assistant tokens, tool calls, approvals, developer mode) must remain functionally equivalent or better. Launch context, lifecycle ownership, and process auth surface must be cleaner than the current `launchctl submit` model.

The architecture must follow the existing Maho monorepo pattern: **heavy logic lives in Rust crates, exposed via `maho-ffi` to a thin Chromium C++ bridge**. ACP transport plumbing is exactly the kind of work that fits that pattern.

## 2. Source of truth

- This file is the authoritative execution plan for the Maho AI runtime transport switch.
- Visual / UX direction continues to be governed by:
  - `.sisyphus/plans/maho-ai-opencode-dia-like-final-plan.md`
  - `.sisyphus/plans/maho-ai-dia-complete-implementation-plan.md`
  - The recently shipped polish + correction waves on `chrome://maho-ai`.
- ACP protocol authority: <https://agentclientprotocol.com> + the `agentclientprotocol/agent-client-protocol` repo.
- OpenCode authority: `opencode-ai/opencode` repo (`packages/opencode/src/cli/cmd/acp.ts`, `packages/opencode/src/acp/agent.ts`, `packages/opencode/src/acp/session.ts`).
- Live wire-format probe: `maho-chromium/browser/resources/maho_ai/standalone/acp_probe.mjs` (PASSed against real `opencode acp` on this workstation; log under `/var/folders/.../maho-ai-acp-probe.log`).

## 3. Foundational commitments

1. **Open-source agent stays.** OpenCode remains the agent runtime. Only the transport changes.
2. **Stdio JSON-RPC nd-JSON is the new transport.** Newline-delimited single JSON-RPC 2.0 objects on stdin/stdout. No HTTP server, no port management, no auth-header injection.
3. **Rust-first.** All ACP wire logic — transport, JSON-RPC, session lifecycle, prompt streaming, tool/permission/error handling — lives in a new Rust crate `maho-acp` under `maho/crates/`. C++ at the Mojo seam acts only as a thin bridge to that crate via `maho-ffi`.
4. **Lifecycle is parent-owned for **graceful** Maho shutdown.** The Rust crate owns the `opencode acp` child via `tokio::process`, and Drop sends SIGKILL on adapter teardown. On **abrupt** parent termination (`kill -9`, panic, OS-level kill), the child may persist briefly as a launchd-reparented orphan because (a) destructors do not run, and (b) opencode acp does not always observe stdin EOF promptly inside the Chromium browser-process FD topology. See §8 for verified live behavior and §11 for follow-up. No `launchctl submit` orphaning either way (we do not use launchctl).
5. **Visual / UX surface unchanged.** The React panel, store shape, Mojo `RuntimeEvent` enum, and on-screen behaviors must remain compatible. Only the underlying transport changes.
6. **Standalone harness preserved.** The standalone React dev loop already works against `opencode acp` via the Node probe; the production-facing standalone bridge will be evolved on top of that. We do not require the standalone bridge itself to be rewritten in Rust as a precondition for production rollout (see §11).
7. **Previous C++ Phase A is retirement-bound.** `maho_acp_transport`, `maho_acp_jsonrpc`, and `maho_opencode_acp_runtime_service` were a useful skeleton for understanding the wire format, but under this plan all of that logic moves to Rust. The C++ files are removed in Phase 0.

## 4. Verified facts (locked from external research and live probe)

| Fact | Source |
|---|---|
| ACP transport is nd-JSON over stdio (`\n`-delimited single JSON-RPC objects, UTF-8). No `Content-Length` framing. | <https://agentclientprotocol.com/protocol/transports> |
| `opencode acp` reads stdin and writes stdout; non-protocol output goes to stderr. | `packages/opencode/src/cli/cmd/acp.ts` |
| Initialization: client sends `initialize` with `protocolVersion: 1` and capabilities; agent responds with its capabilities (`loadSession`, `mcpCapabilities`, `promptCapabilities`, `sessionCapabilities`). | `agent.ts initialize()` + live probe |
| Session lifecycle methods: `session/new`, `session/load`, `session/resume`, `session/close`, `session/list`, `session/fork`, `session/set_mode`, `session/set_config_option`. | `agent.ts` + ACP spec |
| User prompt: `session/prompt` (request, blocking until turn end). Body uses `prompt: [...]` content-block array. Result includes `stopReason ∈ {end_turn, max_tokens, max_turn_requests, refusal, cancelled}` and `usage` (OpenCode extension). | ACP spec + `agent.ts` + live probe |
| Streaming: `session/update` notifications. Discriminator `update.sessionUpdate ∈ {agent_message_chunk, agent_thought_chunk, user_message_chunk, plan, tool_call, tool_call_update, available_commands_update, config_option_update, usage_update}`. | `agent.ts` + ACP spec + live probe |
| `agent_message_chunk.content` is a single object `{type:"text", text:"..."}` — not an array, not a bare string. | live probe |
| `usage_update.cost` is an object `{amount, currency}`, not a scalar. | live probe |
| Permission flow: agent → client request `session/request_permission` with options `[once, always, reject]`; client replies `{outcome:{outcome:"selected", optionId}}` or `{outcome:{outcome:"cancelled"}}`. | `agent.ts` + ACP spec |
| Cancellation: client → agent **notification** `session/cancel`. Pending tool calls flagged cancelled by client; pending permission requests must reply `cancelled`. | ACP spec |
| Errors: standard JSON-RPC `error` object on requests. Tool-level failures arrive as `session/update` with `status: failed`, **not** as JSON-RPC errors. | `agent.ts` + ACP spec |
| Auth: ACP itself carries no API key. OpenCode uses out-of-band `opencode auth login`. The `authMethods` list in `initialize` is informational. | `agent.ts authenticate()` |
| Live end-to-end PASS: `opencode acp` initialize → `session/new` → `session/prompt` → streaming `agent_message_chunk` → final `stopReason: end_turn` → clean child exit. No proxy auth issue (unlike `opencode serve`). | `standalone/acp_probe.mjs` log |

## 5. Non-goals (anti-drift)

1. Do not replace OpenCode with another agent runtime.
2. Do not embed OpenCode as a library; in-process embedding is unsupported.
3. Do not introduce a new persistent local HTTP server. If HTTP-only features are needed, attach to the user's existing `opencode serve`.
4. Do not change React UI structure or store shape beyond what the new event shape strictly requires.
5. Do not delete or rename existing Mojo enums (`RuntimeEventKind`, `SessionInfo`, etc.) — extend if needed, deprecate cleanly.
6. Do not put **AI behavior / orchestration logic** (persona, suggestion shaping, tool authorization policy) in Rust. Only **transport plumbing** belongs in Rust under this plan. AI behavior continues to live where it currently lives in C++ until/unless a separate plan moves it.
7. Do not modify the developer console layout or the recently landed polish CSS as part of this migration. Visual scope is unchanged.
8. Do not regress the panel-width fix, motion + reduced-motion guards, blur token, semantic FG tokens, or the assistant-bubble correction.
9. Do not require the standalone Node bridge to be rewritten in Rust as a blocker for production rollout. The standalone bridge can stay Node (probe-style) for dev iteration.

## 6. Architecture target

### 6.1 Runtime path (production)

```
React (chrome://maho-ai)
    │ Mojo IPC
    ▼
C++ MahoAIPageHandler                                     ← Mojo seam (C++ required)
    │ in-process call
    ▼
C++ MahoOpenCodeAcpAdapter (thin shim)                    ← C++ stays only here
    │ FFI (maho-ffi)
    ▼
Rust crate maho-acp                                       ← all ACP logic here
    │ tokio::process + nd-JSON line codec + serde_json
    ▼
child process: `opencode acp`
transport: stdio (stdin in, stdout out, stderr → log)
```

Key properties:
- All wire-format work, JSON-RPC routing, session state, and event dispatch live in Rust.
- C++ adapter is pure marshalling: it forwards `MahoAiRuntimeAdapter` calls into `maho-ffi`, and routes inbound FFI callbacks back onto Chromium's `base::SequencedTaskRunner`.
- Process lifetime owned by Rust crate via tokio. C++ does not directly touch the child handle.
- No HTTP server, no `launchctl submit`, no port discovery.

### 6.2 Standalone dev path (React harness)

```
React (http://127.0.0.1:4071+)
    │ HTTP / SSE / WS
    ▼
Node bridge (acp_bridge.mjs, evolved from acp_probe.mjs)
    │ child_process.spawn + nd-JSON
    ▼
child process: `opencode acp`
```

The standalone bridge stays Node for now: it already works (probe PASSed), and dev iteration is fastest there. Future work *may* replace it with a thin Rust binary that wraps the same `maho-acp` crate, but that is out of scope for the production rollout.

### 6.3 maho-ffi contract changes

The adapter requires a **callback-streaming** surface. We extend `maho-ffi` with a small set of C-ABI functions:

- Lifecycle: `maho_acp_create()`, `maho_acp_destroy(handle)`, `maho_acp_start(handle, command_path)`, `maho_acp_shutdown(handle)`.
- Calls (all async; results delivered via the event callback): `maho_acp_create_session(handle)`, `maho_acp_submit_prompt(handle, session_id, text, mode)`, `maho_acp_cancel_turn(handle, session_id)`, `maho_acp_reply_permission(handle, request_id, outcome, option_id)`.
- One event callback registration: `maho_acp_set_event_callback(handle, fn_ptr, user_data)`. The callback receives a single tagged-union event encoded as a stable wire format (e.g. CBOR or a length-prefixed JSON blob — pick the simplest stable choice in Phase D). The Rust side guarantees callback invocations are serialized; the C++ side must marshal each callback to the adapter's task runner before touching Chromium state.

Existing maho-ffi patterns are reused where possible. The contract is documented in `maho/crates/maho-ffi/src/acp.rs` (new) and mirrored in `maho-chromium/third_party/maho/maho_ffi.h`.

### 6.4 Mojo / store contract

- Most existing `RuntimeEventKind` values map 1:1 to ACP `session/update` discriminators (same mapping as the previous C++-adapter plan).
- New optional values to add (additive only):
  - `kPlanUpdate` ← `plan`
  - `kAvailableCommandsUpdate` ← `available_commands_update`
  - `kConfigOptionUpdate` ← `config_option_update`
  - `kUsageUpdate` ← `usage_update`
- `SessionInfo.runtimeSessionId` continues to be populated.

### 6.5 Permission flow

- Rust receives `session/request_permission` and forwards it as a `kApprovalRequest` event with the inbound JSON-RPC `id`.
- C++ shim forwards to `MahoAIPageHandler` → React.
- React replies; C++ shim invokes `maho_acp_reply_permission(handle, id, outcome, option_id)`.
- Rust sends the JSON-RPC response on the matching `id`.

### 6.6 Cancellation

- C++ shim invokes `maho_acp_cancel_turn`.
- Rust sends `session/cancel` notification, marks pending tool-calls as cancelled locally, and waits for the original `session/prompt` to resolve with `stopReason: cancelled`.
- That resolution becomes a single `kTurnComplete` event with a cancelled flag. No double-fire.

### 6.7 Auth surface

- We send no custom `Authorization` header; ACP carries none.
- The Rust crate inherits Maho's environment as-is. OpenCode picks up provider credentials from its own config / `opencode auth login` state.
- This eliminates the `hai-proxy.local` auth-context bug that bit us with `opencode serve`.

## 7. Phased migration

Each phase is independently reviewable. Phases 0–7 replace the previous A–F.

### Phase 0 — Retire the C++ ACP skeleton

**Goal:** Remove the previously landed C++-only ACP skeleton so it does not drift or get accidentally wired in.

Atomic steps:
1. Delete the files: `maho-chromium/browser/ai/maho_acp_transport.{h,cc}`, `maho-chromium/browser/ai/maho_acp_jsonrpc.{h,cc}`, `maho-chromium/browser/ai/maho_opencode_acp_runtime_service.{h,cc}`, and the two `_unittest.cc` files alongside them.
2. Remove the corresponding `source_set("maho_opencode_acp_runtime_service")` and `source_set("maho_acp_unittests")` blocks from `maho-chromium/browser/BUILD.gn`.
3. Keep the new pref `maho.ai.runtime_command` (registered in `maho_ai_prefs_registration.cc`) — it will still be used in Phase 5.
4. Confirm `git status` shows only deletions in `maho-chromium/browser/ai/` for the ACP files.

Verification: workspace builds successfully (any pre-existing unrelated build error is documented but not in scope to fix here). LSP shows zero diagnostics for the deleted files.

Exit criteria: no C++ ACP wire-protocol code remains in tree.

### Phase 1 — Rust crate skeleton (`maho-acp`)

**Goal:** Pure-Rust crate that can spawn `opencode acp`, run the `initialize` handshake, and shut down cleanly. No C side yet.

Files (new):
- `maho/crates/maho-acp/Cargo.toml`
- `maho/crates/maho-acp/src/lib.rs`
- `maho/crates/maho-acp/src/transport.rs` (nd-JSON line codec on tokio AsyncRead/AsyncWrite)
- `maho/crates/maho-acp/src/jsonrpc.rs` (JSON-RPC 2.0 router — id correlation, notification/request handlers)
- `maho/crates/maho-acp/src/process.rs` (child spawn + lifecycle)
- `maho/crates/maho-acp/src/types.rs` (typed structs for ACP messages we use, derived via serde)
- `maho/crates/maho-acp/src/client.rs` (high-level client that exposes async `start/initialize/shutdown` first)
- `maho/crates/maho-acp/tests/initialize.rs` (integration test that spawns `opencode acp` if available; skipped otherwise)

Atomic steps:
1. Add the crate to the workspace `Cargo.toml` (root member list; see CLAUDE-flagged repo notes about which workspace gets the entry — this goes in `maho/Cargo.toml`, not the relay or mail workspaces).
2. Implement nd-JSON line codec with the constraints from §4: split on `\n`, reject embedded `\n` in a single decoded line, reject lines >64 KiB, validate UTF-8.
3. Implement JSON-RPC client: outbound id counter, pending-request map, notification handlers map, inbound-request handlers map; default behavior for unhandled inbound requests is JSON-RPC error `-32601`.
4. Implement child spawn with stdin/stdout pipes (`tokio::process::Command`); stderr forwarded to a configurable log sink with a `[acp stderr]` prefix.
5. Implement `initialize` handshake: send the request with `protocolVersion: 1` and the capability set chosen below; assert response `protocolVersion === 1`; cache `agentCapabilities` and `authMethods`.
6. Implement graceful shutdown: close stdin (EOF), wait briefly, escalate SIGTERM, then SIGKILL.

Capability set sent by Rust client:
- `clientCapabilities.fs.readTextFile = true`
- `clientCapabilities.fs.writeTextFile = true`
- `clientCapabilities.terminal = false` (we explicitly do not host a terminal)

Verification:
- `cargo test -p maho-acp` passes for the unit tests (line splitting edge cases, JSON-RPC routing, capability serialization).
- `cargo test -p maho-acp --test initialize -- --ignored` (or env-gated) walks a real `opencode acp` initialize round-trip when the binary is present.
- `cargo clippy -p maho-acp -- -D warnings` clean.

Exit criteria: a Rust crate that can stand up and shut down `opencode acp` cleanly, with `initialize` validated against the live probe shape.

### Phase 2 — Rust adapter: session lifecycle + prompt + events

**Goal:** Extend `maho-acp` to drive a session through one prompt turn end-to-end, with full inbound event handling.

Atomic steps:
1. Implement `session/new` and `session/close`. Cache `sessionId` and the OpenCode-extension fields (`models`, `modes`, `configOptions`).
2. Implement `session/prompt` as a long-running request that resolves with `{stopReason, usage}`. While in flight, dispatch each inbound `session/update` notification to a typed event channel.
3. Parse the full `session/update` discriminator set from §4 into a Rust enum `AcpUpdate`:
   - `AgentMessageChunk { message_id, text }` (decode `content` as `{type:"text", text}` per probe finding)
   - `AgentThoughtChunk { message_id, text }`
   - `UserMessageChunk { message_id, text }`
   - `Plan { entries: Vec<PlanEntry> }`
   - `ToolCall { id, title, kind, status, locations, raw_input }`
   - `ToolCallUpdate { id, status, content_text, raw_output }` (collapse `content` array into a flat text representation; preserve full structured form when needed in a side channel)
   - `AvailableCommandsUpdate { commands }`
   - `ConfigOptionUpdate { options }`
   - `UsageUpdate { used, size, cost: { amount, currency } }`
   - `Other { method, raw_params }` (additive fallback so unknown discriminators don't crash the adapter)
4. Implement inbound `session/request_permission`: surface as a typed event with the inbound `id`; expose a `reply_permission(id, outcome, option_id)` async method.
5. Implement `session/cancel`. Ensure the in-flight `session/prompt` resolves with `stopReason: cancelled` rather than an error.
6. Surface JSON-RPC error responses on request methods as typed errors. Surface tool-failure `session/update` payloads as `ToolCallUpdate { status: Failed, ... }`, not as adapter errors.

Verification:
- `cargo test -p maho-acp` adds tests with a fake JSON-RPC peer (mocked transport) covering each `AcpUpdate` variant, the priority/order of events, cancellation resolution, and permission reply id correlation.
- An end-to-end ignored-by-default test that runs against real `opencode acp` and replicates the probe's PASS scenario, asserting on the typed event sequence (not just the raw stdout).

Exit criteria: pure-Rust client can drive a real prompt turn, observe streaming events, and reply to permission requests, against the live `opencode acp` binary.

### Phase 3 — maho-ffi extension

**Goal:** Expose the Rust adapter through `maho-ffi` as a stable C ABI.

Atomic steps:
1. Add the C-ABI surface listed in §6.3 to `maho/crates/maho-ffi/src/`. Use `cbindgen` (already used by this repo for the existing surface) to regenerate the C header. Verify generated header lands at `maho-chromium/third_party/maho/maho_ffi.h` per the existing convention.
2. Marshal events to the C side as a single tagged union encoded once per event into a stable wire format. Recommended: a length-prefixed `serde_json` blob plus an event-tag enum byte. This avoids exposing every field through the C ABI separately.
3. Guarantee callback ordering and threading: events for a given handle are delivered serially; the C side is responsible for trampolining onto its own task runner before touching Chromium objects.
4. Add a small Rust integration test that drives the FFI surface from a Rust test harness (no C++ involvement): create handle, start, submit prompt, receive event sequence, shutdown.

Verification:
- `cargo test -p maho-ffi` covers the FFI calls and event serialization.
- The generated `maho_ffi.h` builds successfully when included from a minimal C++ test compilation unit (a lightweight `cargo build` script or a small standalone `clang++` invocation in CI). Exact harness TBD; the constraint is "the header is includable by Chromium-style C++ without modification."

Exit criteria: a stable C-ABI surface for the ACP adapter, with no Chromium dependency, exercised by Rust-side tests.

### Phase 4 — C++ thin shim adapter

**Goal:** A new `MahoOpenCodeAcpAdapter` that implements `MahoAiRuntimeAdapter` purely by calling into `maho-ffi`. No wire-protocol code in C++.

Files (new):
- `maho-chromium/browser/ai/maho_opencode_acp_adapter.{h,cc}`
- `maho-chromium/browser/ai/maho_opencode_acp_adapter_unittest.cc`

Atomic steps:
1. Define the C++ class `MahoOpenCodeAcpAdapter : public MahoAiRuntimeAdapter`.
2. On construction, call `maho_acp_create()` to obtain a Rust handle. Register an FFI event callback that bounces events to a `base::SequencedTaskRunner` owned by the adapter, then translates each event into a `MahoAiRuntimeEvent` and dispatches it via the existing `RuntimeEventCallback`.
3. Implement `SubmitMessage`, `CancelCurrentTurn`, and the permission reply path as direct calls into `maho-ffi`.
4. Implement clean teardown: in the destructor, call `maho_acp_shutdown` and `maho_acp_destroy` synchronously.
5. Map `AcpUpdate` variants to existing `MahoAiRuntimeEventType` values, using the additive `kPlanUpdate`/`kAvailableCommandsUpdate`/`kConfigOptionUpdate`/`kUsageUpdate` for the new variants if/when added.
6. Unit-test the adapter against a fake `maho-ffi` (link-time stub) that produces canned event sequences.

Verification:
- LSP diagnostics clean on the new files.
- C++ unit tests exercise translation of every `AcpUpdate` variant into the corresponding Mojo-bound `MahoAiRuntimeEvent`.
- The Chromium build can link the new adapter even when the integration test target is broken by unrelated pre-existing failures (build the source set in isolation as a minimum gate).

Exit criteria: a C++ adapter that is provably correct under unit tests and contains zero ACP wire knowledge — all wire knowledge sits in the Rust crate.

### Phase 5 — Wire into the runtime router behind a pref

**Goal:** Make the ACP adapter selectable via `maho.ai.runtime_transport`. Default remains `"http"` until Phase 6.

Atomic steps:
1. Add the pref `maho.ai.runtime_transport` (string, default `"http"`) in `maho_ai_prefs_registration.cc`. Allowed values: `"http"`, `"acp"`.
2. Update `MahoAiRuntimeRouter::GetActiveAdapter()` to construct the Acp adapter when the pref is `"acp"`, retaining the legacy HTTP adapter otherwise.
3. Construct exactly one adapter at a time. On pref change, rebuild the adapter on the next session start — do not hot-swap mid-turn.
4. Plumb the existing `maho.ai.runtime_command` pref into the Rust crate's `start` call so users can override the binary path or argv.

Verification:
- Adapter selection unit test (mock pref, observe which adapter the router returns).
- Manual QA: set the pref to `"acp"`, restart Maho, confirm a real prompt round-trip from `chrome://maho-ai`.

Exit criteria: the user can opt into the ACP transport via pref. HTTP path is still default.

### Phase 6 — Default flip + lifecycle hardening

**Goal:** Default Maho launches use the Rust ACP adapter. HTTP path remains opt-in for one release as a safety net.

Atomic steps:
1. Default `maho.ai.runtime_transport = "acp"`.
2. Confirm `MahoBrowserMainExtraParts` cleanly tears down the ACP adapter on `PostMainMessageLoopRun`. The Rust crate's `Drop` must SIGTERM and reap the child within a small grace.
3. Crash QA: kill the Maho process; verify no `opencode` child remains. Confirm on macOS arm64 first.
4. Capture cold-start time-to-first-token vs HTTP and confirm no measurable regression.

Verification:
- Open `chrome://maho-ai`, send a prompt, observe streaming, tool calls, approvals, turn complete on a default Maho launch.
- Force-quit / kill-9 of Maho: no orphaned `opencode` child process. No chrome://maho-ai regression on relaunch.

Exit criteria: production default uses ACP via the Rust adapter. HTTP path remains opt-in.

### Phase 7 — Cleanup and decommission

**Goal:** Remove dead code; simplify configuration.

Atomic steps:
1. After one release of soak time on ACP default, remove the legacy `MahoOpenCodeAdapter` (HTTP+SSE) and `MahoOpenCodeRuntimeService`.
2. Remove the `runtime_transport` pref or repurpose it as `runtime_command` only.
3. Update `.sisyphus/plans/maho-ai-opencode-dia-like-final-plan.md` to note that the production transport is ACP via the Rust crate.
4. Update standalone harness README to document both the Node bridge dev path and the Rust crate production path.

Exit criteria: single transport in production, single Rust crate as canonical wire implementation, no parallel HTTP code path to maintain.

## 8. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| ACP wire format drift | Low | Medium | Pin `protocolVersion: 1`. Treat unknown `sessionUpdate` discriminators as additive `Other` events in the Rust enum, never fatal. |
| OpenCode extensions unstable on the wire | Medium | Low | Probe via `agentCapabilities` at `initialize` time. Use only what is advertised. Defensive parsing for `usage_update.cost`, `models`, `modes`, `configOptions`. |
| FFI callback threading mistakes | Medium | High | All inbound FFI callbacks must trampoline through `base::SequencedTaskRunner` before touching Chromium state. Add a debug assertion in the C++ shim that fires if a callback is observed off the expected sequence. |
| `dyld4` deadlock on macOS when spawning `opencode` directly from Chromium (the original reason `launchctl submit` was used for `serve`) | Medium | High | The Rust crate spawns the child with no Chromium dylibs in the child's image; OpenCode itself is a self-contained Bun-built binary. If a deadlock reappears, fall back to a launchctl-style shim **only** for the ACP child while keeping stdio piping. |
| Provider credential context differs between user CLI and Maho-spawned child | Low | High | Inherit Maho's parent environment minus secrets we explicitly redact. Document the env contract. Surface a UI prompt to run `opencode auth login` if `initialize` returns `authMethods` with `id="opencode-login"` and the next `session/prompt` fails with auth error. |
| Permission id correlation bug | Medium | High | Permission id is the inbound JSON-RPC `id`, not OpenCode's internal `permission.id`. Add a Rust-level test that fails if the adapter ever forwards OpenCode's internal id back as the JSON-RPC reply id. |
| Cancellation race between transport error and `cancelled` stop | Medium | Medium | Treat `result.stopReason: cancelled` as authoritative `kTurnComplete`. Suppress duplicate `kError` if a cancelled `kTurnComplete` is already in flight for the same session. |
| nd-JSON edge cases | Medium | Medium | Streaming line buffer with chunked-read tolerance; reject any line containing an embedded `\n` or invalid UTF-8 by closing the connection. |
| Rust ↔ C++ boundary gets ABI-fragile | Low | Medium | Use cbindgen, version the surface, write the FFI integration test in Phase 3 against a Rust-only harness so ABI breakage is caught before Chromium even links it. |
| Visual regression during transport switch | Low | Medium | The CSS surface is untouched. Run live-rendered QA at 520px and 1400px after Phase 6 lands; assistant bubble + row width must match the previous Oracle-PASS state. |
| Tests that need `opencode acp` are flaky in CI without the binary | Medium | Low | Mark integration tests with `#[ignore]` (or `requires_opencode`) and skip cleanly when the binary is absent. The standalone Node probe remains as a lightweight smoke test. |
| `kill -9` of Maho leaves `opencode acp` as a launchd-reparented orphan | Medium | Low | Verified live: 30s+ orphan persistence in Chromium-spawned topology; same opencode binary exits within 5s when parented by a plain Python process via stdin-EOF. The discrepancy points to Chromium helper-process FD inheritance keeping the stdin pipe open beyond Maho main process death. Out of migration scope to fix without touching opencode upstream or Chromium child-process plumbing. Document; revisit after one release of soak. Operationally, `pgrep -fl 'opencode acp'` on Maho relaunch can be used to reap stale orphans. |

## 9. Verification strategy

1. **Rust unit (Phases 1–3):** transport line splitter, JSON-RPC router, event enum coverage, permission id correlation, capability serialization. Pure Rust, no binary dependency.
2. **Rust integration (Phases 1–3, ignored-by-default):** spawn real `opencode acp`, walk one full prompt turn, assert on the typed event sequence. Skipped if `opencode` is absent.
3. **FFI integration (Phase 3):** Rust harness drives the C-ABI surface end-to-end without Chromium.
4. **C++ shim unit (Phase 4):** translation of every `AcpUpdate` variant into the corresponding `MahoAiRuntimeEvent`. Fake `maho-ffi` link-time stub.
5. **Standalone live (any time):** `node maho-chromium/browser/resources/maho_ai/standalone/acp_probe.mjs` continues to be the lightweight smoke test.
6. **Manual QA (Phase 6, mandatory):** open `chrome://maho-ai`, submit a real prompt, observe streaming + tool call + approval + turn complete, light + dark theme, reduced-motion, macOS arm64.
7. **Crash QA (Phase 6):** kill Maho mid-turn, confirm no orphan `opencode` child.
8. **Final review:** Oracle goal-verification + code-quality + security passes on the final diff. Block merge until both verdicts are PASS.

## 10. Rollback plan

- **Phases 0–4:** new code coexists with the legacy HTTP path. Reverting is a one-line pref flip (`runtime_transport = http`) and ignoring the new Rust crate.
- **Phase 5–6:** if ACP default reveals a regression, flip the pref back via config push; the legacy HTTP adapter is still in tree.
- **Phase 7:** only run after a soak release. If a regression appears post-cleanup, the legacy path can be re-imported from git history.

## 11. Open questions / future work

1. **Standalone bridge in Rust?** The current Node bridge `acp_probe.mjs` is the dev-iteration sweet spot, but a pure-Rust bridge would mean **one** wire implementation across dev and production. Out of scope for this plan; revisit after Phase 6.
2. **AI behavior in Rust?** Persona injection, suggestion shaping, and tool authorization policy currently live in C++. They are explicitly out of scope here. A separate plan can move them later if desired.
3. **MCP server passthrough.** `session/new` accepts `mcpServers: [...]`; first-class support is not in scope but the FFI surface should be designed to allow an additional argv parameter without a breaking change.
4. **`kill -9` orphan reaper.** Investigate whether (i) opencode upstream can be persuaded to poll `getppid() == 1` and self-exit, or (ii) the Rust crate should set explicit `FD_CLOEXEC` on the stdin write end so no Chromium descendants can keep the pipe alive after Maho main dies, or (iii) a tiny separate watchdog binary should be introduced. Tracked separately; not blocking the migration.

## 12. Recommended execution order

```
Phase 0  — Retire C++ ACP skeleton                  ← prerequisite, very small
Phase 1  — Rust crate skeleton + initialize         ← foundation
Phase 2  — Rust adapter: session + prompt + events  ← depends on 1
Phase 3  — maho-ffi extension                       ← depends on 1+2
Phase 4  — C++ thin shim                            ← depends on 3
Phase 5  — Router pref                              ← depends on 4
Phase 6  — Default flip + lifecycle hardening       ← depends on 4+5
Phase 7  — Cleanup                                  ← strictly last, after soak
```

Parallelism: Phase 1 is single-track. Phase 2 can split between "session/prompt request path" and "session/update notification dispatcher" tracks. Phase 3 can start once Phase 1's transport+JSON-RPC primitives stabilize, in parallel with Phase 2's later steps. Phase 4 needs Phase 3 done.

## 13. Honesty statement

This plan does not promise feature parity by Phase 1 alone. It promises a Rust-first migration to a non-`serve` transport that:

- Removes the `launchctl submit` / port-discovery / orphan-child failure modes.
- Removes the local-proxy auth context bug we already hit live.
- Keeps the React UI and store contract stable.
- Keeps OpenCode as the agent runtime, unchanged.
- Aligns with the existing Maho monorepo pattern (Rust core, C++ thin shell).

Anything beyond that — visual changes, agent behavior changes, model selection changes — is out of scope for this plan and continues to be governed by the Dia/visual plans already in place.
