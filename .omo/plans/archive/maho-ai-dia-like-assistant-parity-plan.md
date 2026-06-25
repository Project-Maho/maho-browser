# Maho AI Dia-like assistant-first parity plan

## Objective

Make the default `chrome://maho-ai` experience feel like a calm, contextual AI assistant, not a developer console. The target sensation is Dia: conversational, results-first, proactive, quiet. The existing runtime plumbing (sessions, tools, approvals, event streams, browser context) survives and powers the experience, but none of that machinery is the default visible surface.

This plan supersedes and replaces the previous `maho-ai-opencode-console-parity-plan.md`.

## Source of truth

This plan is the authoritative product target for the maho-ai user-facing surface.

`docs/frontend-architecture.md` remains a valid technical reference for the *backend* event model, adapter plumbing, and Mojo contract. It is NOT the product target for the default user-facing experience. That document describes an "OpenCode native agent console" and "operator console" product shape. This plan explicitly rejects that framing as the default face.

## Product correction statement

The previous plan (`maho-ai-opencode-console-parity-plan.md`) aligned with `docs/frontend-architecture.md` as its product target. That document targets a four-panel operator console: left rail, timeline, inspector, composer. Implementing that plan would produce a good developer observability surface, but it would NOT produce a Dia-like assistant experience.

The correction:

- The console layout (left rail, timeline, inspector, composer) is NOT the default user-facing target.
- The default target is an assistant-first conversational surface.
- Console/observability features survive as a secondary expanded mode for power users and developers.
- This plan reuses all existing backend and runtime plumbing without rewriting it.

## Scope lock

### In scope

- `maho-chromium/browser/resources/maho_ai/` (all TS/CSS/HTML): presentation layer reorientation
- `maho-chromium/browser/ui/webui/maho_ai/` (page handler, UI host, Mojom): minimal contract changes if inline surfacing needs new signals
- Interaction model and state machine for the primary shell

### Out of scope

- Rust-side AI logic expansion
- OpenCode server modifications
- New WebUI surface or URL (keep `chrome://maho-ai`, keep side-panel container)
- Backend tool registry/executor changes (they work; this plan consumes their output differently)
- Browser chrome surrounding the panel (sidebar, location bar, command overlay geometry)

### Scope note: orchestration-layer touches

The following workstreams touch the C++ orchestration/adapter layer (`maho_opencode_adapter.cc`, `maho_chat_orchestration_service.cc`) in narrow, well-defined ways. These are NOT backend rewrites; they are behavioral policy injection points on the existing event/prompt path:

- Response shaping (Phase 1B): a filter pass on outbound events before they reach the frontend
- Persona/system prompt injection (Phase 5): a preamble prepended to prompts before they reach OpenCode
- Memory injection (Phase 4): facts appended to the system context at session start
- Authorization policy (Phase 2): a decision point in the tool executor that routes to auto-act vs. approval

None of these change the runtime adapter's connection model, session lifecycle, or event stream contract. They add behavior at existing seams.

## Verified repo facts

These are confirmed from the current codebase:

1. **Side-panel shell exists and renders.** `console_shell.ts` produces a narrow panel with a no-session hero, quick-action pills, page-context chip, and single-column conversation thread. This is already closer to assistant-first than to the console layout.

2. **Backend plumbing is session-oriented and event-driven.** The Mojo contract exposes `StartSession`, `ResumeSession`, `GetSessionList`, `GetSessionHistory`, `SubmitPrompt`, `RespondToApproval`, `CancelTurn`. The `RuntimeEvent` stream carries tool requests, tool results, approval requests, approval results, assistant tokens, session status, connection state, and browser context injection.

3. **Runtime adapter infrastructure is live.** `maho_opencode_adapter` connects to the OpenCode runtime. `maho_ai_runtime_router` dispatches. The adapter creates/resumes sessions and streams events.

4. **Frontend state model already tracks everything.** `types.ts` defines `AppState` with sessions, events, tool calls, tool results, approvals, approval results, inspector state, and turn-pending tracking.

5. **View files exist for console regions.** `left_rail.ts`, `timeline.ts`, `inspector.ts`, `cards.ts`, `composer.ts`, `console_shell.ts`, `conversation_thread.ts` all exist. Their completeness is unverified, but the files are present.

6. **Page context extraction works.** `maho_ai_page_context_extractor` captures page state. The event stream carries structured page context payloads.

7. **Tool and approval lifecycle plumbing exists.** Browser tool registry/executor handle dispatch. The page handler routes events through the stream.

8. **The current no-session screen already matches a reference-parity spec.** `maho-ai-reference-initial-screen-parity.md` locked the idle first-open to: top bar, empty center field, three quick actions, bottom composer. This is closer to Dia than to a console.

## Target experience contract

### Default surface (what users see)

| State | Primary visible surface | Hidden by default |
|-------|------------------------|-------------------|
| Idle first-open | Quiet top bar, empty center, quick actions, composer | Sessions list, timeline, inspector, runtime metadata |
| Command-prefilled open | Same calm shell, prefilled composer | Same |
| Active conversation | Conversation thread (results-first), composer, lightweight activity indicator | Dense tool cards, approval details, session metadata |
| Tool running | Inline progress hint in conversation | Full tool arguments/output |
| Approval required | Inline approval prompt with enough context to decide | Full arguments, raw payloads |
| History/read-only | Quiet read-only conversation view | Full event timeline |

### What "assistant-first" means concretely

1. The conversation thread shows assistant responses as the primary content, not a timeline of runtime events.
2. Tool calls surface as brief inline indicators ("Analyzing page...") not as structured cards with arguments/output by default.
3. Approvals surface as inline prompts with a single sentence of context plus approve/deny, not as multi-field inspector panels.
4. Session management is behind a menu or gesture, never the first thing visible.
5. Proactive/contextual suggestions appear when the assistant can be useful (page change, selected text, navigation) without the user asking.
6. The composer dominates the bottom. No mode selectors, runtime status chips, or session controls crowd it.

### What "conversational results-first" means

- Assistant text streams directly into the conversation.
- When tools run, the user sees the *result* summarized in conversation, not the tool invocation details.
- Long outputs collapse to a headline with expand affordance.
- The conversation reads like a dialogue, not a system log.

## Non-goals / anti-drift rules

1. Do NOT build the four-panel console layout (left rail + timeline + inspector + composer) as the default surface.
2. Do NOT expose sessions/history as a primary navigation element.
3. Do NOT surface tool arguments, raw event streams, or runtime metadata by default.
4. Do NOT remove the console/observability features. They survive as an expanded/developer mode.
5. Do NOT rewrite the runtime adapter, tool registry, or Mojo contract substantially.
6. Do NOT add Rust-side AI logic.
7. Do NOT treat `docs/frontend-architecture.md` as the product target. Treat it as a backend reference only.
8. Do NOT add new surfaces or URLs. Keep `chrome://maho-ai` and the side-panel host.
9. Do NOT let proactive suggestions become intrusive or interruptive. They should feel offered, not pushed.
10. Do NOT optimize for wide/desktop console layouts. The side panel is narrow; the assistant UX should thrive in that constraint.
11. Do NOT complete the console-parity plan first. That is explicitly NOT the priority.

## Phased workstreams

### Phase 0: Plumbing verification

**Goal:** Confirm current backend contract and event model can support the assistant-first surface without changes, and identify any minimum gaps.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
- `maho-chromium/browser/ai/maho_opencode_adapter.cc`
- `maho-chromium/browser/ai/maho_browser_tool_executor.cc`

**Atomic steps:**

1. Verify `StartSession` + `SubmitPrompt` round-trip delivers assistant text tokens through the event stream to the frontend.
2. Verify tool-running events include enough metadata for a one-line inline summary (tool name at minimum).
3. Verify approval events include a human-readable description field suitable for inline display.
4. Verify session history retrieval returns events in a form renderable as conversation turns (not just raw events).
5. Confirm whether page context injection triggers automatically or requires explicit user action.
6. Document any gap that requires a Mojo contract addition (e.g., a "tool summary" field, an "approval description" field).

**Verification:** Written audit with pass/fail per item plus a list of any required minimum contract additions.

**Exit criteria:** Every data point the assistant surface needs is either already available in the event stream or has a documented minimal addition path.

---

### Phase 1: Conversation thread as primary surface

**Goal:** Make the active-session view render a conversational thread (assistant responses as primary content) instead of a timeline of events.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`

**Atomic steps:**

1. Ensure the active-session path in `console_shell.ts` renders `conversation_thread.ts` as the primary content area (not `timeline.ts`).
2. Conversation thread renders: user prompts as sent messages, assistant token streams as received messages.
3. Tool-running state shows as a brief inline indicator within the conversation flow (e.g., "Reading page..." with a subtle spinner). No structured card.
4. Tool results appear as a collapsed summary line within the conversation, expandable on tap.
5. Streaming assistant text appends live within the current assistant message bubble.
6. The thread auto-scrolls on new content, pauses on user scroll-up.
7. Remove or bypass any event-timeline rendering that treats tool/approval/status events as first-class visible cards in the default view.

**Verification:** Start a session, submit a prompt. Confirm the response renders as a conversational message, tool activity shows as a brief inline hint, and no event cards or timeline structure appears.

**Exit criteria:** The active-session default view is a conversation, not a timeline.

---

### Phase 1B: Response shaping and output translation

**Goal:** Insert a translation layer between raw OpenCode SSE events and the user-visible conversation, so assistant replies feel authored for the user rather than relayed from a developer tool.

Raw OpenCode output contains markdown-heavy formatting, code blocks, internal reasoning artifacts, and tool invocation details that make sense in a developer terminal but feel noisy in an assistant context. This phase introduces a response-shaping pass that translates raw output into assistant-appropriate prose before it reaches the conversation renderer.

**Candidate files:**
- `maho-chromium/browser/ai/maho_opencode_adapter.cc` (event emission path)
- `maho-chromium/browser/ai/maho_chat_orchestration_service.cc` (new or existing; `ShapeRustEvent()` integration point)
- `maho-chromium/browser/resources/maho_ai/store.ts` (frontend event ingestion)
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`

**Atomic steps:**

1. Define shaping rules: strip internal reasoning markers, collapse verbose tool narration into single-sentence summaries, normalize markdown intensity for side-panel readability.
2. Decide shaping location: prefer a C++ `ShapeRustEvent()` pass in the orchestration service (keeps frontend simple), with a fallback to a frontend `shapeEvent()` utility if C++ adds too much latency to the streaming path.
3. Implement the shaping pass. For streaming tokens, shaping is lightweight (strip prefixes, normalize whitespace). For complete tool-result events, shaping produces a one-line human summary from the structured payload.
4. Ensure developer mode bypasses shaping entirely, rendering raw events as-is.
5. Preserve the full unshaped event in store state so the inspector/developer view can always show the original.

**Verification:** Submit a prompt that triggers tool use. Confirm the assistant reply reads as natural prose, not as a tool transcript. Toggle developer mode and confirm raw events remain visible there.

**Exit criteria:** The conversation thread never shows raw runtime artifacts in assistant mode. The translation is invisible to the user and lossless for the developer view.

---

### Phase 2: Inline approval UX and tool authorization policy

**Goal:** Surface approval requests within the conversation thread as lightweight inline prompts, backed by a configurable authorization policy that determines which actions auto-execute, which ask inline, and which require full approval.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`
- `maho-chromium/browser/resources/maho_ai/views/cards.ts` (for approval-specific rendering)
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/ai/maho_browser_tool_executor.cc` (authorization decision point)
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`

**Atomic steps:**

1. When an approval event arrives, render an inline approval bubble in the conversation thread.
2. The bubble shows: one-sentence description of what's being approved, approve button, deny button.
3. On decision, the bubble updates to show the outcome (approved/denied) and becomes non-interactive.
4. High-risk approvals get a slightly stronger visual treatment (border or icon) but remain inline.
5. A "View details" link on the approval bubble opens the inspector pane (secondary surface) for users who want full arguments/payload.
6. Wire approve/deny to `store.respondToApproval()`.

**Tool authorization policy (autonomy levels):**

7. Define three authorization tiers in the browser-side tool executor:
   - **Auto-act:** Read-only, non-destructive tools (page read, summarize, explain) execute without user confirmation. The conversation shows a brief "did this" indicator after the fact.
   - **Inline confirm:** Moderate-impact tools (navigate, fill form fields, copy to clipboard) surface a compact inline approval bubble. Default timeout: auto-deny after 30s of no response.
   - **Full approval:** High-impact or irreversible tools (submit forms, send messages, modify settings, external API calls) require explicit approval with expanded context. No auto-timeout.
8. The tier assignment lives in a policy map keyed by tool category, configurable via a prefs surface (not hardcoded). Initial defaults are conservative (most things require inline confirm).
9. In assistant mode, the policy governs behavior silently. In developer mode, all actions surface regardless of tier (for observability).
10. The page handler passes the authorization decision as metadata on the approval event, so the frontend knows whether to render an auto-act indicator vs. an approval bubble.

**Verification:** Trigger an approval-requiring action. Confirm it appears as an inline prompt in the conversation. Approve it. Confirm the round-trip completes. Separately, trigger a read-only tool and confirm it auto-executes with only a post-hoc indicator.

**Exit criteria:** Approvals are actionable directly from the conversation thread without opening any secondary panel. The authorization policy correctly routes tools into auto-act, inline confirm, or full approval paths.

---

### Phase 3: Proactive contextual suggestions and quality gating

**Goal:** Make the assistant feel aware of the user's context and offer help before being asked. Crucially, suggestions must be high-quality and contextually appropriate, not just mechanically triggered.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/ai/maho_ai_page_context_extractor.cc` (read-only audit)
- `maho-chromium/browser/ai/maho_opencode_adapter.cc` (suggestion generation path)

**Atomic steps:**

1. Define suggestion trigger conditions: page navigation, text selection, form focus, error page detection.
2. Add a `suggestions` field to `AppState` that holds zero or more contextual suggestion objects (label + prefilled prompt).
3. Render suggestions as subtle pills or chips above the composer, distinct from the existing quick actions.
4. Clicking a suggestion prefills the composer and optionally auto-submits.
5. Suggestions dismiss after use or after a timeout/navigation away.
6. The existing quick actions (`Analyze`, `Explain`, `Summarize`) become a subset of static suggestions that always show on idle.
7. Page context changes (new URL, new selection) trigger suggestion refresh through existing `onBrowserContextInjected` events.

**Suggestion intelligence and quality gating:**

8. Suggestions are NOT just string templates applied to page titles. They must demonstrate understanding of the page content. A suggestion like "Summarize this page" is acceptable as a static fallback; a suggestion like "Compare these pricing tiers" on a pricing page demonstrates real intelligence.
9. Implement a two-tier suggestion pipeline:
   - **Tier 1 (instant, local):** Pattern-matched suggestions based on page category signals from the context extractor (error page → "Explain this error", form page → "Help me fill this out", article → "Summarize key points"). These appear immediately on page load.
   - **Tier 2 (async, model-backed):** A lightweight prompt sent to the backend (via the existing adapter) asks the model to generate 1-3 contextual suggestions given the page extract. These replace or supplement Tier 1 suggestions when they arrive (typically 1-2s after page load).
10. Quality gate: Tier 2 suggestions pass through a relevance filter before display. If the model returns generic suggestions indistinguishable from Tier 1, suppress them and keep Tier 1 visible. The filter is simple: reject any Tier 2 suggestion whose semantic content duplicates an already-visible Tier 1 suggestion.
11. Rate limiting: Tier 2 generation fires at most once per distinct page URL (deduplicated by origin + pathname). Rapid tab-switching or back/forward navigation reuses cached suggestions rather than spamming the backend.
12. Confidence signal: If the page context extractor returns minimal content (e.g., blank page, login wall, heavily JS-rendered content not yet available), suppress Tier 2 and show only safe Tier 1 fallbacks.

**Verification:** Navigate to a page. Confirm relevant suggestions appear (Tier 1 immediately, Tier 2 after brief delay). Click one. Confirm it starts a session with contextual input. Navigate to a blank/login page and confirm only safe generic suggestions appear.

**Exit criteria:** The assistant surface offers contextual suggestions that feel intelligent and page-aware, not mechanical. Generic or low-quality suggestions are suppressed by the quality gate.

---

### Phase 4: Session and history as secondary navigation, with conversation continuity

**Goal:** Make sessions accessible but not dominant. History is discoverable through a gesture, never the default screen. Conversation context persists meaningfully across interactions so the assistant remembers what it helped with.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/left_rail.ts` (repurposed or kept behind toggle)
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/ai/maho_opencode_adapter.cc` (session resume path)
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`

**Atomic steps:**

1. Add a sessions/history access point: a small icon or menu in the top bar.
2. On activation, show a slide-over or dropdown listing past sessions (title, date, brief summary).
3. Selecting a past session loads its conversation in read-only mode.
4. A "New conversation" action is always accessible from this menu.
5. The session list does NOT appear by default on first open or during an active conversation.
6. Consider reusing `left_rail.ts` as the sessions slide-over content, but only when explicitly triggered.
7. Active session identity (if any) shows as a subtle indicator in the top bar, not a prominent card.

**Conversation continuity and memory strategy:**

8. Define continuity model: when the user opens the panel, the assistant resumes the most recent active session for that tab/origin by default, rather than starting fresh. "New conversation" is explicit.
9. Cross-session memory: maintain a lightweight per-profile "assistant memory" store (key facts the user has told the assistant or the assistant has learned, e.g., "user prefers concise answers", "user's project uses React"). This is NOT full conversation replay; it's a distilled fact set.
10. Memory injection: on session start, the orchestration service injects relevant memory facts into the system prompt context sent to OpenCode. The adapter's prompt construction path (`maho_opencode_adapter.cc`) appends a memory preamble before the user's first message.
11. Memory management: the user can view and delete memory facts via a "What you remember about me" settings surface (accessible from the sessions menu). No opaque storage.
12. Scope boundaries: memory is per-profile, not per-tab and not cross-profile. Sessions are per-tab/origin for continuity, per-profile for the memory store.
13. Memory writes are conservative: only persist facts the user explicitly states ("I prefer...", "My project is...") or that the assistant explicitly confirms ("I'll remember that..."). No silent inference-based memory writes.

**Verification:** With an active session, close and reopen the panel. Confirm the session resumes automatically. Open the sessions menu. Switch to a past session. Confirm read-only rendering. Start a new session from the menu. Tell the assistant a preference, close/reopen, and confirm the preference persists in a new session.

**Exit criteria:** Sessions are accessible in two taps, invisible by default. The assistant demonstrates continuity across interactions through the memory system without requiring the user to repeat context.

---

### Phase 5: Mode-specific personas and developer/observability expanded mode

**Goal:** Preserve the console/developer experience as an opt-in expanded mode. More fundamentally, make the assistant-vs-developer toggle a behavioral distinction (not just a layout change) by injecting different system prompts and persona definitions per mode.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/timeline.ts`
- `maho-chromium/browser/resources/maho_ai/views/inspector.ts`
- `maho-chromium/browser/resources/maho_ai/views/left_rail.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/ai/maho_opencode_adapter.cc` (system prompt injection)
- `maho-chromium/browser/ai/maho_chat_orchestration_service.cc`

**Atomic steps (layout toggle, existing):**

1. Add a "Developer mode" or "Show details" toggle accessible from the top bar or a settings menu.
2. When active, the shell transitions to the console layout: timeline replaces conversation thread, inspector pane appears, session list becomes persistent.
3. All existing console view files (`timeline.ts`, `inspector.ts`, `left_rail.ts`, `cards.ts`) render in this mode.
4. The toggle state persists in prefs so developer users don't toggle every time.
5. Exiting developer mode returns to the assistant-first conversation view.
6. Developer mode does NOT change the backend behavior, only the presentation layer.

**Mode-specific persona and system prompt injection:**

7. The mode toggle is not purely cosmetic. Each mode carries a distinct system prompt preamble injected at session start and on mode switch mid-session:
   - **Assistant mode:** System prompt instructs the model to respond conversationally, avoid code blocks unless asked, prefer plain-language explanations, keep responses concise for side-panel reading, and never expose internal tool mechanics.
   - **Developer mode:** System prompt instructs the model to be technically precise, show code freely, expose reasoning steps, and treat the user as a peer engineer.
8. Persona injection happens in the adapter's prompt construction path (`maho_opencode_adapter.cc`). The orchestration service determines which persona preamble to prepend based on the current mode state communicated from the frontend.
9. Mid-session mode switch: if the user toggles mode during an active session, the next prompt submission carries the new persona preamble. Previous messages are not rewritten; the behavioral shift is forward-only.
10. The persona definitions live in a structured config (not hardcoded strings in C++) so they can be iterated without recompilation. A reasonable location is a JSON resource file loaded by the orchestration service at startup.
11. Future extensibility: the persona system supports additional modes beyond two (e.g., a "research mode" or "writing mode") by adding entries to the config. The initial implementation ships with exactly two: assistant and developer.

**Verification:** Toggle developer mode on. Confirm timeline, inspector, and session rail appear. Submit a prompt and confirm the response style is technical/verbose. Toggle off. Confirm conversation view returns. Submit a prompt and confirm the response is conversational/concise.

**Exit criteria:** Console features are fully accessible behind a toggle without polluting the default assistant experience. The mode distinction is behavioral (different model output character) not just visual.

---

### Phase 6: Polish and personality

**Goal:** Make the assistant surface feel premium, personal, and alive.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`

**Atomic steps:**

1. Refine conversation bubble styling: assistant messages feel calm and readable, user messages feel lightweight.
2. Add typing/thinking indicator when the assistant is processing (not just tool-running).
3. Refine inline tool indicators to feel natural within conversation flow.
4. Tune animation and transitions for suggestion appearance/dismissal.
5. Ensure the idle first-open state (already reference-locked) still feels coherent with the active conversation state.
6. Dark mode parity check.

**Verification:** Visual review of idle, active, tool-running, and approval states. Coherence check across all states.

**Exit criteria:** The surface feels like one cohesive product across all states, not a patchwork of separate implementations.

---

### Phase 7: Legacy cleanup

**Goal:** Remove dead code paths that served the old console-first or compact-panel-first assumptions.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`

**Atomic steps:**

1. Remove any default-path rendering of timeline/inspector/left-rail that is now behind the developer toggle.
2. Remove orphaned CSS for the compact-panel hero, status card, and console-grid assumptions that no longer apply to the default view.
3. Remove dead imports and unused view branches.
4. Confirm build passes.

**Verification:** Build succeeds. LSP diagnostics clean on changed files. Default view still renders correctly.

**Exit criteria:** No dead code remains from either the compact-panel era or the console-first plan.

## Decision points

These decisions require Phase 0 audit results before they can be resolved:

1. **Does the event stream carry enough metadata for inline tool summaries?** If tool events only carry raw arguments, a minimal Mojo addition may be needed to pass a human-readable summary.

2. **Does the approval event include a human-readable description?** If yes, inline approval bubbles can render it directly. If no, a minimal addition is needed.

3. **Can conversation_thread.ts be extended, or does it need replacement?** If it already renders user/assistant turns as conversation, extend it. If it renders raw events, replace the rendering logic.

4. **Is session restore deterministic enough for reliable history?** If session history is complete and ordered, the read-only view is straightforward. If it's partial, caveats are needed.

5. **Can proactive suggestions be derived solely from existing page context events?** If yes, no new backend signal is needed. If richer triggers are wanted (e.g., error detection), a small page-handler addition may be required.

6. **Where does response shaping live: C++ orchestration layer or frontend utility?** The C++ path (`ShapeRustEvent()`) keeps frontend simple and prevents unshaped content from ever reaching the renderer. The frontend path is easier to iterate. Phase 0 should measure whether the streaming latency budget tolerates a C++ shaping pass per token chunk.

7. **What storage mechanism backs the memory store?** Candidates: browser prefs (simple, limited size), a LevelDB-backed profile store (more capacity, already used by other Chromium features), or a field in the session metadata persisted by the OpenCode adapter. Phase 0 should assess expected memory volume.

8. **Can the authorization policy map be configured per-site or only globally?** A per-site policy (e.g., "auto-act on this trusted internal tool") adds complexity but improves UX for power users. Initial implementation should be global with per-site as a documented future extension.

## Risks / mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Inline tool/approval treatment feels hacky or incomplete | Assistant surface feels unfinished | Phase 0 verifies data availability before Phase 2 commits to inline rendering |
| Proactive suggestions feel spammy or irrelevant | Users disable or ignore the feature | Start with conservative triggers (page nav only); quality gate suppresses generic suggestions |
| Developer mode toggle creates two maintenance surfaces | Double the CSS/layout work | Console views already exist; toggle is primarily a mount/unmount decision, not a parallel rewrite |
| Conversation thread doesn't handle long tool outputs well | Scrolling becomes awkward | Collapse by default; expand on demand; cap inline preview length |
| Narrow side-panel constrains layout options | Can't fit rich inline approvals | Keep approval bubbles compact; "View details" escapes to inspector overlay |
| Session persistence unreliable | History feature shows incomplete data | Verify in Phase 0; surface honest "session may be incomplete" cues if needed |
| Users expect console-first if they've used the current build | Confusion on upgrade | Developer mode toggle is immediately accessible; defaults can be pref-switched |
| Response shaping strips useful detail | Power users lose context | Shaping is bypassed entirely in developer mode; assistant mode preserves full event in store for expand-on-demand |
| Authorization policy too conservative | Assistant feels powerless, asking permission for everything | Default to auto-act for read-only tools; iterate tier assignments based on user feedback |
| Authorization policy too permissive | Trust/safety concern; user feels out of control | Ship conservative; require explicit user opt-in to expand auto-act scope |
| Memory store grows unbounded or stores sensitive data | Privacy/storage concern | Conservative write policy (explicit user statements only); cap at reasonable fact count; clear "forget" affordance |
| Persona injection makes model behave inconsistently | Responses feel schizophrenic on mode switch | Forward-only persona switch; clear visual mode indicator so user understands the context shift |
| Tier 2 suggestion latency too high | Suggestions arrive after user has already started typing | Tier 1 provides instant value; Tier 2 supplements, never blocks; cache aggressively |

## Recommended execution order

1. **Phase 0: Plumbing verification** (first, always; blocking for all presentation work)
2. **Phase 1: Conversation thread as primary surface** (the single most impactful change)
3. **Phase 1B: Response shaping and output translation** (immediately after Phase 1; without this, the conversation thread shows raw tool noise)
4. **Phase 2: Inline approval UX and tool authorization policy** (depends on Phase 1 conversation structure; authorization tiers inform what even reaches the user)
5. **Phase 3: Proactive contextual suggestions and quality gating** (can overlap with Phase 2 after Phase 1B is stable)
6. **Phase 4: Session/history navigation + conversation continuity** (memory strategy can start design in parallel with Phase 3, but ships after it)
7. **Phase 5: Mode-specific personas and developer mode** (persona injection depends on Phase 1B shaping infrastructure; layout toggle can start after Phase 1)
8. **Phase 6: Polish and personality** (after all functional surfaces exist)
9. **Phase 7: Legacy cleanup** (strictly last)

Phase 1B is a hard dependency for Phases 2, 3, and 5 because all three assume shaped output. Phase 5's layout toggle (steps 1-6) can start after Phase 1, but persona injection (steps 7-11) requires the adapter prompt path established in Phase 1B.

Phases 3, 4, and 5 have no hard dependencies on each other and can run in parallel once Phase 1B is complete. Phase 6 depends on all functional phases. Phase 7 is always last.

**What NOT to do first:** Do not complete the four-panel console layout as the primary user-facing target. Do not build left rail, timeline, inspector, and composer as a grid and then try to hide them. Build the assistant surface directly; then mount console views behind a toggle.
