// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_RAIL_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_RAIL_VIEW_H_

#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/view.h"

namespace ui {
class Event;
}  // namespace ui

namespace views {
class Button;
class ImageButton;
class View;
}  // namespace views

namespace maho {

class MahoSidebarLibraryRailItemButton;

const gfx::VectorIcon& GetMahoSidebarArchiveboxIcon();

class MahoSidebarLibraryRailView : public views::View {
  METADATA_HEADER(MahoSidebarLibraryRailView, views::View)

 public:
  // Stable persisted IDs. Do not reorder or renumber: removed legacy rail
  // categories previously occupied 4 and 5, so surviving values stay pinned.
  enum class Category {
    kArchivedTabs = 0,
    kDownloads = 1,
    kMedia = 2,
    kSpaces = 3,
  };

  using CategorySelectedCallback = base::RepeatingCallback<void(Category)>;

  MahoSidebarLibraryRailView();
  MahoSidebarLibraryRailView(const MahoSidebarLibraryRailView&) = delete;
  MahoSidebarLibraryRailView& operator=(const MahoSidebarLibraryRailView&) =
      delete;
  ~MahoSidebarLibraryRailView() override;

  void SetSelectedCategory(Category category);
  void ClearSelection();
  Category selected_category() const { return selected_category_; }

  void SetCategorySelectedCallback(CategorySelectedCallback callback);
  void SetBackCallback(base::RepeatingClosure callback);

  // Applies the resolved sidebar palette to every rail control. Called once
  // right after construction and on every subsequent palette change.
  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);

  views::Button* button_for_category_for_testing(Category category);
  std::vector<Category> visible_categories_for_testing() const;
  views::ImageButton* back_button_for_testing() { return back_button_; }

  void Layout(PassKey) override;
  void OnThemeChanged() override;

  // Returns true if `point` (in this view's coordinates) is not over any
  // interactive rail control and should be treated as window-draggable
  // caption area.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

 private:
  void HandleCategoryPressed(Category category, const ui::Event& event);
  void HandleBackPressed(const ui::Event& event);
  void UpdateSelection();
  void UpdateBackButtonAppearance();

  raw_ptr<views::View> utility_host_ = nullptr;
  raw_ptr<views::View> item_container_ = nullptr;
  std::vector<raw_ptr<MahoSidebarLibraryRailItemButton>> category_buttons_;
  raw_ptr<views::ImageButton> back_button_ = nullptr;
  Category selected_category_ = Category::kArchivedTabs;
  CategorySelectedCallback category_selected_callback_;
  base::RepeatingClosure back_callback_;
  MahoSidebarPalette palette_;
  base::WeakPtrFactory<MahoSidebarLibraryRailView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_RAIL_VIEW_H_
