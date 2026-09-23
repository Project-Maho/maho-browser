// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_OVERLAY_H_
#define MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_OVERLAY_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "maho/browser/ui/browser_user_data.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class BrowserWindowInterface;
class GlobalBrowserCollection;
class MahoToastView;

namespace views {
class Widget;
}

class MahoNotificationOverlay
    : public BrowserUserData<MahoNotificationOverlay>,
      public views::WidgetObserver,
      public gfx::AnimationDelegate,
      public BrowserCollectionObserver {
 public:
  explicit MahoNotificationOverlay(Browser* browser);
  ~MahoNotificationOverlay() override;
  MahoNotificationOverlay(const MahoNotificationOverlay&) = delete;
  MahoNotificationOverlay& operator=(const MahoNotificationOverlay&) = delete;

  // Show a toast for `duration`. If a toast is already visible, it is replaced.
  void Show(const std::u16string& title,
            const std::u16string& body,
            base::TimeDelta duration = base::Seconds(4));

  void Hide();
  bool IsVisible() const;

  // For tests.
  MahoToastView* toast_view_for_testing() const { return toast_view_; }
  views::Widget* widget_for_testing() const { return widget_.get(); }
  void set_context_for_testing(gfx::NativeWindow context) {
    context_for_testing_ = context;
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // BrowserCollectionObserver:
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

 private:
  void CreateWidget();
  void PrimeAnimationStartFrame();
  void StartShowAnimation();
  void StartHideAnimation();
  gfx::Rect ComputeTargetBounds() const;
  void OnDismissTimerFired();
  void PostRemovalIfNeeded();

  raw_ptr<Browser> browser_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoToastView> toast_view_ = nullptr;
  raw_ptr<views::Widget> parent_widget_observed_ = nullptr;
  bool removal_posted_ = false;

  gfx::SlideAnimation slide_animation_{this};
  base::OneShotTimer dismiss_timer_;
  bool is_hiding_ = false;

  gfx::NativeWindow context_for_testing_;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  base::WeakPtrFactory<MahoNotificationOverlay> weak_factory_{this};
};

namespace maho {
// Shows the shared upper-right "Link copied" toast (2s) for |browser| via the
// MahoNotificationOverlay; no-ops on null |browser| or a browser without a
// live BrowserView widget. Serves every clipboard-writing copy surface:
// Cmd+Shift+C / command-palette copy_url, and the favorites-menu Copy Link.
// Lives here (moved from maho_command_action_handler.cc) so all copy surfaces
// link one shared helper — clipboard-copy must confirm with a toast.
void ShowLinkCopiedToast(Browser* browser);
}  // namespace maho

#endif  // MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_OVERLAY_H_
