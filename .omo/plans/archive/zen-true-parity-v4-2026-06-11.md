# Zen True Parity v4 — Favorites Grid Layout

**Status:** DRAFT — needs user/momus review before execution
**Date:** 2026-06-11
**Owner:** Sisyphus
**Supersedes:** zen-fluid-grid-layout-v3-2026-06-08.md (hack-type mapping inaccurate)

---

## 1. Goal

Achieve true Zen Browser parity for the essentials (favorites) grid layout in Maho's native sidebar. Plan v3 claimed Zen parity but user testing revealed 5+1 layout bugs exposing fundamental mapping errors. This plan corrects the hack-type lookup table, adds the column-transition animation, fixes drag image rendering, implements the `zen-dragover-background` element, corrects placeholder visuals, and adds missing CSS-equivalent container properties.

---

## 2. Zen Reference (Exact Line Citations)

### 2.1 Hack-Type Assignment
**File:** `/tmp/zen-ZenSpaceManager.mjs:2757-2769`

```js
let essentialHackType = 0;
if (essentialNumChildren === 6 || essentialNumChildren === 9) {
  essentialHackType = 1;
} else if (essentialNumChildren % 2 === 0 && essentialNumChildren < 8) {
  essentialHackType = 2;
} else if (essentialNumChildren === 5) {
  essentialHackType = 3;
}
```

### 2.2 CSS Grid Rules
**File:** `/tmp/zen-vtabs.css:1136-1145`

| data-hack-type | grid-template-columns |
|---|---|
| (none/default) | `repeat(auto-fit, minmax(max(23.7%, var(--min-wrap)), 1fr))` |
| 1 | `repeat(auto-fit, minmax(max(30%, var(--min-wrap)), auto))` |
| 2 | `repeat(auto-fit, minmax(max(23%, var(--min-wrap)), 1fr) minmax(max(23%, var(--min-wrap)), 1fr))` |
| 3 | `repeat(auto-fit, minmax(max(25%, 48px), 1fr))` |

`--min-essentials-width-wrap: calc(var(--tab-min-height) + 4px)` → **48px**.

### 2.3 Hack-Type → Count Lookup Table

| N (children) | hack-type | Effective min% | Typical result |
|---|---|---|---|
| 1 | (none) | 23.7% | 1 col (capped by count) |
| 2 | 2 | 23% twin | 2 cols (paired track) |
| 3 | (none) | 23.7% | 3 cols |
| 4 | 2 | 23% twin | 2 or 4 cols (width-dependent) |
| 5 | 3 | 25% | 4 cols max |
| 6 | 1 | 30% | 3 cols → [3,3] |
| 7 | (none) | 23.7% | 4 cols → [4,3] |
| 8 | (none) | 23.7% | 4 cols → [4,4] |
| 9 | 1 | 30% | 3 cols → [3,3,3] |
| 10 | (none) | 23.7% | 4 cols → [4,4,2] |
| 11 | (none) | 23.7% | 4 cols → [4,4,3] |
| 12 | (none) | 23.7% | 4 cols → [4,4,4] |

### 2.4 Grid Transition
**File:** `/tmp/zen-vtabs.css:1131-1133`
```css
transition: max-height 0.3s ease-out, grid-template-columns 0.3s ease-out;
```

### 2.5 Container Properties
**File:** `/tmp/zen-vtabs.css:1127-1172`
```css
overflow: hidden;
min-height: 2px;
gap: 4px;
scrollbar-width: thin;
min-width: calc(100% + var(--zen-toolbox-padding) * 2);
padding: 0 var(--zen-toolbox-padding);
display: grid;
position: absolute;
```

### 2.6 Drag Image
**File:** `/tmp/zen-dnd.js:161-224` — `#createDragImageForTabs`
Clones actual tab DOM node (not a painted mock). For essentials, constrains width/height to `getBoundingClientRect()` of the source tile.

**Cross-window:** `/tmp/zen-dnd.js:922-943` — `_browserDragImageWrapper`
A 200×130px vbox with a **canvas snapshot** of the browser viewport (not a mock card).

### 2.7 Placeholder
**File:** `/tmp/zen-dnd.js:1484-1525` — `_fakeEssentialTab`
A bare `vbox` (empty XUL element) appended to `.zen-essentials-container`. No favicon, no label, no content.

### 2.8 Dragover Background
**File:** `/tmp/zen-dnd.js:1206-1235` + `/tmp/zen-vtabs.css:1372-1384`
```css
#zen-dragover-background {
  position: absolute; z-index: -1;
  width: calc(100% + var(--zen-toolbox-padding));
  background: var(--zen-primary-color);
  pointer-events: none;
}
```
Height + top set inline from JS (`element.getBoundingClientRect()` of drop target ±2px margin).

---

## 3. Current Maho State (Line Citations)

### 3.1 Hack-Type Logic
**File:** `maho_sidebar_favorites_grid_view.cc:828-836`
```cpp
if (count == 1) {
  int type1_min = std::max(available_width * 30 / 100, kMinWidth);
  max_columns = std::max(1, (available_width + kGap) / (type1_min + kGap));
} else if (count == 2) {
  max_columns = std::min(max_columns, 2);
}
```
**Bug:** N=1 gets 30% (should be default 23.7%). N=2 gets twin cap (correct result but wrong logic path). N=4,5,6,9 get no special treatment (wrong).

### 3.2 Grid Rebuild
**File:** `maho_sidebar_favorites_grid_view.cc:886-954`
`RebuildGridRows()` is instant — no transition/animation on column-count changes.

### 3.3 Drag Image Factory
**File:** `maho_sidebar_drag_image_factory.cc:22-113` and `115-161`
- `BuildTabStackDragImage()`: Paints hardcoded bg/border/text colors
- `BuildEssentialsDragImage()`: Hardcoded RGBA fill (250,250,250 alpha=245)
- Cross-window: `cross_window_image_ = tab_stack_image_` (line 185 of controller) — reuses same painted mock

### 3.4 Placeholder
**File:** `maho_sidebar_favorites_grid_view.cc` (ActivateDragPlaceholder)
Creates a full tile with favicon + title resolved from the dragged tab.

### 3.5 Container Properties
`content_view_` is a raw `views::View` with no explicit overflow clip, no minimum height, no scrollbar-width equivalent.

### 3.6 Layout Tokens
**File:** `maho_sidebar_layout_tokens.h:86-94`
```cpp
kFavoriteTileHeightDp = 44;
kFavoriteTileGapDp = 4;
kMinFavoriteTileWidthDp = 48;
```

---

## 4. Gap Analysis (10 Items)

| # | Gap | Severity | Zen Ref | Maho Ref |
|---|---|---|---|---|
| 1 | **Hack-type mapping completely wrong** | P0/Critical | ZenSpaceManager.mjs:2757-2769 | grid_view.cc:828-836 |
| 2 | **No column transition animation** | P1/High | vtabs.css:1131-1133 | RebuildGridRows instant |
| 3 | **Drag image hardcoded colors** | P2/Medium | dnd.js:161-224 (DOM clone) | drag_image_factory.cc:40-46 |
| 4 | **Cross-window image is mock card** | P3/Low | dnd.js:922-943 (canvas snapshot) | controller.cc:185 |
| 5 | **Container CSS properties missing** | P1/High | vtabs.css:1127-1172 | No clip/overflow/min-height |
| 6 | **Placeholder too rich** | P2/Medium | dnd.js:1484 (empty vbox) | Full tile w/favicon+title |
| 7 | **Cross-platform unverified** | P3/Low | N/A | Win/Linux 0 validation |
| 8 | **zen-dragover-background missing** | P2/Medium | dnd.js:1206-1235 + vtabs.css:1372-1384 | No equivalent |
| 9 | **Linux GTK 0.5 opacity** | P3/Low | drag_image_factory.cc:53-55 | Present but untested |
| 10 | **auto-fit vs max_cols packing** | P2/Medium | CSS auto-fit collapses empty tracks | C++ packing fills from top |

---

## 5. Architecture Changes

### 5.1 Gap #1: Correct Hack-Type Mapping

Replace `GetLayoutSpec()` lines 828-836 with:

```cpp
enum class HackType { kNone = 0, kType1 = 1, kType2 = 2, kType3 = 3 };
HackType hack_type = HackType::kNone;
if (count == 6 || count == 9) {
  hack_type = HackType::kType1;
} else if (count % 2 == 0 && count < 8) {
  hack_type = HackType::kType2;
} else if (count == 5) {
  hack_type = HackType::kType3;
}

int effective_min_cell;
switch (hack_type) {
  case HackType::kType1:
    effective_min_cell = std::max(available_width * 30 / 100, kMinWidth);
    break;
  case HackType::kType2:
    effective_min_cell = std::max(available_width * 23 / 100, kMinWidth);
    max_columns = std::max(1, (available_width + kGap) / (effective_min_cell + kGap));
    if (max_columns % 2 != 0 && max_columns > 1) max_columns--;
    break;
  case HackType::kType3:
    effective_min_cell = std::max(available_width * 25 / 100, kMinWidth);
    break;
  case HackType::kNone:
  default:
    effective_min_cell = default_min_cell;
    break;
}
if (hack_type != HackType::kType2) {
  max_columns = std::max(1, (available_width + kGap) / (effective_min_cell + kGap));
}
```

**Expected results at width=288 (kDefaultRailWidthDp - insets):**

| N | hack | min_cell | max_cols | row_counts |
|---|---|---|---|---|
| 1 | none | 68 (23.7%) | 1 (capped) | [1] |
| 2 | type2 | 66 (23%) | 4→even=4 (capped to 2) | [2] |
| 3 | none | 68 | 4 (capped 3) | [3] |
| 4 | type2 | 66 | 4 (even) | [4] |
| 5 | type3 | 72 (25%) | 3 | [3,2] |
| 6 | type1 | 86 (30%) | 3 | [3,3] |
| 7 | none | 68 | 4 | [4,3] |
| 8 | none | 68 | 4 | [4,4] |
| 9 | type1 | 86 | 3 | [3,3,3] |
| 10 | none | 68 | 4 | [4,4,2] |
| 11 | none | 68 | 4 | [4,4,3] |
| 12 | none | 68 | 4 | [4,4,4] |

### 5.2 Gap #2: Column Transition Animation

**Problem:** `RebuildGridRows()` destroys/recreates row views instantly. No visual interpolation.

**Solution:** FLIP-style column transition when `max_columns` changes:
1. Before `RebuildGridRows()`: snapshot each tile's screen bounds
2. After rebuild: compute new bounds
3. Apply inverse transform (old→new offset), animate to identity over 300ms ease-out

This matches Zen's `grid-template-columns 0.3s ease-out` visually.

**New method:** `AnimateColumnReflow(const LayoutSpec& old_spec)`
- Only fires when `old_spec.max_columns != new_spec.max_columns`
- Uses compositor-thread layer transforms (same infrastructure as existing `AnimateReorder()`)

### 5.3 Gap #3: Drag Image Theme-Aware Colors

Replace hardcoded colors in `BuildTabStackDragImage()` and `BuildEssentialsDragImage()` with theme-derived values:

```cpp
SkColor bg = color_provider->GetColor(kColorTabBackgroundActiveFrameActive);
SkColor border = color_provider->GetColor(kColorFocusableBorderFocused);
SkColor text = color_provider->GetColor(kColorTabForegroundActiveFrameActive);
```

Pass `ui::ColorProvider*` as parameter to all `Build*DragImage()` functions.

### 5.4 Gap #4: Cross-Window Canvas Snapshot

**Deferred to v5.** Zen's `_browserDragImageWrapper` uses an actual browser canvas snapshot. Chromium's `ui::OSExchangeData` drag image API doesn't natively support live canvas capture during cross-window drags on macOS without significant platform work.

**v4 mitigation:** Use the tab-stack drag image. Document as known divergence.

### 5.5 Gap #5: Container CSS Properties

```cpp
content_view_->SetPaintToLayer();
content_view_->layer()->SetMasksToBounds(true);
content_view_->SetPreferredSize(gfx::Size(1, 2));
```

`min-width` and padding are already implicitly handled by the parent layout. `scrollbar-width: thin` has no Views equivalent (no scroll in the grid — max 12 tiles fits without scrolling).

### 5.6 Gap #6: Simplified Placeholder

Replace `ActivateDragPlaceholder()` tile content: instead of resolving favicon+title from the dragged tab, create a **minimal empty tile** matching Zen's bare `vbox`:

```cpp
void MahoSidebarFavoritesGridView::ActivateDragPlaceholder(
    const SidebarDragPayload& payload, int slot) {
  placeholder_active_ = true;
  drag_placeholder_index_ = slot;
  SetPlaceholderTileMinimal(slot);
  RebuildGridRows(favorite_count_ + 1);
  UpdateContainerOwnedPreferredHeight(favorite_count_ + 1);
}
```

### 5.7 Gap #7: Cross-Platform Verification

**Not blocking v4.** Add `TODO(parity)` comments at:
- `maho_sidebar_drag_image_factory.cc:53` (Linux GTK alpha)
- Build script override registrations

Validation deferred to CI integration milestone.

### 5.8 Gap #8: Drag-Over Background Highlight

Add `drag_over_background_` overlay view as child of `content_view_` at z-index -1:

```cpp
drag_over_bg_ = content_view_->AddChildViewAt(
    std::make_unique<views::View>(), 0);
drag_over_bg_->SetPaintToLayer();
drag_over_bg_->layer()->SetFillsBoundsOpaquely(false);
drag_over_bg_->SetVisible(false);
drag_over_bg_->SetBackground(views::CreateThemedRoundedRectBackground(
    kColorMahoSidebarAccent, kFavoriteTileCornerRadiusDp));
```

Activate/deactivate in `OnDragUpdated()`/`OnDragExited()`:
- On drag enter: `drag_over_bg_->SetVisible(true)`, size to grid bounds + padding
- On drag exit: `drag_over_bg_->SetVisible(false)`

### 5.9 Gap #9: Linux GTK 0.5 Opacity

Already implemented at `maho_sidebar_drag_image_factory.cc:53-55`:
```cpp
if (is_linux_gtk) {
  alpha_multiplier *= 0.5f;
}
```
Mark as verified-by-code, untested-at-runtime.

### 5.10 Gap #10: Auto-Fit Empty Track Collapse

CSS `auto-fit` collapses empty grid tracks. Maho's BoxLayout spacers do the same job. The semantic difference only matters if we ever expose the column count to external consumers (we don't).

**No code change needed.** Add a unit test to verify spacer behavior matches CSS auto-fit visually.

---

## 6. File Diff Plan

| File | Change Type | Description |
|---|---|---|
| `maho_sidebar_favorites_grid_view.cc` | **Modify** | Rewrite GetLayoutSpec hack-type (§5.1), add AnimateColumnReflow (§5.2), simplify placeholder (§5.6), add dragover bg (§5.8), add overflow clip (§5.5) |
| `maho_sidebar_favorites_grid_view.h` | **Modify** | Add `HackType` enum, `AnimateColumnReflow()`, `drag_over_bg_` member, `SetPlaceholderTileMinimal()` |
| `maho_sidebar_layout_tokens.h` | **No change** | Tokens already correct (gap=4, min=48, height=44) |
| `maho_sidebar_drag_image_factory.h` | **Modify** | Add `ui::ColorProvider*` param to all Build functions |
| `maho_sidebar_drag_image_factory.cc` | **Modify** | Replace hardcoded colors with ColorProvider lookups (§5.3) |
| `maho_sidebar_drag_image_controller.cc` | **Modify** | Pass ColorProvider to factory calls |
| `maho_sidebar_favorites_grid_layout_test.cc` | **Modify** | Add 12+ new tests for correct hack-type mapping, column reflow, dragover bg |
| `maho_sidebar_drag_image_factory_unittest.cc` | **Modify** | Update test expectations for theme-aware colors (mock ColorProvider) |

---

## 7. Phasing (TDD-Ordered, Atomic Commits)

### Phase 1: Test — Correct Hack-Type Expectations
**Commit:** `test(sidebar): add hack-type parity tests for N=1..12`

Write failing tests that assert the **correct** Zen hack-type mapping for N=1..12 at width=200 and width=288.

**Est:** 1h

### Phase 2: Impl — Correct Hack-Type Logic
**Commit:** `fix(sidebar): implement correct Zen hack-type mapping for N=1..12`

Replace `GetLayoutSpec()` lines 828-836 with the full hack-type switch (§5.1). All Phase 1 tests must pass.

**Est:** 0.5h

### Phase 3: Test — Type-2 Even Column Enforcement
**Commit:** `test(sidebar): type-2 even column enforcement at varied widths`

Tests that type-2 (N=2,4) always produces even `max_columns`.

**Est:** 0.5h

### Phase 4: Impl — Container Overflow Clip
**Commit:** `feat(sidebar): add overflow clip and min-height to favorites grid`

Apply §5.5 changes.

**Est:** 0.25h

### Phase 5: Test — Column Reflow Animation
**Commit:** `test(sidebar): column reflow FLIP animation fires on column change`

Test that when `RebuildGridRows` changes `max_columns`, layer transforms are applied.

**Est:** 1h

### Phase 6: Impl — Column Reflow Animation
**Commit:** `feat(sidebar): FLIP column reflow animation (300ms ease-out)`

Implement `AnimateColumnReflow()` per §5.2.

**Est:** 1.5h

### Phase 7: Impl — Simplified Placeholder
**Commit:** `fix(sidebar): simplify drag placeholder to empty tile (Zen parity)`

Replace rich placeholder with minimal rounded-rect shell per §5.6.

**Est:** 0.5h

### Phase 8: Impl — Drag-Over Background
**Commit:** `feat(sidebar): add drag-over background highlight (zen-dragover-background parity)`

Add `drag_over_bg_` overlay view per §5.8.

**Est:** 1h

### Phase 9: Test — Drag Image Theme Awareness
**Commit:** `test(sidebar): drag image uses ColorProvider colors`

Mock `ui::ColorProvider`, assert no hardcoded colors.

**Est:** 0.5h

### Phase 10: Impl — Drag Image Theme Colors
**Commit:** `fix(sidebar): drag images use theme-derived colors`

Modify factory + controller per §5.3.

**Est:** 1h

### Phase 11: Test — Full Integration Layout
**Commit:** `test(sidebar): integration test for grid layout at all counts 1-12`

Parameterized test, count 1–12, exact `max_columns` and `row_counts` vectors.

**Est:** 0.5h

### Phase 12: Polish — Auto-Fit Equivalence Verification
**Commit:** `test(sidebar): verify spacer-based layout matches CSS auto-fit semantics`

**Est:** 0.25h

**Total: ~9h**

---

## 8. Tests

### Unit Tests (New/Modified)

| Test Name | Phase |
|---|---|
| `HackType_N{1..12}_Width200` | 1 |
| `HackType_N{1..12}_Width288` | 1 |
| `Type2_EvenColumns_Width150` | 3 |
| `Type2_EvenColumns_Width288` | 3 |
| `Type2_EvenColumns_Width400` | 3 |
| `ColumnReflow_AnimationFires` | 5 |
| `ColumnReflow_NoAnim_SameColumns` | 5 |
| `DragImage_UsesColorProvider` | 9 |
| `DragImage_DarkTheme` | 9 |
| `Integration_AllCounts_Width288` | 11 |
| `SpacerEquivalence_AutoFit` | 12 |

### Existing Tests (Must Not Regress)

| Test | Risk | Action |
|---|---|---|
| `FluidColumns_Width200_Returns3` | Low (count=6 → type1 → 3 cols same) | Verify |
| `FluidColumns_Width250_Returns4` | High (count=6 → type1 → 3 cols, not 4) | Update expected |
| `FluidColumns_CapByCount` | Low (count=2 → type2 → 2 cols same) | Verify |
| `FluidColumns_SingleItem_Type1` | High (rename + change semantics) | Rename to `_Default`, expect 1 col |
| `CellWidth_3cols_200w` | Medium | Verify |
| `CellWidth_4cols_250w` | High | Update expected |
| `Placeholder_*` | Low | Verify |
| `CompactMode_*` | None | Untouched |

---

## 9. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Existing tests break due to hack-type change | High | Phase 1 adds correct tests first; Phase 2 updates old tests alongside impl |
| Column reflow animation jank on low-end | Medium | 300ms is short; compositor-thread transforms are cheap |
| Drag-over bg color mismatch with space themes | Medium | Use `kColorMahoSidebarAccent` from ColorProvider |
| Type-2 even enforcement produces 2 cols when 4 looks better | Low | Matches Zen exactly |
| Cross-window drag image divergence remains | Accepted | Deferred to v5 (§5.4) |

---

## 10. Acceptance Criteria

### Functional
- **F1:** At sidebar width 288dp, N=6 produces exactly 3 columns (not 4)
- **F2:** At sidebar width 288dp, N=4 produces exactly 4 columns (even, type-2)
- **F3:** At sidebar width 288dp, N=5 produces exactly 3 columns (type-3, 25% min)
- **F4:** At sidebar width 288dp, N=9 produces exactly 3 columns (type-1, 30% min)
- **F5:** N=1 at any width produces 1 column (count cap, not type-1)
- **F6:** Placeholder shows as empty rounded rect (no favicon/title)
- **F7:** Drag-over background appears during external drag

### Animation
- **A1:** Column count change triggers 300ms ease-out FLIP on all tiles
- **A2:** Height change triggers 300ms ease-out clip animation (existing, unchanged)
- **A3:** No animation when layout spec is unchanged

### Visual
- **V1:** `content_view_` clips children (no overflow bleed during height animation)
- **V2:** Drag images reflect current theme colors (dark/light)
- **V3:** Drag-over background uses space accent color

### Test
- **T1:** All 12 hack-type tests pass (both widths)
- **T2:** Type-2 even-enforcement tests pass
- **T3:** Column reflow animation test passes
- **T4:** No regression in existing test suite

---

## 11. Open Questions

| # | Question | Default if Unresolved |
|---|---|---|
| 1 | Should type-2 at very narrow widths (e.g., 120dp) where even=2 is the only option, still enforce N=4 → 2 cols? | Yes — Zen behavior |
| 2 | Drag-over background: full grid extent or only occupied rows? | Full grid (Zen uses drop target element bounds) |
| 3 | Should we add `opacity: 1` explicitly to content_view? (Zen has it) | No — Views default is fully opaque |
| 4 | `position: absolute` in Zen container — do we need z-stacking? | No — handled by Views child ordering |
| 5 | Cross-window canvas snapshot (gap #4) — defer to v5 or attempt? | Defer to v5 |

---

## Summary of Key Corrections vs v3

- N=1 no longer gets type-1 (30%) — it's default (23.7%), capped to 1 col by count
- N=4 gets type-2 (even columns), not default
- N=5 gets type-3 (25%), not default
- N=6 and N=9 get type-1 (30% → 3 cols), not default
- Column transition animation added (was completely absent)
- Placeholder simplified to empty shell
- Drag-over background element added
