// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
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
  void Hide();
  bool IsVisible() const;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void Show(views::View* anchor);

  raw_ptr<Browser> browser_;
  raw_ptr<views::Widget> widget_ = nullptr;

  base::WeakPtrFactory<MahoCommandOverlayController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
