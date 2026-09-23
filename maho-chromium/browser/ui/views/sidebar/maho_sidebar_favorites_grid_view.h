// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/favicon_base/favicon_types.h"
#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_favorite_edit_dialog.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"

#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class Browser;
class PrefService;

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

class MahoSidebarFavoritesGridView : public views::View,
                                     public MahoFavoritesContextMenu::Delegate {
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
  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);
  bool ActivateFavoriteByIndex(size_t index);
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void Layout(PassKey) override;

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
  // Called by the parent when a drag session originating from this grid starts
  // so the source tile can be visually hidden while the preview animates.
  void OnDragStarted(const std::string& tab_id);
  // Called by the parent when the drag session ends (drop or cancel) so the
  // grid can restore its tile layer state.
  void OnDragEnded();
  void OnExternalDragStarted(const std::string& tab_id);
  void CleanupAfterDrop();
  void HideOverlayAndPhantom();
  void SetActiveDragGhostImage(const gfx::ImageSkia& image,
                               const gfx::Vector2d& offset);
  void RestoreOsDragGhostImage();

  // Fix 3: Called by MahoSidebarView when cursor exits favorites subarea.
  void OnFavoritesZoneExited();

  // Fix 4: Force a synchronous refresh after drop to pick up Rust state.
  void ForceRefreshFromDrop();

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

  bool insertion_preview_visible_for_testing() const {
    return drag_preview_tile_ && drag_preview_tile_->GetVisible();
  }

  const std::string& active_drag_tab_id_for_testing() const {
    return active_drag_tab_id_;
  }

  const std::string& drag_accepted_tab_id_for_testing() const {
    return drag_accepted_tab_id_;
  }

  const std::string& pending_external_drag_tab_id_for_testing() const {
    return pending_external_drag_tab_id_;
  }

  int TileIndexForPointForTesting(const gfx::Point& point,
                                  int item_count) const {
    return TileIndexForPoint(point, item_count);
  }

  views::Button* GetTileForTesting(size_t index) {
    return index < kTileCount ? tile_views_[index].get() : nullptr;
  }

  std::string tile_tab_id_for_testing(size_t index) const {
    return index < tile_tab_ids_.size() ? tile_tab_ids_[index]
                                        : std::string();
  }

  gfx::Rect tile_bounds_in_grid_for_testing(size_t index) const {
    return TileBoundsInGrid(index);
  }

  bool last_update_had_reorder_animation_for_testing() const {
    return last_update_had_reorder_animation_for_testing_;
  }

  gfx::Rect last_insertion_animation_bounds_for_testing() const {
    return last_insertion_animation_bounds_for_testing_;
  }

  views::View* empty_state_view_for_testing() { return empty_state_view_; }

  void DismissHintForTesting() { OnFavoritesHintDismissed(); }

  void SetPrefsForTesting(PrefService* prefs) { prefs_ = prefs; }

  uint64_t favicon_generation_for_testing(size_t index) const {
    return index < tile_favicon_generations_.size()
               ? tile_favicon_generations_[index]
               : 0;
  }
  ui::ImageModel tile_favicon_for_testing(size_t index) const;
  ui::ImageModel cached_favicon_for_tab_id_for_testing(
      const std::string& tab_id) const;
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }
  void RunFaviconLoadedForTesting(
      size_t index,
      const std::string& tab_id,
      const GURL& requested_url,
      uint64_t generation,
      const favicon_base::FaviconImageResult& result) {
    OnFaviconLoaded(index, tab_id, requested_url, generation, result);
  }

  void ShowTileContextMenu(size_t index,
                           const gfx::Point& point,
                           ui::mojom::MenuSourceType source_type);

  // MahoFavoritesContextMenu::Delegate:
  void OnFavoriteShareRequested() override;
  void OnFavoriteRenameRequested() override;
  void OnFavoriteIconChangeRequested() override;
  void OnFavoritePinnedUrlEditRequested() override;

  void MarkDropAccepted(const std::string& tab_id);

 private:
  MahoSidebarPalette palette_;
  enum class DragIdentityCleanup { kKeep, kClear };

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
  void OnFavoritesHintDismissed();
  int ResolveDropIndex(const gfx::Point& point,
                       const std::string& source_tab_id,
                       bool source_is_favorite) const;  // L3-EXEMPT: local parameter
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
                       const std::string& tab_id,
                       const GURL& requested_url,
                       uint64_t generation,
                       const favicon_base::FaviconImageResult& result);
  bool IsCurrentFaviconRequest(size_t index,
                               const std::string& tab_id,
                               const GURL& requested_url,
                               uint64_t generation) const;
  size_t FindTileIndexForTabId(const std::string& tab_id) const;
  void ApplyTileFavicon(size_t index,
                        const std::string& tab_id,
                        const ui::ImageModel& favicon,
                        bool cache_by_tab_id);

  // Insertion-preview (phantom) tile: shown at the target drop index while a
  // tab is being dragged FROM the tab list INTO the favorites grid. Gives the
  // "favicon+box morph" feedback the user expects when the drag ghost (a tab
  // row snapshot) reaches the favorites zone.
  void ShowInsertionPreview(const gfx::Point& cursor,
                            const std::string& tab_id);
  void HideInsertionPreview();
  void ResetDragFeedback(DragIdentityCleanup identity_cleanup,
                         bool restore_os_drag_image);
  void OnInsertionPreviewFaviconLoaded(
      const std::string& tab_id,
      const GURL& requested_url,
      const favicon_base::FaviconImageResult& result);

  ui::ImageModel ResolveFaviconForTabId(const std::string& tab_id);

  // Applies the tile's custom glyph when set: the glyph Label takes over the
  // favicon ImageView's slot inside the glyph container.
  void ApplyTileCustomIcon(size_t index);
  void OpenEditDialog(MahoFavoriteEditDialog::Focus focus);
  // |original_name| / |original_url| are the values the dialog was prefilled
  // with; Save dispatches set_tab_custom_title / set_tab_pinned_url only for
  // fields the user actually changed, so saving a dialog opened for one field
  // cannot freeze a derived title or author an unset pinned URL.
  void OnEditDialogAccepted(const std::string& tab_id,
                            std::u16string original_name,
                            std::string original_url,
                            std::u16string name,
                            std::string icon,
                            std::string url);

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<PrefService> prefs_ = nullptr;
  raw_ptr<views::View> content_view_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  raw_ptr<views::View> empty_state_card_ = nullptr;
  size_t favorite_count_ = 0;
  std::array<std::unique_ptr<views::View>, kTileCount> owned_tiles_;
  std::array<raw_ptr<views::Button>, kTileCount> tile_views_ = {};
  std::array<raw_ptr<views::View>, kTileCount> tile_glyph_containers_ = {};
  std::array<raw_ptr<views::ImageView>, kTileCount> tile_icons_ = {};
  std::array<raw_ptr<views::Label>, kTileCount> tile_labels_ = {};
  std::array<raw_ptr<views::Label>, kTileCount> tile_icon_labels_ = {};
  std::array<std::u16string, kTileCount> tile_custom_icons_ = {};
  std::array<std::string, kTileCount> tile_pinned_urls_ = {};
  std::array<GURL, kTileCount> tile_urls_ = {};
  std::array<std::u16string, kTileCount> tile_titles_ = {};
  // Raw authored custom titles per slot (empty = never renamed); feeds the
  // edit-dialog prefill. Never derived from the resolved display text.
  std::array<std::u16string, kTileCount> tile_custom_titles_ = {};
  std::array<std::string, kTileCount> tile_tab_ids_ = {};
  std::array<uint64_t, kTileCount> tile_favicon_generations_ = {};
  base::flat_map<std::string, ui::ImageModel> cached_favicons_by_tab_id_;
  struct DominantColorCacheEntry {
    ui::ImageModel favicon;
    std::optional<SkColor> color;
  };
  base::flat_map<std::string, DominantColorCacheEntry>
      dominant_color_cache_by_tab_id_;
  std::vector<raw_ptr<views::View>> row_views_;
  LayoutSpec current_layout_spec_;
  bool drag_over_drop_target_ = false;
  bool drag_reveal_active_ = false;
  std::string active_drag_tab_id_;
  std::string drag_accepted_tab_id_;
  std::string pending_external_drag_tab_id_;

  std::unique_ptr<MahoFavoritesContextMenu> active_context_menu_;
  size_t active_context_menu_index_ = 0;
  // Identity snapshot captured at menu-open time. Delegate callbacks and the
  // edit dialog must not re-read the tile arrays by index: a background
  // Update() while the menu is open would re-point them and apply Share/
  // Rename/Icon/Pinned actions to a different favorite.
  std::string active_context_menu_tab_id_;
  GURL active_context_menu_url_;
  std::u16string active_context_menu_title_;
  std::string active_context_menu_pinned_url_;
  std::u16string active_context_menu_custom_icon_;
  std::u16string active_context_menu_custom_title_;
  std::unique_ptr<ui::SimpleMenuModel> active_menu_model_;
  std::unique_ptr<views::MenuRunner> active_menu_runner_;



  // --- Animation infrastructure ---
  std::array<gfx::Rect, kTileCount> snapshot_bounds_ = {};
  std::array<std::string, kTileCount> snapshot_tab_ids_ = {};
  int current_drag_preview_index_ = -1;
  bool last_update_had_reorder_animation_for_testing_ = false;
  gfx::Rect last_insertion_animation_bounds_for_testing_;

  raw_ptr<views::View> drag_preview_tile_ = nullptr;
  raw_ptr<views::ImageView> drag_preview_icon_ = nullptr;
  std::string drag_preview_tab_id_;
  GURL drag_preview_url_;
  int drag_preview_slot_ = -1;

  gfx::ImageSkia active_drag_ghost_image_;
  gfx::Vector2d active_drag_ghost_offset_;
  bool os_ghost_hidden_ = false;
  base::CancelableTaskTracker drag_preview_task_tracker_;

  // Animation helpers
  void ApplyImmediateLayoutForAnimation();
  gfx::Rect TileBoundsInGrid(size_t tile_index) const;
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
