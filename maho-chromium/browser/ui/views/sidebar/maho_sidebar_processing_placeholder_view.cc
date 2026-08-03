// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "cc/paint/paint_flags.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace maho {

namespace {

constexpr double kPi = 3.14159265358979323846;

void DrawCheck(gfx::Canvas* canvas,
               double center_x,
               double center_y,
               double amount,
               double scale,
               SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(3.0f);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setColor(color);

  double first = std::min(1.0, amount * 2.0);
  double second = std::max(0.0, amount * 2.0 - 1.0);

  double ax = center_x - 7.0 * scale;
  double ay = center_y;
  double bx = center_x - 2.0 * scale;
  double by = center_y + 5.0 * scale;
  double cx = center_x + 9.0 * scale;
  double cy = center_y - 2.0 * scale;

  if (first > 0.0) {
    double x1 = ax;
    double y1 = ay;
    double x2 = ax + (bx - ax) * first;
    double y2 = ay + (by - ay) * first;
    canvas->DrawLine(gfx::PointF(x1, y1), gfx::PointF(x2, y2), flags);
  }
  if (second > 0.0) {
    double x1 = bx;
    double y1 = by;
    double x2 = bx + (cx - bx) * second;
    double y2 = by + (cy - by) * second;
    canvas->DrawLine(gfx::PointF(x1, y1), gfx::PointF(x2, y2), flags);
  }
}

}  // namespace

// static
MahoSidebarProcessingPlaceholderView::TimingSpec
MahoSidebarProcessingPlaceholderView::GetTimingSpec() {
  return TimingSpec{
      .merge_duration = base::Milliseconds(300),
      .minimum_working_duration = base::Milliseconds(200),
      .working_loop_duration = base::Milliseconds(2200),
      .success_resolve_duration = base::Milliseconds(300),
      .failure_resolve_duration = base::Milliseconds(300),
      .restore_duration = base::Milliseconds(200),
      .reduced_motion_clear_dwell = base::Milliseconds(180),
      .safety_timeout = base::Seconds(45),
  };
}

MahoSidebarProcessingPlaceholderView::MahoSidebarProcessingPlaceholderView() {
  SetVisible(false);
  SetPaintToLayer(ui::LAYER_TEXTURED);
  layer()->SetFillsBoundsOpaquely(false);
  SetFocusBehavior(FocusBehavior::NEVER);

  GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
  GetViewAccessibility().SetName(u"Organizing tabs");
}

MahoSidebarProcessingPlaceholderView::~MahoSidebarProcessingPlaceholderView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  phase_animation_.Stop();
  minimum_working_timer_.Stop();
  safety_timer_.Stop();
  reduced_motion_timer_.Stop();
}

void MahoSidebarProcessingPlaceholderView::SetButtonSurface(
    views::View* button_surface) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  button_surface_ = button_surface;
}

void MahoSidebarProcessingPlaceholderView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  palette_ = palette;
  SchedulePaint();
}

void MahoSidebarProcessingPlaceholderView::Start(
    Mode mode,
    base::OnceClosure hidden_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_) {
    return;
  }
  active_ = true;
  resolving_ = false;
  mode_ = mode;
  hidden_callback_ = std::move(hidden_callback);
  start_time_ = base::TimeTicks::Now();
  latched_result_ = std::nullopt;
  captured_spark_x_ = 0.0;

  SetVisible(true);
  Announce(u"Organizing tabs");

  safety_timer_.Start(
      FROM_HERE, GetTimingSpec().safety_timeout,
      base::BindOnce(
          &MahoSidebarProcessingPlaceholderView::ForceResolveOnTimeout,
          weak_factory_.GetWeakPtr()));

  if (!ShouldRenderRich()) {
    phase_ = Phase::kWorking;
    UpdateButtonOpacity();
    SchedulePaint();
    if (mode_ == Mode::kSingleCycle) {
      reduced_motion_timer_.Start(
          FROM_HERE, GetTimingSpec().reduced_motion_clear_dwell,
          base::BindOnce(&MahoSidebarProcessingPlaceholderView::FinishAndHide,
                         weak_factory_.GetWeakPtr()));
    }
    return;
  }

  if (disable_animation_for_testing_) {
    phase_ = Phase::kWorking;
    UpdateButtonOpacity();
    SchedulePaint();
    if (mode_ == Mode::kSingleCycle) {
      BeginResolve(/*success=*/true);
    }
    return;
  }

  phase_ = Phase::kMerging;
  phase_animation_.SetDuration(GetTimingSpec().merge_duration);
  phase_animation_.Start();
  UpdateButtonOpacity();
  SchedulePaint();
}

void MahoSidebarProcessingPlaceholderView::ResolveSuccess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (mode_ != Mode::kLoop) {
    return;
  }
  if (!ShouldRenderRich()) {
    BeginResolve(/*success=*/true);
    return;
  }
  const double elapsed_ms =
      (base::TimeTicks::Now() - start_time_).InMillisecondsF();
  if (elapsed_ms < 500.0) {
    latched_result_ = true;
    if (!minimum_working_timer_.IsRunning()) {
      minimum_working_timer_.Start(
          FROM_HERE, base::Milliseconds(500.0 - elapsed_ms),
          base::BindOnce(
              &MahoSidebarProcessingPlaceholderView::OnMinimumWorkingElapsed,
              weak_factory_.GetWeakPtr()));
    }
  } else {
    BeginResolve(/*success=*/true);
  }
}

void MahoSidebarProcessingPlaceholderView::ResolveFailure() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (mode_ != Mode::kLoop) {
    return;
  }
  if (!ShouldRenderRich()) {
    BeginResolve(/*success=*/false);
    return;
  }
  const double elapsed_ms =
      (base::TimeTicks::Now() - start_time_).InMillisecondsF();
  if (elapsed_ms < 500.0) {
    latched_result_ = false;
    if (!minimum_working_timer_.IsRunning()) {
      minimum_working_timer_.Start(
          FROM_HERE, base::Milliseconds(500.0 - elapsed_ms),
          base::BindOnce(
              &MahoSidebarProcessingPlaceholderView::OnMinimumWorkingElapsed,
              weak_factory_.GetWeakPtr()));
    }
  } else {
    BeginResolve(/*success=*/false);
  }
}

void MahoSidebarProcessingPlaceholderView::
    TriggerMinimumWorkingTimerForTesting() {
  OnMinimumWorkingElapsed();
}

void MahoSidebarProcessingPlaceholderView::OnMinimumWorkingElapsed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (latched_result_.has_value()) {
    BeginResolve(latched_result_.value());
  }
}

void MahoSidebarProcessingPlaceholderView::BeginResolve(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_ || resolving_) {
    return;
  }
  resolving_ = true;
  safety_timer_.Stop();
  minimum_working_timer_.Stop();
  reduced_motion_timer_.Stop();

  Announce(success ? u"Tabs organized" : u"Couldn't organize tabs");

  if (!ShouldRenderRich() || disable_animation_for_testing_) {
    FinishAndHide();
    return;
  }

  const double progress = phase_animation_.GetCurrentValue();
  const double width = static_cast<double>(GetLocalBounds().width());
  const double center_x = width / 2.0;
  captured_spark_x_ = ComputeSparkX(progress, phase_, center_x, 0.0);

  phase_animation_.Stop();
  phase_ = success ? Phase::kResolvingSuccess : Phase::kResolvingFailure;
  phase_animation_.SetDuration(success
                                   ? GetTimingSpec().success_resolve_duration
                                   : GetTimingSpec().failure_resolve_duration);
  phase_animation_.Start();
  UpdateButtonOpacity();
  SchedulePaint();
}

void MahoSidebarProcessingPlaceholderView::FinishAndHide() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  safety_timer_.Stop();
  minimum_working_timer_.Stop();
  reduced_motion_timer_.Stop();
  phase_animation_.Stop();

  phase_ = Phase::kIdle;
  SetVisible(false);
  active_ = false;
  resolving_ = false;

  UpdateButtonOpacity();

  base::OnceClosure cb = std::move(hidden_callback_);
  if (cb) {
    std::move(cb).Run();
  }
}

void MahoSidebarProcessingPlaceholderView::ForceResolveOnTimeout() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_ || resolving_) {
    return;
  }
  LOG(WARNING) << "[maho-placeholder] safety timeout fired; force-resolving";
  BeginResolve(/*success=*/false);
}

bool MahoSidebarProcessingPlaceholderView::ShouldRenderRich() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (reduced_motion_for_testing_) {
    return false;
  }
  return gfx::Animation::ShouldRenderRichAnimation();
}

void MahoSidebarProcessingPlaceholderView::Announce(std::u16string name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  GetViewAccessibility().SetName(std::move(name));
  GetViewAccessibility().NotifyEvent(ax::mojom::Event::kAlert,
                                     /*send_native_event=*/true);
}

void MahoSidebarProcessingPlaceholderView::UpdateButtonOpacity() {
  double button_alpha = 1.0;
  if (active_) {
    if (!ShouldRenderRich()) {
      button_alpha = 0.0;
    } else if (phase_ == Phase::kMerging) {
      double progress = phase_animation_.GetCurrentValue();
      double eased = progress * progress * (3.0 - 2.0 * progress);
      button_alpha = 1.0 - eased;
    } else if (phase_ == Phase::kRestoring) {
      double progress = phase_animation_.GetCurrentValue();
      double eased = progress * progress * (3.0 - 2.0 * progress);
      button_alpha = eased;
    } else if (phase_ == Phase::kWorking ||
               phase_ == Phase::kResolvingSuccess ||
               phase_ == Phase::kResolvingFailure) {
      button_alpha = 0.0;
    }
  }
  if (button_surface_ && button_surface_->layer()) {
    button_surface_->layer()->SetOpacity(static_cast<float>(button_alpha));
  }
}

void MahoSidebarProcessingPlaceholderView::AnimationProgressed(
    const gfx::Animation* animation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (animation == &phase_animation_) {
    UpdateButtonOpacity();
    SchedulePaint();
  }
}

void MahoSidebarProcessingPlaceholderView::AnimationEnded(
    const gfx::Animation* animation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (animation == &phase_animation_) {
    switch (phase_) {
      case Phase::kMerging:
        phase_ = Phase::kWorking;
        if (latched_result_.has_value()) {
          if (!minimum_working_timer_.IsRunning()) {
            BeginResolve(latched_result_.value());
          } else {
            phase_animation_.SetDuration(GetTimingSpec().working_loop_duration);
            phase_animation_.Start();
          }
        } else if (mode_ == Mode::kSingleCycle) {
          phase_animation_.SetDuration(
              GetTimingSpec().minimum_working_duration);
          phase_animation_.Start();
        } else {
          phase_animation_.SetDuration(GetTimingSpec().working_loop_duration);
          phase_animation_.Start();
        }
        break;
      case Phase::kWorking:
        if (mode_ == Mode::kSingleCycle) {
          BeginResolve(/*success=*/true);
        } else {
          phase_animation_.SetDuration(GetTimingSpec().working_loop_duration);
          phase_animation_.Start();
        }
        break;
      case Phase::kResolvingSuccess:
      case Phase::kResolvingFailure:
        phase_ = Phase::kRestoring;
        phase_animation_.SetDuration(GetTimingSpec().restore_duration);
        phase_animation_.Start();
        break;
      case Phase::kRestoring:
        FinishAndHide();
        return;
      default:
        break;
    }
    UpdateButtonOpacity();
    SchedulePaint();
  }
}

bool MahoSidebarProcessingPlaceholderView::OnMousePressed(
    const ui::MouseEvent& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return true;
}

bool MahoSidebarProcessingPlaceholderView::OnMouseDragged(
    const ui::MouseEvent& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return true;
}

void MahoSidebarProcessingPlaceholderView::OnGestureEvent(
    ui::GestureEvent* event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  event->SetHandled();
}

void MahoSidebarProcessingPlaceholderView::OnPaint(gfx::Canvas* canvas) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  views::View::OnPaint(canvas);
  if (phase_ == Phase::kIdle) {
    return;
  }
  const double width = static_cast<double>(GetLocalBounds().width());
  const double height = static_cast<double>(GetLocalBounds().height());
  const double center_x = width / 2.0;
  const double center_y = height / 2.0;
  const double progress = phase_animation_.GetCurrentValue();

  const SkColor normal_color = palette_.secondary_text;
  const SkColor accent_color = palette_.focus_ring;

  if (!ShouldRenderRich()) {
    // Renders static centered dots/spark
    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);
    dot_flags.setColor(normal_color);
    for (int i = 0; i < 3; ++i) {
      double x = center_x + (i - 1) * 15.84;
      canvas->DrawCircle(gfx::PointF(x, center_y), 3.6, dot_flags);
    }
    cc::PaintFlags spark_flags;
    spark_flags.setAntiAlias(true);
    spark_flags.setStyle(cc::PaintFlags::kFill_Style);
    spark_flags.setColor(accent_color);
    canvas->DrawCircle(gfx::PointF(center_x, center_y - 8.64), 2.16,
                       spark_flags);
    return;
  }

  // Draw three dots
  cc::PaintFlags dot_flags;
  dot_flags.setAntiAlias(true);
  dot_flags.setStyle(cc::PaintFlags::kFill_Style);
  for (int i = 0; i < 3; ++i) {
    double dot_alpha = ComputeDotAlpha(i, progress, phase_);
    if (dot_alpha > 0.0) {
      double x = ComputeDotX(i, progress, phase_, width, center_x);
      double y = ComputeDotY(i, progress, phase_, center_y);
      double r = ComputeDotRadius(i, progress, phase_);
      SkColor color = SkColorSetA(
          normal_color,
          static_cast<U8CPU>(SkColorGetA(normal_color) * dot_alpha + 0.5));
      dot_flags.setColor(color);
      canvas->DrawCircle(gfx::PointF(x, y), r, dot_flags);
    }
  }

  // Draw spark
  double spark_alpha = ComputeSparkAlpha(progress, phase_);
  if (spark_alpha > 0.0) {
    cc::PaintFlags spark_flags;
    spark_flags.setAntiAlias(true);
    spark_flags.setStyle(cc::PaintFlags::kFill_Style);
    double x = ComputeSparkX(progress, phase_, center_x, captured_spark_x_);
    double y = ComputeSparkY(progress, phase_, center_y);
    double r = ComputeSparkRadius(progress, phase_);
    SkColor color = SkColorSetA(
        accent_color,
        static_cast<U8CPU>(SkColorGetA(accent_color) * spark_alpha + 0.5));
    spark_flags.setColor(color);
    canvas->DrawCircle(gfx::PointF(x, y), r, spark_flags);
  }

  // Draw check
  double check_alpha = ComputeCheckAlpha(progress, phase_);
  if (check_alpha > 0.0) {
    double check_progress = ComputeCheckProgress(progress, phase_);
    SkColor color = SkColorSetA(
        accent_color,
        static_cast<U8CPU>(SkColorGetA(accent_color) * check_alpha + 0.5));
    DrawCheck(canvas, center_x, center_y, check_progress, 1.44, color);
  }
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeDotX(int index,
                                                         double progress,
                                                         Phase phase,
                                                         double width,
                                                         double center_x) {
  double origin = width * (2.0 * index + 1.0) / 6.0;
  double merge_target = center_x + (index - 1) * 15.84;
  if (phase == Phase::kMerging) {
    double eased = progress * progress * (3.0 - 2.0 * progress);
    return origin + (merge_target - origin) * eased;
  }
  if (phase == Phase::kIdle) {
    return origin;
  }
  return center_x + (index - 1) * 14.4;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeDotY(int index,
                                                         double progress,
                                                         Phase phase,
                                                         double center_y) {
  if (phase == Phase::kMerging) {
    return center_y + (index - 1) * 5.76 * sin(kPi * progress);
  }
  return center_y;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeDotRadius(int index,
                                                              double progress,
                                                              Phase phase) {
  double start_r = 4.32;
  double end_r = 3.6;
  if (phase == Phase::kMerging) {
    double eased = progress * progress * (3.0 - 2.0 * progress);
    return start_r + (end_r - start_r) * eased;
  }
  if (phase == Phase::kIdle) {
    return start_r;
  }
  return end_r;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeDotAlpha(int index,
                                                             double progress,
                                                             Phase phase) {
  if (phase == Phase::kMerging) {
    return progress * progress * (3.0 - 2.0 * progress);
  }
  if (phase == Phase::kWorking) {
    return 1.0;
  }
  if (phase == Phase::kResolvingSuccess || phase == Phase::kResolvingFailure) {
    return 1.0 - progress;
  }
  return 0.0;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeSparkX(
    double progress,
    Phase phase,
    double center_x,
    double captured_spark_x) {
  if (phase == Phase::kWorking) {
    return center_x - 14.4 + fmod(progress * 3.0, 1.0) * 28.8;
  }
  if (phase == Phase::kResolvingSuccess || phase == Phase::kResolvingFailure) {
    return captured_spark_x;
  }
  return center_x;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeSparkY(double progress,
                                                           Phase phase,
                                                           double center_y) {
  return center_y - 8.64;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeSparkRadius(double progress,
                                                                Phase phase) {
  return 2.16;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeSparkAlpha(double progress,
                                                               Phase phase) {
  if (phase == Phase::kWorking) {
    return 1.0;
  }
  if (phase == Phase::kResolvingSuccess || phase == Phase::kResolvingFailure) {
    return 1.0 - progress;
  }
  return 0.0;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeCheckProgress(
    double progress,
    Phase phase) {
  if (phase == Phase::kResolvingSuccess) {
    return progress;
  }
  if (phase == Phase::kRestoring) {
    return 1.0;
  }
  return 0.0;
}

// static
double MahoSidebarProcessingPlaceholderView::ComputeCheckAlpha(double progress,
                                                               Phase phase) {
  if (phase == Phase::kResolvingSuccess) {
    return 1.0;
  }
  if (phase == Phase::kRestoring) {
    double eased = progress * progress * (3.0 - 2.0 * progress);
    return 1.0 - eased;
  }
  return 0.0;
}

BEGIN_METADATA(MahoSidebarProcessingPlaceholderView)
END_METADATA

}  // namespace maho
