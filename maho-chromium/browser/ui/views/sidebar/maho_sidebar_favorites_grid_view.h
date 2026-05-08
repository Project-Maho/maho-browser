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
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class Browser;

namespace views {
class Button;
class ImageView;
class Label;
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

  // Called by a visible parent (e.g. MahoSidebarView) to reveal the empty
  // favorites grid during a valid drag so it can accept drops.
  void RevealForDrag();
  // Called by the parent when the drag leaves the sidebar region, collapsing
  // the revealed-but-still-empty grid back to zero height.
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
  bool ShouldSuppressDragRemoval(const gfx::Point& screen_point,
                                 const std::string& tab_id) const;
  void OnTileDragDone(size_t index);
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
  std::array<std::string, kTileCount> tile_tab_ids_ = {};
  std::vector<raw_ptr<views::View>> row_views_;
  LayoutSpec current_layout_spec_;
  bool drag_over_drop_target_ = false;
  bool drag_reveal_active_ = false;
  base::WeakPtrFactory<MahoSidebarFavoritesGridView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FAVORITES_GRID_VIEW_H_
