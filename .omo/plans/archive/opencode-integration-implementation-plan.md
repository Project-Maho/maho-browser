# OpenCode Integration Implementation Plan

## Goal

Make OpenCode the actual agent runtime for Maho, with Maho serving as the local privileged browser host and the Chromium WebUI becoming a rewritten agent console rather than a chat page.

This plan is about the implementation sequence, not the final feature set. The order matters because the new frontend depends on a stable backend contract, the backend contract depends on early validation of OpenCode transport behavior, and browser privileged actions must stay local in Maho throughout the migration.

## Source of Truth

Use these docs as the architecture baseline for the implementation order and ownership split:

1. `docs/frontend-architecture.md`
2. `maho/docs/backend-architecture.md`
3. `.sisyphus/plans/maho-chat-reference-driven-parallel-plan.md`
4. `.sisyphus/plans/command-palette-swift-parity-plan.md`

The two architecture docs are the primary source of truth. The existing plan docs remain useful style and sequencing references because this plan also needs a phased execution order, clear ownership boundaries, and a browser first migration shape.

## Non-Goals

1. Do not turn this into a task tracker or file by file ticket list.
2. Do not modify any source code in this step.
3. Do not invent unsupported OpenCode capabilities.
4. Do not blur frontend and backend ownership.
5. Do not move browser privileged actions into OpenCode.
6. Do not treat the current Rust chat bridge as the main runtime path for the new console.
7. Do not make the frontend rewrite happen before the backend contract exists.
8. Do not assume OpenCode server mode can suspend and resume around local tool callbacks without validating it first.

## Success Criteria

1. The implementation sequence starts with adapter and contract work, not UI work.
2. OpenCode transport and protocol behavior are validated early, before the rewrite depends on them.
3. The new OpenCode path bypasses the current Rust chat bridge except as a migration fallback.
4. Local browser tools remain in Maho and are never migrated into OpenCode.
5. The backend exposes an explicit event oriented contract for sessions, runs, tools, approvals, and connection state.
6. The frontend rewrite happens only after the backend proxy and contract are in place.
7. Approval flow and session persistence are added after the core event stream path works.
8. If OpenCode server mode cannot suspend and resume around local tool callbacks, the local multi round tool loop is already part of the core event stream phase, not deferred to a later rescue phase.
9. The resulting plan can be executed in order without relying on hidden assumptions about OpenCode internals.
10. Settings migration starts with safe reinterpretation and migration guardrails when the adapter is introduced, then moves to cleanup and renaming after the OpenCode path is stable.

## Review Semantics and Completion Rules

This plan uses two different kinds of PASS, and they must never be confused.

1. Phase PASS means Oracle has verified the scoped phase objective, the seam or contract for that phase, and the evidence for that phase only.
2. Final migration PASS means the real OpenCode runtime path is implemented, active, and verified end to end. Stubbed code, fallback only behavior, or contract only scaffolding do not satisfy this gate.
3. Scaffold completion, seam completion, and UI shell completion are useful checkpoints, but none of them are final completion by themselves.
4. If the adapter still returns unavailable, or if the repo still depends on the legacy Rust chat bridge as the actual runtime, the overall migration is not complete even if one or more phases passed.

### Oracle instructions that apply at every gate

Ask Oracle to review the exact phase scope, not the whole migration by default.

For each phase, Oracle should answer three things:

1. Is the phase scope complete for the stated objective?
2. Is there any evidence that a stub, fallback, or mock is being mistaken for the real OpenCode path?
3. Is the next phase safe to start without reopening the same seam?

Evidence required before a phase can be marked PASS:

1. A concrete implementation artifact, not just a written plan.
2. A runtime or integration proof that matches the phase goal.
3. A short note describing what is still not covered, so phase PASS is not inflated into full migration PASS.

Explicit FAIL conditions at every gate:

1. The implementation is still stubbed or returns unavailable.
2. The legacy Rust chat bridge is doing the real runtime work instead of OpenCode.
3. The scope only proves contract shape, UI shape, or settings shape, but not real runtime behavior.
4. The phase depends on a later phase to become meaningfully real.

## Explicit Architecture Decisions

### 1. OpenCode is the runtime, not just a transport

OpenCode owns reasoning, orchestration, session lifecycle, event emission, and the agent loop.

Maho does not keep a parallel agent brain. It translates between the Chromium side WebUI contract, the local browser privilege boundary, and the OpenCode runtime.

### 2. The frontend talks to a Chromium side proxy, not to OpenCode directly

The WebUI should speak to a browser side proxy over Mojo. That proxy talks to the backend adapter, and the adapter talks to OpenCode.

No direct JS to OpenCode path should be added.

### 3. Browser tools stay local in Maho

Browser privileged tools, page extraction, active tab access, selected text, and other browser state remain in Maho.

The local tool host stays with `MahoBrowserToolExecutor`, `MahoBrowserToolRegistry`, and `MahoAiPageContextExtractor` or their successor seam, not in OpenCode.

### 4. The new OpenCode path bypasses the current Rust chat bridge

The current Rust chat orchestration path is not the main runtime path for the new console.

It can remain only as a migration fallback until the OpenCode adapter is proven.

### 5. The backend contract comes before the full frontend rewrite

The rewritten frontend depends on a stable contract for session list, history, live events, prompt submission, approval responses, cancellation, and reconnection.

That contract must exist before the UI is rebuilt around it.

### 6. Approval flow comes after core streaming

The first goal is a working session and event stream path.

Approval handling should be layered on top of that path, not entangled with the first contract draft.

### 7. Session persistence comes after the live event path exists

Persisted session metadata matters, but it should be added after live streaming and event normalization are stable.

That keeps persistence aligned with the actual event model rather than with a provisional chat shape.

### 8. Transport validation happens early and explicitly

OpenCode server mode behavior around session lifecycle, event streaming, and local tool callback suspension must be validated early.

The integration should not assume a capability that has not been confirmed.

### 9. Fallback is conditional, not a later phase rescue

If OpenCode server mode cannot suspend and resume around local browser tool callbacks, Maho must own the local multi round tool loop as part of the core event stream phase.

That fallback preserves the local browser tool boundary and keeps the migration unblocked.

### 10. Feature gate rollout should use existing prefs or a staged gate

The OpenCode path should roll out behind whatever existing preference or staged gate the repo already uses for AI runtime selection.

The plan does not invent a new product switch here. It just keeps the rollout shape explicit so the old and new paths can coexist during migration.

## Recommended Implementation Shape

The implementation should be built around three stable layers:

1. Chromium WebUI and proxy layer
2. Maho backend adapter and browser tool host layer
3. OpenCode external runtime layer

The browser side proxy and adapter are the key seams. They make the new runtime visible to the frontend without collapsing the browser and runtime responsibilities into one layer.

## Ordered Phases

### Phase 0. Contract and ownership lock

#### Objective

Freeze the runtime boundary before implementation begins so the backend and frontend teams are building toward the same shape.

#### What this phase establishes

1. The UI contract is event oriented, not chat only.
2. The runtime contract is session oriented.
3. Browser tools remain local.
4. OpenCode is the runtime and Maho is the privileged browser host.
5. The old Rust chat bridge is fallback only.

#### Acceptance criteria

1. A single contract outline exists for sessions, events, tool calls, approvals, and connection state.
2. The boundary between OpenCode runtime, Chromium proxy, and Maho browser tools is explicit.
3. The team can point to one agreed path for the new runtime and one agreed fallback path for migration.

#### Oracle review gate for Phase 0

Ask Oracle: "Is the runtime ownership split unambiguous, and does this contract prevent later confusion between phase acceptance and final OpenCode migration completion?"

PASS only if:

1. The contract clearly names the OpenCode runtime, the Chromium side proxy, and the Maho local tool host.
2. The contract explicitly says scaffold or seam work is not the same as a complete migration.
3. The fallback path is described as migration support only, not the default definition of done.

FAIL if:

1. The plan still allows the old bridge to be read as the primary runtime.
2. The contract does not distinguish phase PASS from final PASS.
3. The browser tool boundary is vague enough to leak privileged actions into OpenCode.

---

### Phase 1. Validate OpenCode transport and protocol behavior

#### Objective

Prove the OpenCode integration assumptions before building the rest of the stack on top of them.

#### What this phase must answer

1. Can the chosen OpenCode server mode expose session oriented interaction?
2. Can it deliver event streams in the shape Maho needs?
3. Can it accept tool callback round trips cleanly?
4. Can it suspend and resume around local browser tool execution?
5. What session identifiers, callback identifiers, and reconnect semantics are actually available?
6. Does the preferred callback path require the adapter to own the multi round tool loop, or can OpenCode handle that loop externally without losing the browser tool boundary?

#### Implementation focus

This phase is about validation, not feature breadth.

It should produce a concrete transport decision and a protocol map for the rest of the work.

#### Decision gate before Phase 2

The Phase 1 findings must be translated into one clear adapter contract decision before Phase 2 starts.

1. The adapter contract must reflect the chosen transport and callback model.
2. The adapter contract must say whether the adapter owns the multi round tool loop.
3. The Phase 2 shape must match that decision, not a separate guess.

#### Acceptance criteria

1. OpenCode server mode behavior is documented against the Maho requirements.
2. The team knows which parts of the desired protocol are confirmed and which are not.
3. The suspend and resume question is answered clearly enough to choose the primary tool callback path.
4. If a required capability is missing, the fallback local multi round tool loop is chosen now, not later.
5. The transport and callback decision is explicit enough to lock the adapter contract before implementation begins.

#### Oracle review gate for Phase 1

Ask Oracle: "Is the OpenCode transport and callback evidence strong enough to choose the adapter shape, and does it prove that any pass here is only phase scoped?"

PASS only if:

1. The server mode behavior has been tested or otherwise proven against the required transport questions.
2. The decision on suspend and resume versus local multi round loop is explicit.
3. The evidence records what OpenCode can do, what it cannot do, and what must remain local in Maho.

FAIL if:

1. The answer is just an assumption about OpenCode internals.
2. The transport decision is still hand wavy or reversible without a concrete reason.
3. The result is being treated as final migration completion instead of phase scoped validation.

---

### Phase 2. Build the backend adapter and Chromium proxy contract

#### Objective

Create the stable seam between the WebUI and OpenCode.

#### What this phase must produce

1. A browser side proxy contract for the WebUI.
2. A backend adapter that speaks OpenCode on one side and Maho on the other.
3. Session lookup and creation methods.
4. Session history fetch.
5. Event subscription and live delivery.
6. Prompt submission.
7. Cancellation.
8. Tool result and approval response plumbing.
9. Settings migration guardrails that safely reinterpret existing AI prefs while the adapter is introduced.
10. A rollout shape that uses the existing AI pref or staged gate already in the repo, rather than a brand new hidden switch.

#### Key sequencing rule

This phase must finish before the frontend rewrite begins.

The UI should not be rewritten against a moving backend shape.

#### Acceptance criteria

1. The WebUI has a stable proxy boundary to call.
2. The backend adapter can start or reuse an OpenCode session.
3. The proxy can surface live runtime events to the UI.
4. The contract includes the event categories the console needs, not just a text stream.
5. The OpenCode path is the default implementation behind the feature gate shape already used by the product, with the Rust chat bridge kept only as migration fallback.
6. The adapter contract matches the Phase 1 transport and callback decision, including the multi round tool loop choice.
7. Existing settings keep working through safe reinterpretation rather than a one step rename.

#### Oracle review gate for Phase 2

Ask Oracle: "Does this adapter and proxy seam prove real OpenCode integration, or only a contract shaped shell?"

PASS only if:

1. The adapter can talk to real OpenCode transport, not a stub or unavailable return path.
2. Session creation or reuse is wired through OpenCode semantics.
3. Live event delivery exists end to end through the proxy boundary.
4. Settings are migrated safely enough that old values still resolve to the intended runtime path.

FAIL if:

1. The adapter still returns unavailable, mocked, or placeholder responses.
2. The default runtime path still depends on the legacy chat bridge for actual operation.
3. The phase only proves types, method names, or UI wiring without runtime behavior.

---

### Phase 3. Wire the core event stream path end to end

#### Objective

Get the basic runtime loop working before adding approvals or persistence complexity.

#### What this phase includes

1. User prompt submission through the proxy.
2. OpenCode session creation or reuse.
3. Live assistant or runtime event streaming back to the UI.
4. Tool request emission from OpenCode to Maho.
5. Local browser tool execution in Maho.
6. Tool result return back into the OpenCode session.
7. Completion and error reporting back to the UI.
8. The conditional local multi round tool loop, if Phase 1 shows that suspend and resume is not supported.

#### Why this comes next

The console lives or dies by the event stream. If the session and event path are not solid, approvals and persistence will only amplify the gaps.

This phase also absorbs the fallback tool loop, because the core stream cannot be considered complete until the browser callback strategy is real for the chosen OpenCode transport.

#### Acceptance criteria

1. A session can be started from the UI and tracked through the proxy and adapter.
2. Runtime events arrive in order and can be rendered as a timeline.
3. Local browser tools execute in Maho and return structured results.
4. Tool requests round trip back into the OpenCode session.
5. The UI can show connected, running, paused, completed, and error states from real runtime events.
6. If suspend and resume is unavailable, the local multi round tool loop still produces the same external event shape.

#### Oracle review gate for Phase 3

Ask Oracle: "Does the end to end runtime path actually work with real OpenCode events and real Maho browser tool callbacks?"

PASS only if:

1. A prompt submitted in the UI creates or reuses a real OpenCode session.
2. Events stream back from the runtime in a live sequence that the UI can render.
3. Tool callback round trips pass through Maho browser tools and return into the OpenCode session.
4. The approval related state for the core stream is still separate from the later approval flow phase.

FAIL if:

1. The runtime loop is still simulated, replayed, or mocked.
2. Tool callbacks do not actually cross the Maho browser tool boundary.
3. The plan is being read as full migration completion even though approvals, persistence, or final default path cleanup are not done.

---

### Phase 4. Lightweight frontend shell checkpoint

#### Objective

Smoke test the backend contract from the Chromium side before approvals, persistence, and the full rewrite add more moving parts.

#### What this phase includes

1. A minimal frontend shell that can call the proxy contract.
2. A basic live event render path for the core stream.
3. A simple session start or reconnect path.
4. A checkpoint that confirms the frontend can consume the new contract without assuming the old chat page shape.

#### Why this checkpoint exists

The earlier plan jumped straight from backend streaming to approvals. That left too much risk around frontend timing and contract drift.

This checkpoint keeps the rewrite honest. It proves the shell can talk to the new shape before richer UI states depend on it.

#### Acceptance criteria

1. The shell can start or reconnect to a session through the proxy.
2. The shell can show the core event stream without the old chat assumptions.
3. The shell can exercise the current contract while the UI rewrite is still incomplete.
4. Contract problems surface here, before approval and persistence work layers on more complexity.

#### Oracle review gate for Phase 4

Ask Oracle: "Does the shell prove the contract is consumable from Chromium, without pretending the migration is finished?"

PASS only if:

1. The shell can attach to the adapter contract and render the live stream.
2. The shell still reads as a checkpoint, not the finished console.
3. The evidence explicitly says this phase is scaffolding or seam validation only.

FAIL if:

1. The shell is presented as full completion.
2. The test depends on hidden mock behavior instead of the real proxy contract.
3. The checkpoint hides unresolved runtime limitations.

---

### Phase 5. Add approval flow on top of the event stream

#### Objective

Layer explicit user approval on top of the already working session and event path.

#### What this phase includes

1. Approval request events.
2. Approval response plumbing.
3. Pausing and resuming turns around approval decisions.
4. Clear pending approval state in the proxy and adapter.
5. Deny and timeout handling.

#### Why approval comes here

Approvals make sense only once the core runtime stream exists and the frontend shell has already proven the contract.

If they are added too early, the integration will be forced to solve runtime state, tool callbacks, user decision handling, and UI drift at the same time.

#### Acceptance criteria

1. The UI can render an approval request as part of a live session.
2. The adapter can pause a turn while waiting for approval.
3. Approval responses can resume or cancel the turn.
4. Approval state remains visible in the event history after the decision.
5. The approval behavior is explicit enough to distinguish read only actions from state changing browser actions.

#### Oracle review gate for Phase 5

Ask Oracle: "Does approval handling work against the real runtime stream, and is it clearly separate from final migration completion?"

PASS only if:

1. Approval requests and responses operate on the live OpenCode backed session flow.
2. The pause and resume behavior is visible in runtime state, not just in a UI mock.
3. Approval history persists as part of the event record for the session.

FAIL if:

1. Approval behavior only exists in the frontend without runtime effect.
2. The flow is disconnected from actual OpenCode session state.
3. The phase is being treated as proof that the whole migration is done.

---

### Phase 6. Add session persistence and replayable session state

#### Objective

Persist the session metadata needed to reopen and inspect work across restarts.

#### What this phase includes

1. Session identifiers.
2. Session titles and timestamps.
3. Session list state.
4. Reconnection and restore behavior.
5. Last known runtime status.
6. Replayable event history for the console.

#### Sequencing note

This belongs after the stream path because session persistence should be shaped by real runtime events, not by the old chat model.

#### Acceptance criteria

1. A session can be restored from Chromium owned persistence.
2. The session list can show durable metadata about prior work.
3. The UI can reload and reconnect to the current session without pretending the browser owns runtime state.
4. History is stored in a way that supports inspection and replay.

#### Oracle review gate for Phase 6

Ask Oracle: "Does persistence preserve real session identity and replay data, while still acknowledging that the runtime migration may not yet be fully complete?"

PASS only if:

1. Session metadata survives restart and can be reused.
2. Replay and inspection data correspond to real runtime events.
3. Reconnect behavior is stable enough to continue an existing OpenCode session rather than inventing a new one.

FAIL if:

1. Persistence only stores a UI snapshot.
2. Replay is reconstructed from a fake or incomplete event source.
3. The presence of persistence is mistaken for full OpenCode runtime completion.

---

### Phase 7. Rewrite the frontend around sessions, tools, and approvals

#### Objective

Replace the current chat page with the new OpenCode style console once the backend contract is ready.

#### What this phase includes

1. Session home.
2. Active session view.
3. Event timeline.
4. Tool cards.
5. Approval cards.
6. Inspector area for context, tool details, and session metadata.
7. Composer and session creation flow.

#### Key dependency rule

This phase comes after the backend proxy and adapter exist, and after the lightweight shell checkpoint has already proven the contract.

The rewrite should use the finished contract, not define it.

#### Acceptance criteria

1. The UI is organized around sessions and event timelines, not a thin chat thread.
2. Tool calls, approvals, and runtime state are visible in the main console.
3. The frontend consumes the Chromium side proxy contract without knowing OpenCode internals.
4. The browser privileged boundary still feels local and explicit in the UI.

#### Oracle review gate for Phase 7

Ask Oracle: "Does the rewritten console consume the finished contract, while still depending on the real runtime path underneath?"

PASS only if:

1. The UI is rebuilt around the session and event model established in earlier phases.
2. The console shows live runtime behavior, not only static mock content.
3. The rewritten frontend does not expose privileged browser actions as if they were OpenCode native.

FAIL if:

1. The rewrite only changes presentation without proving runtime fidelity.
2. The UI still assumes the old chat page structure as its core model.
3. This phase is being treated as the point where the migration becomes complete by default.

---

### Phase 8. Reduce legacy assumptions, clean up settings, and retire the chat bridge from the default path

#### Objective

Remove the old chat only assumptions once the OpenCode path is stable.

#### What this phase includes

1. Trimming chat oriented assumptions from orchestration and UI code.
2. Keeping the Rust bridge only where fallback or compatibility still needs it.
3. Removing any lingering direct coupling between the UI and the old transport model.
4. Cleaning up the pref and setting names once the new adapter path has settled.
5. Renaming any migration specific labels that were only needed while the old and new paths overlapped.

#### Acceptance criteria

1. The default runtime path is clearly the OpenCode adapter path.
2. The current Rust chat bridge is no longer the main flow.
3. The remaining compatibility path is intentionally narrow and easy to remove later.
4. No new frontend work depends on the legacy chat shape.
5. Settings naming now matches the stabilized OpenCode runtime model.

#### Oracle review gate for Phase 8

Ask Oracle: "Is the default runtime path really OpenCode now, or is the repo still depending on fallback behavior as the actual product path?"

PASS only if:

1. The default path uses the OpenCode adapter in normal operation.
2. The legacy bridge is clearly fallback only, not the thing that makes the product work.
3. Settings names and migrations no longer hide the runtime choice.

FAIL if:

1. The product still runs mainly through the old bridge.
2. The OpenCode path is only present behind a hidden or unreachable condition.
3. The cleanup is used to claim final completion while the runtime path is still stubbed or fallback only.

---

### Phase 9. Polish replay, inspection, and failure recovery

#### Objective

Make the console trustworthy for real work.

#### What this phase includes

1. Better replay of prior session events.
2. Clearer tool inspection.
3. Better connection state recovery.
4. Better error rendering and retry behavior.
5. Redaction or summarization rules for browser local context where needed.

#### Acceptance criteria

1. Sessions can be reviewed without losing the structure of what happened.
2. Tool output and approvals are inspectable after the fact.
3. Connection failures and runtime errors are easy to understand.
4. The console feels like a durable work surface, not a transient chat panel.

#### Oracle review gate for Phase 9

Ask Oracle: "Is the console trustworthy for real work, and does it still leave final migration validation for the global completion gate?"

PASS only if:

1. Replay, inspection, and recovery behave consistently for real sessions.
2. Error handling is based on actual runtime failure modes.
3. The phase note explicitly says this is polish, not the final migration gate.

FAIL if:

1. The polish relies on synthetic or partial data that hides runtime gaps.
2. The review language implies that polish alone makes the migration complete.
3. The runtime is still stubbed or fallback only.

## Global Completion Gate for the Full Migration

The overall plan is complete only when the real OpenCode runtime path is implemented, active, and verified end to end.

This gate is separate from all phase PASS results. A phase can pass while the migration is still incomplete.

### Ask Oracle this exact question

"Is the full OpenCode migration complete, with the real OpenCode runtime path active by default and all required runtime behaviors proven end to end?"

### Final PASS requires all of the following

1. The OpenCode transport and server integration are real, not stubbed.
2. Session creation and session reuse run through OpenCode in the actual product path.
3. Runtime events stream from OpenCode through Maho to Chromium in real time.
4. Tool callback round trips work through Maho browser tools, including local privileged actions.
5. Approval flows work against the live runtime path, not a simulated adapter.
6. Session persistence and replay reflect the actual runtime model and survive restart.
7. Settings migration is complete enough that the intended runtime path is selected correctly without hidden fallback dependence.
8. The OpenCode path is the default runtime path, and fallback exists only as a narrowly defined compatibility path or during explicit migration handling.
9. The repository no longer depends on the stub OpenCode adapter as the effective behavior.

### Final FAIL conditions

1. `maho_opencode_adapter.cc` still returns unavailable or equivalent stub behavior.
2. The product still relies on the legacy Rust chat bridge as the actual runtime.
3. Any critical runtime behavior is only proven at the seam, not in a real end to end session.
4. The system can show phase PASS results but cannot complete a real OpenCode driven session with tools, approvals, and persistence.

### Final verdict matrix

| Status | Meaning |
|---|---|
| Phase accepted | The specific phase scope passed Oracle review with evidence for that phase only. |
| Migration complete | The real OpenCode runtime path is active by default and all end to end requirements above are satisfied. |
| Not complete because OpenCode path is still stubbed or fallback only | The plan may have phase PASS results, but the product still depends on unavailable, stubbed, or legacy runtime behavior. |

## Execution Order

1. Phase 0, contract and ownership lock.
2. Phase 1, validate OpenCode transport and protocol behavior.
3. Phase 2, build the backend adapter and Chromium proxy contract.
4. Phase 3, wire the core event stream path end to end, including the conditional local fallback loop if Phase 1 requires it.
5. Phase 4, run the lightweight frontend shell checkpoint.
6. Phase 5, add approval flow on top of the event stream.
7. Phase 6, add session persistence and replayable session state.
8. Phase 7, rewrite the frontend around sessions, tools, and approvals.
9. Phase 8, reduce legacy assumptions, clean up settings, and retire the chat bridge from the default path.
10. Phase 9, polish replay, inspection, and failure recovery.

## Why This Order

The backend contract comes first because the UI rewrite needs a stable seam to talk to.

OpenCode transport behavior gets validated early because the rest of the plan depends on whether server mode can really suspend and resume around local browser callbacks, or whether the adapter must own the multi round tool loop.

The core event stream path comes before approvals and persistence because those features only make sense once the runtime lifecycle is already flowing through the proxy and adapter.

The lightweight frontend shell checkpoint sits right after core streaming so the browser side contract gets a real smoke test before approvals and persistence make the UI more complex.

Approvals come after that checkpoint because the UI should first prove it can consume the live stream shape, then layer decision handling on top.

Persistence comes after the stream because durable session state should be shaped by real runtime events, not by the old chat model.

The full frontend rewrite comes after the backend and checkpoint because the new console is supposed to reflect the runtime contract, not invent it.

The fallback path is part of the core stream phase because Maho cannot depend on an unconfirmed OpenCode capability for browser tool round trips.

The Rust chat bridge stays out of the main path because the new system is supposed to use OpenCode as the actual runtime, with the legacy bridge kept only for migration safety.

The settings cleanup stays late because the adapter needs a safe migration surface first, then the names can be tightened once the new path is stable.
