# Arc Library Visual Parity Plan (Final)

**Status:** execution plan (finalized)  
**Scope:** Archived Tabs, Downloads, Boosts (inline panes) + shared rail/shell  
**Source of truth:** `maho/reference_screenshots/arc-library-mockup.html`

---

## 1. Purpose

Bring the Library panel to visual and structural parity with the HTML mockup for the three inline surfaces: Archived Tabs, Downloads, and Boosts. The shared rail and shell must also match mockup geometry.

**Spaces is not an inline Library pane.** Selecting Spaces from the Library rail opens a full-screen overlay surface that covers the app. This is the correct, intentional product behavior. The mockup's Spaces panel should be treated as visual reference for that overlay surface itself, not as evidence that Spaces belongs inside the Library content area.

**Stale references removed:** The names `SeedLevel4`, `BuildArchiveInjectionProvenanceHtml`, and `ArchiveReopenClearsSuppression` appeared in the prior execution record and were fictional/stale. They do not describe current code. The previous plan's assumption of an inline `MahoSidebarSpacesView` has also been removed.

---

## 2. In-Scope Surfaces

| # | Surface | Current State | Parity Target |
|---|---------|--------------|---------------|
| 1 | **Downloads** | Real inline pane (`maho_sidebar_downloads_view.cc`). Functional with pause/resume/open/reveal. | Mockup row style: file-type badge (colored rounded rect + extension text), title, subtitle, progress bar, "Done" meta. Grouped by date sections. |
| 2 | **Archived Tabs** | Real inline pane (`maho_sidebar_archive_view.cc`). Working with search + filter. | Mockup row style: colored-initial badge (28x28, 8px radius, first letter of title), title, subtitle (domain). Grouped by date. Filter button in search bar. |
| 3 | **Boosts** | Routes to `MahoSidebarLibraryActionPaneView` stub (single "Open Boost" button). `MahoSidebarBoostsView` exists with full implementation but is **not wired** into library routing. | Real inline pane showing boost list: badge + title + domain subtitle per row. Simple flat list. |

## 3. Spaces: Intentional Exception

Spaces is not an inline pane. It is a full-screen overlay surface.

| Behavior | Detail |
|----------|--------|
| Rail affordance | Spaces category appears in the Library rail visually consistent with other categories |
| Selection action | Clicking Spaces exits library mode and opens `ShowMahoSpacesOverlay()` covering the full app window |
| Why it differs | Spaces is a workspace-switching surface, not a browseable list. Full-screen presentation is the intended UX. |
| Mockup treatment | The Spaces mockup serves as visual reference for the overlay surface layout, not as proof of inline placement |

The existing `HandleLibraryCategorySelected(kSpaces)` → `ShowMahoSpacesOverlay()` → `SetBodyMode(kTabs)` path is correct and remains unchanged by this plan.

## 4. Explicitly Out of Scope

| Surface | Reason |
|---------|--------|
| **Media** | Absent from mockup. Rail category exists but no visual target defined. |
| **Easels** | Absent from mockup. Rail category exists but no visual target defined. |
| **Spaces overlay internals** | The overlay's internal layout/styling is a separate effort; this plan only ensures the rail affordance launches it correctly. |
| Space tinting of Library Rail | Not depicted differently in mockup; future work. |

---

## 5. File Ownership Map

### 5.1 Shared Shell & Routing

| File | Owns |
|------|------|
| `browser/ui/views/sidebar/maho_sidebar_view.cc/.h` | Library mode routing, `HandleLibraryCategorySelected()`, `GetOrCreateLibraryPane()`, `ShowLibraryPane()` |
| `browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc/.h` | Rail layout, category items, selection state, back button |
| `browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | Footer entry into Library mode |
| `browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.cc/.h` | Generic action pane (Easels stub only after this plan) |

### 5.2 Per-Surface Owners

| Surface | Owner Files |
|---------|-------------|
| Downloads | `maho_sidebar_downloads_view.cc/.h` |
| Archived Tabs | `maho_sidebar_archive_view.cc/.h` |
| Boosts | `maho_sidebar_boosts_view.cc/.h`, `maho_sidebar_boosts_data.cc/.h` |

### 5.3 Theme & Color

| File | Owns |
|------|------|
| `browser/ui/theme/maho_color_id.h` | All color ID declarations (13 Archive, 6 Library Rail, 3 Sidebar) |
| `browser/ui/theme/maho_color_mixer.cc` | Color value definitions + space tint pipeline |

### 5.4 Spaces Overlay (not modified by this plan)

| File | Note |
|------|------|
| `browser/ui/views/spaces_overlay/*` | Full-window overlay. Remains as-is. The only change is ensuring the rail affordance launches it cleanly. |

---

## 6. Current State: Grounded Facts

1. **Rail constants diverge from mockup.** Current: `kRailWidthDp=60`, `kRailIconSizeDp=16`, `kRailItemHeightDp=48`, `kRailTileWidthDp=44`. Mockup: rail width `80px`, icon size `24px`, item `64px` wide with `8px 6px` padding, `12px` corner radius.

2. **Spaces routing correctly exits library mode.** `HandleLibraryCategorySelected(kSpaces)` calls `ShowMahoSpacesOverlay()` then `SetBodyMode(kTabs)`. This is correct behavior and must be preserved.

3. **Boosts routing uses wrong view.** `GetOrCreateLibraryPane(kBoosts)` creates a `MahoSidebarLibraryActionPaneView` stub. The real `MahoSidebarBoostsView` (297 lines, with `ReloadBoosts()`, scroll view, row views, FFI bridge calls) exists but is unused in library routing.

4. **Archive fallback uses globe icon.** When favicon data is absent, archive rows fall back to a generic globe icon. The mockup shows colored-initial badges (letter on colored background) as the universal row presentation.

5. **Color system is partially wired.** 13 Archive color IDs and 6 Library Rail color IDs are defined. Views use some of them but not consistently. No Boost-specific color IDs exist.

6. **Downloads grouping.** Mockup shows "Today" / "Yesterday" / "Last Week" date sections. Current downloads view may not group by date.

7. **Search bar.** Mockup shows per-pane search bars. Archive already has search. Downloads mockup shows search. Boosts mockup does not show search.

---

## 7. Mockup Visual Targets (extracted from HTML)

### 7.1 Rail

| Property | Mockup Value | Current Code |
|----------|-------------|--------------|
| Rail width | 80px | 60dp |
| Rail item width | 64px | 44dp (tile) |
| Rail icon size | 24px (stroke) | 16dp |
| Rail item padding | 8px 6px | implicit in tile sizing |
| Rail item corner radius | 12px | 7dp |
| Rail item gap | 4px | 2dp |
| Active background | `rgba(255,255,255,0.08)` | uses `kMahoColorLibraryRailItemSelected` (rgb 51,49,64) |
| Label font size | 9.5px | needs verification |
| Label color (inactive) | `#8a8290` | `kMahoColorLibraryRailItemText` similar |
| Label color (active) | `#d4d0d8` | `kMahoColorLibraryRailItemSelectedText` similar |

### 7.2 Content Area

| Property | Mockup Value |
|----------|-------------|
| Content background | `#3d3444` |
| Search bar background | `rgba(0,0,0,0.25)` |
| Search bar radius | 8px |
| Search bar padding | 8px 12px |
| Section label | 11px, weight 600, `#7a7280` |
| List row padding | 10px |
| List row radius | 10px |
| List row background | `rgba(255,255,255,0.04)` |
| Row gap | 2px margin-bottom |

### 7.3 Archive Rows

| Property | Mockup Value |
|----------|-------------|
| Badge size | 28x28px |
| Badge corner radius | 8px |
| Badge: colored background + white bold initial letter (12px) | 10 color variants defined |
| Title | 13px, weight 500, `#f0edf3` |
| Subtitle | 11px, `#8a8290` |

### 7.4 Downloads Rows

| Property | Mockup Value |
|----------|-------------|
| File badge size | 28x28px, radius 7px |
| Badge: translucent colored background + extension text (8px, weight 800) | PDF/ZIP/PNG/MOV/FIG/DOC |
| Progress bar | 48x3px, radius 2px, green fill `#77d7a1` |
| Meta text ("Done" / percentage) | 10px, `#7a7280` |

### 7.5 Boosts (Inline Panel)

| Property | Mockup Value |
|----------|-------------|
| Row layout | Same as Archive: badge + title + subtitle |
| Badge | 28x28px, colored background, initial letter or icon |
| Title | 13px, weight 500, `#f0edf3` |
| Subtitle | 11px, `#8a8290` (domain) |
| No search bar shown | |
| No toggle/action buttons shown | Simple flat list |

### 7.6 Spaces (Overlay Reference Only)

The mockup's Spaces panel describes the full-screen overlay layout. Values here are reference for the overlay surface, not for an inline Library pane.

| Property | Mockup Value |
|----------|-------------|
| Layout | Horizontal scroll of space columns |
| Column width | 150px fixed |
| Column background | `rgba(255,255,255,0.04)` + optional tint variants |
| Column corner radius | 10px |
| Column padding | 10px |
| Column name | 13px, weight 650 |
| Column badge | 9px, `#8a8290` |
| Section labels | 9px, uppercase, weight 600, `#7a7280` |
| Item favicon | 18x18px, radius 4px |
| Item title | 11px, `#d8d4dc` |
| Fade gradient | right edge 50px gradient |

---

## 8. Implementation Phases

### Phase 1: Rail Geometry Alignment

**Objective:** Match rail to mockup dimensions.

**Files:** `maho_sidebar_library_rail_view.cc/.h`

**Work:**
- Update `kRailWidthDp` 60→80
- Update `kRailIconSizeDp` 16→24
- Update `kRailItemSpacingDp` 2→4
- Update `kRailTileWidthDp` 44→64, `kRailTileHeightDp` 28→(computed from padding)
- Update `kRailTileCornerRadiusDp` 7→12
- Verify label font size is ~9.5dp
- Update active state to use `rgba(255,255,255,0.08)` overlay or confirm existing color is close enough

**Depends on:** nothing

---

### Phase 2: Boosts Inline Wiring

**Objective:** Replace the action-pane stub with the real `MahoSidebarBoostsView` in library routing.

**Files:**
- Modified: `maho_sidebar_view.cc/.h` (`GetOrCreateLibraryPane` kBoosts branch, member type change)
- Modified: `maho_sidebar_boosts_view.cc/.h` (visual adjustments to match mockup row style)

**Work:**
- Change `GetOrCreateLibraryPane(kBoosts)` to instantiate `MahoSidebarBoostsView` instead of `MahoSidebarLibraryActionPaneView`.
- **Member type change:** The existing `boost_view_` member in `maho_sidebar_view.h` is typed as `raw_ptr<MahoSidebarLibraryActionPaneView>`, and `boost_view_for_testing()` returns that same type. Both must change to `raw_ptr<MahoSidebarBoostsView>` / return `MahoSidebarBoostsView*`. Any test code that calls `boost_view_for_testing()` and expects a `MahoSidebarLibraryActionPaneView*` must be updated.
- Call `ReloadBoosts()` in `ShowLibraryPane()` when category is kBoosts (same pattern as `ReloadArchivedTabs()`).
- Adjust `BoostRowView` visual style to match mockup: 28x28 badge, 13px title, 11px domain subtitle.
- Remove toggle buttons from row presentation to match mockup (flat list). If toggle is needed for functionality, hide behind context menu or secondary interaction.

**Depends on:** Phase 1

---

### Phase 3: Archive Visual Polish

**Objective:** Close the gap between current archive view and mockup styling.

**Files:** `maho_sidebar_archive_view.cc/.h`

**Work:**
- Replace globe-icon fallback with colored-initial badge when favicon data is absent.
  - Badge: 28x28dp, 8dp corner radius, colored background, white bold initial letter.
  - Color selection: deterministic hash of domain/title → pick from 10-color palette matching mockup.
- Verify row padding matches mockup (10px padding, 10px radius, 2px gap).
- Verify section label style (11px, weight 600, muted color).
- Verify search bar matches mockup (search icon + placeholder + filter button).

**Depends on:** Phase 1

---

### Phase 4: Downloads Visual Polish

**Objective:** Match downloads rows to mockup style.

**Files:** `maho_sidebar_downloads_view.cc/.h`

**Work:**
- Implement file-type badge: 28x28dp, 7dp radius, translucent colored background, extension text (8px, weight 800).
  - Extension-to-color mapping: PDF→red, ZIP→purple, PNG→cyan, MOV→blue, FIG→orange, DOC→green, default→gray.
- Add date-section grouping ("Today" / "Yesterday" / "Last Week" / older).
- Implement progress bar: 48x3dp, 2dp radius, `#77d7a1` green fill.
- Add "Done" meta text for completed downloads.
- Add search bar if not already present (mockup shows one).

**Depends on:** Phase 1

---

### Phase 5: Spaces Rail Affordance Verification

**Objective:** Confirm the Spaces rail item looks visually correct and launches the overlay properly.

**Files:** `maho_sidebar_library_rail_view.cc/.h`, `maho_sidebar_view.cc` (read-only verification; modify only if broken)

**Work:**
- Verify Spaces rail item renders at correct size (24dp icon, 64dp tile, 12dp radius) after Phase 1 geometry changes.
- Verify clicking Spaces rail item still correctly calls `ShowMahoSpacesOverlay()` and exits library mode.
- If Phase 1 rail geometry changes broke the Spaces selection handler path, fix the regression.
- No inline pane creation. No new view files.

**Depends on:** Phase 1

---

### Phase 6: Color System Wiring & Theme Consistency

**Objective:** Wire defined colors into views consistently. Add missing Boost color IDs if needed.

**Files:** `maho_color_id.h`, `maho_color_mixer.cc`, all three inline surface views, `maho_sidebar_library_rail_view.cc`

**Work:**
- Audit each view's hardcoded colors against `maho_color_id.h` definitions.
- Wire `kMahoColorLibraryRailBackground`, `kMahoColorLibraryRailItemSelected`, etc. into rail view (replace any hardcoded ARGB literals).
- Wire `kMahoColorArchive*` IDs into archive view where currently using raw colors.
- For Boosts: decide whether to add new color IDs or reuse existing Archive/Library Rail tokens. Document the decision.
- Verify content area background (`#3d3444` mockup) aligns with the appropriate color ID.
- Verify row hover/interaction states across all three inline surfaces.

**Depends on:** Phases 2-4

---

### Phase 7: Residual Tuning

**Objective:** Close any remaining visual deltas found during verification.

**Files:** Only files already modified in Phases 1-6.

**Work:**
- Fix any spacing, padding, font-size, or color mismatches identified in verification.
- No new files may be introduced.

**Depends on:** Phase 6

---

## 9. Verification

### V1: Rail Geometry Verification

**Must prove:**
- Rail width renders at 80dp
- Icons render at 24dp
- Item spacing and corner radius match mockup
- All category labels (including Spaces) are legible
- Active state highlight is visible and matches `rgba(255,255,255,0.08)`

**Test evidence:** Existing `LibraryOpensArchivedTabsAndExitReturnsToTabs` rail exclusivity assertions must still pass after constants change.

---

### V2: Spaces Overlay Launch Verification

**Must prove:**
- Clicking Spaces in the rail exits library mode and opens the full-screen overlay
- The overlay covers the app as expected
- Library mode does NOT remain active after Spaces selection
- No inline Spaces pane is created or shown

**Test evidence:** Existing `LibraryOpensSpacesPaneAndShowsCanonicalActions` (which verifies overlay launch and library mode exit) must continue to pass unchanged.

---

### V3: Boosts Inline Pane Verification

**Must prove:**
- Clicking Boosts in rail opens the real `MahoSidebarBoostsView` (not action pane stub)
- Boost rows display with badge + title + domain
- `ReloadBoosts()` is called on pane show
- Row count > 0 when boosts exist

**Test evidence:** **Existing test must be updated.** `LibraryOpensBoostsPaneAndLaunchesCanonicalWebUI` currently verifies the action pane's "Open Boost" button. It must be rewritten to verify `MahoSidebarBoostsView` visibility and row content.

---

### V4: Archive Visual Verification

**Must prove:**
- Rows use colored-initial badges (not globe icon) when favicon is absent
- Badge colors are deterministic and match the 10-color palette
- Section labels, row spacing, search bar match mockup
- Populated and empty states both render correctly

**Test evidence:** Existing archive tests must pass. **New assertion recommended** for badge color determinism.

---

### V5: Downloads Visual Verification

**Must prove:**
- File-type badges render with correct extension text and colors
- Date grouping sections appear
- Progress bar renders for in-progress downloads
- "Done" meta text appears for completed downloads

**Test evidence:** Existing download tests must still pass. **New assertions recommended** for badge presence and section grouping.

---

### V6: Cross-Surface Consistency Verification

**Must prove:**
- All three inline surfaces use consistent row height, padding, and typography
- Color IDs are wired (no raw ARGB literals remaining in view code)
- Rail selection state correctly tracks which inline surface is active
- Spaces rail selection correctly transitions out of library mode (distinct from inline selections)
- Switching between inline surfaces is smooth (no blank/flash states)

**Test evidence:** `LibraryOpensArchivedTabsAndExitReturnsToTabs` rail exclusivity loop must pass for all categories.

---

## 10. Acceptance Criteria

The plan is complete when **all** of the following are true:

1. Three inline surfaces (Downloads, Archive, Boosts) render as inline library panes matching mockup
2. Spaces rail affordance launches the full-screen overlay correctly (no inline pane)
3. Rail geometry matches mockup targets (80dp width, 24dp icons, 4dp gaps, 12dp radius)
4. Archive rows use colored-initial badges, not globe-icon fallback
5. Downloads rows use file-type extension badges with translucent color backgrounds
6. Boosts renders the real `MahoSidebarBoostsView` with boost list, not an action-pane stub
7. Color IDs from `maho_color_id.h` are wired into the corresponding views
8. All existing interactive tests pass after changes
9. Updated test exists for Boosts pane (action-pane stub assertions removed)
10. Spaces overlay test continues to pass unmodified

**Rejection criteria:** Any pass claim that conflates Spaces with inline Library panes is invalid. "Library parity" means parity for the three inline surfaces plus correct overlay launch for Spaces.

---

## 11. Planned Test Work

| Surface | Current Coverage | New Work Needed |
|---------|-----------------|-----------------|
| Downloads | 3 interactive tests (visibility, actions, file-action callbacks) | Add badge rendering assertion, add date-section grouping assertion |
| Archive | 10+ interactive tests (accessibility, semantics, filter, search, empty/populated states) | Add colored-initial badge assertion (verify no globe fallback) |
| Boosts | 1 test (verifies action pane "Open Boost" button) | **Rewrite test:** verify `MahoSidebarBoostsView` visibility, row content, `ReloadBoosts()` call |
| Spaces | 1 test (verifies overlay launch and library mode exit) | **No change needed.** Test already verifies the correct behavior. |

No seed-level infrastructure exists or is required.

---

## 12. Stop & Replan Triggers

1. Rail constant changes break existing navigation test assertions and cannot be reconciled
2. Boosts data loading fails because `maho_sidebar_boosts_data.cc` depends on unimplemented FFI
3. A required edit falls outside the file ownership map
4. New files are needed in Phase 7 (residual tuning)
5. Someone attempts to create an inline Spaces pane (this contradicts the product rule)

---

## 13. Phase Ordering Summary

```
Phase 1: Rail Geometry ─────────────────────────────────────┐
                                                             │
Phase 2: Boosts Inline Wiring ──────────────────┐           │
Phase 3: Archive Visual Polish ─────────────────┤           │
Phase 4: Downloads Visual Polish ───────────────┤           │
Phase 5: Spaces Rail Affordance Verification ───┘           │
                                                             │
Phase 6: Color System Wiring ───────────────────────────────┘
Phase 7: Residual Tuning

V1-V6: Verification (after implementation)
```

Phases 2-5 are independent of each other and may be parallelized. Phase 6 depends on all of them. Phase 7 is last-mile only.
