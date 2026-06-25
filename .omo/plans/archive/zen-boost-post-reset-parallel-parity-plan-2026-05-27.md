# Zen Boost Post-Reset Parallel Parity Plan

**Created:** 2026-05-27
**Status:** Companion scheduling and execution layer that runs **after** the accepted cold/reset state. This plan **does not supersede** `zen-boost-source-level-parity-reset-plan-2026-05-26.md` and **does not replace** `zen-boost-remediation-plan-2026-05-27.md`. It assumes both are the canonical sources of truth and only adds the parallel sequencing on top of the slab they produced.
**Role:** Execution choreography for the next Zen Boost parity push, optimized for maximum safe parallelism from the current cold/reset state.
**Scope key:** Phase-level scheduling, ownership boundaries, merge order, and verification gates. No new product decisions are introduced here. Where this plan and the canonical reset plan disagree, the reset plan wins and this plan is amended in the same commit that introduces the conflict.

---

## 1. Purpose / why this plan exists

The reset plan defines **what** Zen Boost parity means at the source level. The remediation plan defines **what** had to change in the dirty tree to land the Commit A / Commit B slab cleanly. Both are necessary. Neither says **how** the next phase of work should be sliced across people and PRs to actually move forward without each track waiting on every other track.

That is the gap this document fills. It is purely a scheduling and ownership layer.

The push from here forward is large. It includes a full Rust runtime fill, a rendering hook spike, a Mojom redesign, a separate native window editor surface, the restoration of every Boost entry point that was reduced to a TODO no-op, and the final cleanup of the residual `chrome://maho-boost` debt that survived Commit A and Commit B for build-graph reasons. If those pieces are run strictly in series the calendar cost is unacceptable. If they are run naively in parallel they will collide on the same files, the same contracts, and the same verification gates, and the result will look like contract drift dressed up as throughput.

The job of this plan is to find the maximum **safe** parallelism. That means:

1. Serial gates first, where any earlier decision shapes every later track.
2. Independent streams second, where each stream owns a non-overlapping set of files, contracts, and surfaces.
3. A merge order that protects the user from re-encountering Boost UX before parity is real.
4. Verification gates that prove parity at every integration point, not just at the end.

Everything below is grounded in the on-disk state as of 2026-05-27 and in the two upstream documents listed at the top.

---

## 2. Current baseline (accepted cold/reset state)

This is the state this plan accepts as the starting line. It is the slab the reset and remediation plans together produced. **It is not Zen parity.** It is the cold, contract-clean substrate parity is built on top of.

What is true on disk right now, and treated by this plan as fact:

- The Rust contract has been collapsed onto the post-Commit-B shape. `maho-types::Boost` and `maho-types::BoostUpdate` are domain-keyed and typed-knob shaped. `maho-storage` carries the matching schema.
- The Rust persisted Boost FFI surface exists and is wired through to Chromium:
  - `maho_core_boost_list_for_domain`
  - `maho_core_boost_get`
  - `maho_core_boost_create`
  - `maho_core_boost_update`
  - `maho_core_boost_delete`
  - `maho_core_boost_set_active`
  - `maho_core_boost_get_active`
  - `maho_core_boost_compose_css`
- All previously live user-facing Boost surfaces are neutralized:
  - The runtime CSS injection path returns the empty string. No CSS is composed and no JS path exists at runtime.
  - The native-side `open_boost_editor` no longer navigates to `chrome://maho-boost`. The host is unreachable as a navigation target.
  - The sidebar Boost pane has been reduced to an empty-state view. It returns empty models and renders nothing editable.
  - The location-bar Boost quick action has been removed from the toolbar/location-bar surface.
  - Tab context-menu Boost commands have been removed from the context menu.
  - The page-info Boost bubble route has been removed from the page-info surface.
  - The sidebar route into the Boost bubble has been removed.
- The following verifications already passed against this slab in the parent session and are treated as the baseline green for this plan:
  - `cd maho && bun run typecheck`
  - `cd maho && bun run test`
  - `python3 maho-chromium/build/scripts/build_maho.py`
  - Narrow recompile of the affected Chromium targets via depot_tools `autoninja`.

What is **not** true on disk and is **not** parity, listed here so the rest of this plan does not pretend otherwise:

- `MahoBoostWindowController` exists only as a minimal shell. There is no live separate Boost editor window UX. Invocation paths route into `// TODO(maho-boost-reset)` no-ops.
- The `chrome://maho-boost` WebUI surface artifacts are still on disk as cold compile-only stubs. The controller files, the minimal Mojom IDL, and the placeholder resources shell all survive for build-graph stability.
- `kMahoBoostURL` and related host string constants in `maho-chromium/components/constants/webui_url_constants.h` are still defined as symbol-level constants. No path resolves either to a controller, but the constants themselves linger.
- Raw host recognition and other residual references to the legacy host string still exist scattered across the tree as constants, comments, tests, and documentation strings. None of them are reachable as UX. All of them count as debt.
- The runtime composition payload is absent. `compose_css` is exposed at the Rust API surface but the on-disk implementation returns the empty string. No knob round-trips into rendered visual effect end-to-end.
- The Phase 1.5 rendering-hook feasibility spike has not run. The runtime layer (CSS-only versus deeper-than-CSS) is therefore not yet locked.

This plan starts from "the slab is clean, the contracts are stable, no user can see Boost UX" and ends at "Zen Boost parity is live, every previously neutralized entry point is restored, every constant and host string referencing the dead manager is gone, and the runtime renders the typed knobs end to end through whichever layer Phase 1.5 selected."

---

## 3. What this plan does not change

Listed up front so the parallelization layer cannot drift into product decisions it has no authority over.

- It does not relitigate any Section 4 hard constraint of the reset plan. Domain scope key, typed knobs, one active boost per domain, no per-site JS injection, separate top-level native window as the only editor surface, knob names matching Zen, no migration. All inherited as-is.
- It does not re-open the Section 4 item 5 surface ownership decision. The separate floating Boost editor window is the architectural owner. WebUI is subordinate content technology rendered inside the native window. There is no navigable `chrome://maho-boost` host. Any track that drifts from this is rejected on sight.
- It does not pre-decide the Phase 1.5 rendering-hook verdict. CSS-only is the fallback, not the assumed default. A deeper hook is the target if the spike validates one. This plan schedules the spike and gates downstream runtime work on its written verdict; it does not predict the verdict.
- It does not invent new product surfaces. No new bubble, no new sidebar editor, no new manager page, no new tab-level surface. The neutralized entry points are restored as launch sites for the separate window, nothing more.
- It does not assume Boost is parity at any point before the final enable gate. Anywhere the slab is intermediate, this plan keeps it intermediate behind internal-only or test-only routes.
- It does not modify the upstream `chromium/src/**` tree as a default path. If a track discovers that progress requires a raw upstream edit, that edit is treated as approval-gated and stop-and-replan, not as routine work.
- It does not change `cd maho && bun run typecheck`, `cd maho && bun run test`, or `python3 maho-chromium/build/scripts/build_maho.py` as the canonical verification commands. Those are the gates. New verification adds onto them; it does not replace them.

---

## 4. Parallelization policy and ceiling

The whole point of this plan is to extract throughput without paying for it in contract drift. The policy is conservative on contracts, aggressive on independent surface work.

**Policy.**

1. Anything that mutates a contract that crosses two or more crates or two or more language boundaries is a serial gate. It is landed in its own atomic commit and every other track rebases on it before continuing.
2. Anything that lives entirely inside one crate, one Chromium subtree, or one resources directory and does not change a public-facing contract is a candidate parallel stream.
3. Streams that share a file get serialized at the file level even if they are conceptually parallel. The merge protocol below assigns a single owner per file for the duration of the push.
4. The user must not see Boost UX at any intermediate state. Every parallel stream that touches a previously neutralized entry point keeps the entry point neutralized until the final enable gate flips it.
5. Verification is not a stream that runs at the end. It runs alongside, on every PR, with the canonical commands and with the targeted parity harness defined by Stream F.

**Ceiling.**

The maximum useful parallelism on this push, given current ownership and the file map below, is **six concurrent streams** running between serial gates. Going wider does not help because the file collisions multiply faster than the throughput. Going narrower wastes capacity that the file map already supports.

Anything that pushes past six concurrent streams must either subdivide one of the existing streams in a way that does not introduce new shared files, or wait for the next gate.

---

## 5. Serial gates

Serial gates are the points where every active stream pauses, the gate's PR lands as a single atomic commit, and every active stream rebases on the result before resuming. There are exactly three of them in this push, in this order.

### 5.1 Gate G1: Zen parity contract lock and reference freeze

**What it locks.** The exact Zen reference commit and the exact set of Zen surfaces that define parity for this push. The knob names, knob semantics, active-selection model, separate-window interaction model, runtime composition contract at the level of "typed knobs in, composed payload out", and the rendering hook candidates that Phase 1.5 must investigate.

**Why it is serial.** Every downstream stream reads from this lock. The runtime stream needs the knob list. The bridge stream needs the FFI and Mojom shapes the lock implies. The window UI stream needs the interaction model. The QA stream needs the parity acceptance criteria. If the lock moves under any of them, every stream's work becomes potentially off-contract.

**Inputs.**

- `zen-boost-source-level-parity-reset-plan-2026-05-26.md`, Sections 2, 4, 6.
- The Zen Browser source at the commit the reset plan referenced. If that commit is no longer canonical, the gate updates the reference commit explicitly and re-grounds Section 2 of the reset plan against the new commit in the same atomic commit.

**Output (single atomic commit).**

- A frozen reference document at `.sisyphus/plans/assets/` capturing the locked Zen surfaces, the knob list with names and types and units, and the rendering hook candidate list. The document carries the reference commit hash and a verifiable file list.
- A small amendment to `zen-boost-source-level-parity-reset-plan-2026-05-26.md` Section 2 if and only if the reference commit moved, in the same atomic commit.

**Pass criteria.**

- The locked knob list matches the typed records the Rust contract already carries. If it does not match, the gate amends one or both, atomically, before downstream streams resume.
- The rendering hook candidate list is concrete enough that Phase 1.5 can run against it without further interpretation.

**Until G1 lands, no downstream stream starts.**

### 5.2 Gate G2: Surface ownership final decision (separate window vs WebUI hosting vs routing ownership)

**What it locks.** That the separate floating Boost editor window is the architectural owner, that any WebUI is subordinate content rendered inside the window's content frame, that there is no navigable `chrome://` host as part of the parity surface, and that the routing ownership for "open the Boost editor" lives in `MahoBoostWindowController`, not in any WebUI factory and not in any bubble coordinator.

**Why it is serial.** The reset plan already decided this in Section 4 item 5. The reason it is restated as a gate here is operational: every restored entry point in Stream D needs to know exactly which controller method to call, and Stream C needs to know whether to wire WebUI as content technology inside the window or to skip WebUI entirely if the spike's runtime decision changes the picture. If the streams interpret ownership differently, they re-introduce the bubble or the manager page incrementally and by accident.

**Inputs.**

- `zen-boost-source-level-parity-reset-plan-2026-05-26.md`, Section 4 item 5, Section 7.5 (no coexistence), Section 9 Open Question 9.
- The current cold shell at `maho-chromium/browser/ui/views/boost/maho_boost_window_view.{cc,h}` and `maho_boost_window_controller.{cc,h}`.

**Output (single atomic commit).**

- A short ownership memo committed alongside this plan that says, in one paragraph each, what `MahoBoostWindowController` is responsible for, what `MahoBoostWindowView` is responsible for, what (if any) WebUI host lives inside the window's content frame, who owns the Mojom binder for that host, and what the public API of the controller is from the perspective of an entry point that wants to open the editor.
- A frozen list of the public methods `MahoBoostWindowController` will expose to entry points. At minimum, it includes a single `ShowForActiveDomain(BrowserContext*, content::WebContents* active_tab)` style method. The exact signature is fixed at this gate.

**Pass criteria.**

- No part of the memo permits a navigable `chrome://maho-boost` host as a parity surface.
- No part of the memo permits a `BubbleDialogDelegateView` as the editor host.
- The public method list is small (single-digit count) and is what every Stream D subtrack will call.

**Until G2 lands, Stream C and Stream D do not start.** Streams A, B, and F can start under G1 alone.

### 5.3 Gate G3: Rust to Chromium contract freeze

**What it locks.** The exact FFI signatures and the exact Mojom IDL that carry typed Boost data across the Rust ↔ C++ ↔ TypeScript boundary for the rest of this push. This is not the runtime payload contract (that is locked by Phase 1.5 separately). This is the structural contract: how a `Boost` looks on the wire, how `BoostUpdate` looks on the wire, how `set_active` and `get_active` are called, and how change notifications flow.

**Why it is serial.** Every parallel stream that crosses the boundary reads or writes through these signatures. The bridge stream is the one that defines them, but the runtime, window UI, and QA streams all bind to them. If they shift mid-push, every binding has to be redone and every test has to be rewritten.

**Inputs.**

- The eight Rust FFI symbols already on disk:
  - `maho_core_boost_list_for_domain`
  - `maho_core_boost_get`
  - `maho_core_boost_create`
  - `maho_core_boost_update`
  - `maho_core_boost_delete`
  - `maho_core_boost_set_active`
  - `maho_core_boost_get_active`
  - `maho_core_boost_compose_css`
- The empty Mojom stub at `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom`.
- The G1 frozen knob list.
- The G2 ownership memo.

**Output (single atomic commit).**

- A finalized `maho_boost.mojom` carrying the domain-scoped methods named in the reset plan Section 8 Phase 3, with the typed knob fields matching G1 verbatim.
- The Rust FFI shapes confirmed against the new Mojom. If a signature drift exists, it is reconciled in this commit, with the canonical shape being whichever one matches G1's knob list. Bodies may remain `todo!()` or stub returns.
- Regenerated TS bindings consistent with the new IDL. No old `BoostEntry` or `SiteRule` type returns.
- A short contract README placed next to the IDL stating which side owns which method and which side is allowed to extend the IDL.

**Pass criteria.**

- `cd maho && bun run typecheck` passes.
- `cd maho && bun run test` passes.
- `python3 maho-chromium/build/scripts/build_maho.py` passes.
- A narrow `autoninja -C out/Default` of the affected Chromium targets passes.
- No method on the IDL is reachable from any user-facing route. The IDL is in the build, but there is no live binder to a user-reachable surface.

**Until G3 lands, Streams A, C, and D pause at the boundary.** Stream F may continue on the parity harness skeleton without binding to the IDL until G3 lands. Stream E may continue cleaning up dead constants and host references that are not part of the IDL.

---

## 6. Parallel workstream map

After G1 has landed, work fans out into six streams. Stream A and Stream B start immediately after G1. Stream C and Stream D start immediately after G2. All five product streams synchronize at G3 before binding across the boundary. Stream F runs continuously alongside.

The streams are:

- **Stream A. Runtime / data contract fill.** Owns `maho-types`, `maho-storage`, `maho-core/src/boost_*.rs`, and the bodies behind the eight Rust FFI symbols. Targets the post-G1 typed knob list.
- **Stream B. Bridge / injection path and rendering hook spike.** Owns `maho-chromium/browser/net/maho_boost_injection_handler.{cc,h}` (or its replacement entry point), the runtime delivery layer, and the Phase 1.5 feasibility spike that decides what that entry point is.
- **Stream C. Boost editor surface / UI shell.** Owns `maho-chromium/browser/ui/views/boost/maho_boost_window_{view,controller}.{cc,h}` and any subordinate WebUI content host that lives inside the window's content frame.
- **Stream D. Entry-point restoration.** Owns the previously neutralized launch sites. Subdivided into four subtracks below so they do not collide on the integration layers.
- **Stream E. URL, registration, and residual debt cleanup.** Owns `kMahoBoostURL`, `kMahoBoostHost`, residual host recognition, dead constants, dead comments, dead documentation strings, and the cold WebUI controller files / Mojom stub / resources shell once Stream C confirms they are no longer needed for build-graph stability.
- **Stream F. Verification harness and parity QA.** Owns the targeted parity harness, the rendered-output baseline against Zen, the Appium regression coverage, and the gating commands that every stream's PR must pass before merge.

Each stream is owned end to end by one driver. Cross-stream changes go through PR review by the affected stream's driver before merging.

---

## 7. Stream details

Each subsection below is the execution-level brief for one stream. Sections 7.1 through 7.6 are the streams. Section 7.4 is split into subtracks for entry-point restoration.

### 7.1 Stream A. Runtime / data contract fill

**Owns.**

- `maho/crates/maho-types/src/boost.rs`
- `maho/crates/maho-storage/src/sqlite.rs` Boost-relevant migrations and queries
- `maho/crates/maho-core/src/boost_manager.rs`
- `maho/crates/maho-core/src/boost_runtime.rs`
- `maho/crates/maho-core/src/boost_css.rs`
- `maho/crates/maho-core/src/boost_zap.rs`
- `maho/crates/maho-ffi/src/lib.rs` Boost section, behind the eight `maho_core_boost_*` symbols
- The Rust unit tests for all of the above

**Does not own.**

- Anything in `maho-chromium/`
- The Mojom IDL (Stream B contributes to G3, Stream C consumes from G3)
- Any Chromium-side caller of the FFI

**Goals.**

1. Replace the empty-string return from `compose_css` with a real composition that round-trips every typed knob in the locked G1 list, matching Zen's knob semantics.
2. Implement `set_active` and `get_active` end to end on the `domain_boost_state` row, with persisted active selection across sessions.
3. Implement `list_for_domain`, `get`, `create`, `update`, `delete` end to end against the typed schema.
4. Add or restore unit tests for: schema round-trip per knob, active-selection invariants per domain, golden `compose_css` outputs for representative knob combinations.
5. Extend the change-notification channel that the reset plan Section 8 Phase 2 calls out, so the bridge stream and the window UI stream can subscribe.

**Boundary discipline.**

- Field names match the G1 locked knob list verbatim.
- No Chromium-side code is touched.
- No new public Rust API symbol is added without a corresponding entry on the Mojom IDL plan and a heads-up to Stream B and Stream C.
- The change-notification channel is exposed through the existing `maho-core` event dispatcher pattern that other managers already use.

**Internal milestones.**

- A1. Schema and types round-trip per knob, with passing unit tests. Lands before G3.
- A2. `set_active`, `get_active`, and persisted active-selection invariants. Lands before G3.
- A3. `compose_css` golden outputs covering color, typography, zap, and `custom_css`. Lands before G3 if Phase 1.5 selects CSS, otherwise lands as part of the runtime payload reshape Stream B drives.
- A4. Change-notification channel exposed and tested. Lands before merge order step M3 (see Section 8).

**Verification per PR.** `cd maho && bun run typecheck` and `cd maho && bun run test` both pass. Targeted golden-output tests pass. No Chromium-side build is required for this stream's PRs to merge in isolation, but the reset acceptance commands are still run before the final integration merge.

### 7.2 Stream B. Bridge / injection path and rendering hook spike

**Owns.**

- The Phase 1.5 rendering-hook feasibility spike from end to end, including the written verdict and the same-commit amendments to the reset plan that the spike requires.
- `maho-chromium/browser/net/maho_boost_injection_handler.{cc,h}` if the spike selects CSS-only.
- The deeper-hook entry point, wherever it lives, if the spike selects a deeper hook.
- The C++ side of the change-notification subscriber that consumes Stream A's channel.
- The runtime payload type. Named generically in Mojom (`ComposedRuntimePayload`, not `ComposedCss`) per reset plan Section 8 Phase 2.

**Does not own.**

- The Rust composition itself. That is Stream A.
- Any user-facing UI. Stream C and Stream D handle those.
- Any navigable `chrome://` host. None exists as a parity surface.

**Goals.**

1. Run Phase 1.5 against the rendering hook candidate list G1 froze. Produce per-candidate written verdicts (feasible or infeasible) with cost and fork-burden estimates.
2. Land the same-commit amendments to reset plan Sections 4, 8, and 9 that the spike's verdict requires. This is a reset-plan amendment, atomic with the spike's verdict, before any runtime is locked.
3. Implement the runtime delivery path along the strategy the spike selected. If CSS-only, this is the slim version of `maho_boost_injection_handler.cc`. If a deeper hook, this is whichever entry point that strategy requires.
4. Validate the runtime end to end on the five cases the reset plan Section 8 Phase 2 names, plus the rendered-output baseline versus Zen.

**Boundary discipline.**

- The spike does not run before G1, because it needs the locked rendering hook candidate list.
- The runtime implementation does not run before the spike's verdict has landed.
- The runtime implementation does not run before G3 has frozen the Mojom contract, because the change-notification channel crosses the boundary.
- The runtime implementation does not introduce a new navigable host. If a debug surface is unavoidable, it lives at a debug-only host (for example `chrome://maho-boost-debug`), is gated by `is_debug` or a tools-only GN target, and never serves the manager UX or a bubble UX. This is the rule from reset plan Section 7.5 restated.

**Internal milestones.**

- B1. Phase 1.5 spike and verdict committed, with reset-plan amendments in the same commit. Lands before runtime work starts. May start any time after G1.
- B2. Runtime entry point reduced to a single path per the spike's verdict. Lands after G3.
- B3. End-to-end runtime validation across the five reset-plan cases plus the rendered-output baseline. Lands after Stream A's A3 and B2 are both green.

**Verification per PR.** `python3 maho-chromium/build/scripts/build_maho.py` passes. Narrow `autoninja -C out/Default` recompile of the affected Chromium targets passes. Stream F's parity harness consumes B3's output and runs the rendered-output baseline as part of its harness coverage.

### 7.3 Stream C. Boost editor surface / UI shell

**Owns.**

- `maho-chromium/browser/ui/views/boost/maho_boost_window_view.{cc,h}`
- `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.{cc,h}`
- Any in-window content frame the window hosts. If WebUI is the chosen content technology for the editor's HTML/CSS/TS contents, the resources directory and BUILD.gn target that produce that bundle are owned here.
- The window-local Mojom client bindings (the consumer side). The IDL itself is shared at G3.

**Does not own.**

- Any navigable `chrome://` host. None exists as a parity surface.
- Any toolbar bubble. The bubble shell was hard-deleted in Commit A and is not coming back.
- The launch sites. Those are Stream D.
- The runtime composition. That is Streams A and B.

**Goals.**

1. Fill out `MahoBoostWindowController` with a concrete `Show...` method that opens a top-level `views::Widget` hosting `MahoBoostWindowView`, freely positionable, with native chrome, no auto-dismiss on focus loss, no toolbar anchoring, no address-bar anchoring.
2. Fill out `MahoBoostWindowView` to match Zen's separate Boost editor window literally, with sections for color, typography, zap, custom CSS, and a list/switcher for the domain's boosts plus active selection. Visual fidelity follows the strategy Phase 1.5 selected. Pixel-bit identity is not the goal.
3. Wire the in-window content bindings against the G3 Mojom IDL. The window does not become a navigable `chrome://` page. The window stays a native top-level `views::Widget`.
4. Implement live preview with debounced knob dispatch, end-to-end through the change-notification channel.
5. Cover the empty state, the multi-boost list, click-to-activate, per-row delete, and the "New boost" action.
6. Confirm cross-tab live update behavior on the same domain matches reset plan Section 9 Open Question 5's default.

**Boundary discipline.**

- The window is not exposed as a launch target until Stream D wires the entry points. Until then, opening the window is reachable only through an internal-only or test-only command.
- Naming hygiene from reset plan Section 8 Phase 3 is enforced. No "Boost page", no "Boost panel", no "Boost bubble" in code, comments, or strings. The correct names are "Boost editor window", "Boost window", and "Boost window content".
- The window's public API stays exactly as G2 froze it. New launch sites in Stream D bind to that API.

**Internal milestones.**

- C1. Window controller and view scaffolded against the G2 ownership memo, internal-only command can open and close the window. Lands after G2.
- C2. In-window content bindings wired to the G3 Mojom IDL, no live preview yet but the data round-trips. Lands after G3.
- C3. Live preview, multi-boost list, active selection, delete, "New boost", empty state. Lands after C2 and after Stream A's A4 (change-notification channel).
- C4. Cross-tab live update behavior validated against Open Question 5. Lands after C3 and after Stream B's B3.

**Verification per PR.** `python3 maho-chromium/build/scripts/build_maho.py` passes. Narrow `autoninja` recompile passes. The Stream F harness includes a window-open / edit / persist / reload / close / reopen pass.

### 7.4 Stream D. Entry-point restoration

This stream restores the user-facing launch sites that Commit A reduced to TODO no-ops. It is split into four subtracks. Each subtrack owns its own integration layer and binds only to the public API frozen at G2.

A subtrack does not flip its launch site live until the final enable gate (M5 in Section 8). Until then, every restoration lands behind an internal-only command, a feature test, or a build flag that defaults off, so the user does not encounter Boost UX before parity is real.

#### 7.4.1 Stream D1. Toolbar and location-bar entry

**Owns.** The location-bar Boost quick action and any toolbar action button the reset plan Section 9 Open Question 3 confirms as the primary launch point.

**Does not own.** The window itself. The Rust runtime. The Mojom contract.

**Goal.** Restore the registered toolbar / location-bar entry behind a default-off launch state. When invoked through internal-only command or test, it calls the G2-frozen `Show...` method on `MahoBoostWindowController`. No bubble. No navigation.

**Verification.** Toolbar / location-bar surface compiles, the action is registered but not user-discoverable until M5. Stream F covers the action's invocation under test.

#### 7.4.2 Stream D2. Tab context-menu entry

**Owns.** The tab context-menu Boost commands. The reset plan removed them; this subtrack restores them as inactive registered entries that route to the controller.

**Does not own.** The right-click menu shell itself.

**Goal.** Re-register the context-menu entry. Default-off, calls the controller's `Show...` method when invoked.

**Verification.** Context-menu compiles. Entry is registered but hidden until M5.

#### 7.4.3 Stream D3. Page-info Boost route

**Owns.** The page-info Boost bubble route the reset plan removed. This subtrack restores it as a route that opens the separate Boost editor window, not as a bubble.

**Does not own.** Any `BubbleDialogDelegateView` style host. The route opens the separate window through the controller.

**Goal.** Re-register the page-info entry. Default-off, calls the controller's `Show...` method when invoked. The page-info surface no longer hosts the editor inline.

**Verification.** Page-info compiles. Entry is registered but hidden until M5.

#### 7.4.4 Stream D4. Sidebar route

**Owns.** The sidebar route into Boost that the reset plan removed. This subtrack restores it as a route that opens the separate window.

**Does not own.** The sidebar Boost pane content. The pane stays an empty-state view until M5; the reset plan made that explicit and this stream does not change it.

**Goal.** Re-register the sidebar entry. Default-off, calls the controller's `Show...` method when invoked.

**Verification.** Sidebar compiles. Entry is registered but hidden until M5.

**Why these four subtracks are parallel.** Each one touches a different integration layer in the Chromium tree (toolbar / location bar, tab context menu, page-info, sidebar). They share no source files. They all bind to the same controller method that G2 froze. The subtracks are file-disjoint and contract-bound, which is exactly the shape parallelism is allowed to take here.

### 7.5 Stream E. URL, registration, and residual debt cleanup

**Owns.**

- `maho-chromium/components/constants/webui_url_constants.h` lines defining `kMahoBoostHost` and `kMahoBoostURL`.
- Any raw host recognition path that still mentions `maho-boost` as a navigable host.
- Cold WebUI controller files at `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.{cc,h}` and `maho_boost_page_handler.{cc,h}` once Stream C confirms they are not needed as build-graph anchors.
- The minimal Mojom stub at `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom` once it has been replaced by the G3 IDL.
- The placeholder resources shell at `maho-chromium/browser/resources/maho_boost/` once Stream C decides whether the in-window content frame uses these files or not.
- Documentation strings, AGENTS.md mentions, comments, and tests that reference the dead manager surface.

**Does not own.**

- Any live entry point. The launch sites are Stream D.
- The window itself. That is Stream C.

**Goals.**

1. Remove `kMahoBoostURL`, `kMahoBoostHost`, and any related host string constants from the build, in a single atomic commit, after Stream B and Stream C confirm no live code path resolves to them. The reset plan Section 7.1 left these as constants for compatibility; this stream finishes that removal.
2. Sweep the tree for raw host-string recognition and other residual references. Use `grep -RIn "maho-boost"` and `grep -RIn "MahoBoost"` and walk every match. Each match is either deleted, renamed, or documented as legitimate.
3. Once Stream C confirms the in-window content host does not need the cold WebUI controller files, the minimal mojom stub, or the resources shell, this stream deletes them and removes the corresponding GN target wiring.
4. Once the wider build graph is confirmed not to need them, remove the empty `group("maho_boost_bubble")` and `group("interactive_tests")` shells that the remediation plan Section 7.2 of the reset plan flagged for follow-up removal.

**Boundary discipline.**

- This stream never lands a removal whose only justification is "looks dead." Each removal cites the on-disk evidence (`grep` results, LSP references) that no live caller resolves the symbol, and points at the Stream B / Stream C confirmation that no live runtime needs it.
- This stream never modifies the active runtime path, the active controller, or any active entry point. Its job is entirely deletion and renaming of debt.
- This stream's PRs are reviewed against the no-coexistence rule in reset plan Section 7.5 every time. Anything that could be read as "this constant is here in case we want the manager back" is rejected.

**Internal milestones.**

- E1. `kMahoBoostURL` and `kMahoBoostHost` constants removed. Lands after Stream B's B2 and Stream C's C1.
- E2. Tree-wide grep sweep complete and every legitimate remaining reference documented. Continuous; lands incrementally.
- E3. Cold WebUI controller files, minimal Mojom stub, and resources shell deleted, with their GN target wiring. Lands after Stream C's C2 and Stream B's B2.
- E4. Empty BUILD.gn `group()` shells removed. Lands as soon as the rest of the build graph confirms they are unreferenced.

**Verification per PR.** `python3 maho-chromium/build/scripts/build_maho.py` passes. Narrow `autoninja` recompile passes. `cd maho && bun run typecheck` and `cd maho && bun run test` pass. Stream F includes a "no maho-boost host" check in the parity harness.

### 7.6 Stream F. Verification harness and parity QA

**Owns.**

- The targeted parity harness that runs alongside the canonical verification commands.
- The rendered-output baseline against Zen on the test pages the reset plan Section 8 Phase 2 calls out.
- The Appium regression coverage that the reset plan Section 8 Phase 5 lists.
- The acceptance checklist that this plan's M5 enable gate runs against.

**Does not own.**

- Any product code. Stream F adds tests, harnesses, and acceptance criteria. It does not change the runtime, the bridge, the window, or the entry points.

**Goals.**

1. Build a parity harness skeleton that the rest of the streams can plug their fixtures into. Skeleton lands after G1, before any stream's PR is required to bind to it.
2. Add a per-knob round-trip test that opens the editor (through the internal-only command), edits a knob, observes the page change, persists, reloads, observes again, and tears down. One test per knob in the G1 list.
3. Add a cross-tab live update test against reset plan Open Question 5's default behavior.
4. Add a rendered-output baseline against Zen for the test pages the reset plan Section 8 Phase 2 names. The baseline is parity evidence, not a regression gate.
5. Add Appium regression coverage for each restored entry point (Stream D subtracks D1 through D4) plus the M5 enable gate flip.
6. Verify, before M5 lands, that all of:
   - `cd maho && bun run typecheck` is green
   - `cd maho && bun run test` is green
   - `python3 maho-chromium/build/scripts/build_maho.py` is green
   - Narrow `autoninja -C out/Default` of affected Chromium targets is green
   - The parity harness, the rendered-output baseline, and the Appium suite are green

**Boundary discipline.**

- The harness consumes other streams' output. It does not push changes into them.
- The harness does not depend on any test-only host like `chrome://maho-boost-debug` becoming a parity surface. If such a debug host exists, the harness uses it as a development convenience only.

**Internal milestones.**

- F1. Harness skeleton in place after G1.
- F2. Per-knob round-trip tests after Stream A's A3 and Stream C's C3.
- F3. Cross-tab live update test after Stream C's C4.
- F4. Rendered-output baseline after Stream B's B3.
- F5. Full Appium suite covering D1 through D4 plus M5. Lands as the M5 enable gate's evidence.

**Verification per PR.** Every Stream F PR runs the canonical commands itself. Every other stream's PR runs the relevant subset of the Stream F harness as a precondition for merge.

---

## 8. Merge / integration order

Streams produce many independently mergeable PRs. The merge order below is the **sequencing of integration milestones**, not a list of individual PRs. Each milestone is the point at which a previously parallel slice is integrated into the trunk in a way that affects the rest of the push.

The rule that holds across every milestone: **live user-facing Boost surfaces are not re-exposed before M5.** Every milestone before M5 keeps user discoverability off, even if the underlying code is fully wired.

- **M0. Slab accepted.** Current cold/reset state, baseline of this plan. The acceptance commands have already passed in the parent session. No code change at M0.
- **M1. G1 lands.** Zen parity contract and reference freeze, in a single atomic commit. After M1, Streams A, B, F start in parallel.
- **M2. G2 lands.** Surface ownership memo and `MahoBoostWindowController` public method list frozen. After M2, Streams C and D subtracks start in parallel with Streams A, B, F.
- **M3. Stream A's A1, A2 land. Stream B's B1 lands.** Rust schema, types, active-selection, and Phase 1.5 verdict (with same-commit reset-plan amendments) are in trunk. The runtime layer is now decided, even if not yet implemented.
- **M4. G3 lands.** Mojom IDL frozen. FFI and IDL match. TS bindings regenerated. `cd maho && bun run typecheck`, `cd maho && bun run test`, `python3 maho-chromium/build/scripts/build_maho.py` all green. After M4, every cross-boundary stream binds to the G3 contract.
- **M5 prerequisites integrate** (no single commit; this is a window of merges):
  - Stream A's A3, A4 lands (compose_css golden, change-notification channel).
  - Stream B's B2 lands (runtime entry point per Phase 1.5).
  - Stream C's C2, C3, C4 land (window content, live preview, cross-tab update).
  - Stream D subtracks D1 through D4 register their entries behind default-off launch state.
  - Stream E's E1 (host constants gone), E3 (cold WebUI files gone if Stream C does not need them) land.
  - Stream B's B3 lands (end-to-end runtime validation, rendered-output baseline against Zen).
  - Stream F's F2, F3, F4, F5 are green.
- **M5. Final enable gate. Single atomic commit.** Flip every Stream D subtrack's default-off launch state to live. The toolbar / location-bar action becomes user-discoverable. The tab context-menu entry returns. The page-info entry returns and routes to the separate window. The sidebar route returns and routes to the separate window. The empty-state sidebar pane is replaced by the path that opens the separate window from the sidebar entry, per the architecture in reset plan Section 7.2 / Section 8 Phase 4. Boost UX is live. **Before M5, the user has not encountered Boost UX in any form.**
- **M6. Polish, debt cleanup, regression sweep.** Stream E's E2, E4 land. Final tree-wide grep sweep. Final cold-stub deletion sweep per reset plan Section 8 Phase 5. Final Appium suite recorded as the parity baseline.

The split between "M5 prerequisites integrate" (a window of merges) and "M5 final enable gate" (a single atomic commit) is deliberate. It guarantees that the moment Boost UX returns is a single commit, reviewable and revertable in isolation, and not a slowly accreting drift back into UX over a series of small PRs.

---

## 9. Verification gates

Verification runs at three levels. All three are mandatory at the moments listed; none are optional under schedule pressure.

### 9.1 Per-PR verification

Every PR in every stream runs:

- `cd maho && bun run typecheck`
- `cd maho && bun run test`
- `python3 maho-chromium/build/scripts/build_maho.py`
- The relevant subset of Stream F's harness for whatever the PR touches.

Cross-language PRs (the ones that touch both Rust and Chromium) additionally run a narrow `autoninja -C out/Default` recompile of the affected Chromium targets to surface link-time issues earlier than the full build catches them.

A PR that fails any of the four does not merge. There is no "we'll fix it in a follow-up" exemption on these.

### 9.2 Per-gate verification

At G1, G2, and G3, the gate's PR is the only PR in flight on the trunk. Every other open stream rebases on the gate's commit before resuming. The gate's PR runs the per-PR verification plus:

- A targeted parity-harness pass on whatever new contract the gate locked.
- An LSP-driven references audit on every newly frozen public symbol, to confirm no caller is on an old shape.

### 9.3 Pre-M5 verification

Before the M5 single atomic commit lands, **all** of the following are green on a clean working tree:

- `cd maho && bun run typecheck`
- `cd maho && bun run test`
- `python3 maho-chromium/build/scripts/build_maho.py`
- `autoninja -C out/Default chrome` over `chromium/src`
- Stream F's full parity harness.
- Stream F's rendered-output baseline against Zen on the test pages the reset plan Section 8 Phase 2 names.
- Stream F's Appium regression suite covering each Stream D subtrack and the enable gate flip.
- A manual parity QA pass that opens the separate Boost editor window from each registered entry point, edits at least one knob per category, observes the page change, persists across reload, switches active boost, deletes a boost, closes and reopens the window, and confirms cross-tab live update on the same domain.

If any of the above is not green, M5 does not land. The push waits, the failure is fixed, and verification reruns from scratch.

### 9.4 Post-M5 verification (M6)

After M5 lands and the user can see Boost UX, M6 reruns every per-PR command on the cleanup PRs and re-records the parity harness output as the new baseline. M6 also confirms, with one final tree-wide `grep` sweep, that no symbol referencing the dead manager UX survives.

---

## 10. Risks and stop-and-replan triggers

A stop-and-replan trigger means: the affected stream halts, every other stream pauses at its next safe boundary, and this plan is re-opened (along with the reset plan or the remediation plan if needed) before any stream resumes. None of these are handled by "we will work around it"; all of them are handled by stopping and re-deciding.

### 10.1 Phase 1.5 spike returns a verdict that invalidates the assumed runtime payload type

If the spike selects a deeper-than-CSS hook, the runtime payload type that crosses the boundary may need a richer shape than the placeholder `ComposedRuntimePayload` Stream B expects. **Trigger.** Stream B halts after publishing the verdict. Streams A, C re-evaluate the payload contract before G3 freezes it. Reset plan Sections 4 item 8, 8 Phase 2, and 9 are amended in the same commit as the spike's verdict, per reset plan Phase 1.5 hard rule.

### 10.2 G2 surface ownership decision is challenged in code review

If a reviewer proposes "let's expose the editor as a `chrome://` page for now" or "let's anchor the editor in a bubble until the window is ready" or "let's host the editor in a sidebar surface", this is a tripwire under reset plan Section 9 Open Question 9. **Trigger.** Stream C and Stream D pause. The PR proposing the change is rejected and the proposal is escalated to a plan amendment, not a code workaround.

### 10.3 G3 contract drift after merge

If a stream lands a PR that quietly diverges from the G3 frozen IDL, the contract assumption that other streams rely on breaks. **Trigger.** Every stream that crosses the boundary halts. The drifting PR is reverted or reconciled in a single follow-up commit that re-locks G3.

### 10.4 An entry-point subtrack flips live before M5

Any of D1 through D4 going live before the M5 atomic commit means the user encounters intermediate Boost UX. **Trigger.** Immediate revert of the launch-state flip. The subtrack's branch is rebased on trunk and the launch state is restored to default-off. The PR review process for that subtrack is tightened for the rest of the push.

### 10.5 Stream E removes a constant that was still resolving to a live caller

If `kMahoBoostURL` or another constant Stream E targets is still resolving somewhere unexpected, removing it surfaces a runtime regression. **Trigger.** Stream E halts. The unexpected caller is identified, audited, and either deleted (if dead) or routed to the separate window's controller (if live). Stream E's PR is reposted only after the caller is reconciled.

### 10.6 Stream F harness fails on a knob the spike did not flag

If the rendered-output baseline reveals that a knob is not round-tripping correctly even though Stream A's golden tests pass, the issue is a contract mismatch between `compose_css` and the runtime entry point that the spike assumed. **Trigger.** Streams A, B, and F halt. The contract mismatch is reconciled before any further runtime PR lands.

### 10.7 A PR proposes raw `chromium/src/**` edits as a default workaround

The default path through the overlay is `chromium_src/` overrides plus `maho-chromium/` net-new files. Direct edits into upstream `chromium/src/**` are approval-gated only and treated as exceptional. **Trigger.** Any PR that proposes a raw upstream edit as the default path is rejected on sight and escalated to the user before any stream merges code that depends on that edit. If the edit is genuinely unavoidable, it is approved explicitly, scoped narrowly, and called out in the PR description and in the AGENTS.md follow-up.

### 10.8 The G1 reference commit moves under the push

If Zen's `dev` branch shifts during the push and a reviewer points out that Maho's parity contract is now defined against a stale snapshot, the contract risks drifting under every stream simultaneously. **Trigger.** G1 is reopened. The new reference commit is locked. Sections of the reset plan that name specific Zen behaviors are re-grounded against the new commit in the same atomic commit. Every stream rebases on the new G1 before resuming.

---

## 11. Final completion definition

This plan is **done** exactly when **all** of the following hold simultaneously:

1. M5 has landed as a single atomic commit. Boost UX is live for the user. The toolbar / location-bar entry, the tab context-menu entry, the page-info entry, and the sidebar entry all open the separate Boost editor window through `MahoBoostWindowController`'s G2-frozen public method.
2. The separate Boost editor window matches Zen's separate-window interaction model. It is freely positionable, has native title and close chrome, does not auto-dismiss on focus loss, and is not anchored to the toolbar or address bar. There is no `chrome://maho-boost` host as a parity surface. There is no `BubbleDialogDelegateView`-shaped Boost editor anywhere in the tree.
3. Every typed knob in the G1 frozen list round-trips end to end: editor edit, Mojom dispatch, Rust persistence, runtime composition through the strategy Phase 1.5 selected, page change observed, persisted across reload, cross-tab live update on the same domain working per reset plan Open Question 5.
4. `kMahoBoostURL`, `kMahoBoostHost`, the cold WebUI controller files, the minimal Mojom stub, the placeholder resources shell, and the empty BUILD.gn `group()` compatibility shells are gone from the tree, except where Stream C explicitly retained an in-window WebUI content host that lives only inside the native window's content frame.
5. The full reset plan Section 8 Phase 5 cleanup pass has run. No comment, AGENTS.md mention, or test name references the dead manager UX. A final tree-wide `grep -RIn "maho-boost"` and `grep -RIn "MahoBoost"` returns only legitimate references (the renamed editor surface, debug-only hosts gated by `is_debug`, and historical plan documents under `.sisyphus/plans/`).
6. Verification is green:
   - `cd maho && bun run typecheck`
   - `cd maho && bun run test`
   - `python3 maho-chromium/build/scripts/build_maho.py`
   - `autoninja -C out/Default chrome` over `chromium/src`
   - Stream F's parity harness, including the rendered-output baseline against Zen on the test pages reset plan Section 8 Phase 2 names.
   - Stream F's Appium regression suite covering every restored entry point and the M5 enable gate flip.
   - A manual parity QA pass against Zen on at least the five reset-plan validation cases (color knob change, smart_invert on light and dark pages, zap_selectors hiding visible elements, custom_css overriding a known selector, typography knob composition).
7. The reset plan and the remediation plan have not been contradicted by anything this push landed. Every amendment to the reset plan that this push made (Phase 1.5 verdict, runtime payload reshape if any, contract reshape if any) is committed atomically with the change that required it.
8. No stop-and-replan trigger from Section 10 is open at the moment of declaring completion. Every triggered halt was either resolved through an amendment or escalated and signed off explicitly.

Until all eight hold, this plan stays the active scheduling source of truth. Once they hold, the parity push is closed, this plan is archived alongside the reset plan and the remediation plan as the historical execution layer for Zen Boost parity, and any further Boost work is scoped against a new plan.
