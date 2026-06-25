# Existing Location Bar Utility Bubble Parallel Diagnostic/Fix Plan

## Context

This plan covers the existing location-bar SF utility icon path and the existing anchored native Views bubble path.

Primary scope:

- `chromium/src/chrome/browser/ui/views/location_bar/location_bar_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.*`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.*`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.*`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view_interactive_uitest.cc`

This is not a redesign. The task is to localize and fix the current bug or bugs that prevent the existing SF icon from showing and or the existing anchored bubble from opening and rendering correctly.

## Out of Scope

- new icon architecture
- OS-level modal, sheet, or AppKit-native panel conversion
- WebUI conversion
- cross-platform fallback redesign for the location-bar utility icon
- coordinator refactor unless live-path diagnosis proves it is required

## Working Assumptions

- The existing architecture is correct in principle and should be preserved.
- The correct execution model is proof-based diagnosis first, minimal-diff fix second.
- The upstream Chromium integration seam must be treated as a first-class proof gate, not assumed healthy.
- Content degradation from MahoCore or FFI issues is a separate branch from icon visibility or bubble creation failure.

## File Map

### Primary files

- `chromium/src/chrome/browser/ui/views/location_bar/location_bar_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view_interactive_uitest.cc`

### Secondary reference only

- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.*`

Treat the coordinator as non-central unless live-path diagnosis proves it participates in the current production path.

## Execution Model

- **Phase 0 is sequential** and blocks all later work.
- **Phase 0.3 Decision Gate** determines lane routing after Phase 0 passes.
- **Phase 1 is the main happy-path diagnostic ladder** (Lane A), run only if 0.3 does not already prove the bubble opens.
- **Lane B** handles panel content, provider, MahoCore, and FFI degraded-state analysis. It starts only after Lane A step 1.6 passes or 0.3 proves the bubble already opens.
- **Phase 1 Green Gate** blocks Phase 2 until the happy path is proven or the fix is verified.
- **Phase 2** covers unhappy-path hardening, run only after the Phase 1 Green Gate.
- **Phase 3** extends test coverage, run only after the fix is proven and Phase 2 determines which edge cases need it.

## Phase 0 — Structural Proof Gates

### 0.1 Canonical build baseline

Goal:

- prove the current integrated tree builds using the canonical wrapper path

Verify with:

- the repo wrapper build command, not direct autoninja

Failure means:

- stop and fix the build before any runtime diagnosis

### 0.2 Upstream hook verification

Goal:

- verify that the real upstream Chromium file still contains the utility-icon integration seam

Inspect in:

- `chromium/src/chrome/browser/ui/views/location_bar/location_bar_view.cc`

Required hook points:

- utility icon member and construction path
- hover forwarding into utility icon visibility handling
- show callback or `ShowUtilityPanel` integration

Failure means:

- the feature path is broken at integration level; do not debug bubble-side files yet

### 0.3 Existing interactive baseline

Goal:

- determine whether the existing end-to-end path already passes in automation

Inspect or run:

- `maho_location_bar_utility_icon_view_interactive_uitest`

Failure means:

- use the first failing assertion as the first localization anchor

### 0.3 Decision Gate — Lane Selection

After 0.3 completes, route as follows:

- **If the interactive test passes fully** (bubble opens, content renders): skip Lane A, proceed directly to Lane B for content quality or to Phase 2 for unhappy-path hardening, depending on the reported symptom.
- **If the test fails at icon visibility or bubble creation**: proceed through Lane A starting at the first failing assertion.
- **If the test fails at content/render but bubble opens**: skip Lane A, proceed to Lane B.

## Phase 1 — Happy-Path Diagnostic Ladder

### 1.1 Distinguish expected hover-only UX from actual invisibility

Goal:

- determine whether "icon absent" is expected hover-reveal behavior or a true visibility bug

This distinction must be settled before any runtime hierarchy inspection, because the correct diagnostic path depends on whether the icon is designed to be invisible at rest.

Evidence to advance:

- icon appears under the intended reveal trigger, OR
- code inspection confirms the icon is always-visible (not hover-gated)

Failure means:

- proceed to hover forwarding and visibility-state diagnosis (step 1.3)

### 1.2 Prove the icon exists in the live view hierarchy

Goal:

- confirm `MahoLocationBarUtilityIconView` is actually present at runtime in the relevant location bar

Evidence to advance:

- non-null utility icon view found in runtime hierarchy

Failure means:

- integration or instantiation failure, not bubble failure

### 1.3 Verify hover forwarding chain

Goal:

- prove the upstream hover handler reaches the icon visibility path

Inspect:

- upstream hover method
- utility icon `SetHoverVisible`
- visibility state transitions

Evidence to advance:

- hover trigger causes `SetHoverVisible(true)` and visible-state change

Failure means:

- fix hover plumbing before any click or bubble work

### 1.4 Verify icon geometry and allocation

Goal:

- prove the icon has non-zero bounds and is not clipped by layout even if visible

Evidence to advance:

- sane bounds and visible allocation in the location bar

Failure means:

- fix layout or allocation before click-path work

### 1.5 Verify click reaches the toggle path

Goal:

- prove click interaction reaches the icon’s callback path

Evidence to advance:

- `TogglePanel()` or equivalent callback path invoked

Failure means:

- fix callback binding or event-routing issue

### 1.6 Verify bubble creation and show path

Goal:

- prove the anchored widget is created, non-null, and shown

Evidence to advance:

- valid widget returned and visible near the icon

Failure means:

- bubble creation, anchor validity, or show-path issue

### 1.7 Verify normal teardown resets state correctly

Goal:

- prove normal close resets widget, toggled state, and visible state consistently

Evidence to advance:

- after normal close, the icon and bubble path return to a clean base state

Failure means:

- teardown or state-sync issue

## Lane Structure

### Lane A — UI path (runs after Phase 0 passes)

Run in this lane, strictly serial (each step depends on prior proof):

- 1.1 hover-only UX vs true invisibility (conceptual distinction first)
- 1.2 icon exists in live hierarchy
- 1.3 hover forwarding chain
- 1.4 icon geometry/allocation
- 1.5 click reaches callback
- 1.6 bubble creation/show
- 1.7 normal teardown reset

### Lane B — Content and provider path

Lane B starts only when one of these conditions is met:

- Lane A step 1.6 passes (bubble opens), proving the remaining issue is content/render quality, OR
- Phase 0.3 already proves the bubble opens in automation/runtime but content is wrong

Lane B does NOT run in parallel with Phase 0. It is gated behind evidence that the bubble creation path works.

Run in this lane:

#### B1. Model/provider population check

Goal:

- verify section data is actually populated before rendering

#### B2. MahoCore and FFI readiness check

Goal:

- detect degraded or unavailable content caused by backend readiness, not bubble UI

#### B3. Content-vs-container distinction

Goal:

- prove whether “modal does not render properly” is actually an empty or degraded model rather than a widget creation problem

## Phase 1 Green Gate

Phase 2 does not begin until the following condition is met:

- All Lane A steps through 1.7 pass (the happy path is proven end-to-end), OR
- A fix has been applied for the first localized failure, rebuilt, and the failing gate now passes (see Fix Micro-Gates below)

If Lane B was entered via the 0.3 decision gate, its completion also satisfies this gate for content-branch symptoms.

## Phase 2 — Unhappy-Path Verification and Hardening Gates

These checks are mandatory after the happy path is proven or localized.

### 2.1 Narrow-window geometry

Goal:

- verify the anchored bubble does not clip or misplace itself under narrow browser widths

Check:

- narrow browser window
- bubble visibility and anchor geometry

### 2.2 Hover/bubble lifecycle interaction

Goal:

- verify the icon remains correctly visible while the bubble is open, even when hover state changes

Check:

- open bubble
- remove hover
- confirm icon remains correct while bubble is open
- confirm state resets correctly after close

### 2.3 Browser or tab close while bubble is open

Goal:

- verify no crash, no dangling observer, and no bad teardown during close ordering

### 2.4 Repeated open and close cycles

Goal:

- verify no orphaned state or stuck toggled state after repeated interaction

### 2.5 Action callback plus deactivation interaction

Goal:

- if any callback opens new UI or steals focus, verify the bubble closes intentionally and not in a way that appears broken to the user

## Phase 3 — Test Extension (Post-Fix, Based on Phase 2 Findings)

Phase 3 begins only after:

- The fix is proven (rebuild + rerun passes the formerly-failing gate), AND
- Phase 2 unhappy-path verification has determined which edge cases need coverage

### 3.1 Keep existing interactive path green

Goal:

- retain the existing utility icon happy-path automation as a hard gate

### 3.2 Extend unhappy-path interactive coverage based on Phase 2

Priority additions are selected from Phase 2 findings. Candidates include:

- narrow-window geometry (if 2.1 revealed fragility)
- browser-close or tab-close while bubble is open (if 2.3 revealed risk)
- hover-visible false while bubble is open (if 2.2 revealed state issues)
- repeated open and close (if 2.4 revealed orphan state)

Only add coverage for paths where Phase 2 found actual or near-miss defects.

### 3.3 Note current coverage gap

If panel layout or content rendering changes are required, note that panel-view-specific rendering and mapping are under-tested compared with icon lifecycle.

## Fix Ordering Rules

- Fix only the first proven failing gate.
- Do not refactor coordinator or bubble architecture unless diagnosis proves the live path depends on it.
- Treat coordinator cleanup as optional follow-up, not part of the primary fix path.
- Treat FFI or provider failures as content-branch fixes, not icon-visibility fixes.

## Fix Micro-Gates (Rebuild + Rerun)

After every fix applied to a failing gate:

1. Rebuild using the canonical wrapper build command.
2. Rerun the specific failing gate (the step that originally failed).
3. If the gate now passes, advance to the next step in the ladder.
4. If the gate still fails, the fix is incomplete. Do not advance.

This applies to every fix in Lane A, Lane B, and Phase 2. No fix is considered proven until its target gate passes on rebuild.

## Parallelization Notes

### Safe parallel work

- Phase 0 build baseline and saved test inventory review can run alongside plan prep, but Phase 0 still blocks implementation.
- Lane B preparation can begin once the 0.3 decision gate routes to it, or once Lane A step 1.6 passes.
- Test authoring for unhappy paths can begin after the first live failure point is localized, but test execution waits for the Phase 1 Green Gate.

### Not actually parallel

- Hover diagnosis, click diagnosis, and bubble creation diagnosis are dependency-ordered and must not be treated as independent.
- Lane B does not run alongside Lane A unless 0.3 already proved the bubble opens.
- Do not start provider or FFI debugging before proving the bubble path actually opens, unless the symptom already proves the bubble opens with bad content.

## Verification Summary

### Happy path

- build succeeds
- upstream hooks exist
- icon exists
- icon becomes visible under intended UX
- click reaches callback
- bubble widget opens and shows
- normal close resets state

### Unhappy path

- narrow-window geometry sane
- hover-visible changes do not break bubble-open state
- browser or tab close with bubble open is crash-free
- repeated cycles are clean
- focus-stealing callbacks do not produce broken-feeling close behavior

## Notes for Implementation

- Use the canonical wrapper build flow.
- Bias toward the smallest fix consistent with the first proven failure.
- If the user’s symptom remains vague, use the ladder itself to classify it rather than blocking planning.
- If the bubble opens but content is empty or degraded, branch into provider and FFI diagnosis instead of changing icon or bubble wiring.
