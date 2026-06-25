# Zen Boost Source-Validated UI/Runtime Parity Plan

**Created:** 2026-05-28
**Status:** Active reference plan. Source-of-truth corrected against upstream Zen code at the pinned reference commit. Supersedes any prior framing that treated the Zen Boost UX shape as ambiguous.
**Reference commit:** `5bbbadd77be956deaf91633cd4f0611403b0ba53`

This document exists because earlier rounds of plan work drifted toward screenshot-driven and inference-driven framings of how Zen Boosts behave. Source-level reading of Zen at the pinned commit settles the shape questions. This plan re-grounds the Maho execution path on what Zen's code actually does, names the divergence on the Maho side, and lays out a phased plan to close the gap.

This plan does not edit, contradict, or reopen the existing reference and execution documents. Specifically it sits on top of:

- `.sisyphus/plans/assets/zen-boost-g1-reference-lock-2026-05-27.md` (the frozen G1/G2 facts, including the pinned reference commit, the field set, and the launch API shape)
- `.sisyphus/plans/zen-boost-shell-fill-runtime-parity-plan-2026-05-28.md` (the shell-fill execution layer)
- `.sisyphus/plans/zen-boost-source-level-parity-reset-plan-2026-05-26.md` (the canonical contract)
- `.sisyphus/plans/zen-boost-post-reset-parallel-parity-plan-2026-05-27.md` (the parallel scheduling layer)
- `.sisyphus/plans/zen-boost-remediation-plan-2026-05-27.md` (the slab landing plan)

Where this plan and any of those documents disagree, the upstream document wins for its own subject. This plan adds a code-validated re-statement and a corrected execution path; it does not rewrite the contract.

---

## 1. Objective

Pin a single, code-validated reference document for the Zen Boost UX shape on the Maho side. Three goals:

1. **Lock the Zen UX model in writing.** Multiple boosts per domain. Exactly one active boost per domain. A separate floating top-level editor window. A site data panel that lists those boosts and exposes create, edit, toggle, and delete flows. No bubble. No sidebar-embedded editor. No content-injected JS. No URL-pattern scoping.
2. **Name the exact Maho divergence.** Inventory the on-disk Maho state (window controller, dialog, WebUI app, page handler, sidebar pane) and call out where Maho today differs from Zen at the pinned commit, without softening the call.
3. **Hand the streams a phased execution sequence** that lands UI/runtime parity on top of the runtime CSS bridge that already exists, with verification gates and explicit definition-of-done criteria.

This document is not a re-plan of the shell-fill plan. It is a source-of-truth pin plus a corrective overlay. The shell-fill plan still owns granular workstream wiring; this plan owns the Zen-shape grounding and the corrected conclusions that the rest of the plan stack now builds on.

---

## 2. Source-of-Truth Hierarchy

When reviewers, streams, or follow-up plans disagree on what Zen does or what Maho should do, resolve in this order. Higher beats lower.

1. **Upstream Zen code at `5bbbadd77be956deaf91633cd4f0611403b0ba53`.** The files listed in Section 3 are the authoritative shape. If a screenshot, a prior summary, or a memory says otherwise, the code wins.
2. **`.sisyphus/plans/assets/zen-boost-g1-reference-lock-2026-05-27.md`.** The G1/G2 lock pins the Zen reference commit, the data field set, the architectural ownership decision, and the launch API. It is the parity contract.
3. **This plan (`zen-boost-source-validated-ui-runtime-parity-plan-2026-05-28.md`).** Code-validated re-statement plus the corrected execution path. Where this disagrees with anything below it, this wins.
4. **`.sisyphus/plans/zen-boost-source-level-parity-reset-plan-2026-05-26.md`.** The canonical reset contract. Still authoritative for everything it covers; this plan refines, not relaxes.
5. **`.sisyphus/plans/zen-boost-shell-fill-runtime-parity-plan-2026-05-28.md`.** The execution layer for the next two phases. Still owns workstream-level file ownership and milestone wiring.
6. **`.sisyphus/plans/zen-boost-post-reset-parallel-parity-plan-2026-05-27.md`.** The scheduling layer for the parallel fan-out.
7. **`.sisyphus/plans/zen-boost-remediation-plan-2026-05-27.md`.** Historical slab-landing reference.

Anything cited as "the Zen behavior" without a path inside the pinned commit is not authoritative for this push.

---

## 3. Upstream Zen Code-Backed Findings

These are observed at `5bbbadd77be956deaf91633cd4f0611403b0ba53`. Each finding maps to a Zen file. Treat them as facts, not interpretations.

### 3.1 Storage and data model (`ZenBoostsManager.sys.mjs`)

- Boosts are persisted per-domain. A domain entry holds a set of boosts, not a single record.
- Each domain entry tracks an `activeBoostId`. Exactly one of the domain's boosts is active at a time. The active id can be cleared, leaving the domain with stored boosts but no active selection.
- Each boost carries the typed knob fields the G1 lock froze: `boostName`, `dotAngleDeg`, `dotPos`, `dotDistance`, `secondaryDotAngleDegDelta`, `secondaryDotPos`, `brightness`, `saturation`, `contrast`, `fontFamily`, `textCaseOverride`, `sizeOverride`, `enableColorBoost`, `smartInvert`, `autoTheme`, `zapSelectors`, `customCSS`, `changeWasMade`. The set is the contract.
- Mutations (`create`, `update`, `delete`, `setActive`) emit change notifications that downstream consumers (style application, site data panel, editor) subscribe to.
- No URL-pattern scoping. The scope key is the domain string.

### 3.2 Editor surface (`ZenBoostsEditor.mjs` + `zen-boost-editor.inc.xhtml`)

- The editor is a separate floating top-level window. Zen opens it via `Services.ww.openWindow(...)` with a window type of `Zen:BoostEditor`. It is not a bubble, not a panel, and not embedded in any sidebar.
- The editor window owns the editor UI directly. The XHTML template wires the typed knob controls to the active boost record.
- The editor reads and writes through `ZenBoostsManager`, with optimistic local updates and propagation through the change-notification channel back into runtime application.
- The editor supports a per-window "selected boost" cursor across the domain's boost list. Switching the selected boost in the editor swaps the knob values; mutating active state goes through `setActive` against the manager.

### 3.3 Site data panel surface (`ZenSiteDataPanel.sys.mjs` + `site-data.inc`)

- The site data panel renders a list of the current domain's boosts.
- Each row exposes:
  - Selecting the boost as the editor target.
  - Toggling the active boost (a domain has at most one active row at a time; selecting "active" on a different row moves the active marker).
  - Deleting the boost.
- The panel exposes a "create new boost" affordance that triggers opening the editor window with a new boost prepopulated for the current domain.
- All of these flows originate from browser chrome, not from page content. There is no content-script entry into create/edit/toggle.

### 3.4 Runtime application (`nsZenBoostsBackend.cpp/.h` + `ZenBoostStyles.sys.mjs` + actors)

- The runtime is CSS plus a C++ color/layout backend. There is no per-site JavaScript injection.
- `ZenBoostStyles.sys.mjs` composes per-knob CSS from the active boost record.
- `nsZenBoostsBackend.cpp` carries the deeper rendering hooks for color filtering and layout adjustments. At the pinned commit, two correctness fixes are in place that any parity port must preserve:
  - **Form-control / pseudo-element exemption.** The color-boost path exempts form controls and pseudo-element rendered content with refined coverage so that interactive controls and generated content render correctly under the boost.
  - **SVG image-document propagation.** Boost state propagates into SVG image documents through the `nsPresContext` override path. A parity implementation that only handles top-level HTML documents is incomplete.
- Actors (`ZenBoostsChild.sys.mjs`, `ZenBoostsParent.sys.mjs`) carry the boost record from the parent process to the content process so that `ZenBoostStyles` composes against the correct active boost. The child applies; the parent owns truth.

### 3.5 Launch sites (chrome-side)

- The browser chrome has multiple entry points that converge on opening the editor window for the active domain:
  - The site data panel "edit" / "new boost" affordance.
  - A `New Boost` global action contributed by `ZenUBGlobalActions.sys.mjs` (additive, recorded in the G1 lock as a launch-site inventory fact).
  - URL-bar / location-bar adjacent affordances integrated through Zen's chrome layer.
- All of them call into the manager and the editor opener. None of them inject anything into page content. None of them anchor a bubble.

### 3.6 What Zen does **not** do

These are also code-backed findings, expressed as negatives so they cannot drift back into a future plan:

- Zen does not run a manager-page UX as the primary editor.
- Zen does not anchor the editor in a bubble or popover.
- Zen does not embed the editor inside a sidebar pane.
- Zen does not gate boost flows on URL patterns.
- Zen does not inject per-site JavaScript as part of the Boost runtime.
- Zen does not collapse to a single boost per domain.

---

## 4. Corrected Conclusions (Zen Is Model A)

This is the section that ends prior drift. It is intentionally blunt.

**Zen Boost is model A.** Specifically:

1. **Multiple boosts per domain, one active.** Each domain owns a set of boosts. Exactly one (or none) is active at a time. Selecting active is a swap, not a toggle on a single record.
2. **Separate floating top-level editor window.** The editor lives in its own native window, opened via `Services.ww.openWindow(...)` as a `Zen:BoostEditor` type. No bubble, no sidebar, no in-tab page.
3. **Site data panel list + create/edit/toggle flows.** The site data panel is the canonical chrome surface for browsing the current domain's boosts and triggering create, edit, active-toggle, and delete. The editor window is the deep-edit surface. The two surfaces share the data through `ZenBoostsManager`'s change-notification channel.

What this section explicitly forecloses:

- The "model B" framing where Boosts are a single per-domain blob with toggles is rejected. It does not describe Zen at the pinned commit.
- The "is the editor a bubble or a window?" question is closed. It is a window.
- The "is the site data panel just a status pill?" framing is closed. It is a list with create, edit, toggle, and delete flows.
- The "is per-site JS part of Boost?" question is closed. It is not.
- The "is the scope URL patterns or domain?" question is closed. It is domain.
- The "is the Phase 1.5 deeper-hook decision still live?" question is **independent** of the above. The hook strategy stays open per the G1 lock; the UX shape does not.

If a reviewer reopens any of these as if they were unsettled, point them at the file paths in Section 3 of this plan plus the G1 lock, and require a code-level counter-citation against the pinned commit before re-litigating.

---

## 5. Current Maho State and Exact Divergence from Zen

Every claim below maps to a real file at the time of writing, 2026-05-28. The point is to call out divergence honestly so the execution plan in Section 6 lands the right work.

### 5.1 What Maho already has that matches Zen's shape

- **Domain-keyed multi-boost storage on the Rust side.** `maho/crates/maho-core/src/boost_manager.rs` exposes the parity-shaped API: `list_for_domain`, `get`, `create`, `update`, `delete`, `set_active`, `get_active`, `compose_css`. Storage at `maho/crates/maho-storage/src/sqlite.rs` carries the `domain_boost_state` row pattern.
- **Live runtime CSS bridge.** `maho-chromium/third_party/maho/maho_bridge.cc` implements `GetBoostInjectionCss(MahoCore*, const char* url)` correctly. It pulls the active boost JSON for the URL's host, then calls `maho_core_boost_compose_css` for the resolved id.
- **Per-tab CSS injection.** `maho-chromium/browser/net/maho_boost_injection_handler.cc` is a real `WebContentsObserver` that injects the composed CSS through an isolated world on primary-main-frame committed navigations, including same-document SPA/hash navigations. Empty CSS removes any prior `<style id="maho-boost-style">` element.
- **G2-shaped launch API.** `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.cc` exposes `MahoBoostWindowController::GetForBrowser(Browser*)` returning a per-`Browser` instance, with `ShowForActiveDomain(content::WebContents*)`, `Hide()`, and `IsShowing()`.
- **Working editor WebUI app.** `maho-chromium/browser/resources/maho_boost/app.ts` is a real Mojo-bound editor. It calls `getDomain`, `listBoosts`, `getActiveBoost`, `getBoost`, `createBoost`, `updateBoost`, `deleteBoost`, `setActiveBoost`, `composeCss`, `closeDialog`. It renders a domain header, a sidebar list of boosts with active indicator, a name editor, a custom CSS editor, and a composed-CSS preview pane. The CRUD surface is in.
- **Working command-palette entry.** `open_boost_editor` in `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` calls `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`.

### 5.2 Where Maho diverges from Zen at the pinned commit

These are the gaps the execution plan in Section 6 closes. They are real and they matter.

1. **Editor host is a modal dialog, not a separate floating top-level window.** `maho-chromium/browser/ui/views/boost/maho_boost_dialog.cc` builds a `views::DialogDelegate` with `SetModalType(ui::mojom::ModalType::kWindow)` and a fixed `kDialogWidth` of 480 by `kDialogHeight` of 600, hosting a `views::WebView` on `maho::kMahoBoostURL`. Zen's editor is a freely positionable, non-modal, native top-level window opened via `Services.ww.openWindow`. The Maho dialog is anchored to the parent browser window and behaves modally. That is not parity.
2. **Editor host is `chrome://maho-boost`, not the G2-preferred `chrome-untrusted://` subordinate.** The dialog loads `kMahoBoostURL`, which is the trusted host. The G1/G2 lock says the preferred fresh path for the in-window content host is `chrome-untrusted://`, with the trusted manager-page surface staying unreachable. The current dialog reuses the cold trusted host that the G2 lock wanted unreachable.
3. **Editor knob coverage is shallow.** The current `app.ts` UI exposes `name`, `customCss`, and an active toggle. The G1 frozen field set requires every knob in the list (`boostName`, `dotAngleDeg`, `dotPos`, `dotDistance`, `secondaryDotAngleDegDelta`, `secondaryDotPos`, `brightness`, `saturation`, `contrast`, `fontFamily`, `textCaseOverride`, `sizeOverride`, `enableColorBoost`, `smartInvert`, `autoTheme`, `zapSelectors`, `customCSS`, `changeWasMade`) to round-trip without loss. The Mojom `BoostInfo` type and the `updateBoost(string, changesJson)` JSON path do not yet enforce typed coverage for the full set.
4. **Mojom IDL is a JSON-string compromise, not the parity contract.** `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` carries `BoostInfo`, `getDomain`, `listBoosts`, `getActiveBoost`, `getBoost`, `createBoost`, `updateBoost(id, changesJson)`, `deleteBoost`, `setActiveBoost`, `composeCss`, `closeDialog`, plus a `Page::OnBoostsChanged` push. The shape is in the right neighborhood, but `updateBoost` smuggles the change set as a JSON string instead of a typed `BoostUpdate` struct, and the typed knob fields are not on the wire. G3 (Mojom contract freeze) has not landed.
5. **Site data panel surface does not exist.** Zen's `ZenSiteDataPanel.sys.mjs` plus `site-data.inc` is a domain-scoped list with create / edit / toggle / delete flows in the URL-bar-adjacent panel. On the Maho side, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.cc::ParseBoosts()` returns `{}`, and `maho_sidebar_boosts_view.cc::ReloadBoosts()` falls into the empty-state branch on every call. No site data panel equivalent renders the per-domain boost list with the chrome flows. (The sidebar pane is one possible host, but it is not Zen's site data panel; it is the closest current Maho surface and it is currently decorative.)
6. **No typed change-notification event family on the Rust side.** `maho/crates/maho-types/src/events/core_update.rs` does not carry `BoostCreated`, `BoostUpdated`, `BoostDeleted`, or `BoostActiveChanged` variants. The shell-fill plan's M1b is the place this gap closes; it has not closed yet. Without it, cross-tab live preview and cross-surface refresh (editor to site data panel) are impossible without polling or duplicate plumbing.
7. **No per-window `Zen:BoostEditor` analog.** Maho's controller routes `ShowForActiveDomain` into `MahoBoostDialog::Open`, which creates one global `g_boost_dialog_widget`. Zen opens a fresh window per request with full top-level chrome. Behaviorally Maho is closer to a singleton modal than to Zen's per-call floating window.
8. **G1 correctness fixes are not yet ported.** Form-control / pseudo-element exemption refinement and SVG image-document propagation through `nsPresContext` are not in the bridge or the injection handler today. The runtime side composes color filters and zap selectors through Rust, but the C++ rendering-hook backend that Zen carries (`nsZenBoostsBackend`) has no Maho counterpart yet.
9. **Legacy method debt on the Rust API.** `boost_manager.rs` still exposes `get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost`, `restore_active_boost`. These are not parity, they are debt.
10. **Launch sites D1-D4 not restored.** Per the shell-fill plan, the URL-bar quick action, tab context menu, page-info route, and sidebar route into the editor are still neutralized. Only the command-palette `open_boost_editor` is wired.

### 5.3 Net effect

Today, a user can open the editor through the command palette. The editor is a 480x600 modal dialog hosting a WebUI on the trusted `chrome://maho-boost` host. CRUD on `name` and `customCss` works through Mojo. Active toggle works. Composed CSS injects on the active tab. Live cross-tab updates do not work. Most G1 knobs are not in the UI. There is no site data panel list. The dialog is modal and fixed-size, not a floating top-level window.

Functionally the runtime works. Structurally and visually, the surface is several decisions away from Zen.

---

## 6. Execution Plan to Reach UI/Runtime Parity

This section is a phased execution path from the divergence in Section 5 to the parity targets in Section 4. It maps to, but does not replace, the workstreams in `.sisyphus/plans/zen-boost-shell-fill-runtime-parity-plan-2026-05-28.md`. Where the shell-fill plan owns granular file lists, this section adds the source-validated framing and explicit ordering.

### Phase 0. Plan acceptance and re-grounding (M0)

- Accept this plan as the active source-validated reference for the Zen Boost UX shape.
- Pin the corrected conclusions in Section 4 in any future plan or PR description that touches Boost UX.
- Confirm with the active streams (A through F) that the corrected model lands without re-opening the shape questions.

**Likely file targets.** None. This is a reading and acceptance gate.

**Verification.** Reviewers signal acceptance. Subsequent plans cite this document by filename when stating "Zen is model A".

### Phase 1. Rust contract and event channel (M1)

Maps to shell-fill plan Stream A.

- M1a. Drop legacy methods from `maho/crates/maho-core/src/boost_manager.rs` (`get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost`, `restore_active_boost`) after a references audit.
- M1b. Add typed Boost event variants on `maho/crates/maho-types/src/events/core_update.rs`: `BoostCreated(Boost)`, `BoostUpdated(Boost)`, `BoostDeleted(BoostId, domain)`, `BoostActiveChanged(domain, Option<BoostId>)`. Wire emission through `event_dispatcher.rs`.
- M1c. Confirm the storage schema in `maho/crates/maho-storage/src/sqlite.rs` carries the typed knob columns and the `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)` shape.
- M1d. Re-run `cd maho && bun run typecheck` and `cd maho && bun run test`. Add per-knob round-trip tests, golden `compose_css` outputs, event-emission tests.

**Likely file targets.** `maho/crates/maho-core/src/boost_manager.rs`, `maho/crates/maho-core/src/event_dispatcher.rs`, `maho/crates/maho-core/src/maho_core.rs`, `maho/crates/maho-types/src/boost.rs`, `maho/crates/maho-types/src/events/core_update.rs`, `maho/crates/maho-types/src/events/shell_event.rs`, `maho/crates/maho-storage/src/sqlite.rs`, `maho/crates/maho-ffi/src/lib.rs`.

**Verification.** Both `bun run typecheck` and `bun run test` pass. Grep for the dropped legacy method names returns no live callers. Event emission tests pass.

### Phase 2. Mojom contract freeze (G3)

Maps to shell-fill plan Gate G3 plus Stream C.

- M2a. Replace the JSON-string `updateBoost` path with a typed `BoostUpdate` struct on `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom`. `BoostInfo` widens to cover the G1 frozen field set.
- M2b. Regenerate TS bindings. Update `maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.cc/h` to compile against the new IDL. Page handler bodies can stay stubbed where the UI does not yet drive them.
- M2c. Land a contract README next to the IDL noting that the Mojom field set is the wire shape for the G1 frozen list and citing this plan and the G1/G2 lock.

**Likely file targets.** `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom`, `maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.cc/h`, `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.cc/h`, `maho-chromium/browser/resources/maho_boost/maho_boost.mojom-webui.js.d.ts`.

**Verification.** `python3 maho-chromium/build/scripts/build_maho.py` is green. A narrow `autoninja -C out/Default chrome` from `chromium/src` is green. No live UI surface depends on the new fields yet.

### Phase 3. Editor window: dialog -> top-level floating window (M3)

This is the structural correction. Maps to shell-fill plan Stream C and to Section 5 divergence #1, #2, #7.

- M3a. Replace `MahoBoostDialog` (modal, 480x600 fixed, trusted host) with a non-anchored, non-modal, native top-level `views::Widget` plus content frame, owned by `MahoBoostWindowController`. The widget should be freely positionable, resizable, with native title and close chrome, and must not auto-dismiss on focus loss. This matches Zen's `Services.ww.openWindow(... 'Zen:BoostEditor' ...)` shape.
- M3b. Inside the widget, host the editor on `chrome-untrusted://maho-boost-editor` (or the chosen subordinate untrusted host) so the trusted `chrome://maho-boost` host stays unreachable per G2.
- M3c. `ShowForActiveDomain` constructs the widget if absent, parents the editor view, and shows it. `Hide` closes the widget. `IsShowing` reports widget state. Per-`Browser` controller continues to be the lifecycle owner.
- M3d. Retire `maho_boost_dialog.cc/h` once the new path is live and no entry point references it.

**Likely file targets.** `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.cc/h`, new `maho-chromium/browser/ui/views/boost/maho_boost_window_view.cc/h` (or refactor of the existing dialog into a non-modal window view), `maho-chromium/browser/ui/views/boost/maho_boost_dialog.cc/h` (eventually removed), `maho-chromium/browser/ui/views/boost/BUILD.gn`, `maho-chromium/components/constants/webui_url_constants.h` (untrusted host constant).

**Verification.** Opening the editor produces a real native top-level window. The window is freely positionable, has native chrome, does not auto-dismiss on focus loss, is not modal, is not anchored. Closing and reopening preserves Mojom round-trip. The `chrome://maho-boost` host is no longer a navigation target for the editor.

### Phase 4. Editor knob coverage (M4)

Maps to shell-fill plan Stream C and to Section 5 divergence #3, #4.

- M4a. Replace the JSON-string `updateBoost` path on the page handler with the typed `BoostUpdate` struct from G3.
- M4b. Expand `maho-chromium/browser/resources/maho_boost/app.ts` so every G1 knob has a real control: color knobs (`brightness`, `saturation`, `contrast`, `enableColorBoost`, `smartInvert`, `autoTheme`, the dot/secondary-dot fields), typography knobs (`fontFamily`, `textCaseOverride`, `sizeOverride`), zap (`zapSelectors`), and `customCSS`. The `boostName` and `changeWasMade` fields stay surfaced through the existing name field plus an internal dirty marker.
- M4c. Wire debounced `Update` calls so editor changes propagate to Rust through the page handler, then back out through the Phase 1 event channel into the bridge subscriber for live preview.

**Likely file targets.** `maho-chromium/browser/resources/maho_boost/app.ts`, `maho-chromium/browser/resources/maho_boost/boost.html`, `maho-chromium/browser/resources/maho_boost/boost.css`, `maho-chromium/browser/resources/maho_boost/BUILD.gn`, `maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.cc/h`.

**Verification.** Each G1 knob round-trips: open editor, change knob, observe page change, close, reopen, read knob value. Cross-tab live preview holds: change a knob in the editor while a second tab on the same domain is open, observe the second tab update without a navigation.

### Phase 5. Site data panel surface (M5)

Maps to shell-fill plan Stream E and to Section 5 divergence #5. This is the chrome surface that closes the "Zen has a site data panel" gap.

- M5a. Decide whether the site data panel host is the existing sidebar Boost pane (`maho_sidebar_boosts_view`) or a URL-bar-adjacent panel (closer to Zen's `ZenSiteDataPanel`). The default for this plan is the sidebar Boost pane filled out, with a follow-up to relocate to a URL-bar surface if review demands it. Both options route through the same Mojom client.
- M5b. Fill `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.cc::ParseBoosts()` to return the actual per-domain list. Subscribe `MahoSidebarBoostsView` to the Phase 1 event channel for `BoostCreated`, `BoostUpdated`, `BoostDeleted`, `BoostActiveChanged` on the active domain.
- M5c. The pane renders one row per boost for the active domain, with: an active-selection indicator, a click target that opens the editor with the row's boost selected, a per-row active-toggle affordance, and a per-row delete affordance. Plus a "New boost" button that calls `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)` and lets the editor create the new boost.

**Likely file targets.** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.cc/h`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.cc/h`, possibly a new URL-bar-adjacent view if M5a flips that direction.

**Verification.** The pane lists every boost for the active domain. The active row is visually distinct. The pane refreshes within one event tick when the editor mutates state. Per-row activate, per-row delete, and "New boost" route through the parity API.

### Phase 6. Launch sites and runtime correctness (M6)

Maps to shell-fill plan Stream D plus Stream B's correctness work.

- M6a. Restore D1 (URL-bar / location-bar Boost quick action), D2 (tab context menu Boost commands), D3 (page-info Boost route), D4 (sidebar route into the editor). All gated behind a default-off `kMahoBoostEnabled` feature flag. Each entry point calls `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)` exactly. No anchor view. No URL navigation as a substitute.
- M6b. Register the `New Boost` global action analog to Zen's `ZenUBGlobalActions.sys.mjs` action: command palette entry plus global keyboard shortcut, gated.
- M6c. Implement the two G1 correctness expectations on the runtime side:
  - Form-control / pseudo-element exemption refinement on the color-boost path. Composition is in `compose_css` (Rust); the bridge / injection layer owns the validation pass for the rendered pages.
  - SVG image-document propagation. If `WebContentsObserver` alone does not reach SVG image documents, route through `chromium_src/` overrides per the project pattern (no raw `chromium/src/**` edits without explicit approval).
- M6d. Audit the isolated-world id (`ISOLATED_WORLD_ID_CONTENT_END + 4`) for collisions with other Maho isolated worlds. Allocate a fresh id if needed.

**Likely file targets.** `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`, `maho-chromium/browser/ui/views/location_bar/...` (URL-bar action), `maho-chromium/browser/ui/context_menu/...` (tab context menu), `maho-chromium/browser/ui/site_control/...` (page-info route), `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.cc/h` (sidebar route), `maho-chromium/browser/net/maho_boost_injection_handler.cc/h`, `maho-chromium/third_party/maho/maho_bridge.cc/h`, possibly `chromium_src/` overrides for the SVG path.

**Verification.** With the gate flipped on, every entry point opens the editor through the controller. With it off, none are user-discoverable. The form-control and SVG correctness fixes are observed on test pages.

### Phase 7. Verification and exit (M7)

Maps to shell-fill plan Stream F plus Gate G5.

- M7a. Per-knob round-trip test landed in `maho/appium-tests/` plus harness coverage for each G1 knob.
- M7b. Cross-tab live update test on the same domain.
- M7c. Rendered-output baseline against Zen on five canonical validation cases: color knob change, smart_invert on light pages, smart_invert on dark pages, zap_selectors hiding visible elements, custom_css overriding a known selector. Plus a typography knob composition case.
- M7d. Appium coverage for each restored entry point and the M6 enable gate flip.
- M7e. Single atomic commit flips `kMahoBoostEnabled` to default-on. Boost UX is live for the user.

**Likely file targets.** `maho/appium-tests/`, harness skeletons under whatever path the verification suite settles on, the feature-flag definition site.

**Verification.** All canonical commands green: `cd maho && bun run typecheck`, `cd maho && bun run test`, `python3 maho-chromium/build/scripts/build_maho.py`, `autoninja -C out/Default chrome` from `chromium/src`. Harness pass. Appium pass. Manual parity QA against Zen on the validation cases.

### Phase 8. Cleanup (M8)

- M8a. Remove cold artifacts: `kMahoBoostHost`, `kMahoBoostURL`, the navigable `chrome://maho-boost` registration, the modal dialog file (`maho_boost_dialog.cc/h`) once Phase 3 is live, and any other reference to the trusted manager-page surface that no surviving code path needs.
- M8b. Tree-wide grep for `maho-boost` and `MahoBoost` returns only legitimate references: the editor surface, the subordinate untrusted host, debug-only hosts gated on `is_debug`, and the historical plan documents in `.sisyphus/plans/`.

**Verification.** Build green after cleanup. No live caller into the removed symbols.

---

## 7. Verification and Definition of Done

The push is **done** when **every** item below holds at the same time. Any single failure blocks the M7e gate flip.

1. Section 4's corrected conclusions hold on disk: multiple boosts per domain, one active, separate floating top-level editor window, site data panel list with create / edit / toggle / delete flows.
2. The editor is a real non-modal native top-level `views::Widget` with native chrome, freely positionable, not auto-dismissing on focus loss, not anchored.
3. The editor hosts on a `chrome-untrusted://` subordinate. The trusted `chrome://maho-boost` host is unreachable as a user-facing surface.
4. The Mojom contract carries typed `Boost` and `BoostUpdate` structs, the parity method list, and the push interface for `OnBoostsChanged` / `OnActiveChanged`. The TS bindings reflect the IDL. No JSON-string `updateBoost` path remains.
5. The editor UI exposes every G1 knob with a real control. Round-trip is lossless.
6. The site data panel surface lists per-domain boosts with active indicator, opens the editor on row click, supports per-row active-toggle, supports per-row delete, and exposes a "New boost" affordance that routes through the controller.
7. Every chrome entry point (URL bar, tab context menu, page-info, sidebar, command palette, `New Boost` global action) routes through `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. None anchors a bubble. None navigates to a URL as a substitute.
8. The Rust runtime composes CSS for every G1 knob. The bridge re-injects on `BoostUpdated` and `BoostActiveChanged` events without a navigation. Cross-tab live preview holds on the same domain.
9. The two G1 correctness fixes are in place: form-control / pseudo-element exemption on the color-boost path, and SVG image-document propagation through the `nsPresContext` override path or its Chromium-overlay equivalent.
10. Phase 1.5 verdict on the rendering-hook strategy is committed, with any reset-plan amendments atomic with the verdict. (This stays open per the G1 lock; it must be resolved before M7e.)
11. Verification commands and suites are all green:
    - `cd maho && bun run typecheck`
    - `cd maho && bun run test`
    - `python3 maho-chromium/build/scripts/build_maho.py`
    - `autoninja -C out/Default chrome` from `chromium/src`
    - The targeted parity harness, including the rendered-output baseline against Zen on the five validation cases.
    - Appium regression suite covering D1 through D4 plus the M7e gate flip.
    - Manual parity QA against Zen at the pinned commit.
12. No legacy Boost API method (`get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost` outside of storage rehydration) remains in `boost_manager.rs`.
13. Cold artifacts are gone or repurposed strictly as the subordinate untrusted host inside the editor window.
14. No coexistence flag, GN arg, or branch re-enables a manager-page Boost UX, a bubble Boost UX, or a navigable `chrome://maho-boost` Boost UX.

---

## 8. Open Questions That Remain Genuinely Open

These are the questions code-level reading at `5bbbadd` does **not** answer. They stay open. Anything else (UX shape, scope, multi-vs-single, editor host type) is closed and must not be re-opened without a code-level counter-citation against the pinned commit.

1. **Phase 1.5 rendering-hook verdict.** Whether the Maho runtime stays CSS-only via the existing injection handler, moves to a deeper hook closer to `nsZenBoostsBackend`, or some hybrid. The G1 lock holds this open; this plan does not pre-decide it. The verdict ships with same-commit amendments to the reset plan if it requires them.
2. **Site data panel host: sidebar pane vs URL-bar-adjacent panel.** Zen's site data panel is URL-bar-adjacent. Maho's closest existing surface is the sidebar Boost pane. This plan defaults to the sidebar pane filled out, but the URL-bar-adjacent option is not foreclosed. The decision lands at M5a.
3. **Cold artifact disposition.** Whether `kMahoBoostHost`, `kMahoBoostURL`, and the navigable `chrome://maho-boost` host are deleted, or kept as a debug-only surface gated on `is_debug`. The G1/G2 lock says deletion is the default once Phase 3 lands the new hosting path. M8 settles this.
4. **Mojom field-level wire naming for the runtime payload.** If Phase 1.5 selects a deeper hook, the runtime payload type that crosses the boundary may be richer than the composed CSS string. The Mojom-level type names and decomposition stay open until Phase 1.5 closes. The G1 frozen field set is locked; the wire-name decomposition for the runtime payload is not.
5. **Per-`Browser` vs per-`Profile` controller scope.** Today `MahoBoostWindowController::GetForBrowser` is per-`Browser`. Whether multi-window scenarios (two browser windows on the same profile, both opening the editor) require a per-`Profile` collapse, a focus-grab on existing widget, or a multi-window editor model is not settled. The default for this plan is per-`Browser`; this question stays open and lands at the latest in M3.
6. **`changeWasMade` semantics on the wire.** Zen tracks `changeWasMade` as a per-record dirty marker. Whether Maho's typed `BoostUpdate` carries it explicitly, computes it implicitly from field equality, or routes it as an editor-local-only signal is not yet settled. M4a's typed-update landing decides.
7. **Cross-tab live update on different windows / different profiles.** The reset plan's Open Question 5 default is "same-domain across tabs". Whether the same-domain live update propagates across browser windows on the same profile, or across profiles, is not settled. M7b's cross-tab test scope picks a default, but the broader behavior policy is open.

These are the only legitimate open questions. Everything else in the Zen Boost UX shape is settled by the code at the pinned commit, and reopening it requires a counter-citation against that commit.

---

## 9. Notes for Future Readers

This plan exists because earlier rounds of work treated the Zen Boost UX shape as inferential. It is not. The code at `5bbbadd77be956deaf91633cd4f0611403b0ba53` is small enough to read and large enough to settle the shape questions. After this plan is accepted, any future Boost-related plan, PR, or review that re-frames the shape questions as open is wrong and should be redirected here plus the G1/G2 lock.

When the verification gates in Section 7 all hold, this plan is archived alongside the reset, remediation, parallel, shell-fill, and G1/G2 lock documents as the source-validated reference for the period that closes Zen Boost UI/runtime parity. Subsequent Boost work scopes against a fresh plan.
