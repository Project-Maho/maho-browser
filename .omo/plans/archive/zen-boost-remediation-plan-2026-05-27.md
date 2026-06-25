# Zen Boost Remediation Plan (Gap Closure)

**Created:** 2026-05-27
**Status:** Companion to the canonical reset plan. **Does not supersede** `zen-boost-source-level-parity-reset-plan-2026-05-26.md`. **Does not resurrect** `zen-boost-rust-first-parity-implementation-plan-2026-05-25.md`.
**Role:** Execution source-of-truth for closing the currently confirmed parity gaps that block forward progress on the reset plan's Phase 1 / Phase 1.5 entry.
**Scope key:** Only gaps that are grounded in the on-disk state of the working tree as of 2026-05-27 are listed here. Speculation, future-phase polish, and items already covered cleanly by the reset plan's Phase 1 / Phase 1.5 / Phase 2 contract are out of scope.

---

## 1. Why this plan exists

The canonical reset plan defines the destination contract (domain-keyed, typed-knob Boost; separate-window editor; Phase 1.5 rendering-hook spike before any runtime is locked). It assumes Commit A landed cleanly and Commit B (schema and contract reset) is the next atomic step.

The working tree as of 2026-05-27 does **not** match the reset plan's post-Commit-B slab. Several surfaces that were supposed to be either gone, cold, or frozen at the new contract are still alive at the old contract or alive at a partial intermediate contract. Closing those gaps is a precondition for Phase 1 work, not part of it.

This document enumerates exactly those gaps, classifies each one, sequences the fixes, and defines the acceptance criteria that signal "ready to start Phase 1."

The grounding facts it relies on are:

1. The Rust dirty-tree Boost contract currently centers on `domain` + `custom_css` + `enabled`. That is neither the pre-reset shape (URL-pattern + JSON blobs + `custom_js`) nor the post-Commit-B shape (domain + typed knobs + per-domain active row). It is an intermediate shape that Commit B has not finished collapsing into the parity contract.
2. Live Chromium legacy Boost surfaces still exist:
   - the sidebar Boost surface,
   - a Boost bubble whose contents embed `chrome://maho-boost`,
   - a page handler / store / bridge that still consumes the old schema shape.
   These surfaces were supposed to be hard-deleted (bubble) or reduced to cold no-ops (sidebar, page handler / store / bridge) by Commit A. The repo state shows they are still live consumers, not cold stubs.
3. The `_in_space` path is live but ignores the space argument. This is a correctness / parity bug, not cosmetic dead code. Per-space Boost scoping reads as supported at the API surface and is silently disregarded by the implementation.
4. The macOS Boost editor and marketplace controllers compile, but only the known app entrypoints have been deactivated. The controllers themselves remain in the build as orphaned code paths. This is dead-code hygiene, not a parity gate.

Everything below is derived from those four facts plus the constraints of the canonical reset plan.

---

## 2. Relationship to existing plans

- **Canonical reset plan** (`zen-boost-source-level-parity-reset-plan-2026-05-26.md`): This remediation plan is **subordinate** to it. Every classification, sequencing rule, and acceptance criterion here defers to the reset plan's Sections 4 (hard constraints), 6 (salvage vs discard), 7 (legacy treatment), and 8 (phase sequencing). If a conflict appears, the reset plan wins and this plan is amended in the same commit that introduces the conflict.
- **Earlier rust-first implementation plan** (`zen-boost-rust-first-parity-implementation-plan-2026-05-25.md`): historical context only. Items it advocated that conflict with the reset plan (custom_js as first-class, URL-pattern keying, manager-page surface, `BoostEntry`-shaped Mojom, template manager surface) are **not** revived here under any framing. Where this plan references it, it is for traceability of why the current dirty tree looks the way it does, not for direction.

Positioning: this plan sits between Commit A (shipped) and the reset plan's Phase 1. Its job is to make the slab actually look like a post-Commit-B slab so Phase 1 can begin.

---

## 3. Hard constraints inherited from the reset plan

Repeated here only so this plan cannot accidentally relax them:

1. Scope key is `domain`. URL patterns are out.
2. Knobs are typed scalar fields. JSON blob columns for color / font are out.
3. One active boost per domain, persisted on a `domain_boost_state` row. `enabled` per rule is out.
4. No per-site JS injection. `custom_js` is out at every layer.
5. The separate floating Boost editor window is the architectural owner of the editor surface. Toolbar bubble, sidebar editor, and `chrome://maho-boost` are not.
6. No coexistence of old and new Boost UX behind any flag.
7. Runtime layer (CSS-only vs deeper-than-CSS) is decided by Phase 1.5 of the reset plan. This remediation plan does **not** lock a runtime strategy.

Any remediation step that would relax one of these constraints is rejected on sight.

---

## 4. Confirmed gap inventory

Each gap is tagged with one classification:

- **shipped-blocker** — inconsistent with the slab the reset plan declared shipped; blocks Phase 1.
- **compatibility** — present and live at an intermediate contract; not strictly false advertising vs. the slab, but blocks Phase 1 because it leaks the old contract into reachable code.
- **dead-code** — orphaned compile-only artifact with no reachable callers; parallel hygiene, not a phase gate.

### 4.1 Rust contract is at an intermediate shape

**Tag:** shipped-blocker.

The dirty Rust tree carries a Boost contract keyed on `domain` with `custom_css` and `enabled` as live fields. That shape is:

- Not the pre-reset shape: URL pattern is gone, JSON blobs are gone, `custom_js` is gone. So Commit A's intent has partially landed beyond what Section 7.1 of the reset plan claims.
- Not the post-Commit-B shape: typed color / typography knobs are missing, the `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)` table is missing, and `enabled` per rule is still the active-selection mechanism instead of a per-domain `active_boost_id`.

This is the central blocker. As long as the Rust contract is in this state:

- FFI cannot be reset to the Phase 1 surface.
- `boost_manager.rs` cannot be filled in to its Phase 1 API (`set_active`, `get_active`, `compose_css` over typed knobs).
- The Mojom IDL cannot be reshaped to the parity contract because the Rust source-of-truth it should reflect does not yet exist.
- Any Chromium-side consumer that reads `enabled` is reading a field that the parity contract does not define.

This gap must close before Phase 1.5 can be entered, because Phase 1.5 operates on a Phase 1 schema that does not yet exist.

### 4.2 Live Chromium consumers of the old / intermediate schema

**Tag:** shipped-blocker.

Three surfaces are live consumers, not cold stubs, of a schema that no longer matches the parity contract:

- **Sidebar Boost surface** — still wired to read Boost state for the active site and display editable controls, against the intermediate contract. The reset plan (Section 7.2) requires this to be a placeholder view that returns empty models and renders nothing.
- **Boost bubble embedding `chrome://maho-boost`** — the bubble itself is supposed to be hard-deleted in Commit A (Section 7.1). A bubble that hosts the deleted manager via an iframe / content host is doubly off-contract: it both restores the bubble shell and re-exposes the navigable manager, even if only inside the bubble. Whether the embedding uses the navigable host directly or a moral equivalent inside the bubble's content frame, both paths violate the no-coexistence rule (Section 7.5).
- **Page handler / store / bridge old-schema consumers** — the WebUI page handler, the frontend store, and the C++/TS bridge are still wired to the pre-reset SiteRule / `BoostEntry` shape and still flow data end-to-end. Per the reset plan, the page handler files survive only as cold compile-only stubs that no reachable code path constructs, and `store.ts` was supposed to be removed in the resources-shell reduction.

These three surfaces, taken together, mean Boost UX is currently **reachable** in the build at a non-parity contract. That violates the slab Commit A is supposed to have established and it violates Section 7.5 (no-coexistence) of the reset plan.

### 4.3 `_in_space` path is live but ignores space

**Tag:** shipped-blocker (correctness / parity, not cleanup).

A `_in_space` Boost path is wired through the API surface and reachable from callers, but its implementation does not honor the space argument. Boosts are returned, applied, or stored without space-scoping even when the caller passes a space.

Why this is a shipped-blocker, not cleanup:

- The reset plan's parity contract is per-domain, not per-domain-per-space. A live `_in_space` API that pretends to scope by space and silently doesn't is **worse** than no API at all, because every caller that uses it is implicitly making a parity decision (per-space scoping) that the contract does not back.
- Either spaces are part of the parity scope (in which case the reset plan must be amended in the same commit as the implementation) or they are not (in which case the `_in_space` path must be removed, not left lying as a no-op).
- Leaving the path live and broken means Phase 1's schema discussion has to relitigate whether `space_scope` survives, even though the reset plan already dropped it. That relitigation cost is exactly the contract drift the reset plan is built to prevent.

This is not a polish item. It must be resolved one way or the other before Phase 1.

### 4.4 macOS Boost editor / marketplace controllers compile but unreachable

**Tag:** dead-code (parallel hygiene).

The macOS shell's Boost editor controller and marketplace controller still compile. The known app-side entrypoints that would invoke them are deactivated, so they are unreachable user-side, but the source files and their build wiring remain.

This is not a parity gate:

- The reset plan does not require the macOS shell to host the new editor. Section 4 item 5 specifies the separate Boost editor window as a top-level Chromium `views::Widget`, not an AppKit window. The macOS controllers are by construction not the parity surface.
- They do not consume the old Rust contract at runtime, because nothing reaches them. They can drift in the source tree without leaking the old contract into reachable code.
- Their existence does, however, create future-debt: every Rust contract reshape risks touching them, every cross-platform refactor risks pulling them back into reachable wiring, and any future engineer can mistake them for "the macOS Boost editor we are keeping."

So they belong on the cleanup track, not the gate track. Hygiene work, run in parallel with the gate work above, on its own commit boundary.

---

## 5. Phase 0a — Per-surface parity verdict gate (mandatory)

**Inserted between Commit A (shipped) and Commit B (next) as defined by the reset plan's Section 8 / Section 7.4.**

Phase 0a is a **gate**, not a code-shipping phase. It exists because Oracle's critique of the prior remediation thinking was correct: jumping straight to excision / realignment without first issuing a per-surface verdict bakes in whatever the current dirty tree happens to look like. The reset plan's Section 4 item 8 already required a Phase 1.5 spike for runtime fidelity; this phase applies the same discipline one layer up, to the legacy surface inventory.

### 5.1 Inputs

- This document's Section 4 (gap inventory).
- The reset plan's Sections 4, 6, 7, 8 (the contract being verified against).
- The on-disk working tree as of the gate's start commit.

### 5.2 Per-surface verdicts (output)

For each surface listed below, Phase 0a produces one written verdict tagged **excise**, **realign**, or **defer**:

- Rust contract layer (`maho-types::Boost`, `BoostUpdate`, `maho-storage` schema, `boost_manager.rs`, `maho-ffi` Boost surface).
- Sidebar Boost surface.
- Boost bubble + its embedded `chrome://maho-boost` content.
- Page handler / store / bridge old-schema consumers.
- `_in_space` path.
- macOS Boost editor / marketplace controllers.
- Mojom IDL (`maho_boost.mojom`) and any generated TS bindings still in the build.
- WebUI resources shell under `browser/resources/maho_boost/`.

Verdict semantics:

- **excise** — the surface is incompatible with the parity contract and has no future role in it. Plan removal in Phase 0b.
- **realign** — the surface has a future role under the parity contract, but its current shape is wrong. Plan reshape in the appropriate later phase (typically Phase 1, Phase 3, or Phase 4 of the reset plan).
- **defer** — the surface is parallel hygiene; it does not block Phase 1, and its cleanup commit can land independently of the gate sequence.

Each verdict carries:

- The grounding evidence on disk (file paths, function names, callers).
- The reset-plan section it is verified against.
- The atomic commit it should land in (or "out of band, parallel hygiene" for **defer**).

### 5.3 Hard rule

Phase 0b (excision and realignment) **may not begin** until every surface in 5.2 has a written verdict in this document or in a successor document referenced from here. "We'll just start cleaning up and decide later" is rejected on the same grounds the reset plan rejects "let's just do CSS for now."

A verdict may be revised, but it must be revised **explicitly** (via an amendment commit) before the affected surface is touched. Implicit drift of a verdict during implementation is treated as a contract violation.

### 5.4 Confirmed verdicts (Phase 0a completed)

Phase 0a is complete. The following verdicts are confirmed based on on-disk source-level analysis:

- **Rust contract layer** → **realign**.
  - *Evidence:* `maho/crates/maho-types/src/boost.rs` and `maho/crates/maho-storage/src/sqlite.rs` schema are updated to domain-keyed typed-knobs. `maho-core`'s `boost_manager.rs` and FFI layer still expose `_in_space` query paths.
  - *Target:* Remove `_in_space` from FFI/Rust, ensuring clean domain-only contract. Lands in 6.1 (Commit B).
- **Sidebar Boost surface** → **realign**.
  - *Evidence:* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.cc/h` and `maho_sidebar_boosts_data.cc/h` exist on disk. `ParseBoosts()` parses the obsolete `urlPattern`.
  - *Target:* Realign `ParseBoosts()` to return an empty list. The UI will naturally render the empty state view `BoostEmptyStateView` with no editor surface. Lands in 6.2.
- **Boost bubble + embedded `chrome://maho-boost`** → **excise**.
  - *Evidence:* `maho-chromium/browser/ui/views/boost/maho_boost_bubble_coordinator.cc/h` and `maho_boost_bubble_view.cc/h` exist on disk.
  - *Target:* Delete these files completely. Introduce minimal `maho_boost_window_view.{cc,h}` and `maho_boost_window_controller.{cc,h}`. Empty GN groups `maho_boost_bubble` and `interactive_tests` added for graph compatibility. Lands in 6.2.
- **Page handler / store / bridge old-schema consumers** → **realign** page handler, **excise** React frontend.
  - *Evidence:* `maho_boost_page_handler.cc/h` and `maho_boost_ui.cc/h` exist on disk and reference the old schema. `store.ts` implements old manager-page store.
  - *Target:* Page handler reduced to cold stub matching the minimal Mojom stub. Remove `store.ts` and all React components in `maho-chromium/browser/resources/maho_boost/`. Lands in 6.2.
- **`_in_space` path** → **excise**.
  - *Evidence:* `maho_core_get_boosts_for_url_in_space` in `maho-ffi/src/lib.rs`, `boost_manager.rs`, and `macos-shell` Swift bridge.
  - *Target:* Delete FFI declaration, Rust method signatures, and Swift bridge implementations. Lands in 6.3.
- **macOS Boost editor / marketplace controllers** → **excise**.
  - *Evidence:* `BoostEditorController.swift` and `BoostMarketplaceController.swift` are located in `macos-shell/Sources/Boost`.
  - *Target:* Delete these files and remove references from the Xcode project file `Maho.xcodeproj/project.pbxproj`. Lands in 6.4 (parallel hygiene).
- **Mojom IDL** → **realign**.
  - *Evidence:* `maho_boost.mojom` defines obsolete `SiteRule` struct.
  - *Target:* Realign to a minimal stub declaring only `PageHandlerFactory`, `PageHandler::GetState()`, and `Page::OnStateChanged()`. Lands in 6.2.
- **WebUI resources shell** → **realign**.
  - *Evidence:* React/store assets in `browser/resources/maho_boost`.
  - *Target:* Realign to a 4-file placeholder shell (`BUILD.gn`, `app.ts`, `boost.html`, `boost.css`). Lands in 6.2.

---

## 6. Phase 0b — Excision and realignment (after Phase 0a verdicts land)

Phase 0b implements the verdicts from Phase 0a. Each step below is a candidate atomic commit; final commit boundaries are confirmed by the Phase 0a output.

### 6.1 Order of operations (Oracle-corrected)

The earlier (rejected) instinct was to start with the live Chromium surfaces and let the Rust contract follow. That order is wrong because every Chromium surface that gets reshaped while the Rust contract is still at the intermediate `domain/custom_css/enabled` shape risks being reshaped against the wrong target and re-reshaped in a few commits.

The correct order, applied here:

1. **Rust contract freeze first.** Land Commit B of the reset plan (Section 8 / Section 7.4): collapse `maho-types::Boost` / `BoostUpdate` / `maho-storage` schema / `maho-ffi` / `boost_manager.rs` skeleton onto the post-Commit-B shape. This is the contract every other surface in 6.2 will be reshaped against.
2. **Per-surface parity verdict gate** (Phase 0a, Section 5 of this doc). Already done by the time we are in 6.1, but listed here so the order is explicit.
3. **Excision and realignment of live Chromium surfaces** (Section 6.2 of this doc).
4. **`_in_space` resolution** (Section 6.3 of this doc).
5. **macOS dead-code cleanup** (Section 6.4 of this doc), in parallel.

Steps 1, 3, 4 are sequential. Step 5 runs alongside on its own commit.

### 6.2 Live Chromium surface fixes

For each surface marked **realign** by Phase 0a, reduce it to its slab-defined cold form (Section 7.2 of the reset plan): no reachable callers construct it, its body returns empty / no-op, no flag re-enables it.

For each surface marked **excise** by Phase 0a, delete the source files and the build wiring that includes them. If GN target shells must remain for build-graph stability, they are empty `group()` declarations scheduled for removal in a follow-up commit.

Specifically, this remediation plan expects (subject to Phase 0a confirmation):

- Sidebar Boost surface reduced to placeholder state per Section 7.2.
- Boost bubble files deleted; the path that embedded `chrome://maho-boost` inside a bubble removed end-to-end. No replacement bubble is introduced. The Phase 4 separate Boost editor window remains the only future editor entry point.
- Page handler files reduced to cold stubs (the IDL stub of Section 7.2 is the only contract they bind to, and the Mojo binder is not registered against any reachable host). `store.ts` and any TS that assumed `BoostEntry` / `SiteRule` / manager-page lifecycle deleted.
- The constants `kMahoBoostHost` / `kMahoBoostURL` may remain as symbol-level constants, but no path resolves either to a controller after this commit.

After 6.2 lands, the slab matches the reset plan's Section 7.1 description of "Commit A as shipped" properly, for the first time.

### 6.3 `_in_space` resolution

`_in_space` is a correctness / parity bug. It must be either removed or made correct, in the same commit, with a written rationale that ties to the reset plan.

The default action under this remediation plan is removal:

- Drop the `_in_space` path from the API surface across `maho-types`, `maho-core`, `maho-ffi`, and any C++ caller.
- Remove all callers that pass a space argument to a Boost lookup.
- Confirm no test exercises the removed path. If a test exists, delete it; do not preserve it under a TODO.

If Phase 0a returns a non-default verdict that wants per-space Boost preserved, the resolution is instead:

- Implement actual per-space scoping at the schema and runtime layers, with the same atomic commit amending Section 4 of the reset plan to add space scoping to the parity contract.
- Add tests that exercise the scoping.
- This branch is the more invasive one and is **not** assumed by this remediation plan.

Either way, the path does not stay live and broken past 6.3.

### 6.4 macOS dead-code cleanup

Run in parallel with 6.1–6.3 on its own commit boundary. Not a gate.

- Delete the macOS Boost editor controller files, the marketplace controller files, and any AppKit wiring whose only purpose is invoking them.
- Audit for any project-file (Xcode / Tuist / Bazel) entry that lists the deleted files; remove those entries in the same commit.
- Confirm the macOS shell still builds and passes tests.

The macOS shell is not the architectural owner of the Boost editor (Section 4 item 5 of the reset plan). Removing these controllers does not remove anything from the parity surface; it removes orphaned code that compiles only because nothing has cleaned it up.

---

## 7. Dependencies

External-to-this-plan dependencies:

- **Reset plan, Section 7.4 (atomic commit sequencing).** Defines Commit A (shipped) and Commit B (next). Phase 0a / 0b of this remediation plan implement and verify Commit B's slab.
- **Reset plan, Section 8 Phase 1.** Cannot start until 6.1 (Rust contract freeze) lands, which is to say until the Section 4 contract types exist and `boost_manager.rs` carries the Phase 1 API skeleton.
- **Reset plan, Section 8 Phase 1.5.** Cannot start until Phase 1 is complete (Phase 1.5 operates on Phase 1's typed schema). This remediation plan does **not** front-load any rendering-hook spike work.

Internal dependencies:

- 6.2 depends on 6.1: Chromium surface reshapes target the post-Commit-B Rust contract, not the intermediate `domain/custom_css/enabled` one.
- 6.3 depends on 6.1: removing `_in_space` cleanly requires the surrounding Rust API to already be at the parity shape, otherwise the removal touches the same files the contract reshape touches and the diffs entangle.
- 6.4 depends on nothing in this plan and can land any time.

---

## 8. Acceptance criteria

The slab is "ready for Phase 1" exactly when **all** of the following hold:

1. **Rust contract matches Section 4 of the reset plan.** `maho-types::Boost` / `BoostUpdate` carry only the typed knobs, the domain key, and the field set the reset plan names. `maho-storage` has one column per typed knob, plus the `domain_boost_state(domain PRIMARY KEY, active_boost_id NULLABLE FK)` table. The `enabled` column on `boosts` is gone. The `custom_js` column is gone. URL-pattern columns and indexes are gone. JSON-blob columns for color / font are gone.
2. **`boost_manager.rs` exposes the Phase 1 API skeleton.** Method signatures match Section 8 Phase 1 of the reset plan (`list_for_domain`, `get`, `create`, `update`, `delete`, `set_active`, `get_active`, `compose_css`). Bodies may be `todo!()`; this is the slab, not Phase 1's behavior fill.
3. **`maho-ffi` is at the new contract shape.** Signatures only; bodies may be `todo!()`.
4. **No live Chromium surface consumes the old or intermediate schema.** Sidebar is a placeholder. The bubble is gone; nothing embeds `chrome://maho-boost` inside another surface. The page handler / bridge files are cold; no reachable code path constructs them. `store.ts` and any `BoostEntry` / `SiteRule` TS shape are gone.
5. **`_in_space` is resolved.** Either fully removed (default) or fully implemented with the reset plan amended in the same commit. No live-but-ignored variant remains.
6. **macOS Boost editor / marketplace controller files are deleted.**
7. **No coexistence path exists.** No flag, GN arg, or branch re-enables the manager UX or the bubble UX. `chrome://maho-boost` does not return as a navigable host.
8. **Build is green end-to-end.** `python3 maho-chromium/build/scripts/build_maho.py` succeeds. `cd maho && bun run typecheck` and `bun run test` succeed. `cd mail && bun run test && cargo test` succeed (Boost remediation should not regress mail). `cd website && bun run build` succeeds (no expected impact, but verified).
9. **No new old-shape unit tests exist.** Any test that asserts URL-pattern matching, JSON-blob round-trips, `custom_js` injection, `enabled`-per-rule semantics, or `_in_space` correctness is gone.
10. **Phase 0a verdicts are written down.** Section 5.4 of this document is updated with the actual verdicts from the gate, with on-disk evidence per surface, before any 6.x commit lands.

If any one of (1)–(10) fails, Phase 1 of the reset plan does not start.

---

## 9. Surface classification (single-table view)

Restated as a flat reference. Authoritative tags come from Phase 0a; defaults below match Section 4 / Section 5.4.

| Surface | Default tag | Default verdict | Lands in |
|---|---|---|---|
| Rust `maho-types::Boost` / `BoostUpdate` shape | shipped-blocker | realign | 6.1 (= reset plan Commit B) |
| `maho-storage` boosts schema | shipped-blocker | realign | 6.1 |
| `maho-storage` `boosts.custom_js` column | shipped-blocker | excise | 6.1 |
| `maho-storage` `idx_boosts_url_pattern` index | shipped-blocker | excise | 6.1 |
| `boost_manager.rs` API surface | shipped-blocker | realign | 6.1 |
| `maho-ffi` Boost shims | shipped-blocker | realign | 6.1 |
| Sidebar Boost surface | shipped-blocker | realign (placeholder) | 6.2 |
| Boost bubble files | shipped-blocker | excise | 6.2 |
| Bubble-embedded `chrome://maho-boost` content path | shipped-blocker | excise | 6.2 |
| WebUI page handler files | shipped-blocker | realign (cold stub) | 6.2 |
| `store.ts` / `BoostEntry` / `SiteRule` TS shapes | shipped-blocker | excise | 6.2 |
| C++/TS bridge old-schema consumers | shipped-blocker | excise (or realign to cold stub) | 6.2 |
| `maho_boost.mojom` IDL | compatibility | realign (minimal stub) | 6.2 |
| `browser/resources/maho_boost/` shell | compatibility | realign (4-file placeholder) | 6.2 |
| `_in_space` path | shipped-blocker | excise (default) | 6.3 |
| macOS Boost editor controller | dead-code | excise | 6.4 (parallel) |
| macOS Boost marketplace controller | dead-code | excise | 6.4 (parallel) |

---

## 10. Risks

### 10.1 Phase 0a gets skipped under schedule pressure

The most likely failure mode. Skipping the gate means surfaces get reshaped against whatever the dirty tree happened to look like that afternoon, and the contract drift the reset plan is built to prevent reappears at a finer granularity.

Mitigation: Phase 0a's hard rule (Section 5.3) is non-negotiable. Code review rejects any 6.x commit that lands without a written verdict for the surface it touches.

### 10.2 The Rust contract reshape (6.1) churns every Boost-touching file

The reset plan's Section 7.4 already calls this out as the reason Commits A and B were not squashed. The concern is that 6.1 still produces a wide diff and gets reviewed superficially.

Mitigation: 6.1's diff is bounded to the Rust workspace plus the cold stubs in Chromium that need to track the new contract types. Any drift into Chromium UI surfaces in the same commit is rejected and split out into 6.2.

### 10.3 `_in_space` removal breaks a caller no one expected

The path is live, so callers exist. Removing it without a complete caller audit risks a runtime regression in a non-Boost area that happened to use the per-space lookup as a side door.

Mitigation: 6.3's caller audit is exhaustive (`grep` / LSP references on every renamed or removed symbol). Any non-Boost caller that relied on the path is a bug to be fixed at the caller site, not a reason to keep the path.

### 10.4 macOS cleanup pulls in app-side code that thought the controllers were live

If the deactivation of the known app entrypoints missed one, deleting the controllers will surface it. Possible, but small in scope.

Mitigation: 6.4 is its own commit. If a missed entrypoint surfaces, it gets deactivated in the same commit as the controller deletion, with a brief note. No new controller is introduced.

### 10.5 The bubble-embedding excision breaks a flow we did not realize was Boost UX

The bubble that embeds `chrome://maho-boost` may be reachable from a non-obvious entry point (right-click menu, command palette, keyboard shortcut). Removing it risks a small UX hole.

Mitigation: 6.2's caller audit is exhaustive across command / context-menu / location-bar / site-control integrations. Any entry point that opened the bubble is reduced to the same `// TODO(maho-boost-reset): open Phase 4 separate Boost editor window` no-op the rest of the entry points already use (Section 7.3 of reset plan).

### 10.6 Phase 0a expands the gap inventory mid-flight

Verifying each surface against the reset plan may surface a gap not currently listed in Section 4. That is a feature, not a bug.

Mitigation: Phase 0a updates Section 4 of this document in the same commit it issues the new verdict. The gate does not move forward with an undocumented surface.

---

## 11. Explicit next actions

In order. Each is a concrete commit-level action.

1. **Run Phase 0a.** Produce a written verdict for every surface in Section 5.2, on disk, with evidence. Update Section 4 of this document if the gate surfaces a gap not currently listed. Update Section 5.4 with the actual verdicts. This is a documentation commit, no code changes.
2. **Land 6.1 (Rust contract freeze = reset plan Commit B).** `maho-types`, `maho-storage`, `maho-ffi`, `boost_manager.rs` skeleton match the reset plan's Section 4 / Section 8 Phase 1 contract. Old-shape unit tests removed. `cargo check` / `bun run typecheck` / `bun run test` green.
3. **Land 6.2 (live Chromium surface excision and realignment).** Sidebar reduced to placeholder. Bubble + bubble-embedded manager removed. Page handler / bridge cold-stubbed. `store.ts` and old-shape TS removed. `chrome://maho-boost` remains unreachable. Full Chromium build green.
4. **Land 6.3 (`_in_space` resolution).** Default branch: removal across Rust + C++. Non-default branch (only if Phase 0a chose it): full implementation with reset-plan amendment in the same commit. Tests for the path either deleted or written from scratch.
5. **Land 6.4 (macOS dead-code cleanup).** In parallel with 6.1–6.3, on its own commit. macOS controllers and project entries gone, macOS shell builds and tests green.
6. **Verify all Section 8 acceptance criteria.** If any of (1)–(10) fails, fix it before declaring the slab Phase 1-ready.
7. **Hand off to reset plan Phase 1.** This remediation plan is closed once Section 8 acceptance is signed off; Phase 1 of the reset plan begins on the slab the remediation produced.

No commit is squashed across the boundaries above. `git bisect` must be able to land cleanly on each step.

---

## 12. What this plan does **not** do

For clarity, listing the items adjacent to the gap inventory that this plan deliberately does not cover:

- It does not start Phase 1 schema work beyond the API skeleton needed to declare the slab ready.
- It does not start Phase 1.5 rendering-hook feasibility spike work.
- It does not relitigate Section 4 of the reset plan. Knob naming, scope key, runtime gate, separate-window architecture are all inherited as-is.
- It does not introduce new product surfaces. No new bubble, no new sidebar editor, no new manager.
- It does not commit to per-space Boost. If `_in_space` survives Phase 0a, the surviving form is part of the reset plan's amended contract, not a feature this plan introduces.
- It does not make commits. Every "lands in" / "next action" entry above is a planned commit, not a recorded one.

---

## 13. Signoff conditions

This plan is itself "done" when:

- Phase 0a has been run and Section 4 / Section 5.4 reflect its actual output.
- All seven actions in Section 11 have landed.
- All ten acceptance criteria in Section 8 are met.
- The reset plan's Phase 1 has been entered against the slab this remediation produced.

Until all four hold, this remediation plan stays the active execution source-of-truth.
