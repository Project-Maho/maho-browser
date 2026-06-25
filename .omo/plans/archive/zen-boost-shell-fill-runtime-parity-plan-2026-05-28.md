# Zen Boost Shell-Fill Runtime Parity Plan

**Created:** 2026-05-28
**Status:** Active execution plan, written to reflect the on-disk Boost tranche as it actually exists today, not the slab any earlier plan claimed.
**Role:** A truthful path forward from the current shell-heavy state to real Zen parity. Defines workstreams, file ownership, dependency order, and acceptance criteria.

**Subordinate to:**

- `.sisyphus/plans/zen-boost-source-level-parity-reset-plan-2026-05-26.md` (the canonical contract)
- `.sisyphus/plans/zen-boost-remediation-plan-2026-05-27.md` (how the slab was supposed to land)
- `.sisyphus/plans/zen-boost-post-reset-parallel-parity-plan-2026-05-27.md` (parallel scheduling layer)
- `.sisyphus/plans/assets/zen-boost-g1-reference-lock-2026-05-27.md` (frozen G1/G2 facts)

This plan does not relax, contradict, or re-open any of those documents. Where this plan and the G1/G2 lock disagree, the lock wins.

---

## 1. Why this plan exists now

The earlier plan stack established:

1. The reset plan locked the source-level contract: domain-keyed boosts, typed knobs, one active per domain, separate top-level native window, no per-site JS, no manager-page UX.
2. The remediation plan defined exactly what the dirty tree had to look like before Phase 1 could start.
3. The post-reset parallel plan defined how the next push would fan out into Streams A through F gated by G1, G2, G3.
4. The G1/G2 reference lock froze the Zen reference commit (`5bbbadd`), the data shape, the launch API (`MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`), and the architectural ownership decision (top-level native window, not bubble, not navigable host).

What has actually landed since is a partial, real, but limited tranche:

- A live Rust runtime CSS bridge: `maho_bridge.cc` now has a real `GetBoostInjectionCss(MahoCore*, const char* url)` that resolves the active boost per domain and asks Rust to compose CSS through `maho_core_boost_get_active` + `maho_core_boost_compose_css`.
- A live injection handler: `maho_boost_injection_handler.cc` injects the composed CSS into HTTP/HTTPS primary-frame navigations through an isolated world.
- A frozen launch API in shell form: `open_boost_editor` in `maho_command_action_handler.cc` calls `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. The launch path is correct in shape and entry-point routing.
- An overlay wiring path: the injection handler is attached through `apply_chromium_src_overrides.py`, which is the project's standard way of patching upstream into the Maho overlay.

What has **not** landed, and is the entire reason this plan exists:

- `MahoBoostWindowController::ShowForActiveDomain` only flips an `is_showing_` bool. It does not create a `views::Widget`, does not host any content, does not surface a window the user can see.
- `MahoBoostWindowView` is a metadata-only stub. There is no UI in it.
- `maho_sidebar_boosts_data.cc::ParseBoosts()` returns `{}`. Every call to `ReloadBoosts()` falls through to the empty-state view. The sidebar pane is decorative.
- `maho_boost.mojom` only defines `PageHandler::GetState() => bool` and `Page::OnStateChanged(bool)`. There is no domain CRUD, no typed knob, no active-selection method.
- `MahoBoostPageHandler::GetState` returns `false`. There is no boost data ever surfaced through the WebUI boundary.
- `browser/resources/maho_boost/app.ts` is `console.log("Maho Boost placeholder loaded");`. There is no editor UI.
- The Rust contract is partially right at the data layer: `boost_manager.rs` carries `list_for_domain`, `get`, `create`, `update`, `delete`, `set_active`, `get_active`, and a real `compose_css` over typed knobs. But there are still legacy compatibility methods (`get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`) hanging on. Those are not parity, they are debt.
- There is no `Boost`-related event on `core_update.rs`. Cross-tab live updates and editor-to-runtime push notifications have no transport on the Rust side.
- The G3 contract freeze (the post-reset parallel plan's third gate) has not landed. The Mojom IDL is still the cold stub the remediation plan installed. None of the typed-knob fields cross the boundary yet.

The earlier plans are not wrong, but they are not granular enough at this exact moment. Most of them assume the next move is either "land the slab" (remediation) or "fan out into six streams" (parallel parity). Neither matches the reality on disk: the slab is partly past, the parallel push has not started, and a real but narrow runtime tranche has overshot the slab's expectations on the bridge side while everything UI-shaped on top of it remains a shell.

This plan exists to give a single concrete sequence from where the tree actually is today to Zen parity, naming files, naming methods, naming gates, and being explicit about which pieces are still shells.

---

## 2. Current truthful implementation status

This section is descriptive. Every claim below maps to a file and a line range that exists at the time of writing, 2026-05-28. No claim here is aspirational.

### 2.1 What is real

**Rust core (maho-core, maho-types, maho-storage, maho-ffi).**

- `maho/crates/maho-core/src/boost_manager.rs` exposes the parity-shaped API: `list_for_domain`, `get`, `create`, `update`, `delete`, `set_active`, `get_active`, `compose_css`. `compose_css` is a real composer over color (hue rotate, brightness, saturation, contrast), typography (font family, size override, text case), zap selectors with a basic safety filter, and `custom_css`. It composes for one boost at a time.
- The eight FFI symbols listed in the parallel plan exist on disk and round-trip JSON between Rust and the C++ bridge. `maho_core_boost_get_active` returns a JSON dict with at minimum an `id` field. `maho_core_boost_compose_css` returns the composed CSS string.
- The `Boost` and `BoostUpdate` types are domain-keyed and typed-knob shaped on the Rust side. `enabled`-per-rule and `custom_js` are gone from the live API.

**Bridge (`maho-chromium/third_party/maho/maho_bridge.{h,cc}`).**

- `GetBoostInjectionCss(MahoCore*, const char* url)` is implemented, valid, and short. It extracts the host from the URL, asks Rust for the active boost JSON for that domain, parses the JSON to recover the boost id, and asks Rust to compose CSS for that id. Empty inputs and missing actives return the empty string. Memory ownership is correct (`maho_string_free`).

**Injection (`maho-chromium/browser/net/maho_boost_injection_handler.{h,cc}`).**

- A `WebContentsObserver` attached as `WebContentsUserData` per tab, wired through `apply_chromium_src_overrides.py`.
- On primary-main-frame committed navigations (HTTP/HTTPS only, including same-document SPA/hash navigations), it pulls the composed CSS from the bridge and injects it as a `<style id="maho-boost-style">` element using `ExecuteJavaScriptInIsolatedWorld` in a dedicated isolated world id (`ISOLATED_WORLD_ID_CONTENT_END + 4`).
- Empty CSS removes any existing `maho-boost-style` element. Non-empty CSS replaces it.

**Launch API (the G2-frozen shape).**

- `MahoBoostWindowController::GetForBrowser(Browser*)` exists, returns a per-`Browser` instance, lazily constructs it, stores ownership in a process-static map.
- `ShowForActiveDomain(content::WebContents* active_tab)`, `Hide()`, and `IsShowing()` are declared and implemented in the controller. These are exactly the three methods the G2 lock froze.
- `open_boost_editor` in `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` calls `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. The shape of the call is right.

### 2.2 What is shell-only

**`MahoBoostWindowController`.** `ShowForActiveDomain` flips `is_showing_` to `true`. `Hide` flips it to `false`. `IsShowing` returns the bool. There is no `views::Widget` construction, no top-level window, no visible UI. Calling `ShowForActiveDomain` from the command action handler is a state mutation in a per-Browser map and nothing else. The user does not see anything.

**`MahoBoostWindowView`.** Default-constructed `views::View` subclass with `BEGIN_METADATA` / `END_METADATA`. No layout, no children, no controls, no preview, no list, no buttons.

**Sidebar Boost data and view (`maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.{h,cc}`, `maho_sidebar_boosts_view.{h,cc}`).** `ParseBoosts()` returns `std::vector<BoostItem>{}`. The view's `ReloadBoosts()` always falls into the empty-state branch and renders nothing editable. There is no per-domain listing, no active-selection switcher, no "New boost" affordance, no per-row delete.

**Mojom IDL (`maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom`).** Three declarations: `PageHandlerFactory::CreatePageHandler`, `PageHandler::GetState() => (bool active)`, `Page::OnStateChanged(bool active)`. No `Boost` struct, no typed knobs, no domain methods, no list, no CRUD, no active-selection methods, no change-notification payload.

**WebUI page handler (`maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.{h,cc}`).** `GetState` returns `false`. The handler holds a `Profile*` and the receiver/page pair, but never calls into Rust, never touches `MahoCoreHolder`, and never pushes anything through `OnStateChanged`.

**WebUI app (`maho-chromium/browser/resources/maho_boost/app.ts`).** A single `console.log` line. No Mojo client, no DOM, no editor, no preview wiring.

**Cross-process events (`maho/crates/maho-types/src/events/core_update.rs`).** No Boost-related variant. There is no `BoostCreated`, `BoostUpdated`, `BoostDeleted`, or `BoostActiveChanged`. The change-notification channel that the parallel plan's Stream A milestone A4 named does not exist as a typed event yet.

**Rust legacy methods.** `boost_manager.rs` still exposes `get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost`, `restore_active_boost`. These do not flow into anything live anymore, but they keep the contract surface dirty and they were not listed in the reset plan's parity API. They are debt.

### 2.3 Net effect

A real CSS effect can land on a real page, but only if a boost was created in some prior session, persisted into the database, and a domain-active row points at it. Under default state (fresh profile, no boosts), the runtime reaches into Rust, gets `null` for the active, returns the empty string from the bridge, and the injection handler removes (or never installs) a style tag.

The end-user-visible truth:

- A user has no way to create a boost from the UI today.
- A user has no way to see a list of boosts today.
- A user has no way to flip the active boost today.
- A user has no way to edit a knob today.
- A user opening the command palette and triggering `open_boost_editor` causes a per-`Browser` controller to flip a bool. Nothing renders.

The runtime backend works. Every UI surface above it is a shell. That asymmetry is what this plan is built around.

---

## 3. Non-negotiable parity targets

These are inherited from the reset plan Section 4, the G1/G2 lock, and the parallel plan's Section 3. Listed here so the streams below cannot drift.

1. Scope key is `domain`. URL patterns stay out.
2. Knobs are typed scalar fields. JSON blob columns for color and font stay out.
3. One active boost per domain, persisted on a `domain_boost_state` row at the storage layer.
4. Multiple boosts per domain, with an active selection chosen from the stored set.
5. No per-site JS injection. `custom_js` stays out at every layer.
6. The architectural owner of the editor is a top-level native `views::Widget` plus controller. Not a bubble. Not a sidebar editor. Not a navigable `chrome://maho-boost` user surface.
7. The launch API for every chrome entry point is `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. No anchor argument. No URL navigation as a substitute.
8. Hosted web content, where used, lives subordinate inside the native window's content frame. `chrome-untrusted://` is the preferred host scheme for that subordinate content per G2.
9. Knob coverage matches the G1 frozen list verbatim: `boostName`, `dotAngleDeg`, `dotPos`, `dotDistance`, `secondaryDotAngleDegDelta`, `secondaryDotPos`, `brightness`, `saturation`, `contrast`, `fontFamily`, `textCaseOverride`, `sizeOverride`, `enableColorBoost`, `smartInvert`, `autoTheme`, `zapSelectors`, `customCSS`, `changeWasMade`. Internal Maho names may differ; the editor surface and the persisted shape must round-trip these fields without loss.
10. Runtime semantics inherit the two correctness fixes from G1: form-control / pseudo-element exemption refinement on the color-boost path, and SVG image-document propagation through the `nsPresContext` override path.
11. The Phase 1.5 rendering-hook verdict is still open. CSS-only is the current implemented strategy on the bridge side. A deeper hook is allowed only after the spike returns a written verdict and amends the reset plan in the same commit.
12. No coexistence of legacy and new Boost UX behind any flag, GN arg, or branch.

Anything in the streams below that conflicts with one of these targets is rejected.

---

## 4. Workstreams and file ownership

Six workstreams. The scheme echoes the post-reset parallel plan's Streams A through F so the two plans can compose without renaming, but each stream's brief here is rewritten against the on-disk reality of 2026-05-28.

### 4.1 Stream A. Rust contract cleanup and event channel

**Goal.** Bring the Rust side from "parity-shaped with debt" to "parity-shaped, debt-free, with a typed event channel."

**Owns.**

- `maho/crates/maho-types/src/boost.rs` (and any Boost type modules under `maho-types`)
- `maho/crates/maho-types/src/events/core_update.rs` (the new `BoostCreated`, `BoostUpdated`, `BoostDeleted`, `BoostActiveChanged` variants)
- `maho/crates/maho-types/src/events/shell_event.rs` (the inbound editor-driven mutations, if any are routed through the shell event bus rather than direct FFI)
- `maho/crates/maho-core/src/boost_manager.rs` (drop legacy methods, keep parity API, emit events through `event_dispatcher`)
- `maho/crates/maho-core/src/maho_core.rs` (wire Boost manager into the orchestrator's update broadcast loop, register Boost commands if any are added)
- `maho/crates/maho-core/src/event_dispatcher.rs` (route the new Boost variants to the shell, gated by the existing dispatcher contract)
- `maho/crates/maho-storage/src/sqlite.rs` (the `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)` table, if it is not already at parity shape, plus the typed-knob columns)
- `maho/crates/maho-ffi/src/lib.rs` (Boost section: confirm the eight symbols match the parity API exactly, drop any leftover `_in_space` or `_for_url` exposure, expose the change-notification subscription if it crosses the FFI)

**Does not own.**

- Anything in `maho-chromium/`
- The Mojom IDL (Stream B contributes to the contract freeze, Stream C consumes from it)

**Concrete deliverables.**

1. Delete `get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost`, `restore_active_boost` from `boost_manager.rs` unless one of them has a live caller. For each one, run an LSP references check; if a caller exists, decide explicitly whether it stays or migrates to the parity API.
2. Add a `Boost` event family to `core_update.rs`. At minimum: `BoostCreated(Boost)`, `BoostUpdated(Boost)`, `BoostDeleted(BoostId, domain)`, `BoostActiveChanged(domain, Option<BoostId>)`. Field types come from `maho-types::Boost` so the wire shape is the parity shape.
3. Make every mutation in `boost_manager.rs` (`create`, `update`, `delete`, `set_active`) emit the corresponding event through whatever channel `maho_core.rs` already uses to push `CoreUpdate` to listeners.
4. Confirm the storage schema matches the reset plan's typed-knob layout. If `enabled`, `custom_js`, or any URL-pattern column is still in `sqlite.rs`, drop them in this stream's commit.
5. Re-run `cd maho && bun run typecheck` and `cd maho && bun run test`. Add tests where missing: per-knob round-trip, active-selection invariants per domain, golden `compose_css` outputs covering color, typography, zap, and `custom_css`, event emission on each mutation.

**Verification.**

- `cd maho && bun run typecheck` is green.
- `cd maho && bun run test` is green.
- A grep for the legacy method names returns no live callers.
- The `event_dispatcher` correctness tests cover the new Boost variants.

### 4.2 Stream B. Bridge and runtime delivery

**Goal.** Keep the existing live CSS bridge stable, expand its scope where Stream A's event channel makes that possible, and own the Phase 1.5 rendering-hook spike from end to end.

**Owns.**

- `maho-chromium/third_party/maho/maho_bridge.{h,cc}` (the `GetBoostInjectionCss` entry plus any new bridge entries the spike or the event channel requires)
- `maho-chromium/browser/net/maho_boost_injection_handler.{h,cc}` (the handler under the CSS-only strategy; or its replacement if the spike selects a deeper hook)
- `maho-chromium/browser/maho_core_holder.{cc,h}` lines that surface Boost-related accessors
- The C++ subscriber to Stream A's change-notification channel
- The Phase 1.5 rendering-hook spike, the written verdict, and any same-commit amendments to the reset plan that the spike requires

**Does not own.**

- The Rust composition itself (Stream A)
- The window UI (Stream C)
- The launch sites (Stream D)

**Concrete deliverables.**

1. Run the Phase 1.5 spike against the rendering-hook candidate list G1 froze. Produce per-candidate written verdicts (feasible / infeasible) with cost and fork-burden estimates. Land the verdict and any reset-plan amendments atomically.
2. Under the current CSS-only path, add re-injection on the Stream A event channel: when a `BoostUpdated` or `BoostActiveChanged` arrives for a domain that any live `WebContents` is on, recompose CSS and reinject. This makes live preview from the editor possible without a full navigation.
3. Implement the two G1 correctness expectations: form-control / pseudo-element exemption on the color-boost branch (composed via Stream A's `compose_css`, but the bridge owns the validation pass), and SVG image-document propagation. The SVG path requires a check on the document type at the injection site; if SVG image documents are not reachable through `WebContentsObserver` alone, document the gap and route the work through `chromium_src/` overrides where appropriate (no raw `chromium/src/**` edits without explicit approval).
4. Audit the isolated-world id (`ISOLATED_WORLD_ID_CONTENT_END + 4`). Confirm it does not collide with any other Maho isolated world. If it does, allocate a new one and document the choice next to the constant.

**Verification.**

- `python3 maho-chromium/build/scripts/build_maho.py` is green.
- A narrow `autoninja -C out/Default chrome` from `chromium/src` is green.
- An end-to-end pass on the five reset-plan validation cases (color knob change, smart_invert on light and dark pages, zap_selectors hiding visible elements, custom_css overriding a known selector, typography knob composition) passes manually.

### 4.3 Stream C. Editor window and WebUI host

**Goal.** Replace the `MahoBoostWindowView` and `MahoBoostWindowController` shells with a working top-level editor window that hosts the editor UI and round-trips knobs through the Mojom contract.

**Owns.**

- `maho-chromium/browser/ui/views/boost/maho_boost_window_view.{h,cc}`
- `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.{h,cc}`
- `maho-chromium/browser/ui/views/boost/BUILD.gn`
- `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` (in coordination with Stream A on the data shape and Stream B on the runtime payload)
- `maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.{h,cc}`
- `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.{h,cc}`
- `maho-chromium/browser/ui/webui/maho_boost/BUILD.gn`
- `maho-chromium/browser/resources/maho_boost/app.ts`
- `maho-chromium/browser/resources/maho_boost/boost.html`
- `maho-chromium/browser/resources/maho_boost/boost.css`
- `maho-chromium/browser/resources/maho_boost/BUILD.gn`

**Does not own.**

- Any navigable `chrome://maho-boost` user-facing surface (the surface stays a subordinate content host inside the native window, ideally on `chrome-untrusted://` per G2)
- The launch sites (Stream D)
- The runtime composition (Streams A and B)

**Concrete deliverables.**

1. Replace the cold Mojom stub with the parity IDL. At minimum the IDL must carry: a `Boost` struct mapping the G1 field list, a `BoostUpdate` struct, methods for `ListForDomain`, `GetActive`, `Create`, `Update`, `Delete`, `SetActive`, plus a `Page` push interface for `OnBoostChanged(Boost)` and `OnActiveChanged(string domain, mojo_base.mojom.String? active_id)`. Names follow Chromium Mojo conventions; the field set follows G1.
2. Fill `MahoBoostWindowController::ShowForActiveDomain`. It must construct a top-level `views::Widget` (not anchored, native chrome, no auto-dismiss on focus loss), parent the `MahoBoostWindowView` into the widget's content frame, and show it. `Hide` destroys or hides the widget. `IsShowing` reports the widget state.
3. Decide and document the in-window content technology in coordination with the Phase 1.5 verdict. Default is a `WebView` hosting `chrome-untrusted://maho-boost-editor` (or whichever untrusted host the team chooses) inside the native window's content frame. The trusted `chrome://maho-boost` host stays unreachable.
4. Replace `app.ts` with a real editor: domain header, list of boosts for the active domain, active-selection chip, per-knob controls covering the G1 list, "New boost" button, per-row delete. Wire it to the new Mojom client. Live preview is achieved by sending `Update` calls on debounce and observing `OnBoostChanged` from the page side; the runtime side is covered by Stream B's event-channel reinjection.
5. Fill `MahoBoostPageHandler` to call into `MahoCoreHolder` and route requests to the parity FFI methods. `GetState` is removed or repurposed; the new IDL does not define it.

**Verification.**

- `python3 maho-chromium/build/scripts/build_maho.py` is green.
- Opening the editor window from a debug/internal command produces a real top-level window. Editing a color knob triggers a CSS reinjection on the active tab. Closing the window, reopening, and reading the knob value returns the persisted value.
- The window does not auto-dismiss on focus loss, has native title and close chrome, and is freely positionable.

### 4.4 Stream D. Launch sites and entry-point restoration

**Goal.** Restore every previously neutralized chrome entry point, behind a default-off launch flag until the final enable gate, and confirm each one targets the G2-frozen launch API exactly.

**Owns.**

- The command-palette `open_boost_editor` already wired in `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`. Stream D confirms the shape stays correct after Stream C's controller is filled.
- The location-bar / URL-bar Boost quick action (subdivided as D1)
- The tab context-menu Boost commands (subdivided as D2)
- The page-info Boost route (subdivided as D3)
- The sidebar Boost route into the editor (subdivided as D4); the sidebar Boost pane content itself stays empty-state until M5 (see Section 6)
- A new `kMahoBoostEnabled` build-time flag or runtime feature, default-off, that gates entry-point visibility

**Does not own.**

- The window itself (Stream C)
- The Rust runtime (Stream A)
- The Mojom contract (Stream C)

**Concrete deliverables.**

1. For each of D1 through D4, wire the entry point to `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. No anchor view, no URL navigation, no bubble.
2. Each entry point lands behind the default-off feature gate. The user does not see the entry until M5.
3. A new `New Boost` global action (analogous to Zen's `ZenUBGlobalActions.sys.mjs` action) is registered as a command palette entry and as a global keyboard shortcut, both gated.

**Verification.**

- `python3 maho-chromium/build/scripts/build_maho.py` is green.
- With the feature gate flipped on, each of D1 through D4 opens the editor window through the controller. With it off, none of them are user-discoverable.

### 4.5 Stream E. Sidebar Boost pane fill

**Goal.** Replace the empty-state sidebar with a real per-domain Boost pane that lists boosts, supports per-row activation and delete, and surfaces a "New boost" affordance that opens the editor window for the active domain.

**Owns.**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.{h,cc}` (`ParseBoosts` returns the actual domain-scoped list, fed by the new Mojom client or by direct `MahoCoreHolder` calls)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.{h,cc}` (`ReloadBoosts` builds the populated view, the active-selection indicator, the per-row delete affordance, the `New boost` button)

**Does not own.**

- The editor window (Stream C)
- The launch routing into the editor (Stream D)

**Concrete deliverables.**

1. Subscribe the sidebar to the Stream A event channel through whichever side of the boundary the sidebar is on. The pane refreshes on `BoostCreated`, `BoostUpdated`, `BoostDeleted`, `BoostActiveChanged` for the current active domain.
2. The `New boost` button calls `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)` and lets the editor window create the new boost via Mojom. The sidebar does not own creation.
3. Per-row activation calls `SetActive` for the row's `BoostId` on the active domain. Per-row delete calls `Delete`.

**Verification.**

- The pane lists every boost for the active domain, indicates which one is active, and updates within one event tick when the editor window mutates state.

### 4.6 Stream F. Verification harness, parity QA, and acceptance

**Goal.** Build the harness that proves the rest of the streams converged on Zen parity. Reuse the Stream F brief from the post-reset parallel plan; this section narrows it to the harness work that has not yet been done.

**Owns.**

- The targeted parity harness skeleton, expanded incrementally as Streams A through E land
- The rendered-output baseline against Zen on the test pages the reset plan Section 8 Phase 2 names
- The Appium regression suite under `maho/appium-tests/` that exercises each restored entry point and the M5 enable gate flip
- The acceptance checklist for the M5 single atomic commit

**Does not own.**

- Any product code

**Concrete deliverables.**

1. Per-knob round-trip test: open the editor (through an internal-only command), edit one knob from the G1 list, observe the page change, persist, reload, observe again, tear down. One test per knob.
2. Cross-tab live update test on the same domain (per reset plan Open Question 5's default).
3. Rendered-output baseline against Zen for the five validation cases (color knob change, smart_invert on light and dark pages, zap_selectors hiding visible elements, custom_css overriding a known selector, typography knob composition).
4. Appium coverage for D1 through D4 plus the M5 enable gate flip.

**Verification.**

- All canonical commands stay green: `cd maho && bun run typecheck`, `cd maho && bun run test`, `python3 maho-chromium/build/scripts/build_maho.py`, narrow `autoninja -C out/Default chrome`.
- The harness, the rendered-output baseline, and the Appium suite are all green at the moment M5 lands.

---

## 5. Serial gates and dependency order

The post-reset parallel plan locked three gates: G1 (Zen reference and contract), G2 (surface ownership), G3 (Rust-to-Chromium contract freeze). G1 and G2 have already landed. G3 has not.

This plan adds two gates that the on-disk reality calls for, and reuses G3 with the same meaning.

### 5.1 Gate G3. Mojom contract freeze (parity IDL)

**What it locks.** The exact `Boost` and `BoostUpdate` Mojom shapes, the exact method list on `PageHandler`, and the exact push interface on `Page`. The runtime payload type that crosses the boundary if Phase 1.5 selects something richer than CSS is a separate, downstream contract; G3 covers the structural data IDL only.

**Why it is serial.** Streams C, D, E, and F all bind to the IDL. If it shifts mid-push, every binding has to be redone.

**Output, single atomic commit.** A reshaped `maho_boost.mojom` matching the G1 field list verbatim, regenerated TS bindings, an updated `MahoBoostPageHandler` that compiles against the new IDL even if its bodies are still stubs, a contract README placed next to the IDL.

**Pass criteria.** `cd maho && bun run typecheck`, `cd maho && bun run test`, `python3 maho-chromium/build/scripts/build_maho.py`, narrow `autoninja -C out/Default chrome` all green. No live surface is reachable through the IDL yet.

**Owners.** Stream C drives, with Stream A reviewing the data shape and Stream B reviewing any payload-adjacent fields.

### 5.2 Gate G4. Phase 1.5 rendering-hook verdict

**What it locks.** Whether the runtime path stays CSS-only via the existing injection handler, or moves to a deeper hook, or some hybrid. The verdict is a written document committed alongside the same-commit amendments to the reset plan that the verdict requires (per the reset plan's Phase 1.5 hard rule).

**Why it is serial.** Stream B's runtime work past the current state and Stream C's choice of in-window content host both depend on the verdict.

**Output.** The verdict document, the reset-plan amendments (if any), and a concrete decision on the runtime payload type. If CSS-only stays, the existing path is the implementation. If a deeper hook is chosen, Stream B's mid-push deliverables expand to include implementing it.

**Pass criteria.** A reviewable per-candidate verdict with cost estimates and fork-burden estimates exists on disk. Reset plan Sections 4 item 8, 8 Phase 2, and 9 are amended in the same commit if the verdict requires.

**Owners.** Stream B drives. Streams A, C, F observe.

### 5.3 Gate G5. Final enable gate

**What it locks.** That every prerequisite for the user to see Boost UX has landed and verified. This is the M5 of the post-reset parallel plan, restated here so the exit point is explicit.

**Pass criteria.** Every item in Section 7 below is true.

**Output.** A single atomic commit that flips the Stream D feature gate from default-off to default-on. The commit also collapses any remaining feature-flag scaffolding into the production path.

### 5.4 Order

```
[Stream A: typed events]   [Stream B: spike + reinjection]   [Stream F: harness skeleton]
                |                          |                              |
                +--------------------------+------------------------------+
                                           |
                                          G3
                                           |
                            [Stream C: window + IDL fill]
                                           |
                                          G4
                                           |
                       [Stream B: runtime path per verdict]
                       [Stream C: live preview against payload]
                       [Stream D: entry points behind gate]
                       [Stream E: sidebar pane fill]
                                           |
                              [Stream F: full harness pass]
                                           |
                                          G5 (M5)
```

Streams A, B's spike, and F's harness skeleton can run in parallel as soon as this plan is accepted. G3 is the next gate. G4 follows independently of G3 in calendar terms but precedes any Stream C work that binds to the runtime payload. G5 is the exit.

---

## 6. Implementation phases and milestones

Phases below sequence the deliverables. Each milestone is a single point at which a tranche of work is integrated into trunk in a way that affects the rest of the push.

### Phase 0. Plan acceptance

- M0a. This plan is reviewed and accepted as the active scheduling source of truth.
- M0b. Streams A, B, F start. Stream A picks up legacy-method pruning and event-channel work. Stream B picks up the Phase 1.5 spike. Stream F picks up the harness skeleton.

### Phase 1. Rust event channel and bridge stabilization

- M1a. Stream A lands the legacy-method pruning. `boost_manager.rs` exposes only the parity API.
- M1b. Stream A lands the typed Boost event variants on `core_update.rs` and wires `boost_manager.rs` mutations through `event_dispatcher.rs`. Tests cover emission on every mutation.
- M1c. Stream B subscribes the bridge / injection layer to the event channel. CSS reinjection on `BoostUpdated` and `BoostActiveChanged` is live behind a debug flag (no user-visible launch sites yet).
- M1d. Stream B's Phase 1.5 spike returns its verdict. Reset-plan amendments, if any, land in the same commit. G4 closes.

### Phase 2. Mojom contract and editor window

- M2a. G3 lands. The new IDL is in trunk. `MahoBoostPageHandler` compiles against it. Generated TS bindings are in. Nothing user-facing yet.
- M2b. Stream C fills `MahoBoostWindowController::ShowForActiveDomain` with a real top-level `views::Widget`. The widget is reachable only through an internal-only debug command.
- M2c. Stream C fills `MahoBoostWindowView` with the editor layout. The subordinate web content host is decided (default `chrome-untrusted://maho-boost-editor`) and the resources `app.ts` is replaced with the real editor. The editor round-trips knobs through Mojom against the live Rust API.
- M2d. Stream C wires live preview: editor `Update` calls debounce, the page handler dispatches them into Rust, the Stream A event channel propagates `BoostUpdated`, Stream B's bridge subscriber reinjects CSS on every live tab on the affected domain. The user, if they could see the editor, would see live preview.

### Phase 3. Sidebar and entry points

- M3a. Stream E fills the sidebar pane. With the debug command opening the editor and the sidebar listening on the event channel, the pane stays in sync with mutations made from the editor.
- M3b. Stream D restores D1, D2, D3, D4 entry points behind the default-off feature gate. Each one calls the G2-frozen launch API. None are user-discoverable.
- M3c. Stream D adds the `New Boost` global action (command palette entry and keyboard shortcut), gated.

### Phase 4. Verification and exit

- M4a. Stream F's per-knob round-trip tests land. Cross-tab live update test lands. Rendered-output baseline against Zen on the five validation cases lands.
- M4b. Stream F's Appium regression suite covers D1 through D4 plus the M5 gate flip.
- M4c. Pre-M5 verification runs on a clean tree: the four canonical commands plus the harness plus the Appium suite plus a manual parity QA pass.
- M5. The single atomic commit flips the Stream D feature gate to on. Boost UX is live for the user.

### Phase 5. Cleanup and debt sweep

- M6a. Cold artifacts (`kMahoBoostHost`, `kMahoBoostURL`, the navigable `chrome://maho-boost` registration, any leftover references to a manager-page surface) are removed once Stream C confirms the in-window content host does not depend on them. This matches the post-reset parallel plan's M6.
- M6b. A final tree-wide grep for `maho-boost` and `MahoBoost` returns only legitimate references: the editor surface, the subordinate untrusted host, debug-only hosts gated on `is_debug`, and the historical plan documents in `.sisyphus/plans/`.

---

## 7. Verification and exit criteria

The push is **done** exactly when **all** of the following hold simultaneously.

1. Every item in Section 3 (non-negotiable parity targets) holds on disk.
2. `MahoBoostWindowController::ShowForActiveDomain` opens a real top-level `views::Widget` that hosts the editor. `Hide` and `IsShowing` operate on the widget. The window is freely positionable, has native title and close chrome, does not auto-dismiss on focus loss, is not anchored to the toolbar or address bar.
3. `MahoBoostWindowView` renders the editor layout: domain header, boost list with active-selection indicator, per-knob controls covering the G1 list, "New boost" button, per-row delete.
4. `maho_boost.mojom` carries `Boost`, `BoostUpdate`, the parity method list, and the push interface. `MahoBoostPageHandler` calls into Rust for each method. The TS bindings reflect the IDL.
5. `browser/resources/maho_boost/app.ts` is a real editor that consumes the Mojom client, dispatches `Update` on debounce, listens on `OnBoostChanged` and `OnActiveChanged`, and produces a working live preview.
6. `maho_sidebar_boosts_data.cc::ParseBoosts` returns the actual per-domain list. The sidebar reflects edits made from the editor within one event tick.
7. Every entry point in Stream D (D1, D2, D3, D4, `New Boost` global action, `open_boost_editor` command) routes through `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)`. None of them anchor a bubble. None of them navigate to a URL. None of them is user-discoverable until M5.
8. The Rust runtime composes CSS for every G1 knob and the bridge re-injects on `BoostUpdated` and `BoostActiveChanged` events without a navigation. The two G1 correctness fixes (form-control / pseudo-element exemption, SVG image-document propagation) are in place.
9. Phase 1.5 verdict is committed, with any reset-plan amendments atomic with the verdict.
10. Verification is green:
    - `cd maho && bun run typecheck`
    - `cd maho && bun run test`
    - `python3 maho-chromium/build/scripts/build_maho.py`
    - `autoninja -C out/Default chrome` over `chromium/src`
    - Stream F's full parity harness, including the rendered-output baseline against Zen on the five validation cases.
    - Stream F's Appium regression suite covering D1 through D4 plus the M5 enable gate flip.
    - Manual parity QA: open the editor from each registered entry point, edit at least one knob per category, observe the page change, persist across reload, switch active boost, delete a boost, close and reopen the window, confirm cross-tab live update on the same domain.
11. No legacy Boost API method (`get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost` outside of storage rehydration paths) remains in `boost_manager.rs`.
12. `kMahoBoostHost` and `kMahoBoostURL` are gone from the build, the navigable `chrome://maho-boost` host is unregistered, the cold WebUI controller files are either deleted or repurposed strictly as the subordinate untrusted host inside the native window's content frame.

If any one of (1) through (12) fails, M5 does not land.

---

## 8. Explicit non-goals: what this plan does NOT claim is done

Listed here so no future reviewer can mistake the on-disk runtime tranche for full parity.

- **The editor does not exist yet.** `MahoBoostWindowView` is empty. The user cannot see anything when `ShowForActiveDomain` is called. This plan describes how to build the editor; the build is not done.
- **The Mojom contract is not at parity.** The current IDL is `GetState() => bool`. None of the typed knobs, none of the CRUD methods, none of the active-selection methods are on the wire. G3 is open.
- **The WebUI app is a placeholder.** `app.ts` logs a string. There is no Mojo client, no DOM, no preview wiring.
- **The page handler is a stub.** `MahoBoostPageHandler::GetState` returns `false`. It does not call into Rust. The plumbing through `MahoCoreHolder` is unwired.
- **The sidebar is decorative.** `ParseBoosts` returns `{}`. Every call to `ReloadBoosts` falls into the empty-state branch. The pane has no list, no controls, no creation affordance.
- **The change-notification channel does not exist on the Rust side.** `core_update.rs` has no Boost variants. Cross-tab live updates and editor-to-runtime push notifications have no transport.
- **The legacy method debt is still on the Rust API surface.** `boost_manager.rs` exposes `get_boosts_for_url`, `to_view_model`, `get_all_view_models`, `is_globally_enabled`, `is_js_enabled`, `list_boosts`, `restore_boost`, `restore_active_boost`. Stream A removes them; this plan does not claim they are removed.
- **Phase 1.5 verdict is not in.** The bridge currently runs CSS-only. A deeper hook is allowed only after the spike and the same-commit reset-plan amendments. Stream B owns this; this plan does not pre-decide it.
- **The two G1 correctness fixes are not in.** Form-control / pseudo-element exemption and SVG image-document propagation are listed as Stream B deliverables, not as accomplished work.
- **Launch restoration is not done.** D1 (toolbar / location bar), D2 (tab context menu), D3 (page-info), D4 (sidebar) are still neutralized. Only the command-palette `open_boost_editor` is wired, and even that opens nothing visible. Restoration happens behind a default-off gate; user-visibility happens at M5.
- **No coexistence relaxation.** No flag, GN arg, or branch will re-enable a manager-page Boost UX, a bubble Boost UX, or a navigable `chrome://maho-boost` Boost UX. The G2 lock holds.
- **Parity is not a runtime CSS round-trip alone.** Even if every G1 knob composed correctly today through the existing CSS bridge, the user still has no editor, no list, no active selection control, and no creation affordance. Parity is the full path through the editor, the Mojom contract, the runtime, the launch sites, and the verification harness.

---

## 9. Working assumptions and stop-and-replan triggers

A trigger means: the affected stream halts, every other stream pauses at its next safe boundary, and this plan is re-opened (along with the upstream documents) before any stream resumes.

1. **Phase 1.5 verdict invalidates the runtime payload assumption.** If the spike returns a deeper-than-CSS hook, `compose_css` may be a subset of a richer payload type. Stream B halts after publishing the verdict. Stream C re-evaluates the in-window content host. G3's IDL may need to widen.
2. **The G2 ownership decision is challenged in code review.** A reviewer proposing "let's expose a `chrome://maho-boost` page", or "let's anchor the editor in a bubble", or "let's host the editor in the sidebar" is a tripwire. Stream C and Stream D pause. The proposal escalates to a plan amendment, not a code workaround.
3. **The G1 reference commit moves under the push.** If a newer Zen commit is cited as the parity contract by any reviewer, G1 is reopened. Reset-plan and lock-document re-grounding lands in a single atomic commit before any stream resumes.
4. **A Stream D entry point flips live before M5.** Immediate revert of the gate flip. Branch rebases. PR review for that subtrack is tightened for the rest of the push.
5. **A Mojom field is added that does not appear on the G1 frozen list.** Either G1 is amended atomically (with a reset-plan amendment if the addition is structural) or the field is rejected. No silent extensions.
6. **A raw `chromium/src/**` edit appears as a default workaround.** Rejected on sight. The default path through the overlay is `chromium_src/` overrides plus `maho-chromium/` net-new files. Raw upstream edits require explicit user approval, narrow scoping, and an AGENTS.md follow-up.
7. **Stream A's legacy-method pruning surfaces a live caller.** The pruning halts. The caller is identified, audited, and either deleted (if dead) or migrated to the parity API (if live). Stream A's PR is reposted only after the caller is reconciled.
8. **The bridge subscriber to the event channel races with navigation.** If reinjection arrives during a same-document navigation and the page is in a partial state, the handler must serialize through the existing observer pattern. If a race is observed, Stream B halts and the synchronization is reconciled before any further runtime PR lands.

---

## 10. Notes for future readers

This plan is not a re-plan of the reset plan. It is a here-and-now execution layer that names what is true on disk, what is not, and the fastest sequence from one to the other without contradicting any earlier decision. If a reviewer reads this and finds it shorter or differently scoped than the post-reset parallel plan, that is intentional. The parallel plan is the scheduling layer for a fan-out that has not started; this plan is the execution layer for the next two phases of work given that the runtime CSS bridge has already landed and most surfaces above it remain shells.

When this plan's exit criteria all hold, this plan is archived alongside the reset, remediation, parallel, and G1/G2 lock documents as the historical execution layer for the period between the runtime CSS tranche and full Zen Boost parity. Any further Boost work after that is scoped against a new plan.
