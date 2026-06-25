# Theme Parity Migration Plan: Old `create-theme` → Shared `maho_space_config`

> **⚠️ SUPERSEDED (2026-05-22)**: The migration to `maho_space_config` proposed in this plan is **rejected and forbidden**. See `.sisyphus/plans/theme-picker-architecture-decision.md` (ADR) — theme picker permanently lives at `chrome://maho-space-create`. The component-level inventory and analysis below remains valid as historical reference, but **do not pursue the migration direction**.

**Status:** Planning (no implementation yet)
**Owner:** TBD (next implementation session)
**Last updated:** 2026-05-19

---

## 0. Goal

Bring the **old `create-theme` UI and behavior** to full parity inside the **shared `maho_space_config` WebUI surface**, using:

- The **external Image1 / reference artifact** (the screenshot supplied this session) as the **visual target**.
- The **old `maho_space_create` surface** (Views dialog, WebUI page, React app, store, CSS, page handler, helpers, tests) as the **implementation/spec target** for behavior, callback shapes, theme JSON, and lifecycle.

Production must remain on the **shared** `maho_space_config` path. There will be **no second production WebUI surface**, and no runtime fallback to the old path. The old surface stays in-tree only as a parity reference until late cleanup.

> **Already true in production today (do not redo):**
> `MahoSidebarCreateSpaceView::OnThemePressed()` already routes the create flow into `MahoSpaceConfigDialog::OpenForCreate(...)`. The old native theme picker Views code path is dead in production. The live gap is **create-mode composition** on the shared React UI and **migration of dead-picker tests**.

---

## 1. Scope, Non-Scope, Invariants

### In scope
- Create-mode UI composition inside `maho_space_config/react/app.tsx` matching the Image1 reference visually and matching old `maho_space_create` behaviorally.
- Preview lifecycle parity (open/close/cancel/commit) on the shared dialog/page-handler.
- Theme JSON shape and callback parity between create and edit so the same store/page-handler can serve both.
- Test migration off dead native-picker hooks onto the shared surface.
- Parity fixtures and golden snapshots (visual and JSON) covering create + edit.
- Late, gated deletion of the old `maho_space_create` surface and dead picker code.

### Out of scope
- Any new theme features beyond what old `create-theme` already shipped.
- Any second production WebUI route, runtime fallback, or feature flag toggling between old and new.
- Refactoring unrelated sidebar / spaces code beyond what is required to delete dead picker entry points at the end.

### Invariants (must hold every phase)
1. Production entry stays: `MahoSidebarCreateSpaceView::OnThemePressed()` → `MahoSpaceConfigDialog::OpenForCreate(...)`.
2. Shared host/store/page-handler is canonical. Create vs edit splits at the **React render-tree level** inside `maho_space_config/react/app.tsx`, not at the host or page-handler level.
3. Old `maho_space_create/*` files are **read-only reference** until Phase 9. No new code links against them.
4. No phase ships if it regresses edit-mode behavior of `maho_space_config`.

---

## 2. File Inventory and Disposition

### KEEP as canonical (production target)
- `maho-chromium/browser/resources/maho_space_config/react/app.tsx`
- `maho-chromium/browser/resources/maho_space_config/react/store.ts`
- `maho-chromium/browser/resources/maho_space_config/space_config.css`
- `maho-chromium/browser/resources/maho_space_config/space_config.html`
- `maho-chromium/browser/resources/maho_space_config/app.ts`
- `maho-chromium/browser/resources/maho_space_config/bundle_react.mjs`
- `maho-chromium/browser/resources/maho_space_config/BUILD.gn`
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.cc`
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.h`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_ui.cc`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_ui.h`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config.mojom`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`

### KEEP TEMPORARILY as parity reference (delete in Phase 9 only)
- `maho-chromium/browser/resources/maho_space_create/react/app.tsx`
- `maho-chromium/browser/resources/maho_space_create/react/store.ts`
- `maho-chromium/browser/resources/maho_space_create/react/types.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`
- `maho-chromium/browser/resources/maho_space_create/space_create.html`
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/bundle_react.mjs`
- `maho-chromium/browser/resources/maho_space_create/BUILD.gn`
- `maho-chromium/browser/ui/views/space_create/maho_space_create_dialog.cc`
- `maho-chromium/browser/ui/views/space_create/maho_space_create_dialog.h`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_ui.cc`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_ui.h`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create.mojom`
- Any dead native theme-picker Views files referenced only by `maho_sidebar_spaces_interactive_test.cc` (audit during Phase 1).

### MIGRATE (tests + dead hooks)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

### DELETE in Phase 9 (gated by all earlier acceptance gates)
- All files in the "KEEP TEMPORARILY" list above.
- Any `maho_space_create` build deps, runtime registrations, resource map entries, and `BUILD.gn`/`chrome/browser/BUILD.gn` references.
- Dead native theme-picker Views entry points still referenced from sidebar code, once tests no longer hook them.

---

## 3. Unresolved-but-Bounded Decisions (defaults, not blockers)

| Topic | Default | Trigger to revisit |
|---|---|---|
| Create-mode dialog width | Prototype at the existing `maho_space_config` width first; if Image1 visually requires a wider canvas, add a create-only width in `MahoSpaceConfigDialog::OpenForCreate` and document the delta. | Visual diff vs Image1 in Phase 6 fails at the canonical width. |
| Image1 | Treated as an **external visual artifact**, not a code symbol. Stored as a checked-in PNG under `.sisyphus/plans/assets/` (or equivalent) for reviewer reference, plus golden snapshots produced in Phase 7. | Reviewer cannot locate the artifact during Phase 7. |
| Create-mode default theme seed | Reuse old `maho_space_create` initial-state defaults verbatim (palette, gradient, name placeholder). | Old defaults fail to satisfy edit-mode invariants in Phase 5. |
| Old WebUI registration | Keep registered but unreached from production until Phase 9. | A reviewer asks for earlier removal; only allowed if Phase 7 goldens exist. |

---

## 4. Phase-by-Phase Plan

Each phase lists: **Goal · Files touched · Specific changes · Dependencies · Verification · Deletion boundary**.

Phases 1–7 are additive or migrational. Phase 8 is a soft cutover. Phase 9 is destructive cleanup and must not start until all prior gates pass.

---

### Phase 1 — Artifact capture and parity reference freeze

**Goal:** Lock the spec inputs so later phases have a stable target. No production behavior changes.

**Files touched (new):**
- `.sisyphus/plans/assets/theme-parity-image1.png` (the external reference screenshot; commit a copy).
- `.sisyphus/plans/theme-parity-reference-index.md` (this section, expanded into a reference index).

**Files touched (read-only audit, no edits):**
- `maho-chromium/browser/resources/maho_space_create/react/app.tsx`
- `maho-chromium/browser/resources/maho_space_create/react/store.ts`
- `maho-chromium/browser/resources/maho_space_create/react/types.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`
- `maho-chromium/browser/ui/views/space_create/maho_space_create_dialog.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

**Specific changes:**
1. Commit Image1 into the repo at the path above so it is reviewable.
2. In the new reference index, list:
   - Each old React component in `maho_space_create/react/app.tsx` and its responsibility (header, name field, theme grid, gradient editor, preview, primary CTA, cancel).
   - Each store action in `maho_space_create/react/store.ts` and the page-handler call it triggers.
   - Each page-handler method in `maho_space_create_page_handler.cc/.h` and the C++ side effect (preview apply, preview revert, commit, cancel).
   - Each dead native picker symbol referenced by `maho_sidebar_spaces_interactive_test.cc` (interactive-test fixture hooks).
3. Note any old behavior that is **not** present in shared `maho_space_config` today (this becomes the implementation backlog for Phases 4–6).

**Dependencies:** None.

**Verification:**
- Reference index reviewed; every old surface element has either a "matched on shared" or "gap, fix in Phase X" annotation.
- Image1 renders in the markdown.

**Deletion boundary:** Nothing deleted. Old surface remains compiled and linked.

---

### Phase 2 — Preview lifecycle hardening on the shared surface

**Goal:** Make `maho_space_config` preview behavior symmetric and idempotent so create-mode (Phase 5) can layer on without races. Edit mode is the immediate beneficiary.

**Files touched:**
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h`
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.cc`
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.h`
- `maho-chromium/browser/resources/maho_space_config/react/store.ts`
- `maho-chromium/browser/resources/maho_space_config/react/app.tsx` (only where it calls store actions)

**Specific changes:**
1. In the page handler, ensure every "apply preview" mojo call has an exactly-paired "revert preview" path on:
   - dialog cancel,
   - dialog close-without-commit (esc, OS close, programmatic close),
   - tab/profile teardown.
2. In the dialog (`maho_space_config_dialog.cc`), gate destruction so that `Cancel` or window-close always invokes the page handler's revert hook before the WebUI is torn down. Document the destruction order in a header comment in `.h`.
3. In `store.ts`, expose two intent-level actions used by both create and edit:
   - `applyPreview(themeJson)` — debounced.
   - `revertPreview()` — idempotent, safe to call twice.
4. In `app.tsx`, route any place that currently mutates preview state directly through these two actions.
5. Match the **old `maho_space_create_page_handler.cc`** revert behavior verbatim where the shared one is silently weaker.

**Dependencies:** Phase 1 reference index identifies any old preview behavior the shared surface lacks.

**Verification:**
- `lsp_diagnostics` clean on all touched files.
- Manual: open edit, change theme, cancel → original theme restored. Repeat with OS close. Repeat with double-cancel.
- Add a unit/browser test that asserts revert is called exactly once per open if no commit happens. (Co-locate in the existing space_config test target if it exists; otherwise queue in Phase 3.)

**Deletion boundary:** Nothing deleted.

---

### Phase 3 — Test migration off dead native-picker hooks

**Goal:** Stop interactive tests from depending on dead Views picker code so Phase 9 can delete that code safely. Move them onto the shared WebUI surface.

**Files touched:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` (only to expose any test seam needed for the WebUI dialog; prefer no change)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h` (same)
- (New) test helpers under `maho-chromium/browser/ui/views/space_config/` if reusable seams are needed.

**Specific changes:**
1. Audit every test in `maho_sidebar_spaces_interactive_test.cc` that pokes a native theme-picker symbol. For each:
   - Identify the user-visible behavior under test (e.g., "pressing theme on a new space opens theme picker and persists choice").
   - Rewrite it against the shared dialog: drive `MahoSidebarCreateSpaceView::OnThemePressed()` and assert against `MahoSpaceConfigDialog`/page-handler observable state, not native picker internals.
2. Where a behavior is currently only observable through the dead picker, add a **minimal** observer hook on `MahoSpaceConfigDialog` (e.g., a `TestObserver` interface in the `.h` already present, otherwise a small one) rather than re-exposing the dead picker.
3. Do not delete any picker symbol yet; only stop referencing it from tests.

**Dependencies:** Phase 2 (revert symmetry assertions become much easier to write after lifecycle hardening).

**Verification:**
- `cd maho && bun run typecheck` clean (Rust side unaffected, but run anyway).
- Build: `python3 maho-chromium/build/scripts/build_maho.py` succeeds.
- The interactive test target builds and passes.
- `grep` shows zero remaining test references to the dead picker symbols enumerated in Phase 1.

**Deletion boundary:** Test references to dead symbols are gone, but the symbols themselves stay (they are still linked elsewhere or registered; cleanup is Phase 9).

---

### Phase 4 — Render-tree split: create vs edit inside shared React app

**Goal:** Inside `maho_space_config/react/app.tsx`, introduce a clean create/edit split at the React level so create-mode composition (Phase 5) has a place to live without forking the host.

**Files touched:**
- `maho-chromium/browser/resources/maho_space_config/react/app.tsx`
- `maho-chromium/browser/resources/maho_space_config/react/store.ts`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config.mojom`
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.cc` (only `OpenForCreate`)
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.h` (only `OpenForCreate`)

**Specific changes:**
1. In `space_config.mojom`, ensure the page handshake exposes a `mode` field (`kCreate` | `kEdit`) on initial state. If it already exists, reuse it; do not invent a parallel signal.
2. In `maho_space_config_page_handler.cc/.h`, plumb the mode from `OpenForCreate(...)` / edit entry through to the initial state sent to the WebUI. No behavior change for edit mode.
3. In `store.ts`, surface `mode` as a top-level selector. Add no business logic yet; create-mode just switches which root component renders.
4. In `app.tsx`, split into:
   - `<EditRoot />` — current edit UI, untouched in this phase.
   - `<CreateRoot />` — new component, renders a placeholder ("Create mode") plus a wired primary CTA and cancel that already call existing commit/cancel store actions.
   - Top-level switch on `mode`.
5. `MahoSpaceConfigDialog::OpenForCreate(...)` already exists; confirm it sets the create mode on the page handler. Adjust only if the mode is currently being inferred instead of set.

**Dependencies:** Phase 2 (revert symmetry must hold for both roots), Phase 3 (tests are no longer reaching into the old picker internals).

**Verification:**
- `lsp_diagnostics` clean on touched TS and C++ files.
- Build: `python3 maho-chromium/build/scripts/build_maho.py` succeeds.
- Manual: opening edit renders unchanged UI. Opening create from `OnThemePressed` renders the placeholder `CreateRoot` and the cancel/commit buttons round-trip through the page handler without crashing.

**Deletion boundary:** Nothing deleted.

---

### Phase 5 — Create-mode composition port from old `maho_space_create`

**Goal:** Port the **behavior and structure** of old `maho_space_create/react/app.tsx` into `<CreateRoot />`, talking to the shared `store.ts` and shared page-handler. Visual parity to Image1 is finalized in Phase 6.

**Files touched:**
- `maho-chromium/browser/resources/maho_space_config/react/app.tsx`
- `maho-chromium/browser/resources/maho_space_config/react/store.ts`
- `maho-chromium/browser/resources/maho_space_config/space_config.css`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config.mojom`
- (Reference, do not edit) `maho-chromium/browser/resources/maho_space_create/react/app.tsx`, `store.ts`, `types.ts`, `space_create.css`.

**Specific changes:**
1. For each component in old `maho_space_create/react/app.tsx`, port a counterpart inside `<CreateRoot />`. Required parity surface (derived from the old surface inventory in Phase 1):
   - Space name input (with the same validation/placeholder behavior).
   - Theme grid / palette selector.
   - Gradient editor (if present in old surface).
   - Live preview tied to `applyPreview` from Phase 2.
   - Primary CTA (Create) → shared commit action.
   - Cancel → shared revert + close.
2. In `store.ts`, add only the create-specific actions that the shared store does not already cover. Each new action must have a 1:1 mapping to an old `maho_space_create_page_handler.cc/.h` method. No new mojo verbs that did not exist in the old handler.
3. In `maho_space_config_page_handler.cc/.h` and `space_config.mojom`, add the missing methods, **named after the shared convention**, but with semantics matching the old handler. Inline a comment in the `.cc` cross-referencing the old method for traceability.
4. In `space_config.css`, port required class names/structure under a `:root[data-mode="create"]` (or equivalent) scope. Do not modify edit styles.
5. Width: prototype at the existing shared dialog width. If create UI overflows or visibly under-fills relative to Image1, add an explicit width in `MahoSpaceConfigDialog::OpenForCreate(...)` and document it in the `.h`.

**Dependencies:** Phase 4 split exists; Phase 1 reference index is the spec for what to port.

**Verification:**
- `lsp_diagnostics` clean.
- Build green.
- Manual smoke against Image1: shape and information density roughly match (final visual polish is Phase 6).
- Theme JSON emitted by create-mode commit, captured to a temp file and diffed against an old-surface capture for the same inputs (used as input to Phase 7 goldens). Diff must be empty modulo intentional, documented deltas.

**Deletion boundary:** Old `maho_space_create/*` still untouched.

---

### Phase 6 — Visual parity to Image1

**Goal:** Make `<CreateRoot />` match Image1 pixel-by-pixel within an agreed tolerance. Pure CSS/markup polish; no behavior changes.

**Files touched:**
- `maho-chromium/browser/resources/maho_space_config/space_config.css`
- `maho-chromium/browser/resources/maho_space_config/react/app.tsx` (markup-only tweaks)
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.cc/.h` (only if width/height needs adjusting; document the change)

**Specific changes:**
1. Side-by-side compare `<CreateRoot />` against Image1 (committed under `.sisyphus/plans/assets/`). Capture a screenshot at the same DPI.
2. Adjust spacing, typography, color tokens, and component proportions in `space_config.css` (under the create-mode scope) until visual diff is within tolerance.
3. If width is wrong, change it once in `OpenForCreate` and re-shoot.
4. Avoid touching edit-mode CSS rules.

**Dependencies:** Phase 5 produces the structure to style.

**Verification:**
- Manual visual diff against Image1 within tolerance.
- Edit-mode screenshot unchanged from a Phase 4 baseline screenshot.

**Deletion boundary:** None.

---

### Phase 7 — Parity fixtures and goldens

**Goal:** Lock parity so Phase 9 deletion is safe and so future regressions are caught.

**Files touched (new, locations indicative; match existing test conventions):**
- `maho-chromium/browser/ui/webui/maho_space_config/test/space_config_create_parity_test.cc`
- `maho-chromium/browser/ui/webui/maho_space_config/test/data/create_default_theme.json` (golden)
- `maho-chromium/browser/ui/webui/maho_space_config/test/data/edit_default_theme.json` (golden, if not already covered)
- `maho-chromium/browser/ui/webui/maho_space_config/test/data/create_screenshot_baseline.png` (visual golden)
- BUILD.gn additions in `maho-chromium/browser/ui/webui/maho_space_config/` for the new test target.

**Specific changes:**
1. Theme JSON goldens: drive create-mode commit with a fixed seed input; assert emitted theme JSON equals the golden. Generate the initial golden from Phase 5's captured diff, reviewed against an old-surface capture for the same input.
2. Lifecycle goldens: assert `applyPreview` / `revertPreview` call counts and order for: open→cancel, open→commit, open→OS-close, open→change→cancel.
3. Visual golden: snapshot `<CreateRoot />` at the canonical width/DPI. Tolerance documented in the test.
4. Wire all new tests into BUILD.gn so CI runs them.

**Dependencies:** Phases 5 + 6 complete and stable.

**Verification:**
- All new tests pass on CI.
- Tests fail loudly if `applyPreview`/`revertPreview` symmetry is violated.

**Deletion boundary:** Nothing deleted yet, but **this phase is the deletion gate** for Phase 9.

---

### Phase 8 — Soft cutover audit

**Goal:** Prove there are zero runtime references to `maho_space_create` from production code before deleting anything.

**Files touched (read-only audit; produce a checklist artifact):**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc/.h`
- All sidebar/spaces wiring that historically opened the old surface.
- `chrome/browser/BUILD.gn` (or wherever `maho_space_create` is currently registered in your overlay).
- WebUI URL registry for the overlay.
- `maho-chromium/browser/ui/webui/maho_space_create/*`

**Specific changes:**
1. Confirm every production code path to a "create theme" entry resolves into `MahoSpaceConfigDialog::OpenForCreate`.
2. Confirm no production code instantiates `MahoSpaceCreateDialog`, `MahoSpaceCreateUI`, or `MahoSpaceCreatePageHandler`.
3. Confirm the `maho_space_create` WebUI URL is unreachable from any production entry (sidebar, menu, command, keyboard shortcut, internal redirect).
4. Confirm Phase 3 already removed all test references to dead native picker symbols.
5. Produce a signed-off audit checklist in the PR description; this is the gate signal for Phase 9.

**Dependencies:** Phase 7 goldens green.

**Verification:**
- `grep` over the overlay shows zero production references to `maho_space_create` symbols.
- Tests still green.

**Deletion boundary:** Nothing deleted yet.

---

### Phase 9 — Destructive cleanup

**Goal:** Delete the old surface and dead picker code now that parity is locked and references are gone.

**Files deleted:**
- `maho-chromium/browser/resources/maho_space_create/` (entire directory).
- `maho-chromium/browser/ui/webui/maho_space_create/` (entire directory).
- `maho-chromium/browser/ui/views/space_create/` (entire directory).
- Any dead native theme-picker Views files identified in Phase 1 and confirmed unreferenced in Phase 8.
- WebUI URL registration for `maho_space_create` in the overlay.
- Build deps and resource map entries for `maho_space_create`.

**Files touched (edits, not deletions):**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc/.h` (remove any include or call that only existed to support the dead picker).
- The overlay's top-level `BUILD.gn` files that listed `maho_space_create` targets.
- Resource map / WebUI URL registry files.

**Specific changes:**
1. Delete files listed above.
2. Remove every BUILD.gn dep, source list entry, and resource map entry pointing at the deleted paths.
3. Remove now-unused includes in surviving sidebar files.
4. Run a global `grep` for `maho_space_create` across the overlay; the only remaining hit must be in this plan document and in git history.

**Dependencies:** Phases 7 and 8 must be green and signed off.

**Verification:**
- `python3 maho-chromium/build/scripts/build_maho.py` succeeds.
- All Phase 7 goldens still green.
- Interactive tests from Phase 3 still green.
- `grep -r maho_space_create maho-chromium/` returns no production hits.

**Deletion boundary:** This is the only phase that deletes things. If any earlier phase wants to delete, the answer is "no, defer to Phase 9".

---

## 5. Acceptance Gates (defensible from synthesis)

A phase only ships when its own verification passes **and** all prior gates still hold.

1. **G1 — Test migration gate (end of Phase 3):** Zero test references to dead native picker symbols. Interactive tests pass against the shared dialog.
2. **G2 — Preview lifecycle gate (end of Phase 2):** `applyPreview` / `revertPreview` are symmetric and idempotent. Edit mode regression-free.
3. **G3 — Create-mode composition gate (end of Phase 5):** `<CreateRoot />` covers every old `maho_space_create` UI/behavior element from the Phase 1 inventory.
4. **G4 — Theme JSON & callback parity gate (end of Phase 5, locked in Phase 7):** Theme JSON emitted by create-mode commit equals the old-surface JSON for matching inputs, modulo documented deltas. Page-handler callback shapes for create map 1:1 to old methods.
5. **G5 — Visual parity gate (end of Phase 6):** `<CreateRoot />` matches Image1 within tolerance; edit mode screenshot unchanged.
6. **G6 — Fixtures/goldens gate (end of Phase 7):** JSON goldens, lifecycle goldens, and visual golden are committed and green in CI.
7. **G7 — Audit gate (end of Phase 8):** No production references to `maho_space_create`. Production "create theme" entry unambiguously goes through `MahoSpaceConfigDialog::OpenForCreate`.
8. **G8 — Cleanup gate (end of Phase 9):** Old surface deleted, build green, all goldens still green.

Phase 9 is **blocked** until G1–G7 are all green.

---

## 6. Risks and Mitigations

- **Risk:** Create-mode reveals a behavior in old `maho_space_create_page_handler.cc` not covered in Phase 1 inventory. **Mitigation:** Phase 5 explicitly cross-references each new shared method to an old one; missing behavior gets a sub-task and Phase 5 does not exit until all are mapped.
- **Risk:** Width/visual mismatch with Image1 forces a create-only dialog size. **Mitigation:** Allowed; documented in `OpenForCreate` and the plan's "unresolved-but-bounded" table; not a blocker.
- **Risk:** Dead picker symbols are referenced from a non-test surface we missed. **Mitigation:** Phase 8 audit is explicit and gated.
- **Risk:** Goldens go stale with intentional theme tweaks. **Mitigation:** Goldens live next to the test; updates require a comment justifying the diff.

---

## 7. Out-of-Plan Reminders

- Production stays on shared `maho_space_config`. There is no second production surface and no runtime fallback.
- Old `maho_space_create/*` is **reference only** until Phase 9. New code must not link against it.
- Image1 is an external artifact, not a code symbol. Treat it as a checked-in PNG plus visual golden.
- `MahoSidebarCreateSpaceView::OnThemePressed()` → `MahoSpaceConfigDialog::OpenForCreate(...)` is already wired. Do not reroute it.
