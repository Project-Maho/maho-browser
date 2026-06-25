# Zen Boost Parity — G1/G2 Reference Lock

**Date:** 2026-05-27
**Scope:** This document freezes the G1 (remote reference) and G2 (Maho-side architectural ownership) facts for the active Zen Boost parity push. It is a reference lock, not a roadmap. It exists so the parent session and the fan-out streams can stop re-litigating shape questions and move to implementation against a stable contract.

**Derived from:**
- `.sisyphus/plans/zen-boost-source-level-parity-reset-plan-2026-05-26.md`
- `.sisyphus/plans/zen-boost-post-reset-parallel-parity-plan-2026-05-27.md`

This file does not modify either of those plans. It just pins down what they already established so subsequent streams operate against a single shared snapshot.

---

## G1 — Remote Zen Reference (Locked)

### Pinned commit

The Zen reference for this push is frozen at:

```
5bbbadd77be956deaf91633cd4f0611403b0ba53
```

Every reference comparison, port, or shape decision in this push reads from that commit. If a stream encounters drift versus a newer Zen state, it must flag it as a separate finding rather than silently re-baselining.

### Reconciliation against older Zen state

Runtime semantics at the pinned commit were cross-checked against the older Zen commit:

```
b94dd3b927b0419c47eb3b41a92dfc8a0b28ce19
```

`5bbbadd` is forward-compatible with `b94dd3b` for the parity surface in this push. `b94dd3b` is recorded here only as a verified second data point for runtime-behavior archaeology. It is not the reference and must not be cited as the contract; the contract is `5bbbadd`.

Two correctness fixes are present at `5bbbadd` and are part of parity. They must not be dropped when porting to the Maho side, and an implementation that mirrors only the older `b94dd3b` runtime behavior is incomplete:

- **Color-filter coverage for form controls / pseudo-elements.** The color-boost runtime backend at `5bbbadd` carries refined exemption logic so that form controls and pseudo-element-rendered content are covered correctly. Parity is the post-fix behavior, not the pre-fix behavior.
- **SVG image-document propagation.** At `5bbbadd`, boost state is propagated into SVG image documents through the `nsPresContext` override path. Parity requires this propagation; an implementation that only handles top-level HTML documents does not satisfy G1.

### Additive launch-site fact

At `5bbbadd`, `ZenUBGlobalActions.sys.mjs` contributes an additive `New Boost` browser-chrome launch action. This is recorded here as a launch-site inventory fact for Stream D only. It does not expand the locked parity surface, does not change the Boost data shape in this section, and does not change the editor ownership contract in G2. Treat it as one more entry point that must converge on the G2 launch API, not as a separate ownership story.

### Verified Zen file map

These are the remote paths that anchor the parity surface. They are confirmed to exist at the pinned commit and are the authoritative reference points for shape, naming, and behavior:

- `src/zen/boosts/ZenBoostsManager.sys.mjs`
- `src/zen/boosts/ZenBoostsEditor.mjs`
- `src/zen/boosts/ZenBoostStyles.sys.mjs`
- `src/zen/boosts/zen-boost-editor.inc.xhtml`
- `src/zen/boosts/actors/ZenBoostsChild.sys.mjs`
- `src/zen/boosts/actors/ZenBoostsParent.sys.mjs`
- `src/zen/boosts/nsZenBoostsBackend.h` / `nsZenBoostsBackend.cpp`
- `src/zen/urlbar/ZenSiteDataPanel.sys.mjs`
- `src/zen/common/sys/ZenActorsManager.sys.mjs`

Anything outside this set is not part of the locked Boost parity surface for this push and should not be cited as ground truth without an explicit additional lookup.

### Frozen behavioral facts

These are not interpretations. These are observed facts at the pinned commit, and the streams must treat them as immovable:

- A separate floating Boost editor window is real in Zen. It is opened via `Services.ww.openWindow(...)` as a `Zen:BoostEditor` window, not embedded in a bubble or sidebar.
- One active boost per domain is real. Each domain entry tracks an `activeBoostId`.
- Multiple boosts per domain are real. The active one is selected from a stored set, not the only one.
- Create and edit flows originate from browser chrome (URL bar / site data panel surface), not from page content. The chrome surface is what triggers opening the editor window.
- No per-site JavaScript injection is part of the Zen Boost parity surface. Runtime expression is CSS plus the C++ color/layout backend (`nsZenBoostsBackend`). Streams must not introduce content-script-style JS injection under the banner of "matching Zen."

### Frozen Boost data shape

The locked set of knob and data fields, as expressed by Zen at the pinned commit. This is the parity contract for any Maho-side data model exposed in this push:

- `boostName`
- `dotAngleDeg`
- `dotPos`
- `dotDistance`
- `secondaryDotAngleDegDelta`
- `secondaryDotPos`
- `brightness`
- `saturation`
- `contrast`
- `fontFamily`
- `textCaseOverride`
- `sizeOverride`
- `enableColorBoost`
- `smartInvert`
- `autoTheme`
- `zapSelectors`
- `customCSS`
- `changeWasMade`

Streams may shorten or normalize names at the Maho internal layer, but the editor surface and the persisted shape must round-trip these fields without loss.

---

## G2 — Maho-Side Architectural Ownership (Locked)

### Decision

For this push, the architectural owner of the Boost editor on the Maho side is a top-level native Boost editor window. That is final for this iteration.

Concretely, that decision excludes:

- No bubble-hosted Boost editor.
- No sidebar-embedded Boost editor.
- No navigable `chrome://maho-boost` parity surface intended as the user-facing editor.

Hosted web content, where used, is subordinate content technology inside the native window. It is not the architectural owner.

### Recommended hosting pattern

The recommended pattern for the push:

- A native `views::Widget` plus controller acts as the owner of the editor window. Lifecycle, focus, and integration with the rest of the browser chrome are driven from the C++ side.
- Inside that window, hosted subordinate web content renders the editor UI. The web layer is a rendering and input surface, not the source of truth for window identity or behavior.
- If a hosted web surface is used, `chrome-untrusted://` subordinate hosting is the preferred fresh-path option. This avoids reviving the older trusted manager host route and minimizes collisions with the cold `chrome://maho-boost` artifacts that already exist in the tree.

The point of the `chrome-untrusted://` preference is straightforward. The cold artifacts are a known hazard. Going through the untrusted subordinate path keeps the new editor's hosting story isolated from those artifacts and from the trusted-manager wiring, so Stream C can validate the new path without dragging old assumptions along.

### Controller-side API shape (frozen)

For the next implementation tranche, the Maho-side ownership model is frozen around the existing browser-keyed controller pattern. The ownership entry point is:

- `MahoBoostWindowController::GetForBrowser(Browser*)`

That call returns the per-`Browser` controller instance. All Boost editor lifecycle goes through that instance. Entry points must not construct the controller directly and must not bypass `GetForBrowser`.

The public launch API shape for this tranche is a non-anchored, native-window call. It is **not** a bubble-style anchored call. The current shell signature

```
Show(views::View* anchor_view, Browser*, content::WebContents*)
```

is transitional debt. It is **not** the target API shape. It must not be hardened, extended, or treated as the contract going forward.

The frozen target public method shape on the controller instance is:

- `ShowForActiveDomain(content::WebContents* active_tab)`
- `Hide()`
- `IsShowing()`

That is the minimal lifecycle surface for this tranche. Anything outside those three methods is out of scope for this freeze and must not be added under cover of "controller API work" without re-opening G2.

Explicitly forbidden in the public API for this tranche:

- Passing a bubble anchor view (e.g., `views::View* anchor_view`) as part of the public API. The editor is a top-level native window; it has no anchor.
- Passing a `chrome://maho-boost` URL — or any URL — as a navigation target to open the editor. URL navigation is not the launch mechanism.
- Any API shape that would make WebUI or URL navigation the architectural owner of the editor. Hosted web content remains subordinate; it is not the owner.

Browser chrome entry points (URL bar action, site data panel surface, the `New Boost` global action observed in `ZenUBGlobalActions.sys.mjs` at `5bbbadd`, and any future equivalent) must converge on a single launch path:

```
MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)
```

Each entry point calls that one shape. Entry points do not invent their own launch sequences, do not synthesize anchors, and do not navigate to a URL as a substitute for calling the controller.

---

## What Remains Intentionally Unfrozen

Some questions are deliberately not pinned in this document. Locking them now would either pre-empt evidence the streams are about to gather, or it would freeze a decision the parent session has explicitly held open. These remain open at the time of this lock:

- The Phase 1.5 rendering-hook verdict. The hooking strategy for runtime CSS / color / layout application is still subject to Phase 1.5 outcomes and is not pinned here.
- The exact runtime payload type naming at the Mojom boundary. Naming and field-level wire shape on the Mojom side stay open until G3. The data field set above is locked, but the Mojom-level type names and decomposition are not.
- Whether the cold `chrome://maho-boost` artifacts are reused internally or deleted. That decision waits on Stream C confirming the new hosting path. If Stream C lands cleanly on the native-widget-plus-`chrome-untrusted://` hosting path, the disposition of the cold artifacts can be settled then. Until then, they are neither blessed for reuse nor scheduled for deletion.

---

## Implementation Consequences for Streams A–F

A short, direct read-out of what this lock means for the fan-out streams:

- **Stream A (data shape and storage):** Build to the locked field list. Multiple boosts per domain with an `activeBoostId` is the storage shape. Do not collapse the data model to a single boost per domain.
- **Stream B (entry points / chrome triggers):** Create and edit flows originate from browser chrome (URL bar / site data panel equivalent). Hook here, not in page content. Do not introduce a content-script entry point.
- **Stream C (editor hosting):** Implement the editor as a native top-level `views::Widget` plus controller, with hosted subordinate web content inside. Use `chrome-untrusted://` for that hosted content as the preferred fresh path. Do not target a bubble, sidebar, or navigable `chrome://maho-boost` user surface. Stream C is also responsible for filling out the controller and window implementation **against the frozen API shape in G2** — `MahoBoostWindowController::GetForBrowser(Browser*)` as the entry point, with `ShowForActiveDomain(content::WebContents*)`, `Hide()`, and `IsShowing()` as the minimal lifecycle surface. The transitional `Show(views::View* anchor_view, Browser*, content::WebContents*)` signature must be retired, not extended. Stream C's landing is also the trigger for resolving the cold-artifact disposition.
- **Stream D (runtime application):** Treat runtime as CSS plus the C++ color/layout backend, mirroring `nsZenBoostsBackend` at `5bbbadd`. The two correctness fixes from G1 are in scope: form-control / pseudo-element exemption refinement in the color-boost path, and SVG image-document propagation via the `nsPresContext` override path. Do not add per-site JS injection to claim parity with Zen. Launch-site restoration (URL bar action, site data panel surface, the `New Boost` global action equivalent from `ZenUBGlobalActions.sys.mjs`) must target the G2 launch API exactly — `MahoBoostWindowController::GetForBrowser(browser).ShowForActiveDomain(active_tab)` — and must remain default-off until the final enable gate. The Phase 1.5 rendering-hook verdict still governs the exact hook points; until that verdict, do not foreclose options inside Stream D's local design.
- **Stream E (Mojom / boundary types and cold-artifact cleanup):** The data field set is locked; Mojom type naming and decomposition are not. Implement to the locked fields, but expect type-name churn until G3 closes that question. Cleanup of the cold `chrome://maho-boost` artifacts may proceed only after Stream C has confirmed the new hosting path on the frozen controller API. The new G2 API shape does **not** depend on those artifacts, so cold-artifact cleanup is not on the critical path for the API freeze and must not be used to justify slipping the API freeze.
- **Stream F (integration / window lifecycle):** Treat the editor window as a real top-level window owned natively. Lifecycle, focus, and chrome integration are C++ responsibilities. The web layer inside the window is subordinate and must not be allowed to drive window identity.

This lock does not declare G3. G3 remains open. What this document gives the streams is a stable G1/G2 floor so they can move in parallel without tripping over each other on shape and ownership questions that are already settled.
