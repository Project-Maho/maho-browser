# Arc Sidebar Parity Plan

Date: 2026-06-14
Status: approved for planning by user; revised 2026-06-15 for stricter Arc
parity
References:
- `maho/reference_screenshots/arc-sidebar.jpeg`
- `maho/reference_screenshots/arc.jpeg`

## Objective

Make Maho Chromium's native sidebar reach visual parity with the provided Arc
references at the browser-shell level while preserving the existing Chromium
Views architecture and current Maho sidebar behavior.

This plan is dark-mode-first because both references are dark-mode Arc
screenshots. Light mode must be regression-checked, but not treated as a
pixel-parity target for this pass.

## Plan Fidelity

The earlier version of this plan was sufficient for an Arc-inspired refresh, but
not strict enough for Arc parity. This revised plan treats parity as measurable:
the work must derive geometry, color, density, and state targets from the
reference images before implementation, then compare fresh Maho screenshots
against those targets.

## Visual Target

The target is Arc sidebar parity, not a generic dark sidebar refresh:

- Dark purple/charcoal sidebar integrated into the app frame.
- Darker rounded content panel with a visible inset gutter where the existing
  shell exposes the needed surface.
- Top controls aligned with traffic lights, quiet icon-only affordances, and no
  hard button borders.
- Rounded search/address pill around 34-40 px high, one contrast step above the
  sidebar background.
- 3-column favorites grid with tight gaps and 9 visible reference-state tiles.
- Compact tab rows around 30-36 px, transparent by default, with subtle rounded
  hover and selected fills.
- Integrated footer with update affordance and space switcher, avoiding heavy
  dividers or card-like framing.

## Reference Normalization

Reference measurements are tracked under:

```text
.omo/artifacts/arc-sidebar-look-and-feel/reference-measurements.md
```

Current measured anchors from the provided screenshots:

- `arc.jpeg`: image size 2280 x 2656.
- `arc-sidebar.jpeg`: image size 4336 x 2656.
- both references use the same UI scale; the wider image changes window width,
  not sidebar scale.
- hard window left: x ~= 108 in both references.
- hard window right: x ~= 2171 in `arc.jpeg`, x ~= 4227 in
  `arc-sidebar.jpeg`.
- content panel left edge: x ~= 570 in both references.
- sidebar shell width target: ~= 462 px at reference scale.
- search pill: x 128-551, y 168-239, size ~= 424 x 72 px.
- favorites grid: three columns at x 128-258, 275-405, 421-551; each tile is
  ~= 131 px wide with 16-17 px gaps.
- favorites rows: y 256-349, 366-459, 476-569; each tile is ~= 94 px high with
  16-17 px gaps.
- update pill: x 130-549, y 2343-2392, size ~= 420 x 50 px.
- sampled sidebar/window surface: `#272537` to `#2a2537`.
- sampled content panel surface: `#41394a`.
- sampled search surface: `#3b3a4b` to `#3b3a4c`.
- sampled favorite tile surface: `#363446` to `#363546`.

The implementation must scale these targets by the ratio between the final Maho
QA window height and the Arc hard-window height of roughly 2436 px
(`2511 - 75`). Use the hard-window size, not the full image canvas, because the
reference images include shadow and white background.

If deeper precision is required during implementation, extend the measurement
note with:

- window outer bounds and corner radius;
- content panel top/right/bottom bounds, gutter, and corner radius;
- traffic-light center positions and top-control centerline;
- search pill radius;
- favorites tile corner radius;
- first tab row or `+ New Tab` row bounds, row height, icon/text offsets;
- bottom space switcher icon/dot row bounds.

Do not choose dp values by taste alone.

## Required QA States

Parity must be checked in two deterministic states:

- populated state matching `arc-sidebar.jpeg`:
  nine visible favorites, a populated tab list, `+ New Tab`, update pill, and
  bottom space switcher visible;
- sparse state matching `arc.jpeg`:
  nine visible favorites, minimal list content, `+ New Tab`, update pill, and
  bottom space switcher visible.

Implementation should create or document the exact profile/fixture steps used
to produce these states. A one-off screenshot from an arbitrary browser profile
is not enough evidence for parity.

## Acceptance Standard

The implementation is acceptable only if the final visual QA report records:

- geometry deltas for sidebar width, content gutter, search pill, favorites
  grid, row height, update pill, and footer row;
- sampled color deltas for sidebar background, content panel, search pill,
  favorite tiles, selected/hover rows, and text;
- screenshot overlays or annotated crops for the two required states;
- pass/fail notes for clipping, overlap, and traffic-light/top-control
  alignment.

Target tolerance:

- sidebar shell width: scaled equivalent of 462 reference px, within 2-4 dp;
- content panel left edge: hard window left plus scaled sidebar shell width,
  within 2-4 dp;
- search pill: scaled equivalent of 424 x 72 reference px, within 1-2 dp for
  repeated dimensions and 2-4 dp for whole-shell placement;
- favorites grid: scaled equivalent of 131 x 94 reference px tiles with 16-17 px
  gaps, within 1-2 dp;
- update pill: scaled equivalent of 420 x 50 reference px, within 1-2 dp for
  size and 2-4 dp for placement;
- major shell geometry: within 2-4 dp after scaling from the reference window;
- repeated row/grid geometry: within 1-2 dp after scaling;
- color/material: visually matched in sampled regions, with no high-contrast
  borders/cards where the Arc reference uses soft translucent surfaces;
- behavior: no regression in sidebar navigation, favorite activation, tab
  selection, spaces, or resizing.

## Non-Goals

- Do not rewrite the sidebar in a new UI stack.
- Do not introduce new GN workarounds for build failures without explicit user
  approval.
- Do not implement full per-space adaptive theming unless the required plumbing
  is already exposed through the current color-token path.
- Do not use `maho-sidebar-alignment-qa.png` as proof of Arc parity; it only
  covers the earlier top-left alignment issue.
- Do not mark the implementation complete if the content panel inset/gutter is
  missing. The content panel is part of the visible Arc shell; if the owner
  surface cannot be changed safely, the implementation must stop and report the
  blocker rather than calling sidebar-only work parity.

## Current Code Surface

Primary sidebar files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
  controls rail width, insets, spacing, radii, search/favorites/tab/footer
  sizing.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` owns root
  sidebar composition, section ordering, background, and border behavior.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc` owns the
  traffic-light-aligned navigation/top control row.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc` owns the
  search/address pill.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
  owns favorite tile layout, density, and painting.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` owns tab
  rows, selected/hover styling, indentation, and list density.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` owns the
  bottom controls and space-dot cluster.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_view.cc` owns the
  spaces panel and scroll padding.

Browser shell/layout files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc` owns
  sidebar hosting, width persistence, reveal behavior, and resize handle bounds.
- `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc`
  documents the upstream `BrowserView` overlay that creates and wires the Maho
  sidebar container.
- `maho-chromium/chromium_src/chrome/browser/ui/views/frame/layout/browser_view_layout.h`
  documents the `BrowserViewLayoutViews::maho_sidebar_container` field used by
  layout.
- `maho-chromium/chromium_src/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc`
  documents the Arc-mode two-pane layout. This is the critical surface for
  content-panel inset/gutter/radius ownership.

Theme files:

- `maho-chromium/browser/ui/theme/maho_color_id.h`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`
- `maho-chromium/browser/ui/theme/maho_space_theme_io.cc`

Tests and build:

- `maho-chromium/browser/ui/views/sidebar/BUILD.gn`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_layout_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`
- sidebar interactive tests for actions, navigation, spaces, drag/drop, and edge
  cases.

## Worktree Guard

Before implementation, capture the current sidebar/theme dirty state:

```bash
git status --short -- maho-chromium/browser/ui/views/sidebar maho-chromium/browser/ui/theme
git diff -- maho-chromium/browser/ui/views/sidebar maho-chromium/browser/ui/theme
```

Treat existing edits as user or concurrent-agent work. Do not revert them. If a
planned edit touches an already-modified file, re-read the file and integrate
with the current content rather than resetting it.

## Implementation Sequence

### 1. Reference Measurement And Baseline Visual Capture

Create a reference measurement file first. Then capture current Maho sidebar
screenshots before editing.

Capture current Maho states:

- populated/list state comparable to `arc-sidebar.jpeg`;
- sparse/empty state comparable to `arc.jpeg`;
- full-window screenshot, not only the top-left toolbar crop.

Record the capture paths under `.omo/artifacts/arc-sidebar-look-and-feel/`.

Output required before code edits:

- `reference-measurements.md`;
- `baseline-populated.png`;
- `baseline-sparse.png`;
- a short baseline gap table mapping each Arc target to the current Maho
  screenshot delta.

### 2. Shell Layout Pass

Start with shell-level parity before internal row polish. Arc's defining visual
trait is the two-pane shell: sidebar plus rounded inset content panel.

Inspect and, if safe, tune:

- `maho_sidebar_container_view.cc` for host width/resize constraints and any
  visual side effects of the resize handle;
- the `BrowserView`/layout overlay surfaces documented in
  `chromium_src/chrome/browser/ui/views/frame/**` for content area bounds,
  gutter, and panel radius ownership;
- color IDs used for content/window/sidebar backgrounds.

Success criteria:

- sidebar width matches the normalized reference target at the QA window size;
- content panel is visibly inset with the same left/top/bottom gutter rhythm as
  the reference;
- rounded content panel is either implemented or a concrete blocker is reported
  with the owner file and reason.

### 3. Geometry And Token Pass

Start with `maho_sidebar_layout_tokens.h` and avoid component-specific paint
changes until token-only improvements are exhausted.

Adjust:

- default rail width and min/max width toward an Arc-like calm sidebar;
- sidebar outer and section insets;
- top region spacing;
- search pill height/radius/insets;
- favorites tile height, gap, radius, and glyph/text proportions;
- tab row height, row radius, icon/text spacing, and row vertical gap;
- footer row/control size and bottom spacing.

Success criteria:

- top controls, search, favorites, tab list, and footer share one vertical
  rhythm;
- favorites still lay out as a 3-column grid at default width;
- tab rows remain compact and scannable;
- no overlap or clipped text at default and minimum rail widths.

### 4. Surface And Color Pass

Use existing color IDs and the mixer before adding new color IDs.

Tune only where the current palette fails the references:

- sidebar background;
- content/window background if exposed by the existing shell;
- search field background/border;
- favorites tile hover/selected/empty states;
- tab row hover/selected fills;
- footer divider/update/space switcher surfaces;
- primary, secondary, and tertiary text contrast.

Success criteria:

- sidebar reads as dark purple/charcoal rather than neutral gray;
- interactive surfaces are subtle translucent steps, not bright cards;
- selected and hovered rows are visible without becoming saturated accents.

### 5. Component Refinement Pass

Only after the token and color pass, touch component implementation where needed:

- `maho_sidebar_top_bar_view.cc`: make navigation controls visually quiet and
  keep traffic-light alignment intact.
- `maho_sidebar_search_view.cc`: ensure the search pill reads as one rounded
  surface with muted icon/text hierarchy.
- `maho_sidebar_favorites_grid_view.cc`: refine tile painting, label behavior,
  and empty/placeholder state if token changes are insufficient.
- `maho_sidebar_tab_list_view.cc`: refine row background, hover/selected states,
  indentation, and section rhythm.
- `maho_sidebar_footer_view.cc`: integrate update affordance and space switcher
  without heavy dividers.

Success criteria:

- component-specific changes are minimal and explainable by a visible gap in the
  Arc references;
- no behavior regressions in tab selection, favorite activation, spaces, or
  drag/drop.

## Verification

Run builds and tests through the project wrapper from repo root:

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests
```

Run targeted tests based on touched surfaces:

- favorites geometry: sidebar favorites grid layout test;
- top/search/footer/tab behavior: relevant sidebar interactive tests;
- spaces behavior: sidebar spaces interactive test;
- drag/drop behavior: browser or interactive DnD tests only if DnD-adjacent
  files are touched.

Do not edit GN files to bypass failures unless the user explicitly approves that
specific GN change. If wrapper build or GN regen fails in a way that appears
unrelated to the sidebar changes, stop and report the failure with the command
and the first actionable error.

## Manual Visual QA Gate

The implementation is not complete until the app is driven through the visible
surface and new screenshots are captured.

Capture:

- current Maho populated/list state;
- current Maho sparse/empty state;
- both at a stable desktop size comparable to the reference images.

Compare against:

- `maho/reference_screenshots/arc-sidebar.jpeg`;
- `maho/reference_screenshots/arc.jpeg`.

Check:

- traffic lights and top controls align;
- search pill size/radius/contrast matches the reference intent;
- favorites grid is 3 columns with Arc-like density;
- tab rows are compact, readable, and subtly selected/hovered;
- footer/update/space switcher integrates with the sidebar;
- content/sidebar gutter and rounded panel treatment are matched; if this cannot
  be done safely, the parity work is blocked rather than complete;
- no clipping or overlap at default and minimum sidebar width.

Create:

- `final-populated.png`;
- `final-sparse.png`;
- `parity-report.md` with before/after crops, measured deltas, and a verdict for
  each acceptance item.

Store visual QA artifacts under:

```text
.omo/artifacts/arc-sidebar-look-and-feel/
```

## Handoff Notes

The implementation should be done as a small sequence of observable visual
changes, not one broad rewrite. The highest-leverage first edit is now the
reference measurement plus shell layout pass, followed by sidebar tokens and the
color mixer. Component-specific code should be reserved for gaps that remain
visible after shell, token, and color changes.

Because the current worktree is already heavily dirty in sidebar/theme files,
every implementation step must re-read the files it touches and avoid reverting
unrelated user or concurrent-agent edits.
