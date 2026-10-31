// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LIBRARY_OVERLAY_MAHO_LIBRARY_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_LIBRARY_OVERLAY_MAHO_LIBRARY_OVERLAY_CONTROLLER_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/views/library_overlay/maho_library_overlay_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class Widget;
}  // namespace views

namespace maho {

class MahoSidebarContainerView;

class MahoLibraryOverlayController : public views::WidgetObserver {
 public:
  using Category = MahoSidebarLibraryRailView::Category;
  using RestoreCallback = MahoLibraryOverlayView::RestoreCallback;
  using DeleteCallback = MahoLibraryOverlayView::DeleteCallback;

  enum class DismissReason {
    kExplicitCancel,
    kAction,
    kDeactivation,
  };

  explicit MahoLibraryOverlayController(Browser* browser);
  MahoLibraryOverlayController(const MahoLibraryOverlayController&) = delete;
  MahoLibraryOverlayController& operator=(const MahoLibraryOverlayController&) =
      delete;
  ~MahoLibraryOverlayController() override;

  void Toggle(views::Widget* parent_widget, Category category);
  void Show(views::Widget* parent_widget, Category category);
  void Dismiss(DismissReason reason);
  bool IsVisible() const;
  void SetCategory(Category category);
  void SetPalette(const MahoSidebarPalette& palette);
  Category category() const { return category_; }

  // Re-anchors the overlay to the sidebar's right edge. Safe to call when the
  // overlay is not showing (no-op).
  void UpdateBounds();

  void set_sidebar_container(MahoSidebarContainerView* container) {
    sidebar_container_ = container;
  }
  void set_dismiss_callback(base::RepeatingClosure callback) {
    dismiss_callback_ = std::move(callback);
  }
  void set_restore_callback(RestoreCallback callback) {
    restore_callback_ = std::move(callback);
  }
  void set_delete_callback(DeleteCallback callback) {
    delete_callback_ = std::move(callback);
  }

  // Re-pulls the docked rail's authoritative snapshot and applies it to the
  // live overlay. Wired as the overlay view's theme-refresh callback: the
  // overlay is its own top-level widget, so a theme change delivered to that
  // widget alone must not leave the overlay painted from a snapshot the rail has
  // already replaced.
  void RefreshPaletteFromSidebar();

  views::Widget* GetOverlayWidgetForTesting() const { return widget_.get(); }
  int GetOverlayWidthForTesting() const;
  MahoLibraryOverlayView* GetOverlayViewForTesting() const {
    return overlay_view_;
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

 private:
  void Hide();
  void CloseWidget(bool restore_focus, bool suppress_dismiss_callback = false);
  gfx::Rect ComputeOverlayBounds() const;

  raw_ptr<Browser> browser_;
  raw_ptr<MahoSidebarContainerView> sidebar_container_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoLibraryOverlayView> overlay_view_ = nullptr;
  raw_ptr<views::Widget> parent_widget_ = nullptr;
  views::ViewTracker focus_restore_tracker_;
  Category category_ = Category::kArchivedTabs;
  bool is_hiding_ = false;
  bool restore_focus_on_hide_ = false;
  bool is_showing_ = false;
  base::RepeatingClosure dismiss_callback_;
  RestoreCallback restore_callback_;
  DeleteCallback delete_callback_;
  MahoSidebarPalette palette_;

  base::WeakPtrFactory<MahoLibraryOverlayController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LIBRARY_OVERLAY_MAHO_LIBRARY_OVERLAY_CONTROLLER_H_
