// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_scroll_bar.h"

#include <algorithm>

#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/native_theme/overlay_scrollbar_constants.h"
#include "ui/views/layout/fill_layout.h"

namespace maho {

namespace {

constexpr int kPillWidthIdle = 6;
constexpr int kPillWidthHovered = 10;
constexpr int kPillTrailingMarginIdle = 3;
constexpr int kPillTrailingMarginHovered = 1;
constexpr int kPillAxisInset = 1;

}  // namespace

class MahoSidebarScrollBar::MahoThumb : public views::BaseScrollBarThumb {
 public:
  explicit MahoThumb(MahoSidebarScrollBar* scroll_bar)
      : views::BaseScrollBarThumb(scroll_bar), scroll_bar_(scroll_bar) {}
  MahoThumb(const MahoThumb&) = delete;
  MahoThumb& operator=(const MahoThumb&) = delete;
  ~MahoThumb() override = default;

  bool IsActive() const { return GetState() != views::Button::STATE_NORMAL; }

  // The thumb spans the whole interactive strip; only the painted pill is slim.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& /*available_size*/) const override {
    return gfx::Size(MahoSidebarScrollBar::kThickness,
                     MahoSidebarScrollBar::kThickness);
  }

 protected:
  void OnPaint(gfx::Canvas* canvas) override {
    const bool horizontal = IsHorizontal();
    const gfx::RectF pill = MahoSidebarScrollBar::PillBounds(
        GetLocalBounds(), horizontal, IsActive());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(IsActive() ? scroll_bar_->hovered_color_
                              : scroll_bar_->idle_color_);
    canvas->DrawRoundRect(
        pill, MahoSidebarScrollBar::PillCornerRadius(pill, horizontal), flags);
  }

  void OnStateChanged() override { SchedulePaint(); }

 private:
  raw_ptr<MahoSidebarScrollBar> scroll_bar_;
};

MahoSidebarScrollBar::MahoSidebarScrollBar(Orientation orientation)
    : views::ScrollBar(orientation) {
  SetNotifyEnterExitOnChild(true);
  SetPaintToLayer();
  layer()->SetMasksToBounds(true);
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.0f);

  // Layout only sets the thumb cross-axis position; ScrollBar::Update() sets
  // its length and position along the track.
  SetLayoutManager(std::make_unique<views::FillLayout>());
  auto* thumb = new MahoThumb(this);
  SetThumb(thumb);
  thumb_ = thumb;
}

MahoSidebarScrollBar::~MahoSidebarScrollBar() = default;

void MahoSidebarScrollBar::SetNeutralColor(SkColor neutral, bool opaque) {
  idle_color_ = opaque ? neutral : SkColorSetA(neutral, kIdleAlpha);
  hovered_color_ = opaque ? neutral : SkColorSetA(neutral, kHoveredAlpha);
  if (thumb_) {
    thumb_->SchedulePaint();
  }
}

// static
gfx::RectF MahoSidebarScrollBar::PillBounds(const gfx::Rect& local_bounds,
                                            bool horizontal,
                                            bool hovered) {
  const float width =
      hovered ? kPillWidthHovered : kPillWidthIdle;
  const float margin =
      hovered ? kPillTrailingMarginHovered : kPillTrailingMarginIdle;
  if (horizontal) {
    return gfx::RectF(local_bounds.x() + kPillAxisInset,
                      std::max(0.0f, local_bounds.bottom() - margin - width),
                      std::max(0, local_bounds.width() - 2 * kPillAxisInset),
                      width);
  }
  return gfx::RectF(std::max(0.0f, local_bounds.right() - margin - width),
                    local_bounds.y() + kPillAxisInset, width,
                    std::max(0, local_bounds.height() - 2 * kPillAxisInset));
}

// static
float MahoSidebarScrollBar::PillCornerRadius(const gfx::RectF& pill,
                                             bool horizontal) {
  return (horizontal ? pill.height() : pill.width()) / 2.0f;
}

gfx::RectF MahoSidebarScrollBar::PillBoundsInBarForTesting() const {
  if (!thumb_) {
    return gfx::RectF();
  }
  const bool horizontal = GetOrientation() == Orientation::kHorizontal;
  const gfx::RectF local =
      PillBounds(thumb_->GetLocalBounds(), horizontal, /*hovered=*/false);
  return gfx::RectF(local.x() + thumb_->x(), local.y() + thumb_->y(),
                    local.width(), local.height());
}

gfx::Rect MahoSidebarScrollBar::GetTrackBounds() const {
  return GetContentsBounds();
}

int MahoSidebarScrollBar::GetThickness() const {
  return kThickness;
}

bool MahoSidebarScrollBar::OverlapsContent() const {
  return true;
}

void MahoSidebarScrollBar::Update(int viewport_size,
                                  int content_size,
                                  int contents_scroll_offset) {
  const int previous_position = GetPosition();
  views::ScrollBar::Update(viewport_size, content_size, contents_scroll_offset);
  // Only a real scroll reveals the bar; relayouts and content-height changes
  // must not flash it.
  if (GetPosition() != previous_position) {
    Show();
    StartHideCountdown();
  }
}

void MahoSidebarScrollBar::ObserveScrollEvent(const ui::ScrollEvent& event) {
  Show();
  StartHideCountdown();
  views::ScrollBar::ObserveScrollEvent(event);
}

void MahoSidebarScrollBar::OnMouseEntered(const ui::MouseEvent& event) {
  is_hovered_ = true;
  Show();
}

void MahoSidebarScrollBar::OnMouseExited(const ui::MouseEvent& event) {
  is_hovered_ = false;
  StartHideCountdown();
}

void MahoSidebarScrollBar::Show() {
  layer()->SetOpacity(1.0f);
  hide_timer_.Stop();
}

void MahoSidebarScrollBar::Hide() {
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(ui::GetOverlayScrollbarFadeDuration());
  layer()->SetOpacity(0.0f);
}

void MahoSidebarScrollBar::StartHideCountdown() {
  if (is_hovered_ || thumb_->IsActive()) {
    return;
  }
  hide_timer_.Start(FROM_HERE, ui::GetOverlayScrollbarFadeDelay(),
                    base::BindOnce(&MahoSidebarScrollBar::Hide,
                                   base::Unretained(this)));
}

BEGIN_METADATA(MahoSidebarScrollBar)
END_METADATA

}  // namespace maho
