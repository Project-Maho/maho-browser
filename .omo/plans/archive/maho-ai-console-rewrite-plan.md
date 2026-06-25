# Maho AI OpenCode console rewrite plan

## Purpose

This plan locks the recovery path for `maho_ai` before implementation starts. The current work polished the old compact page copilot and chat shell, but that is not the target. The required product shape is an OpenCode native agent console with a left rail, a main timeline, a right inspector, and a bottom composer.

This plan is based on:

- `/Users/indo/code/project/maho-workspace/docs/frontend-architecture.md`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/resources/maho_ai/app.ts`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/resources/maho_ai/maho_ai.css`

## Target

The target UI is a session centered OpenCode console, not a page chat panel.

Required shape:

- Left rail for sessions, search, and current runtime status
- Main timeline for prompts, assistant turns, tool calls, approvals, and replayed events
- Right inspector for session metadata, tool details, approval details, and browser context
- Bottom composer for prompts, attachments, mode shortcuts, and session actions

The UI must assume a wide, full height workspace. A single column chat shell is explicitly the wrong shape for this product.

## Scope

In scope:

- Replace the current page copilot shell with an OpenCode style console
- Reframe the frontend state around sessions and events, not message bubbles
- Harden the Mojom contract around session lifecycle, runtime events, tool calls, approvals, and replay
- Keep browser privileged actions local to Maho and bridged through the Chromium side proxy
- Preserve migration fallback for legacy chat behavior only while the new path is brought up
- Add timeline cards, inspector views, and a real composer workflow
- Support session replay, live updates, and approval flow visibility

Out of scope:

- Polishing the current chat page into a final product
- Keeping the legacy chat model as the primary UX
- Moving OpenCode runtime calls into the frontend
- Designing a generic agent platform beyond the repo needs
- Shipping every possible OpenCode feature in the first pass

## Source constraints

The architecture doc is the authority. It says:

- OpenCode is the runtime and server
- Maho is the browser shell and privilege bridge
- WebUI talks to a Chromium side proxy over Mojo
- The frontend should be event oriented and session oriented
- The old chat contract can exist only as migration fallback

The current code confirms the mismatch:

- `maho_ai.mojom` mixes legacy chat methods with the new session contract
- `maho_ai_page_handler.cc` still routes through chat history and streaming callbacks
- `app.ts` renders a compact toolbar, empty state, page preview, quick action pills, and a message list
- `maho_ai.css` styles a narrow panel experience, not a console workspace

## Goals

1. Make the console feel like an operator surface for an agent run.
2. Make sessions, events, tool calls, and approvals first class.
3. Keep the browser bridge explicit and local.
4. Show live runtime state, replay state, and error state clearly.
5. Preserve enough legacy support to migrate without blocking the rewrite.

## Non goals

1. Minor visual tweaks to the current chat UI.
2. A direct JS to OpenCode connection.
3. A layout that depends on sidebar width or narrow panel constraints.
4. A fully generic agent framework.
5. Hidden approvals or tool execution that users cannot inspect.

## Current implementation summary

### Mojom

`/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom` already contains a useful bridge shape, but it is split between two eras:

- Legacy chat types and methods remain for migration
- New session, runtime event, tool call, approval, and connection types already exist

The contract is directionally right, but it still needs hardening so the new UI does not inherit chat first assumptions.

### Page handler

`/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h` and `.cc` still do too much legacy orchestration:

- Maintains chat history and legacy streaming callbacks
- Persists session metadata and event logs in prefs
- Bridges to `MahoOpenCodeRuntimeService`, `MahoAiRuntimeRouter`, and page context extraction
- Converts internal events into Mojo events, but still preserves the old single response flow

### Frontend

`/Users/indo/code/project/maho-workspace/maho-chromium/browser/resources/maho_ai/app.ts` and `.css` are still a compact chat surface:

- One toolbar
- One central messages column
- Empty state copy about page copilot help
- Page preview card
- Quick action pills for analyze, explain, summarize
- A single bottom composer card

That structure is not enough for the OpenCode console.

## Architecture gaps to close

1. The frontend store is message centric instead of session and event centric.
2. The UI has no left rail session browser.
3. The UI has no persistent right inspector.
4. The timeline does not render tool calls, approval cards, or replay cards as real units.
5. The composer does not reflect session aware actions or attachments.
6. The page handler still exposes legacy chat behavior alongside the new contract.
7. Migration fallback is present, but it is not yet isolated from the target architecture.
8. The CSS assumes a narrow, vertically stacked panel, which fights the target layout.

## Reusable vs replaceable parts

### Reusable

- `MahoOpenCodeRuntimeService` integration for runtime availability and status
- `MahoAiRuntimeRouter` as the browser side runtime switchboard
- `MahoAiPageContextExtractor` for local browser context
- Session persistence helpers in the page handler, once they are normalized
- `ClosePanel()` behavior for side panel dismissal
- Existing prefs for session list and event log storage

### Replaceable

- The current `app.ts` DOM tree and state flow
- The compact page copilot visual language
- The empty state, page preview, and quick action pill emphasis
- The chat bubble model as the primary representation of work
- The current CSS layout and spacing system for the AI surface

## File level change map

### Primary files

1. `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
   - Harden the session oriented contract
   - Keep legacy chat methods only as a migration bridge
   - Make event, tool, approval, and replay semantics explicit
   - Ensure naming stays consistent with the architecture doc, especially approval states

2. `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`
   - Split legacy helpers from console era helpers
   - Add clear ownership for active session, replay state, tool state, and approval state
   - Prepare the handler to act like a thin bridge, not the runtime brain

3. `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`
   - Route session list, session history, prompt submission, approval responses, and cancellation through the new contract path
   - Normalize event persistence and replay loading
   - Keep legacy chat fallback isolated and easy to remove later
   - Preserve browser local context and runtime status propagation

4. `/Users/indo/code/project/maho-workspace/maho-chromium/browser/resources/maho_ai/app.ts`
   - Replace the single column page copilot shell with the console shell
   - Introduce a normalized frontend store and event driven rendering
   - Render left rail, timeline, inspector, and composer as separate concerns
   - Move quick actions into session aware composer actions or timeline helpers

5. `/Users/indo/code/project/maho-workspace/maho-chromium/browser/resources/maho_ai/maho_ai.css`
   - Replace panel styling with a full console grid
   - Add layout primitives for rail, timeline, inspector, composer, and cards
   - Add visual states for live, replaying, blocked, error, and approval pending

### Likely follow on files if the rewrite is split cleanly

- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.h`
- `/Users/indo/code/project/maho-workspace/maho-chromium/browser/ui/webui/maho_ai/BUILD.gn`

These should only change if the UI bootstrap or build wiring needs to match the new frontend module shape.

## Contract changes

The current contract should be hardened, not merely restyled.

### Keep, but demote to migration fallback

- `SendMessage`
- `GetPageContent`
- `GetChatHistory`
- `clearHistory`
- `SetApiKey`
- `GetApiKeyStatus`

These are only acceptable if they are clearly marked as compatibility paths and not the main product model.

### Core console methods to anchor the rewrite

- `GetConnectionState`
- `GetSessionList`
- `StartSession`
- `ResumeSession`
- `SubmitPrompt`
- `CancelTurn`
- `RespondToApproval`
- `ClosePanel`

### Contract expectations

- Session metadata must be fetchable and restorable
- Runtime events must be append only and replayable
- Tool calls and tool results must be represented separately from assistant text
- Approval requests must have a stable state model and visible payload
- Connection status must be a first class concept
- Browser local context must remain a distinct input from OpenCode runtime data

## Frontend structure

The current `app.ts` should be treated as a bootstrap starting point, not the final architecture.

Recommended internal structure for the rewrite:

- `app.ts`
  - Bootstrap only
  - Wire the Mojo client
  - Load initial session state
  - Mount the console shell

- `store` layer
  - Active session
  - Session list
  - Runtime connection state
  - Ordered event log
  - Active tool call map
  - Pending approval map
  - Browser context snapshot

- `session shell`
  - Left rail
  - Timeline
  - Inspector
  - Composer

- `timeline cards`
  - User prompt card
  - Assistant token stream card
  - Tool call card
  - Tool result card
  - Approval card
  - Session lifecycle card
  - Error card

- `inspector`
  - Session metadata
  - Runtime status
  - Tool detail payload
  - Approval detail payload
  - Browser context summary

- `composer`
  - Prompt entry
  - Attach current tab or browser context
  - Session creation entry point
  - Cancel or stop controls when a run is active

## Phased implementation plan

### Phase 1. Contract hardening

Outcome:

- The Mojom contract clearly separates session console behavior from migration behavior.

Work:

- Tighten session and event semantics in `maho_ai.mojom`
- Make approval naming and event naming consistent with the architecture doc
- Confirm which legacy methods stay, and annotate them as migration only
- Ensure event payloads can support replay and UI inspection without extra inference

Exit criteria:

- A developer can read the contract and understand the target console without reading the old chat code.

### Phase 2. Page handler and proxy updates

Outcome:

- The Chromium side handler behaves like a console transport bridge.

Work:

- Refactor `maho_ai_page_handler.h` and `.cc` around active session, replay, tool state, and approval state
- Keep `MahoAiRuntimeRouter` as the adapter selector
- Preserve pref backed session storage, but normalize it around session lists and event logs
- Ensure `ResumeSession` returns history suitable for replay
- Isolate the chat fallback path so it can be removed later without touching the console path

Exit criteria:

- Session list, session resume, prompt submission, cancellation, and approval response all work through one console oriented path.

### Phase 3. Frontend store rewrite

Outcome:

- The frontend has a normalized state model for sessions and runtime events.

Work:

- Replace message list state with session and event entities
- Track live connection state, pending approvals, tool calls, and replay state explicitly
- Keep raw runtime events separate from presentation state
- Derive cards and inspector data from the store instead of from DOM patches

Exit criteria:

- The UI can answer what session is active, what event is live, what approval is pending, and what context is attached.

### Phase 4. Shell rebuild

Outcome:

- The main layout becomes the OpenCode console grid.

Work:

- Replace the current vertical panel layout in `app.ts`
- Build the left rail, timeline, inspector, and composer as distinct regions
- Add empty and loading states for the console, not for chat
- Make the shell wide enough for timeline and inspector to coexist

Exit criteria:

- The surface no longer reads like a compact chat panel.

### Phase 5. Timeline cards

Outcome:

- The center column shows the actual work the agent is doing.

Work:

- Add card types for user input, assistant streaming, tool start, tool progress, tool result, approval request, approval result, and session lifecycle changes
- Make each event visible even when it can collapse visually
- Render long outputs with expand and collapse behavior
- Preserve sequence and timestamp metadata for replay clarity

Exit criteria:

- Users can tell when the agent is thinking, calling a tool, waiting for approval, or finishing a run.

### Phase 6. Inspector

Outcome:

- The right rail explains what the selected session or event means.

Work:

- Show session metadata, runtime state, last meaningful event, and active approval count
- Show tool input and output details in structured form
- Show approval rationale, scope, and action preview when available
- Show browser context state and any privacy sensitive redactions clearly

Exit criteria:

- Inspecting a run does not require scanning the entire feed.

### Phase 7. Composer

Outcome:

- The composer starts tasks, not just messages.

Work:

- Add session aware prompt entry
- Add attachments or page context actions where supported
- Add explicit create session and submit prompt paths
- Add cancel, stop, or retry actions as runtime state allows
- Remove composer assumptions that only make sense for a single chat prompt

Exit criteria:

- The entry point supports starting and continuing real sessions, not just sending text.

### Phase 8. Migration and fallback handling

Outcome:

- The old path remains available only long enough to keep the rewrite safe.

Work:

- Keep legacy chat methods behind a compatibility path
- Make fallback behavior obvious in code and in UI state
- Ensure the new console can boot even if some runtime capabilities are not ready yet
- Add clear error states for missing runtime, disconnected adapter, or failed replay

Exit criteria:

- The new console can ship incrementally without pretending the old panel is the target.

### Phase 9. Validation

Outcome:

- The rewrite is verifiable before it is considered done.

Work:

- Run diagnostics on modified files
- Verify type safety and build health for the touched frontend and WebUI code
- Smoke test session list, session creation, replay, tool cards, approval cards, and cancel behavior
- Confirm the console renders in a wide layout with left rail, timeline, inspector, and composer

Exit criteria:

- The implementation passes static checks, builds cleanly, and matches the target screen model.

## Risks and mitigations

1. Contract drift between OpenCode and the browser bridge
   - Mitigation: keep the Mojo surface explicit, event oriented, and replay friendly.

2. Frontend remains chat shaped after the rewrite
   - Mitigation: block any design that does not include rail, timeline, inspector, and composer.

3. Approval flow gets hidden or simplified too early
   - Mitigation: make approval cards first class and keep the decision visible in the timeline.

4. Browser logic leaks into the frontend
   - Mitigation: keep all privileged operations in the Chromium side proxy and handler layer.

5. Replay becomes fragile because event state is not normalized
   - Mitigation: store raw runtime events separately and derive presentation state from them.

6. Legacy support stays forever
   - Mitigation: label fallback paths clearly and remove them once the new bridge is stable.

7. Narrow width assumptions reappear in CSS
   - Mitigation: build the console grid first and style cards second.

## Validation checkpoints

### After contract hardening

- `maho_ai.mojom` reads like a session console contract, not a chat patch
- Legacy methods are clearly marked as fallback only

### After handler updates

- Session creation, resume, prompt submission, cancellation, and approval response go through the new flow
- Session history persists and reloads without depending on chat semantics

### After frontend rewrite

- The UI shows left rail, main timeline, right inspector, and bottom composer
- There is no single column page copilot shell left in the target path

### After validation

- Diagnostics are clean on the changed files
- Build or type checks pass for the affected surface
- Manual smoke testing confirms session replay, tool cards, approval cards, and runtime state all render correctly

## Acceptance checklist

- [ ] The plan is implemented against the exact file targets listed above
- [ ] The console layout is left rail, main timeline, right inspector, bottom composer
- [ ] The current single column page copilot chat shell is gone from the target experience
- [ ] Session list, session resume, and session creation work end to end
- [ ] Tool calls and tool results render as timeline cards
- [ ] Approval requests and approval responses render and persist as first class events
- [ ] Runtime connection state is visible in the shell and inspector
- [ ] Browser context is shown separately from agent runtime state
- [ ] Legacy chat behavior remains only as migration fallback
- [ ] The frontend store is session and event oriented
- [ ] CSS supports the console workspace shape without collapsing back into a panel
- [ ] Diagnostics on changed files are clean
- [ ] Build or type verification passes for the touched surface

## Implementation guardrails

- Do not add a new chat only skin over the current surface
- Do not move browser privileged actions into the frontend
- Do not hide approvals behind a toast or passive indicator
- Do not keep both models as equally valid end states
- Do not let the layout collapse into a narrow sidebar view

## Deliverable definition

When this plan is followed, the codebase should be ready for implementation of the OpenCode console without re deriving scope. The next step is code, but only after the contract, state model, and shell shape above are agreed and applied in order.
