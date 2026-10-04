#include "maho/browser/ui/views/frame/maho_tab_controlled_banner_view.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace maho {
namespace {

constexpr int kBannerHeightDp = 34;
constexpr int kCornerRadiusDp = 17;
constexpr int kDotDiameterDp = 8;
constexpr base::TimeDelta kSlideDuration = base::Milliseconds(220);

}

BEGIN_METADATA(MahoTabControlledBannerView, StatusDotView)
END_METADATA

BEGIN_METADATA(MahoTabControlledBannerView)
END_METADATA

MahoTabControlledBannerView::StatusDotView::StatusDotView() {
  pulse_animation_.SetSlideDuration(base::Milliseconds(1200));
  pulse_animation_.SetTweenType(gfx::Tween::EASE_IN_OUT);
  GetViewAccessibility().SetIsIgnored(true);
}

MahoTabControlledBannerView::StatusDotView::~StatusDotView() = default;

void MahoTabControlledBannerView::StatusDotView::StartPulse() {
  if (gfx::Animation::ShouldRenderRichAnimation()) {
    pulse_animation_.Reset(0.0);
    pulse_animation_.Show();
  }
}

void MahoTabControlledBannerView::StatusDotView::StopPulse() {
  pulse_animation_.Reset(0.0);
  SchedulePaint();
}

void MahoTabControlledBannerView::StatusDotView::AnimationProgressed(
    const gfx::Animation* animation) {
  SchedulePaint();
}

void MahoTabControlledBannerView::StatusDotView::AnimationEnded(
    const gfx::Animation* animation) {
  if (gfx::Animation::ShouldRenderRichAnimation()) {
    pulse_animation_.Reset(0.0);
    pulse_animation_.Show();
  }
}

gfx::Size MahoTabControlledBannerView::StatusDotView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kDotDiameterDp + 8, kDotDiameterDp + 8);
}

void MahoTabControlledBannerView::StatusDotView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  const auto* color_provider = GetColorProvider();
  const SkColor dot_color = color_provider
                                ? color_provider->GetColor(ui::kColorSysPrimary)
                                : SkColorSetRGB(0x36, 0x6C, 0xF4);
  const gfx::PointF center(GetLocalBounds().CenterPoint());

  const double progress = pulse_animation_.is_animating()
                              ? pulse_animation_.GetCurrentValue()
                              : 0.0;
  const double pulse = 4.0 * progress * (1.0 - progress);
  const float radius = 3.5f + static_cast<float>(pulse * 1.5);
  const int alpha = std::clamp(static_cast<int>(180 + 75 * pulse), 0, 255);

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SkColorSetA(dot_color, alpha));
  canvas->DrawCircle(center, radius, flags);
}

MahoTabControlledBannerView::MahoTabControlledBannerView(StopCallback stop_callback)
    : stop_callback_(std::move(stop_callback)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);

  slide_animation_.SetSlideDuration(kSlideDuration);
  slide_animation_.SetTweenType(gfx::Tween::EASE_OUT);

  auto layout = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(4, 12), 8);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetLayoutManager(std::move(layout));

  auto dot = std::make_unique<StatusDotView>();
  status_dot_ = dot.get();
  AddChildView(std::move(dot));

  auto label = std::make_unique<views::Label>(
      u"This tab is controlled by the agent",
      views::style::CONTEXT_LABEL,
      views::style::STYLE_PRIMARY);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  // The banner widget is translucent and paints into its own layer, so the
  // subpixel opacity assumption in Label does not hold for this surface.
  label->SetSkipSubpixelRenderingOpacityCheck(true);
  label_ = AddChildView(std::move(label));

  auto stop_button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoTabControlledBannerView::OnStopPressed,
                          base::Unretained(this)),
      u"Stop");
  stop_button->SetStyle(ui::ButtonStyle::kProminent);
  stop_button->SetCornerRadius(10);
  stop_button_ = AddChildView(std::move(stop_button));
}

MahoTabControlledBannerView::~MahoTabControlledBannerView() = default;

void MahoTabControlledBannerView::SetControlledTarget(
    int64_t tab_id,
    const std::u16string& controller_name) {
  if (tab_id <= 0) {
    ClearControlledTarget();
    return;
  }

  active_tab_id_ = tab_id;
  controller_name_ = controller_name;

  if (label_) {
    if (!controller_name_.empty()) {
      label_->SetText(u"This tab is controlled by " + controller_name_);
    } else {
      label_->SetText(u"This tab is controlled by the agent");
    }
  }

  SetVisible(true);
  if (status_dot_) {
    status_dot_->StartPulse();
  }
  slide_animation_.Show();
}

void MahoTabControlledBannerView::ClearControlledTarget() {
  if (active_tab_id_ == 0) {
    return;
  }
  active_tab_id_ = 0;
  controller_name_.clear();
  if (status_dot_) {
    status_dot_->StopPulse();
  }
  slide_animation_.Hide();
}

void MahoTabControlledBannerView::OnStopPressed() {
  const int64_t target_id = active_tab_id_;
  ClearControlledTarget();
  if (stop_callback_ && target_id > 0) {
    stop_callback_.Run(target_id);
  }
}

void MahoTabControlledBannerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void MahoTabControlledBannerView::UpdateColors() {
  const auto* cp = GetColorProvider();
  if (!cp) return;

  if (label_) {
    label_->SetEnabledColor(cp->GetColor(ui::kColorSysOnSurface));
  }
  SchedulePaint();
}

gfx::Size MahoTabControlledBannerView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  size.set_height(kBannerHeightDp);
  return size;
}

void MahoTabControlledBannerView::OnPaint(gfx::Canvas* canvas) {
  const auto* cp = GetColorProvider();
  const SkColor bg_color = cp ? cp->GetColor(ui::kColorSysSurfaceVariant)
                              : SkColorSetARGB(240, 32, 32, 36);
  const SkColor border_color = cp ? cp->GetColor(ui::kColorSysOutline)
                                  : SkColorSetARGB(100, 255, 255, 255);

  gfx::RectF bounds(GetLocalBounds());
  bounds.Inset(0.5f);

  cc::PaintFlags bg_flags;
  bg_flags.setAntiAlias(true);
  bg_flags.setStyle(cc::PaintFlags::kFill_Style);
  bg_flags.setColor(bg_color);
  canvas->DrawRoundRect(bounds, kCornerRadiusDp, bg_flags);

  cc::PaintFlags border_flags;
  border_flags.setAntiAlias(true);
  border_flags.setStyle(cc::PaintFlags::kStroke_Style);
  border_flags.setStrokeWidth(1.0f);
  border_flags.setColor(border_color);
  canvas->DrawRoundRect(bounds, kCornerRadiusDp, border_flags);

  views::View::OnPaint(canvas);
}

void MahoTabControlledBannerView::AnimationProgressed(
    const gfx::Animation* animation) {
  SchedulePaint();
}

void MahoTabControlledBannerView::AnimationEnded(
    const gfx::Animation* animation) {
  if (slide_animation_.GetCurrentValue() == 0.0) {
    SetVisible(false);
  }
}

}
