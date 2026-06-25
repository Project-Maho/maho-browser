# Sidebar Sticky Space Header and Tab Scroll Plan

## Goal

Add a sticky "space header" row showing the active space's emoji icon and name to the Maho Browser sidebar. The header lives inside the existing sticky `top_region_` (search + favorites zone), positioned as the **last** child of `top_region_` (i.e. `search → favorites → space_header`). It stays fixed while the pinned/normal tab list scrolls beneath it.

To make the tab list actually scroll under the sticky region (today it merely clips when content overflows), wrap `tab_list_view_` in a vertical-only `views::ScrollView` owned by `MahoSidebarView`. This is the long-promised but missing scroll container that the existing `RebuildRows()` comment and the contract test in `maho_sidebar_dnd_dispatch_unittest.cc:2649–2663` already anticipate.

The header itself is also a drop target: dropping a normal tab onto it pins the tab and inserts it at the top of the pinned list.

The visual outcome the user expects:

1. `top_region_` shows three stacked rows: `search_view_`, `favorites_view_`, `space_header_view_`.
2. `space_header_view_` shows the active space's icon (emoji string from the space's `icon` field) on the left and the space's display name on the right of the icon.
3. Below `top_region_`, the pinned tabs, the pinned/normal separator, the `+ New Tab` inline row, and the normal tabs all scroll vertically as a single scrollable region.
4. When normal/pinned content exceeds the visible scroll viewport height, the user can scroll the tab list. `top_region_` (with the new space header) does not move.
5. Dragging a normal tab onto `space_header_view_` results in the tab being pinned and inserted at the top of the pinned list (or appended if pinned is empty). Dropping anywhere on the header is treated identically to dropping on the topmost insertion lane of the pinned section.
6. While dragging anywhere over the scrollable tab area, dragging within ~24dp of the viewport's top or bottom edge auto-scrolls the viewport at ~120 dp/s.

## Target contract

After this plan is implemented, the following invariants must hold simultaneously:

### Layout contract — `MahoSidebarView::BuildUi()` view tree

1. `top_region_->children().size() == 3`.
2. `top_region_->children()[0] == search_view_`.
3. `top_region_->children()[1] == favorites_view_`.
4. `top_region_->children()[2] == space_header_view_`.
5. `space_header_view_->GetVisible()` is `true` whenever `MahoSidebarTabListModel::active_space_name` is non-empty, and `false` otherwise.
6. `tabs_surface_->children().size() == 2`. The first is `top_region_`, the second is `tab_scroll_view_`.
7. `tab_scroll_view_` is a `views::ScrollView`. Its `contents()` is `tab_list_view_` (a `MahoSidebarTabListView`).
8. `tabs_surface_`'s `BoxLayout` assigns flex `(1, /*shrink=*/true)` to `tab_scroll_view_` only. `top_region_` keeps its preferred size (no flex). This is what produces the sticky behavior.
9. `tab_scroll_view_->GetHorizontalScrollBarMode() == views::ScrollView::ScrollBarMode::kDisabled`. Vertical scroll bar mode is `kHiddenButEnabled` (scrollable but no visible bar; matches Arc/Zen sidebar feel).
10. `tab_scroll_view_->ClipHeightTo(0, INT_MAX)` so it grows to consume the full flex allocation.

### TabListView invariant — preserved from the current contract test

11. `MahoSidebarTabListView::tab_rows_->parent() == MahoSidebarTabListView*` itself. The contract test at `maho_sidebar_dnd_dispatch_unittest.cc:2649–2663` already asserts this and must continue passing without modification. The ScrollView wraps `MahoSidebarTabListView` from the outside; internal structure of `MahoSidebarTabListView` is unchanged.

### Data model contract — `MahoSidebarTabListModel`

12. `MahoSidebarTabListModel` has two new fields: `std::string active_space_icon;` (raw emoji string; empty if none) and `std::u16string active_space_name;` (display name; empty if no active space resolved).
13. `MahoSidebarStateAdapter::BuildTabListModel(browser)` populates both fields by reusing the existing `GetCoreFooterSpaceState()` helper at `maho_sidebar_state_adapter.cc:104` and indexing into its parsed `space_icons` and `space_names` vectors using `active_space_index`. No new FFI calls are introduced.
14. When `active_space_index` is `-1` or out of range, both fields are left empty.

### Drop forwarding contract — `SidebarSpaceHeaderRow`

15. `SidebarSpaceHeaderRow` accepts the same `application/x-maho-sidebar-drag` payload as `SidebarSectionDropTarget`.
16. On drop, it constructs the same `SidebarDropPlan` that `SidebarSectionDropTarget(SectionKind::kPinned)` would for an empty drop region: a `pin_tab` step (when `payload.origin == kNormalSection`), followed by a `reorder_root_item` step that places the tab as the first child of the pinned section (`before_node_id = first_pinned_id` when pinned has children, else `append = true`).
17. The header drop reuses `PlanSectionDrop(SectionKind::kPinned, payload)` and `ExecuteDropPlan(plan)` — no new drop logic is introduced. Only the wiring is new.
18. Dropping a payload whose `origin` is already `kPinnedSection` onto the header is a no-op (or equivalently, a reorder-to-top within the pinned section). Mirror whatever `SidebarSectionDropTarget(kPinned)` does today for same-section drops. Do not invent new behavior.

### Drag-auto-scroll contract — `tab_scroll_view_`

19. While a Maho sidebar drag is in progress and the cursor is inside `tab_scroll_view_`'s viewport bounds, if the cursor's y coordinate is within `kAutoScrollEdgeDp = 24` of either edge of the visible rect, the scroll view scrolls toward that edge at `kAutoScrollStepDp = 4` per timer tick (timer frequency `kAutoScrollHz = 30`, giving ~120 dp/s).
20. The auto-scroll timer is started on `OnDragEntered` / `OnDragUpdated` of any drop target inside the scroll view (or by a single observer at the `MahoSidebarTabListView` level), and stopped on `OnDragExited`, drop completion, or drop cancellation.
21. Auto-scroll never triggers outside an active drag. Static mouse hover near the edge does nothing.

### Update path contract — `MahoSidebarView::UpdateTabList()`

22. After `tab_list_view_->Update(model, browser_)`, `MahoSidebarView` also calls `UpdateSpaceHeaderRow(space_header_view_, model.active_space_icon, model.active_space_name)` (or equivalent member method). The header refresh is part of the same model-push cycle; no separate refresh path is introduced.

## Explicit non-goals

1. No interactive behavior on `space_header_view_` beyond drop target. No click handler, no context menu, no rename, no icon picker. v1 is read-only display + drop forwarding.
2. No sticky behavior change for `top_bar_view_` or `footer_view_`. Both stay exactly as today.
3. No change to `search_view_` or `favorites_view_` rendering, position relative to each other (search above favorites), or internal logic.
4. No change to `MahoSidebarTabListView`'s internal `tab_rows_` structure: pinned section drop target, separator, `+ New Tab`, normal section drop target, insertion lanes, row classes — all unchanged.
5. No change to `SidebarTabRowView`, `SidebarFolderRowView`, `SidebarSectionDropTarget`, `SidebarInsertionLaneView`, `SidebarSeparatorRow`. These are read-only references for this plan.
6. No change to `PlanSectionDrop`, `ExecuteDropPlan`, `ResolveDropPlan`, `SidebarDropPlan`, or any other DnD planner type. The header drop reuses these as-is.
7. No new FFI exports. Reuse `maho_core_get_space_view_models` and `maho_core_get_active_space_id` via the existing `GetCoreFooterSpaceState()` helper.
8. No new files. All new code goes into existing `.cc` and `.h` files. This avoids any `BUILD.gn` modification, per `AGENTS.md` "GN 파일 강제 수정 금지" rule.
9. No change to library-mode surfaces (`library_container_`, `library_rail_view_`, archive/downloads/media/spaces/notes/easels panes). The sticky header lives only in tabs mode.
10. No change to `MahoSidebarFooterView`, the space dot row, or the existing horizontal `scroll_view_` inside the footer.
11. No change to Mojo IDL (`*.mojom`) or any WebUI code. Sidebar tab UI is native Views; nothing crosses the WebUI boundary in this plan.
12. No change to space data model in Rust (`Space`, `SpaceViewModel`, `SpaceManager`). Read-only consumer side only.
13. No drag-auto-scroll for non-tab-list ScrollViews (footer, archive, downloads, media, spaces, create-space, library panes). Out of scope for v1.
14. Static comment at `maho_sidebar_tab_list_view.cc:2436` ("outer scroll view owned by MahoSidebarView") becomes accurate after this plan. Removing or rewording the comment is allowed but not required.

## Current code findings

All paths below are absolute relative to repo root `/Users/indo/code/project/maho-workspace`.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` and `.h`

`MahoSidebarView::BuildUi()` (lines ~165–260) builds this view tree:

```
MahoSidebarView                   BoxLayout VERTICAL, kRailInsets, kSectionSpacingDp, kStretch
├── top_bar_view_                 first child, no flex
└── body_host_                    flex 1, BoxLayout VERTICAL insets=0 spacing=0
    ├── content_host_             flex 1, FillLayout
    │   ├── tabs_surface_         BoxLayout VERTICAL insets=0 spacing=kSectionSpacingDp(5dp)
    │   │   ├── top_region_       NO flex, BoxLayout VERTICAL insets=0 spacing=kTopRegionSpacingDp(6dp)
    │   │   │   ├── search_view_
    │   │   │   └── favorites_view_
    │   │   └── tab_list_view_    flex 1 shrink=true
    │   └── library_container_    out of scope; not touched
    └── footer_view_              fixed bottom; not touched
```

Key facts about today's behavior:

1. `top_region_` is **already sticky**: it is non-flex inside `tabs_surface_`'s `BoxLayout`. It always claims its preferred height first. `tab_list_view_` is the flex child and consumes the rest.
2. There is **no `views::ScrollView` anywhere** in the tab list path. `tab_list_view_` simply receives an allocated rect; if its content exceeds that rect, it clips. The intended scroll container is documented (in `maho_sidebar_tab_list_view.cc:2436` comment and the contract test at `maho_sidebar_dnd_dispatch_unittest.cc:2649–2663` justification) but not implemented.
3. `MahoSidebarView` exposes member pointers `raw_ptr<views::View> top_region_`, `raw_ptr<MahoSidebarSearchView> search_view_`, `raw_ptr<MahoSidebarFavoritesGridView> favorites_view_`, `raw_ptr<MahoSidebarTabListView> tab_list_view_`. There is no `tab_scroll_view_` member yet and no `space_header_view_` member yet.
4. The flex assignment at the bottom of `BuildUi()` is `normal_body_layout->SetFlexForView(tab_list_view_, 1, true);`. After this plan, the flex target moves to `tab_scroll_view_`.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`

`MahoSidebarTabListModel` already has `std::string active_space_id;`. It does not yet have `active_space_icon` or `active_space_name`.

`MahoSidebarFooterModel` (same file) has `std::vector<std::string> space_icons;`, `std::vector<std::string> space_names;`, `std::vector<std::string> space_ids;`, and `int active_space_index;`. These already exist and are populated by the state adapter for the footer's space dot row. This is the data we need to read for the new header — but populated into `MahoSidebarTabListModel` instead.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` and `.h`

`GetCoreFooterSpaceState()` at line 104 is a file-local helper that:

1. Calls `maho_core_get_space_view_models(core_ptr)` and `maho_core_get_active_space_id(core_ptr)`.
2. Parses the JSON and returns a struct containing parallel `space_icons`, `space_names`, `space_colors`, `space_ids` vectors, plus `int active_space_index`.
3. Frees the FFI-returned strings via `maho_string_free()`.

`BuildFooterModel()` at line ~913 already calls `GetCoreFooterSpaceState()` and copies the parsed fields into `MahoSidebarFooterModel`. `BuildTabListModel()` at line ~784 does **not** currently call this helper because it does not need to today.

After this plan, `BuildTabListModel()` will call `GetCoreFooterSpaceState()` and copy the active space's icon and name into the new `active_space_icon` and `active_space_name` fields. There is exactly one helper call shared between both builders; no FFI duplication.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` and `.h`

`MahoSidebarTabListView::RebuildRows()` (lines ~2381–2458) builds `tab_rows_` children in this order: pinned section drop target, separator, `+ New Tab`, normal section drop target. **No change** to this function in this plan beyond, optionally, adjusting the stale comment at line ~2436.

`SidebarSeparatorRow` (anonymous class, lines ~235–257) and `SidebarSectionDropTarget` (anonymous, owns `PlanSectionDrop` invocation and drop-callback wiring) are the closest existing patterns for the new `SidebarSpaceHeaderRow`. The header is a sibling-style anonymous class inside the same `.cc` file.

`SidebarSectionDropTarget`'s drop methods (`GetDropFormats`, `AreDropTypesRequired`, `CanDrop`, `OnDragEntered`, `OnDragUpdated`, `OnDragExited`, `GetDropCallback`) are the template the header copies, narrowed to always treat the drop as section-pinned.

`MahoSidebarSpaceDotView::Configure(space_id, name, icon, color, is_active)` is the canonical existing renderer for a single space's emoji icon. The header reuses the same emoji-as-`views::Label` pattern: a `views::Label` with `SetText(base::UTF8ToUTF16(icon))` and a font sized to match the dot view's icon label.

`MahoSidebarTabListView` does **not** become the owner of `space_header_view_`. The header is owned by `MahoSidebarView` and lives in `top_region_`. The tab list view exposes a small free-function factory and updater for the header row so that both creation (in `MahoSidebarView::BuildUi()`) and per-update refresh (in `MahoSidebarView::UpdateTabList()`) can call into the same code.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` (lines ~2649–2663)

Asserts: `EXPECT_EQ(rows->parent(), &list)` with a comment that "tabs-mode vertical scrolling belongs to MahoSidebarView". This invariant is preserved by this plan because the ScrollView wraps `MahoSidebarTabListView` from the outside; inside `MahoSidebarTabListView`, `tab_rows_` is still a direct child of the list view.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_util.h` and `.cc`

`CreateSidebarDragImage()` builds the drag image bitmap. It does not depend on any specific ancestor coordinate system. ScrollView introduction does not affect drag image creation.

`ReadMahoDragData(data, &payload)` and `WriteMahoDragData(payload, &data)` are payload (de)serialization helpers used by both source and target sides today. The new header drop target uses `ReadMahoDragData` exactly like `SidebarSectionDropTarget` does.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc` and `.h`

`PlanSectionDrop(SectionKind section, const SidebarDragPayload& payload)` already returns a fully-formed `SidebarDropPlan` for whole-section drops. Calling it with `SectionKind::kPinned` produces the right plan for "pin this tab and place it at top of pinned list". The header drop reuses this entry point unchanged.

`ExecuteDropPlan(const SidebarDropPlan& plan)` dispatches the steps via the existing `MahoCore` ShellEvent surface. Unchanged.

## File-by-file implementation plan

Five files modified in total. No new files.

### File 1: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`

#### Edit 1.1: Add two fields to `MahoSidebarTabListModel`

Locate the `MahoSidebarTabListModel` struct (around line 253). Find the existing `std::string active_space_id;` field. Immediately below it, add:

```cpp
// Active space's icon (raw emoji string, e.g. "💼"). Empty if no icon set.
std::string active_space_icon;

// Active space's display name. Empty if no active space resolved.
std::u16string active_space_name;
```

Do not reorder existing fields. Do not change visibility, default-initialization, or any other aspect of the struct.

### File 2: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

#### Edit 2.1: Populate the two new fields in `BuildTabListModel()`

Locate `MahoSidebarStateAdapter::BuildTabListModel(Browser* browser)` around line 784. After the existing field assignments and before the return, add a call to `GetCoreFooterSpaceState()` and copy the active space's icon and name:

```cpp
// Populate active space header fields for the sidebar's sticky space header row.
const auto space_state = GetCoreFooterSpaceState(core_ptr);
if (space_state.active_space_index >= 0 &&
    space_state.active_space_index <
        static_cast<int>(space_state.space_names.size())) {
  model.active_space_icon =
      space_state.space_icons[space_state.active_space_index];
  model.active_space_name = base::UTF8ToUTF16(
      space_state.space_names[space_state.active_space_index]);
}
// If active_space_index is invalid, both fields stay default-constructed (empty).
```

`core_ptr` is the same maho-core handle the function already uses for other adapter calls. Reuse whatever local variable already holds it. Do not introduce a second handle acquisition.

#### Edit 2.2: No other change in this file

`BuildFooterModel()` already calls `GetCoreFooterSpaceState()`. Calling it twice per refresh cycle (once for tab list, once for footer) is acceptable for v1; the helper is cheap (one FFI call, JSON parse). If profiling later shows this is a hotspot, hoist into a shared per-cycle cache. Not in scope for this plan.

### File 3: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

#### Edit 3.1: Add `SidebarSpaceHeaderRow` anonymous class

Locate the anonymous namespace that contains `SidebarSeparatorRow`, `SidebarInlineRow`, and `SidebarSectionDropTarget` (top of file, before `MahoSidebarTabListView` class definition). Add a new class `SidebarSpaceHeaderRow` in the same anonymous namespace.

Class spec:

1. Inherits `views::View`.
2. Constructor signature: `SidebarSpaceHeaderRow()`. No parameters; values are pushed via `Configure()`.
3. Members: `raw_ptr<views::Label> icon_label_;`, `raw_ptr<views::Label> name_label_;`.
4. Constructor body: sets `BoxLayout(HORIZONTAL)` with insets matching adjacent tab rows. Use existing layout token if one matches (`kRowInsets`, `kTabRowInsets`, or whatever is defined in `maho_sidebar_layout_tokens.h`). If no exact match exists, use `gfx::Insets::TLBR(4, 12, 4, 12)`. Cross-axis alignment `kCenter`. Spacing between icon and name: 8dp.
5. Add `icon_label_` (a `views::Label` with empty initial text, font matching `MahoSidebarSpaceDotView`'s emoji label — read that file for the exact font setup; if it uses `gfx::FontList` of size 14, do the same).
6. Add `name_label_` (a `views::Label` with empty initial text, secondary text color via `views::style::CONTEXT_LABEL` and `views::style::STYLE_SECONDARY` or whichever style the existing sidebar section labels use; if no precedent exists, use `ui::kColorSysOnSurfaceSubtle`).
7. Public method: `void Configure(const std::string& icon, const std::u16string& name)`. Sets `icon_label_->SetText(base::UTF8ToUTF16(icon))`, `icon_label_->SetVisible(!icon.empty())`, `name_label_->SetText(name)`. Triggers a layout invalidate via `InvalidateLayout()` so the row resizes if the icon visibility changes.
8. Override `views::View::GetDropFormats(int* formats, std::set<ui::ClipboardFormatType>* types)`: register the `application/x-maho-sidebar-drag` custom format. Mirror exactly what `SidebarSectionDropTarget::GetDropFormats` does.
9. Override `AreDropTypesRequired() const`: return `true`.
10. Override `CanDrop(const OSExchangeData& data) const`: parse the payload via `ReadMahoDragData`. Return `true` only when payload origin is `kNormalSection` (drop is meaningful) or `kPinnedSection` (no-op or top-of-section reorder). Reject favorites and unknown origins.
11. Override `OnDragEntered(const ui::DropTargetEvent& event)`: visually mark the header as a drop hover target (e.g. background change to `ui::kColorSysHoverOnSubtle`). Read what `SidebarSectionDropTarget` does and mirror it.
12. Override `OnDragUpdated(const ui::DropTargetEvent& event)`: return `ui::mojom::DragOperation::kMove`.
13. Override `OnDragExited()`: clear the hover background.
14. Override `GetDropCallback(const ui::DropTargetEvent& event)`: parse payload, call `PlanSectionDrop(SectionKind::kPinned, payload)`, capture the resulting `SidebarDropPlan`, and return a callback that invokes `ExecuteDropPlan(captured_plan)`. This is exactly the pattern `SidebarSectionDropTarget` uses; do not invent a parallel implementation.
15. Wire the auto-scroll edge detection (see Edit 3.3 below) by forwarding `OnDragUpdated` events to the parent ScrollView's auto-scroll observer if needed. Most likely the auto-scroll lives at the `MahoSidebarTabListView` or `MahoSidebarView` level, so the header itself does nothing for auto-scroll.

#### Edit 3.2: Expose factory and updater functions for the header

Add these two free functions at the bottom of the same anonymous namespace, but **expose them publicly** by declaring them in the anonymous namespace's surrounding scope at the top of the file (or move them to file-internal-linkage `static` functions in the same translation unit, then expose via two short non-static thunks at the end of the namespace). The cleanest approach is to add them to the public `maho` or `sidebar` namespace inside the file:

```cpp
// At the bottom of the .cc file, in the file's outer namespace:
std::unique_ptr<views::View> CreateSidebarSpaceHeaderRow() {
  return std::make_unique<SidebarSpaceHeaderRow>();
}

void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name) {
  if (!row) {
    return;
  }
  static_cast<SidebarSpaceHeaderRow*>(row)->Configure(icon, name);
}
```

Add forward declarations for these two functions to `maho_sidebar_tab_list_view.h` (in the same namespace as `MahoSidebarTabListView`). No class definition for `SidebarSpaceHeaderRow` is exposed; the header only sees `views::View*` opaque handles. Implementation detail (the `SidebarSpaceHeaderRow` class) stays in the anonymous namespace inside the `.cc` file.

#### Edit 3.3: Add drag-auto-scroll observer

Add a new file-local class `SidebarAutoScroller` in the anonymous namespace of `maho_sidebar_tab_list_view.cc`. Spec:

1. Constructor: `SidebarAutoScroller(views::ScrollView* scroll_view)`. Stores raw pointer; does not own.
2. Method: `void OnDragOver(const gfx::Point& screen_point)`. Computes the cursor position in the scroll view's local coordinates via `views::View::ConvertPointFromScreen`. If the cursor's y is within `kAutoScrollEdgeDp = 24` of `GetVisibleRect()`'s top or bottom edge, sets a direction flag (`+1` or `-1`) and starts the timer if not already running.
3. Method: `void OnDragEnded()`. Stops the timer; clears direction flag.
4. Internal: `base::RepeatingTimer timer_` firing at 30 Hz (`base::Hertz(30)`); each tick calls `scroll_view_->ScrollByOffset(gfx::Vector2d(0, kAutoScrollStepDp * direction_))` where `kAutoScrollStepDp = 4`.
5. Constants live as `constexpr int kAutoScrollEdgeDp = 24;` and `constexpr int kAutoScrollStepDp = 4;` near the top of the anonymous namespace.

Wire the auto-scroller into `MahoSidebarTabListView`:

1. Add `std::unique_ptr<SidebarAutoScroller> auto_scroller_;` member to `MahoSidebarTabListView` (in `maho_sidebar_tab_list_view.h`, private section).
2. Add a public setter `void SetAutoScrollScrollView(views::ScrollView* scroll_view)` that initializes `auto_scroller_` lazily on first non-null call. `MahoSidebarView::BuildUi()` calls this after creating `tab_scroll_view_`.
3. In `MahoSidebarTabListView::OnDragUpdated(const ui::DropTargetEvent& event)` (which already exists or is added per the existing drop forwarding flow on the outer list), forward to `auto_scroller_->OnDragOver(...)`.
4. In `MahoSidebarTabListView::OnDragExited()`, call `auto_scroller_->OnDragEnded()`.
5. In `MahoSidebarTabListView::GetDropCallback()`, after constructing the callback, also call `auto_scroller_->OnDragEnded()` to stop the timer when the drop completes (the callback may run later, but auto-scroll must stop the moment the drop is committed).

If the existing `MahoSidebarTabListView` does not have its own drag-target overrides, add minimal pass-through overrides whose only job is to invoke the auto-scroller and otherwise return `kMove`. The existing per-row and per-section drop targets continue to handle the actual drop semantics.

#### Edit 3.4: Optional comment cleanup

The stale comment at `maho_sidebar_tab_list_view.cc:2436` ("outer scroll view owned by MahoSidebarView commits the new content bounds") becomes accurate after this plan. No edit required. Optionally simplify it.

### File 4: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`

#### Edit 4.1: Add two new member pointers

In the private section of `MahoSidebarView`, near the existing `raw_ptr<views::View> top_region_;`, `raw_ptr<MahoSidebarTabListView> tab_list_view_;`, and `raw_ptr<MahoSidebarFavoritesGridView> favorites_view_;` declarations, add:

```cpp
raw_ptr<views::View> space_header_view_ = nullptr;
raw_ptr<views::ScrollView> tab_scroll_view_ = nullptr;
```

`space_header_view_` is `views::View*` (not `SidebarSpaceHeaderRow*`) because the implementation class stays anonymous in the `.cc` file. `tab_scroll_view_` is a `views::ScrollView*` and requires `#include "ui/views/controls/scroll_view.h"` if not already included.

### File 5: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

#### Edit 5.1: Include header

Add `#include "ui/views/controls/scroll_view.h"` if not already present.

Add include for the tab list view's new factory/updater declarations (likely already present via `maho_sidebar_tab_list_view.h`).

#### Edit 5.2: Modify `BuildUi()` — add space header to `top_region_`

Locate the spot in `BuildUi()` (around line 211–212) where `favorites_view_` is added to `top_region`. Immediately after that line, add:

```cpp
space_header_view_ =
    top_region->AddChildView(CreateSidebarSpaceHeaderRow());
space_header_view_->SetVisible(false);  // Hidden until first model update.
```

Order is now `search_view_` → `favorites_view_` → `space_header_view_` as the three children of `top_region_`.

#### Edit 5.3: Modify `BuildUi()` — wrap `tab_list_view_` in `views::ScrollView`

Replace the current `tab_list_view_` direct add (around line 213–214) and the subsequent flex assignment with:

```cpp
auto scroll = std::make_unique<views::ScrollView>();
scroll->SetBackgroundColor(absl::nullopt);
scroll->SetHorizontalScrollBarMode(
    views::ScrollView::ScrollBarMode::kDisabled);
scroll->SetVerticalScrollBarMode(
    views::ScrollView::ScrollBarMode::kHiddenButEnabled);
scroll->SetDrawOverflowIndicator(false);
scroll->ClipHeightTo(0, std::numeric_limits<int>::max());

auto tab_list = std::make_unique<MahoSidebarTabListView>(browser_);
tab_list_view_ = scroll->SetContents(std::move(tab_list));
tab_scroll_view_ = tabs_surface_->AddChildView(std::move(scroll));

tab_list_view_->SetAutoScrollScrollView(tab_scroll_view_);
```

Update the flex assignment immediately below this block:

```cpp
normal_body_layout->SetFlexForView(tab_scroll_view_, 1, /*use_min_size=*/true);
```

The previous `SetFlexForView(tab_list_view_, 1, true);` call must be removed. Confirm that the only flex assignment in `tabs_surface_`'s `BoxLayout` is on `tab_scroll_view_` after the edit.

#### Edit 5.4: Modify `UpdateTabList()` — refresh space header

Locate `MahoSidebarView::UpdateTabList(const MahoSidebarTabListModel& model)` (or whichever method pushes models into the tab list view). After the `tab_list_view_->Update(model, browser_)` call, add:

```cpp
UpdateSidebarSpaceHeaderRow(space_header_view_, model.active_space_icon,
                            model.active_space_name);
space_header_view_->SetVisible(!model.active_space_name.empty());
```

If the visibility toggle changes the row's visibility, the `top_region_` will relayout automatically because `BoxLayout` recomputes when child visibility changes. No explicit `InvalidateLayout()` call needed at the `MahoSidebarView` level.

#### Edit 5.5: No change to `ShowCreateSpace()`, library-mode wiring, or footer

These are out of scope. Confirm that the new `space_header_view_` and `tab_scroll_view_` members are not referenced from any code path other than `BuildUi()` and `UpdateTabList()`.

## Verification plan

Execute these checks **in order**. Stop at the first failure and report.

### Check 1: LSP diagnostics on all five modified files (parallel)

Run `lsp_diagnostics` on:

1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
2. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
3. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
4. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
5. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

Severity `error` must be empty for each. Warnings should be reviewed but do not block.

### Check 2: Rust workspace typecheck

Run `cd maho && bun run typecheck` from repo root. Must pass. Rust types are not directly affected by this plan, but the FFI surface is shared, so typecheck guards against accidental breakage.

### Check 3: Rust workspace tests

Run `cd maho && bun run test` from repo root. Must pass.

### Check 4: Read-back the contract test

Read `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` lines 2649–2663. Confirm the assertion `EXPECT_EQ(rows->parent(), &list)` is unchanged and is still satisfied by the new code: `rows` (which is `MahoSidebarTabListView::tab_rows_`) still has `MahoSidebarTabListView` as its direct parent. The ScrollView wraps `MahoSidebarTabListView` from the outside.

### Check 5: Read-back all five modified files

Read each modified file in full and check:

1. No `// TODO` or `// FIXME` left.
2. No `as_any`-style casts or unsafe pointer manipulation (apart from the documented `static_cast` from `views::View*` to `SidebarSpaceHeaderRow*` inside `UpdateSidebarSpaceHeaderRow`, which is safe because creation is via `CreateSidebarSpaceHeaderRow`).
3. No new `#include` that does not have a corresponding new use.
4. All new `raw_ptr` members default-initialize to `nullptr` and are reset in destructors if applicable.
5. No new file added to disk. The diff touches only the five files listed above.

### Check 6: Manual reasoning audit (no Chromium build)

Walk through these scenarios in code reasoning, since a full Chromium build is too expensive to run during verification:

1. **Active space exists, has icon and name**: `BuildTabListModel` populates both fields. `UpdateTabList` calls `UpdateSidebarSpaceHeaderRow` with non-empty values. `space_header_view_->SetVisible(true)`. `top_region_` has three visible children stacked.
2. **Active space has name but no icon**: icon_label hidden, name_label visible. Header still visible.
3. **No active space (initial state, race)**: `active_space_index == -1`. Both fields empty. `space_header_view_->SetVisible(false)`. `top_region_` shows two children. Layout collapses gracefully.
4. **Active space changes via ShellEvent**: next `UpdateTabList` cycle pushes new model. Header refreshes with new icon and name on the next refresh tick.
5. **User drags normal tab onto header**: `OnDragEntered` paints hover background. `OnDragUpdated` returns `kMove`. `GetDropCallback` returns a closure that runs `PlanSectionDrop(kPinned, payload)` then `ExecuteDropPlan`. Drop completes; `OnDragExited` clears background.
6. **User drags normal tab into auto-scroll edge zone**: `MahoSidebarTabListView::OnDragUpdated` forwards to `auto_scroller_->OnDragOver`. Timer starts. ScrollView scrolls. On exit or drop, `OnDragEnded` stops timer.
7. **User drops onto the first insertion lane below the header (existing behavior)**: unchanged. Existing `SidebarInsertionLaneView` handles it. The header drop and the lane drop are visually adjacent but logically equivalent for this case.
8. **Tab content fits within viewport (no scroll needed)**: `views::ScrollView` with `ClipHeightTo(0, INT_MAX)` sizes to content. No scroll bar appears (`kHiddenButEnabled`). Visually identical to today.
9. **Tab content exceeds viewport**: ScrollView clips. User scrolls with mouse wheel or trackpad. Sticky region (`top_bar_view_`, `top_region_` with new header) does not move. Footer also stays put.
10. **Library mode active**: `tabs_surface_` is hidden by the existing `content_host_` FillLayout swap. New header and ScrollView both implicitly hidden. No regression in library mode.

## Risks and mitigations

### Risk 1: ScrollView introduction breaks DnD coordinate handling

Drag events use widget-local or screen coordinates. `views::ScrollView` translates child coordinates by its scroll offset, but Views handles this transparently via `ConvertPointFromWidget`/`ConvertPointToWidget` in the View hierarchy walk. The DnD reference doc at `maho-chromium/docs/chromium-sidebar-dnd-reference.md` does not flag scroll-offset issues for the existing scrolled surfaces (archive, downloads, media, etc.), so the same machinery should handle the tab list correctly.

**Mitigation**: After implementation, manually trace one drag operation through the code: drag start in `SidebarTabRowView`, drag image creation in `CreateSidebarDragImage`, hit-testing through ScrollView to a target row, payload read in `ReadMahoDragData`, plan resolution. Confirm no assumption is made about an unscrolled tab list parent.

### Risk 2: Auto-scroll timer leaks across drop completion

If `OnDragEnded` is not called in the drop callback path, the timer keeps firing and continues to scroll the view after the user released the mouse.

**Mitigation**: Edit 3.3 step 5 explicitly stops the timer in `GetDropCallback`. Additionally, `OnDragExited` is the standard cleanup hook and is called in all cancel paths. Belt and suspenders.

### Risk 3: Hover synthesis issues during fast scroll

The stale comment at `maho_sidebar_tab_list_view.cc:2436` mentions "outer scroll view owned by MahoSidebarView commits the new content bounds". This refers to a known Views quirk: when a view's content is repositioned, hover state can become stale until layout commits. The new ScrollView is what the comment was anticipating, so behavior should improve, not regress.

**Mitigation**: If hover ghosting appears during testing, call `tab_scroll_view_->Layout()` or `DeprecatedLayoutImmediately()` in the path that synthesizes hover. Defer to follow-up if not observed.

### Risk 4: `views::ScrollView` Clipboard format propagation

When a child view inside a ScrollView declares custom drop formats, Views must propagate those formats to the ScrollView's drag-target chain. This works for archive/downloads/media today, so the precedent is established.

**Mitigation**: If drop formats are not received by inner targets, override `views::View::GetDropFormats` on the ScrollView itself (or its inner viewport) to propagate. Address only if observed.

### Risk 5: Two `GetCoreFooterSpaceState()` calls per refresh cycle

`BuildTabListModel` and `BuildFooterModel` both now call this helper. Each call performs one FFI call plus JSON parse.

**Mitigation**: Acceptable for v1. If profiling shows this is a hotspot, hoist into a per-refresh-cycle cache on `MahoSidebarStateAdapter`. Out of scope for this plan.

## Out-of-scope / follow-ups

1. Click-to-rename or context menu on `space_header_view_`. Header is read-only in v1.
2. Sticky sub-header within the scrollable tab list (e.g. pinned section label). Not needed; the new top-of-region space header subsumes that semantic.
3. Drag-auto-scroll for archive/downloads/media/spaces panes. Out of scope.
4. Animated transitions when active space changes (header crossfade, slide). Out of scope; v1 is instant swap.
5. Header collapse on small sidebar widths. The header inherits `top_region_`'s width and the existing favorites grid handles narrow widths gracefully. If the new header's icon+name combination overflows, follow up with ellipsis on `name_label_` only.
6. Per-cycle adapter cache for `GetCoreFooterSpaceState()` results. Risk 5 mitigation; out of scope.
7. Comment refresh at `maho_sidebar_tab_list_view.cc:2436`. Optional; no functional impact.
