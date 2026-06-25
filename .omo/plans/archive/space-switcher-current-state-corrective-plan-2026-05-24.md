# Space Switcher Current-State Corrective Plan

**Created:** 2026-05-24
**Status:** Draft, grounded in the live tree as of today
**Scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.{cc,h}` (required), `maho-chromium/browser/ui/views/sidebar/BUILD.gn` (required for the new unit test target), `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` (optional, font-only), plus a single new unit test file under `maho-chromium/browser/ui/views/sidebar/`.
**Relationship to prior plans:**

- Supersedes the implementation-side guidance in `.sisyphus/plans/space-switcher-unicode-icon-remediation-plan-2026-05-24.md` Sections 5-6. That plan described the change as a future predicate flip; the predicate flip has since landed and the live behaviour now needs a corrective pass, not a fresh implementation.
- Does not alter the bridge / shortcut work in `.sisyphus/plans/space-switcher-revised-plan-2026-05-24.md`. Phases A/B of that plan are marked DONE in the file but the landing is incomplete in two specific ways called out below; this plan finishes those threads narrowly without re-opening Phase D.
- Treats default-icon seeding in `maho/crates/maho-core/src/space_manager.rs` as still-deferred follow-up. Out of scope here.

---

## 1. Objective

Bring the sidebar space switcher's rest-state visual behaviour to a clean, defensible end state given the code that currently exists in the tree. Specifically:

1. Restore active-state visual emphasis for icon-bearing spaces. The current `OnPaint` returns early whenever `dot_opacity_ <= 0.0f`, which is exactly the case for icon-bearing spaces under the new `UpdateAppearance` semantics. The active halo, which is the only remaining active-emphasis affordance besides size tier, never paints for active spaces that have an icon.
2. Resolve the dot-size constant question explicitly. The header currently carries enlarged constants (`4.0 / 4.8 / 5.8 / 7.6`) that were introduced as the visual fix for an older "dots are too small" report. With the icon now the primary rest-state surface for icon-bearing spaces, those constants only affect icon-less fallbacks and the prior justification no longer applies uniformly. Pick a position and document it.
3. Remove the stale class-level comment in `maho_sidebar_space_dot_view.h` that still describes the old hover-revealed-icon behaviour. The implementation no longer matches the comment.
4. Lock in the corrected behaviour with a focused unit test on `MahoSidebarSpaceDotView`, plus targeted use of the existing interactive tests for integration confidence. The current `BUILD.gn` `unit_tests` source_set is already wired up but only contains the DnD dispatch unit test; this is the natural seam for a new dot-view unit test.

This plan is intentionally narrow. It is a corrective follow-up to a partially landed change, not a redesign.

---

## 2. Current-Code Diagnosis

The following is what the tree actually does today, not what an earlier plan wanted it to do. All line numbers are against the current files.

### 2.1 `maho_sidebar_space_dot_view.cc::UpdateAppearance()` (lines 230-243)

```cpp
void MahoSidebarSpaceDotView::UpdateAppearance() {
  const bool show_icon = has_icon_;
  const float target_icon_opacity = show_icon ? 1.0f : 0.0f;

  dot_opacity_ = show_icon ? 0.0f : 1.0f;
  ...
}
```

The icon-visibility predicate has already been flipped from the older `(is_hovered_ || is_active_) && has_icon_` to `has_icon_`. This is correct by the design recorded in the unicode-icon remediation plan. The follow-on consequence is that `dot_opacity_` is now `0.0f` for every icon-bearing space at every interaction state, including active.

### 2.2 `maho_sidebar_space_dot_view.cc::OnPaint()` (lines 107-140)

```cpp
void MahoSidebarSpaceDotView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);

  if (dot_opacity_ <= 0.0f) {
    return;
  }
  ...
  if (is_active_) {
    cc::PaintFlags halo_flags;
    ...
    canvas->DrawCircle(center, radius + 0.8f, halo_flags);
  }
}
```

The early return is the bug that this plan exists to correct. It is fine to skip painting the gray fallback dot when an icon is shown, but the active halo block at the end of the function is also gated by that early return. Result: an icon-bearing active space paints only its glyph, with no halo and no other rest-state active emphasis. The user sees the same glyph regardless of which icon-bearing space is active.

The size tier from `GetCurrentDotSize()` does not currently rescue this either, because the size tier is only consumed inside `OnPaint` for the dot circle. The `icon_label_` bounds are pinned at construction time (`SetBoundsRect((kViewSize - kIndicatorSize)/2, ...)`) and do not respond to `is_active_`. So losing the halo loses the only remaining active affordance for icon-bearing spaces.

### 2.3 `maho_sidebar_space_dot_view.h` (lines 35-37, 75-80)

The header still carries:

```cpp
// Renders a single space as a compact footer dot with subtle inactive states
// and a slightly more prominent active state. Hover still reveals the icon when
// one exists so existing affordances remain intact.
```

The "Hover still reveals the icon" sentence is now false; icons are visible at rest whenever the space has one. Leaving the comment as-is is a real maintenance hazard because the next reader will assume the documented behaviour is the live behaviour.

The size tier constants currently stand at:

```cpp
static constexpr float kDotSizeDefault = 4.0f;
static constexpr float kDotSizeSelected = 4.8f;
static constexpr float kDotSizeHoveredNoIcon = 5.8f;
static constexpr float kDotSizeActive = 7.6f;
```

These were enlarged from the older `2.0 / 2.4 / 2.9 / 3.8` set as a remediation for "dots are too small to read as a navigation surface". With the rest-state being a glyph for icon-bearing spaces, these constants now affect only:

- icon-less fallback spaces, in all states,
- icon-less hover (`kDotSizeHoveredNoIcon`).

It is not obvious whether the enlarged tier still reads correctly when surrounded by icon-bearing rows that no longer paint a dot at all. This is a decision point, not an implementation step. Section 3.2 records the recommended position.

### 2.4 `maho_sidebar_create_space_view.cc::SpaceIconPickerButton` (lines 553-586, 1162)

The picker preview button is a plain `views::Button` containing a single `views::Label` initialized with `u"+"`. When a glyph is picked, the label text is replaced (line 1162 region). It uses the default `views::Label` font list. This is the same font path as `icon_label_` in the dot view, so font fallback behaviour is consistent across the two surfaces today.

This file is the consistency surface for any font-handling change. If this plan recommends an explicit font list on the dot view's `icon_label_`, the same change must land in `SpaceIconPickerButton` to keep the two glyph renderers visually identical. This plan does not recommend such a change; see Section 3.3.

### 2.5 `maho_sidebar_footer_view.cc`

The icon and active-id pipeline through the footer is correct. `RebuildDots` already passes the icon string verbatim into `MahoSidebarSpaceDotView::Configure` and the bridge-scoped active id is already used for `is_active`. This file should not be touched by this plan.

### 2.6 Tests

Existing relevant tests in `maho-chromium/browser/ui/views/sidebar/`:

- `maho_sidebar_spaces_interactive_test.cc` covers space switch flows end-to-end through the footer.
- `maho_sidebar_contract_test.cc` covers the data-contract surface but not paint behaviour.
- `maho_sidebar_dnd_dispatch_unittest.cc` is the only file currently in the `unit_tests` source_set in `BUILD.gn`.

There is no `maho_sidebar_space_dot_view_unittest.cc` today. The `unit_tests` source_set at `BUILD.gn:140` already pulls in `//ui/views:test_support`, so adding a focused dot-view unit test fits naturally without GN scaffolding beyond a new source line.

---

## 3. Corrective Design Decisions

### 3.1 Active halo must paint independently of `dot_opacity_`

The minimal correct fix is to keep the dot circle draw gated on `dot_opacity_ > 0.0f`, but lift the active halo out from under that gate. The halo is a structural emphasis that should fire whenever `is_active_` is true, regardless of whether the dot itself is visible.

Concretely, the function should paint in this order:

1. Always call `views::View::OnPaint(canvas)`.
2. If `dot_opacity_ > 0.0f`, paint the fallback dot circle as today.
3. Independently, if `is_active_`, paint the halo.

This keeps the existing halo color, stroke width, radius offset, and the existing fallback dot color logic all unchanged. It does not reintroduce a fallback dot under the icon. It does not introduce new state on the view.

This is the smallest-surface-area fix that restores active emphasis for icon-bearing spaces. Any other shape (animating the icon label, scaling the icon label, color-tier changing the icon label) is rejected as out of scope here. If those are wanted, file separately.

### 3.2 Dot-size constants: keep and document, do not silently revert

Recommendation: **keep** `kDotSizeDefault = 4.0f`, `kDotSizeSelected = 4.8f`, `kDotSizeHoveredNoIcon = 5.8f`, `kDotSizeActive = 7.6f`. Document in a header comment that these only affect icon-less spaces and are tuned to read as a footer affordance at rest. Do not revert to the old `2.0 / 2.4 / 2.9 / 3.8` set silently as cleanup, because:

- The original "dots are too small" report still applies to icon-less spaces, which are now the only spaces using these constants.
- A revert would visually destabilise the row in the mixed icon-having + icon-less case, where the dot fallback would shrink while neighbouring glyphs stay the same effective size.
- A revert is a behaviour change disguised as cleanup; a keep-and-document is a cleanup move with no behaviour change.

If a future plan decides icon-less spaces should also shrink, it can do so with a clear visual rationale rather than as a side effect of this corrective plan.

### 3.3 No font-list change to `icon_label_`

`icon_label_` and `SpaceIconPickerButton`'s label both rely on the default `views::Label` font list today. macOS resolves emoji via Apple Color Emoji and most symbol glyphs via system symbol fallback. The unicode-icon remediation plan explicitly listed font-list customization as a deferred follow-up. This plan honours that. If a glyph fails to render in either surface, we revisit and change both surfaces in lock-step.

### 3.4 Class comment must match implementation

The `maho_sidebar_space_dot_view.h` class doc comment must stop saying "Hover still reveals the icon when one exists." Replace with a description that matches the live predicate: an icon-bearing space shows its glyph at rest in all states; icon-less spaces fall back to the dot circle, which has its own size tier per state.

---

## 4. Scope

### In scope

- Decoupling the active halo paint from the `dot_opacity_ <= 0.0f` early return in `OnPaint`.
- Documenting the size tier constants and updating the class comment in `maho_sidebar_space_dot_view.h`.
- Adding a new `maho_sidebar_space_dot_view_unittest.cc` to lock in the corrected paint and appearance behaviour.
- Wiring that unit test file into the existing `unit_tests` source_set in `maho-chromium/browser/ui/views/sidebar/BUILD.gn`.
- Targeted re-run guidance for the existing interactive tests as integration confidence (no new interactive tests).

### Out of scope

- Any change to `maho_sidebar_footer_view.cc`. The pipeline is correct.
- Any change to `maho_sidebar_state_adapter.cc`. The pipeline is correct.
- Any change to `maho_space_profile_bridge.{cc,h}`.
- Any change to the keyboard shortcut delivery path in `BrowserView`. That is Phase D of the revised plan and is unrelated.
- Any change in `maho/crates/maho-core/src/space_manager.rs`. Default-icon seeding remains deferred.
- Any font-list change on the dot view or the picker preview.
- Any GN target restructuring beyond adding one source line to the existing `unit_tests` source_set.
- Any WebUI surface. The space switcher is native Views.

---

## 5. File-by-File Implementation Plan

### 5.1 Required edits

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc`

`OnPaint(gfx::Canvas* canvas)` (currently lines 107-140) restructure:

- Keep `views::View::OnPaint(canvas)` as the first line.
- Replace the unconditional `if (dot_opacity_ <= 0.0f) return;` with a guarded fallback-dot block: only execute the dot fill (current lines 114-130, including `dot_size`, `radius`, `center`, fill color, alpha, `cc::PaintFlags`, and `canvas->DrawCircle(center, radius, flags)`) when `dot_opacity_ > 0.0f`.
- Hoist `center` to the function scope (or recompute) so the active halo block can use it without depending on the dot block having executed.
- Keep the active-halo block (current lines 132-139) outside that guard, and make it run whenever `is_active_` is true. The halo radius today is `radius + 0.8f`. With the new structure, when the dot fill did not run, `radius` is not in scope; recompute halo geometry from `GetCurrentDotSize() / 2.0f + 0.8f`. Preserve color `SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF)`, stroke style, and stroke width `1.0f` exactly.

No other functions in this file need to change. Do not touch `UpdateAppearance`. Do not touch `Configure`. Do not touch the mouse handlers. Do not introduce new state members or new constants.

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h`

- Update the class doc comment (currently lines 35-37) to describe the live behaviour: icon-bearing spaces show their glyph at rest in every interaction state; icon-less spaces show the fallback dot at a size tier that depends on `is_active_`, `is_selected_`, and `is_hovered_`.
- Add a one-line comment block above `kDotSizeDefault` (line 77) noting that these size tiers apply to icon-less fallback rendering only and were tuned to read as a footer affordance at rest. Do not change the constant values.
- No new members. No new methods. No reordering.

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view_unittest.cc` (new file)

Pure `views::View`-level unit test. Target gtest fixture: `views::ViewsTestBase` (already available via `//ui/views:test_support`, which is in the existing `unit_tests` source_set).

Cases to cover, all on a freshly constructed `MahoSidebarSpaceDotView`:

1. `Configure` with a non-empty icon and `is_active = false` results in `icon_label()->layer()->GetTargetOpacity()` going to `1.0f`. The internal `dot_opacity_` is not directly observable; assert through paint behaviour using a `gfx::Canvas` test seam if reasonable, or through behavioural proxies. If `dot_opacity_` is not exposed, the test should assert via `OnPaint` invocation count or a paint recording canvas; if that is too heavy, restrict the test to the icon label opacity assertion and document the limitation in the test comment.
2. `Configure` with an empty icon and `is_active = false`: icon label opacity target is `0.0f`.
3. `Configure` with a non-empty icon and `is_active = true`: icon label opacity target is `1.0f`. Active halo paints (verify via paint recording canvas or by exposing a small testing-only accessor; prefer the paint recording canvas approach to avoid widening the public API).
4. `Configure` with an empty icon and `is_active = true`: dot circle paints, halo paints, icon label opacity target is `0.0f`.
5. `SetActive(true)` after construction with a non-empty icon flips the halo on without changing icon label opacity.

If exposing a testing seam is unavoidable, use a `friend class` declaration rather than a public accessor. Document the seam in a comment so it is not mistaken for production API.

#### `maho-chromium/browser/ui/views/sidebar/BUILD.gn`

In the existing `source_set("unit_tests")` block (currently lines 140-152), append `"maho_sidebar_space_dot_view_unittest.cc"` to `sources`. Confirm the existing `deps` already cover what the new test needs:

- `//base`
- `//base/test:test_support`
- `//chrome/browser/ui` (covers `maho_sidebar_core` transitively if the dot view target is a public dep there; verify)
- `//testing/gtest`
- `//ui/base`
- `//ui/views:test_support`

Add a direct dep on the dot-view source_set if and only if `//chrome/browser/ui` does not already transit it. Do not refactor target boundaries to make this work. If the dep wiring needs more than one new line, stop and re-scope the test boundary instead of expanding the GN diff.

### 5.2 Optional follow-up edits

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

No edits unless visual QA against the corrected dot view shows the picker preview glyph reads at a noticeably different size or weight from the footer glyph. If a delta is found:

- Match `SpaceIconPickerButton`'s label sizing to the dot view's `icon_label_` effective bounds. Today the picker is 24x24 and the dot view's label region is `kIndicatorSize x kIndicatorSize` (11x11). A direct size match is not required; a perceptual match is. Document the chosen size in a one-line comment and leave the font path at the default.
- Do not change the picker's default `views::Label` font list. Font behaviour stays consistent with the dot view by remaining defaulted on both sides.

If no delta is found, leave this file untouched. The optional status is real, not aspirational.

### 5.3 Explicit no-change files

The following files are deliberately not edited by this plan. Any change to them must be filed as a separate plan.

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` (icon and active-id pipeline are correct).
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` (icon string passes through verbatim).
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc/.h` (browser-scoped active-id read is correct).
- `maho-chromium/browser/maho_space_profile_bridge.{cc,h}` (activation chokepoint, unaffected).
- `maho-chromium/browser/maho_command_action_handler.cc` (already routes through the bridge).
- `maho/crates/maho-core/src/space_manager.rs` (default-icon seeding is deferred).
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc` (no new test cases here; re-run only).
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc` (no new cases; re-run only).
- Any WebUI surface under `maho-chromium/browser/ui/webui/`.
- Any `chromium_src/` override.

---

## 6. Verification Plan

This plan does not claim any builds or tests have been run. The following describes what should be done when the implementation lands.

### 6.1 Static checks

- `lsp_diagnostics` clean on `maho_sidebar_space_dot_view.cc`, `maho_sidebar_space_dot_view.h`, `maho_sidebar_space_dot_view_unittest.cc`, and `BUILD.gn` after edits.
- No new warnings introduced in surrounding files.

### 6.2 Unit tests

Run the new unit test directly through the existing `unit_tests` source_set wiring. Each of the five cases in Section 5.1 must pass. If a case is degraded to a weaker assertion because exposing a testing seam was too invasive, that degradation must be documented in the test comment so the next reader understands the limitation.

### 6.3 Integration

- Re-run `maho_sidebar_spaces_interactive_test.cc` end-to-end. No new cases here; the integration confidence comes from the existing space-switch flows continuing to pass with the corrected paint behaviour.
- Re-run `maho_sidebar_contract_test.cc`. No paint coverage expected; this is a regression smoke check on the data contract.

### 6.4 Manual QA

1. Create one space with a Unicode icon and one without. Confirm:
   - Icon-bearing space at rest shows the glyph and no underlying gray dot.
   - Icon-bearing space when active shows the glyph plus the halo ring.
   - Icon-less space at rest shows the gray fallback dot at the documented size tier.
   - Icon-less space when active shows the larger fallback dot plus the halo ring.
2. Hover an icon-less space. Size tier increases per `kDotSizeHoveredNoIcon`.
3. Hover an icon-bearing space. Glyph stays visible, no fallback dot bleed-through.
4. Switch active between icon-bearing and icon-less spaces. Halo follows active correctly. No double-rendering of dot + glyph in the same row.
5. 1x and 2x display passes. Verify active halo radius reads correctly at both densities.
6. Light, dark, and themed-space passes. No new contrast regressions versus the pre-change tree.

---

## 7. Risks and Open Questions

### R1. Halo radius depends on `GetCurrentDotSize()` even when no dot is drawn

The halo radius is currently `radius + 0.8f` where `radius = GetCurrentDotSize() / 2.0f`. With the dot fill removed for icon-bearing active spaces, the halo's radius is still being computed from the icon-less size tier (`kDotSizeActive = 7.6f` so radius `~3.8f`, halo at `~4.6f`). That happens to land just inside `kIndicatorSize / 2` which is `5.5f`, so the halo will draw inside the icon label region. Verify visually that the halo sits sensibly around the glyph. If the halo reads too tight against the glyph, file a follow-up to introduce an explicit halo-radius constant rather than reusing the dot size tier. Do not introduce that constant in this plan; that is exactly the kind of scope creep this plan is trying to avoid.

### R2. Test seam vs production API surface

If the unit tests cannot assert paint outcomes without either a paint recording canvas or a `friend class`, the friend-class option is preferred over widening the public API of `MahoSidebarSpaceDotView`. Document the seam clearly. If neither option proves feasible without a heavier refactor, downgrade case 3 and 4 to icon-label-opacity assertions plus a `SchedulePaint` call count, and document the gap.

### R3. Decision drift on size constants

The size constants are kept here on the rationale that they apply only to icon-less fallback. If a later visual review concludes icon-less and icon-bearing rows need to look more uniform, the right move is to bring icon-bearing spaces up to a consistent footprint, not to shrink icon-less dots back to the old tier. Record that intent in the header comment so the next reader does not silently revert.

### R4. SpaceIconPickerButton drift

The picker preview is left unchanged here. If a future plan adds an explicit font list to `icon_label_`, the picker's label must change in lock-step. This is called out under Section 3.3 specifically because forgetting it would re-introduce a perceptual mismatch between picker and footer.

### Open question: should `OnPaint` paint the halo even when `is_selected_` but not `is_active_`?

Today the halo only paints for `is_active_`. With the icon now the rest-state for icon-bearing spaces, there is an argument that `is_selected_` (keyboard-driven candidate) should also have a softer halo to make selection legible without active. This plan does not make that change and explicitly defers it. Re-open after the corrective fix lands and the team has eyeballed the row in real use.

---

## 8. Notes

- This plan is corrective, not aspirational. The rest-state swap (icon at rest for icon-bearing spaces) is already in the tree. What is broken is the secondary effect of that swap on active emphasis.
- This plan does not edit GN beyond a single source line addition to an existing `unit_tests` source_set. It does not touch any `chromium_src/` override. It does not touch any WebUI surface.
- The "minimal active-halo fix" shape recorded in Section 3.1 is the explicitly recommended change shape. Any other approach (animating icon scale, color-tiering the glyph, introducing a dedicated active-state outline view, etc.) belongs in a separate visual plan.
- The size-constant question is answered explicitly: keep and document. Section 3.2 records the rationale so a later reader does not reopen the question without new evidence.
