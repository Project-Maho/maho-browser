// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoCommandOverlayController : public views::WidgetObserver {
 public:
  explicit MahoCommandOverlayController(Browser* browser);
  MahoCommandOverlayController(const MahoCommandOverlayController&) = delete;
  MahoCommandOverlayController& operator=(
      const MahoCommandOverlayController&) = delete;
  ~MahoCommandOverlayController() override;

  void Toggle(views::View* anchor);
  void Show(views::View* anchor);
  void Hide();
  bool IsVisible() const;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

   private:
  gfx::Rect ComputePopupBounds() const;
  void Reposition();

  raw_ptr<Browser> browser_;
  raw_ptr<views::Widget> widget_ = nullptr;
  raw_ptr<views::View> anchor_ = nullptr;
  raw_ptr<views::Widget> parent_widget_ = nullptr;
  bool is_hiding_ = false;

  base::WeakPtrFactory<MahoCommandOverlayController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
