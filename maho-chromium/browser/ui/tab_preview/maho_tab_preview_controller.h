// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TAB_PREVIEW_MAHO_TAB_PREVIEW_CONTROLLER_H_
#define MAHO_BROWSER_UI_TAB_PREVIEW_MAHO_TAB_PREVIEW_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class View;
class ViewTracker;
class Widget;
}  // namespace views

namespace maho {

class MahoTabPreviewController : public views::WidgetObserver {
 public:
  explicit MahoTabPreviewController(Browser* browser);
  ~MahoTabPreviewController() override;

  void ShowPreview(views::View* anchor_view, int tab_index);
  void HidePreview();

  bool IsShowingForTesting() const;
  views::Widget* preview_widget_for_testing() const { return bubble_widget_; }
  const std::u16string& preview_title_for_testing() const {
    return preview_title_for_testing_;
  }
  const std::u16string& preview_url_for_testing() const {
    return preview_url_for_testing_;
  }
  void SetShowDelayForTesting(base::TimeDelta delay) {
    show_delay_for_testing_ = delay;
  }

 private:
  void ShowPreviewNow();
  void ResetAnchorView();
  std::u16string GetUrlTextForTab(int tab_index) const;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  raw_ptr<Browser> browser_;
  raw_ptr<views::Widget> bubble_widget_ = nullptr;
  std::unique_ptr<views::ViewTracker> anchor_view_tracker_;
  base::OneShotTimer show_timer_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      bubble_widget_observation_{this};
  int pending_tab_index_ = -1;
  std::optional<base::TimeDelta> show_delay_for_testing_;
  std::u16string preview_title_for_testing_;
  std::u16string preview_url_for_testing_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TAB_PREVIEW_MAHO_TAB_PREVIEW_CONTROLLER_H_
