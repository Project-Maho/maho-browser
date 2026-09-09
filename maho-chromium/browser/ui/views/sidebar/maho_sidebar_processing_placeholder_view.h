// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PROCESSING_PLACEHOLDER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PROCESSING_PLACEHOLDER_VIEW_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/view.h"

namespace gfx {
class Canvas;
}  // namespace gfx

namespace maho {

class MahoSidebarProcessingPlaceholderView : public views::View,
                                             public gfx::AnimationDelegate {
  METADATA_HEADER(MahoSidebarProcessingPlaceholderView, views::View)

 public:
  enum class Mode { kLoop, kSingleCycle };

  enum class Phase {
    kIdle,
    kMerging,
    kWorking,
    kResolvingSuccess,
    kResolvingFailure,
    kRestoring
  };

  struct TimingSpec {
    base::TimeDelta merge_duration;
    base::TimeDelta minimum_working_duration;
    base::TimeDelta working_loop_duration;
    base::TimeDelta success_resolve_duration;
    base::TimeDelta failure_resolve_duration;
    base::TimeDelta restore_duration;
    base::TimeDelta reduced_motion_clear_dwell;
    base::TimeDelta safety_timeout;
  };

  static TimingSpec GetTimingSpec();

  MahoSidebarProcessingPlaceholderView();
  MahoSidebarProcessingPlaceholderView(
      const MahoSidebarProcessingPlaceholderView&) = delete;
  MahoSidebarProcessingPlaceholderView& operator=(
      const MahoSidebarProcessingPlaceholderView&) = delete;
  ~MahoSidebarProcessingPlaceholderView() override;

  void Start(Mode mode, base::OnceClosure hidden_callback);
  void ResolveSuccess();
  void ResolveFailure();

  void SetButtonSurface(views::View* button_surface);
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // Test helpers:
  void set_disable_animation_for_testing(bool disable) {
    disable_animation_for_testing_ = disable;
  }
  void set_reduced_motion_for_testing(bool reduced) {
    reduced_motion_for_testing_ = reduced;
  }
  Phase phase_for_testing() const { return phase_; }
  gfx::LinearAnimation* phase_animation_for_testing() {
    return &phase_animation_;
  }
  bool is_active_for_testing() const { return active_; }
  void TriggerAnimationEndedForTesting() { AnimationEnded(&phase_animation_); }
  void TriggerSafetyTimeoutForTesting() { ForceResolveOnTimeout(); }
  void TriggerMinimumWorkingTimerForTesting();
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }

  // Static render model functions for geometry/state calculations at a given
  // phase/progress.
  static double ComputeDotX(int index,
                            double progress,
                            Phase phase,
                            double width,
                            double center_x);
  static double ComputeDotY(int index,
                            double progress,
                            Phase phase,
                            double center_y);
  static double ComputeDotRadius(int index, double progress, Phase phase);
  static double ComputeDotAlpha(int index, double progress, Phase phase);

  static double ComputeSparkX(double progress,
                              Phase phase,
                              double center_x,
                              double captured_spark_x);
  static double ComputeSparkY(double progress, Phase phase, double center_y);
  static double ComputeSparkRadius(double progress, Phase phase);
  static double ComputeSparkAlpha(double progress, Phase phase);

  static double ComputeCheckProgress(double progress, Phase phase);
  static double ComputeCheckAlpha(double progress, Phase phase);

 private:
  void BeginResolve(bool success);
  void FinishAndHide();
  void ForceResolveOnTimeout();
  void OnMinimumWorkingElapsed();
  bool ShouldRenderRich() const;
  void Announce(std::u16string name);
  void UpdateButtonOpacity();

  Phase phase_ = Phase::kIdle;
  Mode mode_ = Mode::kLoop;
  bool active_ = false;
  bool resolving_ = false;

  base::TimeTicks start_time_;
  std::optional<bool> latched_result_;
  double captured_spark_x_ = 0.0;

  gfx::LinearAnimation phase_animation_{this};
  base::OneShotTimer minimum_working_timer_;
  base::OneShotTimer safety_timer_;
  base::OneShotTimer reduced_motion_timer_;

  base::OnceClosure hidden_callback_;

  bool disable_animation_for_testing_ = false;
  bool reduced_motion_for_testing_ = false;

  raw_ptr<views::View> button_surface_ = nullptr;
  MahoSidebarPalette palette_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSidebarProcessingPlaceholderView> weak_factory_{
      this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PROCESSING_PLACEHOLDER_VIEW_H_
