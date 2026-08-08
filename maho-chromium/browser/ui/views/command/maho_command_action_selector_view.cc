// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/animation/animation_delegate_views.h"

namespace maho {

namespace {

constexpr int kSelectorCornerRadiusDp = 8;
constexpr int kSegmentCornerRadiusDp = 6;
constexpr int kSegmentWidthDp = 64;
constexpr int kSegmentHeightDp = 26;
constexpr base::TimeDelta kFlowDuration = base::Seconds(3);
constexpr int kFlowFrameRate = 30;
constexpr double kTwoPi = 6.28318530717958647692;

int g_running_flow_animation_count = 0;

SkColor BlendRgb(SkColor first, SkColor second) {
  return SkColorSetRGB(
      (SkColorGetR(first) + SkColorGetR(second)) / 2,
      (SkColorGetG(first) + SkColorGetG(second)) / 2,
      (SkColorGetB(first) + SkColorGetB(second)) / 2);
}

SkColor BrightenRgb(SkColor color, int amount) {
  return SkColorSetRGB(
      std::min(255, static_cast<int>(SkColorGetR(color)) + amount),
      std::min(255, static_cast<int>(SkColorGetG(color)) + amount),
      std::min(255, static_cast<int>(SkColorGetB(color)) + amount));
}

class SegmentFlowBackground : public views::Background,
                              public gfx::LinearAnimation,
                              public views::AnimationDelegateViews {
 public:
  SegmentFlowBackground(views::View* target,
                        SkColor base_color,
                        SkColor accent_color)
      : gfx::LinearAnimation(kFlowDuration, kFlowFrameRate, this),
        views::AnimationDelegateViews(target),
        target_(target),
        base_color_(base_color),
        accent_color_(accent_color) {}

  SegmentFlowBackground(const SegmentFlowBackground&) = delete;
  SegmentFlowBackground& operator=(const SegmentFlowBackground&) = delete;

  ~SegmentFlowBackground() override {
    StopFlow();
    set_delegate(nullptr);
  }

  void StartFlow() {
    if (flow_running_) {
      return;
    }
    flow_running_ = true;
    ++g_running_flow_animation_count;
    Start();
  }

  void StopFlow() {
    if (!flow_running_) {
      return;
    }
    flow_running_ = false;
    if (is_animating()) {
      Stop();
    }
    --g_running_flow_animation_count;
  }

  bool flow_running() const { return flow_running_ && is_animating(); }
  SkColor accent_color() const { return accent_color_; }

  void SetColors(SkColor base_color, SkColor accent_color) {
    base_color_ = base_color;
    accent_color_ = accent_color;
    target_->SchedulePaint();
  }

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const double phase = GetCurrentValue();
    const double angle = phase * kTwoPi;
    const float primary_x =
        0.18f + static_cast<float>(0.16 * (1.0 - std::cos(angle)));
    const float primary_y =
        0.45f + static_cast<float>(0.18 * std::sin(angle));
    const float secondary_x =
        0.82f - static_cast<float>(0.14 * (1.0 - std::cos(angle)));
    const float secondary_y =
        0.55f - static_cast<float>(0.16 * std::sin(angle));
    const gfx::RectF rounded_bounds(bounds);

    cc::PaintFlags fill_flags;
    fill_flags.setAntiAlias(true);
    fill_flags.setDither(true);
    fill_flags.setColor(base_color_);
    canvas->DrawRoundRect(rounded_bounds, kSegmentCornerRadiusDp, fill_flags);

    const auto draw_lobe = [&](float x, float y, float radius_scale,
                               SkColor color, SkAlpha alpha) {
      const SkColor4f colors[] = {
          SkColor4f::FromColor(SkColorSetA(color, alpha)),
          SkColor4f::FromColor(SkColorSetA(color, alpha / 3)),
          SkColor4f::FromColor(SK_ColorTRANSPARENT),
      };
      const SkScalar stops[] = {0.0f, 0.5f, 1.0f};
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setDither(true);
      flags.setShader(cc::PaintShader::MakeRadialGradient(
          SkPoint::Make(bounds.x() + bounds.width() * x,
                        bounds.y() + bounds.height() * y),
          std::max(bounds.width(), bounds.height()) * radius_scale, colors,
          stops, std::size(colors), SkTileMode::kClamp));
      canvas->DrawRoundRect(rounded_bounds, kSegmentCornerRadiusDp, flags);
    };

    draw_lobe(primary_x, primary_y, 0.75f, accent_color_, 0x82);
    draw_lobe(secondary_x, secondary_y, 0.68f,
              BlendRgb(base_color_, accent_color_), 0x62);

    const float sheen_position =
        static_cast<float>(0.5 * (1.0 - std::cos(angle)));
    const float sheen_x = bounds.x() + bounds.width() * sheen_position;
    const SkColor sheen = BrightenRgb(
        BlendRgb(base_color_, accent_color_), 72);
    const SkColor4f sheen_colors[] = {
        SkColor4f::FromColor(SK_ColorTRANSPARENT),
        SkColor4f::FromColor(SkColorSetA(sheen, 0x0A)),
        SkColor4f::FromColor(SkColorSetA(sheen, 0x38)),
        SkColor4f::FromColor(SkColorSetA(sheen, 0x0A)),
        SkColor4f::FromColor(SK_ColorTRANSPARENT),
    };
    const SkScalar sheen_stops[] = {0.0f, 0.28f, 0.5f, 0.72f, 1.0f};
    const SkPoint sheen_points[] = {
        SkPoint::Make(sheen_x - bounds.width() * 0.32f, bounds.bottom()),
        SkPoint::Make(sheen_x + bounds.width() * 0.32f, bounds.y()),
    };
    cc::PaintFlags sheen_flags;
    sheen_flags.setAntiAlias(true);
    sheen_flags.setDither(true);
    sheen_flags.setShader(cc::PaintShader::MakeLinearGradient(
        sheen_points, sheen_colors, sheen_stops, std::size(sheen_colors),
        SkTileMode::kClamp));
    canvas->DrawRoundRect(rounded_bounds, kSegmentCornerRadiusDp, sheen_flags);
  }

  std::optional<gfx::RoundedCornersF> GetRoundedCornerRadii() const override {
    return gfx::RoundedCornersF(kSegmentCornerRadiusDp);
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    target_->SchedulePaint();
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    // Test environments can globally reduce animation durations to zero. Do
    // not recursively restart a zero-duration animation; production rich
    // animation remains continuous.
    if (flow_running_ && gfx::Animation::ShouldRenderRichAnimation()) {
      Start();
    }
  }

 private:
  const raw_ptr<views::View> target_;
  SkColor base_color_;
  SkColor accent_color_;
  bool flow_running_ = false;
};

}  // namespace

class SegmentButton : public views::LabelButton {
 public:
  METADATA_HEADER(SegmentButton, views::LabelButton)
 public:
  SegmentButton(PressedCallback callback, const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetElideBehavior(gfx::NO_ELIDE);
    SetPreferredSize(gfx::Size(kSegmentWidthDp, kSegmentHeightDp));
    SetMinSize(gfx::Size(kSegmentWidthDp, kSegmentHeightDp));
    SetFocusRingCornerRadius(kSegmentCornerRadiusDp);
    label()->SetSubpixelRenderingEnabled(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
  }

  ~SegmentButton() override {
    if (flow_background_) {
      flow_background_->StopFlow();
    }
  }

  void SetSelectedVisuals(bool selected,
                          bool animation_enabled,
                          SkColor accent_color) {
    selected_ = selected;
    if (selected) {
      if (const auto* cp = GetColorProvider()) {
        const SkColor base_color =
            cp->GetColor(kMahoColorCommandBarInputFill);
        if (!flow_background_) {
          auto background = std::make_unique<SegmentFlowBackground>(
              this, base_color, accent_color);
          flow_background_ = background.get();
          SetBackground(std::move(background));
        } else {
          flow_background_->SetColors(base_color, accent_color);
        }
        if (animation_enabled) {
          flow_background_->StartFlow();
        } else {
          flow_background_->StopFlow();
        }
        SetBorder(views::CreateRoundedRectBorder(
            1, kSegmentCornerRadiusDp,
            cp->GetColor(kMahoColorCommandBarKeycapBorder)));
        SetEnabledTextColors(cp->GetColor(kMahoColorPrimaryText));
      }
      GetViewAccessibility().SetIsSelected(true);
    } else {
      if (flow_background_) {
        flow_background_->StopFlow();
        flow_background_ = nullptr;
      }
      SetBackground(nullptr);
      SetBorder(views::CreateSolidBorder(1, SkColorSetA(accent_color, 0x00)));
      if (GetColorProvider()) {
        SetEnabledTextColors(
            GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary));
      }
      GetViewAccessibility().SetIsSelected(false);
    }
  }

  bool HasFlowBackground() const {
    return flow_background_ && GetBackground() == flow_background_;
  }
  bool IsFlowAnimating() const {
    return flow_background_ && flow_background_->flow_running();
  }
  void StopFlow() {
    if (flow_background_) {
      flow_background_->StopFlow();
    }
  }
  void StartFlow() {
    if (selected_ && flow_background_) {
      flow_background_->StartFlow();
    }
  }
  SkColor FlowAccent() const {
    return flow_background_ ? flow_background_->accent_color()
                            : SK_ColorTRANSPARENT;
  }
  SkColor EnabledTextColor() const { return label()->GetEnabledColor(); }
  bool UsesSubpixelRendering() const {
    return label()->GetSubpixelRenderingEnabled();
  }

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    if (!selected_ && GetColorProvider()) {
      SetEnabledTextColors(
          GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary));
    }
  }

 private:
  raw_ptr<SegmentFlowBackground> flow_background_ = nullptr;
  bool selected_ = false;
};

MahoCommandActionSelectorView::MahoCommandActionSelectorView(
    base::RepeatingCallback<void(PaletteAction)> on_change)
    : on_change_(std::move(on_change)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
  GetViewAccessibility().SetName(u"Command Palette Action Selector");
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarShortcutBadge, kSelectorCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(
      1, kSelectorCornerRadiusDp, kMahoColorCommandBarKeycapBorder));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(3, 3), 2));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_button_ = AddChildView(std::make_unique<SegmentButton>(
      base::BindRepeating(&MahoCommandActionSelectorView::OnSegmentClicked,
                          base::Unretained(this), PaletteAction::kSearch),
      u"Search"));
  search_button_->GetViewAccessibility().SetName(u"Search Mode");

  ask_maho_button_ = AddChildView(std::make_unique<SegmentButton>(
      base::BindRepeating(&MahoCommandActionSelectorView::OnSegmentClicked,
                          base::Unretained(this), PaletteAction::kAskMaho),
      u"Ask Maho"));
  ask_maho_button_->GetViewAccessibility().SetName(u"Ask Maho Mode");

  hint_label_ = AddChildView(std::make_unique<views::Label>(
      u"Tab to switch", views::style::CONTEXT_LABEL,
      views::style::STYLE_SECONDARY));
  hint_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  hint_label_->SetSubpixelRenderingEnabled(false);
  hint_label_->SetVisible(false);

  UpdateVisuals();
}

MahoCommandActionSelectorView::~MahoCommandActionSelectorView() {
  StopFlowAnimations();
  widget_observation_.Reset();
}

bool MahoCommandActionSelectorView::IsFlowBackgroundInstalledForTesting(
    PaletteAction action) const {
  const auto* button = static_cast<const SegmentButton*>(
      action == PaletteAction::kSearch ? search_button_.get()
                                       : ask_maho_button_.get());
  return button->HasFlowBackground();
}

bool MahoCommandActionSelectorView::IsFlowAnimatingForTesting(
    PaletteAction action) const {
  const auto* button = static_cast<const SegmentButton*>(
      action == PaletteAction::kSearch ? search_button_.get()
                                       : ask_maho_button_.get());
  return button->IsFlowAnimating();
}

SkColor MahoCommandActionSelectorView::FlowAccentForTesting(
    PaletteAction action) const {
  const auto* button = static_cast<const SegmentButton*>(
      action == PaletteAction::kSearch ? search_button_.get()
                                       : ask_maho_button_.get());
  return button->FlowAccent();
}

SkColor MahoCommandActionSelectorView::SegmentTextColorForTesting(
    PaletteAction action) const {
  const auto* button = static_cast<const SegmentButton*>(
      action == PaletteAction::kSearch ? search_button_.get()
                                       : ask_maho_button_.get());
  return button->EnabledTextColor();
}

bool MahoCommandActionSelectorView::SegmentUsesSubpixelRenderingForTesting(
    PaletteAction action) const {
  const auto* button = static_cast<const SegmentButton*>(
      action == PaletteAction::kSearch ? search_button_.get()
                                       : ask_maho_button_.get());
  return button->UsesSubpixelRendering();
}

// static
int MahoCommandActionSelectorView::GetRunningFlowAnimationCountForTesting() {
  return g_running_flow_animation_count;
}

// static
int MahoCommandActionSelectorView::GetSegmentCornerRadiusForTesting() {
  return kSegmentCornerRadiusDp;
}

void MahoCommandActionSelectorView::SetSelected(PaletteAction action) {
  if (selected_ == action) {
    return;
  }
  selected_ = action;
  UpdateVisuals();
  GetViewAccessibility().NotifyEvent(ax::mojom::Event::kSelectedChildrenChanged, true);
  on_change_.Run(selected_);
}

void MahoCommandActionSelectorView::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }
  enabled_ = enabled;
  search_button_->SetEnabled(enabled);
  ask_maho_button_->SetEnabled(enabled);
  UpdateVisuals();
}

void MahoCommandActionSelectorView::AddedToWidget() {
  views::View::AddedToWidget();
  views::Widget* widget = GetWidget();
  CHECK(widget);
  if (widget_observation_.IsObserving()) {
    widget_observation_.Reset();
  }
  widget_observation_.Observe(widget);
  UpdateFlowAnimationState();
}

void MahoCommandActionSelectorView::RemovedFromWidget() {
  StopFlowAnimations();
  widget_observation_.Reset();
  views::View::RemovedFromWidget();
}

void MahoCommandActionSelectorView::VisibilityChanged(
    views::View* starting_from,
    bool is_visible) {
  views::View::VisibilityChanged(starting_from, is_visible);
  UpdateFlowAnimationState();
}

void MahoCommandActionSelectorView::OnWidgetVisibilityChanged(
    views::Widget* widget,
    bool visible) {
  CHECK_EQ(widget_observation_.GetSource(), widget);
  UpdateFlowAnimationState();
}

void MahoCommandActionSelectorView::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  CHECK_EQ(widget_observation_.GetSource(), widget);
  UpdateFlowAnimationState();
}

void MahoCommandActionSelectorView::OnWidgetDestroyed(
    views::Widget* widget) {
  CHECK_EQ(widget_observation_.GetSource(), widget);
  StopFlowAnimations();
  widget_observation_.Reset();
}

bool MahoCommandActionSelectorView::ShouldAnimateFlow() const {
  const views::Widget* widget = GetWidget();
  return enabled_ && widget && widget_observation_.IsObserving() &&
         widget->IsVisible() && IsDrawn();
}

void MahoCommandActionSelectorView::StopFlowAnimations() {
  static_cast<SegmentButton*>(search_button_)->StopFlow();
  static_cast<SegmentButton*>(ask_maho_button_)->StopFlow();
}

void MahoCommandActionSelectorView::UpdateFlowAnimationState() {
  if (!ShouldAnimateFlow()) {
    StopFlowAnimations();
    return;
  }

  SegmentButton* selected_button = static_cast<SegmentButton*>(
      selected_ == PaletteAction::kSearch ? search_button_.get()
                                          : ask_maho_button_.get());
  selected_button->StartFlow();
}

void MahoCommandActionSelectorView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateVisuals();
}

void MahoCommandActionSelectorView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);
}

void MahoCommandActionSelectorView::OnSegmentClicked(PaletteAction action) {
  if (!enabled_) {
    return;
  }
  SetSelected(action);
}

void MahoCommandActionSelectorView::UpdateVisuals() {
  if (!GetColorProvider()) {
    return;
  }

  SkColor accent_color;
  if (!enabled_) {
    accent_color = GetColorProvider()->GetColor(ui::kColorLabelForegroundDisabled);
  } else {
    accent_color = GetColorProvider()->GetColor(
        selected_ == PaletteAction::kAskMaho ? kMahoColorAccentBlueBright
                                             : kMahoColorAccentBlue);
  }

  const bool animation_enabled = ShouldAnimateFlow();
  static_cast<SegmentButton*>(search_button_)
      ->SetSelectedVisuals(selected_ == PaletteAction::kSearch,
                           animation_enabled, accent_color);
  static_cast<SegmentButton*>(ask_maho_button_)
      ->SetSelectedVisuals(selected_ == PaletteAction::kAskMaho,
                           animation_enabled, accent_color);

  // If disabled, apply disabled colors to the label as well
  if (!enabled_) {
    hint_label_->SetEnabledColor(GetColorProvider()->GetColor(ui::kColorLabelForegroundDisabled));
  } else {
    hint_label_->SetEnabledColor(GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary));
  }
}

BEGIN_METADATA(SegmentButton)
END_METADATA

BEGIN_METADATA(MahoCommandActionSelectorView)
END_METADATA

}  // namespace maho
