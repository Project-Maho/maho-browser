// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_COORDINATOR_H_
#define MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_COORDINATOR_H_

#include <cstdint>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

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

class MahoShieldBubbleCoordinator : public views::WidgetObserver,
                                    public content::WebContentsObserver,
                                    public TabStripModelObserver {
 public:
  MahoShieldBubbleCoordinator();
  MahoShieldBubbleCoordinator(const MahoShieldBubbleCoordinator&) = delete;
  MahoShieldBubbleCoordinator& operator=(const MahoShieldBubbleCoordinator&) =
      delete;
  ~MahoShieldBubbleCoordinator() override;

  static MahoShieldBubbleCoordinator& GetForBrowser(Browser* browser);

  void Show(views::View* anchor_view,
            Browser* browser,
            content::WebContents* target_web_contents = nullptr);
  void Show(views::View* anchor_view,
            const std::string& origin,
            uint32_t blocked_count,
            bool is_excepted,
            content::WebContents* target_web_contents = nullptr);
  void Show(views::Widget* anchor_widget,
            const gfx::Rect& anchor_rect,
            const std::string& origin,
            uint32_t blocked_count,
            bool is_excepted,
            content::WebContents* target_web_contents);
  void Hide();
  bool IsShowing() const;
  views::Widget* GetBubbleWidgetForTesting() const;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // content::WebContentsObserver:
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

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::Widget> bubble_widget_ = nullptr;
  raw_ptr<TabStripModel> observed_tab_strip_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  views::ViewTracker anchor_view_tracker_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_COORDINATOR_H_
