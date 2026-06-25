# Zen Boost UI Parity Recovery Plan

**Created:** 2026-05-29
**Status:** Active recovery plan, narrowly scoped to the remaining visual and structural parity work. Subordinate to every prior reference and contract document in the Boost stack; reopens nothing those documents settled.
**Reference commit:** `5bbbadd77be956deaf91633cd4f0611403b0ba53`

This plan exists because a screenshot of the current Maho Boost editor, taken against the live build at the time of writing, shows functional UI scaffolding that is **not** parity-matched to the Zen reference. The runtime, the contract, and the typed editor surface have substantially landed. The visual and structural parity push has not. That gap is the entire subject of this document.

This plan does not declare the Boost feature complete. It does not relitigate the Zen UX shape. It does not restart the project. It reopens exactly one workstream: making the editor, the surrounding chrome surfaces, and the launch paths look and behave like Zen at the pinned commit, with strict acceptance gates that block completion this time.

---

## 1. Position in the Plan Stack

This plan sits **below** every existing Boost contract and reference document. Where it disagrees with any of them, the upstream document wins for its own subject. Resolution order, higher beats lower:

1. Upstream Zen code at `5bbbadd77be956deaf91633cd4f0611403b0ba53`. The pinned commit is the parity baseline.
2. `.sisyphus/plans/assets/zen-boost-g1-reference-lock-2026-05-27.md`. The G1/G2 lock is the parity contract: pinned commit, frozen field set, frozen launch API, frozen architectural ownership.
3. `.sisyphus/plans/zen-boost-source-validated-ui-runtime-parity-plan-2026-05-28.md`. The code-validated re-statement and corrected execution path. Sections 4 and 7 of that plan are the parity definition this recovery plan binds to.
4. `.sisyphus/plans/zen-boost-source-level-parity-reset-plan-2026-05-26.md`. The canonical reset contract.
5. `.sisyphus/plans/zen-boost-shell-fill-runtime-parity-plan-2026-05-28.md`. The execution layer that covers Phases 1, 2, and 6 of the source-validated plan; this recovery plan binds against its workstream definitions for Streams C, D, E, and F.
6. `.sisyphus/plans/zen-boost-remediation-plan-2026-05-27.md`. The historical slab-landing reference.
7. **This plan.** Recovery layer for visual and structural parity. Does not modify the documents above.

If a future reviewer reads this plan and wants to relitigate the Zen UX shape (single vs multiple boosts per domain, bubble vs floating window, embedded sidebar editor vs separate window, per-site JS as part of parity, URL-pattern scoping), redirect them to documents (1) through (4) and require a code-level counter-citation against the pinned commit before reopening. Those questions are closed.

---

## 2. The Honest Status (What This Plan Calls Out)

### 2.1 What is real or substantially complete

These items are on disk, exercised by the current build, and should not be re-implemented or rolled back. The recovery push **inherits** them.

- **Domain-keyed multi-boost storage on the Rust side.** `maho/crates/maho-core/src/boost_manager.rs` exposes the parity-shaped API: `list_for_domain`, `get`, `create`, `update`, `delete`, `set_active`, `get_active`, `compose_css`. The storage schema in `maho/crates/maho-storage/src/sqlite.rs` carries the `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)` row pattern.
- **Typed Boost event channel.** `maho/crates/maho-types/src/events/core_update.rs` carries the `BoostCreated`, `BoostUpdated`, `BoostDeleted`, `BoostActiveChanged` variants. Mutations in `boost_manager.rs` emit them through the dispatcher.
- **Live runtime CSS bridge.** `maho-chromium/third_party/maho/maho_bridge.cc` implements `GetBoostInjectionCss(MahoCore*, const char* url)` correctly, resolving the active boost per domain and composing CSS through Rust.
- **Per-tab CSS injection with active-change reinjection.** `maho-chromium/browser/net/maho_boost_injection_handler.cc` injects the composed CSS through an isolated world on primary-frame committed navigations and on same-document SPA/hash navigations, **and** subscribes to the typed event channel so `BoostUpdated` and `BoostActiveChanged` reinject without a full navigation. Empty CSS removes the prior `<style id="maho-boost-style">` element.
- **G2-shaped launch API in shell form.** `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.cc` exposes `MahoBoostWindowController::GetForBrowser(Browser*)` returning a per-`Browser` instance, with `ShowForActiveDomain(content::WebContents*)`, `Hide()`, and `IsShowing()`.
- **Typed Mojom contract on the wire.** `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` carries the parity method list with a typed `BoostInfo` and a `BoostUpdate` struct rather than a JSON-string compromise. The G3 contract freeze is in.
- **Functional editor surface with knob coverage.** `maho-chromium/browser/resources/maho_boost/app.ts` is a real Mojo-bound editor that reads and writes the G1 frozen field set through the page handler. Knobs round-trip. Custom CSS round-trips. The active selection round-trips.
- **Active reinjection fix.** Switching the active boost for a domain triggers reinjection on every live `WebContents` on that domain without requiring a navigation, on the strength of the typed event channel.
- **Command palette entry point.** `open_boost_editor` in `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` calls into the controller.

The runtime works. The contract is correct. The editor is functional. Cross-tab live update on the same domain is observable. **None of that is in scope for rollback.**

### 2.2 What is open for UI parity

These items are why this plan exists. The current build renders the editor and the surrounding surfaces, but the rendered result is not Zen-shaped. A screenshot taken at the time of writing demonstrates this directly: the editor opens, knobs respond, persistence works, and yet a side-by-side against Zen at the pinned commit shows divergence at almost every visual layer.

1. **Editor window chrome and host.** The editor is still parented as a hosted view, not as Zen's freely positionable native top-level `Zen:BoostEditor` window with native title bar, native close affordance, native resize, and no auto-dismiss on focus loss. Whether the current host is a residual modal dialog (`maho_boost_dialog.cc`) or an early `views::Widget` that has not been promoted to top-level chrome, the user-visible result is that the editor does not behave like a real OS window. Phase 3 of the source-validated plan named this gap; it is not closed.
2. **Visual parity of the editor body.** The editor exposes the G1 knobs, but the rendering of those knobs (typography, spacing, control style, section grouping, the live-preview swatch, the boost-list rail, the active indicator, the empty-state pane, the new-boost affordance) does not match Zen. This is the gap the screenshot makes obvious. Knob coverage is a contract; visual parity is a separate target this plan now treats as first-class.
3. **Sidebar / site data panel parity.** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.cc::ParseBoosts()` returns a real list now, but the rendered pane (`maho_sidebar_boosts_view.cc`) does not match Zen's site data panel surface (`ZenSiteDataPanel.sys.mjs` plus `site-data.inc`) in row layout, active indicator, per-row active-toggle affordance, per-row delete affordance, or the "New boost" button placement. The data plumbing is in; the surface presentation is not.
4. **Launch-path coverage and QA.** The command palette entry works. D1 (URL bar / location bar quick action), D2 (tab context menu), D3 (page-info route), and D4 (sidebar route into the editor) per the shell-fill plan are either still neutralized, still gated, or wired but not yet QA'd against the actual Zen launch sites at the pinned commit, including the additive `New Boost` global action from `ZenUBGlobalActions.sys.mjs`.
5. **Subordinate hosting on `chrome-untrusted://`.** The G1/G2 lock prefers `chrome-untrusted://maho-boost-editor` (or the chosen subordinate untrusted host) for the in-window content frame, with the trusted `chrome://maho-boost` host kept unreachable. The current build needs to be audited end-to-end to confirm the trusted host is genuinely unreachable as a navigable user surface. The cold artifacts (`kMahoBoostHost`, `kMahoBoostURL`, the navigable registration) need to be either removed or repurposed strictly as the subordinate untrusted host.
6. **Remaining verification.** The targeted parity harness, the rendered-output baseline against Zen on the five canonical validation cases, the Appium regression coverage for D1 through D4, and the manual parity QA pass are not yet at the level the source-validated plan's Section 7 requires. Verification has not closed.

### 2.3 The current screenshot, named honestly

The screenshot of the live Boost editor proves three things at once:

- The editor is real, opens, and is interactive. The substantial runtime and contract work is not theoretical.
- The editor is **not** visually parity-matched to Zen. Window chrome, layout density, control styling, list rail, and active indicator are all visibly different from the reference.
- "Functional" is not the bar. The bar is the rendered result side-by-side against Zen at the pinned commit on the same domain with the same knob values.

Future reviewers reading this plan should treat that screenshot as the closing argument for why this recovery plan is needed. Anyone arguing the editor is already at parity should be required to produce a side-by-side screenshot pair (Maho left, Zen right, identical knob values, identical domain) before that claim is accepted.

---

## 3. Inherited Contract (Locked, Not Relitigated)

The recovery push inherits the following from the documents above and does **not** reopen any of them:

1. Multiple boosts per domain. Exactly one (or none) is active at a time.
2. Separate floating top-level editor window. Not a bubble. Not a sidebar-embedded editor. Not a navigable `chrome://` page.
3. Site data panel surface lists per-domain boosts and exposes create, edit, active-toggle, and delete flows from browser chrome.
4. Domain is the scope key. URL patterns stay out.
5. No per-site JavaScript injection.
6. Knob set is the G1 frozen list verbatim: `boostName`, `dotAngleDeg`, `dotPos`, `dotDistance`, `secondaryDotAngleDegDelta`, `secondaryDotPos`, `brightness`, `saturation`, `contrast`, `fontFamily`, `textCaseOverride`, `sizeOverride`, `enableColorBoost`, `smartInvert`, `autoTheme`, `zapSelectors`, `customCSS`, `changeWasMade`.
7. Launch API for every chrome entry point is `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. No anchor argument. No URL navigation as a substitute.
8. The two G1 correctness fixes on the runtime side are part of parity: form-control / pseudo-element exemption refinement on the color-boost path, and SVG image-document propagation through the `nsPresContext` override path or its Chromium-overlay equivalent.
9. The Phase 1.5 rendering-hook verdict stays open per the G1 lock and is independent of the UI parity work this plan covers.

If any of (1) through (9) is challenged in review, redirect to the upstream documents. This plan does not amend them.

---

## 4. Recovery Workstreams

Six workstreams. The first four are visual and structural parity, the fifth is launch-path closure, the sixth is verification. Numbering does not align with the shell-fill plan's A through F; this plan owns its own scope and references the shell-fill plan's workstreams where ownership overlaps.

### 4.1 W1. Editor window chrome and host promotion

**Goal.** Replace whatever the current host is with a real native top-level `views::Widget` that behaves like a Zen-style `Zen:BoostEditor` window.

**Acceptance signals.**

- The editor window is a real top-level OS window. It appears in the OS window list (Dock/Mission Control on macOS, taskbar on other platforms). It is **not** parented as a child of the parent browser window.
- It has native title bar and native close, minimize, and resize affordances per the host platform's standard chrome.
- It is freely positionable. Dragging the title bar moves it. Resizing the edges resizes it. The position is not anchored to the parent browser's address bar, toolbar, or any other view.
- It does **not** auto-dismiss on focus loss. Clicking back into the parent browser leaves the editor open and visible.
- It is **not** modal. The parent browser remains fully interactive while the editor is open.
- Closing and reopening through the controller round-trips Mojom state. Per-`Browser` controller scope is preserved.
- The legacy modal dialog implementation (`maho-chromium/browser/ui/views/boost/maho_boost_dialog.cc/h`), if it still exists in the tree, is removed in the same commit that promotes the host. No coexistence path.

**Likely file targets.** `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.cc/h`, `maho-chromium/browser/ui/views/boost/maho_boost_window_view.cc/h`, `maho-chromium/browser/ui/views/boost/maho_boost_dialog.cc/h` (removal), `maho-chromium/browser/ui/views/boost/BUILD.gn`.

### 4.2 W2. In-window content host on `chrome-untrusted://`

**Goal.** Confirm and, where needed, complete the move of the editor's web content frame to a subordinate `chrome-untrusted://` host. Confirm the trusted `chrome://maho-boost` route is not navigable.

**Acceptance signals.**

- The editor's content frame loads from `chrome-untrusted://maho-boost-editor` (or whichever subordinate untrusted host the team has settled on).
- Manually entering `chrome://maho-boost` in the omnibox does not open the editor and does not return any navigable page. Either the navigation fails outright, or the host is not registered with the WebUI controller factory at all.
- A tree-wide grep for `kMahoBoostHost`, `kMahoBoostURL`, and `chrome://maho-boost` returns either: no live references, references inside a debug-only path gated by `is_debug`, or references in the historical plan documents under `.sisyphus/plans/`.
- The Mojom contract continues to bind correctly when the host is on the untrusted scheme. Bindings round-trip without privilege errors.

**Likely file targets.** `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.cc/h`, `maho-chromium/components/constants/webui_url_constants.h`, the WebUI controller factory registration site, the resources `BUILD.gn`.

### 4.3 W3. Editor body visual parity

**Goal.** Bring the rendered editor body to parity with Zen at the pinned commit, on the same five validation cases used elsewhere in the plan stack, plus a top-level visual diff of the empty state and the populated state.

**Acceptance signals (screenshot-level).**

A reviewer can produce a screenshot pair, Maho left, Zen right, on each of the following scenarios, and the rendered editor body is **visually equivalent** to a degree that survives reasonable platform-styling differences. "Visually equivalent" here means: identical layout grid, identical section grouping and ordering, identical control types per knob (slider, toggle, dropdown, color swatch, multi-line CSS textarea), identical active-row indicator semantics, identical empty-state composition, identical "New boost" affordance placement.

- **Editor empty state.** Domain has no boosts yet. The editor renders Zen's empty composition, including the call to action that creates the first boost.
- **Editor populated state, single boost.** One boost on the domain, marked active. The list rail, the active indicator, the knob panel, and the live-preview area match Zen.
- **Editor populated state, multiple boosts.** Three or more boosts on the domain, one active. The list rail shows the full set. Switching the selected boost in the rail swaps the knob panel without dismissing the window.
- **Knob panel, color section.** Each color knob (`enableColorBoost`, `dotAngleDeg`, `secondaryDotAngleDegDelta`, `dotPos`, `dotDistance`, `secondaryDotPos`, `brightness`, `saturation`, `contrast`, `autoTheme`, `smartInvert`) renders with the same control type and the same labelling shape as Zen.
- **Knob panel, typography section.** `fontFamily`, `textCaseOverride`, `sizeOverride` render with the same control types.
- **Knob panel, structural section.** `zapSelectors` and `customCSS` render with the same editor affordances (multi-row textarea or equivalent).

**Acceptance signals (behavioral).**

- The "New boost" affordance creates a new boost for the active domain and selects it in the rail. The new row appears without a window reload.
- Per-row delete removes the row, advances the active selection (or clears it) per Zen's behavior, and propagates through the event channel so the sidebar pane (W4) refreshes within one event tick.
- Per-row active toggle calls `SetActive` for the row's id on the active domain. The rail and the runtime injection both update without a navigation.
- The "selected boost" cursor in the editor is independent of the "active boost" marker, matching Zen's `ZenBoostsEditor.mjs` behavior. Selecting a row for editing does **not** automatically activate it; activation is a separate explicit affordance.
- `changeWasMade` is tracked correctly per record. Closing and reopening the editor preserves the dirty marker semantics.

**Likely file targets.** `maho-chromium/browser/resources/maho_boost/app.ts`, `maho-chromium/browser/resources/maho_boost/boost.html`, `maho-chromium/browser/resources/maho_boost/boost.css`, `maho-chromium/browser/resources/maho_boost/BUILD.gn`. Page handler bindings (`maho_boost_page_handler.cc/h`) only if a contract gap surfaces during the rebuild; the IDL itself is frozen at G3 and is not in scope to reshape inside W3.

### 4.4 W4. Site data panel / sidebar parity surface

**Goal.** Bring the per-domain Boost surface in the sidebar (or the URL-bar-adjacent panel, per the M5a decision in the source-validated plan) to parity with Zen's site data panel.

**Acceptance signals (screenshot-level).**

A reviewer can produce a screenshot pair, Maho left, Zen right, on each of:

- **Empty state.** Domain has no boosts. The pane shows Zen's empty composition with the create affordance.
- **Populated state.** One or more boosts. Each row exposes: an active indicator (matching Zen's visual treatment), a click target that opens the editor with the row's boost selected, a per-row active-toggle affordance, and a per-row delete affordance.
- **Active toggle.** Toggling active on a row that is not currently active moves the active marker and triggers reinjection on every open tab on the domain.

**Acceptance signals (behavioral).**

- The pane subscribes to the typed event channel for `BoostCreated`, `BoostUpdated`, `BoostDeleted`, `BoostActiveChanged` on the active domain and refreshes within one event tick.
- The "New boost" affordance routes through the controller (`MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`) and lets the editor create the new boost. The pane does not own creation directly.
- Per-row activate calls `SetActive`. Per-row delete calls `Delete`.

**Likely file targets.** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.cc/h`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.cc/h`. If the M5a decision flips to a URL-bar-adjacent panel, additional location-bar view files come in scope; the default for this plan stays the sidebar pane.

### 4.5 W5. Launch-path coverage and QA

**Goal.** Restore D1 through D4 plus the `New Boost` global action, behind the default-off `kMahoBoostEnabled` feature flag where applicable, and confirm each entry point opens the new top-level editor window through the frozen launch API exactly.

**Acceptance signals.**

- D1 (URL bar / location bar quick action) is wired to `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. No anchor view. No URL navigation. With the gate on, a click on the action opens the editor; with the gate off, the action is not user-discoverable.
- D2 (tab context menu Boost commands) is wired through the same launch API. With the gate on, the menu items appear and route correctly; with the gate off, they are not present.
- D3 (page-info Boost route) is wired through the same launch API.
- D4 (sidebar route into the editor) is wired through the same launch API. The sidebar pane (W4) has a "New boost" affordance and per-row "open in editor" routing that both go through the controller.
- The `New Boost` global action analog from `ZenUBGlobalActions.sys.mjs` is registered as a command palette entry **and** as a global keyboard shortcut. Both gated.
- A QA matrix is recorded somewhere reviewable: for each entry point, "opens editor", "shows correct domain", "creates new boost when no boost exists", "selects existing active boost when one exists" are all green.

**Likely file targets.** `maho-chromium/browser/ui/views/location_bar/...`, `maho-chromium/browser/ui/context_menu/...`, `maho-chromium/browser/ui/site_control/...`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.cc/h`, the feature-flag definition site for `kMahoBoostEnabled`.

### 4.6 W6. Verification, parity QA, and remaining harness coverage

**Goal.** Close the verification gap that the source-validated plan's Section 7 requires and that the shell-fill plan's Stream F was originally tasked with. This plan does not redefine those targets; it makes them blocking gates again.

**Acceptance signals.**

- Per-knob round-trip test exists in `maho/appium-tests/` for every G1 knob: open editor, edit knob, observe page change, persist, reload, observe again, tear down.
- Cross-tab live update test on the same domain holds.
- Rendered-output baseline against Zen on the five canonical validation cases (color knob change, smart invert on light pages, smart invert on dark pages, zap selectors hiding visible elements, custom CSS overriding a known selector) plus a typography knob composition case is in place and green.
- Appium coverage for D1, D2, D3, D4, plus the `kMahoBoostEnabled` gate flip.
- Manual parity QA against Zen at the pinned commit, scripted: open the editor from each registered entry point, edit at least one knob per category, observe the page change, persist across reload, switch active boost, delete a boost, close and reopen the window, confirm cross-tab live update on the same domain, confirm the editor stays open on focus loss to the parent browser.
- A screenshot bundle attached to the verification record. Maho on the left, Zen on the right, on the same domain with the same knob values. The reviewer's bar is visual equivalence per W3 and W4.

**Likely file targets.** `maho/appium-tests/`, the harness skeletons under whatever path the verification suite settles on, the feature-flag definition site for the final gate flip.

---

## 5. Strict Acceptance Criteria

The recovery push is **done** when **every** item below holds at the same time. Any single failure blocks completion.

1. **Window chrome.** The editor opens as a real native top-level OS window with native title bar, native close affordance, native resize, freely positionable, not anchored, not modal, not auto-dismissing on focus loss. The OS window list shows it as a top-level entry.
2. **Subordinate hosting.** The editor's content frame loads from `chrome-untrusted://maho-boost-editor`. The trusted `chrome://maho-boost` host is not user-navigable; either the route is unregistered or the navigation fails outright.
3. **Editor body visual parity.** Maho-left / Zen-right screenshot pairs on the six W3 scenarios (empty, single, multi, color section, typography section, structural section) demonstrate visual equivalence at the layout-grid, control-type, and section-grouping level. A reviewer can produce these pairs on demand.
4. **Editor body behavioral parity.** New boost, per-row delete, per-row active toggle, selected-vs-active independence, and `changeWasMade` semantics all match Zen as described in W3.
5. **Site data panel parity.** The sidebar (or URL-bar-adjacent) pane renders Zen-shaped rows for the active domain, refreshes on event-channel notifications, supports per-row active and delete, and routes "New boost" through the controller. Maho-left / Zen-right screenshot pairs hold for the empty state, the populated state, and the active-toggle moment.
6. **Launch paths.** D1, D2, D3, D4, the command palette `open_boost_editor`, and the `New Boost` global action all route through `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. None anchors a bubble. None navigates to a URL. None auto-creates a boost without going through the editor.
7. **Cross-surface refresh.** Mutating a boost in the editor refreshes the site data panel within one event tick. Toggling active in the panel reinjects on every live tab on the domain within one event tick.
8. **Runtime correctness fixes.** Form-control / pseudo-element exemption on the color-boost path is observed correct on test pages. SVG image-document propagation is observed correct on a test page that embeds an SVG document.
9. **Cleanup.** Cold artifacts (`kMahoBoostHost`, `kMahoBoostURL`, the navigable `chrome://maho-boost` registration, `maho_boost_dialog.cc/h` if still present) are gone or repurposed strictly as the subordinate untrusted host. Tree-wide grep for `chrome://maho-boost` returns only non-live or historical references.
10. **Verification commands green.**
    - `cd maho && bun run typecheck`
    - `cd maho && bun run test`
    - `python3 maho-chromium/build/scripts/build_maho.py`
    - `autoninja -C out/Default chrome` from `chromium/src`
    - The targeted parity harness (per-knob round-trip, cross-tab live update, rendered-output baseline against Zen on the five validation cases plus a typography case).
    - Appium regression suite covering D1 through D4 plus the gate flip.
11. **Manual parity QA pass.** Recorded against Zen at the pinned commit, with the screenshot bundle attached. Reviewer signoff explicitly references "visual parity confirmed against `5bbbadd`".
12. **Feature gate flip.** A single atomic commit flips `kMahoBoostEnabled` to default-on **only after** items (1) through (11) hold.

If any one of (1) through (12) fails, the gate does not flip. "Functional but not visually parity-matched" is **not** an acceptable terminal state. That has been the trap on prior iterations and is the entire reason this plan exists.

---

## 6. Execution Sequence

Sequencing for the recovery push. Each step is a candidate atomic commit boundary; the sequence is strict where dependencies bind, parallel where they do not.

### Step 1. Parity baseline screenshot capture

- Capture Zen at the pinned commit on a clean profile. Domain set: a curated list of three or four domains that exercise color, typography, zap, and custom CSS knob ranges. Capture the editor empty state, the editor populated state with one boost, the editor populated state with three boosts, the site data panel empty state, the site data panel populated state, and a side-by-side of a boosted page (active vs no-boost) for one validation domain.
- Store the captures under `.sisyphus/plans/assets/zen-boost-ui-parity-baseline-2026-05-29/` (or a sibling location agreed on with the reviewer).
- Record the exact knob values used per capture so Maho captures can reproduce them deterministically.

This step is non-code. It is the parity reference for every subsequent acceptance check.

### Step 2. Window host promotion (W1)

- Promote the editor host to a native top-level `views::Widget` per W1.
- Remove the legacy modal dialog implementation in the same commit if it still exists.
- Verify acceptance signals (1) end-to-end: open from command palette, observe top-level OS window with native chrome, free positioning, no auto-dismiss, no modality.

This step gates the rest because every other visual-parity acceptance requires the host to be a real top-level window first. A landing built on the modal dialog cannot achieve W3 or W4 parity.

### Step 3. Subordinate hosting confirmation (W2)

- Audit the editor content frame's host. If it is already on `chrome-untrusted://maho-boost-editor`, confirm and document. If not, complete the move.
- Audit the navigable `chrome://maho-boost` registration. Remove or gate.
- Run the tree-wide grep audit and record the result.

Step 3 can run in parallel with Step 2 but must complete before Steps 4 and 5.

### Step 4. Editor body visual parity (W3)

- Rebuild the editor body's layout, control palette, section grouping, list rail, active indicator, empty state, and "New boost" affordance against the Step 1 baseline captures.
- Do not change the Mojom contract or the page handler unless a contract gap surfaces. If one does, file it as a separate atomic commit with the reviewer; do not silently widen the IDL.
- Iterate Maho captures against Step 1 baselines until W3 acceptance signals hold. A reviewer signs off the screenshot pair for each of the six scenarios.

Step 4 dominates the calendar. Treat it as the primary visual workstream and assign accordingly.

### Step 5. Site data panel parity (W4)

- Rebuild the sidebar Boost pane's row layout, active indicator, per-row affordances, and "New boost" placement against Step 1 baselines.
- Wire the event-channel subscription. Confirm one-tick refresh.
- Iterate captures until W4 acceptance signals hold.

Step 5 can run in parallel with Step 4 once the typed event channel and the Step 2 host are both in.

### Step 6. Launch-path closure (W5)

- Restore D1, D2, D3, D4, and the `New Boost` global action behind the gate. Wire each through the frozen launch API.
- Record the QA matrix.

Step 6 binds on Step 2 (the host must exist) but does not bind on Step 4 or Step 5.

### Step 7. Verification harness (W6)

- Land per-knob round-trip tests, cross-tab live update tests, rendered-output baseline harness.
- Land Appium coverage for D1 through D4 plus the gate flip.
- Run the manual parity QA pass and attach the screenshot bundle.

Step 7 starts in parallel with Steps 4 through 6 (skeletons can land early) and closes after all of them.

### Step 8. Gate flip

- One atomic commit flips `kMahoBoostEnabled` to default-on only after every Section 5 acceptance criterion holds.

---

## 7. Non-Goals (Explicit)

To prevent the recovery push from sprawling and to prevent reopening settled questions, the following are out of scope for this plan and any commits made under it:

- **Reopening the Zen UX shape.** Multi-vs-single, bubble-vs-window, embedded-vs-separate, JS-as-parity, URL-pattern-vs-domain are all closed by the upstream documents. Not in scope.
- **Reshaping the Mojom contract.** G3 is in. The IDL stays frozen at the parity shape unless an unavoidable gap surfaces during W3, in which case a separate atomic commit handles it under reviewer signoff.
- **Reshaping the Rust contract.** The typed knob set, the event channel, and the storage schema are in. Not in scope.
- **Phase 1.5 rendering-hook verdict.** Stays open per the G1 lock. Independent of this plan.
- **A second, parallel Boost feature behind a different flag.** No coexistence path. The recovery push reshapes the existing surfaces; it does not introduce a new branch.
- **Restarting the project.** The runtime works. The contract is correct. The editor is functional. This plan recovers the visual and structural parity layer; it does not relitigate the rest.
- **Claiming completion before Section 5 holds.** Functional UI is not parity. The bar is the screenshot pair against Zen at the pinned commit.

---

## 8. Open Questions That Stay Open

These are inherited from the source-validated plan and are not closed by the recovery push:

1. **Phase 1.5 rendering-hook verdict.** Whether the runtime stays CSS-only or moves to a deeper hook. Independent of UI parity. Stays open.
2. **Site data panel host.** Sidebar pane (default) vs URL-bar-adjacent panel. The W4 acceptance criteria are written against either host; the choice is a routing detail and lands at W4 implementation time, with reviewer signoff.
3. **`changeWasMade` wire shape.** Whether the typed `BoostUpdate` carries it explicitly, computes it implicitly from field equality, or routes it as an editor-local-only signal. The W3 behavioral acceptance covers the user-visible result; the wire-shape question can be settled inside W3 without amending the IDL if the field equality computation is acceptable.
4. **Cross-window live update scope.** Same-domain across tabs in one window is the floor. Whether the same-domain live update propagates across browser windows on the same profile, or across profiles, is not pinned by this plan. The W6 verification matrix's cross-tab tests pin same-window same-domain; broader behavior is a follow-up.
5. **Cold artifact disposition.** Deletion is the default per the G1/G2 lock. The W2 audit settles whether any debug-only retention is warranted under `is_debug` gating. Default action remains deletion.

These stay open. Anything else in the Zen Boost UX shape is closed by the upstream documents and reopening it requires a code-level counter-citation against the pinned commit.

---

## 9. Notes for Future Readers

This plan exists because functional UI and parity UI are not the same target. The runtime, the contract, and the editor's data plumbing landed substantially. The visible result, at the time of writing, is not Zen-shaped. Closing that gap is a discrete workstream with its own acceptance criteria. Treating it as "polish to be done after enabling" is the failure mode this plan is built to prevent.

When Section 5's acceptance criteria all hold and the gate flips, this plan is archived alongside the reset, remediation, parallel, shell-fill, source-validated, and G1/G2 lock documents as the recovery layer for the period that closed Zen Boost UI parity. Subsequent Boost work scopes against a fresh plan.

If a future reviewer reads this and finds the recovery scope smaller than expected, that is intentional. The runtime and contract layers are not in scope. The visual and structural parity layer is. That is the gap the screenshot proves, and that is the gap this plan closes.
