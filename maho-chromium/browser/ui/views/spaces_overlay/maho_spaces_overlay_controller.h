// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_CONTROLLER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class Widget;
}  // namespace views

namespace maho {

class MahoSpacesOverlayView;
class MahoSidebarContainerView;

class MahoSpacesOverlayController : public views::WidgetObserver,
                                    public MahoSpaceProfileBridge::Observer {
 public:
  enum class DismissReason {
    kExplicitCancel,
    kAction,
    kDeactivation,
  };

  explicit MahoSpacesOverlayController(Browser* browser);
  MahoSpacesOverlayController(const MahoSpacesOverlayController&) = delete;
  MahoSpacesOverlayController& operator=(
      const MahoSpacesOverlayController&) = delete;
  ~MahoSpacesOverlayController() override;

  void Toggle(views::Widget* parent_widget);
  void Show(views::Widget* parent_widget);
  void Dismiss(DismissReason reason);
  bool IsVisible() const;
  void SetPalette(const MahoSidebarPalette& palette);
  // Re-pulls the docked rail's authoritative snapshot and applies it to the
  // live overlay. Wired as the overlay view's theme-refresh callback: the
  // overlay is its own top-level widget, so a theme change delivered to that
  // widget alone must not leave the overlay painted from a snapshot the rail has
  // already replaced.
  void RefreshPaletteFromSidebar();

  views::Widget* GetOverlayWidgetForTesting() const { return widget_.get(); }

  void set_dismiss_callback(base::RepeatingClosure callback) {
    dismiss_callback_ = std::move(callback);
  }

  void set_sidebar_container(MahoSidebarContainerView* container) {
    sidebar_container_ = container;
  }

  void set_disable_animation_for_testing(bool disable) {
    disable_animation_for_testing_ = disable;
  }

  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  void OnSpaceProfileBridgeChanged(bool is_structural) override;
  void OnSpaceProfileBridgeChanged() override;

 private:
  void Hide();
  void CloseWidget(bool restore_focus, bool suppress_dismiss_callback = false);
  void FinishHide();
  gfx::Rect ComputeOverlayBounds() const;
  void UpdateBounds();

  friend class MahoSpacesOverlayControllerTest;

  raw_ptr<Browser> browser_;
  raw_ptr<MahoSidebarContainerView> sidebar_container_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoSpacesOverlayView> overlay_view_ = nullptr;
  raw_ptr<views::Widget> parent_widget_ = nullptr;
  views::ViewTracker focus_restore_tracker_;
  bool is_hiding_ = false;
  bool restore_focus_on_hide_ = false;
  // True from the moment Show() calls widget_->Activate() until the first
  // posted task fires. Suppresses parent-deactivation dismissal during the
  // activation handoff window, when widget_->IsActive() may not yet reflect
  // the new active state.
  bool is_showing_ = false;
  bool disable_animation_for_testing_ = false;
  // Guarantees the overlay widget is destroyed even when the fade-out
  // animation never reports completion. Without it a dropped OnEnded/OnAborted
  // leaves a full-size, invisible widget hit-testing over the web contents.
  base::OneShotTimer hide_watchdog_;
  base::RepeatingClosure dismiss_callback_;
  MahoSidebarPalette palette_;
  base::WeakPtrFactory<MahoSpacesOverlayController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_CONTROLLER_H_
