# Maho AI frontend architecture

## Purpose

This document describes the frontend architecture for a full rewrite of the Maho AI UI in `maho-chromium`. The target experience is an OpenCode native agent console, not a thin chat page. OpenCode should be the real agent runtime and server. Maho should provide the browser side shell, local browser context, and privileged browser actions through a backend bridge.

The current UI in `maho-chromium/browser/resources/maho_ai/app.ts` is a simple page chat surface with a page preview and a small Mojo API in `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`. That contract should be treated as replaceable. The new frontend should be designed around sessions, tool calls, approvals, event streams, and run state from the start.

## Current repo context

The rewrite belongs in the existing Chromium overlay structure under `maho-chromium`, with the UI still served from a WebUI surface. The important current files are:

1. `maho-chromium/browser/resources/maho_ai/app.ts`
2. `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
3. `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`
4. `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`

Today, those files support a chat flow with page content extraction, a short message history, streaming text, and API key storage. That is useful as a reference point, but it is not the target architecture.

The broader repository already uses a split between browser logic in `browser/ui/webui`, browser intelligence helpers in `browser/ai`, and WebUI resources in `browser/resources`. The new AI frontend should fit that pattern, but the actual interaction model should no longer be constrained by the current chat oriented Mojom contract.

## Goals

1. Make OpenCode the actual agent runtime and server for Maho AI.
2. Present an agent console that makes sessions, runs, tool calls, approvals, and event streams visible to users.
3. Keep browser privileged capabilities local to Maho, and expose them through a backend bridge rather than by moving them into the frontend.
4. Support a richer working model than plain chat, including multi step runs, intermediate tool output, and explicit approval moments.
5. Keep the frontend replaceable and implementation ready, so product, design, and engineering can reason about the same screen model.
6. Make the architecture suitable for a full UI rewrite, not a gradual patch on top of the current chat page.

## Non goals

1. Recreating the current chat UI with minor visual polish.
2. Locking the future frontend to the existing `maho_ai.mojom` surface.
3. Moving browser privileged actions into OpenCode or into the browser UI layer without a bridge.
4. Building every possible OpenCode feature on day one. The target is the core console experience first.
5. Designing a generic agent platform spec. This document is specific to Maho and the current Chromium overlay architecture.

## Product shape

The new Maho AI surface should feel like an operator console for an agent run, not a messenger thread. A user should be able to enter a task, watch the agent think and act, inspect tool usage, approve privileged steps, and revisit past sessions.

OpenCode specific concepts that need first class UI support are:

1. Sessions, as durable containers for work.
2. Tool calls, as visible units of agent action.
3. Approvals, as explicit user decisions before privileged operations.
4. Event streams, as the live record of what the runtime is doing.
5. Runs or turns, as the grouped execution cycle inside a session.

## Information architecture and screen model

The frontend should be organized around a small set of stable surfaces.

### 1. Session home

This is the landing state for `chrome://maho-ai`.

It should show:

1. Recent sessions.
2. A clear entry point for creating a new session.
3. Any pinned or in progress session if the product wants to keep one active workspace.
4. A light summary of the connected runtime state, such as whether OpenCode is reachable and whether local browser bridge services are available.

This screen is not a chat inbox. It is a workspace chooser.

### 2. Active session view

This is the main console.

Recommended layout:

1. Left rail, session list and session search.
2. Main center column, conversation and event timeline.
3. Right inspector, context, tool details, approval details, and session metadata.
4. Bottom composer, prompt input, attachments, and action shortcuts.

This layout keeps the primary work area focused on live agent activity while still making it possible to inspect what the agent is doing. If the product stays in a narrow sidebar sized surface, the timeline, inspector, and approval cards will fight for space. That would make the console feel cramped fast. The target layout should assume a full height page or a wide drawer, not a thin panel.

### 3. Approval surface

Approvals should appear as a persistent in flow card in the timeline, with a stronger modal or drawer only when the action is risky or destructive.

The user should never have to infer that a browser privileged action is waiting for approval. The request must be explicit and actionable.

### 4. Session detail states

A session view needs distinct states, not just a blank conversation.

1. Loading session history.
2. Connected and live.
3. Replaying prior events.
4. Waiting for approval.
5. Tool running.
6. Error or disconnected.
7. Completed or idle.

## State model

The frontend state should be normalized around session and event entities, not only around message text.

### Core entities

1. Session.
   Contains session id, title, created time, updated time, status, summary, and runtime connection state.

2. Message.
   Represents user, assistant, or system text within a session.

3. Event.
   Represents any runtime event, including token streaming, tool start, tool progress, tool result, approval request, approval decision, error, and completion.

4. Tool call.
   Represents a single invocation, such as page context capture, browser action, file lookup, or any other OpenCode tool.

5. Approval request.
   Represents a pending decision with a scope, rationale, diff or preview, and a response.

6. Context source.
   Represents the browser state that Maho contributes, such as the active page, selected tab, current URL, page text, or other local browser data.

### Suggested frontend store shape

The UI should track state in a store that can answer these questions quickly:

1. What session is active?
2. What events belong to this session?
3. Which tool call is currently live?
4. Which approval, if any, is pending?
5. What browser context is attached to the current run?
6. What is the live connection status to the backend bridge and OpenCode runtime?

This makes it easier to render a timeline, an inspector, and a composer without duplicating logic in the DOM.

### State ownership

Session metadata and UI history should live in Chromium owned persistence, not in transient runtime state. That includes session ids, titles, timestamps, pinning, the last known status, and the local list of sessions that the user can reopen.

Runtime state should stay with the backend adapter and OpenCode session. That includes live request ids, streaming tokens, pending tool callbacks, approval deadlines, replay cursors, and other short lived execution details.

The frontend should rehydrate from stored session metadata, then subscribe back into the live runtime stream for the current session. That split keeps the UI reloadable without pretending the browser is the source of truth for runtime execution.

### State principles

1. Append only events, then derive views from them.
2. Keep raw runtime events separate from presentation state.
3. Normalize tool output and approval data so the UI can re render from history.
4. Treat streaming text as one kind of event, not as the entire state model.
5. Preserve enough metadata for replay and debugging.

## Event model

The current UI in `app.ts` only understands a user message, streaming assistant text, and a final assistant message. The new frontend should instead render a richer event log.

### Required event categories

1. Session lifecycle, such as created, resumed, closed, or restored.
2. User input, such as prompt submitted or attachment added.
3. Assistant stream, such as reasoning or response chunks if the runtime exposes them.
4. Tool execution, such as start, progress, output, and completion.
5. Approval flow, such as requested, granted, denied, timed out, or revoked.
6. Connection state, such as connected, disconnected, or retrying.
7. Error and completion, such as runtime failed, run completed, or run cancelled.

### Rendering rule

Every event should have a presentation card or row. Some can collapse visually, but they should still exist in the timeline so users can understand what happened during a run.

This is important for OpenCode because the product value is not only the final answer. It is also the visible process of the agent.

## Session UX

Session UX should make it easy to return to work, not just start new work.

### Session list

The session list should support:

1. Search by title or content.
2. Recency sorting.
3. Session status indicators, such as active, paused, failed, or completed.
4. Optional grouping by day or workspace context.

### Session header

The active session header should show:

1. Session title.
2. Runtime connection status.
3. Last meaningful event.
4. Active approval count, if any.
5. Entry points for session settings and export or copy actions if those are supported later.

### Session creation

Creating a new session should feel like starting a task, not opening a blank chat.

The create flow should allow:

1. A task prompt.
2. Optional browser context attachment.
3. Optional mode selection if OpenCode exposes modes.
4. A clear start action that creates a new session and begins the first run.

## Streaming and tool timeline UX

This is the most important part of the new console.

### Timeline structure

The center column should present a single chronological feed with these nested patterns:

1. User prompt card.
2. Assistant thinking or planning card.
3. Tool call card.
4. Tool output expansion.
5. Approval card.
6. Final response card.

### Streaming behavior

The UI should not wait for the final answer before showing useful information.

It should stream:

1. Assistant text chunks.
2. Tool start markers.
3. Tool progress or partial results.
4. Explicit pauses when the runtime needs approval.

### Tool call card expectations

Each tool card should show:

1. Tool name.
2. Tool status, such as running, waiting, done, or failed.
3. Short summary of what the tool is doing.
4. Input or arguments in a structured, inspectable form.
5. Output, with collapse and expand behavior for long content.
6. Any associated approval requirements.

### Why this matters

The current `app.ts` UI only appends assistant text and a final message. That is too flat for OpenCode. Users need to understand when the agent is thinking, when it is calling a tool, and when it is blocked on a decision.

## Approval UX

Approvals are a first class workflow, not a toast.

### Approval card content

An approval request should include:

1. What the agent wants to do.
2. Why it wants to do it.
3. What scope it affects.
4. A preview of the change or action if available.
5. Whether the action is reversible.
6. Approve and deny actions.

### Approval naming

Use one consistent set of names in the UI and contract: `approval_requested`, `approval_granted`, `approval_denied`, and `approval_timed_out`. Avoid alternate labels that mean the same thing, because that makes the stream hard to replay and harder to test.

### UX rules

1. The default state must be safe and clear.
2. The user should understand the consequence before approving.
3. Denial should be easy and not destructive.
4. Approvals should remain visible in the timeline after the decision.
5. High risk actions should get stronger visual emphasis than low risk ones.

### Maho specific note

Browser privileged operations remain local to Maho. The approval UI should make it obvious when an action crosses from agent reasoning into browser control or page manipulation.

## Backend contract expectations

The frontend should be built against a backend contract that reflects OpenCode as the runtime and Maho as the browser bridge.

### Transport decision

The WebUI should not talk to OpenCode directly. OpenCode is an external runtime and server, so the browser side UI should talk to a Chromium side proxy over Mojo. That proxy can live in `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc` and related browser code, and it should translate between WebUI requests and the backend adapter.

That proxy is the frontend transport boundary. The JavaScript app talks to Mojo, the browser side proxy talks to the backend adapter, and the adapter talks to OpenCode. No direct JS to OpenCode path should be added.

### Runtime side expectations

OpenCode should provide or expose, at minimum, a session oriented runtime surface that can carry these kinds of data:

1. Session lifecycle.
2. Event stream delivery.
3. Tool invocation metadata.
4. Approval request and response flow.
5. Session replay or history fetch.
6. Connection status and error reporting.

The exact OpenCode server shape still needs validation if a specific capability is not already confirmed in the repo. The frontend should be ready for a server or proxy based runtime, but it should not assume more than that.

### Browser side proxy expectations

The Chromium side proxy should own the contract that the WebUI sees. A practical sketch looks like this:

```text
CreateSession(CreateSessionRequest) -> SessionSummary
ListSessions(ListSessionsRequest) -> SessionList
GetSessionHistory(GetSessionHistoryRequest) -> SessionHistory
SubscribeToSessionEvents(SubscribeRequest) -> SessionEventObserver
SubmitPrompt(SubmitPromptRequest) -> SendResult
RespondToApproval(ApprovalResponseRequest) -> ApprovalAck
CancelSession(CancelSessionRequest) -> CancelAck
RestoreSession(RestoreSessionRequest) -> RestoreAck
```

The matching observer should carry event categories rather than a single flat text stream:

```text
OnSessionStateChanged
OnRuntimeEvent
OnMessageChunk
OnToolCallStarted
OnToolCallUpdated
OnToolCallCompleted
OnApprovalRequested
OnApprovalResolved
OnSessionHistoryLoaded
OnConnectionStatusChanged
OnError
```

The exact symbol names can change, but the shape should stay stable. The important part is that the frontend can subscribe to a session, load history, submit a prompt, answer an approval, and keep receiving runtime events without knowing how OpenCode is implemented.

### Contract shape principles

1. The contract should be event oriented, not only request response.
2. It should support streaming and replay.
3. It should separate text output from tool output.
4. It should keep browser local capabilities explicit.
5. It should allow the frontend to render without knowing the implementation details of the runtime.
6. It should make session history a first class fetchable resource, not just a side effect of live streaming.

### Existing contract guidance

The current Mojom surface can be replaced or heavily reduced. If any compatibility layer is kept during migration, it should be a thin bridge, not the permanent application model.

The current Rust core bridge path is not the target for the new console. The OpenCode path bypasses the existing Rust chat orchestration flow, and coexistence should only exist as migration fallback while the new adapter is proven.

## Migration phases

### Phase 1. Establish the backend adapter first

Before the new UI lands, the browser side adapter and Mojo proxy need to exist. The frontend depends on that adapter for session list, session history, live event subscription, prompt submission, and approval responses. Without that dependency in place, the new screen would have nowhere stable to connect.

### Phase 2. Build the new console shell

Replace the existing simple chat page with the new screen model. At this stage, the UI can still be backed by a partial adapter, but the layout and state shape should already match the final product.

### Phase 3. Connect OpenCode runtime events

Wire live session creation, event streaming, tool cards, and session history to the real OpenCode runtime and server.

### Phase 4. Add approval flows

Implement browser privileged approval requests end to end. Make sure the UI can pause, resume, and persist approval decisions.

### Phase 5. Remove legacy assumptions

Remove any dependence on the old chat only model, including assumptions about single prompt plus single response interactions. The old Rust bridge should remain only as a migration fallback until the OpenCode path is stable.

### Phase 6. Polish replay and inspection

Improve session replay, event inspection, and error recovery so the console feels reliable for real work.

## Risks

1. Contract drift between OpenCode and the Maho browser bridge.
2. Overloading the frontend with both runtime and browser logic.
3. Losing safe approval semantics while adding more powerful browser actions.
4. Building a UI that shows streaming output but hides tool context, which would make the console feel incomplete.
5. Carrying over the current chat page assumptions into the rewrite and ending up with a new skin on the same model.
6. Session history and replay can become expensive if events are not normalized early.
7. Browser specific context may need careful redaction before it is shown in the UI.
8. A sidebar sized surface will not comfortably fit the timeline, inspector, and approval flow at the same time.

## Open questions

1. Which browser context fields should be visible in the inspector, and which should stay hidden for privacy or noise reasons?
2. How much of the timeline should be replayable from history versus only live?
3. Which session metadata fields should be persisted across restarts, and which should be rebuilt from the runtime stream?
4. What is the smallest approval payload that still gives users enough context to make a safe decision?

## Design summary

The target frontend is a console for agent work, not a generic chat app. OpenCode owns the agent runtime, Maho owns the browser privilege boundary, and the frontend owns the presentation of sessions, runs, tools, approvals, and streams. The WebUI should talk to a Chromium side proxy over Mojo, that proxy should talk to the backend adapter, and the OpenCode path should bypass the old Rust chat orchestration bridge except during migration fallback. If the rewrite keeps those boundaries clear, it will be easier to ship a product that is both more powerful and easier to reason about than the current UI.
