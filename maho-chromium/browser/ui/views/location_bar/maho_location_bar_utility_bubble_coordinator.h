// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_BUBBLE_COORDINATOR_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_BUBBLE_COORDINATOR_H_

#include "base/callback_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class TabStripModel;

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoLocationBarUtilityPanelView;

// Coordinator for the address-bar utility bubble instance.
// Modeled on MahoBoostBubbleCoordinator — extends widget-close observation
// with a TabStripModelObserver so the bubble is dismissed whenever the active
// tab changes, keeping the bubble scoped to the tab context in which it was
// opened.
//
// Lifetime:
//   Create one per browser window.  Call AttachToBrowser() once the browser's
//   TabStripModel is available.  The coordinator self-detaches when the
//   TabStripModel is destroyed (OnTabStripModelDestroyed).
class MahoLocationBarUtilityBubbleCoordinator
    : public views::WidgetObserver,
      public TabStripModelObserver {
 public:
  MahoLocationBarUtilityBubbleCoordinator();
  MahoLocationBarUtilityBubbleCoordinator(
      const MahoLocationBarUtilityBubbleCoordinator&) = delete;
  MahoLocationBarUtilityBubbleCoordinator& operator=(
      const MahoLocationBarUtilityBubbleCoordinator&) = delete;
  ~MahoLocationBarUtilityBubbleCoordinator() override;

  // Attaches this coordinator to the given browser's TabStripModel so that
  // active-tab changes auto-dismiss the bubble.  Safe to call multiple times
  // (re-attaches if the browser changes).  |browser| must outlive this call
  // unless DetachFromBrowser() is called first.
  void AttachToBrowser(Browser* browser);

  // Detaches from the currently observed TabStripModel.  Called automatically
  // by OnTabStripModelDestroyed; exposed for tests and explicit teardown.
  void DetachFromBrowser();

  // Shows the utility bubble anchored to |anchor_view|. If the bubble is
  // already showing, this is a no-op.
  void ShowBubble(views::View* anchor_view,
                  MahoLocationBarUtilityPanelModel model);

  // Hides the currently showing bubble, if any.
  void Hide();

  // Returns true if the utility bubble is currently showing.
  bool IsShowing() const;

  // Returns the current bubble view, or nullptr if not showing.
  MahoLocationBarUtilityPanelView* GetBubble() const;

  // Register a callback invoked when the bubble closes (for any reason).
  base::CallbackListSubscription RegisterOnCloseCallback(
      base::RepeatingClosure callback);

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<MahoLocationBarUtilityPanelView> bubble_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::RepeatingClosureList on_close_callbacks_;

  // Non-owning pointer to the observed tab strip; nullptr when not attached.
  raw_ptr<TabStripModel> observed_tab_strip_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_BUBBLE_COORDINATOR_H_
