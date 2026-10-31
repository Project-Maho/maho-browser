// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/auto_reset.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class NavigationHandle;
}

class Browser;

namespace views {
class EventMonitor;
class FocusManager;
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoCommandOverlayView;

class MahoCommandOverlayController : public views::WidgetObserver,
                                     public ui::EventObserver,
                                     public ui::AcceleratorTarget,
                                     public gfx::AnimationDelegate,
                                     public content::WebContentsObserver,
                                     public TabStripModelObserver,
                                     public views::FocusChangeListener {
 public:
  explicit MahoCommandOverlayController(Browser* browser);
  MahoCommandOverlayController(const MahoCommandOverlayController&) = delete;
  MahoCommandOverlayController& operator=(
      const MahoCommandOverlayController&) = delete;
  ~MahoCommandOverlayController() override;

  // A command controller/overlay may exist only for regular or exact-primary
  // Incognito normal windows. Guest/system/DevTools/other OTR get none.
  static bool ShouldCreateForContext(MahoPrivateContextClass klass);

  void Toggle(views::View* anchor,
              CommandOverlayMode mode = CommandOverlayMode::kSearch);
  void Show(views::View* anchor,
            CommandOverlayMode mode = CommandOverlayMode::kSearch,
            const std::string& initial_text = std::string(),
            bool select_initial_text = false);
  void Hide();
  bool IsVisible() const;

  MahoCommandOverlayView* GetOverlayViewForTesting() const {
    return overlay_view_;
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // views::FocusChangeListener:
  void OnWillChangeFocus(views::View* focused_before,
                         views::View* focused_now) override;
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;
  void OnFocusManagerDestroying(views::FocusManager* focus_manager) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void FinishHideAnimation();
  gfx::Rect ComputePopupBounds() const;
  gfx::Rect ComputeBoundsForHeight(int height) const;
  void Reposition();
  void AnimateBoundsToHeight(int height);

  void RestoreFocusToPreShowView();
  void FocusOverlayTextfield();

  // Dismisses the palette when native activation moves to another Chrome window
  // while keeping it open when another application takes focus. Reschedules
  // itself if the post-open grace window has not elapsed.
  void MaybeHideAfterDeactivation();

  // Drops the widget retained by FinishHideAnimation() once the hide has
  // unwound back to the message loop.
  void DestroyClosingWidget();

  // Outside-click dismissal: forwards Chrome-targeted mouse presses so a click
  // outside the palette (and its picker) closes it, without seeing other apps.
  void StartOutsideClickMonitor();

  // Escape-to-close while the browser (not the palette widget) holds focus.
  void RegisterParentEscAccelerator();
  void UnregisterParentEscAccelerator();

  friend class MahoCommandOverlayControllerTest;
  friend class MahoLocationBarCommandOverlayBehaviorTest;

  raw_ptr<Browser> browser_;
  std::unique_ptr<views::Widget> widget_;
  // Widget whose hide animation finished and which is awaiting destruction on a
  // follow-up task. Owned here so window teardown destroys it even when that
  // task never runs.
  std::unique_ptr<views::Widget> closing_widget_;
  raw_ptr<MahoCommandOverlayView> overlay_view_ = nullptr;
  raw_ptr<views::View> anchor_ = nullptr;
  raw_ptr<views::Widget> parent_widget_ = nullptr;
  gfx::Rect animation_start_bounds_;
  gfx::Rect animation_target_bounds_;
  gfx::SlideAnimation resize_animation_{this};
  base::OneShotTimer deactivation_hide_timer_;
  bool is_hiding_ = false;
  // Guards OnWidgetActivationChanged against the synchronous activation↔focus
  // re-entrancy that otherwise recurses until the stack overflows.
  bool handling_activation_change_ = false;
  // Unit tests have no active browser window; this stands in for
  // GetActiveBrowser() so deactivation-dismiss logic can be exercised.
  bool treat_chrome_window_active_for_testing_ = false;

  std::unique_ptr<views::EventMonitor> event_monitor_;
  raw_ptr<views::FocusManager> esc_target_focus_manager_ = nullptr;

  views::ViewTracker pre_show_focused_view_;

  // Latency instrumentation: records when Show() was called so we can log the
  // open-to-first-paint interval once the widget is visible and active.
  base::TimeTicks open_start_ticks_;

  base::WeakPtrFactory<MahoCommandOverlayController> weak_factory_{this};

  // Separate factory for the deferred `closing_widget_` drop: the widget
  // teardown path invalidates `weak_factory_`, which would otherwise cancel
  // that cleanup task.
  base::WeakPtrFactory<MahoCommandOverlayController> widget_teardown_weak_factory_{
      this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
