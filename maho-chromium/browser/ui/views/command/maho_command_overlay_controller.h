// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoCommandOverlayView;

class MahoCommandOverlayController : public views::WidgetObserver,
                                     public gfx::AnimationDelegate {
 public:
  explicit MahoCommandOverlayController(Browser* browser);
  MahoCommandOverlayController(const MahoCommandOverlayController&) = delete;
  MahoCommandOverlayController& operator=(
      const MahoCommandOverlayController&) = delete;
  ~MahoCommandOverlayController() override;

  void Toggle(views::View* anchor,
              CommandOverlayMode mode = CommandOverlayMode::kSearch);
  void Show(views::View* anchor,
            CommandOverlayMode mode = CommandOverlayMode::kSearch,
            const std::string& initial_text = std::string(),
            bool select_initial_text = false);
  void Hide();
  bool IsVisible() const;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void FinishHideAnimation();
  void ScheduleFocusRetentionPasses();
  gfx::Rect ComputePopupBounds() const;
  gfx::Rect ComputeBoundsForHeight(int height) const;
  void Reposition();
  void AnimateBoundsToHeight(int height);

  raw_ptr<Browser> browser_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoCommandOverlayView> overlay_view_ = nullptr;
  raw_ptr<views::View> anchor_ = nullptr;
  raw_ptr<views::Widget> parent_widget_ = nullptr;
  gfx::Rect animation_start_bounds_;
  gfx::Rect animation_target_bounds_;
  gfx::SlideAnimation resize_animation_{this};
  base::OneShotTimer deactivation_hide_timer_;
  int remaining_focus_retention_passes_ = 0;
  bool is_hiding_ = false;

  base::WeakPtrFactory<MahoCommandOverlayController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_CONTROLLER_H_
