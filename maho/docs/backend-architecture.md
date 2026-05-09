# Backend architecture for OpenCode runtime integration

## Purpose

This document describes how Maho should integrate OpenCode as the actual AI runtime while keeping Maho in charge of local browser capabilities. It is written for engineers who will implement the backend split in the Chromium fork.

The core idea is simple:

1. OpenCode becomes the external reasoning and orchestration runtime.
2. Maho keeps the privileged browser host role.
3. Browser actions, page extraction, and tab state stay local in Maho.
4. The current local orchestration and LLM transport layer becomes a thinner adapter around OpenCode.

The architecture below is based on the current repo structure and the confirmed findings for this project. Where OpenCode server behavior is not already confirmed in repo evidence, this document treats it as a Phase 1 assumption to validate rather than a settled fact.

## Confirmed findings that shape the design

1. OpenCode is usable as an external runtime through server, SDK, and CLI surfaces.
2. Server mode is the preferred integration path, pending Phase 1 validation of the exact callback and session behavior needed by Maho.
3. The cleanest local seam in Maho is around `MahoChatOrchestrationService`.
4. Local browser capabilities should remain in Maho through `MahoBrowserToolExecutor`, `MahoBrowserToolRegistry`, and `MahoAiPageContextExtractor`.
5. The current local orchestration and LLM transport can be reduced or replaced by an OpenCode backend adapter.

## Goals

1. Use OpenCode as the main runtime for chat, planning, tool selection, and response synthesis.
2. Keep all browser privileged operations inside Maho.
3. Preserve Maho specific page context extraction and tab aware browser tools.
4. Keep the browser UI and WebUI surfaces in Chromium, not inside OpenCode.
5. Make the backend split explicit so the code is easy to test and replace.
6. Support incremental migration, not a big bang rewrite.

## Non goals

1. Do not embed OpenCode in process unless a separate in process API is explicitly confirmed later.
2. Do not move browser control into OpenCode.
3. Do not redesign the WebUI layer as part of this backend change.
4. Do not change unrelated settings, sync, or browser shell behavior.
5. Do not invent private OpenCode APIs in this plan.
6. Do not blur frontend and backend ownership. The UI stays in Chromium. The runtime stays external. The browser host stays local.

## Current state in the repo

The current flow lives in `maho-chromium/browser/ai/`.

`MahoChatOrchestrationService` currently does all of the following:

1. Accepts user chat messages.
2. Classifies the request.
3. Extracts page context.
4. Shapes a JSON event for the Rust core.
5. Calls the Rust side event handler on a background thread.
6. Reads the Rust response.
7. Runs LLM calls through `MahoAiLlmClient`.
8. Executes browser tools through `MahoBrowserToolExecutor`.
9. Streams tokens and final responses back to the UI.

`MahoAiLlmClient` currently owns endpoint, model, and provider transport logic. It also contains provider specific request planning and streaming response handling.

`MahoAiPageContextExtractor` owns local page reading and structured context extraction from the active tab.

`MahoBrowserToolRegistry` owns the browser tool schema list.

`MahoBrowserToolExecutor` owns tool execution against the active browser and active tab.

`maho_ai_prefs.h` currently contains the AI related preference keys, and `maho_settings_page_handler.cc` is where user facing settings are wired into the app preference and core settings system.

## Proposed architecture

### High level split

The backend should be split into three layers.

1. OpenCode runtime adapter.
2. Maho browser host services.
3. Chromium UI orchestration.

The adapter layer should be the new center of the backend. It should accept Maho chat requests and translate them into an OpenCode session or run request. It should also translate OpenCode runtime events back into Maho events, including tool requests that need local browser execution.

The browser host layer should remain in Maho. It is the only place allowed to read the active tab, search page content, or query browser state.

The UI orchestration layer should stay in `MahoChatOrchestrationService` and related WebUI handlers. Its job becomes request routing, lifecycle management, and event delivery to and from the backend adapter.

### Proposed components

#### 1. `MahoChatOrchestrationService`

This stays as the local entry point for chat requests from the UI.

Future responsibilities:

1. Validate and classify the user request.
2. Collect local page context when needed.
3. Create a runtime request envelope.
4. Pass the request to the backend adapter.
5. Receive runtime events and forward them to the UI.
6. Dispatch browser tool calls back into the local browser tool executor.
7. Keep the one request at a time lifecycle and cancellation behavior.

What should leave this class:

1. Direct OpenAI style transport code.
2. Provider specific endpoint handling.
3. Any OpenCode specific protocol knowledge that can live in a backend adapter.
4. Any large tool planning logic that belongs to the runtime adapter.

#### 2. `MahoOpenCodeBackendAdapter`

This is the new backend abstraction. It is the boundary between Maho and the external OpenCode runtime.

It should own:

1. Session creation and reuse.
2. Request submission to OpenCode server mode.
3. Streaming runtime events.
4. Translation between Maho request envelopes and OpenCode messages.
5. Translation between OpenCode tool calls and Maho tool invocation requests.
6. Cancellation and retry handling.
7. Session metadata needed for reconnection or debugging.

The adapter should be the only part that knows the OpenCode transport contract.

#### 3. `MahoBrowserToolHost`

This is a logical backend role, not necessarily a new file name on day one.

It represents the local browser privilege boundary in Maho.

It should expose a narrow interface for:

1. Read current page.
2. Get active tab.
3. Get selected text.
4. Search within the page.
5. Extract structured page context.

The current implementation already exists in `MahoBrowserToolExecutor` and should remain there, but the architecture should treat it as a browser tool host that is called by the runtime adapter or orchestration service.

#### 4. `MahoBrowserToolRegistry`

This remains the source of truth for browser tool names, schema, and description.

Future role:

1. Publish browser tool schema to the backend adapter.
2. Keep tool input schemas canonical.
3. Allow the runtime adapter to advertise available local tools to OpenCode.

#### 5. `MahoAiPageContextExtractor`

This remains the local page context source.

Future role:

1. Extract contextual data from the active browser tab.
2. Produce structured context that can be attached to runtime requests.
3. Provide the same browser local data that the OpenCode runtime can consume as input.

#### 6. `MahoAiSettingsModel`

This is a settings concept, whether it is a new class or a thin settings wrapper.

It should centralize:

1. OpenCode server endpoint.
2. Selected model or profile.
3. Runtime mode selection.
4. Browser tool enablement.
5. Approval policy.
6. Optional session persistence settings.

## Proposed interface shape

This is a design level interface description, not a promise of exact code names.

### Runtime adapter interface

The backend adapter should support a shape like this:

```text
StartSession(settings, browser_context, request) -> runtime_session_id
SendUserMessage(runtime_session_id, request)
Subscribe(runtime_session_id, event_handler)
Cancel(runtime_session_id)
Close(runtime_session_id)
```

It should emit events such as:

```text
assistant_token
assistant_message
tool_call_requested
tool_call_result_ready
approval_requested
approval_decision_required
runtime_error
session_state_changed
```

### Browser tool host interface

The browser tool host should support a shape like this:

```text
ListTools() -> schema list
ExecuteTool(tool_name, arguments) -> result
GetCurrentPageContext() -> structured context
```

The runtime adapter should never talk to the browser directly. It should request browser tool execution through this host interface or through the orchestration service that wraps it.

## Integration contract between Maho and OpenCode

This section matches the frontend contract in spirit and makes the backend side explicit without pretending the full OpenCode server surface is already proven.

### Transport choice

The browser side should treat OpenCode as an external runtime service. Maho should own a Chromium side proxy that receives Mojo calls from the WebUI, then forwards them to the backend adapter. The adapter then talks to OpenCode over whatever server or SDK transport is validated in Phase 1.

The UI must not speak to OpenCode directly.

### Proxy model

The proxy layer should own these responsibilities:

1. Session lookup and creation.
2. Live event subscription.
3. Session history fetch.
4. Prompt submission.
5. Approval response delivery.
6. Cancellation and cleanup.
7. Translation between browser local state and runtime session identifiers.

### Event schema categories

The backend should preserve event categories that the frontend can render consistently:

1. Session lifecycle.
2. User input.
3. Assistant stream.
4. Tool execution.
5. Approval flow.
6. Connection state.
7. Error and completion.

The event payloads should stay structured so history can be replayed and inspected later.

### Approval and tool callback round trip

The high level round trip should look like this:

1. The UI submits a prompt through the browser side proxy.
2. The proxy passes the request to the backend adapter.
3. The adapter sends the request to OpenCode.
4. If OpenCode asks for a local browser tool, the adapter emits a tool request event.
5. The browser host executes the tool locally and returns a structured result.
6. The adapter sends that result back to OpenCode.
7. If the action needs user approval, the adapter pauses the request and exposes an approval event to the UI.
8. The UI responds, and the adapter resumes or cancels the run.

This is the desired shape, but Phase 1 should confirm the exact OpenCode server behavior around suspension, resumption, and callback handling before the production path depends on it.

## Request flow

1. The user sends a message from the Maho UI.
2. `MahoChatOrchestrationService` classifies the request.
3. If page context is needed, `MahoAiPageContextExtractor` reads the active tab.
4. The orchestration service builds a request envelope containing the user message, intent, page context, browser tool schemas, and runtime settings.
5. The envelope is passed to the OpenCode backend adapter.
6. The adapter opens or reuses an OpenCode server session.
7. OpenCode receives the message and begins reasoning.
8. If OpenCode needs local browser data or actions, it emits a tool call request.
9. The adapter forwards the call to Maho browser tools.
10. Maho executes the tool locally and returns the result.
11. The adapter feeds the tool result back to OpenCode.
12. OpenCode completes the response and streams final tokens or a final message.
13. `MahoChatOrchestrationService` forwards the output to the WebUI.

## Event flow

The event flow should be explicit and replayable.

1. UI event, user submits message.
2. Local orchestration event, request classified and page context attached.
3. Runtime event, request sent to OpenCode server.
4. Runtime stream event, token or delta arrives from OpenCode.
5. Tool event, OpenCode asks for a browser tool.
6. Local tool event, Maho executes the browser action.
7. Runtime stream event, OpenCode consumes the tool result and continues.
8. Completion event, final answer or error returns to the UI.

The event stream should carry a request id and a session id so logs, retries, and cancellation stay tied together.

## Tool round trip protocol risk

The biggest integration risk is whether OpenCode server mode can suspend and resume around local tool callbacks in the way Maho needs.

If that behavior is confirmed in Phase 1, the adapter can map OpenCode tool requests to local browser execution and feed the result back into the same session.

If it is not confirmed, Maho should keep a local multi round tool loop adapter. In that mode, OpenCode is treated as the reasoning endpoint, not as a fully managed runtime. Maho would own the loop that sends a prompt, receives a tool request or partial response, runs the local tool, sends the result back, and continues until the turn completes.

That fallback keeps browser control local and avoids blocking the migration on an unproven server contract.

## Browser tool bridge design

Maho should remain the privileged browser tool host.

### Why this stays local

The browser tools need direct access to the active browser, active tab, selected text, and page DOM or content extraction. These capabilities belong in Maho because they depend on Chromium process state, tab ownership, and browser permissions.

### What the bridge should expose

The bridge should expose only a narrow tool set at first:

1. `read_current_page`
2. `get_selected_text`
3. `get_active_tab`
4. `search_in_page`
5. `extract_structured_page_context`

These are already present in `MahoBrowserToolRegistry` and `MahoBrowserToolExecutor`.

### Bridge behavior

1. The runtime adapter publishes tool schemas to OpenCode.
2. OpenCode requests a tool by name with JSON arguments.
3. Maho validates the tool name against the registry.
4. `MahoBrowserToolExecutor` executes the tool locally.
5. Maho returns a structured result object.
6. The adapter translates that result back into the OpenCode session stream.

### Result contract

The tool result should remain structured, with fields similar to:

1. `ok`
2. `tool`
3. `context_scope`
4. `page_status`
5. `error`
6. `data`
7. `selected_text`
8. `matches`
9. `title`
10. `url`

This matches the current executor behavior and avoids collapsing everything into a text blob.

## Session management

OpenCode server sessions should be first class in the Maho backend.

### Session lifecycle

1. Create session on first user message or on first turn in a chat thread.
2. Reuse the session for the same conversation thread.
3. Keep a local mapping from Maho conversation id to OpenCode session id.
4. Reset or retire the session when the user starts a fresh thread, clears history, or changes runtime settings in a way that invalidates the session.

### Session data to retain locally

1. OpenCode session id.
2. Maho conversation id.
3. Runtime endpoint used for the session.
4. Selected model or profile.
5. Approval state for pending tool calls.
6. Cancellation token or request id.

### State boundaries

Maho should own the browser state and UI state.
OpenCode should own the runtime reasoning state.
The adapter should own the mapping between them.

## Approval model

Browser tool execution needs a clear approval model.

### Recommended model

Use a policy based model with per tool and per action gating.

1. Safe read only tools can run automatically.
2. Tab inspection and page extraction can run automatically.
3. Any tool that changes browser state should require explicit approval.
4. Any future navigation, form submission, download, or destructive action should be blocked unless the policy allows it.

### Policy inputs

1. Tool name.
2. Tool category, read only or state changing.
3. User setting.
4. Request context.
5. OpenCode request justification if provided.

### Approval timeout and default deny

Approval requests must not linger forever. If the user does not respond within a configured timeout, the request should default to deny and the adapter should clean up the pending approval state. That cleanup should cancel any waiting runtime turn, clear the pending request from the local map, and leave the session in a visible paused or failed state with a clear reason.

### UI behavior

If approval is needed, `MahoChatOrchestrationService` should surface a clear approval event to the UI. The UI can then show an approval prompt or status. The runtime adapter should pause the request until the approval decision returns.

## Settings and config model

The current AI settings already have the right shape for a migration, but they need a clearer runtime split.

### Current pref keys that matter

From `maho_ai_prefs.h`:

1. `maho.ai.api_key`
2. `maho.ai.endpoint`
3. `maho.ai.model`
4. `maho.ai.structured_context_v2_enabled`
5. `maho.ai.browser_tools_v1_enabled`
6. `maho.ai.chat_orchestrator_v2_enabled`
7. `maho.ai.provider_adapter_v2_enabled`
8. `maho.ai.ui_thin_mode_enabled`
9. `maho.ai.pending_initial_query`

### Recommended config split

The settings model should be split into these groups:

1. Runtime provider settings.
2. OpenCode server connection settings.
3. Browser tool settings.
4. Approval settings.
5. Session and history settings.
6. UI mode settings.

### Suggested runtime meaning

1. `provider_adapter_v2_enabled` becomes the main gate for the new backend adapter.
2. `chat_orchestrator_v2_enabled` controls whether the new runtime flow is active.
3. `browser_tools_v1_enabled` remains the gate for local privileged tools.
4. `structured_context_v2_enabled` remains the gate for structured page context input.
5. `endpoint` and `model` should be interpreted as OpenCode server connection values, not direct LLM transport values, once the migration is complete.

### Settings migration guidance

Existing endpoint and model values must not disappear on upgrade. During migration, keep the current values, reinterpret them as OpenCode server connection defaults where possible, and show a clear fallback or reset path if the saved values do not map cleanly to the new runtime.

Users should not be silently broken by the new semantics. If a stored provider value cannot be reused, the UI needs to surface that mismatch and ask for an explicit update instead of assuming a hidden default.

### Settings UI responsibility

`maho_settings_page_handler.cc` should keep converting user settings into app state, but the backend should no longer assume the settings directly describe an OpenAI style transport. Instead, settings should describe the OpenCode runtime and the local browser host policy.

## Migration phases

### Phase 1, adapter introduction

1. Add the OpenCode backend adapter.
2. Keep `MahoChatOrchestrationService` as the entry point.
3. Route new requests through the adapter behind a feature gate.
4. Keep the current LLM client as a fallback path.
5. Keep browser tools local and unchanged.
6. Validate the OpenCode server assumptions that are still unconfirmed, especially session lifecycle, event streaming, and tool callback suspension.

Success criteria:

1. The new adapter can send a request to OpenCode server mode.
2. Browser tool schemas are visible to the runtime.
3. Local browser extraction still works.

### Phase 2, runtime switch

1. Make OpenCode the default runtime path for chat.
2. Keep the current transport only as a fallback for compatibility.
3. Move more request planning logic out of `MahoAiLlmClient` and into the adapter.
4. Add session id persistence.

Success criteria:

1. Chat requests work end to end through OpenCode.
2. Tool calls round trip through Maho correctly.
3. Cancellation works.

### Phase 3, thin transport removal

1. Remove the old provider specific LLM transport from the default flow.
2. Keep only the adapter path for OpenCode server integration.
3. Retain browser tools and page extraction in Maho.
4. Simplify settings labels to match the new runtime model.

Success criteria:

1. The backend path is clearly split.
2. The old transport no longer owns the main AI flow.
3. The browser host boundary remains local.

## Observability

The backend split needs logs and metrics at each handoff.

### Log points

1. Request received.
2. Request classified.
3. Page context extracted.
4. Session created or reused.
5. Tool schema published.
6. Tool call requested.
7. Tool execution success or failure.
8. OpenCode streaming token received.
9. Completion received.
10. Cancellation or error.

### Useful fields

1. Maho request id.
2. OpenCode session id.
3. Conversation id.
4. Request mode.
5. Intent.
6. Tool name.
7. Page context status.
8. Approval outcome.
9. Error category.
10. Runtime endpoint.

### Metrics

Track simple counts and timings for:

1. Request duration.
2. Time to first token.
3. Tool call count.
4. Tool failure count.
5. Page extraction failure count.
6. Session reuse rate.
7. Approval prompt rate.

## Failure handling

### Runtime connection failures

If the OpenCode server is unavailable, the adapter should return a clear error and the UI should show a runtime unavailable state. If a fallback path exists during migration, only then should Maho switch to the fallback transport.

### Tool failures

If a browser tool fails, return a structured error with the tool name and browser status. Do not collapse browser failures into generic runtime failures unless the adapter itself breaks.

### Page context failures

If page extraction fails, preserve the current message level error handling in `MahoChatOrchestrationService`.

### Session corruption or desync

If the OpenCode session id no longer works, the adapter should drop the cached session, create a new one, and attach a clear diagnostic to the request log.

### Cancellation

Cancellation should invalidate the current request and stop any local tool execution that has not yet completed. The adapter should also cancel the OpenCode side session request if the server supports it.

### Approval cleanup

When a request is cancelled, denied, or timed out, the adapter must clear any pending approval record and stop waiting on the run. This prevents stale approvals from leaking into the next turn.

## Security considerations

1. Keep browser tool execution local so the privileged browser host is not exposed over a public network boundary.
2. Treat the OpenCode server as a separate trust boundary from the browser.
3. Do not send more page data than the current request needs.
4. Avoid exposing credentials, cookies, or other browser secrets through the tool bridge.
5. Scope tools to the active tab unless a future explicit design adds multi tab support.
6. Require approval for any state changing browser action.
7. Validate all tool names and JSON arguments before execution.
8. Keep the OpenCode endpoint configurable and clear in settings so users know where their data goes.
9. Prefer local network or user controlled server endpoints for OpenCode server mode.

## Explicit mapping from current files to future responsibilities

### `maho-chromium/browser/ai/maho_chat_orchestration_service.cc`

Future role:

1. Keep request entry point and lifecycle.
2. Own request classification and UI callback wiring.
3. Call the OpenCode backend adapter instead of directly owning provider transport.
4. Keep local page context gathering when required.
5. Remain the place that connects UI events to backend events.

### `maho-chromium/browser/ai/maho_ai_llm_client.cc`

Future role:

1. Shrink into an OpenCode transport helper only if needed.
2. Otherwise be removed from the main chat flow.
3. Stop owning the primary AI provider transport path.
4. Do not remain the main place for request planning once OpenCode is the runtime.

### `maho-chromium/browser/ai/maho_browser_tool_executor.cc`

Future role:

1. Stay the local executor for browser tools.
2. Keep execution against the active browser and tab.
3. Return structured results back to the adapter.
4. Remain the privileged browser capability host.

### `maho-chromium/browser/ai/maho_browser_tool_registry.cc`

Future role:

1. Stay the canonical browser tool schema source.
2. Feed the runtime adapter with tool definitions.
3. Keep tool names and argument schemas centralized.

### `maho-chromium/browser/ai/maho_ai_page_context_extractor.cc`

Future role:

1. Keep page extraction local and browser aware.
2. Provide structured context for the OpenCode runtime.
3. Continue to enforce page context limits and warnings.

### `maho-chromium/browser/ui/webui/maho_ai_prefs.h`

Future role:

1. Hold the runtime feature and policy pref keys.
2. Become the preference source for OpenCode connection settings and approval policy.
3. Keep browser tool and orchestration feature gates.

### `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`

Future role:

1. Persist the runtime settings that the user can change.
2. Map the settings UI to the new OpenCode runtime model.
3. Keep the browser settings bridge separate from runtime execution.

## Rust-side impact

The new path moves chat orchestration responsibilities out of `maho-core` for this feature. That keeps the OpenCode runtime on the Chromium side adapter path instead of forcing the Rust core to keep owning the agent loop.

Browser-local capabilities still remain in Chromium-side C++ through the browser tool executor, registry, and page context extractor. The Rust core should not absorb those responsibilities as part of the OpenCode migration.

## Implementation notes

1. Prefer a narrow adapter interface over a large runtime service.
2. Keep OpenCode specific details out of the UI layer.
3. Keep browser tool execution synchronous from the perspective of the local host, even if the runtime stream is asynchronous.
4. Preserve the structured context object shape, since the current code already uses it well.
5. Keep the migration gate driven by prefs so rollout can happen safely.
6. Treat any unconfirmed OpenCode server capability as an assumption until Phase 1 proves it.

## Acceptance criteria

The architecture is ready when these are true:

1. OpenCode server can receive a Maho chat request.
2. The runtime can ask Maho to read the active page or selected text.
3. Maho can execute local browser tools and send results back.
4. The browser host remains local and privileged.
5. Settings can describe the new runtime without referring to the old transport semantics.
6. The codebase has a clear path to remove or minimize the old LLM transport path.

## Summary

The target shape is a clean split. OpenCode becomes the external runtime and reasoning engine. Maho becomes the local browser host and privileged tool executor. `MahoChatOrchestrationService` stays as the seam that ties the two together, while `MahoBrowserToolExecutor`, `MahoBrowserToolRegistry`, and `MahoAiPageContextExtractor` remain the local browser capability layer. The OpenCode path bypasses the old Rust chat bridge once enabled, with fallback only during migration, and the backend must be honest about any OpenCode server behavior that still needs Phase 1 validation.
