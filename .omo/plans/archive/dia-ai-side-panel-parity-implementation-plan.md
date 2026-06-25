# Dia AI Side Panel Parity Implementation Plan

## Goal

Align Maho's AI side panel UI/UX with Dia's product direction while preserving advanced functionality.

This plan is explicitly **not** about stripping features. It is about making the default first-open and active chat experience feel closer to Dia:

- minimal and input-first on first open
- calm, premium, and non-console-like by default
- progressively disclosed advanced surfaces
- command/ask entry paths that land in the same AI experience
- familiar surrounding browser chrome, with novelty concentrated in chat

This plan incorporates the Oracle review and corrects the earlier weaknesses around state-contract definition, command continuity, approval/tool surfacing, and ownership assumptions.

---

## Product Intent

### Core principle

The benchmark is **Dia parity**, not feature removal.

That means:

- advanced functionality stays in product
- advanced functionality does **not** dominate first impression
- the main shell should feel like a premium contextual AI surface, not a developer console
- command-surface entry and direct panel open should converge into one coherent calm experience

### Non-goals

- removing history/timeline/inspector entirely
- forcing a separate PR split
- treating settings/password/docs/build-script work as proof of AI parity
- making the AI panel more dashboard-like, admin-like, or multi-panel by default

---

## Files In Scope

### A. AI panel core frontend

- `maho-chromium/browser/resources/maho_ai/app.ts`
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/views/cards.ts`
- `maho-chromium/browser/resources/maho_ai/views/inspector.ts`
- `maho-chromium/browser/resources/maho_ai/views/timeline.ts`
- `maho-chromium/browser/resources/maho_ai/views/left_rail.ts`

### B. AI panel backend / contract / runtime continuity audit targets

- `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
- `maho-chromium/browser/ui/webui/maho_ai_prefs.h`
- `maho-chromium/browser/ai/maho_ai_runtime_adapter.h`
- `maho-chromium/browser/ai/maho_opencode_adapter.cc`
- `maho-chromium/browser/ai/maho_opencode_adapter.h`
- `maho-chromium/browser/ai/maho_legacy_chat_adapter.cc`
- `maho-chromium/browser/ai/maho_legacy_chat_adapter.h`

### C. Command to chat continuity

- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.h`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_model.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc`
- `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc`

### D. Theme parity support

- `maho-chromium/browser/ui/theme/maho_color_id.h`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`

### E. Optional surrounding chrome polish

Only if needed after core parity gaps remain:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc`

---

## State Contract

The Oracle review identified the missing full-state contract as the biggest blocker. This section is therefore the source of truth for implementation.

### State A — Idle first-open

#### Trigger

- panel opened with no restored live session selected
- no pending command-prefilled query

#### Primary visible surface

- simple top bar
- large empty center prompt surface
- quiet visual halo / brand-like center anchor
- quick actions (`Analyze`, `Explain`, `Summarize`)
- bottom composer
- page chip
- one primary ask/send CTA

#### Secondary / hidden by default

- sessions list
- history browser
- timeline stream
- inspector tabs
- approval controls
- tool detail cards
- dense runtime metadata

#### Must not leak

- session summary card
- admin / console / workspace framing
- developer-oriented status chrome

---

### State B — Command-prefilled first-open

#### Trigger

- command overlay routes into AI with a pending query

#### Primary visible surface

- same shell as idle first-open
- same visual calmness
- same page chip placement
- prefilled composer text is present quietly

#### Rules

- command entry must not create a second-class or more technical shell
- command entry must not auto-expose advanced overlay
- command entry must not create a visibly different first impression from manual open

---

### State C — Quick-action start

#### Trigger

- `Analyze`, `Explain`, or `Summarize`

#### Rules

- quick action should preserve calm shell feel
- quick action must not cause the surface to jump into dense console mode
- intent may appear in composer or initial turn, but not by introducing extra chrome

---

### State D — Active conversation

#### Primary visible surface

- conversation body
- composer
- quiet context cue
- lightweight activity awareness

#### Secondary / hidden until requested

- sessions/history/timeline/inspector
- dense runtime metadata

#### Rules

- conversation body and composer must be visually primary
- summary/status chrome may exist, but must be de-emphasized or relocated
- the shell must still feel like a chat product, not a console transcript viewer

---

### State E — Tool running

#### Primary visible surface

- lightweight inline progress in the main conversation surface

#### Secondary surface

- full tool details remain in Inspector / Timeline

#### Rules

- user must understand that the assistant is working without opening Inspector
- tool-running status must not explode into a large card-heavy console layout

---

### State F — Approval required

#### Primary visible surface

- lightweight inline approval affordance in the main conversation
- enough explanation for user to know what is being approved

#### Secondary surface

- full arguments/raw payloads/deep details remain in Inspector

#### Rules

- approval must not remain Inspector-only
- inline approval must be lighter than a full inspector panel
- approval treatment must not make the main shell feel admin-heavy

---

### State G — History reopen / read-only session

#### Primary visible surface

- quiet read-only conversation view
- subtle archived/history cue

#### Secondary surface

- full timeline and inspector details

#### Rules

- history discoverability should improve enough to avoid Dia’s current weakness
- history must not become the dominant first-open surface

---

### State H — Advanced overlay open

#### Trigger

- explicit user action only

#### Rules

- sessions / timeline / inspector remain explicitly secondary
- naming, copy, and structure should feel less devtool-like
- overlay may be refined for continuity, but must not reintroduce dense UI into the default impression

---

## Ownership Audit Requirements

The Oracle review explicitly warned against assuming the WebUI shell alone controls behavior.

Before changing presentation, the implementation must confirm ownership and side effects across:

- `app.ts`
- `store.ts`
- `maho_ai_prefs.h`
- `maho_ai_page_handler.*`
- `maho_ai_runtime_adapter.h`
- `maho_opencode_adapter.*`
- `maho_legacy_chat_adapter.*`

Questions to answer early:

1. What is the authoritative source for first-open state?
2. Can prefs/runtime/session restore override the intended calm shell?
3. Are command-prefill and manual-open guaranteed to converge into the same visible state?
4. Are approval/tool-running events available in a form suitable for lightweight inline surfacing?

If any answer is “no,” the parity plan must include the minimal contract change needed, rather than papering over it in the frontend.

---

## File-by-File Change Intent

### `app.ts`

#### Current responsibility

- main render entry
- pending query retrieval
- panel reuse lifecycle

#### Intended change

- enforce a single shell contract for manual open, command-prefilled open, and panel reuse
- prevent command path from creating a visually different AI entry experience

#### Likely mistakes to avoid

- making command-open feel more technical than manual-open
- letting visibility/poll logic override the state contract unexpectedly

---

### `store.ts`

#### Current responsibility

- session lifecycle
- reuse/reset behavior
- advanced state ownership
- pending runtime event buffering

#### Intended change

- lock down which states can appear by default
- preserve existing session/pending-query fixes
- add only the minimum new state required for lightweight inline tool/approval surfacing

#### Likely mistakes to avoid

- overloading main-shell state with inspector-level detail
- allowing session restore to leak advanced or history-first presentation into first-open

---

### `types.ts`

#### Current responsibility

- explicit `AppState` contract

#### Intended change

- encode the parity state matrix cleanly
- make inline tool/approval states explicit if needed

#### Likely mistakes to avoid

- ambiguous state that forces `console_shell.ts` to infer too much

---

### `console_shell.ts`

#### Current responsibility

- primary composition of idle shell, conversation shell, advanced overlay, and local error banner

#### Intended change

- preserve minimal first-open shell
- reduce the prominence of summary/status chrome in active conversation
- move toward a more Dia-like conversation-first presentation
- add lightweight inline tool/approval treatment without turning the shell into a timeline console

#### Likely mistakes to avoid

- deleting discoverability instead of de-emphasizing it
- leaking advanced card patterns into the primary shell
- making overlay changes that degrade the calm first impression

---

### `composer.ts`

#### Current responsibility

- page chip
- input field
- CTA / stop affordance

#### Intended change

- keep page-context cue
- keep command/prefill context subtle
- make the composer feel primary and clean in both idle and active states

#### Likely mistakes to avoid

- adding so much context affordance that the shell becomes busy

---

### `cards.ts`

#### Current responsibility

- shared card primitives and event rendering helpers

#### Intended change

- keep heavy card treatment inside advanced/detail surfaces where possible
- avoid using card-heavy primitives as the main chat language unless necessary

#### Likely mistakes to avoid

- importing timeline/inspector visual density back into the main conversation shell

---

### `timeline.ts`, `inspector.ts`, `left_rail.ts`

#### Current responsibility

- advanced surfaces only

#### Intended change

- preserve full capabilities
- make advanced labeling/copy/tone less devtool-like where needed
- improve discoverability just enough without promoting them to the first impression

#### Likely mistakes to avoid

- overcorrecting into a history-first or admin-first product

---

### `maho_ai.css`

#### Current responsibility

- most of the product feel is encoded here

#### Intended change

- maintain quiet/premium first-open presentation
- weaken active-session status chrome
- support lightweight inline tool/approval treatment
- keep advanced overlay visually secondary

#### Likely mistakes to avoid

- using borders, cards, or spacing that push the shell back toward console aesthetics

---

### `maho_ai_page_handler.*`, `maho_ai.mojom`, adapter files

#### Current responsibility

- event semantics and continuity plumbing

#### Intended change

- prefer no contract change unless parity requires it
- if lightweight inline approval/progress needs additional backend signal, make the minimum change necessary

#### Likely mistakes to avoid

- assuming frontend-only fixes are sufficient without checking event timing and session semantics

---

### Command overlay files

#### Current responsibility

- command → AI entry path
- compact geometry and row interaction

#### Intended change

- keep premium geometry/selection polish
- enforce that command entry lands in the same calm AI shell contract

#### Likely mistakes to avoid

- treating continuity as a late-stage integration detail instead of a core parity requirement

---

### Optional surrounding chrome files

#### Current responsibility

- sidebar and location-bar polish around the AI experience

#### Intended change

- touch only if a real parity gap remains after core shell validation

#### Likely mistakes to avoid

- drifting into broad redesign work without closing a defined parity gap

---

## Phase Plan

### Phase 1 — State contract + ownership audit

#### Goal

Lock the behavior contract before touching shell polish.

#### Files

- `app.ts`
- `store.ts`
- `types.ts`
- `maho_ai_page_handler.*`
- `maho_ai.mojom`
- `maho_ai_prefs.h`
- adapter files

#### Exact steps

1. Enumerate idle, command-prefilled, restored session, active conversation, tool-running, approval-required, history reopen, and advanced-overlay-open states in code terms.
2. Trace which file truly owns entry semantics for each state.
3. Confirm pending-query behavior is the same for manual open and command-open.
4. Confirm session restore cannot override calm first-open rules unexpectedly.
5. Identify whether inline approval/progress needs new event/state plumbing.

#### Why first

Without this, later UI polish can optimize screenshots while leaving the real flow non-Dia.

#### Validation before moving on

- a written state matrix exists
- continuity contract is explicit
- ownership assumptions are verified

---

### Phase 2 — Primary shell refinement

#### Goal

Make the first-open and active conversation shell feel more Dia-like without losing advanced capabilities.

#### Files

- `console_shell.ts`
- `composer.ts`
- `maho_ai.css`
- possibly `store.ts` / `types.ts`

#### Exact steps

1. Preserve the quiet first-open shell.
2. Reduce active-session summary/status prominence.
3. Center the conversation body and composer visually.
4. Keep page context visible but quiet.
5. Verify no dense advanced UI leaks into the primary shell.

#### Why before next phase

Inline activity/approval treatment must fit into the refined primary shell, not the old one.

#### Validation before moving on

- first-open remains calm and premium
- active chat still feels like a chat product, not a console

---

### Phase 3 — Inline lightweight tool / approval treatment

#### Goal

Close the parity gap where important assistant activity is currently too hidden in Inspector.

#### Files

- `console_shell.ts`
- `cards.ts`
- `composer.ts`
- `maho_ai.css`
- possibly `store.ts` / `types.ts`
- minimum backend file changes only if required

#### Exact steps

1. Define a lightweight inline progress treatment for tool-running state.
2. Define a lightweight inline approval affordance for approval-required state.
3. Keep Inspector as the detailed secondary destination.
4. Prevent inline treatment from becoming a big status dashboard.

#### Why before next phase

Advanced overlay refinement should happen after deciding what no longer needs to be inspector-only.

#### Validation before moving on

- user can understand tool/approval state without opening Inspector
- main shell still feels calm

---

### Phase 4 — Progressive disclosure refinement

#### Goal

Keep advanced surfaces powerful but clearly secondary.

#### Files

- `left_rail.ts`
- `timeline.ts`
- `inspector.ts`
- `console_shell.ts`
- `maho_ai.css`

#### Exact steps

1. Refine advanced overlay copy, labels, and tone.
2. Make history discoverability slightly better than Dia’s current weakness.
3. Keep a clear explicit exit path.
4. Re-evaluate whether overlay behavior unnecessarily breaks conversation continuity.

#### Why before next phase

Command continuity must target the final advanced/secondary behavior, not an intermediate one.

#### Validation before moving on

- advanced surfaces remain secondary
- no devtool-like feel dominates the product

---

### Phase 5 — Command to chat continuity polish

#### Goal

Ensure command entry behaves like the same product, not a separate AI mode.

#### Files

- command overlay files
- sidebar container files
- `browser_view.cc`
- color files

#### Exact steps

1. Validate command-prefilled open against the state contract.
2. Preserve geometry and selection behavior that supports premium parity.
3. Verify light/dark command theme support.
4. Verify current-tab AI mode continuity.

#### Validation before moving on

- manual open, quick-action open, and command-open all feel like one AI experience

---

### Phase 6 — Optional surrounding chrome polish

#### Goal

Only polish the surrounding chrome if a specific parity gap remains.

#### Files

- optional sidebar/layout/location-bar set

#### Hard stop rules

Do not touch these files unless:

1. core parity gaps remain after Phases 1–5
2. a concrete unmet parity requirement is identified
3. the change will not make first-open or active chat busier or more devtool-like

#### Validation before moving on

- each optional change has a concrete parity justification

---

### Phase 7 — Runtime validation

#### Goal

Validate real behavior in the built app, not just code structure.

#### Required checks

1. idle first-open
2. command-prefilled first-open
3. quick-action start
4. active conversation
5. tool-running state
6. approval-required state
7. history reopen / read-only state
8. advanced overlay open/close
9. command / manual continuity
10. light/dark command surface
11. built-app runtime verification

---

## Mistake Prevention

### Mistake 1

Polishing idle screenshots while command/prefill/restore states stay inconsistent.

#### Detection

- command-open and manual-open do not share the same first impression

### Mistake 2

Removing status chrome by deleting discoverability instead of de-emphasizing it.

#### Detection

- users lose the ability to understand current session/activity state without opening advanced views

### Mistake 3

Leaving approval/tool surfacing inspector-only.

#### Detection

- a tool or approval event occurs and the main shell gives no meaningful signal

### Mistake 4

Improving history discoverability so aggressively that the default shell becomes busy.

#### Detection

- history/session browsing becomes part of first impression instead of explicit secondary navigation

### Mistake 5

Assuming frontend files alone own behavior.

#### Detection

- runtime/prefs/session semantics override the intended shell unexpectedly

### Mistake 6

Optional chrome polish becomes scope drift.

#### Detection

- a surrounding-chrome file changes without a named unmet parity requirement

---

## Validation Checklist

### Per-phase validation

#### After Phase 1

- full state matrix is written and verified
- command, manual, and restored entry paths are mapped
- no hidden ownership assumptions remain

#### After Phase 2

- first-open remains minimal and premium
- active chat is visually conversation-first

#### After Phase 3

- tool-running is visible without Inspector
- approvals are not inspector-only anymore

#### After Phase 4

- advanced surfaces remain secondary
- history is easier to find without taking over the shell

#### After Phase 5

- command-open and manual-open visually converge
- command palette still feels premium and stable in light/dark mode

#### After Phase 6

- every optional chrome change has a clear parity justification

### Final validation

- Does the panel first open into a minimal, quiet, reference-like state?
- Do command-prefilled opens land in the same calm shell?
- Does the active conversation feel like a chat product rather than a console?
- Are tool-running and approval-required states understandable from the main surface?
- Are sessions/history/timeline/inspector still present but clearly secondary?
- Is history discoverability better than Dia’s weakness without becoming dominant?
- Do all entry points land in one coherent AI experience?
- Does the built app validate the intended behavior, not just the code path?

---

## Final Execution Order

1. Phase 1 — state contract + ownership audit
2. Phase 2 — primary shell refinement
3. Phase 3 — inline lightweight tool / approval treatment
4. Phase 4 — progressive disclosure refinement
5. Phase 5 — command to chat continuity polish
6. Phase 6 — optional surrounding chrome polish
7. Phase 7 — runtime validation

---

## Oracle Review Outcome Incorporated

The prior draft failed Oracle review because:

1. it defined the visual first-open contract but not the full state matrix
2. it treated approval/tool surfacing as optional
3. it assumed frontend ownership too early
4. it sequenced command-to-chat continuity too late
5. it under-specified the minimum context-aware parity target
6. it lacked hard stop rules for overlay/chrome drift

This plan explicitly corrects all six issues.
