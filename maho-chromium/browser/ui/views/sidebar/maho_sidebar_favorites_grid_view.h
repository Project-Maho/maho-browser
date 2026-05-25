// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <array>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/favicon_base/favicon_types.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class Browser;
class MahoFavoritesContextMenu;

namespace ui {
class SimpleMenuModel;
}

namespace views {
class Button;
class ImageView;
class Label;
class MenuRunner;
class View;
}  // namespace views

namespace maho {

class MahoSidebarFavoritesGridView : public views::View {
  METADATA_HEADER(MahoSidebarFavoritesGridView, views::View)

 public:
  static constexpr size_t kTileCount =
      sidebar_layout::kMahoSidebarFavoriteSlotCount;
  static constexpr size_t kRowCount =
      kTileCount / sidebar_layout::kFavoritesColumns;

  explicit MahoSidebarFavoritesGridView(Browser* browser);
  MahoSidebarFavoritesGridView(const MahoSidebarFavoritesGridView&) = delete;
  MahoSidebarFavoritesGridView& operator=(
      const MahoSidebarFavoritesGridView&) = delete;
  ~MahoSidebarFavoritesGridView() override;

  void Update(const MahoSidebarFavoritesModel& model);
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;

  // Called by a visible parent (e.g. MahoSidebarView) to ensure the empty
  // favorites surface is shown during a valid drag so it can accept drops.
  void RevealForDrag();
  // Called by the parent when the drag leaves the sidebar region, clearing any
  // temporary drag-highlight state.
  void CollapseFromDrag();

  ui::DropTargetEvent ForwardDropEventToGrid(
      const ui::DropTargetEvent& event,
      const views::View* source) const;

  views::View::DropCallback GetDropCallbackForTesting(
      const ui::DropTargetEvent& event) {
    return GetDropCallback(event);
  }

  bool is_drop_indicator_visible_for_testing() const {
    return drag_over_drop_target_;
  }

  bool is_drag_reveal_active_for_testing() const {
    return drag_reveal_active_;
  }

  views::Button* GetTileForTesting(size_t index) {
    return index < kTileCount ? tile_views_[index].get() : nullptr;
  }

  void ShowTileContextMenu(size_t index,
                           const gfx::Point& point,
                           ui::mojom::MenuSourceType source_type);

  void MarkDropAccepted(const std::string& tab_id);

 private:
  struct LayoutSpec {
    LayoutSpec();
    LayoutSpec(const LayoutSpec&);
    LayoutSpec& operator=(const LayoutSpec&);
    ~LayoutSpec();

    std::vector<int> row_counts;
    int max_columns = 1;
    bool uses_compact_row_card = false;
  };

  void OnTilePressed(size_t index);
  void OnTileDragDone(const std::string& tab_id);
  int ResolveDropIndex(const gfx::Point& point,
                       const std::string& source_tab_id,
                       bool source_is_favorite) const;
  int TileIndexForPoint(const gfx::Point& point, int item_count) const;
  std::array<gfx::Rect, kTileCount> GetTileBoundsForCount(int item_count) const;
  size_t GetVisibleFavoriteCount() const;
  static LayoutSpec GetLayoutSpec(size_t favorite_count, int available_width);
  static int GetPreferredHeightForWidth(int available_width,
                                        size_t favorite_count);
  void RebuildGridRows(size_t favorite_count);
  void UpdateContainerOwnedPreferredHeight(size_t favorite_count);
  void UpdateEmptyStateHighlight(bool highlighted);
  void OnFaviconLoaded(size_t index,
                       const GURL& requested_url,
                       const favicon_base::FaviconImageResult& result);

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::View> content_view_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  raw_ptr<views::View> empty_state_card_ = nullptr;
  size_t favorite_count_ = 0;
  std::array<std::unique_ptr<views::View>, kTileCount> owned_tiles_;
  std::array<raw_ptr<views::Button>, kTileCount> tile_views_ = {};
  std::array<raw_ptr<views::View>, kTileCount> tile_glyph_containers_ = {};
  std::array<raw_ptr<views::ImageView>, kTileCount> tile_icons_ = {};
  std::array<raw_ptr<views::Label>, kTileCount> tile_labels_ = {};
  std::array<GURL, kTileCount> tile_urls_ = {};
  std::array<std::u16string, kTileCount> tile_titles_ = {};
  std::array<std::string, kTileCount> tile_tab_ids_ = {};
  std::vector<raw_ptr<views::View>> row_views_;
  LayoutSpec current_layout_spec_;
  bool drag_over_drop_target_ = false;
  bool drag_reveal_active_ = false;
  std::string drag_accepted_tab_id_;

  std::unique_ptr<MahoFavoritesContextMenu> active_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_menu_model_;
  std::unique_ptr<views::MenuRunner> active_menu_runner_;

  // --- Animation infrastructure ---
  std::array<gfx::Rect, kTileCount> snapshot_bounds_ = {};
  std::array<std::string, kTileCount> snapshot_tab_ids_ = {};
  int current_drag_preview_index_ = -1;

  // Animation helpers
  void SnapshotCurrentBounds();
  void AnimateReorder();
  void AnimateInsertion(size_t tile_index);
  void AnimateRemoval(size_t tile_index, base::OnceClosure on_complete);
  void AnimateDragPreview(int preview_index, const std::string& dragged_tab_id);
  void CancelAnimations();
  int FindSnapshotIndexForTabId(const std::string& tab_id) const;

  base::CancelableTaskTracker cancelable_task_tracker_;
  base::WeakPtrFactory<MahoSidebarFavoritesGridView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FAVORITES_GRID_VIEW_H_
