// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost.mojom.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class Profile;
class TabStripModel;

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace views {
class Widget;
}  // namespace views

namespace maho {

// Size constants for the two Boost editor modes.
inline constexpr int kBoostModeWidth = 184;
inline constexpr int kBoostModeHeight = 582;
inline constexpr int kCodeModeWidth = 452;
inline constexpr int kCodeModeHeight = 582;
inline constexpr int kCodeModeRightAlignOffsetPx = 265;

// Owns a non-modal top-level native views::Widget (TYPE_WINDOW) that hosts the
// Boost WebUI editor for a single browser instance.  One controller per
// browser, kept alive in the static per-browser map.
//
// On macOS the window is frameless (remove_standard_frame); custom drag regions
// are handled via -webkit-app-region:drag on the WebUI side.  On all platforms
// the window is always-on-top (kFloatingWindow) and top-level (not parented to
// BrowserView).
//
// Auto-closes on: active-tab change, parent-tab top-level navigation, or when
// another Boost editor opens for the same Browser (broadcasts OnEditorKilled
// first).
class MahoBoostWindowController : public views::WidgetObserver,
                                   public TabStripModelObserver,
                                   public content::WebContentsObserver {
 public:
  MahoBoostWindowController(Browser* browser, Profile* profile);
  MahoBoostWindowController(const MahoBoostWindowController&) = delete;
  MahoBoostWindowController& operator=(const MahoBoostWindowController&) =
      delete;
  ~MahoBoostWindowController() override;

  // Returns the controller for |browser|, creating it if necessary.
  static MahoBoostWindowController& GetForBrowser(Browser* browser,
                                                   Profile* profile);

  // Hides the active Boost widget across all browsers.  Used by the Mojo page
  // handler so it does not need to depend on views headers.
  static void HideActive();

  void ShowForActiveDomain(content::WebContents* active_tab);
  void Hide();
  bool IsShowing() const;

  // Resize the editor between boost mode (184×582) and code mode (452×582).
  // Called by the page handler when WebUI invokes RequestModeResize.
  void SetMode(maho_boost::mojom::WindowMode mode);

  // Register a callback invoked when a new editor replaces the current one
  // (OnEditorKilled).  The webui layer binds this to the page handler.
  void SetEditorKilledCallback(base::RepeatingClosure callback);

  // Returns the pending controller weak ptr for the most recently opened
  // Boost widget, then clears it (consume-once).  Called by MahoBoostUI
  // during construction.
  static base::WeakPtr<MahoBoostWindowController>
  ConsumePendingControllerForUI();

  base::WeakPtr<MahoBoostWindowController> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  views::Widget* GetWidgetForTesting() const { return widget_.get(); }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend content::WebContents* GetActiveTabForBoost(
      base::WeakPtr<MahoBoostWindowController> controller);

  void CreateAndShowWidget(const std::string& domain);
  void DestroyWidget();
  void OnDelegateWindowClosing();
  void BroadcastEditorKilledAndClose();
  void StartObservingTab(content::WebContents* active_tab);
  void StopObservingTab();

  raw_ptr<Browser> browser_;
  raw_ptr<Profile> profile_;
  std::unique_ptr<views::Widget> widget_;
  std::string current_domain_;
  raw_ptr<TabStripModel> observed_tab_strip_ = nullptr;

  // Current window mode; starts in kBoost.
  maho_boost::mojom::WindowMode current_mode_ =
      maho_boost::mojom::WindowMode::kBoost;

  base::RepeatingClosure editor_killed_callback_;

  base::WeakPtrFactory<MahoBoostWindowController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_H_
