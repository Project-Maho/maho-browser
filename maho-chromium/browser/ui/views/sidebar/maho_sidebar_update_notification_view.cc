// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
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
constexpr int kGradientCycleMs = 1800;
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
// true, the button paints an animated linear-gradient background that sweeps
// continuously, mirroring Arc's "Restart and Update" CTA + Zen's
// `zen-text-gradient` keyframe applied to a button surface.
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
      gradient_delegate_ = std::make_unique<GradientDelegate>(this);
      animation_ = std::make_unique<gfx::ThrobAnimation>(gradient_delegate_.get());
      animation_->SetThrobDuration(base::Milliseconds(kGradientCycleMs));
      animation_->SetTweenType(gfx::Tween::LINEAR);
      animation_->StartThrobbing(/*cycles_til_stop=*/-1);
    }
  }
  ActionRowButton(const ActionRowButton &) = delete;
  ActionRowButton &operator=(const ActionRowButton &) = delete;
  ~ActionRowButton() override = default;

  // views::Button:
  void OnPaintBackground(gfx::Canvas *canvas) override {
    if (!special_ || !animation_) {
      views::Button::OnPaintBackground(canvas);
      return;
    }
    const auto *color_provider = GetColorProvider();
    if (!color_provider) {
      return;
    }
    const gfx::Rect bounds = GetLocalBounds();
    const float w = static_cast<float>(bounds.width());
    const float h = static_cast<float>(bounds.height());
    const float t = static_cast<float>(animation_->GetCurrentValue());

    const SkColor accent = color_provider->GetColor(ui::kColorSysPrimary);
    const gfx::RectF rect_f(bounds);

    // Step 1: paint the solid accent base behind everything so the button
    // reads as a single colored CTA (matching Arc's solid-fill button).
    cc::PaintFlags base_flags;
    base_flags.setAntiAlias(true);
    base_flags.setStyle(cc::PaintFlags::kFill_Style);
    base_flags.setColor(accent);
    canvas->DrawRoundRect(rect_f, kSpecialRowCornerRadius, base_flags);

    // Step 2: overlay a moving diagonal highlight that translates from
    // off-screen left to off-screen right over a single throb cycle. The
    // gradient stops are transparent on each side and bright in the middle
    // so a clear shine band sweeps across the solid base.
    const float band_offset = (t * 2.0f - 0.5f) * w * 2.0f;
    const SkColor4f highlight_colors[5] = {
        SkColor4f::FromColor(SkColorSetA(SK_ColorWHITE, 0)),
        SkColor4f::FromColor(SkColorSetA(SK_ColorWHITE, 0)),
        SkColor4f::FromColor(SkColorSetA(SK_ColorWHITE, 0xB0)),
        SkColor4f::FromColor(SkColorSetA(SK_ColorWHITE, 0)),
        SkColor4f::FromColor(SkColorSetA(SK_ColorWHITE, 0))};
    const SkScalar highlight_stops[5] = {0.0f, 0.42f, 0.5f, 0.58f, 1.0f};
    const SkPoint highlight_pts[2] = {
        SkPoint::Make(band_offset - w * 1.5f, -h),
        SkPoint::Make(band_offset + w * 1.5f, h * 2.0f)};

    cc::PaintFlags highlight_flags;
    highlight_flags.setAntiAlias(true);
    highlight_flags.setStyle(cc::PaintFlags::kFill_Style);
    highlight_flags.setShader(cc::PaintShader::MakeLinearGradient(
        highlight_pts, highlight_colors, highlight_stops, 5,
        SkTileMode::kClamp));
    canvas->DrawRoundRect(rect_f, kSpecialRowCornerRadius, highlight_flags);
  }

  bool is_special() const { return special_; }

private:
  // Inner AnimationDelegate so that the gradient animation does not collide
  // with views::Button's own AnimationDelegate inheritance (used by ink-drop
  // and state animations). The delegate forwards AnimationProgressed to the
  // owning button's SchedulePaint.
  class GradientDelegate : public gfx::AnimationDelegate {
  public:
    explicit GradientDelegate(ActionRowButton *button) : button_(button) {}
    void AnimationProgressed(const gfx::Animation * /*animation*/) override {
      button_->SchedulePaint();
    }

  private:
    raw_ptr<ActionRowButton> button_;
  };

  const bool special_;
  std::unique_ptr<GradientDelegate> gradient_delegate_;
  std::unique_ptr<gfx::ThrobAnimation> animation_;
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
  heading_->SetEnabledColor(ui::kColorSysOnSurface);
  heading_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  heading_->SetElideBehavior(gfx::ELIDE_TAIL);
  heading_->SetSubpixelRenderingEnabled(false);
  heading_->SetCanProcessEventsWithinSubtree(false);
  header_raw->SetHeading(heading_);

  divider_ = AddChildView(std::make_unique<views::View>());
  divider_->SetPreferredSize(gfx::Size(0, 1));
  divider_->SetBackground(views::CreateSolidBackground(ui::kColorSysDivider));
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
  const auto *color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }
  gfx::RectF rect(GetLocalBounds());
  rect.Inset(gfx::InsetsF::TLBR(2, 2, 2, 2));

  cc::PaintFlags shadow_flags;
  shadow_flags.setAntiAlias(true);
  shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
  shadow_flags.setColor(color_provider->GetColor(ui::kColorSysSurface1));
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
  stroke_flags.setColor(color_provider->GetColor(ui::kColorSysDivider));
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
  body_->RemoveAllChildViews();
  action_rows_.clear();
  checkbox_ = nullptr;

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
    label->SetEnabledColor(action.special ? SK_ColorWHITE
                                          : ui::kColorSysOnSurface);
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
