// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_space_dot_view.h"

#include <cmath>

#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"

namespace maho {

BEGIN_METADATA(MahoSidebarSpaceDotView)
END_METADATA

MahoSidebarSpaceDotView::MahoSidebarSpaceDotView() {
  SetPreferredSize(gfx::Size(kViewSize, kViewSize));
  GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  icon_label_ = AddChildView(std::make_unique<views::Label>());
  icon_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  icon_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  icon_label_->SetAutoColorReadabilityEnabled(false);
  icon_label_->SetVisible(true);
  icon_label_->SetBoundsRect(gfx::Rect(
      (kViewSize - kIndicatorSize) / 2, (kViewSize - kIndicatorSize) / 2,
      kIndicatorSize, kIndicatorSize));
  icon_label_->SetPaintToLayer();
  icon_label_->layer()->SetFillsBoundsOpaquely(false);
  icon_label_->layer()->SetOpacity(0.0f);

  constexpr int kBadgeSize = 4;
  constexpr int kBadgeOffset = kViewSize / 2 + kIndicatorSize / 2 - 5;
  auto badge = std::make_unique<views::View>();
  badge->SetBounds(kBadgeOffset, kBadgeOffset, kBadgeSize, kBadgeSize);
  badge->SetBackground(
      views::CreateRoundedRectBackground(SK_ColorTRANSPARENT, 2));
  badge->SetBorder(views::CreateRoundedRectBorder(1, 2, SK_ColorTRANSPARENT));
  badge->SetVisible(false);
  profile_badge_ = AddChildView(std::move(badge));
}

MahoSidebarSpaceDotView::~MahoSidebarSpaceDotView() = default;

void MahoSidebarSpaceDotView::Configure(const std::string& space_id,
                                         const std::string& name,
                                         const std::string& icon,
                                         SkColor color,
                                         bool is_active) {
  space_id_ = space_id;
  name_ = name;
  icon_ = icon;
  dot_color_ = color;
  is_active_ = is_active;
  has_icon_ = !icon.empty();

  SetTooltipText(base::UTF8ToUTF16(name));
  SetAccessibleName(base::UTF8ToUTF16(name));

  if (has_icon_) {
    icon_label_->SetText(base::UTF8ToUTF16(icon));
  } else {
    icon_label_->SetText(u"");
  }

  UpdateAppearance();
}

void MahoSidebarSpaceDotView::SetActive(bool active) {
  if (is_active_ == active) {
    return;
  }
  is_active_ = active;
  UpdateAppearance();
}

void MahoSidebarSpaceDotView::SetSelected(bool selected) {
  if (is_selected_ == selected) {
    return;
  }
  is_selected_ = selected;
  UpdateAppearance();
}

gfx::Size MahoSidebarSpaceDotView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kViewSize, kViewSize);
}

void MahoSidebarSpaceDotView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);

  if (dot_opacity_ <= 0.0f) {
    return;
  }

  const float dot_size = GetCurrentDotSize();
  const float radius = dot_size / 2.0f;
  const gfx::PointF center(kViewSize / 2.0f, kViewSize / 2.0f);

  const auto* cp = GetColorProvider();
  const SkColor fill = is_active_
                           ? SkColorSetRGB(0xE8, 0xEF, 0xFB)
                           : (cp ? cp->GetColor(ui::kColorSysOnSurfaceSubtle)
                                 : SkColorSetRGB(0xA2, 0xA8, 0xB6));
  const int alpha = is_active_ ? static_cast<int>(dot_opacity_ * 212)
                               : static_cast<int>(dot_opacity_ * 72);

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SkColorSetA(fill, alpha));
  canvas->DrawCircle(center, radius, flags);

  if (is_active_) {
    cc::PaintFlags halo_flags;
    halo_flags.setAntiAlias(true);
    halo_flags.setStyle(cc::PaintFlags::kStroke_Style);
    halo_flags.setStrokeWidth(1.0f);
    halo_flags.setColor(SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF));
    canvas->DrawCircle(center, radius + 0.8f, halo_flags);
  }
}

void MahoSidebarSpaceDotView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  is_hovered_ = true;
  UpdateAppearance();
}

void MahoSidebarSpaceDotView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  is_hovered_ = false;
  UpdateAppearance();
}

bool MahoSidebarSpaceDotView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsRightMouseButton()) {
    if (delegate_) {
      gfx::Point screen_point = event.location();
      views::View::ConvertPointToScreen(this, &screen_point);
      delegate_->OnSpaceDotContextMenu(this, screen_point);
    }
    return true;
  }

  drag_start_point_ = event.location();
  did_start_drag_ = false;
  return true;
}

bool MahoSidebarSpaceDotView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!did_start_drag_) {
    const gfx::Vector2d delta = event.location() - drag_start_point_;
    const float distance = std::sqrt(static_cast<float>(
        delta.x() * delta.x() + delta.y() * delta.y()));

    if (distance > kDragStartThreshold) {
      did_start_drag_ = true;
      if (delegate_) {
        delegate_->OnSpaceDotDragStarted(this, drag_start_point_);
      }
    }
  }

  if (did_start_drag_ && delegate_) {
    gfx::Point screen_point = event.location();
    views::View::ConvertPointToScreen(this, &screen_point);
    delegate_->OnSpaceDotDragMoved(this, screen_point);
  }
  return true;
}

void MahoSidebarSpaceDotView::OnMouseReleased(const ui::MouseEvent& event) {
  if (did_start_drag_) {
    did_start_drag_ = false;
    if (delegate_) {
      delegate_->OnSpaceDotDragEnded(this, /*cancelled=*/false);
    }
    return;
  }

  if (event.IsLeftMouseButton() && HitTestPoint(event.location())) {
    if (delegate_) {
      delegate_->OnSpaceDotActivated(this);
    }
  }
  did_start_drag_ = false;
}

void MahoSidebarSpaceDotView::OnMouseCaptureLost() {
  if (did_start_drag_) {
    did_start_drag_ = false;
    if (delegate_) {
      delegate_->OnSpaceDotDragEnded(this, /*cancelled=*/true);
    }
  }
}

float MahoSidebarSpaceDotView::GetCurrentDotSize() const {
  if (is_active_) {
    return kDotSizeActive;
  }
  if (is_selected_) {
    return kDotSizeSelected;
  }
  if (is_hovered_ && !has_icon_) {
    return kDotSizeHoveredNoIcon;
  }
  return kDotSizeDefault;
}

void MahoSidebarSpaceDotView::UpdateAppearance() {
  const bool show_icon = is_hovered_ && has_icon_;
  const float target_icon_opacity = show_icon ? 1.0f : 0.0f;

  dot_opacity_ = show_icon ? 0.0f : 1.0f;

  views::AnimationBuilder()
      .Once()
      .SetDuration(base::Milliseconds(160))
      .SetOpacity(icon_label_->layer(), target_icon_opacity,
                  gfx::Tween::EASE_IN_OUT);

  SchedulePaint();
}

void MahoSidebarSpaceDotView::ConfigureProfileBadge(SkColor color,
                                                     bool visible) {
  profile_badge_->SetBackground(
      views::CreateRoundedRectBackground(color, 2));
  profile_badge_->SetVisible(visible);
}

}  // namespace maho
