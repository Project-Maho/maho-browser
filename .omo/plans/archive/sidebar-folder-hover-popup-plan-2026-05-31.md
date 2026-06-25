# Sidebar Folder Hover Popup — Implementation Plan

**Date:** 2026-05-31
**Surface:** Native Views (C++)
**Scope:** Add a hover-triggered popup that displays a folder's tab list with a search input, anchored to closed/collapsed sidebar folder rows.

---

## 1. User Story

When the user hovers over a folder row in the sidebar, after a short delay a popup appears anchored to the row.

**Closed (collapsed) folder** — full popup:
- A **search input** at the top with placeholder `Search <folder name>…`
- A scrollable list of the folder's children: tabs (favicon + title) and sub-folders (folder icon + name) in their stored order
- Clicking a tab row → activate that tab and dismiss
- Clicking a sub-folder row → navigate into it (breadcrumb pushes; popup contents replaced with the sub-folder's children)

**Expanded folder** — search-only mode:
- Only the **search input** is visible (since the tab list is already shown inline in the sidebar)
- As the user types, matching tabs from the folder appear in a results section below the input
- When the input is empty, no list is shown — popup is search-only

Common dismissal: Esc, mouse leave (with grace period — see §5.3), tab activated externally.

Reference UX: see attached screenshot — left side is the sidebar with closed folders; right side is the closed-folder popup with search bar + tab rows.

Reference UX: see attached screenshot — left side is the sidebar with closed folders; right side is the popup with search bar + tab rows.

---

## 2. Discovery Summary

Three explore agents produced the following ground truth:

### 2a. Sidebar is 100% Native Views
- The sidebar is implemented entirely in C++ Views under `maho-chromium/browser/ui/views/sidebar/`. **There is no WebUI / Mojo surface for the sidebar.**
- The `chrome://maho-sidebar` reference in some AGENTS notes does not correspond to actual files — disregard.

### 2b. Folder row class
- **`SidebarFolderRowView`** in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.{h,cc}`
  - Header: `h:337-449`
  - Constructor: `cc:1716`
  - `OnMouseEntered`: `cc:1889` (currently only calls `UpdateAppearance()`)
  - `OnMouseExited`: `cc:1894` (currently only calls `UpdateAppearance()`)
  - `is_expanded_` is a `const bool` member at `h:430`
  - `ShowContextMenuForViewImpl` (right-click) at `cc:1804` — separate from hover popup, leave intact

### 2c. Folder + tab data is already available in the row
- `MahoSidebarStateAdapter::BuildTabListModel()` calls `maho_core_get_sidebar_tree(...)` once and parses the JSON into `std::vector<SidebarTreeNode>`.
- Each `SidebarTreeNode` for a folder already has `children` (vector of nested `SidebarTreeNode` — tabs and sub-folders), plus `folder_name` (`std::u16string`), `is_expanded`, etc.
- **The data flows into `SidebarFolderRowView` at construction time. No new FFI / Mojo / Rust work is required for this feature.**

### 2d. Reusable patterns in the codebase

| Concern | Reference class | Where |
|---|---|---|
| Hover-delay timer + non-stealing bubble | `MahoTabPreviewController` | `browser/ui/tab_preview/maho_tab_preview_controller.{h,cc}` |
| Sidebar-anchored popover with row list, hover highlight, keyboard nav, Esc dismiss | `MahoSidebarActionPopoverView` | `browser/ui/views/sidebar/maho_sidebar_action_popover_view.{h,cc}` |
| `views::Textfield` → `ContentsChanged` → filter results list synchronously | `MahoCommandOverlayView` | `browser/ui/views/command/maho_command_overlay_view.{h,cc}` |

### 2e. Established constants (reuse directly)
- Hover show delay: **`base::Milliseconds(225)`** (`MahoTabPreviewController::kTabPreviewShowDelay`)
- Test wait headroom: 350ms over the 225ms delay
- Visual chrome: `kCardCornerRadius=16`, `kRowCornerRadius=10`, `kRowHeight=48`, `kRowWidth=256` (from `MahoSidebarActionPopoverView`)
- Background / border colors: `ui::kColorSysSurface2`, `ui::kColorSysNeutralOutline`

### 2f. Icons
- Use `maho_lucide_icons::kSearchIcon` via `ui::ImageModel::FromVectorIcon(...)`. Search icon already exists; `folder` icon may need `.icon` file added under `maho-chromium/browser/ui/views/maho_lucide_icons/` if needed.
- License: `LICENSES/lucide.txt` already present.

### 2g. Folder name localization
- `Folder::name` (Rust) / `node.folder_name` (C++ `std::u16string`) is **user-defined free-form text**, not a localization key. The placeholder `Search <node.folder_name>…` works for any language including Korean.

---

## 3. Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ MahoSidebarTabListView                                          │
│   ├── tab_preview_controller_       (existing, for tab rows)    │
│   └── folder_hover_controller_      (NEW)                       │
│         │                                                        │
│         └── owns / shows / hides:                                │
│             MahoSidebarFolderHoverPopupView (NEW BubbleDialog)   │
│                                                                  │
│ SidebarFolderRowView                                             │
│   ├── child_tabs_snapshot_  (NEW: std::vector<SidebarTreeNode>) │
│   ├── show_popup_callback_  (NEW: RepeatingClosure)             │
│   ├── hide_popup_callback_  (NEW: RepeatingClosure)             │
│   └── OnMouseEntered/Exited                                      │
│         └── if (!is_expanded_) → invoke callbacks                │
└──────────────────────────────────────────────────────────────────┘
```

Data path stays unchanged: `Rust SpaceManager → maho_core_get_sidebar_tree → JSON → SidebarTreeNode → constructor of SidebarFolderRowView`. We just snapshot the children we already receive.

---

## 4. File Changes

### 4.1 New files

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.h`
```cpp
class MahoSidebarFolderHoverPopupView : public views::BubbleDialogDelegateView,
                                        public views::TextfieldController {
 public:
  enum class Mode { kFullList, kSearchOnly };

  using ActivateTabCallback = base::RepeatingCallback<void(const std::string& tab_id)>;

  // Returns the widget; caller must observe lifetime via WidgetObserver.
  static views::Widget* Show(views::View* anchor_view,
                             const std::u16string& folder_name,
                             std::string folder_id,
                             std::vector<SidebarTreeNode> children,
                             Mode mode,
                             ActivateTabCallback on_activate);

  // BubbleDialogDelegateView
  void Init() override;
  gfx::Rect GetAnchorRect() const override;   // returns anchor_view()->GetBoundsInScreen()
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  // TextfieldController
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // Identity check used by the controller to no-op re-hover on the same folder.
  const std::string& current_root_folder_id() const { return root_folder_id_; }

 private:
  struct Level {
    std::u16string name;
    std::vector<SidebarTreeNode> children;
  };

  MahoSidebarFolderHoverPopupView(views::View* anchor,
                                  std::u16string folder_name,
                                  std::string folder_id,
                                  std::vector<SidebarTreeNode> children,
                                  Mode mode,
                                  ActivateTabCallback on_activate);

  void RebuildResultRows();           // re-renders rows_container_ from filtered_indices_
  void ApplyFilter(const std::u16string& query);
  void MoveSelection(int delta);
  void ActivateSelected();            // tab → activate+dismiss; folder → PushLevel
  void PushLevel(const SidebarTreeNode& folder_node);
  void PopLevel();                    // dismisses if at root
  void RebuildBreadcrumb();
  bool ShouldRenderList() const;      // false in kSearchOnly when query is empty

  Mode mode_;
  std::string root_folder_id_;
  std::vector<Level> nav_stack_;       // [0] = root folder; deeper levels pushed on sub-folder click
  std::vector<size_t> filtered_indices_;
  int selected_index_ = 0;

  raw_ptr<views::View> breadcrumb_row_ = nullptr;   // hidden at root
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;

  ActivateTabCallback activate_callback_;
};
```

Implementation details:
- Layout: `BoxLayout(kVertical)`. Optional breadcrumb row (28px, hidden at root) + search row (40px) + separator + scroll view (max-height 312px in `kFullList`; auto-size in `kSearchOnly`).
- Search row contains: `kSearchIcon` (16px) + `views::Textfield` (`SetPlaceholderText(u"Search " + nav_stack_.back().name + u"…")` — placeholder updates when navigating into a sub-folder).
- Each tab row: favicon (16px) + title label, `kRowHeight=40`, hover/selected highlight via `ui::kColorSysStateHover` / `ui::kColorSysStateActive`.
- Each sub-folder row: `kFolderIcon` (16px) + name label + trailing `kChevronRightIcon` (12px), same height + highlight states.
- Filtering: case-insensitive substring on tab `title`/`url` and folder `name` using `base::i18n::ToLower`. Walk current level once per keystroke (no debounce).
- Scroll view: `SetHorizontalScrollBarMode(kDisabled)`, vertical only.
- Empty state: when `nav_stack_.back().children.empty()` (or filtered to empty), show `Label(u"No tabs in this folder")` / `Label(u"No matching results")` instead of rows. In `kSearchOnly` mode, render nothing when query is empty.
- Initial focus: `search_field_->RequestFocus()`.
- `set_close_on_deactivate(false)` so typing in the search field does not dismiss the popup; controller handles dismiss explicitly.
- `SetCanActivate(true)` (must accept focus for typing) — different from `MahoTabPreviewController`.
- `GetAnchorRect()` override returns `anchor_view()->GetBoundsInScreen()` directly so the bubble has zero anchor padding and visually aligns top-to-top with the folder row.
- `set_anchor_view_insets(gfx::Insets())`, `SetArrow(views::BubbleBorder::NONE)`, and zero shadow inset on the anchor side (`set_shadow(views::BubbleBorder::STANDARD_SHADOW)` then trim via `set_anchor_view_insets`) so the popup left edge sits ~4px right of the row's right edge.
- Backspace in empty `search_field_` while `nav_stack_.size() > 1` → call `PopLevel()`.

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.h`
```cpp
class MahoSidebarFolderHoverController : public views::WidgetObserver {
 public:
  explicit MahoSidebarFolderHoverController(Browser* browser);
  ~MahoSidebarFolderHoverController() override;

  // No-op if popup is already open for the same folder_id.
  // Closes existing popup (if any) when folder_id differs, then schedules show.
  void ScheduleShow(views::View* anchor,
                    std::string folder_id,
                    std::u16string folder_name,
                    std::vector<SidebarTreeNode> children,
                    MahoSidebarFolderHoverPopupView::Mode mode);

  // Called on row mouse-exit. Starts a hide grace timer; cancelled if mouse
  // re-enters anchor row OR moves into popup widget bounds.
  void OnRowMouseExited();

  void HideImmediately();

  // views::WidgetObserver
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void ShowNow();
  void OnHideGraceTimerFired();
  void OnActivateTab(const std::string& tab_id);
  bool IsCursorInsidePopup() const;

  raw_ptr<Browser> browser_;
  base::OneShotTimer show_timer_;
  base::OneShotTimer hide_grace_timer_;
  views::ViewTracker anchor_tracker_;

  // Pending show params (set by ScheduleShow, consumed by ShowNow).
  std::string pending_folder_id_;
  std::u16string pending_folder_name_;
  std::vector<SidebarTreeNode> pending_children_;
  MahoSidebarFolderHoverPopupView::Mode pending_mode_ =
      MahoSidebarFolderHoverPopupView::Mode::kFullList;

  // Open popup state.
  raw_ptr<views::Widget> popup_widget_ = nullptr;
  std::string open_folder_id_;          // folder id of the currently open popup

  base::WeakPtrFactory<MahoSidebarFolderHoverController> weak_factory_{this};
};
```

Implementation details:
- `ScheduleShow`: if `popup_widget_ && open_folder_id_ == folder_id`, **no-op** (re-hover same folder = keep popup as-is). Otherwise, if `popup_widget_` exists, `HideImmediately()` first; then store params and `show_timer_.Start(FROM_HERE, base::Milliseconds(225), &ShowNow)`.
- `OnRowMouseExited`: starts `hide_grace_timer_` (150ms). On fire, check `IsCursorInsidePopup()` — if true, do nothing; else `HideImmediately()`. Cancel grace timer if `ScheduleShow` is called again on the same folder before fire.
- `ShowNow`: bails if `anchor_tracker_.view() == nullptr`; calls `MahoSidebarFolderHoverPopupView::Show(anchor, name, id, children, mode, on_activate_cb)`; observes the resulting widget; sets `popup_widget_` and `open_folder_id_`.
- `OnActivateTab`: looks up tab by id via `browser_->tab_strip_model()`, activates it, then `HideImmediately()`.
- `OnWidgetDestroying`: clears `popup_widget_`, `open_folder_id_`, removes observer.
- `IsCursorInsidePopup`: query `display::Screen::GetCursorScreenPoint()` and check against `popup_widget_->GetWindowBoundsInScreen()`.

### 4.2 Modified files

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`
- `SidebarFolderRowView` (around line 337):
  - Add member: `std::vector<SidebarTreeNode> children_snapshot_;` (stores **all** children — both `Tab` and `Folder` kinds, in stored order, so the popup can render sub-folders too).
  - Add member: `std::string folder_id_snapshot_;` (already present as `folder_id_`; just expose if needed).
  - Add members: `base::RepeatingClosure show_popup_callback_;` `base::RepeatingClosure hide_popup_callback_;`
  - Add setter: `void SetHoverPopupCallbacks(base::RepeatingClosure show, base::RepeatingClosure hide);`
- `MahoSidebarTabListView`:
  - Add member: `std::unique_ptr<MahoSidebarFolderHoverController> folder_hover_controller_;`

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `SidebarFolderRowView::OnMouseEntered` (line 1889) — extend:
  ```cpp
  void SidebarFolderRowView::OnMouseEntered(const ui::MouseEvent& event) {
    views::View::OnMouseEntered(event);
    UpdateAppearance();
    if (show_popup_callback_) {
      show_popup_callback_.Run();   // mode is decided by the controller using is_expanded_
    }
  }
  ```
- `SidebarFolderRowView::OnMouseExited` (line 1894) — extend:
  ```cpp
  void SidebarFolderRowView::OnMouseExited(const ui::MouseEvent& event) {
    views::View::OnMouseExited(event);
    UpdateAppearance();
    if (hide_popup_callback_) {
      hide_popup_callback_.Run();   // -> controller::OnRowMouseExited (starts grace timer)
    }
  }
  ```
- `MahoSidebarTabListView::Update()` / `RebuildTreeNodes()`:
  - When constructing each `SidebarFolderRowView` for a `SidebarTreeNode` whose `kind == kFolder`, store the FULL `node.children` (Tab + Folder kinds) into `children_snapshot_`. Do not pre-filter to tabs — the popup needs sub-folders too.
  - Wire callbacks (mode chosen here based on `is_expanded_`):
    ```cpp
    auto mode = node.is_expanded
        ? MahoSidebarFolderHoverPopupView::Mode::kSearchOnly
        : MahoSidebarFolderHoverPopupView::Mode::kFullList;

    row->SetHoverPopupCallbacks(
        base::BindRepeating(&MahoSidebarFolderHoverController::ScheduleShow,
                            folder_hover_controller_->AsWeakPtr(),
                            row, node.folder_id, node.folder_name,
                            row->children_snapshot_, mode),
        base::BindRepeating(&MahoSidebarFolderHoverController::OnRowMouseExited,
                            folder_hover_controller_->AsWeakPtr()));
    ```
- Constructor: instantiate `folder_hover_controller_ = std::make_unique<MahoSidebarFolderHoverController>(browser_)`.

### 4.3 Build files

#### `maho-chromium/browser/ui/views/sidebar/BUILD.gn`
Add to `sources`:
- `maho_sidebar_folder_hover_popup_view.h`
- `maho_sidebar_folder_hover_popup_view.cc`
- `maho_sidebar_folder_hover_controller.h`
- `maho_sidebar_folder_hover_controller.cc`

Verify `deps` already include `views`, `views/bubble`, `gfx/vector_icon_types`, and the maho_lucide_icons GN target. Add if missing.

> Per AGENTS.md: do NOT modify any other `BUILD.gn` (especially in `chromium/src/**`) without explicit approval.

---

## 5. Behavior Specification

### 5.1 Show conditions
- Mouse enters `SidebarFolderRowView` (any folder, regardless of `is_expanded_`).
- Timer fires after 225ms of continuous hover.
- If anchor view is destroyed before fire, abort silently.
- The popup's **mode** is decided at show time:
  - `is_expanded_ == false` → `kFullList` mode (search + full child list)
  - `is_expanded_ == true` → `kSearchOnly` mode (search input only; results appear below the input only when query is non-empty)
- If the same folder's popup is **already open**, re-hover is a **no-op** (do not refresh, do not steal focus, do not reset query).
- If a **different** folder's popup is open and user hovers a new folder, immediately close the existing popup and schedule the new one.

### 5.2 Hide conditions (any one triggers)
- Mouse leaves the folder row AND mouse is not over the popup widget bounds → hide immediately.
- Esc pressed while popup has focus → hide immediately.
- A tab row is clicked → activate tab + hide.
- Browser tab strip changes (tab activated externally) → hide.
- Folder row's anchor view is destroyed → hide.

### 5.3 Mouse-leave grace period
The popup is anchored to the **right** of the row. Moving the cursor from row → popup crosses dead space. Use the same approach as `MahoTabPreviewController`: do not hide on row exit alone — require both (a) row exit AND (b) cursor outside popup widget bounds for >150ms. Implement via `popup_widget_->GetWindowBoundsInScreen().Contains(cursor_pos)` check inside a short `OneShotTimer`.

### 5.4 Search behavior
- Synchronous filter on every `ContentsChanged` (no debounce; folders are small).
- Match on lowercased `title` (use `custom_title` if non-empty, else `title`) AND `url`.
- Sub-folder rows: match on lowercased folder `name`.
- Filter operates only on the **current navigation level**'s children (does not recurse into sub-folders).
- Selected index resets to `0` on every filter change.
- ↑/↓ moves selected index within filtered results.
- Enter activates `filtered_indices_[selected_index_]`:
  - Tab row → activate tab + dismiss
  - Sub-folder row → push navigation level (do NOT dismiss)
- Backspace in **empty** search field while inside a sub-folder → pop one navigation level (clears query, replaces children with parent's level).
- `kSearchOnly` mode (expanded folder): when query is empty, no list is shown; when query is non-empty, the same filter applies and results render below the input.

### 5.5 Visual spec
- Width: `260px` (matches `MahoSidebarActionPopoverView::kRowWidth + 4`).
- Max height: `360px` in `kFullList` mode (search row 40 + 8 separator + scroll up to 312); `kSearchOnly` mode auto-sizes (40 when empty query, expands as results appear, capped at 360).
- **Anchor: precisely to the right of the folder row, top-aligned.** Use `BubbleBorder::RIGHT_TOP` with **arrow disabled** (`SetArrow(BubbleBorder::NONE)` is not enough — also call `set_anchor_view_insets(gfx::Insets())` and override `GetAnchorRect()` to return the folder row's bounds in screen coordinates with no padding). The popup's top edge must align with the folder row's top edge; the popup's left edge must abut (or be exactly N px from) the folder row's right edge.
  - Concretely: override `GetAnchorRect()` to return `anchor_view()->GetBoundsInScreen()` directly. Set bubble margin / shadow inset to zero on the anchor side so visual leading edge is `row.right + kPopupGap` where `kPopupGap` = small fixed offset (e.g., 4px) accounting for shadow.
  - In RTL (`base::i18n::IsRTL()`), use `BubbleBorder::LEFT_TOP` with mirrored anchoring (popup right edge abuts row left edge).
- Border / fill: `ui::kColorSysSurface2` background, `ui::kColorSysNeutralOutline` 1px border, 16px corner radius.
- No bubble arrow / pointer (set `BubbleBorder::NONE`).
- Search field: 32px height, leading `kSearchIcon` (16px), `kColorSysOnSurfaceSubtle` placeholder.
- Tab row: 40px, leading 16px favicon (use existing favicon utility — check `SidebarTabRowView` for the helper), single-line ellipsized title.
- Sub-folder row: 40px, leading `kFolderIcon` (16px) — add to `maho_lucide_icons/` if not present (`folder.icon`), single-line ellipsized name, trailing `>` chevron icon (`kChevronRightIcon`) to indicate navigability.
- Hover row: `kColorSysStateHover`. Selected (keyboard) row: `kColorSysStateActive`.
- Breadcrumb header (visible only when navigated into a sub-folder): 28px row above the search field showing `← Back · <parent name> / <current name>`. Click `← Back` or press Backspace in empty search field to pop one level.

### 5.6 Edge cases
| Case | Behavior |
|---|---|
| Folder with 0 tabs and 0 sub-folders | `kFullList` mode shows empty state label `No tabs in this folder` |
| Folder with sub-folders | Render sub-folders as folder rows with `kFolderIcon` + chevron. Click pushes a navigation level (breadcrumb appears, popup contents replaced with sub-folder's children). Backspace in empty search or click `← Back` pops one level. Search filter operates only on the **current** level (not recursive). |
| Live folder (`provider_type` set) | Same flow — children come from same path; sub-folders inside live folders work the same way |
| Drag in progress | Skip showing (mirror `MahoTabPreviewController`'s drag check if it has one) |
| Sidebar collapsed (icon-only mode) | Anchor still uses row's `GetBoundsInScreen()`; popup positions next to icon. Verify width does not overflow screen. |
| RTL | `BubbleBorder::LEFT_TOP` + mirrored anchoring (popup right edge abuts row left edge) when `base::i18n::IsRTL()` |
| Multiple folders rapid-hover | New hover on different folder cancels previous timer + closes existing popup before scheduling new one. Hover on **same** folder while popup open is a no-op (per §5.1). |
| Popup vertical overflow | If `row.top + popup_height > screen.bottom`, shift popup up so its bottom edge aligns with `screen.bottom - margin`. Top edge no longer aligns with row top in this case — acceptable trade-off. |
| `kSearchOnly` mode + Enter on empty results | No-op |
| Navigation deep inside sub-folders + parent collapses | Popup keeps current navigation state until dismissed; closing/reopening resets to root |

---

## 6. Verification Plan

### 6.1 Static checks (after each edit)
- `lsp_diagnostics` on every modified C++ file → zero errors before moving to build.
- Verify includes: `views/bubble/bubble_dialog_delegate_view.h`, `views/controls/textfield/textfield.h`, `views/controls/scroll_view.h`, `views/view_tracker.h`, `base/timer/timer.h`, `ui/base/metadata/metadata_header_macros.h`, `ui/color/color_id.h`, the maho_lucide_icons header.

### 6.2 Build
- Run `python3 maho-chromium/build/scripts/build_maho.py` from repo root.
- If GN regen fails, **stop and report** — do NOT auto-edit GN files (per AGENTS.md).

### 6.3 Manual QA (must perform after build succeeds)
1. Open browser with at least one closed folder containing 5+ tabs (e.g., create folder `강의` with multiple tabs, collapse it).
2. Hover the closed folder row — confirm popup appears after ~225ms with placeholder `Search 강의…` and full tab list.
3. Confirm popup top edge aligns precisely with folder row top edge; popup left edge sits ~4px right of row right edge.
4. Hover an **expanded** folder — confirm popup appears in `kSearchOnly` mode (search input only, no list).
5. In `kSearchOnly` mode, type a query — confirm matching tabs appear below input; clear query → list disappears.
6. Type Korean text into search on closed folder → confirm tabs filter live.
7. Click a tab row → confirm correct tab activates and popup dismisses.
8. ↑/↓ + Enter keyboard nav → confirm selection moves and Enter activates.
9. Esc → confirm dismiss.
10. Hover folder, move cursor through dead-space to popup → confirm popup does NOT prematurely hide (150ms grace).
11. Hover folder A → popup A opens. Hover folder B → popup A closes, popup B opens.
12. Hover folder A → popup A opens. Re-hover folder A → confirm **no-op** (no flicker, no query reset, focus state preserved).
13. Switch tabs externally → confirm popup hides.
14. RTL: switch UI to RTL locale → confirm popup opens to the left of the row, top-aligned.
15. Empty folder → confirm `No tabs in this folder` shows in `kFullList`; nothing in `kSearchOnly` (empty query).
16. **Sub-folder navigation**: create a folder with sub-folders. Hover parent (closed). Confirm sub-folder rows render with folder icon + chevron. Click a sub-folder → confirm breadcrumb appears, contents replaced with sub-folder's children, search placeholder updates to sub-folder name.
17. Backspace in empty search field while inside a sub-folder → confirm pops one level back.
18. Click `← Back` in breadcrumb → confirm pops one level back.
19. Search inside a sub-folder → confirm filter operates only on that level (does not search parent's tabs).
20. Folder near bottom of screen — confirm popup repositions upward when it would otherwise overflow screen bottom.
21. Rapid hover across multiple folders → confirm only one popup visible at a time and no flicker.

### 6.4 Regression
- `cd maho && bun run test` (Rust workspace — should be a no-op since no Rust changed; sanity check)
- `cd maho && bun run typecheck`
- Existing `maho_sidebar_tab_preview_interactive_test.cc` should still pass — confirm tab preview did not regress.

---

## 7. Out of Scope (v1)

- Showing live-folder external items (PRs, calendar events) — they already flow through the same path; v1 treats them like normal tabs.
- Drag-from-popup to reorder. v1: click-only.
- Pinning the popup open (to use it as a temporary side panel).
- Recent / frequently-used sorting. v1: maintain stored order from `Folder::tab_ids` / `Folder` children order.
- Touch/Pen input opening trigger. v1: mouse hover only (matches `MahoTabPreviewController`).
- **Recursive search** — search filter operates only on the current navigation level. Searching across nested sub-folders is v2.
- **Sub-folder preview** — hovering a sub-folder row inside the popup does NOT open another nested popup. Only click navigates.

---

## 8. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Mouse-leave dead-space causes flicker | Implement 150ms hide grace + cursor-in-popup check (§5.3). |
| Popup steals focus while user is typing in omnibox | `set_close_on_deactivate(false)` + only `RequestFocus()` on the search field on first `Show`, not on re-show during same hover session. |
| Anchor view destroyed mid-show | `views::ViewTracker` checks before show; `WidgetObserver` cleans up after. |
| Folder with 1000s of tabs | Filter is O(n) per keystroke. Acceptable up to ~5k. If becomes an issue, add 80ms debounce. v1: no debounce. |
| GN regen failure | Per AGENTS.md, stop and report — never auto-edit GN. |
| Sidebar repositioned (left ↔ right) | Read sidebar-position pref at popup show time; choose `RIGHT_TOP` vs `LEFT_TOP` accordingly. |
| `MahoFolderContextMenu` (right-click) conflicts | They use different events (right-click vs hover). Verify both can coexist; popup hides on right-click via deactivation observer if needed. |

---

## 9. Delegation

**Recommended path:** delegate the entire implementation to the `visual-engineering` category with `frontend-ui-ux` skill loaded, since this is sidebar UI work spanning multiple Native Views files and visual polish is the dominant concern.

Single delegation prompt scope:
- Create the two new file pairs (popup view + controller).
- Modify `maho_sidebar_tab_list_view.{h,cc}` to wire callbacks and store snapshots.
- Update `BUILD.gn`.
- Run `lsp_diagnostics`, then `build_maho.py`, then perform manual QA.

After delegation: read every modified file, re-run diagnostics, and verify the manual QA checklist passes before declaring done.

---

## 10. Confirmed Decisions (2026-05-31)

All §10 questions resolved with the user. Decisions baked into §1, §5, §7 above.

| # | Question | Decision |
|---|---|---|
| 1 | Show popup on expanded folders too? | **Yes — but in `kSearchOnly` mode** (search input only; results appear only when query is non-empty) |
| 2 | Anchor side / position | **`BubbleBorder::RIGHT_TOP` with precise alignment** — popup top edge aligned to folder row top edge, popup left edge abuts row right edge (zero offset modulo small shadow gap). Override `GetAnchorRect()` to return `anchor_view()->GetBoundsInScreen()`. RTL flips to `LEFT_TOP` mirrored. |
| 3 | Hover delay | **225ms** (matches `MahoTabPreviewController::kTabPreviewShowDelay`) |
| 4 | Sub-folder handling | **Render as folder rows in the list with `kFolderIcon` + chevron**; click pushes navigation level (breadcrumb header appears, contents replaced). Backspace in empty search or `← Back` click pops. Search filter operates only on the current level (not recursive). |
| 5 | Re-hover same folder | **No-op** — keep existing popup as-is, do not refresh, do not reset query, do not steal focus |
