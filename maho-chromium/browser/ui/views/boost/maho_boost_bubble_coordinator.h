// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_BUBBLE_COORDINATOR_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_BUBBLE_COORDINATOR_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"
#include "url/origin.h"

class Browser;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoBoostBubbleView;

class MahoBoostBubbleCoordinator : public views::WidgetObserver,
                                    public content::WebContentsObserver,
                                    public TabStripModelObserver {
 public:
  MahoBoostBubbleCoordinator();
  MahoBoostBubbleCoordinator(const MahoBoostBubbleCoordinator&) = delete;
  MahoBoostBubbleCoordinator& operator=(const MahoBoostBubbleCoordinator&) =
      delete;
  ~MahoBoostBubbleCoordinator() override;

  static MahoBoostBubbleCoordinator& GetForBrowser(Browser* browser);

  void Show(Browser* browser);
  void Show(views::View* anchor_view,
            Browser* browser,
            content::WebContents* target_web_contents);

  void Hide();
  bool IsShowing() const;
  views::Widget* GetBubbleWidgetForTesting() const;

  base::CallbackListSubscription RegisterOnCloseCallback(
      base::RepeatingClosure callback);

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  // Owning browser; stored so OnTabStripModelDestroyed() can erase this
  // coordinator from the global map in O(1) without scanning.
  raw_ptr<Browser> browser_ = nullptr;

  raw_ptr<MahoBoostBubbleView> bubble_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::RepeatingClosureList on_close_callbacks_;
  views::ViewTracker anchor_view_tracker_;

  // Origin of the page for which the bubble was last opened, used to detect
  // cross-origin primary-main-frame navigations that should auto-close it.
  url::Origin observed_origin_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_BUBBLE_COORDINATOR_H_
