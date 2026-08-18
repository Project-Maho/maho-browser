// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/ui/browser_user_data.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost.mojom.h"

class Browser;
class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;
class TabStripModel;

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace views {
class Widget;
class WidgetDelegate;
}  // namespace views

namespace maho {

// Size constants for the two Boost editor modes.
inline constexpr int kBoostModeWidth = 184;
inline constexpr int kBoostModeHeight = 582;
inline constexpr int kCodeModeWidth = 452;
inline constexpr int kCodeModeHeight = 582;
inline constexpr int kCodeModeRightAlignOffsetPx = 265;

// Owns a non-modal top-level native views::Widget (TYPE_WINDOW) that hosts the
// Boost WebUI editor for a single browser instance.  Owned by BrowserUserData
// so the controller is automatically destroyed when its Browser is torn down —
// no leak path (fixes audit finding C4).
//
// On macOS the window is frameless (remove_standard_frame); custom drag regions
// are handled via -webkit-app-region:drag on the WebUI side.  On all platforms
// the window is always-on-top (kFloatingWindow) and top-level (not parented to
// BrowserView).
//
// Auto-closes on: active-tab change, parent-tab top-level navigation, or when
// another Boost editor opens for the same Browser (broadcasts OnEditorKilled
// first).
class MahoBoostWindowController
    : public BrowserUserData<MahoBoostWindowController>,
      public BrowserCollectionObserver,
      public TabStripModelObserver,
      public content::WebContentsObserver {
 public:
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

  void ShowForActiveDomain(content::WebContents* target_tab);
  void Hide();
  void CompleteHostClose();
  void FinalizeRendererClose();
  void SetTemporaryBoostId(std::optional<std::string> boost_id);
  void SetHostCloseState(std::optional<std::string> boost_id, bool dirty);
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
  static base::WeakPtr<MahoBoostWindowController>
  ConsumePendingControllerForUI(const std::string& controller_token);

  base::WeakPtr<MahoBoostWindowController> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // True while an editor widget exists, whether or not it is currently visible.
  bool HasWidget() const { return widget_ != nullptr; }

  views::Widget* GetWidgetForTesting() const { return widget_.get(); }

  // Returns the Boost WebUI contents without exposing the native widget tree to
  // interactive tests. The tree includes platform frame views, so positional
  // child traversal is not a stable test contract.
  content::WebContents* GetEditorWebContentsForTesting() const;
  content::WebContents* GetTargetWebContentsForTesting() const {
    return target_web_contents_.get();
  }
  bool IsHostClosePendingForTesting() const { return host_close_pending_; }
  void FireHostCloseWatchdogForTesting() { ForceHostClose(); }

  // BrowserCollectionObserver:
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

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
  friend class BrowserUserData<MahoBoostWindowController>;
  friend content::WebContents* GetTargetTabForBoost(
      base::WeakPtr<MahoBoostWindowController> controller);

  explicit MahoBoostWindowController(Browser* browser);

  void CreateAndShowWidget(const std::string& domain);
  void DestroyWidget();
  void RequestHostClose();
  void ForceHostClose();
  void OpenForTarget(content::WebContents* target_tab);
  bool ActivateTarget(content::WebContents* target_tab);
  void StartObservingTab(content::WebContents* target_tab);
  void StopObservingTab();
  void ScheduleDestroy();

  raw_ptr<Browser> browser_;
  raw_ptr<Profile> profile_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::string current_domain_;
  std::string controller_token_;
  base::WeakPtr<content::WebContents> target_web_contents_;
  base::WeakPtr<content::WebContents> pending_show_target_;
  bool host_close_pending_ = false;
  base::OneShotTimer host_close_watchdog_;
  std::optional<std::string> selected_boost_id_;
  std::optional<std::string> temporary_boost_id_;
  bool renderer_dirty_ = false;
  raw_ptr<TabStripModel> observed_tab_strip_ = nullptr;

  // Current window mode; starts in kBoost.
  maho_boost::mojom::WindowMode current_mode_ =
      maho_boost::mojom::WindowMode::kBoost;

  // True only when entering Code mode shifted the widget left. Returning to
  // Boost consumes this state to reverse that exact shift.
  bool code_mode_offset_applied_ = false;

  bool removal_posted_ = false;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  base::RepeatingClosure editor_killed_callback_;

  base::WeakPtrFactory<MahoBoostWindowController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_WINDOW_CONTROLLER_H_
