# AI Side Panel Redesign - OpenCode Prompt

```text
You are working on a single-PR, end-to-end implementation task.

Do not split this into multiple PRs, follow-up PRs, milestone PRs, or staged deliverables.
Do not propose "Phase 1 in this PR, Phase 2 later."
Do not leave core UI restructuring for a later PR.
You may use internal phases only as execution order inside the same implementation effort.
Everything required for this redesign must be planned, implemented, verified, and delivered in one branch and one final PR.

Your task:
Inspect the existing AI side panel implementation, write an extremely detailed file-by-file and step-by-step execution plan, then implement the redesign in the same task, and verify it thoroughly.

Do not stop after the plan.
Do not provide only a high-level outline.
Do not be generic.
Be concrete enough that execution mistakes are minimized.

Core product and visual constraint:
- The default first-open state must remain visually very close to the provided reference image.
- The default first-open state must feel minimal, quiet, spacious, and premium.
- It must not feel like a developer console, admin tool, workspace dashboard, or multi-panel productivity UI by default.
- The default first-open state must include only:
  1. A simple top bar
  2. A large empty center area
  3. Quick actions: Analyze, Explain, Summarize
  4. A bottom composer with a page context chip and one primary send button
- Session list, history, timeline, inspector, approval UI, dense metadata, event streams, and any other advanced console-like surfaces must NOT be visible in the default first-open state.
- Advanced functionality may still exist, but only behind explicit user action, secondary views, expanded modes, overlays, drawers, or drill-in states.

Important implementation context you must respect:
- The current default mount path is:
  maho_ai.html -> app.ts -> renderConsoleShell(app, store)
- The default first-open UI is currently controlled primarily by:
  - browser/resources/maho_ai/app.ts
  - browser/resources/maho_ai/store.ts
  - browser/resources/maho_ai/types.ts
  - browser/resources/maho_ai/views/console_shell.ts
  - browser/resources/maho_ai/views/composer.ts
  - browser/resources/maho_ai/maho_ai.css
- The following advanced UI files exist and are compiled into the bundle, but are currently not mounted in the default shell:
  - browser/resources/maho_ai/views/left_rail.ts
  - browser/resources/maho_ai/views/timeline.ts
  - browser/resources/maho_ai/views/inspector.ts
- These advanced files must not accidentally leak into the default first-open state.
- browser/resources/maho_ai/views/cards.ts is shared utility infrastructure and may be used carefully, but session-heavy cards must not dominate the default first-open state.
- store.ts already tracks advanced state such as sessions, events, tool calls, approval state, and selectedInspector even if those views are not mounted.
- app.ts is the only render entry point and should be treated as the primary gate for layout branching.
- browser/ui/webui/maho_ai/maho_ai_ui.cc injects pendingAiQuery and isMahoAiThinMode through loadTimeData.
- isMahoAiThinMode is currently injected but not consumed by the TypeScript frontend.
- store.ts bootstrap currently auto-selects a session via currentSessionId = sessions[0]?.sessionId || null, which may conflict with preserving a minimal first-open state.
- Pure visual/default-state work should avoid unnecessary changes to:
  - browser/ui/webui/maho_ai/maho_ai_page_handler.cc
  - browser/ui/webui/maho_ai/maho_ai.mojom
  unless the redesign truly requires contract changes.

Your process must be:

==================================================
STEP 1. INSPECT THE CURRENT IMPLEMENTATION
==================================================

Audit the current AI panel carefully before making any edits.

You must inspect, at minimum, the following files:

Frontend entry and state:
- browser/resources/maho_ai/maho_ai.html
- browser/resources/maho_ai/app.ts
- browser/resources/maho_ai/store.ts
- browser/resources/maho_ai/types.ts

Default-shell rendering:
- browser/resources/maho_ai/views/console_shell.ts
- browser/resources/maho_ai/views/composer.ts
- browser/resources/maho_ai/maho_ai.css

Advanced but currently unmounted UI:
- browser/resources/maho_ai/views/left_rail.ts
- browser/resources/maho_ai/views/timeline.ts
- browser/resources/maho_ai/views/inspector.ts
- browser/resources/maho_ai/views/cards.ts

WebUI and native host integration:
- browser/ui/webui/maho_ai/maho_ai_ui.cc
- browser/ui/webui/maho_ai/maho_ai_prefs_registration.cc
- browser/ui/webui/maho_ai_prefs.h
- browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc
- browser/ui/views/command/maho_command_overlay_view.cc
- browser/ui/views/side_panel/maho_ai_side_panel_coordinator.cc

You must determine:
- how the first-open state is created
- how pendingAiQuery flows into the composer
- how currentSessionId changes the visible UI
- how bootstrap and session restoration affect the first render
- which files directly control visible structure
- which files only prepare advanced secondary states
- where advanced UI could accidentally leak into the default state

==================================================
STEP 2. WRITE AN EXTREMELY DETAILED PLAN BEFORE EDITING
==================================================

Before implementation, produce a very detailed execution plan.
Do not keep it high level.
Do not summarize vaguely.
Do not skip file-level detail.

Your planning output must include all of the following sections:

## 2.1 Current state summary
Write a concrete summary of:
- what the current default first-open UI actually looks like
- what the current active-session UI looks like
- how the current implementation differs from the reference image
- which current elements create visual drift from the reference
- which current behaviors break the intended minimal first-open state

## 2.2 File inventory
For every relevant file, provide:
- exact file path
- current responsibility
- whether it should be modified, preserved, deferred, or used only in advanced mode
- whether it affects default state, secondary state, or both
- why it matters
- risk level if changed

You must be explicit.
Do not combine unrelated files into one vague bullet.

## 2.3 File-by-file change plan
For every file that may need changes, provide:
- exact file path
- the exact current problem in that file
- the exact intended change
- whether the change is structural, behavioral, visual, or state-related
- whether the change is required for default first-open state, active conversation state, secondary advanced state, or multiple states
- dependencies on other files
- likely mistakes to avoid in that file
- validation notes specific to that file

This section must be detailed enough that someone could work file by file without guessing.

## 2.4 Phase-by-phase implementation plan
Break the work into strict internal phases in execution order.
These phases are for sequencing only, not for separate PRs.

For each phase, include:
- phase name
- goal
- exact files touched in that phase
- why those files belong together
- why this phase must happen before the next phase
- what risks exist in the phase
- what must be validated before moving on

## 2.5 Step-by-step execution plan inside each phase
Inside every phase, list exact ordered implementation steps.
Do not say "update UI as needed."
Do not say "refine styles."
Be explicit.

Examples of the detail expected:
- inspect this function
- change this branch condition
- stop auto-selecting sessions here
- preserve pendingAiQuery prefill here
- reduce header copy here
- hide this status card in idle state
- move this control into secondary mode
- keep this file untouched to avoid advanced-state leakage
- adjust these CSS tokens only
- do not broaden layout branching beyond app.ts

Each step must be specific enough to reduce mistakes.

## 2.6 Default first-open state contract
Define in strict detail what the default first-open state must be.

Include:
- exact visible elements
- exact hidden elements
- exact visual hierarchy
- exact interaction affordances allowed from the default state
- which text, controls, or metadata should be minimized
- what would count as visual drift from the reference image
- what must not appear in the first impression

This section must be strict and reviewable.

## 2.7 Progressive disclosure plan
Define exactly how advanced UI should be handled.

You must explain:
- which advanced UI surfaces remain in the product
- which files implement them
- where they should live
- how the user reaches them
- what interaction reveals them
- what conditions must be true before they render
- what must never be visible by default

Be explicit that left_rail.ts, timeline.ts, and inspector.ts are advanced-layout-only unless the redesign explicitly introduces a guarded secondary mode.

## 2.8 UI states and transitions
List every required UI state, including at minimum:
- default first-open idle state
- prefilled idle state
- active conversation state
- loading state
- boot error state
- secondary expanded state
- session browsing state
- history state
- approval state
- any other state needed by the redesign

For each state, define:
- visible elements
- hidden elements
- trigger to enter the state
- trigger to exit the state
- files responsible for the state
- risk of accidental leakage into the default state

## 2.9 Mistake Prevention
Add a dedicated section called exactly:
Mistake Prevention

In that section, list:
- likely implementation mistakes
- files that are easy to over-change
- places where the default first-open state could accidentally become too dense
- places where advanced UI could accidentally leak into the default render tree
- store/bootstrap behaviors that could accidentally force non-minimal first-open state
- CSS changes that could unintentionally shift the entire product away from the reference image
- sequencing mistakes to avoid
- regression risks
- how to detect each mistake early

This section must be practical, not generic.

## 2.10 Validation checklist
Provide both:
- per-phase validation checklist
- final validation checklist

The checklist must include:
- visual checks
- interaction checks
- state transition checks
- first-open behavior checks
- advanced UI containment checks
- entry-point consistency checks
- regression checks

The final checklist must explicitly ask:
- Does the panel first open into a minimal reference-like state?
- Does it still look very close to the reference image at a glance?
- Are left rail, history, timeline, inspector, approval UI, and other dense advanced surfaces hidden by default?
- Does the default state feel like a quiet AI side panel instead of a console?
- Do advanced UI surfaces appear only after explicit user action?
- Do all entry points land in the same correct first-open experience?

## 2.11 Final execution order
End the planning section with the exact recommended execution order:
- first
- second
- third
- etc.

==================================================
STEP 3. IMPLEMENT THE REDESIGN IN THE SAME TASK
==================================================

After writing the detailed plan, implement the redesign in the same task.
Do not stop after the plan.
Do not postpone core work for a later PR.
Do not turn the plan into separate implementation waves.

Implementation rules:
- Keep the default first-open state visually very close to the reference image.
- Keep the first-open state minimal.
- Do not expose advanced UI in the default first-open render path.
- Do not let session/history/inspector/timeline/approval UI dominate the first impression.
- Prefer focused changes over unnecessary rewrites.
- Treat app.ts as the main render gate.
- Be careful with store.ts bootstrap because currentSessionId restoration can change first-open behavior.
- Be extremely careful with console_shell.ts and composer.ts because they directly control visible default-state structure.
- Use maho_ai.css as the main visual-adjustment surface.
- Avoid touching Mojo/C++ contracts unless truly necessary.
- Avoid accidental broadening of advanced UI visibility.

==================================================
STEP 4. VERIFY THOROUGHLY
==================================================

After implementation, verify:
- the default first-open state
- query-prefilled first-open state
- active conversation state
- any advanced secondary state introduced by the redesign
- all user entry points
- no leakage of advanced UI into the default render path
- no visual drift away from the reference image

==================================================
STEP 5. DELIVER AS ONE COMPLETE PR
==================================================

This must land as one coherent redesign in a single PR.
No separate follow-up PRs for core layout cleanup.
No "later" PR for default-state cleanup.
No staged rollout of the essential redesign.

Important rules:
- one PR only
- plan and implementation both required
- no generic summary without file-level detail
- no skipping the detailed plan
- no implementation that drifts far from the reference
- no default first-open state that feels like a console
- no accidental mounting of left_rail.ts, timeline.ts, or inspector.ts into the default render path unless they are strictly gated behind explicit advanced mode entry
- if uncertainty exists, call it out explicitly in the plan and resolve it carefully during implementation
```
