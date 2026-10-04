// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOLDER_HOVER_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOLDER_HOVER_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoSidebarFolderHoverController : public views::WidgetObserver,
                                         public TabStripModelObserver {
 public:
  explicit MahoSidebarFolderHoverController(Browser* browser);
  ~MahoSidebarFolderHoverController() override;

  void ScheduleShow(views::View* anchor,
                    std::string folder_id,
                    std::u16string folder_name,
                    std::vector<SidebarTreeNode> children);

  void OnRowMouseExited();
  void HideImmediately();

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  base::WeakPtr<MahoSidebarFolderHoverController> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  void ShowNow();
  void OnHideGraceTimerFired();
  void OnActivateTab(const std::string& tab_id);
  void OnActivateTabDelayed(int index);
  bool IsCursorInsidePopup() const;

  raw_ptr<Browser> browser_;
  base::OneShotTimer show_timer_;
  base::OneShotTimer hide_grace_timer_;
  views::ViewTracker anchor_tracker_;

  // Pending show params
  std::string pending_folder_id_;
  std::u16string pending_folder_name_;
  std::vector<SidebarTreeNode> pending_children_;

  // Open popup state
  raw_ptr<views::Widget> popup_widget_ = nullptr;
  std::string open_folder_id_;
  
  // C1 Focus loss & browser widget tracking
  raw_ptr<views::Widget> observed_browser_widget_ = nullptr;

  // M1 Grace timer cap
  static constexpr int kMaxGraceRearms = 10;
  int grace_rearm_count_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);

  // WeakPtrFactory must be the absolute last member inside the class definition
  base::WeakPtrFactory<MahoSidebarFolderHoverController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOLDER_HOVER_CONTROLLER_H_
