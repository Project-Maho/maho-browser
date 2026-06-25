# Zen Boost Source-Level Parity Reset Plan

**Created:** 2026-05-26
**Status:** Canonical. Supersedes earlier Maho Boost plans, including `zen-boost-rust-first-parity-implementation-plan-2026-05-25.md` and the manager/WebUI-centric direction it implied.
**Reference baseline:** Zen Browser (`zen-browser/desktop`) Boost feature, read at source level.
**Maho baseline:** `maho-types/src/boost.rs`, `maho-core/src/boost_manager.rs`, `maho-storage/src/sqlite.rs`, `maho-chromium/browser/net/maho_boost_injection_handler.cc`, `maho-chromium/browser/resources/maho_boost/`, `chrome://maho-boost` WebUI host.
**Decision bias:** Treat the existing `chrome://maho-boost` manager surface as a wrong product, not a refactor target. Reset the data model and UI shape to match Zen, then port behavior on top.

---

## 1. User Intent / Objective

The user has explicitly read Zen's source and wants Maho's "Boost" feature to be the same feature as Zen's Boost, not a Maho-flavored cousin of it.

Concretely, parity here means:

1. The user-visible primitive is a **per-domain Boost**, attached to the active site, edited from a **separate floating Boost editor window** invoked from chrome (toolbar / menu / context entry), not from a manager page and not from a toolbar-anchored bubble. The window is a freely positionable native chrome window, not a popup that auto-dismisses on focus loss.
2. The editing model is **typed knobs** (color, typography, zap, custom CSS) with live preview on the current page, not a JSON blob editor.
3. The runtime effect is **CSS-driven for the vast majority of cases as a fallback**, but the runtime strategy is decided by a mandatory feasibility spike (Section 4 item 8 / Phase 1.5) that determines whether closer-to-Zen rendering hooks are reachable inside Maho's Chromium overlay. No per-site JavaScript injection exists as part of the parity surface regardless of which runtime strategy is selected.
4. Multiple boosts per domain are supported, with **one active boost per domain** persisted across sessions.
5. The screenshot-style floating Boost UI provided as reference is the target shell, hosted as a separate top-level native chrome window — not as inspiration, and not as a toolbar bubble.

Anything in the current Maho implementation that conflicts with the above is in scope to remove, not to preserve.

---

## 2. What Zen Boost Actually Is (source-level)

Zen Boost, as it exists in `zen-browser/desktop`:

- **Scope key:** domain (eTLD+1-ish host string), not URL pattern, not regex.
- **UI shell:** a **separate floating Boost editor window** invoked from chrome and opened against the current tab's domain. It is a per-site editor in its own top-level window, freely positionable, not anchored to the address bar / toolbar, and not a global manager. Tab switching does not auto-retarget the open editor mid-edit; the user closes / reopens to switch domain context.
- **Storage shape:** per-domain list of boosts; each boost is a typed record with the following knobs (names taken from Zen):
  - Color engine:
    - `enableColorBoost` (bool)
    - `dotAngleDeg` (number, hue rotation primary)
    - `secondaryDotAngleDegDelta` (number, secondary hue delta)
    - `brightness` (number)
    - `saturation` (number)
    - `contrast` (number)
    - `autoTheme` (bool)
    - `smartInvert` (bool)
  - Typography:
    - `fontFamily` (string / nullable)
    - `textCaseOverride` (enum-ish: none / upper / lower / capitalize)
    - `sizeOverride` (number / nullable)
  - Structural:
    - `zapSelectors` (list of selectors hidden via generated CSS)
    - `customCSS` (free-form CSS appended after the typed knobs)
- **Active state:** for each domain, one boost is "active". Activation is persistent across sessions. The separate Boost editor window reflects/edits the active boost and lets the user create / switch / delete boosts for the current domain.
- **Runtime:** Zen renders the effect by composing CSS-equivalent rules from the typed knobs (color filters / variables / generated rules / zap selectors / customCSS) and applying it scoped to the matched domain's documents, hooked into Gecko's rendering pipeline. The color engine is implemented as filter math over those scalar knobs, integrated below CSS-only-injection level. There is **no per-site JS injection** as part of the parity feature; `customCSS` is the only escape hatch.
- **Templates:** Zen's parity surface does not require a "template marketplace" UI on the site editor. Cloning / preset behavior is handled inside the same per-domain list.

Implication: Boost in Zen is a **typed-knob, separate-window, per-domain** feature whose runtime hooks Zen's renderer below pure CSS injection. That is the contract Maho must approximate — exactly how closely the runtime hook can be approximated inside Chromium is the question Phase 1.5 answers (Section 4, item 8).

---

## 3. What Current Maho Boost Actually Is (source-level)

The current Maho implementation, read at source level, is a different product wearing the same name:

- **Scope key:** URL pattern (glob/regex-ish), not domain. `maho-types::Boost` and `BoostUpdate` carry pattern fields, not a domain key.
- **Storage shape:** boosts persist `custom_css`, `custom_js`, `color_theme_json` (opaque JSON blob), `font_override_json` (opaque JSON blob), `zap_selectors`, `enabled`. The color/font knobs are not first-class typed fields; they are JSON blobs the UI is expected to parse.
- **Runtime:** `maho_boost_injection_handler.cc` injects CSS **and JS**. JS injection is a first-class path. Templates are also a first-class concept routed through `boost_manager.rs`.
- **UI shell:** `chrome://maho-boost` is a full-page WebUI manager (`browser/resources/maho_boost/`), with a richer editor/store (`store.ts`), template flows, and global enable flags. It is not a per-site separate editor window and does not match Zen's interaction model.
- **Active state:** "enabled" is per-rule, not "one active boost per domain". There is no domain-level active selection concept.

In short: current Maho Boost is a **URL-pattern-scoped, JSON-blob-backed, JS-injection-capable, template-driven, manager-page** feature. It is not Zen Boost. Continuing to evolve it as a manager will not converge to parity, it will diverge further.

---

## 4. Hard Constraints For Parity

These are non-negotiable for this reset.

1. **Scope key is domain.** The persisted primary key of a boost rule is `(domain, boost_id)`. URL patterns are out of the parity surface entirely.
2. **Knobs are typed.** Color and typography fields are stored as concrete scalar columns / typed structs, never as opaque `*_json` blobs. The schema is the contract.
3. **One active boost per domain.** Activation is persistent and stored on the domain row, not derived.
4. **No per-site JS injection, runtime layer decided by Phase 1.5.** JS injection is **not** part of the parity feature surface, regardless of which runtime strategy Phase 1.5 selects. The runtime layer for color / typography / zap / custom_css composition is decided by the mandatory rendering-hook feasibility spike (item 8 below / Phase 1.5 in Section 8) — CSS-only composition (CSS filter math from typed knobs + generated rules + zap selectors + `custom_css`) is the **fallback** if the spike rejects deeper hooks, not the assumed default. `custom_css` remains the only user-controlled escape hatch in either path.
5. **A separate floating Boost editor window is the only editor — and it is the architectural owner.** The user-facing Boost surface is a **dedicated top-level native chrome window**: a `DialogDelegateView`-style host (or equivalent freestanding `views::WidgetDelegateView`) running in its own top-level `views::Widget`, with native window chrome (frame, title, close), launched on demand and freely positionable by the user. This mirrors Zen's separate Boost editor window literally: it is **not** anchored to the toolbar, **not** anchored to the address bar, **not** bound to a single tab's lifetime, and **not** a popup that auto-dismisses on focus loss. It is also **not** a navigable `chrome://` page, **not** a tab, **not** a sidebar editor, **not** a manager-style full-page WebUI, and **not** a `BubbleDialogDelegateView`-style toolbar bubble. WebUI / Mojom may be used as **subordinate implementation technology** to render the window's contents (HTML/CSS/TS inside the native window's content frame) and to talk to the Rust core via Mojom, but WebUI is never the architectural owner, never the routing entry point, and never user-navigable. No `chrome://maho-boost` (or similarly named) navigable host returns to the build under this plan. The earlier toolbar-bubble + Browser-keyed-coordinator approach (`maho_boost_bubble_view` + `maho_boost_bubble_coordinator`) is **superseded** by the separate-window host and is treated as legacy in this reset: any remaining bubble / coordinator code is secondary, off the critical path, and likely discarded outright in Phase 0 (Section 7). If a debug surface is needed, it is internal-only, gated, and not user-facing (see Section 9, Open Question 8).
6. **Knob names match Zen.** Field names in storage, FFI, and Mojom mirror Zen's names (`dot_angle_deg`, `secondary_dot_angle_deg_delta`, `brightness`, `saturation`, `contrast`, `enable_color_boost`, `auto_theme`, `smart_invert`, `font_family`, `text_case_override`, `size_override`, `zap_selectors`, `custom_css`). This kills translation drift across crates and Mojom.
7. **Color math matches Zen's parameter semantics; the rendering layer is decided by Phase 1.5.** Zen's runtime hooks Gecko's color pipeline. Maho's runtime renders the same scalar knobs through whichever layer Phase 1.5 selects — a deeper-than-CSS hook if feasible, CSS filter math (or equivalent CSS variables) as the fallback. Implementation strategy differs, the parameter contract does not, and the parameter contract is **not** held hostage to a CSS-only runtime: if Phase 1.5 selects a deeper hook, the same scalar knobs feed that hook unchanged. See item 8 for the fidelity scope.
8. **Visual-fidelity stance is settled by a mandatory early rendering-hook feasibility spike, not pre-decided.** "Source-level parity" in this plan means parity of: storage shape, knob names, knob semantics, active-selection model, separate-window editor interaction model, and runtime composition of the typed knobs into a per-domain visual effect. The runtime composition strategy — CSS-only vs deeper-than-CSS rendering hooks — is **not** pre-decided in this plan. Before any user-facing runtime is locked in, a **mandatory feasibility spike (Phase 1.5 in Section 8) must determine whether closer Zen-like rendering hooks are reachable inside Maho's Chromium overlay**. The spike must concretely investigate, with code probes and a written verdict: blink-level color-pipeline / paint-stage interception, compositor / GPU-stage filter injection, content-shell-level injection beyond user stylesheets, renderer-process color transforms, and any other path that would let Maho match Zen's rendered output more literally than CSS filter math allows. The spike's output is a yes/no verdict per candidate hook plus the cost / fork-burden estimate, not a vibe. **Phase 2 is gated on the spike's verdict.** Only if the spike concludes deeper hooks are infeasible, prohibitively expensive in fork-maintenance terms, or otherwise out of scope for the current Maho/Chromium overlay does CSS-only / overlay-runtime composition (CSS filter math from typed knobs, generated rules, zap selectors, `custom_css`) become the runtime contract. If any deeper hook is feasible at acceptable cost, the runtime contract shifts to the closest-to-Zen path the spike validates, and Sections 8 and 9 of this plan are amended in the same commit before Phase 2 begins. Code review may not unilaterally lock a runtime strategy ahead of the spike: "let's just do CSS for now and revisit later" is rejected on the same grounds as "let's just match Zen at the pixel level today" — both pre-empt the spike. Pixel-bit-identical reproduction of Zen's output is still not the bar; the bar is "as close to Zen's rendering contract as Chromium realistically permits, decided by evidence."
9. **Reset, not migrate.** No automatic migration from the old `custom_js` / `*_json` data shape into the new schema. The previous implementation's user-facing surface is internal and there is no install base to preserve. Any leftover rows from the old schema are dropped.

---

## 5. Source-Level Mismatch Matrix

| Axis | Zen (parity target) | Current Maho | Verdict |
|------|---------------------|---------------|---------|
| Scope key | Domain (host) | URL pattern (glob) | **Mismatch.** Replace with domain. |
| Knob storage | Typed scalar fields per knob | `color_theme_json`, `font_override_json` blobs | **Mismatch.** Replace with typed columns. |
| Color knobs | `dotAngleDeg`, `secondaryDotAngleDegDelta`, `brightness`, `saturation`, `contrast`, `enableColorBoost`, `autoTheme`, `smartInvert` | Implicit inside JSON blob, no first-class fields | **Mismatch.** Add as first-class fields. |
| Typography knobs | `fontFamily`, `textCaseOverride`, `sizeOverride` | Inside `font_override_json` blob | **Mismatch.** Add as first-class fields. |
| Zap selectors | List of selectors, CSS-emitted | List of selectors, CSS-emitted | **Match.** Salvage. |
| Custom CSS | Free-form trailing CSS | `custom_css` field exists | **Match.** Salvage. |
| Custom JS | Not part of parity surface | `custom_js` injection path is first-class | **Mismatch.** Remove from parity surface. |
| Active selection | One active boost per domain, persisted | `enabled` per rule, no domain-level active | **Mismatch.** Add domain row with `active_boost_id`. |
| Editor surface | Separate floating Boost editor window (top-level chrome window) | `chrome://maho-boost` full-page manager WebUI | **Mismatch.** Replace surface entirely with a separate top-level native window. |
| Templates UI | Implicit (clone/preset inside per-domain list) | First-class template flow in manager | **Mismatch.** Drop template manager surface. |
| Runtime injection | Renderer-pipeline-level CSS-equivalent composition (Gecko) | CSS + JS injection paths | **Mismatch.** Constrain runtime to non-JS path; exact injection layer (CSS-only vs deeper hook) decided by Phase 1.5 spike. |
| Mojom contract | n/a (Zen is Gecko) | `SiteRule`-shaped, drifted vs `store.ts`'s `BoostEntry` | **Throwaway.** Redesign Mojom around domain + typed boost. |

The matrix is dominated by mismatches. Salvage is the exception, not the rule.

---

## 6. Salvage vs Discard

### 6.1 Salvage (keep, possibly rename)

- `maho-storage` SQLite plumbing and migration runner.
- `maho-core` scaffolding for a manager-per-concern boost component (the *concept*, not the current `boost_manager.rs` API).
- `zap_selectors` shape and CSS emission.
- `custom_css` field and emission.
- The C++ injection plumbing in `maho_boost_injection_handler.cc` (the hook into URLLoader / document-start CSS), as a delivery mechanism. The *payload composition* is rewritten in Rust.
- Existing per-feature WebUI build wiring (GN targets, resource ids, BrowserContext keyed service registration), reusable for hosting WebUI content **inside the new separate Boost editor window** (subordinate content technology only, never as a navigable `chrome://` page).

### 6.2 Discard (delete or quarantine)

- `chrome://maho-boost` manager-page WebUI (`browser/resources/maho_boost/` page-level shell, store, manager screens, template manager screens).
- `custom_js` field, JS injection path in `maho_boost_injection_handler.cc`, and any FFI / Mojom surface exposing JS injection.
- `color_theme_json` and `font_override_json` blob fields. Replaced by typed columns. No migration.
- URL-pattern scope key in `maho-types::Boost` / `BoostUpdate`. Replaced by domain.
- Template manager surface and its CRUD. Cloning/preset behavior, if any, is internal to the per-domain list.
- Current `SiteRule`-shaped `maho_boost.mojom`. Replaced wholesale.
- Generated Mojom TS typings derived from the old shape. Regenerated from the new IDL.
- Any `BoostEntry` shape in `store.ts` that assumes the manager-page lifecycle. The new separate Boost editor window has its own minimal store.
- **Toolbar-bubble + Browser-keyed-coordinator infrastructure** (`maho_boost_bubble_view.{cc,h}`, `maho_boost_bubble_coordinator.{cc,h}`, plus the toolbar / location-bar anchor wiring that bound the editor to the address bar). **Superseded** by the separate floating Boost editor window (Section 4, item 5). These files are not "salvaged for the new shell" — they are removed in Phase 0 unless a concrete reuse case survives review (Section 7). No bubble-anchored editor returns under this plan.

The discarded items are not "frozen for later". They are removed from the build so they cannot drift back in.

---

## 7. Legacy Code Treatment Strategy

Section 6 says **what** to keep and discard at the conceptual level. This section says **how** the existing Boost code in the repo is physically handled during the reset: which files are hard-deleted now, which are reduced to cold compile-only stubs, which are retained as infrastructure, and which atomic commit each change lands in. Anything not listed here is out of scope for the reset and stays untouched.

The grounding source for this section is the current on-disk inventory: `maho/crates/maho-types/src/boost.rs`, `maho/crates/maho-core/src/boost_{manager,runtime,zap,css}.rs`, `maho-storage` schema, `maho-ffi` Boost surface, `maho-chromium/browser/net/maho_boost_injection_handler.{cc,h}`, `maho-chromium/browser/ui/webui/maho_boost/{maho_boost.mojom,maho_boost_page_handler.{cc,h},maho_boost_ui.{cc,h}}`, `maho-chromium/browser/resources/maho_boost/`, **the now-superseded** `maho-chromium/browser/ui/views/boost/maho_boost_bubble_{view,coordinator}.{cc,h}` (+ `maho_boost_bubble_interactive_uitest.cc`), `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_{view,data}.{cc,h}`, command/context-menu/location-bar/site-control integrations that wire into Boost, BUILD.gn targets for the above, and `maho/maho-webui/boost/maho-boost.ts` (+ corresponding bundle output under `maho/maho-webui/dist/assets/`). The bubble + coordinator pair is in scope for hard-deletion under Section 7.1, not retention; the new separate Boost editor window (Section 4, item 5) replaces it.

### 7.1 What Commit A actually shipped (manager-excision: navigable surface gone, build green)

Commit A as shipped is an **unreachable-stub reset**, not a clean-slate file purge. The reachable-from-the-user manager surface is gone, the JS-injection path is gone, the toolbar-bubble shell is gone, and the build is green. A number of files that the original plan slated for hard-deletion are still on disk as cold compile-only artifacts so the build graph stayed stable; those are inventoried in 7.2 and are scheduled for further cleanup in Commit B and later phases. The split is deliberate: getting to "no user-reachable Boost UX, no JS injection, build green" in one atomic commit was prioritized over physical file removal.

What Commit A removed from disk and from runtime reachability:

- **Navigable `chrome://maho-boost` surface (route gone, even though the WebUI controller files survive as cold stubs):**
  - The WebUIConfig / WebUI-controller-factory registration that made `chrome://maho-boost` a navigable host. After Commit A, no path inside Chromium routes a navigation to `MahoBoostUIConfig` / `MahoBoostUI`, and the Mojo binder that exposed `maho_boost::mojom::PageHandlerFactory` to a renderer is gone. `chrome://maho-boost` is therefore unreachable.
  - The renderer-bound binder entry that would have allowed a WebUI page to acquire `PageHandlerFactory` from the browser process.
  - The old page-level host `maho/maho-webui/boost/maho-boost.ts` (and its bundle output under `maho/maho-webui/dist/assets/`) — verify and remove any leftovers in Commit B if any survived.

- **Toolbar-bubble + Browser-keyed-coordinator editor shell (full physical removal):**
  - `maho-chromium/browser/ui/views/boost/maho_boost_bubble_view.{cc,h}` and `maho_boost_bubble_coordinator.{cc,h}` are gone from disk. They are not the architectural owner; the separate Boost editor window (Section 4, item 5) is.
  - `maho_boost_bubble_interactive_uitest.cc` is gone from disk. New tests against the separate Boost editor window are written from scratch in Phase 4; nothing is rescued from the bubble's UI test, because the bubble's anchoring / focus / dismissal behavior does not apply to a top-level window.
  - Anchor wiring whose **only** purpose was anchoring the bubble (anchor-view lookup helpers, bubble-specific `BrowserUserData` registrations) is gone. Command / context-menu / location-bar / site-control entry points that *invoke* the editor are kept (Section 7.3), with their handler bodies reduced to "open the Phase 4 separate Boost editor window" TODOs.
  - The source-list entries that wired the bubble files into the build are gone, but `maho-chromium/browser/ui/views/boost/BUILD.gn` retains empty `group("maho_boost_bubble")` and `group("interactive_tests")` shells as compatibility scaffolding (see 7.2). The shells contain no sources and no deps; they exist only so external GN references during the reset do not break and are scheduled for removal in Commit B once the rest of the build graph is confirmed not to need them.

- **`custom_js` end-to-end at the contract layer:**
  - `custom_js` is removed from `maho-types::Boost` and `maho-types::BoostUpdate`.
  - The JS-injection branch in `maho-chromium/browser/net/maho_boost_injection_handler.{cc,h}` is gone; the handler's `DidFinishNavigation` body is now a single `// TODO(maho-boost-reset): fill in Phase 2` no-op.
  - The old mojom `custom_js`-shaped fields are gone (the entire SiteRule shape was replaced — see the mojom bullet below).
  - **Caveat:** the `maho-storage` `boosts.custom_js` column and the corresponding accessors are still in the schema as a compatibility remnant (see 7.2 / Commit B). No code reads or writes through them after Commit A; they are scheduled for destructive-migration removal in Commit B together with the rest of the URL-pattern / JSON-blob schema.

- **Old `SiteRule`-shaped Mojom contents:**
  - `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` no longer carries the URL-pattern / JS-injection / SiteRule shape. The file itself stays on disk as a near-empty placeholder declaring only `PageHandlerFactory`, `PageHandler::GetState() => (bool active)`, and `Page::OnStateChanged(bool active)` — see 7.2. No method on this stub IDL is invoked at runtime, because the binder is gone.
  - Any pre-existing generated TS bindings derived from the old IDL are out of the build via the new minimal mojom; any `BoostEntry` / `SiteRule` shape in TS is gone.

- **Tests targeting the deleted surface:**
  - The bubble interactive UI test is deleted (see above).
  - Any unit tests in `maho-core` / `maho-storage` that asserted URL-pattern matching, `custom_js` injection, JSON-blob round-trips, or template CRUD have been removed. Storage round-trip tests against the still-present URL-pattern / JSON-blob columns are scheduled for removal in Commit B alongside the schema reset.

After Commit A, the user-facing reachability picture is: `chrome://maho-boost` is unreachable, no JS-injection path exists at runtime, no toolbar-bubble editor exists, no manager / template UI exists in any layer, and the build is green. There is no user-facing Boost UI; this is intentional. What does still exist on disk — and is therefore the input to Commit B and later phases — is enumerated in 7.2.

### 7.2 Cold compile-only stubs left on disk after Commit A (NOT fallback UX)

A larger set of artifacts than originally planned is on disk after Commit A as cold compile-only stubs. They render nothing, route nothing, and have no flag that re-enables manager / bubble behavior. Their only purpose is to keep the build graph stable while Commits B+ reshape the schema, contracts, and runtime entry points. **None of these surfaces is reachable as Boost UX during the reset.** Any stub still empty at the end of Phase 4 is deleted in Phase 5.

- **Separate Boost editor window shell** — `maho-chromium/browser/ui/views/boost/maho_boost_window_view.{cc,h}` and `maho_boost_window_controller.{cc,h}`. Introduced in Commit A as a minimal `views::View` plus a no-op controller with empty `Show()` / `Hide()` bodies. Phase 4 fills these out into a `DialogDelegateView` (or equivalent freestanding `views::WidgetDelegateView`) hosted in its own top-level `views::Widget`, with native window chrome. **This shell — not the deleted bubble — is the architectural owner of the Boost editor surface** (Section 4, item 5). Phases earlier than 4 may show it only behind a debug command, never as default UX.

- **Cold `chrome://maho-boost` WebUI controller files (compiled but unreachable):**
  - `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.{cc,h}` and `maho_boost_page_handler.{cc,h}` are still compiled by `maho-chromium/browser/ui/webui/maho_boost/BUILD.gn`. They are **not** the architectural owner of the editor and **not** scheduled to become the editor's host. They are cold because the navigable-route registration (`MahoBoostUIConfig` on the WebUI controller factory) and the renderer-side binder are gone (7.1). No user-reachable code path constructs `MahoBoostUI` after Commit A.
  - These files survive Commit A only to keep `browser/ui/webui/maho_boost/BUILD.gn` and the resources GN target green without a same-commit churn through the resources-pipeline. They are scheduled for deletion in Phase 5 (or earlier, as soon as Phase 3's window-content host is wired and the resources shell is no longer needed for build-graph reasons).

- **Minimal `maho_boost.mojom` stub** — the IDL file `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` survives Commit A as a near-empty stub declaring only `PageHandlerFactory.CreatePageHandler`, `PageHandler.GetState() => (bool active)`, and `Page.OnStateChanged(bool active)`. The old `SiteRule` / URL-pattern / `custom_js` / template shape is gone. This stub exists purely so the GN graph (`mojom("mojo_bindings")` in the same `BUILD.gn`) stays green; no method on it is invoked at runtime, because the binder is gone (7.1). Phase 3 replaces this IDL wholesale with the domain-scoped contract listed in Section 8 Phase 3.

- **Minimal `browser/resources/maho_boost/` shell (4 files, no React, no store):** the directory still exists, reduced to:
  - `BUILD.gn` (drives `build_webui` against the minimal asset set),
  - `boost.html` (placeholder),
  - `boost.css` (placeholder),
  - `app.ts` (placeholder).
  The earlier full page-level shell — `package.json`, `bun.lock`, `bundle_react.mjs`, `vitest.config.ts`, the `react/` / `views/` / `standalone/` subtrees, and the `maho_boost.mojom-webui.js.d.ts` typings — is gone. The remaining four files are not navigable (the route is gone — 7.1) and serve only as build-graph anchors for the cold WebUI controller files above. Phase 3 either replaces this content (when the separate window's content frame is wired up) or deletes it; either path is consistent with Section 4, item 5 (no navigable `chrome://` host).

- **`browser/ui/views/boost/BUILD.gn` empty compatibility groups** — the file retains two empty `group()` declarations:
  - `group("maho_boost_bubble") { }` — placeholder for any external GN reference that still pointed at the bubble target during the Commit A churn.
  - `group("interactive_tests") { }` — placeholder for the interactive-test target that previously aggregated `maho_boost_bubble_interactive_uitest.cc`.
  Both groups have no sources and no deps. They are scheduled for removal in Commit B (or as soon as the rest of the build graph is confirmed not to need them). They are **not** evidence that the bubble is coming back; the bubble files themselves are gone (7.1) and the no-coexistence rule (7.5) still applies.

- **Sidebar boosts shell** — `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.{cc,h}` and `maho_sidebar_boosts_data.{cc,h}` are reduced to placeholder views that return empty models and render nothing. The sidebar entry is hidden or shows an explicit empty state with no editor surface. This is **not** the new editor; it stays for compile-graph stability and is deleted in Phase 5 if the parity surface does not use it.

- **Injection handler shell** — `maho-chromium/browser/net/maho_boost_injection_handler.{cc,h}` survives as a `WebContentsObserver` whose `DidFinishNavigation` is a single TODO no-op. The URLLoader / document-start hook is retained infrastructure (see 7.3) **only as a candidate delivery mechanism**: whether it is the runtime entry point at all, vs a deeper-than-CSS hook, is decided by Phase 1.5 (Section 8). If Phase 1.5 selects CSS-only composition, Phase 2 fills this handler. If Phase 1.5 selects a deeper hook, this handler may be deleted in Phase 2 and replaced with the chosen entry point.

- **Rust schema / type / runtime compatibility remnants** — Commit A removed `custom_js` from `maho-types::Boost` / `BoostUpdate` and reduced the runtime modules (`boost_runtime.rs`, `boost_css.rs`, `boost_zap.rs`, `boost_manager.rs`) to skeletal placeholders, but it intentionally did **not** reshape the deeper schema and type surface in the same commit. The following items are still on disk in their pre-parity (URL-pattern-keyed, JSON-blob-shaped) form and are scheduled for Commit B / Phase 1:
  - `maho-types::Boost` still carries `url_pattern: UrlPattern`, the composite `color_theme: Option<ColorOverride>` and `font_override: Option<FontOverride>` records, the `BoostEditorState` and `BoostColorEngineConfig` companion types, and `enabled` / `space_scope` fields. None of these match Section 4's domain-keyed typed-knob contract. They survive Commit A as compatibility shapes; they do not represent the parity contract.
  - `maho-types::BoostUpdate` mirrors that shape.
  - `maho-storage` schema (`boosts` table) still has columns for `url_pattern`, `pattern_type`, `custom_css`, `custom_js`, `color_theme`, `font_override`, `enabled`, `space_scope`, `zap_selectors_json`, `editor_state_json`, `color_engine_config_json`, `runtime_flags_json`, plus the `idx_boosts_url_pattern` index. The `custom_js` column survives as a compatibility remnant (see 7.1 caveat); nothing in the rest of the tree reads or writes it after Commit A.
  - `maho-core/src/boost_manager.rs` still consumes the old URL-pattern / composite-record types. Its body is skeletal / TODO and not exercised by the rest of the core, but its type-surface is the pre-reset shape.
  - `maho-ffi` Boost surface is correspondingly still pre-reset shaped where it exists; it is scheduled for the new contract in Commit B.

  The decision to defer this cleanup to Commit B is deliberate: removing a navigable host, removing a JS-injection path, and removing the bubble shell could be landed atomically with `chrome://maho-boost` unreachable and the build green; reshaping `maho-types`, `maho-storage`, `maho-ffi`, and `boost_manager` in the same commit would have churned every boost-touching file in the workspace and made the Commit A bisect point useless. Commit B is where that schema/contract reset lands (7.4 / Section 8 Phase 0 Commit B).

These stubs are explicitly **not** a fallback UX. They render nothing, return nothing, have no flag that re-enables manager / bubble behavior, and live for one or two more commits before Commits B+ either fill them in (window shell, injection handler if Phase 1.5 selects CSS) or delete them (cold WebUI controller files, mojom stub, resources shell, BUILD.gn empty groups, schema/type remnants).

### 7.3 Retained infrastructure (no behavioral change)

These items are general plumbing the new parity surface reuses. They are **not** modified in Commit A or Commit B beyond removing references to deleted types.

- **`maho-storage` SQLite migration runner** and the `Storage` surface itself. Only the Boost-related migrations and table definitions are rewritten.
- **`maho-ffi` crate shell** and its build wiring (bridge generation, C++ glue). The Boost-specific FFI symbols are reset to the new contract in Commit B / Phase 2; the crate, its bridge codegen, and its non-Boost surface are retained as-is.
- **C++ injection plumbing concept** in `browser/net/maho_boost_injection_handler.{cc,h}`: the URLLoader / document-start hook is the delivery mechanism the new CSS path reuses. Only the payload composition is rewritten.
- **Top-level `views::Widget` / `DialogDelegateView` pattern** in Chromium / Maho's existing standalone-dialog code (e.g. how DevTools-undocked, certificate viewer, task manager, and similar standalone windows are constructed). This is the pattern the new separate Boost editor window (Section 7.2 stub) reuses. **The separate Boost editor window is the architectural owner of the Boost editor surface** (Section 4, item 5). Any WebUI / Mojom host introduced in Phase 3 lives *inside* this native window's content frame as content technology; it does not replace the window, does not become a navigable host, and does not become the entry point. The previously planned `BubbleDialogDelegateView` + `BrowserUserData`-keyed coordinator pattern is **not** retained infrastructure — it is removed in Section 7.1 and not reintroduced here.
- **BUILD.gn target shells** under `browser/ui/webui/maho_boost/`, `browser/ui/views/boost/`, `browser/ui/views/sidebar/`, `browser/net/`, and the `maho-core` / `maho-storage` / `maho-ffi` `Cargo.toml` / `build.rs` wiring. Source lists are pruned to match the file set after Commit A; the targets themselves stay so the build graph is stable.
- **`maho-core` event dispatcher / change-notification infrastructure** that other managers already use. The new domain-keyed Boost manager subscribes through the same channel.
- **Command, context-menu, location-bar, and site-control integrations** that currently expose a Boost entry point: their registration is preserved, but their handler bodies are reduced to "open the Phase 4 separate Boost editor window via the new window controller" stubs (or no-ops, until the window controller exists). The integration points are not deleted, because the new separate window reuses them as launch sites. None of them anchor the window — invocation simply asks the controller to show its top-level `Widget`.

### 7.4 Atomic-commit sequencing

Phase 0 in Section 8 is **two commits, not one**. They are atomic and strictly ordered.

- **Commit A — Manager / bubble / JS-injection excision (shipped, build-green, unreachable-stub reset).** Apply 7.1 (hard-delete) and 7.2 (cold compile-only stubs). After Commit A: the tree compiles, surviving tests pass, `chrome://maho-boost` is unreachable (route registration and renderer binder gone), the toolbar-bubble + Browser-keyed-coordinator shell is physically gone, no JS-injection path exists at runtime, no manager / template UI exists in any layer, and the new separate-window stub compiles as a cold no-op. The schema and the `maho-types::Boost` / `BoostUpdate` shape are **not yet** parity-shaped — they are still URL-pattern-keyed with composite color/font records and JSON-blob columns (7.2), and the cold WebUI controller files, the minimal mojom stub, the 4-file resources shell, and the empty `views/boost/BUILD.gn` compatibility groups are still on disk. There is no Boost UX visible to the user. Full `python3 maho-chromium/build/scripts/build_maho.py` is green in this state.
- **Commit B — Schema and contract reset (next; subsumes the deferred Rust/storage cleanup).** Drop the URL-pattern scope key, composite `ColorOverride` / `FontOverride` records, `BoostEditorState`, `BoostColorEngineConfig`, `enabled` / `space_scope`, and the matching JSON-blob storage columns from `maho-types::Boost` / `BoostUpdate` and the `maho-storage` schema (destructive migration; no data migration). Drop the still-present `boosts.custom_js` column and the `idx_boosts_url_pattern` index. Replace `maho-types::Boost` / `BoostUpdate` with the typed domain-keyed records named in Section 4. Update `maho-ffi` to the new contract shape (signatures only; bodies may remain `todo!()`). Reset `boost_manager.rs` to the Phase 1 API skeleton (signatures only). Old-shape unit tests are removed; new-shape tests are deferred to Phase 1's commit. Remove the empty `group("maho_boost_bubble")` / `group("interactive_tests")` shells in `maho-chromium/browser/ui/views/boost/BUILD.gn` once the rest of the build graph is confirmed not to reference them. Audit and remove any leftover `maho/maho-webui/boost/maho-boost.ts` and bundle output if any survived. After Commit B: the contract types match Section 4 exactly, even if behavior is still `todo!()`. The cold WebUI controller files, minimal mojom stub, and 4-file resources shell may stay until Phase 3 / Phase 5 (their removal is not a Commit B prerequisite as long as the build stays green).
- **Commits C and after — New parity surfaces.** Phase 1 (schema + manager body), Phase 1.5 (rendering-hook feasibility spike, mandatory gate), Phase 2 (runtime implementation per Phase 1.5's verdict), Phase 3 (Mojom + in-window content host — at which point the cold WebUI controller files, mojom stub, and resources shell are either replaced wholesale or deleted), Phase 4 (separate Boost editor window UI), Phase 5 (polish, including final removal of any remaining cold stubs from 7.2) ship in that order on top of the slab Commits A and B established. Each phase is its own commit or PR.

Commits A and B are not squashed. The split exists so `git bisect` can land cleanly on "manager / bubble / JS-injection gone, schema not yet reshaped" vs "schema reshaped, runtime not yet rewired", and so review of the deletion is not entangled with review of the new contract.

### 7.5 No-coexistence rule

The old manager UX and the new Zen-parity UX must **never** coexist in the same build, behind any flag, feature switch, or dev override.

- No `--enable-features=MahoBoostManager` flag. No `maho_boost_legacy_ui` GN arg. No `if (legacy_manager) { ... } else { ... }` branches anywhere in C++, Rust, TS, or Mojom. Equally: no flag that re-enables the deleted toolbar-bubble shell as a fallback editor.
- `chrome://maho-boost` does not return to the build as a navigable host. If a debug surface is needed during development (Section 9, Open Question 8), it lives under a different host (e.g. `chrome://maho-boost-debug`), is gated by `is_debug` or a tools-only GN target, and never serves the manager UX or a bubble UX.
- The Phase 4 separate Boost editor window is the sole user-facing entry point. If the window is incomplete on a given commit, the answer is "no UI yet", not "fall back to the manager" and not "fall back to the bubble".
- Generated Mojom TS bindings, `store.ts` shapes, and any frontend type named `BoostEntry` / `SiteRule` are removed in Commit A and never re-added.

A "let's keep both for safety" suggestion in code review is rejected on sight. The reset's correctness depends on the old surface being unreachable, not coexisting.

---

## 8. Recommended Sequencing (data-model first, UI last)

The goal of the sequencing is: **never let UI work outrun the contract**. Each phase ends in a state that compiles, tests, and is internally consistent, even if user-visible surface is reduced or absent.

### Phase 0. Demolition (two atomic commits — see Section 7.4)

Phase 0 is split into Commit A (manager / bubble / JS-injection excision; **shipped**) and Commit B (schema / contract reset; **next**). Section 7 is the full source-of-truth mapping for what was hard-deleted in Commit A, what stayed as cold compile-only stubs, and what is retained as infrastructure.

**Commit A — Manager / bubble / JS-injection excision (shipped, build-green, unreachable-stub reset; applies Sections 7.1 + 7.2):**

- Removed the `chrome://maho-boost` route from the build: the WebUIConfig / WebUI-controller-factory registration that made `MahoBoostUI` navigable is gone, and the renderer-side Mojo binder for `maho_boost::mojom::PageHandlerFactory` is gone. The host string still appears in `maho-chromium/components/constants/webui_url_constants.h` (`kMahoBoostHost` / `kMahoBoostURL`) as a constant for legacy contracts, but no path resolves it to a controller. `chrome://maho-boost` returns a navigation failure.
- Hard-deleted the toolbar-bubble + Browser-keyed-coordinator shell (`maho_boost_bubble_view.{cc,h}`, `maho_boost_bubble_coordinator.{cc,h}`, `maho_boost_bubble_interactive_uitest.cc`) and any anchor wiring whose only purpose was hosting that bubble.
- Removed `custom_js` from `maho-types::Boost` and `maho-types::BoostUpdate` and removed the JS-injection branch from `maho-chromium/browser/net/maho_boost_injection_handler.{cc,h}`. The `maho-storage` `boosts.custom_js` column is **still present** as a compatibility remnant (no code reads or writes it) and is scheduled for destructive-migration removal in Commit B.
- Replaced the old `SiteRule` / URL-pattern / JS-injection contents of `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` with a near-empty stub declaring only `PageHandlerFactory`, `PageHandler::GetState() => (bool active)`, and `Page::OnStateChanged(bool active)`. The IDL file itself stays for build-graph stability; no method on it is invoked at runtime.
- Reduced `maho-chromium/browser/resources/maho_boost/` to a 4-file shell (`BUILD.gn`, `boost.html`, `boost.css`, `app.ts`). The earlier React / store / templates / standalone subtrees and TS `.d.ts` typings are gone.
- Left `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.{cc,h}` and `maho_boost_page_handler.{cc,h}` on disk and still compiled by `browser/ui/webui/maho_boost/BUILD.gn`. They are cold and unreachable (no route, no binder). They are scheduled for deletion in Phase 5 (or earlier, when Phase 3 wires the in-window content host).
- Reduced the sidebar boosts shells (`maho_sidebar_boosts_{view,data}.{cc,h}`) and the injection handler (`maho_boost_injection_handler.{cc,h}`) to cold no-op stubs per Section 7.2. **These are not fallback UX.**
- Introduced the new separate Boost editor window stub in `maho-chromium/browser/ui/views/boost/`: `maho_boost_window_view.{cc,h}` (minimal `views::View`) plus `maho_boost_window_controller.{cc,h}` (no-op `Show()` / `Hide()`). This shell — not the deleted bubble — is the architectural owner of the future Boost editor surface (Section 4, item 5). Phase 4 fills it out into a `DialogDelegateView` / top-level `views::Widget`.
- Retained empty `group("maho_boost_bubble") {}` and `group("interactive_tests") {}` declarations in `maho-chromium/browser/ui/views/boost/BUILD.gn` as build-graph compatibility shells. Both are scheduled for removal in Commit B.
- Reduced `maho/crates/maho-core/src/boost_manager.rs`, `boost_runtime.rs`, `boost_css.rs`, and `boost_zap.rs` to skeletal placeholders. The template manager flow (template CRUD, list-by-pattern) is gone. `boost_manager.rs` still type-references the pre-reset URL-pattern / composite-record `maho-types::Boost` shape; that is intentional and is reset in Commit B.
- Rewired the command / context-menu / location-bar / site-control entry points to `// TODO(maho-boost-reset): open Phase 4 separate Boost editor window` no-ops (verified at `maho_command_action_handler.cc`, `maho_page_info_ui.cc`, `maho_sidebar_tab_list_view.cc`).
- Verified state: full `python3 maho-chromium/build/scripts/build_maho.py` is green, `chrome://maho-boost` is unreachable, no JS-injection path exists at runtime, no manager / template / bubble surface exists in any layer, no user-facing Boost UI exists, and the cold stubs above are inert.

**Commit B — Schema and contract reset (next; subsumes the deferred Rust/storage cleanup that Commit A intentionally did not touch):**

- Reshape `maho-types::Boost` / `BoostUpdate` to the domain-keyed typed-knob contract in Section 4. Drop the `url_pattern: UrlPattern` field; drop the composite `ColorOverride` / `FontOverride` records; drop `BoostEditorState`, `BoostColorEngineConfig`, `enabled`, and `space_scope`; drop the `CompiledBoostRuntime` shape (replaced by the `ComposedRuntimePayload` of Phase 2). Type bodies use the Section 4 names (`dot_angle_deg`, `secondary_dot_angle_deg_delta`, `brightness`, `saturation`, `contrast`, `enable_color_boost`, `auto_theme`, `smart_invert`, `font_family`, `text_case_override`, `size_override`, `zap_selectors`, `custom_css`).
- Reshape the `maho-storage` `boosts` schema (destructive migration; no data migration). Drop the `url_pattern`, `pattern_type`, `custom_js`, `color_theme`, `font_override`, `enabled`, `space_scope`, `zap_selectors_json`, `editor_state_json`, `color_engine_config_json`, `runtime_flags_json` columns and the `idx_boosts_url_pattern` index. The replacement schema is finalized by Phase 1 (one column per typed knob, plus `domain` / `id` and the `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)` table); Commit B may either land the new schema directly or land an intermediate "no Boost rows persisted" schema, whichever keeps the workspace `cargo check` and Chromium build green.
- Update `maho-ffi` to the new contract shape; bodies may remain `todo!()` until Phase 1 / Phase 2.
- Reset `boost_manager.rs` to the Phase 1 API skeleton (signatures only — `list_for_domain`, `get`, `create`, `update`, `delete`, `set_active`, `get_active`, `compose_css`); internals may remain `todo!()` until the Phase 1 commit lands.
- Remove old-shape unit tests in `maho-core` and `maho-storage` that asserted URL-pattern matching, JSON-blob round-trips, composite color/font records, or `enabled`-per-rule semantics.
- Remove the empty `group("maho_boost_bubble") {}` and `group("interactive_tests") {}` shells in `maho-chromium/browser/ui/views/boost/BUILD.gn` once the rest of the build graph is confirmed not to reference them.
- Audit the workspace for any leftover `maho/maho-webui/boost/maho-boost.ts` source or `maho/maho-webui/dist/assets/boost-*.js` bundle output and delete them if found.
- Leave the cold WebUI controller files (`maho_boost_ui.{cc,h}`, `maho_boost_page_handler.{cc,h}`), the minimal `maho_boost.mojom` stub, and the 4-file `browser/resources/maho_boost/` shell on disk for now. Their removal is not a Commit B prerequisite as long as the build stays green; they are deleted or replaced wholesale in Phase 3 (when the in-window content host is wired) or Phase 5 (final cold-stub sweep).
- After Commit B: the contract types match Section 4 exactly, the workspace `cargo check` and full Chromium build remain green, and the slab is ready for Phase 1's behavior fill.

### Phase 1. Schema and model (Rust)

- Define new typed records in `maho-types/src/boost.rs`:
  - `Boost { id, domain, name, color: ColorBoost, typography: TypographyBoost, zap_selectors: Vec<String>, custom_css: String, created_at, updated_at }`
  - `ColorBoost { enable_color_boost, dot_angle_deg, secondary_dot_angle_deg_delta, brightness, saturation, contrast, auto_theme, smart_invert }`
  - `TypographyBoost { font_family: Option<String>, text_case_override: TextCase, size_override: Option<f32> }`
  - `TextCase` enum: `None | Upper | Lower | Capitalize`.
  - `BoostUpdate` shaped as field-wise `Option<...>` over the same typed knobs.
- Schema in `maho-storage/src/sqlite.rs`:
  - `boosts` table with a column per knob, `domain` column, `id` column, indexes on `domain`.
  - `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)`.
- `boost_manager.rs` (rewritten, not patched):
  - `list_for_domain(domain)`
  - `get(boost_id)`
  - `create(domain, init)`
  - `update(boost_id, BoostUpdate)`
  - `delete(boost_id)`
  - `set_active(domain, Option<boost_id>)`
  - `get_active(domain)`
  - `compose_css(boost) -> String` (typed knobs + zap + custom_css)
- Unit tests at this layer: schema round-trip, `set_active` invariants, `compose_css` golden outputs for representative knob combinations.

### Phase 1.5. Rendering-hook feasibility spike (MANDATORY GATE on Phase 2)

This phase is **non-optional**. It exists because Section 4, item 8 forbids locking a runtime strategy by default. Phase 2 cannot start until Phase 1.5 produces a written verdict.

- Investigate, with concrete code probes inside `chromium/src/` and `maho-chromium/`, whether the following deeper-than-CSS rendering hooks are reachable at acceptable cost in Maho's Chromium overlay:
  - **Blink-level color-pipeline / paint-stage interception** (e.g. hooking color-correction or paint filters at the blink layer for matched documents).
  - **Compositor / GPU-stage filter injection** (post-paint, pre-display color transforms scoped to a frame tree).
  - **Renderer-process color transforms** beyond user stylesheets (style-resolver hooks, `ColorFilter` insertion, frame-tree-scoped paint flags).
  - **Content-shell / process-level injection** mechanisms that go beyond Chromium's user stylesheet API and could match Zen's rendering contract more literally.
  - Any other path the spike author identifies that brings rendered output closer to Zen than CSS filter math allows.
- Output an explicit written verdict per candidate hook: **feasible / infeasible**, with a one-page cost note covering: number / scope of `chromium_src/` overrides, fork-rebase burden estimate, perf and security implications, and whether it can be staged without forking large Chromium subsystems.
- Output a runtime contract recommendation:
  - If **any** deeper hook is feasible at acceptable cost → recommend the closest-to-Zen feasible hook as the runtime contract.
  - If **all** deeper hooks are infeasible / prohibitively costly → recommend CSS-only / overlay-runtime composition as the fallback runtime contract, **with the spike's evidence as the justification**, not as the default.
- Amend Sections 4 (item 8), 8 (Phase 2 contents), and 9 (items 1, 2, 10) **in the same commit that records the spike's verdict** so the plan and the runtime contract stay aligned.
- This phase ships **no user-facing UI** and may ship behind throwaway test-only code paths. Its deliverable is the verdict + amendments, not shipping product code.

**Hard gate:** Phase 2 may not begin while Phase 1.5 is unresolved. "We'll start CSS work and decide later" is rejected.

### Phase 2. Runtime implementation (strategy decided by Phase 1.5)

The runtime strategy implemented in this phase is **whatever Phase 1.5 selected**, not pre-committed CSS-only. The bullets below describe the CSS-only fallback path; if Phase 1.5 selects a deeper hook, this section is amended in the same commit and the implementation targets that path instead, while preserving the same end-to-end contract (active boost for active document → composed visual effect → live-update channel).

- Wire `maho-ffi` to expose `get_active_for_domain(domain) -> Option<ComposedRuntimePayload>` and a change-notification channel. The payload type is named generically (`ComposedRuntimePayload`, not `ComposedCss`) so it can carry CSS today and richer rendering instructions if Phase 1.5 selects a deeper hook.
- Reduce `maho_boost_injection_handler.cc` (or the deeper-hook entry point selected by Phase 1.5) to a single path: on document start for a navigation, query the active runtime payload for the domain, apply it via the strategy chosen by Phase 1.5 (user stylesheet / blink hook / compositor filter / etc.), nothing else.
- Validate end-to-end on at least:
  - Pure color knob change (hue / brightness / saturation / contrast).
  - `smart_invert` on a light page and a dark page.
  - `zap_selectors` hiding visible elements.
  - `custom_css` overriding a known selector.
  - Typography knobs (font family swap, text case, size override).
- Visual-fidelity baseline: the validation suite captures rendered output of the test pages and compares against equivalent Zen output (screenshots / instrumented metrics, depending on what Phase 1.5's strategy enables). This is parity evidence, not a regression gate.
- This phase ships **without any user-facing UI**. Activation is poked via test hooks or a debug command. The point is to prove the runtime contract before building the editor window.

### Phase 3. Mojom contract and in-window content host

Per Section 4, item 5, the user-facing Boost surface is a **separate top-level native chrome window** owned by the Views layer (Section 7.2 stub). Phase 3 wires the IPC contract and the content-rendering layer **that lives inside that native window's content frame**; it does not introduce a navigable WebUI page and does not reintroduce a toolbar bubble.

- New `maho_boost.mojom`:
  - Domain-scoped methods: `ListBoostsForDomain(domain) -> array<Boost>`, `GetActiveBoost(domain) -> Boost?`, `SetActiveBoost(domain, boost_id?)`, `CreateBoost(domain, BoostInit) -> Boost`, `UpdateBoost(boost_id, BoostUpdate) -> Boost`, `DeleteBoost(boost_id)`.
  - A change observer interface so the editor window and the runtime stay in sync (and so any other open windows on the same domain re-apply per Open Question 5).
- The Phase 4 editor renders inside the separate native window. If HTML/CSS/TS is the chosen content layer (default), it is hosted as **subordinate WebUI content embedded inside the window's content `WebView`**, not as a navigable `chrome://` host. There is no address-bar-reachable URL, no entry in the WebUI controller factory that exposes a navigable Boost host, and no tab-level surface. The Mojom interface is bound from the window's content frame to the Rust core; the window itself remains a native top-level `views::Widget` with native chrome.
- TS bindings regenerated from the new IDL. `store.ts` is rewritten as a thin per-window store keyed on the active domain, scoped to the window's lifetime. Old `BoostEntry`-style abstractions are not carried over.
- Naming hygiene: any constant, GN target, or comment that describes this layer as a "manager page", "WebUI page", "chrome:// host", "panel", "popup", or "bubble" is rejected. The correct names are "Boost editor window", "Boost window", and "Boost window content" — never "Boost page", "Boost panel", or "Boost bubble".

### Phase 4. Separate Boost editor window UI

- Build the editor UI to match Zen's separate Boost editor window literally: a dedicated top-level native chrome window (Section 7.2 stub filled out) with sections for color, typography, zap, custom CSS, and a list/switcher for the domain's boosts plus active selection. The window is the sole user-facing entry point; invocation routes through the command / context-menu / location-bar / site-control integrations preserved in Section 7.3, all of which simply ask the window controller to show its top-level `Widget`. The window is freely positionable, has native title/close chrome, does not auto-dismiss on focus loss, and is not anchored to the toolbar or address bar.
- Live preview: edits dispatch through Mojom, runtime recomposes the runtime payload (CSS or deeper-hook payload per Phase 1.5), current tab and other open tabs on the same domain re-apply per Open Question 5. Debounce on slider knobs.
- Visual-fidelity target: per Section 4, item 8, the window reproduces Zen's separate-window interaction model, and reproduces Zen's visual effect via whichever runtime strategy Phase 1.5 selected (CSS-only fallback or deeper hook). It does not attempt pixel-bit-identical Zen rendering; it does aim "as close to Zen's rendering contract as Chromium realistically permits, by evidence".
- Empty state: "No Boost for this site. Create one." with a single primary action.
- Multi-boost UX: list of named boosts for the domain, click-to-activate, per-row delete, "New boost".
- The window's domain context follows the active tab's domain at the moment of invocation; the window does not silently re-target on tab switches mid-edit (decision recorded under Open Question 5).

### Phase 5. Polish and removal pass

- Remove any remaining references to the old manager / templates / JS injection / toolbar bubble in code, comments, GN files, AGENTS.md. Any sidebar or stub file from Section 7.2 still empty after Phase 4 is deleted here.
- Add Appium regression coverage: open the separate Boost editor window from each registered entry point, edit a knob, observe page change, persist across reload, switch active boost, delete a boost, close and reopen window, confirm cross-tab live update behavior matches Open Question 5.
- Confirm `chrome://maho-boost` is fully unreachable and not registered anywhere, and confirm no `BubbleDialogDelegateView`-shaped Boost editor remains in the tree.

The ordering is deliberate: the runtime contract is **decided by evidence (Phase 1.5)** and **proven (Phase 2)** before any user can see the separate editor window. UI is the final layer, not the leading one, and the runtime strategy is not assumed in advance.

---

## 9. Risks / Open Questions Requiring Explicit Decisions

These need an explicit answer from the user before or during execution. Defaults are listed but should be confirmed.

1. **Color math fidelity vs Zen.** Zen's color filter is implemented inside Gecko's color pipeline. **This question is genuinely open and is resolved by Phase 1.5's rendering-hook feasibility spike (Section 8).** If the spike finds a feasible blink / compositor / renderer-process hook at acceptable cost, Maho's color math targets that hook and aims for the closest-to-Zen rendering Chromium permits. If the spike finds no feasible deeper hook, the runtime falls back to CSS filter math from typed knobs and accepts the resulting delta vs Zen, with the spike's evidence as the justification. Document representative test pages so the spike has a concrete fidelity baseline. "We'll just use CSS for now and revisit later" is rejected: the spike must produce the verdict before Phase 2 ships.
2. **`smart_invert` scope.** Zen's smart invert touches non-CSS surfaces (e.g., `<img>` heuristics). **Bound to Phase 1.5 (Section 4, item 8 / Section 8).** Whether `smart_invert` ships as a CSS-only filter strategy with known image-heuristic gaps, or as a deeper-hook strategy that approaches Zen's image handling, is decided by the spike's verdict. If the spike selects a deeper hook for color, `smart_invert` should ride on the same hook where it improves image / non-CSS-reachable behavior. If the spike rejects deeper hooks, `smart_invert` ships as CSS filter math with the gaps documented. Either way, the parameter contract stays the same (`smart_invert` boolean knob with Zen's name).
3. **Editor entry point.** From which surface(s) is the separate Boost editor window opened? *Default:* a dedicated toolbar action button is the primary launch point, with the existing context-menu / location-bar / site-control integrations preserved as secondary launch points (Section 7.3). Confirm if the user wants a different primary entry point. The window itself, once open, is independent of the entry point — closing it does not require returning to the entry point.
4. **Multiple-domain matching.** Subdomains: does `boost@example.com` apply to `www.example.com`? Zen's behavior is host-scoped with limited eTLD+1 fallback. *Default:* exact host match for v1, eTLD+1 fallback as a follow-up flag.
5. **Cross-tab live update.** When the editor window edits the active boost, do all currently open tabs on that domain re-apply, or only the tab that was active at the moment the editor window was opened? *Default:* all tabs on that domain, via the change observer. The editor window itself does not silently re-target as the user switches tabs mid-edit; it stays bound to the domain captured at invocation time, and the user closes / reopens the window to switch domain context.
6. **Discoverability of the entry point.** Shipping a toolbar action implies icon, label, and tooltip. *Default:* lift Zen's icon/label semantics; do not invent new naming.
7. **No-JS guarantee in injection handler.** Confirm that removing `custom_js` is acceptable as a hard cut, with no compatibility flag to re-enable. *Default:* hard cut.
8. **Internal debug surface.** Is a `chrome://maho-boost-debug` page acceptable for dev-only inspection of the typed model? *Default:* no, until proven necessary; the editor window + tests are sufficient. If accepted, it must obey Section 4, item 5: it is not a manager UX, not a bubble UX, not user-discoverable, and lives behind a tools-only / `is_debug` GN gate (Section 7.5).
9. **Editor-host architecture (decided, do not re-open).** **Decided by Section 4, item 5:** the editor is a **separate top-level native chrome window** (a `DialogDelegateView` / freestanding `views::WidgetDelegateView` in its own `views::Widget`), introduced as a new shell in Section 7.2. The toolbar-bubble + Browser-keyed-coordinator approach is superseded and hard-deleted in Commit A (Section 7.1). WebUI / Mojom is subordinate content technology rendered inside the separate window, never the architectural owner. This question exists in this list only as a tripwire: if a future PR proposes "let's just expose the editor as a `chrome://` page for now", "let's host the editor in a sidebar / tab instead", or "let's put it back in a toolbar bubble", it must amend this plan first, not be merged.
10. **Deeper rendering-hook feasibility (mandatory spike, not a separate plan).** **Decided by Section 4, item 8 and Phase 1.5 (Section 8):** the choice between CSS-only runtime composition and deeper-than-CSS rendering hooks is **not** pre-decided and is **not** deferred to a separate future plan. It is resolved by the mandatory Phase 1.5 spike inside this plan, which gates Phase 2. Any PR that lands runtime work — CSS-only or deeper-hook — without referencing the Phase 1.5 verdict is rejected. Conversely, "let's just match Zen at the pixel level today" without spike evidence is also rejected; the spike's verdict is the authority.

Items 4 and 7 carry the most behavioral risk on already-decided axes and should be answered before Phase 2 ships. Items 1, 2, and 10 are resolved by the mandatory Phase 1.5 spike inside this plan, not by future plans. Item 9 is decided in favor of a separate top-level Boost editor window and exists to prevent drift, not to be revisited.

---

## 10. Concrete Next-Step Checklist

Order matters. Each item is a concrete, demonstrable artifact.

- [ ] **Confirm hard constraints (Section 4) and legacy-code treatment (Section 7) with the user**, especially Section 4 items 5 (separate floating Boost editor window, no manager page, no toolbar bubble), 6 (knob names match Zen), 8 (mandatory rendering-hook feasibility spike before runtime is locked), 9 (no migration), and Section 7.5 (no coexistence). Block on this.
- [x] **Phase 0 — Commit A: Manager / bubble / JS-injection excision (shipped, build-green, unreachable-stub reset).** What this commit actually achieved (Section 7.1 + 7.2):
  - Removed `chrome://maho-boost` from the route table by deleting the WebUIConfig / WebUI-controller-factory registration and the renderer-side Mojo binder; the host string in `maho-chromium/components/constants/webui_url_constants.h` survives as a constant only.
  - Hard-deleted the toolbar-bubble shell: `maho_boost_bubble_view.{cc,h}`, `maho_boost_bubble_coordinator.{cc,h}`, `maho_boost_bubble_interactive_uitest.cc`, and bubble-only anchor wiring.
  - Removed `custom_js` from `maho-types::Boost` / `BoostUpdate` and from `maho_boost_injection_handler.{cc,h}` (the JS-injection branch is gone; `DidFinishNavigation` is a TODO no-op). The `maho-storage` `boosts.custom_js` column is still present as a compatibility remnant; nothing reads it.
  - Replaced the old SiteRule-shaped `maho_boost.mojom` with a near-empty stub (`PageHandlerFactory`, `PageHandler::GetState`, `Page::OnStateChanged`).
  - Reduced `browser/resources/maho_boost/` to a 4-file shell (`BUILD.gn`, `boost.html`, `boost.css`, `app.ts`).
  - Left `maho_boost_ui.{cc,h}` and `maho_boost_page_handler.{cc,h}` on disk and still compiled by `browser/ui/webui/maho_boost/BUILD.gn`, but cold and unreachable.
  - Reduced sidebar boosts shells, injection handler, and `boost_manager.rs` / `boost_runtime.rs` / `boost_css.rs` / `boost_zap.rs` to cold placeholders. **Not fallback UX.**
  - Introduced `maho_boost_window_view.{cc,h}` and `maho_boost_window_controller.{cc,h}` as the cold separate-window stub.
  - Retained empty `group("maho_boost_bubble")` and `group("interactive_tests")` shells in `views/boost/BUILD.gn` for build-graph compatibility (scheduled for removal in Commit B).
  - Rewired command / context-menu / location-bar / site-control entry points to "open Phase 4 separate Boost editor window" TODOs.
  - Verified: full `python3 maho-chromium/build/scripts/build_maho.py` is green, `chrome://maho-boost` is unreachable, no JS-injection path exists at runtime, no manager / template / bubble surface remains, no user-facing Boost UI exists.
- [ ] **Phase 0 — Commit B: Schema and contract reset PR (next; subsumes the deferred Rust/storage cleanup that Commit A intentionally did not touch).** Section 7.4.
  - Reshape `maho-types::Boost` / `BoostUpdate` to the Section 4 domain-keyed typed-knob contract. Drop `url_pattern: UrlPattern`, the composite `ColorOverride` / `FontOverride` records, `BoostEditorState`, `BoostColorEngineConfig`, `enabled`, `space_scope`, and `CompiledBoostRuntime`. Field names match Zen exactly.
  - Reshape the `maho-storage` `boosts` schema (destructive migration). Drop `url_pattern`, `pattern_type`, `custom_js`, `color_theme`, `font_override`, `enabled`, `space_scope`, `zap_selectors_json`, `editor_state_json`, `color_engine_config_json`, `runtime_flags_json` columns and the `idx_boosts_url_pattern` index.
  - Update `maho-ffi` to the new contract shape; bodies may remain `todo!()` until Phase 1.
  - Reset `boost_manager.rs` to the Phase 1 API skeleton (signatures only).
  - Remove old-shape unit tests in `maho-core` / `maho-storage`.
  - Remove the empty `group("maho_boost_bubble") {}` and `group("interactive_tests") {}` shells in `views/boost/BUILD.gn` once the rest of the build graph confirms no external reference.
  - Audit and remove any leftover `maho/maho-webui/boost/maho-boost.ts` source or bundle output.
  - Leave the cold WebUI controller files, minimal mojom stub, and 4-file resources shell on disk for now (they are deleted or replaced wholesale in Phase 3 / Phase 5).
  - Verify `cargo check` and full Chromium build remain green.
- [ ] **Schema + model PR (Phase 1).**
  - New typed `Boost`, `ColorBoost`, `TypographyBoost`, `TextCase` in `maho-types`.
  - New `boosts` and `domain_boost_state` schema in `maho-storage`.
  - Rewritten `boost_manager.rs` with the API listed in Phase 1.
  - Unit tests: schema round-trip, active-selection invariants, `compose_css` goldens.
- [ ] **Rendering-hook feasibility spike PR (Phase 1.5) — MANDATORY GATE.**
  - Code probes against `chromium/src/` and `maho-chromium/` for blink / compositor / renderer-process / content-shell hooks listed in Section 8 Phase 1.5.
  - Per-candidate written verdict: feasible / infeasible, plus cost / fork-burden / perf / security note.
  - Runtime-contract recommendation (closest-to-Zen feasible hook, or CSS-only fallback with evidence).
  - **In the same commit:** amend Sections 4 (item 8), 8 (Phase 2), and 9 (items 1, 2, 10) to match the verdict.
  - Block Phase 2 PR until this PR lands.
- [ ] **Runtime PR (Phase 2) — strategy decided by Phase 1.5.**
  - `maho-ffi` `get_active_for_domain` + change channel exposing `ComposedRuntimePayload`.
  - Slimmed `maho_boost_injection_handler.cc` (or the deeper-hook entry point selected by Phase 1.5) implementing the chosen strategy only.
  - Manual verification matrix on the five cases listed in Phase 2, plus the rendered-output baseline vs Zen.
- [ ] **Mojom + window content host PR (Phase 3).**
  - New `maho_boost.mojom` (domain-scoped + observer).
  - New WebUI content host wiring **inside** the separate Boost editor window (subordinate content, not a navigable `chrome://` page).
  - Regenerated TS bindings; minimal new `store.ts`.
- [ ] **Separate Boost editor window UI PR (Phase 4).**
  - Implement the editor inside the Section 7.2 separate-window shell, matching Zen's separate-window interaction model.
  - Wire live preview, multi-boost list, active selection, delete.
  - Confirm cross-tab live update behavior matches the decision in Open Question 5.
  - Confirm window is freely positionable, has native chrome, does not auto-dismiss on focus loss, and is not anchored to the toolbar.
- [ ] **Polish + regression PR (Phase 5).**
  - Appium regression coverage covering each registered editor entry point.
  - Final sweep for stale references in code / GN / AGENTS.md, including any lingering "panel" / "bubble" terminology.
  - Confirm no path leads back to a manager-page interpretation of Boost and no `BubbleDialogDelegateView`-shaped editor remains.

This plan is the canonical source of truth for Boost work going forward. Earlier plans treating Boost as a `chrome://maho-boost` manager / templates / JS-injection feature are explicitly retired by this document.
