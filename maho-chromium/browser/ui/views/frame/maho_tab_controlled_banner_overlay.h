#ifndef MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_TAB_CONTROLLED_BANNER_OVERLAY_H_
#define MAHO_BROWSER_UI_VIEWS_FRAME_MAHO_TAB_CONTROLLED_BANNER_OVERLAY_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "maho/browser/ui/browser_user_data.h"
#include "maho/browser/ui/views/frame/maho_tab_controlled_banner_view.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace maho {

class MahoTabControlledBannerOverlay
    : public BrowserUserData<MahoTabControlledBannerOverlay>,
      public views::WidgetObserver,
      public maho::ai::MahoControlActivityService::Observer,
      public TabStripModelObserver,
      public BrowserCollectionObserver {
 public:
  explicit MahoTabControlledBannerOverlay(Browser* browser);
  ~MahoTabControlledBannerOverlay() override;
  MahoTabControlledBannerOverlay(const MahoTabControlledBannerOverlay&) = delete;
  MahoTabControlledBannerOverlay& operator=(
      const MahoTabControlledBannerOverlay&) = delete;

  void ShowBanner(int64_t tab_id, const std::u16string& controller_name);
  void HideBanner();
  bool IsVisible() const;

  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  void OnControlActivityChanged(
      const maho::ai::ControlActivity& activity) override;

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  MahoTabControlledBannerView* banner_view_for_testing() const {
    return banner_view_;
  }
  views::Widget* widget_for_testing() const { return widget_.get(); }

 private:
  void CreateWidget();
  void UpdatePosition();
  gfx::Rect ComputeTargetBounds() const;
  void OnStopRequested(int64_t tab_id);

  const raw_ptr<Browser> browser_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoTabControlledBannerView> banner_view_ = nullptr;
  raw_ptr<views::Widget> parent_widget_observed_ = nullptr;

  int64_t current_controlled_tab_id_ = 0;
  std::u16string current_controller_name_;

  base::ScopedObservation<maho::ai::MahoControlActivityService,
                          maho::ai::MahoControlActivityService::Observer>
      control_observation_{this};

  base::WeakPtrFactory<MahoTabControlledBannerOverlay> weak_factory_{this};
};

}

#endif
