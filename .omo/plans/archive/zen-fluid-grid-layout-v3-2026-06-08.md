# Zen Parity Fluid Grid Layout — v3

**Date:** 2026-06-08
**Owner:** Sisyphus
**Status:** DRAFT — needs user/momus review before execution
**Goal:** Replace Maho's fixed 3-column favorites grid with a Zen-style fluid CSS Grid equivalent using `auto-fit` column logic, per-width cell distribution, animated layout transitions, and compact mode collapse.

---

## 0. Zen Reference Specification (line-cited)

All design decisions are grounded in the following Zen Browser CSS (`/tmp/zen-vtabs.css`).

### 0.1 Main Grid Container (L1127-1151)

```css
.zen-essentials-container {
  overflow: hidden;
  min-height: 2px;
  gap: 4px;
  transition: max-height 0.3s ease-out, grid-template-columns 0.3s ease-out;
  opacity: 1;
  --min-essentials-width-wrap: calc(var(--tab-min-height) + 4px);  /* 44+4 = 48px */
  grid-template-columns: repeat(auto-fit, minmax(max(23.7%, var(--min-essentials-width-wrap)), 1fr));
  display: grid;
}
```

**Key behavior:** columns auto-fit into available width. Minimum cell width = `max(23.7% of container, 48px)`. Equal 1fr distribution fills remaining space.

### 0.2 data-hack-type Variations (L1137-1145)

| Variant | Rule | Effect |
|---------|------|--------|
| `[data-hack-type="1"]` | `repeat(auto-fit, minmax(max(30%, --min-wrap), auto))` | Wider min (30%), auto max — items don't stretch to fill |
| `[data-hack-type="2"]` | `repeat(auto-fit, minmax(max(23%, --min-wrap), 1fr) minmax(...))` | Twin-column template |
| `[data-hack-type="3"]` | `repeat(auto-fit, minmax(max(25%, 48px), 1fr))` | Stable 4-col at standard sidebar widths |

### 0.3 Compact/Collapsed Mode (L733-740)

```css
#navigator-toolbox:not([zen-sidebar-expanded="true"]) {
  & .zen-essentials-container {
    justify-content: center;
    grid-template-columns: 1fr !important;  /* single column forced */
    padding: 0 !important;
    max-width: var(--zen-sidebar-width) !important;
  }
}
```

### 0.4 Transitions

| Property | Duration | Easing |
|----------|----------|--------|
| `max-height` | 0.3s | ease-out |
| `grid-template-columns` | 0.3s | ease-out |

---

## 1. Maho Current State

### 1.1 Layout Constants (`maho_sidebar_layout_tokens.h`)

| Constant | Line | Value |
|----------|------|-------|
| `kMahoSidebarFavoriteSlotCount` | L85 | 10 |
| `kFavoritesColumns` | L86 | 3 (fixed) |
| `kFavoriteTileHeightDp` | L87 | 44dp |
| `kFavoriteTileCornerRadiusDp` | L88 | 12dp |
| `kFavoriteTileSpacingDp` | L89 | 6dp |

**No `kFavoriteTileWidthDp`** — tile width is flex-distributed (BoxLayout flex=1).

### 1.2 GetLayoutSpec (`maho_sidebar_favorites_grid_view.cc:766-795`)

```cpp
LayoutSpec GetLayoutSpec(size_t favorite_count, int available_width) {
  const size_t count = std::min(favorite_count, kTileCount);
  LayoutSpec spec;
  switch (count) {
    case 0: break;
    case 1: spec.row_counts = {1}; spec.max_columns = 3; break;
    case 2: spec.row_counts = {2}; spec.max_columns = 3; break;
    default:
      spec.max_columns = 3;
      for (size_t remaining = count; remaining > 0;) {
        spec.row_counts.push_back(std::min<int>(kFavoritesColumns, remaining));
        remaining -= spec.row_counts.back();
      }
      break;
  }
  return spec;
}
```

**Problem:** `max_columns` hardcoded to 3. `available_width` accepted but **never used**.

### 1.3 RebuildGridRows (`.cc:841-905`)

Creates horizontal `BoxLayout` row views. Tiles get `flex=1`, trailing filler spacers `flex=1`. Spacing = `kFavoriteTileSpacingDp` (6dp).

### 1.4 GetPreferredHeightForWidth (`.cc:797-819`)

Sums row heights (all 44dp) + inter-row spacing. `kFavoriteSingleCardHeightDp == kFavoriteTwoTileHeightDp == 44dp` (L86-87 anonymous namespace).

### 1.5 OnBoundsChanged (`.cc:530-541`)

Triggers `RebuildGridRows` + `UpdateContainerOwnedPreferredHeight` on size change — but since GetLayoutSpec ignores width, columns stay at 3.

### 1.6 Animation Infrastructure

Uses `views::AnimationBuilder` FLIP pattern (L1558-1631): `SnapshotCurrentBounds()` → layout change → `AnimateReorder()` (220ms EASE_IN_OUT).

### 1.7 Compact Mode Detection

`sidebar_prefs::IsSidebarPanelExpanded(prefs)` via `MahoSidebarContainerView::IsAutoHideMode()` (container_view.cc:431). Rail width: 40dp. **No grid compact mode exists.**

---

## 2. Gap Analysis

| # | Gap | Zen | Maho |
|---|-----|-----|------|
| G1 | Fixed columns | auto-fit fluid | `max_columns = 3` hardcoded |
| G2 | No width-adaptive cell sizing | 1fr equal distribution | flex=1 works but column count static |
| G3 | No layout transition | 0.3s ease-out | Snaps instantly |
| G4 | No compact mode | `1fr` in collapsed sidebar | No single-column fallback |
| G5 | Placeholder doesn't expand grid visually | Grid grows naturally | Height-only hack, no column adaptation |
| G6 | Gap mismatch | 4px | 6dp |
| G7 | No hack-type mapping | 3 hack types for edge cases | Single switch statement |

---

## 3. Architecture Change

### 3.1 Core Algorithm — Fluid Column Calculation

```cpp
// Zen: repeat(auto-fit, minmax(max(23.7%, min_tile_width), 1fr))
// Native equivalent: max_columns = floor((width + gap) / (min_tile_width + gap))
// min_tile_width = kFavoriteTileHeightDp + 4 = 48dp
// gap = 4dp

constexpr int kMinFavoriteTileWidthDp = 48;  // NEW
constexpr int kFavoriteTileGapDp = 4;        // NEW (Zen: gap: 4px)

int max_columns = std::max(1, (available_width + kGap) / (kMinWidth + kGap));
```

### 3.2 LayoutSpec Extensions

```cpp
struct LayoutSpec {
  std::vector<int> row_counts;
  int max_columns = 1;
  int cell_width = 0;            // NEW: computed dp width per cell
  int gap = kFavoriteTileGapDp;  // NEW: inter-cell gap
  bool is_compact_mode = false;  // NEW: single-column forced
  bool uses_compact_row_card = false;  // DEPRECATED (never true)
};
```

### 3.3 Compact Mode

When `IsAutoHideMode()` = true → `max_columns = 1`, `is_compact_mode = true`.

### 3.4 Hack-Type Mapping

| Items | Zen Type | Maho Rule |
|-------|----------|-----------|
| 1 | type-1 | min_width = `max(30% * width, 48)` → usually 1 col |
| 2 | type-2 | cap `max_columns = min(fluid_cols, 2)` |
| 3+ | default | standard fluid formula |

### 3.5 Layout Transition (FLIP + Height)

Existing `AnimateReorder()` already provides tile-position FLIP. Add height animation:

```cpp
constexpr base::TimeDelta kGridResizeDuration = base::Milliseconds(300);
// Clip-rect animation on content_view_ layer from old_height to new_height
```

### 3.6 Placeholder Grid Expansion

Treat as N+1 items. Fluid algorithm naturally determines if a new row is needed. Combined with height animation → visible expansion.

---

## 4. File Diff Plan

### Modified Files

| File | Changes |
|------|---------|
| `maho_sidebar_layout_tokens.h` | +2 constants: `kMinFavoriteTileWidthDp=48`, `kFavoriteTileGapDp=4` |
| `maho_sidebar_favorites_grid_view.h` | LayoutSpec: +`cell_width`, +`gap`, +`is_compact_mode`; +`IsCompactMode() const`; +`AnimateHeightChange(int,int)` |
| `maho_sidebar_favorites_grid_view.cc:766-795` | Full rewrite of `GetLayoutSpec` → fluid algorithm |
| `maho_sidebar_favorites_grid_view.cc:797-819` | Simplify `GetPreferredHeightForWidth` → use `spec.gap` |
| `maho_sidebar_favorites_grid_view.cc:841-905` | `RebuildGridRows` → use `spec.gap`, `spec.cell_width` |
| `maho_sidebar_favorites_grid_view.cc:530-541` | `OnBoundsChanged` → add Snapshot + AnimateReorder |
| `maho_sidebar_favorites_grid_view.cc:821-839` | `UpdateContainerOwnedPreferredHeight` → integrate height animation |
| `maho_sidebar_favorites_grid_view.cc` anonymous | +`kGridResizeDuration`, remove `kFavoriteSingleCardHeightDp`/`kFavoriteTwoTileHeightDp` aliases |

### New Files

| File | Purpose |
|------|---------|
| `maho_sidebar_favorites_grid_layout_test.cc` | Unit tests for fluid algorithm, compact mode, height calc |

---

## 5. Detailed GetLayoutSpec Rewrite

```cpp
MahoSidebarFavoritesGridView::LayoutSpec
MahoSidebarFavoritesGridView::GetLayoutSpec(size_t favorite_count,
                                           int available_width) {
  const size_t count = std::min(favorite_count, kTileCount);
  LayoutSpec spec;
  if (count == 0 || available_width <= 0) return spec;

  constexpr int kGap = sidebar_layout::kFavoriteTileGapDp;
  constexpr int kMinWidth = sidebar_layout::kMinFavoriteTileWidthDp;

  // Compact mode: single column (Zen L733-740)
  if (IsCompactMode()) {
    spec.max_columns = 1;
    spec.is_compact_mode = true;
    spec.gap = kGap;
    spec.cell_width = available_width;
    spec.row_counts.assign(count, 1);
    return spec;
  }

  // Fluid column calculation
  int max_columns = std::max(1, (available_width + kGap) / (kMinWidth + kGap));

  // Hack-type adjustments
  if (count == 1) {
    int type1_min = std::max(available_width * 30 / 100, kMinWidth);
    max_columns = std::max(1, (available_width + kGap) / (type1_min + kGap));
  } else if (count == 2) {
    max_columns = std::min(max_columns, 2);
  }

  // Don't allocate more columns than items
  max_columns = std::min<int>(max_columns, count);

  spec.max_columns = max_columns;
  spec.gap = kGap;
  spec.cell_width = (available_width - (max_columns - 1) * kGap) / max_columns;

  for (size_t remaining = count; remaining > 0;) {
    const int row_count = std::min<int>(max_columns, remaining);
    spec.row_counts.push_back(row_count);
    remaining -= row_count;
  }
  return spec;
}
```

---

## 6. Phasing & Atomic Commit Strategy

| Phase | Deliverable | Commit | Est. |
|-------|-------------|--------|------|
| 1 | Add constants to layout_tokens.h | `feat(sidebar): add fluid grid constants` | 0.5h |
| 2 | Extend LayoutSpec struct | `refactor(sidebar): extend LayoutSpec for fluid grid` | 0.5h |
| 3 | **TDD:** Write unit tests for GetLayoutSpec | `test(sidebar): add fluid grid layout spec tests` | 1.5h |
| 4 | Rewrite GetLayoutSpec | `feat(sidebar): fluid auto-fit column calculation` | 1h |
| 5 | Simplify GetPreferredHeightForWidth | `refactor(sidebar): simplify height calc with spec.gap` | 0.5h |
| 6 | Update RebuildGridRows | `feat(sidebar): dynamic gap and cell width in grid rows` | 1h |
| 7 | **TDD:** Write compact mode tests | `test(sidebar): compact mode single-column tests` | 0.5h |
| 8 | Implement IsCompactMode() | `feat(sidebar): compact mode detection for favorites grid` | 0.5h |
| 9 | **TDD:** Write height transition tests | `test(sidebar): grid height animation tests` | 0.5h |
| 10 | Implement AnimateHeightChange | `feat(sidebar): animated grid height transitions (300ms)` | 1h |
| 11 | Integrate FLIP on resize | `feat(sidebar): FLIP animation on grid column reflow` | 1h |
| 12 | Placeholder expansion | `fix(sidebar): placeholder expands grid visually during drag` | 1h |
| 13 | Hack-type edge cases | `feat(sidebar): Zen hack-type mapping for 1/2 items` | 0.5h |
| 14 | Interactive test | `test(sidebar): interactive test for drag grid expansion` | 1h |
| 15 | Visual QA + polish | `fix(sidebar): grid layout visual polish` | 1h |

**Total: ~12 hours.** TDD commits precede implementation commits (3→4, 7→8, 9→10).

---

## 7. Tests

### Unit Tests — Fluid Algorithm (Phase 3)

| Test | width | count | Expected max_columns |
|------|-------|-------|---------------------|
| `FluidColumns_Width200_Returns3` | 200 | 6 | 3 |
| `FluidColumns_Width250_Returns4` | 250 | 6 | 4 |
| `FluidColumns_Width100_Returns1` | 100 | 6 | 1 |
| `FluidColumns_CapByCount` | 250 | 2 | 2 (twin cap) |
| `FluidColumns_SingleItem_Type1` | 200 | 1 | 1 (30% rule) |
| `FluidColumns_ZeroWidth` | 0 | 5 | empty spec |

### Unit Tests — Cell Width

| Test | Cols | Width | Expected cell_width |
|------|------|-------|---------------------|
| `CellWidth_3cols_200w` | 3 | 200 | (200-8)/3 = 64 |
| `CellWidth_4cols_250w` | 4 | 250 | (250-12)/4 = 59 |

### Unit Tests — Height

| Test | Rows | Expected |
|------|------|----------|
| `Height_2rows` | 2 | 2*44+4 = 92 |
| `Height_3rows` | 3 | 3*44+8 = 140 |

### Unit Tests — Compact & Placeholder

- `CompactMode_ForcedSingleColumn`
- `Placeholder_AddsRow_WhenNeeded`
- `Placeholder_NoExtraRow_WhenFits`

### Interactive Tests (Phase 14)

- `DragEntryExpandsGridVisible` — drag into grid → height increases + animation
- `ResizeSidebarReflowsColumns` — resize 200→300dp → columns change + FLIP

---

## 8. Risks & Mitigations

| # | Risk | Sev | Mitigation |
|---|------|-----|-----------|
| R1 | Integer rounding in cell_width | Med | Remainder to last cell; invisible at retina |
| R2 | FLIP conflicts with drag-preview | Med | CancelAnimations() before reflow; drag-preview takes priority |
| R3 | Compact detection via view-tree walk fragile | Low | Null-checks + fallback to 3 columns |
| R4 | Gap 6→4dp breaks golden screenshots | Low | Update baselines in same commit |
| R5 | Height clip-rect conflicts with tile layers | Med | Apply clip on content_view_ layer, not tiles |
| R6 | Rapid resize thrashing | Med | Debounce: skip reflow if width delta < 24dp |
| R7 | `kFavoriteTileSpacingDp` used elsewhere | Low | Keep it; grid uses new `kFavoriteTileGapDp` |

---

## 9. Acceptance Criteria

### Functional
- [F1] Column count adapts to sidebar width dynamically.
- [F2] Min cell width = 48dp. No cell smaller.
- [F3] Gap = 4dp.
- [F4] Collapsed sidebar → single column.
- [F5] Drag placeholder causes visible grid expansion.
- [F6] 1-item: wider min (30% rule). 2-item: twin column cap.
- [F7] Cell width = `(container - (cols-1)*gap) / cols`.

### Animation
- [A1] Tile reflow animates (FLIP, 220ms EASE_IN_OUT).
- [A2] Height changes animate (300ms EASE_OUT).
- [A3] Drag placeholder triggers both reorder + height animation.

### Visual Parity
- [V1] 250dp sidebar → 4 columns (Zen match).
- [V2] 180dp sidebar → 3 columns.
- [V3] Collapsed → 1 column.
- [V4] No visual snapping between column counts.

### Tests
- [T1] All unit tests pass.
- [T2] Compact mode tests pass.
- [T3] Interactive resize + drag tests pass.
- [T4] Existing tests (reorder, insertion, removal) still pass.

---

## 10. Open Questions

1. **Gap: 4dp or 6dp?** Zen = 4px. Maho = 6dp. Recommend 4dp for parity.
2. **Height animation: clip-rect vs bounds?** Clip-rect simpler; bounds smoother. Recommend clip-rect for v1.
3. **Resize debounce threshold?** 24dp proposed — needs user feel testing.
4. **Deprecate `uses_compact_row_card`?** Never true anywhere — recommend removal.
5. **Tile preferred size + flex=1?** Keep flex=1 for sub-pixel distribution.
