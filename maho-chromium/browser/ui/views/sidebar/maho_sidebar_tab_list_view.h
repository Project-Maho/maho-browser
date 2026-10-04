// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_

#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/tabs/public/tab_interface.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "ui/views/view_tracker.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

namespace favicon_base {
struct FaviconImageResult;
}  // namespace favicon_base

class Browser;
#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"

namespace ui {
class Event;
}

namespace views {
class ImageButton;
class ImageView;
class LabelButton;
class MenuRunner;
class ScrollView;
class Textfield;
class View;
class Widget;
}  // namespace views

namespace content {
class WebContents;
}

namespace gfx {
class Canvas;
}

namespace maho {

class SidebarTabRowView;
class SidebarFolderRowView;
class SidebarVisibilityManager;
class MahoTabPreviewController;
class MahoSidebarFolderHoverController;
class MahoSidebarView;
class MahoSidebarTabListView;
struct SidebarDropPlan;

const ui::ClipboardFormatType& GetMahoDragFormatType();

std::string SerializeDragPayload(const SidebarDragPayload& payload);
bool DeserializeDragPayload(const std::string& data, SidebarDragPayload& out);

void WriteMahoDragData(const SidebarDragPayload& payload,
                       ui::OSExchangeData* data);
bool ReadMahoDragData(const ui::OSExchangeData& data,
                      SidebarDragPayload& out);

std::unique_ptr<views::View> CreateSidebarSpaceHeaderRow();
void SetSidebarSpaceHeaderRowTabListView(
    views::View* row,
    base::WeakPtr<MahoSidebarTabListView> tab_list_view);
void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name);
void ConfigureSidebarSpaceHeaderRowPrivate(views::View* row,
                                            const ui::ImageModel& icon,
                                            const std::u16string& name,
                                            ui::ColorId text_color);
void SetSidebarSpaceHeaderRowPalette(views::View* row,
                                      const MahoSidebarPalette& palette);
MahoSidebarPalette GetSidebarSpaceHeaderRowPaletteForTesting(
    views::View* row);

class RebuildRowsBatchScoper {
 public:
  explicit RebuildRowsBatchScoper(MahoSidebarTabListView* view);
  ~RebuildRowsBatchScoper();
 private:
  raw_ptr<MahoSidebarTabListView> view_;
};

// ---------------------------------------------------------------------------
// Section-local row virtualization projection.
//
// A SidebarVisualRow is a flat, deterministic description of one visual row in
// a section's render order, mirroring the exact DFS order produced by
// RebuildTreeNodes(). ReconcileSectionWindow() realizes only the rows inside
// the current visible window (viewport + overscan) from this projection, so the
// realized view count is bounded by the viewport rather than the tab count.
// See maho_sidebar_visibility_manager.h for the matching variable-height
// visible-range computation.
// ---------------------------------------------------------------------------
enum class SidebarVisualRowKind {
  kInsertionLane,
  kTab,
  kFolder,
  kCollapsedStickyTab,
  kSplitGroup,
};

struct SidebarVisualRow {
  SidebarVisualRow();
  SidebarVisualRow(const SidebarVisualRow&);
  SidebarVisualRow(SidebarVisualRow&&);
  SidebarVisualRow& operator=(const SidebarVisualRow&);
  SidebarVisualRow& operator=(SidebarVisualRow&&);
  ~SidebarVisualRow();

  SidebarVisualRowKind kind = SidebarVisualRowKind::kTab;
  // tab_id | folder_id | split_id | lane_key | sticky_key (see .cc for scheme).
  std::string stable_id;
  MahoSidebarTabSection section = MahoSidebarTabSection::kNormal;
  int indent_depth = 0;
  int height_dp = 0;          // deterministic (see MeasureVisualRowHeight).
  int cumulative_top_dp = 0;  // prefix sum within the section.
  // Shallow projection: folders/tabs are childless; splits keep children.
  SidebarTreeNode node;
  std::string parent_folder_id;
  std::string folder_next_sibling_id;                        // folder-only
  bool folder_has_children = false;
  bool split_force_stacked = false;
  SidebarNodeKind lane_target_kind = SidebarNodeKind::kTab;  // lane-only
  std::string lane_target_id;                                // lane-only
  bool lane_append = false;                                  // lane-only
  std::string lane_parent_folder_id;                         // lane-only
  int lane_target_child_index = -1;                          // lane-only
};

// Deterministic per-row height in dp (see .cc for the exact token mapping).
int MeasureVisualRowHeight(const SidebarVisualRow& row);

// Flattens |nodes| into |out| in the exact DFS visual order of
// RebuildTreeNodes(), filling height_dp and the running cumulative_top_dp
// prefix sum within the section.
void BuildSectionProjection(const std::vector<SidebarTreeNode>& nodes,
                            MahoSidebarTabSection section,
                            std::vector<SidebarVisualRow>* out,
                            bool force_split_stacked);

class MahoSidebarTabListView : public views::View,
                               public views::ContextMenuController,
                               public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MahoSidebarTabListView, views::View)

 public:
  explicit MahoSidebarTabListView(Browser* browser);
  MahoSidebarTabListView(const MahoSidebarTabListView&) = delete;
  MahoSidebarTabListView& operator=(const MahoSidebarTabListView&) = delete;
  ~MahoSidebarTabListView() override;

  base::WeakPtr<MahoSidebarTabListView> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void Update(MahoSidebarTabListModel model, Browser* browser);
  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);
  void SetControlledTabId(std::string stable_tab_id);
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }
  void SetPrivateMode(bool private_mode);
  void SetLastFavoritesModel(const MahoSidebarFavoritesModel& model);

  views::View* action_row_for_testing() { return action_row_; }
  views::View* action_buttons_for_testing() { return action_buttons_; }
  views::View* tidy_button_for_testing() { return tidy_button_; }
  views::View* clear_button_for_testing() { return clear_button_; }
  MahoSidebarProcessingPlaceholderView* action_processing_view_for_testing() {
    return action_processing_view_;
  }
  bool action_placeholder_active_for_testing() const {
    return action_placeholder_active_;
  }

  // Thin test wrappers: anonymous-namespace gtest subclasses cannot be granted
  // access via fixture friend declarations, so these delegate to the private
  // production methods without exposing the methods themselves.
  void RebuildRowsForTesting(const MahoSidebarTabListModel& model,
                             Browser* browser) {
    RebuildRows(model, browser);
  }
  void StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode mode,
      views::View* trigger_button) {
    StartActionProcessing(mode, trigger_button);
  }
  void ResolveActionPlaceholderDeferredForTesting(bool success) {
    ResolveActionPlaceholderDeferred(success);
  }

  // Sets a callback that section drop targets will invoke when a favorites-
  // origin tab is successfully dropped on a section container.  The callback
  // receives the tab_id of the dropped tab and should call
  // MahoSidebarFavoritesGridView::MarkDropAccepted to suppress the duplicate
  // destructive close_tab that OnTileDragDone would otherwise fire when a
  // drag terminates outside any accepting target (see ADR 11 — role transitions
  // vs. destructive close semantics).
  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> callback);

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;

  void SetAutoScrollScrollView(views::ScrollView* scroll_view);

  void OnAutoScrollDragOver(const gfx::Point& screen_point);
  void OnAutoScrollDragEnded();

  // Called by MahoSidebarView when the scroll position changes.
  // Invalidates tab_rows_ layout to recalculate visible set.
  void OnScrollChanged();
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void ActivateTabById(const std::string& tab_id);
  // Arc-style tab navigation in the sidebar's visual order (the DFS render
  // order from GetTabRowsInVisualOrder(): pinned/normal sections, folder
  // nesting, splits). Implicitly Space-scoped since the tab list model is
  // Space-filtered. Wraps at the ends. Returns false when no navigable tab
  // rows exist, letting the caller fall back to positional next/prev.
  // |forward| = next/down, !forward = previous/up.
  bool ActivateAdjacentTabInVisualOrder(bool forward);

  // Ensures the row for |tab_id| is realized and scrolled into view even when
  // it is currently outside the section window (off-window). Used by activation,
  // focus, and keyboard/range navigation to reach culled rows.
  void RevealTabById(const std::string& tab_id);

  // Spring-load drag support. On macOS a drag runs a nested run loop on the
  // drag-source row's own stack frame, so freeing that row mid-drag (as a Space
  // switch's RebuildRows would) is a use-after-free. SetDragSourceTabId records
  // the row that started the drag; while set, RebuildRows detaches and preserves
  // that row instead of destroying it. OnDragSourceFinished clears the record
  // and frees the preserved row after the drag stack has fully unwound.
  void SetDragSourceTabId(const std::string& tab_id);
  void OnDragSourceFinished();
  // Flushes any Space-switch rebuild that was deferred because a drag holds
  // capture, so a spring-loaded Space switch becomes visible during the drag.
  void FlushSpringLoadedSpaceSwitch();

  // Materializes a Maho core-only "suspended" tab into the TabStripModel via a
  // foreground navigation and returns the new WebContents (nullptr on failure).
  // Public so the sidebar drop path can wake tabs before splitting them.
  content::WebContents* WakeSuspendedTab(const std::string& tab_id);

  // Creates the sidebar's strict two-pane, side-by-side split. Stable tab IDs
  // and WebContents pointers are authoritative; strip indices are re-resolved
  // after every mutation that can shift them.
  bool CreateSidebarTwoPaneSplit(const std::string& source_tab_id,
                                 const std::string& target_tab_id,
                                 bool source_before_target);

  bool IsTabRowRealizedAt(int strip_index) const;

  views::View* FindSectionDropTargetForTesting(MahoSidebarTabSection section);
  // Requests that the folder with the given id enter inline name-editing mode
  // after the next rebuild completes (used for newly-created folders).
  void SetPendingFolderEdit(const std::string& folder_id);

  SidebarTabRowView* FindTabRowByIdForTesting(const std::string& tab_id);
  SidebarTabRowView* FindTabRowByTabIndexForTesting(int tab_index);
  SidebarTabRowView* FindActiveTabRowForTesting();
  SidebarTabRowView* FindFirstInactiveTabRowForTesting();
  SidebarFolderRowView* FindFolderRowByIdForTesting(
      const std::string& folder_id);
  views::View* FindInsertionLaneBeforeTabByIdForTesting(
      const std::string& tab_id);
  views::View* FindInsertionLaneBeforeFolderByIdForTesting(
      const std::string& folder_id);
  views::LabelButton* new_tab_button_for_testing() { return new_tab_button_; }
  views::View* tab_rows_for_testing() { return tab_rows_; }
  const std::vector<SidebarVisualRow>& pinned_projection_for_testing() const {
    return pinned_projection_;
  }
  const std::vector<SidebarVisualRow>& normal_projection_for_testing() const {
    return normal_projection_;
  }
  std::vector<std::string> tab_visual_order_ids_for_testing() const {
    return GetTabVisualOrderIds();
  }
  int velocity_leading_overscan_rows_for_testing() const {
    return velocity_leading_overscan_rows_for_testing_;
  }
  views::View* pinned_separator_for_testing() { return pinned_separator_; }
  int scroll_layout_invalidation_count_for_testing() const {
    return scroll_layout_invalidation_count_for_testing_;
  }
  void ResetScrollLayoutInvalidationStateForTesting();
  uint64_t row_favicon_generation_for_testing(
      const std::string& tab_id) const;
  void RunFaviconLoadedForTesting(
      const std::string& tab_id,
      const GURL& requested_url,
      uint64_t generation,
      const favicon_base::FaviconImageResult& result) {
    OnFaviconLoaded(tab_id, requested_url, generation, result);
  }
  void ForgetTabRowForTesting(const std::string& tab_id) {
    tab_id_to_row_view_.erase(tab_id);
  }

  // Returns true if a re-entrant ReconcileSectionWindow call is correctly
  // blocked (no-op) while an outer reconcile is notionally in progress — i.e.
  // the window/children are left untouched. Locks the fast-fling re-entrancy
  // crash regression.
  bool ReentrantReconcileIsBlockedForTesting();

  void ToggleFolderExpandedForTesting(const std::string& folder_id);
  bool IsTabPreviewShowingForTesting() const;
  views::Widget* tab_preview_widget_for_testing() const;
  const std::u16string& tab_preview_title_for_testing() const;
  const std::u16string& tab_preview_url_for_testing() const;
  void SetTabPreviewShowDelayForTesting(base::TimeDelta delay);
  bool is_pinned_drop_lane_revealed_for_testing() const {
    return pinned_drop_lane_revealed_;
  }
  MahoTabContextMenu* active_context_menu_for_testing() {
    return active_context_menu_.get();
  }

  // Returns the maho-core tab_id for the given WebContents, or empty string.
  std::string FindCoreTabIdByWebContents(content::WebContents* contents) const;

  MahoSidebarView* GetSidebarView();
  void HandlePostDropTransition(const SidebarDragPayload& payload,
                                const SidebarDropPlan& plan);

  void ShowTabRowContextMenu(views::View* source,
                             int live_index,
                             const gfx::Point& point,
                             ui::mojom::MenuSourceType source_type,
                             MahoTabContextMenu::Delegate* delegate = nullptr);

  void UpdateActiveTabHighlightOnly(const std::string& new_active_tab_id);

  // Suspends a close-protected (pinned/favorite) tab that arrives as a
  // kRemoved but stays in the model: flips its row minus->X + last_model_ node,
  // moves the highlight to |new_active_tab_id| (empty = no active change), and
  // arms the one-shot suppress fingerprint so the async core push is deduped
  // instead of forcing a full RebuildRows. Returns false (caller then
  // ScheduleRefreshAll) when any precondition fails.
  bool TrySuspendFastPath(const std::string& suspended_tab_id,
                          const std::string& new_active_tab_id);

  // Inverse of TrySuspendFastPath for waking a suspended tab. Uses
  // |waking_tab_id_| (set by WakeSuspendedTab before Navigate) to flip that
  // row is_suspended=false (X->-) + last_model_ node in place, move the
  // highlight to it, and arm the one-shot suppress fingerprint. Decoupled from
  // the new WebContents' stable-id timing (which is still the throwaway UUID at
  // the synchronous kInserted moment). Returns false when it isn't a tracked
  // wake of a currently-suspended row, so the caller falls through.
  bool TryWakeFastPath();

  // Corrects a row rendered X purely because its live WebContents had not yet
  // attached at the last rebuild (CreateTabRowView derives is_suspended from
  // live_index). The strip insertion proves the tab is live, so re-sync the
  // row to core truth: flip X->minus iff last_model_ says it is not suspended.
  // No last_model_/fingerprint mutation — the core node is already not
  // suspended, so the follow-up push fp-dedupes with no rebuild. Returns true
  // when a row was flipped.
  bool ReconcileInsertedRowLiveness(content::WebContents* inserted_contents);

  void SetFavoritesSyncCallback(base::RepeatingClosure callback);

  // Called by MahoSidebarView when the overlay has fully faded/hidden. Clears
  // the in-progress guard and re-enables the action buttons.
  void OnActionPlaceholderHidden();

  bool HasTabInLastModel(const std::string& tab_id) const;

  bool CoreSuspendedForTab(const std::string& tab_id) const;

  const std::vector<SidebarTreeNode>* FindFolderChildrenForTesting(
      const std::string& folder_id) const {
    return FindFolderChildren(folder_id);
  }

  int rebuild_rows_count_for_testing() const {
    return rebuild_rows_count_for_testing_;
  }
  int accessibility_rebuild_count_for_testing() const {
    return accessibility_rebuild_count_for_testing_;
  }
  void set_accessibility_tree_enabled_for_testing(bool enabled);

  void ApplyCosmeticRefresh();

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // Multi-selection public APIs
  void ToggleTabSelected(const std::string& tab_id);
  void SelectRangeTo(const std::string& tab_id);
  void ClearSelection();
  bool IsTabSelected(const std::string& tab_id) const;
  const std::unordered_set<std::string>& selected_tab_ids() const {
    return selected_tab_ids_;
  }
  bool HasMultiSelection() const { return selected_tab_ids_.size() > 1; }

  void CloseSelected();
  void PinSelected(bool pin);
  void MoveSelectedToSpace(const std::string& space_id);
  void MuteSelected(bool mute);
  void ArchiveSelected();
  void OpenSelectedInSplit();
  void NewFolderWithSelected();

  // Two-stage close-protection funnel (suspend-then-delete for pinned/favorite).
  // Public so SidebarTabRowView's context-menu Delegate can route single-tab
  // closes here.
  void CloseTabById(const std::string& tab_id);

  // Flushes a rebuild that was deferred while the widget held capture (drag in
  // progress). Public so MahoSidebarView::OnDragEnded can flush after a drag
  // that ended without a drop (e.g. ESC-cancelled), which otherwise leaves the
  // sidebar stale until the next model change.
  void FlushDeferredRebuild();

  // Clears the tab_id->strip-index cache. Public so
  // MahoSidebarView::OnTabStripModelChanged can invalidate it on index-only
  // strip mutations (kMoved/kInserted/kRemoved); the linear-scan fallback in
  // ResolveTabStripIndexForTabId keeps lookups correct after the clear.
  void InvalidateTabIndexCache();

  // Re-syncs each section's realized window against the CURRENT viewport after
  // the hosting ScrollView finished a layout pass.
  //
  // RebuildRows() reconciles while the new rows are still unlaid-out, so it
  // necessarily reads a stale scroll geometry; the following layout resizes the
  // content extent and ScrollView::Layout() then CLAMPS the scroll offset via
  // ConstrainScrollToBounds(). That clamp moves the viewport without ever
  // calling OnScrolled(), so no contents-scrolled notification fires and
  // OnScrollChanged() never runs — the realized rows stay parked outside the
  // viewport while the skeleton spacers fill it, which reads to the user as
  // "the sidebar tab list vanished" until the next scroll or rebuild.
  //
  // Reconciling here (post-layout, post-clamp) is the only point where the
  // final viewport is known. Converges: a window that already matches leaves
  // both spacer sizes untouched, and View::SetPreferredSize() is a no-op for an
  // unchanged size, so no further layout is scheduled.
  void SyncRealizedWindowAfterLayout();

  // Called from the ScrollView post-layout callback. That callback runs before
  // this view's own subtree is laid out, so row/spacer geometry is still stale
  // there; the sync is posted to run once the layout pass has settled.
  // Coalesces repeated layout passes into one sync.
  void SchedulePostLayoutSync();

 private:
  friend class RebuildRowsBatchScoper;
  friend class SidebarInsertionLaneView;
  friend class SidebarSectionDropTarget;

  void RebuildRows(const MahoSidebarTabListModel& model, Browser* browser);
  bool IsControlledTabRow(const SidebarTabRowView& row) const;
  void ApplyControlledTabState();
  static std::string ComputeModelFingerprint(
      const MahoSidebarTabListModel& model);
  void RevealEmptyPinnedDropLane();
  void HideEmptyPinnedDropLane();
  void UpdatePinnedDropLaneVisibility(bool visible);
  void RebuildTreeSection(views::View* parent,
                          const std::vector<SidebarTreeNode>& nodes,
                          MahoSidebarTabSection section,
                          Browser* browser);
  // Realizes only the rows of |section|'s projection that fall inside the
  // current visible window (viewport + overscan), diffing against the section's
  // RealizedWindow: staying rows are kept untouched, exiting rows destroyed,
  // entering rows built in order between the section's top/bottom spacers. The
  // spacers absorb the culled extent so the section's preferred height always
  // equals the full projected extent (extent invariant).
  void ReconcileSectionWindow(MahoSidebarTabSection section, Browser* browser);
  // Builds a single realized view for one projection row, dispatching on kind
  // to the existing row factories.
  std::unique_ptr<views::View> RealizeProjectionRow(const SidebarVisualRow& row,
                                                     Browser* browser);
  // Rebuilds the AXVirtualView subtree that exposes the FULL per-section tab/
  // folder projection to assistive tech, independent of which rows are
  // realized. Realized row Views are AX-ignored so they don't duplicate these.
  void ScheduleAccessibilityTreeRebuild();
  void RunScheduledAccessibilityTreeRebuild();
  void RebuildAccessibilityTree();
  bool ShouldBuildAccessibilityTree() const;
  void RebuildTreeNodes(views::View* parent,
                        const std::vector<SidebarTreeNode>& nodes,
                        MahoSidebarTabSection section,
                        Browser* browser,
                        const std::string& parent_folder_id);
  std::unique_ptr<SidebarTabRowView> CreateTabRowView(
      const SidebarTreeNode& node,
      MahoSidebarTabSection section,
      Browser* browser);
  // A collapsed folder keeps its active descendant visible as a single
  // "sticky" row directly under the folder row. Returns the row (registered
  // in tab_id_to_row_view_), or nullptr when the folder has no active
  // descendant.
  SidebarTabRowView* AddCollapsedActiveRow(views::View* parent,
                                           size_t insert_index,
                                           const SidebarTreeNode& folder_node,
                                           MahoSidebarTabSection section,
                                           Browser* browser);
  // Fast-path counterpart used by UpdateActiveTabHighlightOnly: creates the
  // sticky row for |tab_id| under the outermost collapsed folder on its
  // last_model_ path, if any.
  void MaterializeStickyRowForTab(const std::string& tab_id);
  void RebuildSplitGroupContainer(views::View* parent,
                                  const SidebarTreeNode& group_node,
                                  MahoSidebarTabSection section,
                                  Browser* browser,
                                  const std::string& parent_folder_id);
  // Returning counterpart of RebuildSplitGroupContainer used by the windowed
  // realizer: builds the split container (and its split-tracking side state)
  // without attaching it to a parent.
  std::unique_ptr<views::View> BuildSplitGroupContainer(
      const SidebarTreeNode& group_node,
      MahoSidebarTabSection section,
      Browser* browser,
      const std::string& parent_folder_id);
  void AnimateAwaitingSplits();
  std::u16string GetNodeDisplayText(const SidebarTreeNode& node) const;
  std::u16string GetNodeTooltipText(const SidebarTreeNode& node) const;
  void ActivateTab(int tab_index);
  void CloseTab(int tab_index);
  int ResolveTabStripIndexForTabId(const std::string& tab_id) const;
  // Rebuilds tab_id_to_index_ from the live tab strip. Shared by every path
  // that refreshes last_model_ so the cache never diverges from the strip.
  void RebuildTabIndexCache();
  // Recursively erases every descendant SidebarTabRowView from
  // tab_id_to_row_view_ (and sub-folders from folder_rows_by_id_) before their
  // views are freed. Prevents dangling raw_ptr / UAF on folder collapse.
  void UnregisterRowsRecursive(views::View* view);
  void MuteTabById(const std::string& tab_id);
  void OpenNewTab(const ui::Event& event);
  // Opens Maho AI side panel. Auto-firing Tab Tidy on open deferred (ADR-0010 §future).
  void OnTidyPressed(const ui::Event& event);
  void OnTidyFinished(int folder_count, const std::string& error_message);
  void OnClearPressed(const ui::Event& event);
  void ApplyActionRowPalette();

  // ui::SimpleMenuModel::Delegate (Clear menu only).
  void ExecuteCommand(int command_id, int event_flags) override;
  void ToggleFolderExpanded(const std::string& folder_id);
  void CommitTabTitleChange(const std::string& tab_id,
                            const std::u16string& new_title);
  void CommitFolderNameChange(const std::string& folder_id,
                              const std::u16string& new_name);
  std::u16string ResolveTabDisplayText(const SidebarTreeNode& node) const;
  void PruneLocalTitleCache(const MahoSidebarTabListModel& model);
  void HandleTabRowHoverStart(views::View* anchor_view, int tab_index);
  void HandleTabRowHoverEnd();
  void ScheduleFolderHoverPopup(views::View* anchor,
                                std::string folder_id,
                                std::u16string folder_name);
  void OnFaviconLoaded(const std::string& tab_id,
                       const GURL& requested_url,
                       uint64_t generation,
                       const favicon_base::FaviconImageResult& result);
  void RefreshRowFaviconRequestsFromModel(
      const MahoSidebarTabListModel& model);
  uint64_t UpdateRowFaviconRequest(const std::string& tab_id,
                                   const GURL& requested_url);
  bool IsCurrentRowFaviconRequest(const std::string& tab_id,
                                   const GURL& requested_url,
                                  uint64_t generation) const;

  struct RowFaviconRequestState {
    GURL requested_url;
    uint64_t generation = 0;
  };

  raw_ptr<Browser> browser_ = nullptr;
  bool private_mode_ = false;
  raw_ptr<views::LabelButton> new_tab_button_ = nullptr;
  raw_ptr<views::View> tidy_button_ = nullptr;
  raw_ptr<views::View> clear_button_ = nullptr;
  std::vector<raw_ptr<views::View>> action_dividers_;
  std::unique_ptr<ui::SimpleMenuModel> clear_menu_model_;
  std::unique_ptr<views::MenuRunner> clear_menu_runner_;
  raw_ptr<views::View> tab_rows_ = nullptr;
  raw_ptr<views::View> pinned_section_target_ = nullptr;
  raw_ptr<views::View> normal_section_target_ = nullptr;
  std::string active_space_id_;
  base::RepeatingCallback<void(const std::string&)> favorites_drop_accepted_callback_;
  std::map<std::string, std::u16string> local_custom_titles_;
  MahoSidebarTabListModel last_model_;
  MahoSidebarFavoritesModel last_favorites_model_;
  bool has_last_model_ = false;
  bool pinned_drop_lane_revealed_ = false;
  raw_ptr<views::View> pinned_separator_ = nullptr;

  std::unique_ptr<MahoTabContextMenu> active_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_menu_model_;
  std::unique_ptr<views::MenuRunner> active_menu_runner_;
  std::unique_ptr<MahoTabPreviewController> tab_preview_controller_;
  std::unique_ptr<MahoSidebarFolderHoverController> folder_hover_controller_;

  class SidebarAutoScroller;
  std::unique_ptr<SidebarAutoScroller> auto_scroller_;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  int last_visible_row_ = -1;
  int scroll_layout_invalidation_count_for_testing_ = 0;

  // Scroll-velocity tracking for directional overscan (OnScrollChanged). The
  // leading (scroll-direction) window edge grows with fling velocity, capped at
  // SidebarVisibilityManager::kMaxVelocityOverscanRows.
  int last_scroll_velocity_y_ = 0;
  base::TimeTicks last_scroll_velocity_time_;
  int velocity_leading_overscan_rows_for_testing_ = 0;

  // Re-entrancy guard for ReconcileSectionWindow. A synchronous scroll callback
  // (e.g. a spacer resize clamping the offset and re-firing the contents-
  // scrolled callback) must never re-enter reconciliation while an outer
  // reconcile is mid-mutation of window_ indices / rows_container children.
  bool reconciling_ = false;
  // Re-entrancy guard for SyncRealizedWindowAfterLayout. Realizing rows inside
  // the post-layout callback can schedule another layout pass; without this the
  // callback could nest inside itself mid-reconcile.
  bool post_layout_syncing_ = false;
  // True while a SchedulePostLayoutSync() task is queued.
  bool post_layout_sync_pending_ = false;
  // reconciling_ ends after each reconcile; this spans the full scroll
  // reconcile plus synchronous presentation callback.
  bool scroll_presenting_ = false;
  bool rebuilding_rows_ = false;

  std::unique_ptr<SidebarVisibilityManager> visibility_manager_;

  base::CancelableTaskTracker favicon_task_tracker_;
  std::map<std::string, RowFaviconRequestState> row_favicon_requests_;

  bool rebuild_deferred_ = false;
  MahoSidebarTabListModel deferred_model_;
  raw_ptr<Browser> deferred_browser_ = nullptr;

  // Fingerprint of the model we optimistically toggled to in
  // ToggleFolderExpanded. The matching async state-push is suppressed exactly
  // once (it would otherwise pay a second full RebuildRows on 870+ rows). A
  // bare bool here swallowed *any* next update, including unrelated ones, so
  // we gate suppression on the fingerprint actually matching. Empty = disarmed.
  std::string suppress_push_fingerprint_;

  std::string last_model_fingerprint_;
  std::string last_active_tab_id_;
  std::string controlled_tab_id_;
  std::string waking_tab_id_;
  base::RepeatingClosure favorites_sync_callback_;

  bool action_placeholder_active_ = false;
  void ApplyActionPlaceholderButtonState();
  void ResolveActionPlaceholderDeferred(bool success);
  void StartActionProcessing(MahoSidebarProcessingPlaceholderView::Mode mode,
                             views::View* trigger_button);
  std::unique_ptr<views::View> CreateActionRow();

  raw_ptr<views::View> action_row_ = nullptr;
  raw_ptr<views::View> action_buttons_ = nullptr;
  raw_ptr<MahoSidebarProcessingPlaceholderView> action_processing_view_ =
      nullptr;
  views::ViewTracker focus_tracker_;
  raw_ptr<views::View> trigger_button_ = nullptr;
  MahoSidebarPalette palette_;

  int rebuild_rows_count_for_testing_ = 0;
  int accessibility_rebuild_count_for_testing_ = 0;
  bool accessibility_rebuild_pending_ = false;
  std::optional<bool> accessibility_tree_enabled_for_testing_;

  std::map<std::string, raw_ptr<SidebarFolderRowView>> folder_rows_by_id_;

  std::string pending_folder_edit_id_;

  // P3: O(1) tab_id→strip index lookup eliminates O(870) linear scan per
  // hover event (~870 calls/hover cycle). Rebuilt on Update() success. Mutable
  // so the const resolver can self-heal a stale entry after a validated scan.
  mutable std::unordered_map<std::string, int> tab_id_to_index_;

  // P4 (Fix 2): O(1) tab_id->row view lookup for UpdateActiveTabHighlightOnly.
  // REALIZED-ONLY: holds an entry only for tab rows currently inside a section
  // window (populated by RealizeProjectionRow, dropped by UnregisterRowsRecursive
  // as rows leave the window). Callers that must reach every model tab
  // (selection prune, bulk actions) use the projection / HasTabInLastModel(),
  // never this map.
  std::unordered_map<std::string, raw_ptr<SidebarTabRowView>> tab_id_to_row_view_;

  // Split IDs rendered in the previous rebuild, used to detect newly-created
  // splits so their creation animates once instead of on every rebuild.
  std::set<std::string> last_rendered_split_ids_;
  std::set<std::string> current_rebuild_split_ids_;

  // Live split_id -> container view, rebuilt every RebuildRows. Lets the
  // debounced creation animation find the current container after the burst of
  // rebuilds that a single split creation triggers has settled.
  std::unordered_map<std::string, raw_ptr<views::View>> split_id_to_container_;
  // Split IDs created this session that still need their one-time reveal
  // animation. Persists across rebuilds (unlike the maps above) until the
  // debounce timer actually plays the animation, then is cleared.
  std::set<std::string> splits_awaiting_anim_;
  // Coalesces the rebuild burst: (re)started on each rebuild that still has a
  // split awaiting animation, so the reveal fires once on the settled view.
  base::OneShotTimer split_anim_debounce_timer_;

  // Selection states
  std::unordered_set<std::string> selected_tab_ids_;
  std::string selection_anchor_tab_id_;
  // Depth of nested RebuildRowsBatchScoper instances. Rebuild fires only when
  // it returns to 0, so bulk multi-select operations can nest scopers safely.
  int batch_depth_ = 0;
  bool batch_rebuild_needed_ = false;

  std::vector<SidebarTabRowView*> GetTabRowsInVisualOrder();

  // All tab ids in DFS visual order from the section projections (every tab,
  // realized or not). Used by keyboard/range/adjacent navigation so targets
  // past the realized window are reachable; realized-only callers keep using
  // GetTabRowsInVisualOrder().
  std::vector<std::string> GetTabVisualOrderIds() const;
  const std::vector<SidebarTreeNode>* FindFolderChildren(
      const std::string& folder_id) const;

  // Section-local visual projections, rebuilt in RebuildRows. Drive windowed
  // realization via ReconcileSectionWindow(); the projected height of a
  // realized variable-height row is refreshed to its measured height so the
  // extent invariant stays exact.
  std::vector<SidebarVisualRow> pinned_projection_;
  std::vector<SidebarVisualRow> normal_projection_;

  // Inclusive projection-index range currently realized for each section
  // (last < first means nothing realized).
  struct RealizedWindow {
    int first = 0;
    int last = -1;
  };
  RealizedWindow pinned_window_;
  RealizedWindow normal_window_;
  // Leading/trailing spacers that absorb the culled extent of each section's
  // rows_container so its preferred height equals the full projected extent.
  raw_ptr<views::View> pinned_top_spacer_ = nullptr;
  raw_ptr<views::View> pinned_bottom_spacer_ = nullptr;
  raw_ptr<views::View> normal_top_spacer_ = nullptr;
  raw_ptr<views::View> normal_bottom_spacer_ = nullptr;

  std::string drag_source_tab_id_;
  raw_ptr<views::View> drag_preserve_holder_ = nullptr;

  base::WeakPtrFactory<MahoSidebarTabListView> weak_factory_{this};
};

class SidebarTabRowView : public views::View,
                            public views::DragController,
                            public views::ContextMenuController,
                            public views::TextfieldController,
                            public gfx::AnimationDelegate,
                            public MahoTabContextMenu::Delegate {
  METADATA_HEADER(SidebarTabRowView, views::View)

 public:
  SidebarTabRowView(const SidebarTreeNode& node,
                    ui::ImageModel favicon,
                     Browser* browser,
                     MahoSidebarTabSection section,
                     std::string active_space_id,
                     std::u16string display_title,
                     views::Button::PressedCallback activate_callback,
                     views::Button::PressedCallback close_callback,
                     views::Button::PressedCallback mute_callback,
                     base::RepeatingCallback<void(const std::string&, const std::u16string&)>
                          rename_callback,
                     base::RepeatingCallback<int(const std::string&)>
                         resolve_tab_index,
                     base::RepeatingCallback<void(views::View*, int)>
                         preview_show_callback,
                     base::RepeatingClosure preview_hide_callback);
  SidebarTabRowView(const SidebarTabRowView&) = delete;
  SidebarTabRowView& operator=(const SidebarTabRowView&) = delete;
  ~SidebarTabRowView() override;

  void OnThemeChanged() override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnFocus() override;
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  void SetSplitCompactMode(bool compact);

  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // MahoTabContextMenu::Delegate:
  void RenameTab(int tab_index) override;
  void CloseSelectedTabs() override;
  void PinSelectedTabs(bool pin) override;
  void MoveSelectedTabsToSpace(const std::string& space_id) override;
  void MuteSelectedTabs(bool mute) override;
  void ArchiveSelectedTabs() override;
  void OpenSelectedTabsInSplit() override;
  void NewFolderWithSelectedTabs() override;
  void CloseTabById(const std::string& tab_id) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  void OnDragDone() override;
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override;

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> callback) {
    favorites_drop_accepted_callback_ = std::move(callback);
  }

  void SetRevealEmptyPinnedDropLaneCallback(
      base::RepeatingClosure callback) {
    reveal_empty_pinned_drop_lane_callback_ = std::move(callback);
  }

  void SetOwningTabListView(
      base::WeakPtr<MahoSidebarTabListView> tab_list_view) {
    owning_tab_list_view_ = std::move(tab_list_view);
  }

  views::View::DropCallback GetDropCallbackForTesting(
      const ui::DropTargetEvent& event) {
    return GetDropCallback(event);
  }

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  enum class SidebarTabDropZone {
    kNone,
    kBefore,
    kAfter,
    kSplit,
  };

  SidebarTabDropZone drop_zone_for_testing() const {
    return active_drop_zone_;
  }
  bool is_split_preview_visible_for_testing() const {
    return split_preview_target_visible_;
  }
  MahoSplitDropSide split_preview_side_for_testing() const {
    return split_preview_side_;
  }

  bool is_active_for_testing() const { return active_; }
  bool is_controlled_for_testing() const { return controlled_; }
  bool control_pulse_animating_for_testing() const {
    return control_pulse_animation_.is_animating();
  }
  const std::string& tab_id_for_testing() const { return tab_id_; }
  int tab_index_for_testing() const { return tab_index_; }
  int live_tab_index_for_testing() const;
  MahoSidebarTabSection section_for_testing() const { return section_; }
  views::ImageButton* close_button_for_testing() { return close_button_; }
  views::Textfield* title_field_for_testing() { return title_field_; }
  views::LabelButton* title_button_for_testing() { return title_button_; }

  void SetSuspended(bool suspended);
  void SetSidebarPalette(const MahoSidebarPalette& palette);
  bool is_suspended() const { return is_suspended_; }

  // Toggles active-highlight without rebuilding. Enables the fast path in
  // MahoSidebarTabListView that skips ~2s RebuildRows() on tab activation.
  void SetActive(bool active);
  void SetControlled(bool controlled);

  void SetSelected(bool selected);
  bool is_selected() const { return is_selected_; }

  void SetFavicon(ui::ImageModel image);
  ui::ImageModel favicon_for_testing() const;
  const std::string& tab_id() const { return tab_id_; }
  const std::string& containing_folder_id() const { return source_parent_folder_id_; }

  // Non-empty when this row is the sticky active-tab row a collapsed folder
  // keeps visible; holds that (visible, collapsed) folder's id, which may be
  // an ancestor above the tab's immediate parent folder.
  void SetCollapsedStickyFolderId(const std::string& folder_id) {
    collapsed_sticky_folder_id_ = folder_id;
  }
  const std::string& collapsed_sticky_folder_id() const {
    return collapsed_sticky_folder_id_;
  }
  void ApplyDisplayedTitle(const std::u16string& title);
  void UpdateAudioState(bool audible, bool muted);

 private:
  MahoSidebarTabListView* GetTabListView();
  void ActivateTabFromEvent(const ui::Event& event);
  void ActivateFromRowClick(const ui::MouseEvent& event);
  void OnAudioButtonPressed(const ui::Event& event);
  SidebarTabDropZone ResolveDropZone(const ui::DropTargetEvent& event,
                                     const SidebarDragPayload& payload) const;
  bool IsSplitDropEligible(const SidebarDragPayload& payload) const;
  void BeginTitleEditing();
  void EndTitleEditing(bool commit);
  double GetSplitPreviewProgress() const;
  MahoSplitDropSide GetSplitPreviewSide() const;
  void ApplySplitPreviewContentLayout(double progress);
  void RefreshSplitPreviewLayout();
  void ShowSplitPreview(MahoSplitDropSide side);
  void ClearSplitPreview();
  void UpdateSplitPreviewAnimation(bool visible);
  void UpdateAppearance();

  bool active_;
  bool controlled_ = false;
  const bool pinned_;
  bool audible_;
  bool muted_;
  const std::string tab_id_;
  const MahoSidebarTabSection section_;
  const std::string space_id_;
  const std::string source_parent_folder_id_;
  std::string collapsed_sticky_folder_id_;
  const int folder_child_index_;
  raw_ptr<Browser> browser_ = nullptr;
  int tab_index_ = -1;
  std::u16string committed_title_;
  bool is_editing_title_ = false;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::ImageButton> audio_button_ = nullptr;
  raw_ptr<views::LabelButton> title_button_ = nullptr;
  raw_ptr<views::Textfield> title_field_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  base::RepeatingCallback<void(const std::string&, const std::u16string&)> rename_callback_;
  base::RepeatingCallback<int(const std::string&)> resolve_tab_index_;
  base::RepeatingCallback<void(views::View*, int)> preview_show_callback_;
  base::RepeatingClosure preview_hide_callback_;
  views::Button::PressedCallback activate_callback_;
  views::Button::PressedCallback mute_callback_;
  base::RepeatingCallback<void(const std::string&)>
      favorites_drop_accepted_callback_;
  base::RepeatingClosure reveal_empty_pinned_drop_lane_callback_;
  base::WeakPtr<MahoSidebarTabListView> owning_tab_list_view_;
  bool drag_over_drop_target_ = false;
  SidebarTabDropZone active_drop_zone_ = SidebarTabDropZone::kNone;
  gfx::SlideAnimation split_preview_animation_{this};
  gfx::SlideAnimation control_pulse_animation_{this};
  bool split_preview_target_visible_ = false;
  bool split_preview_paint_installed_ = false;
  MahoSplitDropSide split_preview_side_ = MahoSplitDropSide::kRight;
  MahoSplitDropSide split_preview_visual_side_ = MahoSplitDropSide::kRight;
  bool activate_pending_ = false;
  bool activation_handled_for_current_press_ = false;
  bool is_suspended_ = false;
  bool is_selected_ = false;

  // P3: Skip redundant SetBackground/SetBorder when visual state is unchanged.
  // UpdateAppearance() called ~870×/hover cycle; early-exit saves allocation.
  enum class AppearanceVisualState {
    kUninitialized, kIdle, kHovered, kActive, kDropTarget, kSelected
  };
  AppearanceVisualState last_visual_state_ = AppearanceVisualState::kIdle;

  // P3: Skip redundant close-button icon swap (VectorIcon re-render) when the
  // minus/X state hasn't toggled. Called ~870×/hover cycle.
  bool last_close_button_show_minus_ = false;
  // False until the close glyphs are rendered for the current palette/theme.
  bool close_button_images_valid_ = false;
  SkColor last_title_color_ = SK_ColorTRANSPARENT;
  bool split_compact_ = false;
  MahoSidebarPalette palette_;
};

class SidebarFolderRowView : public views::View,
                             public views::DragController,
                             public views::ContextMenuController,
                             public views::TextfieldController {
  METADATA_HEADER(SidebarFolderRowView, views::View)

 public:
  SidebarFolderRowView(const SidebarTreeNode& node,
                       MahoSidebarTabSection section,
                       std::string active_space_id,
                       Browser* browser,
                       base::RepeatingClosure toggle_callback,
                       base::RepeatingCallback<void(const std::string&,
                                                    const std::u16string&)>
                           rename_callback,
                       std::string next_sibling_folder_id = std::string(),
                       std::string parent_folder_id = std::string(),
                       bool has_children = false);
  SidebarFolderRowView(const SidebarFolderRowView&) = delete;
  SidebarFolderRowView& operator=(const SidebarFolderRowView&) = delete;
  ~SidebarFolderRowView() override;

  void OnThemeChanged() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;

  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  void OnDragDone() override;

  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& current_pt) override;

  void BeginFolderEditing();

  void SetFavoritesDropAcceptedCallback(
      base::RepeatingCallback<void(const std::string&)> callback) {
    favorites_drop_accepted_callback_ = std::move(callback);
  }

  void SetRevealEmptyPinnedDropLaneCallback(
      base::RepeatingClosure callback) {
    reveal_empty_pinned_drop_lane_callback_ = std::move(callback);
  }

  void SetHoverPopupCallbacks(base::RepeatingClosure show, base::RepeatingClosure hide) {
    show_popup_callback_ = std::move(show);
    hide_popup_callback_ = std::move(hide);
  }

  void SetSpringExpandCallback(base::RepeatingClosure callback) {
    spring_expand_callback_ = std::move(callback);
  }

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  bool is_editing_for_testing() const { return is_editing_name_; }
  views::Textfield* name_field_for_testing() { return name_field_; }
  views::LabelButton* label_button_for_testing() { return label_button_; }

  enum class SidebarFolderDropZone {
    kNone,
    kBefore,
    kInto,
    kAfter,
  };

  SidebarFolderDropZone drop_zone_for_testing() const {
    return active_drop_zone_;
  }

  const std::string& folder_id_for_testing() const { return folder_id_; }
  const std::string& folder_id() const { return folder_id_; }
  const std::string& parent_folder_id() const { return parent_folder_id_; }
  MahoSidebarTabSection section() const { return section_; }
  void SetExpanded(bool expanded);
  void SetSidebarPalette(const MahoSidebarPalette& palette);

 private:
  SidebarFolderDropZone ResolveDropZone(const ui::DropTargetEvent& event,
                                         const SidebarDragPayload& payload) const;
  MahoSidebarTabListView* GetTabListView();
  void EndFolderEditing(bool commit);
  void ToggleFromLabelButton(const ui::Event& event);
  void RunToggleDeferred();
  void UpdateAppearance();

  const std::string folder_id_;
  const std::string space_id_;
  const bool folder_is_pinned_;
  const std::string next_sibling_folder_id_;
  const std::string parent_folder_id_;
  const bool has_children_;
  bool is_expanded_;
  MahoSidebarTabSection section_;
  raw_ptr<Browser> browser_ = nullptr;
  base::RepeatingCallback<void(const std::string&, const std::u16string&)>
      rename_callback_;
  base::RepeatingCallback<void(const std::string&)>
      favorites_drop_accepted_callback_;
  base::RepeatingClosure reveal_empty_pinned_drop_lane_callback_;
  base::RepeatingClosure show_popup_callback_;
  base::RepeatingClosure hide_popup_callback_;
  base::RepeatingClosure spring_expand_callback_;
  base::OneShotTimer spring_load_timer_;
  base::RepeatingClosure toggle_callback_;
  bool toggle_pending_ = false;
  bool toggle_handled_for_current_press_ = false;
  std::u16string committed_name_;
  bool is_editing_name_ = false;
  raw_ptr<views::View> folder_glyph_container_ = nullptr;
  raw_ptr<views::ImageView> folder_icon_ = nullptr;
  raw_ptr<views::LabelButton> label_button_ = nullptr;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  bool drag_over_drop_target_ = false;
  enum class FolderVisualState { kIdle, kHovered, kDropTarget };
  FolderVisualState last_folder_visual_state_ = FolderVisualState::kIdle;
  SidebarFolderDropZone active_drop_zone_ = SidebarFolderDropZone::kNone;
  std::unique_ptr<MahoFolderContextMenu> active_folder_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_folder_menu_model_;
  std::unique_ptr<views::MenuRunner> active_folder_menu_runner_;

  mutable bool cached_icon_expanded_ = false;
  mutable SkColor cached_icon_color_ = SK_ColorTRANSPARENT;
  mutable bool icon_cache_valid_ = false;
  MahoSidebarPalette palette_;

  base::WeakPtrFactory<SidebarFolderRowView> weak_factory_{this};
};

struct SidebarSectionDropIndicatorStateForTesting {
  bool visible_after_update = false;
  bool visible_after_exit = false;
};

views::View::DropCallback GetSectionDropCallbackForTesting(
    MahoSidebarTabSection section,
    const ui::DropTargetEvent& event);
SidebarSectionDropIndicatorStateForTesting
GetSectionDropIndicatorStateForTesting(MahoSidebarTabSection section,
                                        const ui::DropTargetEvent& event);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_LIST_VIEW_H_
