// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/timer/timer.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

namespace maho {

namespace {

constexpr int kCardCornerRadius = 10;
constexpr int kHeaderPadding = 8;
constexpr int kHeaderMinHeight = 28;
constexpr int kBodyPadding = 6;
constexpr int kBodySpacing = 2;
constexpr int kRowPadding = 2;
constexpr int kSpecialRowPadding = 6;
constexpr int kSpecialRowCornerRadius = 8;
constexpr int kRowSpacing = 6;
constexpr int kCheckboxBottomMargin = 4;
constexpr int kShadowBlurRadius = 4;
constexpr SkAlpha kShadowAlpha = 0x20;
constexpr int kAnimationDurationMs = 200;
constexpr int kAnimationSlideOffsetDip = 12;
constexpr int kExpandAnimationMs = 200;
constexpr int kPillVerticalPadding = 6;

// Header view that places `heading_` centered across the full width while the
// close button overlays the top-right corner. Mirrors Zen's
// `.zen-sidebar-notification-header` layout (heading absolute-centered, close
// absolute top-right).
class HeaderView : public views::View {
  METADATA_HEADER(HeaderView, views::View)

public:
  HeaderView() = default;
  HeaderView(const HeaderView &) = delete;
  HeaderView &operator=(const HeaderView &) = delete;
  ~HeaderView() override = default;

  void SetHeading(views::Label *heading) { heading_ = heading; }

  // views::View:
  void Layout(PassKey) override {
    if (heading_) {
      const int inner_x = kHeaderPadding;
      const int inner_w = std::max(0, width() - kHeaderPadding * 2);
      const gfx::Size pref =
          heading_->GetPreferredSize(views::SizeBounds(inner_w, {}));
      const int h = pref.height();
      const int y = std::max(0, (height() - h) / 2);
      heading_->SetBoundsRect(gfx::Rect(inner_x, y, inner_w, h));
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds &available) const override {
    int h = kHeaderMinHeight;
    if (heading_) {
      const gfx::Size pref = heading_->GetPreferredSize(available);
      h = std::max(h, pref.height() + kHeaderPadding * 2);
    }
    return gfx::Size(available.width().is_bounded() ? available.width().value()
                                                    : 0,
                     h);
  }

private:
  raw_ptr<views::Label> heading_ = nullptr;
};

BEGIN_METADATA(HeaderView)
END_METADATA

// ActionRowButton renders a horizontal icon + label row. When `special` is
// true, the button paints an animated 3-stop linear-gradient at 135 degrees
// whose hue rotates continuously through the full 360-degree wheel, mirroring
// the CSS `filter: hue-rotate` keyframe used in the design HTML reference.
// Hue rotation 0deg <-> 360deg is identical, so the loop is seamless.
class ActionRowButton : public views::Button {
  METADATA_HEADER(ActionRowButton, views::Button)

public:
  ActionRowButton(PressedCallback callback, bool special)
      : views::Button(std::move(callback)), special_(special) {
    auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets(special_ ? kSpecialRowPadding : kRowPadding), kRowSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    if (special_) {
      layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kCenter);
      animation_timer_.Start(
          FROM_HERE, base::Milliseconds(kFrameMs),
          base::BindRepeating(&ActionRowButton::OnAnimationTick,
                              base::Unretained(this)));
    }
  }
  ActionRowButton(const ActionRowButton &) = delete;
  ActionRowButton &operator=(const ActionRowButton &) = delete;
  ~ActionRowButton() override = default;

  // views::Button:
  void OnPaintBackground(gfx::Canvas *canvas) override {
    if (!special_) {
      views::Button::OnPaintBackground(canvas);
      return;
    }
    const gfx::Rect bounds = GetLocalBounds();
    const float w = static_cast<float>(bounds.width());
    const float h = static_cast<float>(bounds.height());

    // 3 base stops define the rainbow at phase=0; per-frame each one is
    // hue-rotated by `phase_` turns so the whole gradient pans around the
    // hue wheel without changing saturation or lightness.
    constexpr SkColor kBaseColors[3] = {
        SkColorSetRGB(0xff, 0x5e, 0x8a),
        SkColorSetRGB(0x7c, 0x5c, 0xff),
        SkColorSetRGB(0x5c, 0xe0, 0xff)};
    const SkColor4f colors[3] = {
        SkColor4f::FromColor(RotateHue(kBaseColors[0], phase_)),
        SkColor4f::FromColor(RotateHue(kBaseColors[1], phase_)),
        SkColor4f::FromColor(RotateHue(kBaseColors[2], phase_))};
    const SkScalar stops[3] = {0.0f, 0.5f, 1.0f};
    const SkPoint pts[2] = {SkPoint::Make(0.0f, 0.0f), SkPoint::Make(w, h)};

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setShader(cc::PaintShader::MakeLinearGradient(
        pts, colors, stops, 3, SkTileMode::kClamp));
    canvas->DrawRoundRect(gfx::RectF(bounds), kSpecialRowCornerRadius, flags);
  }

  bool is_special() const { return special_; }

private:
  static SkColor RotateHue(SkColor color, double turns) {
    color_utils::HSL hsl;
    color_utils::SkColorToHSL(color, &hsl);
    hsl.h = std::fmod(hsl.h + turns, 1.0);
    if (hsl.h < 0.0) {
      hsl.h += 1.0;
    }
    return color_utils::HSLToSkColor(hsl, SkColorGetA(color));
  }

  void OnAnimationTick() {
    phase_ += kPhaseIncrement;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    SchedulePaint();
  }

  static constexpr int kFrameMs = 16;
  static constexpr float kCycleMs = 5000.0f;
  static constexpr float kPhaseIncrement =
      static_cast<float>(kFrameMs) / kCycleMs;

  const bool special_;
  base::RepeatingTimer animation_timer_;
  float phase_ = 0.0f;
};

BEGIN_METADATA(ActionRowButton)
END_METADATA

} // namespace

MahoSidebarUpdateNotificationView::MahoSidebarUpdateNotificationView() {
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetNotifyEnterExitOnChild(true);
  GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
  GetViewAccessibility().SetName(u"Maho update notification");

  auto *root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(0, 0, 0, 0),
      0));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto header_owned = std::make_unique<HeaderView>();
  HeaderView *header_raw = header_owned.get();
  header_ = AddChildView(std::move(header_owned));

  heading_ = header_->AddChildView(std::make_unique<views::Label>());
  heading_->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
  heading_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  heading_->SetElideBehavior(gfx::ELIDE_TAIL);
  heading_->SetSubpixelRenderingEnabled(false);
  heading_->SetAutoColorReadabilityEnabled(false);
  heading_->SetCanProcessEventsWithinSubtree(false);
  header_raw->SetHeading(heading_);

  divider_ = AddChildView(std::make_unique<views::View>());
  divider_->SetPreferredSize(gfx::Size(0, 1));
  divider_->SetPaintToLayer();
  divider_->layer()->SetFillsBoundsOpaquely(false);

  body_ = AddChildView(std::make_unique<views::View>());
  auto *body_layout =
      body_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(kBodyPadding),
          kBodySpacing));
  body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  body_->SetPaintToLayer();
  body_->layer()->SetFillsBoundsOpaquely(false);

  expand_animation_.SetSlideDuration(base::Milliseconds(kExpandAnimationMs));
  expand_animation_.SetTweenType(gfx::Tween::EASE_OUT);
  UpdateExpandedState();
}

MahoSidebarUpdateNotificationView::~MahoSidebarUpdateNotificationView() =
    default;

void MahoSidebarUpdateNotificationView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (heading_) {
    heading_->SetEnabledColor(palette_.primary_text);
  }
  if (divider_) {
    divider_->SetBackground(views::CreateSolidBackground(palette_.outline));
  }
  if (checkbox_) {
    checkbox_->SetEnabledTextColors(palette_.primary_text);
    checkbox_->SetTextColor(views::Button::STATE_DISABLED,
                            palette_.disabled_text);
    checkbox_->SetCheckedIconImageColor(palette_.focus_ring);
  }
  for (const auto& action_row : action_rows_) {
    if (!action_row) {
      continue;
    }
    const auto* action_button = views::AsViewClass<ActionRowButton>(
        action_row.get());
    const SkColor label_color = action_button && action_button->is_special()
                                    ? SK_ColorWHITE
                                    : palette_.primary_text;
    for (views::View* child : action_row->children()) {
      if (auto* label = views::AsViewClass<views::Label>(child)) {
        label->SetEnabledColor(label_color);
      }
    }
  }
  SchedulePaint();
}

void MahoSidebarUpdateNotificationView::Update(
    const MahoSidebarUpdateNotificationModel &model) {
  on_dismiss_ = model.on_dismiss;

  if (heading_) {
    heading_->SetText(model.heading);
  }

  RebuildBody(model);

  if (model.visible != last_visible_) {
    AnimateVisibilityChange(model.visible);
    last_visible_ = model.visible;
  } else {
    SetVisible(model.visible);
  }
}

views::Button *
MahoSidebarUpdateNotificationView::action_row_for_testing(size_t index) {
  if (index >= action_rows_.size()) {
    return nullptr;
  }
  return action_rows_[index].get();
}

void MahoSidebarUpdateNotificationView::OnPaintBackground(gfx::Canvas *canvas) {
  if (!GetVisible()) {
    return;
  }
  gfx::RectF rect(GetLocalBounds());
  rect.Inset(gfx::InsetsF::TLBR(2, 2, 2, 2));

  cc::PaintFlags shadow_flags;
  shadow_flags.setAntiAlias(true);
  shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
  shadow_flags.setColor(palette_.normal_contrast_surfaces.empty()
                            ? palette_.row_selected
                            : palette_.normal_contrast_surfaces.front());
  cc::DrawLooperBuilder looper;
  looper.AddShadow(
      /*offset=*/{0, 2},
      /*blur_sigma=*/kShadowBlurRadius / 2.0f,
      SkColor4f::FromColor(SkColorSetA(SK_ColorBLACK, kShadowAlpha)),
      /*flags=*/0u);
  shadow_flags.setLooper(looper.Detach());
  canvas->DrawRoundRect(rect, kCardCornerRadius, shadow_flags);

  cc::PaintFlags stroke_flags;
  stroke_flags.setAntiAlias(true);
  stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
  stroke_flags.setStrokeWidth(1);
  stroke_flags.setColor(palette_.outline);
  canvas->DrawRoundRect(rect, kCardCornerRadius, stroke_flags);
}

gfx::Size MahoSidebarUpdateNotificationView::CalculatePreferredSize(
    const views::SizeBounds &available_size) const {
  if (!GetVisible() && !last_visible_) {
    return gfx::Size();
  }
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : views::View::CalculatePreferredSize(available_size)
                              .width();
  const int collapsed = CollapsedHeight();
  const int expanded = ExpandedHeight(available_size);
  const double t = expand_animation_.GetCurrentValue();
  const int height =
      static_cast<int>(collapsed + (expanded - collapsed) * t + 0.5);
  return gfx::Size(width, height);
}

void MahoSidebarUpdateNotificationView::OnMouseEntered(
    const ui::MouseEvent & /*event*/) {
  if (!last_visible_) {
    return;
  }
  expand_animation_.Show();
}

void MahoSidebarUpdateNotificationView::OnMouseExited(
    const ui::MouseEvent & /*event*/) {
  expand_animation_.Hide();
}

bool MahoSidebarUpdateNotificationView::OnMousePressed(
    const ui::MouseEvent & /*event*/) {
  if (!last_visible_) {
    return false;
  }
  if (expand_animation_.IsShowing()) {
    expand_animation_.Hide();
  } else {
    expand_animation_.Show();
  }
  return true;
}

void MahoSidebarUpdateNotificationView::AnimationProgressed(
    const gfx::Animation *animation) {
  UpdateExpandedState();
}

void MahoSidebarUpdateNotificationView::UpdateExpandedState() {
  const float t =
      static_cast<float>(expand_animation_.GetCurrentValue());
  const bool interactive = t > 0.5f;
  if (divider_ && divider_->layer()) {
    divider_->layer()->SetOpacity(t);
  }
  if (body_) {
    if (body_->layer()) {
      body_->layer()->SetOpacity(t);
    }
    body_->SetCanProcessEventsWithinSubtree(interactive);
  }
  PreferredSizeChanged();
  SchedulePaint();
}

int MahoSidebarUpdateNotificationView::CollapsedHeight() const {
  if (!heading_) {
    return 0;
  }
  const int text_h =
      heading_->GetPreferredSize(views::SizeBounds()).height();
  return text_h + kPillVerticalPadding * 2;
}

int MahoSidebarUpdateNotificationView::ExpandedHeight(
    const views::SizeBounds &available) const {
  int h = 0;
  if (header_) {
    h += header_->GetPreferredSize(available).height();
  }
  if (divider_) {
    h += divider_->GetPreferredSize(available).height();
  }
  if (body_) {
    h += body_->GetPreferredSize(available).height();
  }
  return std::max(h, CollapsedHeight());
}

void MahoSidebarUpdateNotificationView::RebuildBody(
    const MahoSidebarUpdateNotificationModel &model) {
  action_rows_.clear();
  checkbox_ = nullptr;
  body_->RemoveAllChildViews();

  if (model.checkbox.has_value()) {
    const auto &cb = *model.checkbox;
    auto checkbox_owned = std::make_unique<views::Checkbox>(cb.label);
    checkbox_owned->SetChecked(cb.checked);
    // The Checkbox's internal Label inherits subpixel rendering enabled, but
    // our parent view has a translucent layer (SetFillsBoundsOpaquely(false))
    // which trips DCHECK in views::Label::PaintText. Disable subpixel
    // rendering on every descendant Label.
    for (views::View *child : checkbox_owned->children()) {
      if (auto *lbl = views::AsViewClass<views::Label>(child)) {
        lbl->SetSubpixelRenderingEnabled(false);
      }
    }
    if (!cb.accessible_label.empty()) {
      checkbox_owned->SetAccessibleName(cb.accessible_label);
    }
    checkbox_owned->SetProperty(
        views::kMarginsKey,
        gfx::Insets::TLBR(0, 0, kCheckboxBottomMargin, 0));
    checkbox_ = body_->AddChildView(std::move(checkbox_owned));
    checkbox_->SetEnabledTextColors(palette_.primary_text);
    checkbox_->SetTextColor(views::Button::STATE_DISABLED,
                            palette_.disabled_text);
    checkbox_->SetCheckedIconImageColor(palette_.focus_ring);
    if (cb.on_toggle) {
      base::RepeatingCallback<void(bool)> on_toggle = cb.on_toggle;
      views::Checkbox *cb_raw = checkbox_;
      checkbox_->SetCallback(base::BindRepeating(
          [](views::Checkbox *self,
             base::RepeatingCallback<void(bool)> handler) {
            handler.Run(self->GetChecked());
          },
          base::Unretained(cb_raw), std::move(on_toggle)));
    }
  }

  for (const auto &action : model.actions) {
    base::RepeatingClosure on_activate = action.on_activate;
    auto *button = body_->AddChildView(std::make_unique<ActionRowButton>(
        on_activate ? on_activate : base::DoNothing(), action.special));
    button->SetAccessibleName(action.accessible_label.empty()
                                  ? action.label
                                  : action.accessible_label);
    button->SetTooltipText(action.label);

    if (!action.icon.IsEmpty()) {
      auto *icon =
          button->AddChildView(std::make_unique<views::ImageView>(action.icon));
      icon->SetCanProcessEventsWithinSubtree(false);
    }
    auto *label =
        button->AddChildView(std::make_unique<views::Label>(action.label));
    label->SetTextStyle(views::style::STYLE_BODY_4);
    label->SetEnabledColor(action.special ? SK_ColorWHITE : palette_.primary_text);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetHorizontalAlignment(action.special ? gfx::ALIGN_CENTER
                                                 : gfx::ALIGN_LEFT);
    label->SetSubpixelRenderingEnabled(false);
    label->SetCanProcessEventsWithinSubtree(false);

    action_rows_.push_back(button);
  }
  body_->InvalidateLayout();
}

void MahoSidebarUpdateNotificationView::AnimateVisibilityChange(
    bool target_visible) {
  if (!layer()) {
    SetVisible(target_visible);
    return;
  }
  if (target_visible) {
    SetVisible(true);
    layer()->SetOpacity(0.0f);
    gfx::Transform start_transform;
    start_transform.Translate(0, kAnimationSlideOffsetDip);
    layer()->SetTransform(start_transform);
    views::AnimationBuilder()
        .Once()
        .SetDuration(base::Milliseconds(kAnimationDurationMs))
        .SetOpacity(layer(), 1.0f, gfx::Tween::EASE_OUT)
        .SetTransform(layer(), gfx::Transform(), gfx::Tween::EASE_OUT);
    return;
  }
  base::WeakPtr<MahoSidebarUpdateNotificationView> weak =
      weak_factory_.GetWeakPtr();
  gfx::Transform end_transform;
  end_transform.Translate(0, kAnimationSlideOffsetDip / 2);
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<MahoSidebarUpdateNotificationView> self) {
            if (self) {
              self->SetVisible(false);
            }
          },
          weak))
      .Once()
      .SetDuration(base::Milliseconds(kAnimationDurationMs * 3 / 4))
      .SetOpacity(layer(), 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(layer(), end_transform, gfx::Tween::EASE_IN);
}

BEGIN_METADATA(MahoSidebarUpdateNotificationView)
END_METADATA

} // namespace maho
