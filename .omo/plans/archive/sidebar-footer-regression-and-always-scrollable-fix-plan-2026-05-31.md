# Sidebar Footer Regression Fix and Always-Scrollable Tab Area Plan

## Goal

Restore the missing footer area (`MahoSidebarFooterView` — space dots, archive button, plus button) at the bottom of the Maho Browser sidebar, and make the tab list area (`MahoSidebarTabListView`) always vertically scrollable beneath the sticky `top_region_` (search + favorites + space header), regardless of how many tabs are present. The architecture must match Zen browser's reference pattern (height-bounded scroll container as a sibling of header and footer, both inside a parent that establishes an explicit height chain).

The visual outcome the user expects:

1. The footer is always visible at the bottom of the sidebar in tabs mode, exactly as it was before this regression.
2. The pinned and normal tab area scrolls vertically when its content exceeds the viewport height. The scroll experience is always available — there is no fallback "clip" behavior.
3. `top_region_` (search + favorites + space header) stays sticky above the scrollable area; nothing about its sticky behavior changes.
4. Active drag operations near the top or bottom edge of the scroll viewport continue to auto-scroll at the same rate as before (24dp edge zone, 30 Hz × 4 dp/tick).

## Target contract

After this plan is implemented, the following invariants must hold simultaneously:

### Layout contract — `MahoSidebarView::BuildUi()`

1. `body_layout->SetFlexForView(content_host_, 1, /*use_min_size=*/false)`. The `use_min_size` flag flips from `true` to `false`. This is the root-cause fix for the footer regression: with `use_min_size=true`, `content_host_` reserved at least its `GetMinimumSize().height()` (which equals the tab list's full content height when tab count is large) before any space is given to footer. With `use_min_size=false`, `content_host_` no longer reserves that minimum, and footer's preferred height is satisfied first.
2. `normal_body_layout->SetFlexForView(tab_scroll_view_, 1, /*use_min_size=*/false)`. The same `use_min_size=false` change is applied recursively at the inner `tabs_surface_` BoxLayout level so that `top_region_` (sticky) gets its preferred height first and `tab_scroll_view_` consumes the remainder without inflating `tabs_surface_`'s minimum size.
3. `tab_list_view_` is wrapped inside `tab_scroll_view_` (a `views::ScrollView`). This wrapping was introduced earlier and was reverted as part of regression diagnosis. It must be restored as the always-scrollable mechanism.
4. `tab_scroll_view_` is configured with `SetBackgroundColor(std::nullopt)`, `SetHorizontalScrollBarMode(kDisabled)`, and `SetDrawOverflowIndicator(false)`. **No `ClipHeightTo` call.** **No `SetVerticalScrollBarMode` call** (the default `kEnabled` is acceptable; if vertical scrollbar should be hidden visually, that is an out-of-scope styling concern).
5. `tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_)` is called immediately after the ScrollView is added to `tabs_surface_`, restoring drag-auto-scroll wiring.

### Footer contract

6. `footer_view_` is the second (last) child of `body_host_`. No flex assigned. It receives its preferred height first in `body_layout`'s allocation pass, before flex distribution.
7. `footer_view_->GetVisible()` is `true` whenever the sidebar is in tabs mode (matches existing `MahoSidebarBodyMode::kTabs` toggle behavior — unchanged from current code).

### Sticky region contract — `top_region_`

8. `top_region_->children().size() == 3`: `search_view_`, `favorites_view_`, `space_header_view_`. Order unchanged from the prior plan.
9. `space_header_view_->GetVisible()` reflects `!model.active_space_name.empty()`. Unchanged from prior plan.

### Always-scrollable contract — `tab_scroll_view_`

10. When `tab_list_view_->GetPreferredSize().height() <= tab_scroll_view_->GetVisibleRect().height()`, the scroll bar may be hidden but the scroll view is still functional (mouse wheel scroll within the area is a no-op because there is nothing to scroll, but the scrollable affordance is present).
11. When `tab_list_view_->GetPreferredSize().height() > tab_scroll_view_->GetVisibleRect().height()`, the scroll bar appears and the user can scroll the tab list. `top_region_` (search + favorites + space header) stays fixed; only the contents of `tab_scroll_view_` move.
12. Drag-auto-scroll continues to work via the existing `MahoSidebarTabListView::OnAutoScrollDragOver` / `OnAutoScrollDragEnded` chain. No change to those public methods or to `SidebarAutoScroller`.

### Tab list invariants — preserved

13. `MahoSidebarTabListView::tab_rows_->parent() == MahoSidebarTabListView*`. The contract test at `maho_sidebar_dnd_dispatch_unittest.cc:2649–2663` continues to pass.
14. The `tab_scroll_view_` member on `MahoSidebarView` is a `raw_ptr<views::ScrollView>`. It must be reassigned to the new ScrollView in `BuildUi()` (currently set to `nullptr` in the diagnostic state).

## Explicit non-goals

1. No change to the ScrollView vertical scrollbar mode beyond the default. `SetVerticalScrollBarMode(kHiddenButEnabled)` is **not** restored. If the user later wants the scrollbar visually hidden, that is a separate styling step and must not be conflated with this regression fix.
2. No change to `MahoSidebarTabListView` internal layout, `tab_rows_` structure, drop targets, drop planner, drag image, or any DnD plumbing.
3. No change to `MahoSidebarFooterView`, `MahoSidebarSearchView`, `MahoSidebarFavoritesGridView`, or `SidebarSpaceHeaderRow` view classes themselves.
4. No change to `MahoSidebarStateAdapter::BuildTabListModel()` or `BuildFooterModel()`. Active space data flow is unchanged.
5. No change to `MahoSidebarTabListModel.active_space_icon` / `active_space_name` fields. They remain.
6. No change to `OnAutoScrollDragOver`, `OnAutoScrollDragEnded`, `SetAutoScrollScrollView`, `auto_scroller_`, `SidebarAutoScroller`, `ForwardDragToAutoScroller`, `ForwardDragEnded`, or any drop handler call sites that invoke the forwarders. The encapsulation fix from the prior session stays.
7. No change to forward declarations in `namespace maho` block at the top of `maho_sidebar_tab_list_view.cc`.
8. No change to the folder DnD guard in `SidebarFolderRowView::ResolveDropZone` (already restored).
9. No new files. No `BUILD.gn` changes. No GN edits per repo policy.
10. No new FFI calls. No Mojo IDL changes. No WebUI changes.
11. No investigation or fix for the lucide icon migration that the user is independently in progress on. If a lucide-related compile error blocks the rebuild, report and stop per the user's standing instruction.
12. No change to `MahoSidebarView::UpdateTabList()`. The `space_header_view_->SetVisible(...)` call sequence stays.
13. No removal of `tab_scroll_view_` member from `MahoSidebarView`. It must be assigned in `BuildUi()` to the new ScrollView, not left as `nullptr`.
14. No `SetVerticalScrollBarMode` configuration. Default mode is acceptable.

## Current code findings

All paths below are absolute relative to repo root `/Users/indo/code/project/maho-workspace`.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

`MahoSidebarView::BuildUi()` currently has the following relevant state after the diagnostic revert (lines 218–222 and line 264):

```cpp
tab_list_view_ = tabs_surface_->AddChildView(
    std::make_unique<MahoSidebarTabListView>(browser_));
tab_scroll_view_ = nullptr;

tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_);
```

```cpp
normal_body_layout->SetFlexForView(tab_list_view_, 1, /*use_min_size=*/true);
body_layout->SetFlexForView(content_host_, 1, true);
root_layout->SetFlexForView(body_host_, 1, true);
```

This is the source of the footer regression. The `use_min_size=true` on line 264 (verified by Oracle as unchanged from the original code) was tolerable when `top_region_` had only `search_view_` and `favorites_view_`, but adding `space_header_view_` pushed the layout's preferred-size accounting past a threshold that exposed the latent overflow.

### Source: Oracle's diagnostic ladder

Oracle ran a five-step suspect-elimination on changes 1 through 10 (see footer-regression-root-cause-hunt task at session `ses_1844169e5ffeo5ymEovAkob9Sq`). The verdict:

- HIGH suspect: `space_header_view_` (change #3), but only because it sits under the existing `use_min_size=true` flex on `content_host_`. The structural fix is the flex flag, not the header.
- MEDIUM suspect: changes #1 and #2 (model fields + adapter) — also gated by `use_min_size=true` and resolved by the same fix.
- LOW: every other suspect including `ForwardDrag*` call sites, `GetCoreFooterSpaceState` double call, `SetAutoScrollScrollView(nullptr)`.

Recommended quickest disprover: flip line 264 `true` → `false`. If footer reappears, root cause confirmed.

### Source: Zen browser sidebar architecture

Zen browser (`zen-browser/desktop`, commit `d76fa13`, `src/zen/tabs/zen-tabs/vertical-tabs.css`, `src/browser/base/content/navigator-toolbox-inc-xhtml.patch`, `src/zen/spaces/ZenSpace.mjs`) achieves the always-scrollable + sticky-header + persistent-footer pattern via:

1. A column-direction parent with explicit height: `#titlebar { flex-direction: column; height: 100%; }`.
2. A scroll container as a sibling of header and footer: `#zen-tabs-wrapper { overflow-y: auto; height: 100%; }`.
3. Native overflow on inner tab strip explicitly disabled: `#tabbrowser-tabs { overflow-y: unset !important; }`.
4. The footer toolbar (`#zen-sidebar-foot-buttons`) is a natural last-child of the column flex parent, never `position: fixed` or `position: sticky`.

The Chromium Views equivalent of `flex: 1; min-height: 0` (the canonical CSS pattern) is `BoxLayout::SetFlexForView(child, weight, /*use_min_size=*/false)`. Setting `use_min_size=false` tells BoxLayout: "do not reserve this child's minimum size; just give it whatever flex share remains after non-flex children take their preferred sizes." This matches Zen's height-bounded scroll container behavior.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`

The reference Chromium Views ScrollView setup in this codebase is at `MahoSidebarArchiveView::BuildUi()`:

```cpp
scroll_view_->SetBackgroundColor(std::nullopt);
scroll_view_->SetHorizontalScrollBarMode(
    views::ScrollView::ScrollBarMode::kDisabled);
scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
scroll_view_->GetViewAccessibility().SetName(...);
list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
```

No `ClipHeightTo`, no `SetVerticalScrollBarMode`, no `SetDrawOverflowIndicator`. The flex sizing on the parent layout alone bounds the ScrollView's actual size; the ScrollView's preferred size is `(0, 0)` and BoxLayout's flex distribution gives it the remainder.

### Source: prior plan

`/Users/indo/code/project/maho-workspace/.sisyphus/plans/sidebar-sticky-space-header-and-tab-scroll-plan-2026-05-30.md` introduced the ScrollView wrapping with `ClipHeightTo(0, std::numeric_limits<int>::max())` and `SetVerticalScrollBarMode(kHiddenButEnabled)`. Both have been removed during regression diagnosis. The plan in this file removes them permanently (already done) and adjusts the flex flag to `use_min_size=false` (the missing piece).

## File-by-file implementation plan

Two files modified. No new files. No header changes.

### File 1: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

#### Edit 1.1: Restore the ScrollView wrapping around `tab_list_view_`

Locate the diagnostic-state lines (currently lines 218–222):

```cpp
  tab_list_view_ = tabs_surface_->AddChildView(
      std::make_unique<MahoSidebarTabListView>(browser_));
  tab_scroll_view_ = nullptr;

  tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_);
```

Replace with the restored ScrollView setup, modeled exactly on `MahoSidebarArchiveView`'s pattern:

```cpp
  auto scroll = std::make_unique<views::ScrollView>();
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetDrawOverflowIndicator(false);

  auto tab_list = std::make_unique<MahoSidebarTabListView>(browser_);
  tab_list_view_ = scroll->SetContents(std::move(tab_list));
  tab_scroll_view_ = tabs_surface_->AddChildView(std::move(scroll));

  tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_);
```

Specifically:

1. Construct a `views::ScrollView` with `std::nullopt` background, `kDisabled` horizontal scroll bar mode, and `SetDrawOverflowIndicator(false)`.
2. Do NOT call `ClipHeightTo` on the scroll view.
3. Do NOT call `SetVerticalScrollBarMode` on the scroll view.
4. Construct the `MahoSidebarTabListView` and assign it as the scroll view's contents via `scroll->SetContents(...)`.
5. Capture the contents pointer into the existing `tab_list_view_` member.
6. Capture the scroll view pointer into the existing `tab_scroll_view_` member.
7. Wire the auto-scroll observer via `tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_)`.

#### Edit 1.2: Change inner `normal_body_layout` flex to `use_min_size=false`

Locate the flex assignment line (currently around line 263):

```cpp
  normal_body_layout->SetFlexForView(tab_list_view_, 1, /*use_min_size=*/true);
```

Replace with:

```cpp
  normal_body_layout->SetFlexForView(tab_scroll_view_, 1, /*use_min_size=*/false);
```

Note: the target view changes from `tab_list_view_` to `tab_scroll_view_` (because `tab_scroll_view_` is now the direct child of `tabs_surface_`, not `tab_list_view_`), AND the `use_min_size` flag flips from `true` to `false`.

#### Edit 1.3: Change outer `body_layout` flex to `use_min_size=false`

Locate the flex assignment line (currently around line 264):

```cpp
  body_layout->SetFlexForView(content_host_, 1, true);
```

Replace with:

```cpp
  body_layout->SetFlexForView(content_host_, 1, /*use_min_size=*/false);
```

This is the root-cause fix per Oracle's diagnosis. It tells BoxLayout: "do not reserve `content_host_->GetMinimumSize().height()` before allocating flex remainder; just give `content_host_` the flex share of remaining space after `footer_view_` takes its preferred size."

#### Edit 1.4: Do NOT modify the `root_layout` flex assignment

The root-layout flex on `body_host_` (currently `root_layout->SetFlexForView(body_host_, 1, true)`) is **kept as is**. Reason: at the root level, `body_host_` should consume all space below `top_bar_view_`, and `use_min_size=true` is harmless at this level because `body_host_` itself does not have a runaway minimum size.

### File 2: No changes

No edit is required to `maho_sidebar_view.h`, `maho_sidebar_tab_list_view.cc`, `maho_sidebar_tab_list_view.h`, `maho_sidebar_state_adapter.cc`, or `maho_sidebar_state_models.h`. All previous fixes (encapsulation of `auto_scroller_`, `kColorSysStateHoverOnSubtle`, non-const `AreDropTypesRequired`, `gfx::PointF` in `ScrollByOffset`, forward declarations, folder guard restoration) stay.

## Verification plan

Execute these checks **in order**. Stop at the first failure and report.

### Check 1: LSP diagnostics on modified file

Run `lsp_diagnostics` on `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`. Pre-existing Chromium include-path errors are noise; ignore. Severity-error count above the noise floor must be 0.

### Check 2: Chromium build

Run `python3 maho-chromium/build/scripts/build_maho.py` from repo root. Expected outcome: EXIT=0, `libchrome_dll.dylib` relinked. If build fails on a file unrelated to `maho_sidebar_view.cc` (such as the lucide icon migration files in the user's parallel work), report and stop per repo policy.

### Check 3: Manual visual QA

Relaunch the app via `killall Maho 2>/dev/null; open /Users/indo/code/project/maho-workspace/chromium/src/out/Default/Maho.app`. Verify in tabs mode:

1. Footer is visible at the bottom of the sidebar (space dots, archive button, plus button all rendered).
2. `top_region_` is visible at the top (search bar, favorites grid, space header row with active space icon and name).
3. Tab list is rendered between `top_region_` and footer.
4. When the number of tabs exceeds the visible scroll area, the list scrolls smoothly via mouse wheel or trackpad. Sticky region does not move during scroll.
5. When the number of tabs fits the visible area, no scroll bar is shown (or a non-functional one is present), and the list paints cleanly.
6. Drag a normal tab toward the top or bottom edge of `tab_scroll_view_`. Auto-scroll should trigger within ~24dp of the edge at approximately 120 dp/s.
7. Footer remains visible throughout all of the above interactions.

### Check 4: Diagnostic state cleanup

Verify that `tab_scroll_view_` is no longer assigned `nullptr` in `BuildUi()`. After Edit 1.1, it must be the result of `tabs_surface_->AddChildView(std::move(scroll))`.

### Check 5: Read-back of modified file

Read `maho_sidebar_view.cc` lines 215–270 in full and confirm:

1. The three children of `top_region_` are added in order: `search_view_`, `favorites_view_`, `space_header_view_`. Unchanged.
2. `space_header_view_->SetVisible(false)` is called immediately after creation. Unchanged.
3. The ScrollView setup matches Edit 1.1 exactly.
4. The flex assignments match Edits 1.2 and 1.3 exactly.
5. The root flex on `body_host_` is unchanged from the existing `true`.

## Risks and mitigations

### Risk 1: The flex flag flip affects other layout cases

`body_layout` only has two children: `content_host_` (flex=1) and `footer_view_` (no flex). With `use_min_size=false` on `content_host_`, content_host_ may shrink below its minimum if `body_host_` is unusually small. In practice the sidebar has at least 200dp of body height, and footer preferred is ~50dp, so content_host_ gets at least 150dp, which is enough for `top_region_` plus a tiny scroll viewport.

**Mitigation**: Verify in manual QA that the layout still works with the sidebar resized to its smallest allowed width (which controls minimum height implicitly). If degraded, set a hard minimum height on `content_host_` via `SetMinimumSize` (not used currently in this codebase, but available).

### Risk 2: `MahoSidebarTabListView`'s preferred size still reaches `tabs_surface_`

Even with `tab_scroll_view_` wrapping, if `tab_scroll_view_->GetPreferredSize()` returns a non-zero height (because somewhere in the call chain `ClipHeightTo` is implicitly active), `tabs_surface_`'s preferred would inflate, and through FillLayout into `content_host_`'s preferred, possibly back to the same overflow. Oracle and Zen both confirm that without `ClipHeightTo`, ScrollView's `View::CalculatePreferredSize` returns `(0, 0)` because there is no layout manager.

**Mitigation**: Confirm by inspection that no `ClipHeightTo` call is in the new ScrollView setup (Edit 1.1 omits it). If footer regression returns, run `lsp_find_references` on `ClipHeightTo` across the sidebar tree to ensure no other call site re-introduces the binding.

### Risk 3: Drag-auto-scroll regression

`SidebarAutoScroller::OnDragOver` reads `scroll_view_->height()` and `GetVisibleRect()` to compute edge proximity. With the ScrollView restored and properly sized, both calls return real values. With the ScrollView reverted (current diagnostic state), the auto_scroller is null (because `SetAutoScrollScrollView(nullptr)` was called), and auto-scroll is silently disabled.

**Mitigation**: After fix, manually drag a tab inside the scroll view and verify auto-scroll fires near edges. If not, inspect `SidebarAutoScroller`'s timer and edge-detection logic; this is outside the present plan's scope and would be a separate follow-up.

### Risk 4: The `space_header_view_` Configure → SetVisible window

Oracle noted that `UpdateSidebarSpaceHeaderRow(...)` is called before `SetVisible(...)` in `MahoSidebarView::UpdateTabList()`. During the brief window between the two calls, the row's preferred size might be (~30dp) included in the parent BoxLayout's accounting, potentially flicker. With `use_min_size=false` on the outer flex, this flicker no longer affects footer visibility because content_host_ no longer reserves min space.

**Mitigation**: The `use_min_size=false` change neutralizes this risk. No code change needed.

### Risk 5: Lucide icon migration

The user is independently working on a Lucide icon migration that touches `maho_sidebar_top_bar_view.cc` and other files. If the rebuild fails on those files, the issue is in the user's parallel work, not this fix. Per repo policy and the user's explicit instruction, do not modify lucide-related files; report and stop.

**Mitigation**: Build the entire chromium target after applying edits. If failure is in a file untouched by this plan, surface the error to the user and pause.

## Out-of-scope / follow-ups

1. Hide the scrollbar visually (without disabling functionality). Could re-introduce `SetVerticalScrollBarMode(kHiddenButEnabled)` if desired, but would require user confirmation that the visual change is wanted.
2. Constrain `space_header_view_->Configure()` to only invalidate layout when the icon or name actually changes. Currently it invalidates unconditionally.
3. `AreDropTypesRequired()` on `SidebarSpaceHeaderRow` — Oracle suggested verifying it correctly overrides the base. The compiler error in a prior build session confirmed the base is non-const, and the current non-const override is correct.
4. Investigate whether the lucide top-bar migration affects the sidebar's preferred height calculation. Currently parked because the user is actively working on it.
5. Performance: `GetCoreFooterSpaceState()` is called twice per refresh cycle. Hoist into a per-cycle cache on `MahoSidebarStateAdapter` if profiling shows it as a hotspot. Out of scope for this plan.
