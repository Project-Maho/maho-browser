// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SCROLL_BAR_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SCROLL_BAR_H_

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/scrollbar/base_scroll_bar_thumb.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"

namespace ui {
class MouseEvent;
class ScrollEvent;
}  // namespace ui

namespace maho {

// Sidebar overlay scroll bar with the macOS pill look.
//
// Behaviour is views::OverlayScrollBar's (overlays the content, never reserves
// layout width, fades in on scroll and out when idle) so sidebar geometry stays
// deterministic regardless of the macOS "Show scroll bars" preference.
// Appearance differs: upstream paints the Fluent/ChromeOS skin (square thumb,
// hard 1 DIP outline, browser-theme colors), while this bar paints one fully
// rounded translucent pill tinted by the resolved MahoSidebarPalette. Sidebar
// surfaces must source colors from that palette, so the tint is injected via
// SetNeutralColor() rather than read from the ColorProvider.
class MahoSidebarScrollBar : public views::ScrollBar {
  METADATA_HEADER(MahoSidebarScrollBar, views::ScrollBar)

 public:
  static constexpr int kThickness = 12;
  static constexpr SkAlpha kIdleAlpha = 0x59;
  static constexpr SkAlpha kHoveredAlpha = 0x99;

  explicit MahoSidebarScrollBar(Orientation orientation);
  MahoSidebarScrollBar(const MahoSidebarScrollBar&) = delete;
  MahoSidebarScrollBar& operator=(const MahoSidebarScrollBar&) = delete;
  ~MahoSidebarScrollBar() override;

  // `opaque` serves forced-colors / high-contrast palettes, which must not rely
  // on translucency.
  void SetNeutralColor(SkColor neutral, bool opaque = false);

  static gfx::RectF PillBounds(const gfx::Rect& local_bounds,
                               bool horizontal,
                               bool hovered);
  static float PillCornerRadius(const gfx::RectF& pill, bool horizontal);

  // views::ScrollBar:
  gfx::Rect GetTrackBounds() const override;
  int GetThickness() const override;
  bool OverlapsContent() const override;
  void Update(int viewport_size,
              int content_size,
              int contents_scroll_offset) override;
  void ObserveScrollEvent(const ui::ScrollEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  SkColor idle_color_for_testing() const { return idle_color_; }
  SkColor hovered_color_for_testing() const { return hovered_color_; }
  bool IsHidePendingForTesting() const { return hide_timer_.IsRunning(); }
  gfx::RectF PillBoundsInBarForTesting() const;

 private:
  class MahoThumb;
  friend class MahoThumb;

  void Show();
  void Hide();
  void StartHideCountdown();

  raw_ptr<MahoThumb> thumb_ = nullptr;
  bool is_hovered_ = false;
  SkColor idle_color_ = SK_ColorTRANSPARENT;
  SkColor hovered_color_ = SK_ColorTRANSPARENT;
  base::OneShotTimer hide_timer_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SCROLL_BAR_H_
