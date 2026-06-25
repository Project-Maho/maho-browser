# OpenCode Session-Create Runtime Recovery Plan

## Goal

Eliminate the `Failed to create OpenCode session: no response.` failure in the Maho AI panel by hardening the OpenCode session-creation path, covering the adjacent SSE readiness race, tightening runtime readiness handling, and improving diagnostics enough to distinguish transport failure, empty-body failure, and runtime-not-ready failure without guesswork.

## Grounded Findings

1. The exact user-facing failure string is emitted in `maho-chromium/browser/ai/maho_opencode_adapter.cc` inside `MahoOpenCodeAdapter::OnCreateSessionResponse(std::optional<std::string> body)` when `!body || body->empty()`.
2. `MahoOpenCodeAdapter::CreateSession()` issues `POST {base_url}/session` with JSON body `{"title":"Maho AI Session"}`.
3. `MahoAIPageHandler::SubmitPrompt()` already short-circuits with a different error path when `!opencode_adapter_.IsAvailable()`, so the current `no response` message implies the request path got past the basic availability gate. In practice, this points to a narrow race or degraded runtime condition after the availability check rather than the steady-state `runtime not configured` path.
4. `MahoOpenCodeRuntimeService` only exposes the managed endpoint when its state is `kReady`; startup and health signals already exist in `maho_opencode_runtime_service.cc`.
5. Direct `chrome://maho-ai/` navigation redirects into the side panel, so absence of a stable `maho-ai` DevTools target is not a reliable failure signal.
6. macOS Keychain prompts can interfere with runtime QA and may delay or block process startup, so the implementation should prefer log- and state-based diagnostics over UI-only assumptions.

## Non-Goals

- Reworking the AI panel UI flow.
- Changing the OpenCode server API contract.
- Adding fallback to a removed legacy runtime path.
- Fixing unrelated Keychain behavior beyond what is needed for session-create reliability and diagnostics.

## Root-Cause Space This Plan Covers

### Required to Address

1. The adapter treats `nullopt` and empty-body session-create responses as one opaque error.
2. The adapter has no retry/defer strategy for short runtime readiness races.
3. The SSE `GET /event` path is started before session creation and can race or fail independently; that ordering needs to be handled explicitly so a recovered session-create does not leave the adapter disconnected from runtime events.
4. The runtime service and adapter do not provide a crisp handoff proving whether `/session` failed because the service was not actually ready, the process died after readiness, or the server returned an empty body.

### Optional but Strongly Recommended

1. Improve diagnostic fidelity so future failures can be classified from logs and persisted state.
2. Surface richer failure reasons to the page handler/UI without making the user-visible copy noisy.

## Affected Files

| File | Why it matters | Planned change type |
| --- | --- | --- |
| `maho-chromium/browser/ai/maho_opencode_adapter.h` | Declares the session-create path and holds loader/retry state | Required |
| `maho-chromium/browser/ai/maho_opencode_adapter.cc` | Owns `CreateSession()`, `OnCreateSessionResponse()`, prompt submission, and transport error mapping | Required |
| `maho-chromium/browser/ai/maho_opencode_runtime_service.h` | Defines runtime service state machine and public observability surface | Likely required |
| `maho-chromium/browser/ai/maho_opencode_runtime_service.cc` | Owns runtime readiness, launch, and health probing | Likely required |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc` | Bridges adapter failures to Mojo/UI | Optional diagnostic refinement |
| `maho-chromium/browser/ui/webui/maho_ai_prefs.h` | Holds persisted runtime endpoint/model keys if diagnostics need a pref-backed trace | Optional diagnostic refinement |

## Confirmed Constraints From Existing Code

1. HTTP response codes are already observable from `SimpleURLLoader::ResponseInfo()->headers->response_code()`; the runtime service uses this pattern today.
2. `MahoOpenCodeRuntimeService` does not currently expose an observer surface.
3. The adapter can exist long after the runtime service already reached `kReady`, so any readiness design must support both:
   - check-current-state on demand
   - notification of future transitions
4. The adapter keeps callbacks and loaders as members, so any deferred/retry flow must define explicit cleanup on timeout, reset, and destruction.

## Implementation Strategy

### Phase 0 — Preserve existing success semantics while tightening failure handling

#### File: `maho-chromium/browser/ai/maho_opencode_adapter.cc`

1. Document the current ordering in comments before changing it:
   - `SubmitMessage()` arms SSE connection first
   - `CreateSession()` can follow in the same flow
2. Make the new implementation preserve the current success semantics:
   - successful runtime still creates one session
   - original prompt is still submitted once
   - no duplicate user-prompt event should be emitted upstream

### Phase 1 — Make session creation resilient to short readiness races

#### File: `maho-chromium/browser/ai/maho_opencode_adapter.h`

1. Add explicit state for in-flight session-create recovery.
   - `session_create_retry_count_`
   - `session_create_started_at_`
   - `waiting_for_runtime_ready_` or equivalent deferred-create flag
   - retry/defer timer handle
2. Add small, fixed retry policy constants.
   - max retry attempts
   - retry delay
   - optional create-session timeout budget
3. Add private helpers for the new flow.
   - `CanAttemptCreateSessionNow()`
   - `ScheduleCreateSessionRetry()`
   - `FailPendingSessionCreate(...)`
   - `ResetSessionCreateRecoveryState()`

#### File: `maho-chromium/browser/ai/maho_opencode_adapter.cc`

1. Re-check runtime readiness at the start of `CreateSession()` instead of relying solely on the earlier `IsAvailable()` gate in the page handler.
   - If base URL is empty or the managed service is not actually `kReady`, do not fire the request immediately.
   - Defer a bounded retry rather than failing on the first race.
2. Add a bounded retry path for session creation.
   - Retry only the create-session call.
   - Do not duplicate prompt submission if the session was already created.
   - Reset retry counters on success.
3. Use HTTP status code and body-shape information to decide whether retry is appropriate.
   - Retry on transport-style failure (`nullopt`) and clearly retryable server failures.
   - Fail immediately on non-retryable response shapes, including parse failure and valid-but-empty 2xx bodies if they indicate a server-side contract bug rather than a readiness race.
4. Keep the failure contract strict after retries are exhausted.
   - Preserve the existing error event path.
   - Replace the current single opaque message with a more specific final message only when we can prove the reason.
5. Ensure retry/defer state is cleared on `Reset()` and on any path that abandons the current prompt/session attempt.
6. Define explicit callback cleanup.
   - If a deferred session-create attempt is abandoned because of timeout, reset, or destruction, complete `pending_on_accepted_` with a failure and emit the matching runtime error event instead of silently dropping the callback.

### Phase 1.5 — Cover the adjacent SSE race

#### File: `maho-chromium/browser/ai/maho_opencode_adapter.h`

1. Add minimal state needed to re-arm the SSE connection if the initial `GET /event` was attempted too early.

#### File: `maho-chromium/browser/ai/maho_opencode_adapter.cc`

1. Audit `EnsureEventStreamConnected()` behavior when called before the runtime is truly ready.
2. If session creation is deferred or retried because readiness was not stable, ensure SSE connection establishment is retried or re-validated before relying on streamed runtime events.
3. Do not couple SSE retry and session-create retry so tightly that a noisy SSE path can block a valid session-create forever; the goal is to restore a healthy event path once session creation succeeds.

### Phase 2 — Distinguish runtime-not-ready from transport failure

#### File: `maho-chromium/browser/ai/maho_opencode_runtime_service.h`

1. Add a lightweight multi-observer or callback surface.
   - Adapter needs to know when the managed runtime transitions to `kReady` or terminal failure.
   - Multiple adapters may exist across browser windows, so this must support more than one observer.
2. Expose enough state for adapter-side decisions without coupling too much implementation detail.
   - current `State`
   - current managed endpoint
   - current status message

#### File: `maho-chromium/browser/ai/maho_opencode_runtime_service.cc`

1. Emit observer notifications on state transitions.
2. Keep the existing logging strings stable, but make the transition ordering unambiguous.
3. Define the observer contract as check-then-subscribe, not subscribe-and-wait.
   - An adapter created after the service is already `kReady` must still be able to act immediately.
   - A newly registered observer should be able to learn the current terminal/ready state without waiting for another transition.
4. If the process reaches `kReady`, ensure the adapter can immediately flush one deferred session-create attempt.
5. If the service falls into `kBinaryMissing`, `kLaunchFailed`, or `kHealthCheckFailed`, ensure the adapter receives enough information to fail decisively instead of continuing blind retries.

### Phase 3 — Upgrade diagnostics around `POST /session`

#### File: `maho-chromium/browser/ai/maho_opencode_adapter.cc`

1. Before collapsing into the generic `no response` string, capture the strongest available request diagnostics.
   - whether `body` is `nullopt` vs empty string
   - HTTP response code from the loader if present
   - final URL
   - retry attempt number
   - elapsed time since first create-session attempt
2. Add structured `LOG(WARNING)` or `VLOG(1)` diagnostics around create-session start and completion.
3. Keep user-facing messages concise, but make internal logs precise enough to classify:
   - runtime not ready
   - transport failure / no HTTP body
   - empty response body
   - malformed response
   - valid response missing `id` / `sessionId`

### Phase 4 — Refine page-handler/UI error surfacing only if needed

#### File: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`

1. Preserve the current transport of adapter failures into runtime events and prompt callbacks.
2. Optionally normalize richer internal failure reasons into stable UI-facing text buckets.
3. Do not add fallback behavior here; page handler should remain a thin bridge.

#### File: `maho-chromium/browser/ui/webui/maho_ai_prefs.h`

1. Only touch this file if we decide to persist a small diagnostic breadcrumb for last runtime failure classification.
2. Prefer not to add new prefs unless runtime analysis proves persistent breadcrumbs are necessary.

## File-by-File Execution Order

### Step 1
- `maho_opencode_adapter.h`
- `maho_opencode_adapter.cc`

Add recovery state, bounded retry/defer machinery, and cleanup semantics first. This is the smallest required surface to eliminate short readiness races.

### Step 2
- `maho_opencode_runtime_service.h`
- `maho_opencode_runtime_service.cc`

Add explicit adapter-observable readiness/terminal-state signaling only if the adapter cannot correctly resolve readiness from existing APIs.

### Step 3
- `maho_opencode_adapter.cc`

Add log-quality improvements and final failure classification once the retry/defer flow is settled.

### Step 4
- `maho_ai_page_handler.cc`
- optionally `maho_ai_prefs.h`

Only refine the bridge/UI-facing error semantics after the transport behavior is correct.

## Verification Plan

### Unit-Level Verification

#### `maho_opencode_adapter` tests

Add or extend tests to cover:

1. create-session deferred when managed runtime is not yet `kReady`
2. one empty/null response followed by a successful retry
3. repeated empty/null responses exhausting retry budget
4. malformed JSON response still failing immediately with the parse-specific path
5. valid JSON without `id` or `sessionId` preserving the current dedicated error path
6. `Reset()` clearing deferred/retry state cleanly

#### `maho_opencode_runtime_service` tests

Add or extend tests to cover:

1. observer notification ordering across `kStarting` → `kWaitingForHealth` → `kReady`
2. terminal-state notification for `kBinaryMissing`, `kLaunchFailed`, and `kHealthCheckFailed`
3. managed-endpoint availability only when `kReady`

### Runtime Verification

1. Start Maho with logging enabled and reproduce the prior error.
2. Confirm runtime logs show a classified create-session attempt instead of a single opaque failure.
3. Reproduce with a deliberately delayed or unavailable runtime and verify:
   - bounded retry occurs
   - final failure is classified correctly
4. Reproduce with a healthy runtime and verify:
   - session is created successfully
   - no duplicate prompt submission occurs
   - SSE connection is present or is re-established if the first attempt raced readiness
5. Re-run the existing direct runtime checks that inspect:
   - service readiness logs
   - `/session` and `/event` endpoint behavior
   - panel-visible runtime error output

### Additional Unit-Level Verification

1. Service already `kReady` before adapter construction: no deferral, immediate create-session attempt.
2. Multiple adapters observing one runtime service: one adapter teardown must not break notifications to another.
3. Deferred session-create succeeds after readiness transition, and SSE event flow is healthy afterward.
4. Timer cleanup on `Reset()` and destruction: no silent drop of pending callback, no dangling delayed task behavior.

### Manual QA Constraints

1. Account for Keychain interference when choosing the runtime QA environment.
2. Prefer service logs, endpoint checks, and persisted runtime state over UI-only evidence.
3. Treat `chrome://maho-ai` target absence as non-authoritative because of the redirect-to-side-panel behavior.

## Risks

1. Retry logic could hide a real server-side bug if the retry window is too long.
   - Mitigation: keep retries small and bounded; preserve precise logs.
2. Observer wiring between service and adapter can introduce lifecycle bugs.
   - Mitigation: unregister on adapter destruction and keep the surface narrow.
3. Session-create retry could accidentally duplicate work if success/failure boundaries are not explicit.
   - Mitigation: retry only before a session ID is assigned.
4. SSE recovery can lag behind successful session creation and create misleading partial-success states.
   - Mitigation: explicitly verify or re-arm SSE after deferred create-session recovery.
5. Keychain delays can still make runtime verification noisy.
   - Mitigation: base pass/fail on logs and endpoint results, not just visible UI state.

## Optional Diagnostic Enhancements

These are not required for the functional fix, but should be considered if the required changes still leave runtime failures hard to classify.

1. Persist the last classified runtime transport failure for the active session.
2. Add a lightweight internal diagnostics page or log snapshot path for OpenCode runtime state.
3. Include create-session attempt count and last runtime-service state in non-user-facing logs.

## Open Questions To Resolve During Implementation

1. Can the adapter derive all needed readiness state from existing service APIs, or is a new observer surface justified?
2. Should a create-session retry be attempted for both `nullopt` and empty-body responses, or only for transport-style failures and clearly retryable HTTP statuses?
3. What is the smallest reliable SSE recovery rule that avoids masking unrelated stream failures?

## Completion Criteria

The plan is complete when all of the following are true:

1. A session-create request made during a short runtime readiness race no longer fails immediately with the generic `no response` error.
2. Terminal runtime failures produce a specific, stable classification path.
3. Runtime logs are sufficient to distinguish readiness failure from empty/null session-create response.
4. Existing success paths still create a session once and submit the original prompt once.
5. Session-create recovery does not leave the runtime event stream disconnected when the underlying runtime is healthy.
6. Verification covers both managed-endpoint and custom-endpoint behavior where applicable.
