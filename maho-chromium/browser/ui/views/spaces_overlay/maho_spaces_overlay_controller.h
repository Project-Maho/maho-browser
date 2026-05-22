// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class Widget;
}  // namespace views

namespace maho {

class MahoSpacesOverlayView;

class MahoSpacesOverlayController : public views::WidgetObserver {
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

  views::Widget* GetOverlayWidgetForTesting() const { return widget_.get(); }

  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

 private:
  void Hide();
  void CloseWidget(bool restore_focus);
  void FinishHide();
  gfx::Rect ComputeOverlayBounds() const;
  void UpdateBounds();

  friend class MahoSpacesOverlayControllerTest;

  raw_ptr<Browser> browser_;
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
  base::WeakPtrFactory<MahoSpacesOverlayController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_CONTROLLER_H_
