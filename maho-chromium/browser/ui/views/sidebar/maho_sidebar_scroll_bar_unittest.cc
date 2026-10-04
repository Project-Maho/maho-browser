// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_scroll_bar.h"

#include <optional>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/native_theme/overlay_scrollbar_constants.h"
#include "ui/views/paint_info.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

constexpr SkColor kNeutral = SkColorSetRGB(0x8A, 0x8F, 0x96);

int AlphaOf(SkColor color) {
  return static_cast<int>(SkColorGetA(color));
}

// views::test::PaintViewToBitmap() clears with an opaque placeholder color, so
// the pill's translucency has to be captured on a transparent canvas instead.
SkBitmap PaintBarToTransparentBitmap(views::View* view) {
  SkBitmap bitmap;
  const gfx::Size size = view->size();
  ui::CanvasPainter painter(&bitmap, size, 1.f, SK_ColorTRANSPARENT,
                            /*is_pixel_canvas=*/false);
  view->Paint(views::PaintInfo::CreateRootPaintInfo(painter.context(), size));
  return bitmap;
}

// The bar is hosted inside a real widget, matching how the sidebar uses it.
class MahoSidebarScrollBarTest : public views::ViewsTestBase {
 public:
  MahoSidebarScrollBarTest()
      : views::ViewsTestBase(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params =
        CreateParams(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                     views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.bounds = gfx::Rect(0, 0, 240, 600);
    if (display::Screen* screen = display::Screen::Get()) {
      // Keep the widget clear of the real pointer: a hovered thumb suppresses
      // the idle-hide countdown, which is orthogonal to what is being tested.
      const gfx::Point cursor = screen->GetCursorScreenPoint();
      params.bounds.Offset(400, 400);
      params.bounds.set_x(params.bounds.x() + cursor.x());
      params.bounds.set_y(params.bounds.y() + cursor.y());
    }
    widget_->Init(std::move(params));

    auto* host = widget_->SetContentsView(std::make_unique<views::View>());
    auto bar = std::make_unique<MahoSidebarScrollBar>(
        views::ScrollBar::Orientation::kVertical);
    bar_ = host->AddChildView(std::move(bar));
    bar_->SetBounds(0, 0, MahoSidebarScrollBar::kThickness, 120);
    widget_->Show();
  }

  void TearDown() override {
    bar_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  MahoSidebarScrollBar* bar() { return bar_; }

 private:
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoSidebarScrollBar> bar_ = nullptr;
};

TEST_F(MahoSidebarScrollBarTest, OverlayContractKeepsSidebarLayoutStable) {
  EXPECT_EQ(std::string(bar()->GetClassName()), "MahoSidebarScrollBar");
  EXPECT_TRUE(bar()->OverlapsContent());
  EXPECT_EQ(bar()->GetThickness(), MahoSidebarScrollBar::kThickness);
}

TEST_F(MahoSidebarScrollBarTest, PaletteNeutralDrivesTranslucentAlphaRamp) {
  bar()->SetNeutralColor(kNeutral);
  EXPECT_EQ(AlphaOf(bar()->idle_color_for_testing()),
            static_cast<int>(MahoSidebarScrollBar::kIdleAlpha));
  EXPECT_EQ(AlphaOf(bar()->hovered_color_for_testing()),
            static_cast<int>(MahoSidebarScrollBar::kHoveredAlpha));
  EXPECT_EQ(SkColorGetR(bar()->idle_color_for_testing()), SkColorGetR(kNeutral));
  EXPECT_EQ(SkColorGetB(bar()->hovered_color_for_testing()),
            SkColorGetB(kNeutral));

  bar()->SetNeutralColor(kNeutral, /*opaque=*/true);
  EXPECT_EQ(bar()->idle_color_for_testing(), kNeutral);
  EXPECT_EQ(bar()->hovered_color_for_testing(), kNeutral);
}

TEST_F(MahoSidebarScrollBarTest, PillIsSlimFullyRoundedAndInsideTheStrip) {
  const gfx::Rect thumb(0, 0, MahoSidebarScrollBar::kThickness, 200);

  const gfx::RectF idle =
      MahoSidebarScrollBar::PillBounds(thumb, /*horizontal=*/false,
                                       /*hovered=*/false);
  const gfx::RectF hovered =
      MahoSidebarScrollBar::PillBounds(thumb, /*horizontal=*/false,
                                       /*hovered=*/true);

  EXPECT_GT(idle.width(), 0.0f);
  EXPECT_LT(idle.width(), static_cast<float>(thumb.width()));
  EXPECT_GT(hovered.width(), idle.width());
  EXPECT_LE(hovered.right(), static_cast<float>(thumb.right()));
  EXPECT_GE(hovered.x(), 0.0f);
  EXPECT_GT(idle.height(), 0.0f);
  EXPECT_LT(idle.height(), static_cast<float>(thumb.height()));

  EXPECT_FLOAT_EQ(MahoSidebarScrollBar::PillCornerRadius(idle, false),
                  idle.width() / 2.0f);
  EXPECT_FLOAT_EQ(MahoSidebarScrollBar::PillCornerRadius(hovered, false),
                  hovered.width() / 2.0f);

  const gfx::RectF horizontal =
      MahoSidebarScrollBar::PillBounds(gfx::Rect(0, 0, 200, 12),
                                       /*horizontal=*/true, /*hovered=*/false);
  EXPECT_FLOAT_EQ(horizontal.height(), idle.width());
  EXPECT_FLOAT_EQ(MahoSidebarScrollBar::PillCornerRadius(horizontal, true),
                  horizontal.height() / 2.0f);
}

TEST_F(MahoSidebarScrollBarTest, RevealsOnScrollAndHidesAgainWhenIdle) {
  ASSERT_NE(bar()->layer(), nullptr);
  EXPECT_EQ(bar()->layer()->GetTargetOpacity(), 0.0f);

  bar()->Update(120, 600, 0);
  EXPECT_EQ(bar()->layer()->GetTargetOpacity(), 0.0f)
      << "relayout must not flash the bar";

  bar()->Update(120, 600, 120);
  EXPECT_EQ(bar()->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_TRUE(bar()->IsHidePendingForTesting());

  task_environment()->FastForwardBy(ui::GetOverlayScrollbarFadeDelay() +
                                    base::Milliseconds(1));
  EXPECT_FALSE(bar()->IsHidePendingForTesting());
  EXPECT_EQ(bar()->layer()->GetTargetOpacity(), 0.0f)
      << "the bar must fade back out once scrolling stops";
}

TEST_F(MahoSidebarScrollBarTest, PaintsRoundedTranslucentPillInPaletteColor) {
  bar()->Update(120, 600, 0);
  bar()->SetNeutralColor(kNeutral);

  SkBitmap bitmap = PaintBarToTransparentBitmap(bar());
  ASSERT_FALSE(bitmap.isNull());
  ASSERT_EQ(bitmap.width(), MahoSidebarScrollBar::kThickness);

  const gfx::RectF pill = bar()->PillBoundsInBarForTesting();
  ASSERT_GT(pill.width(), 0.0f);

  const int center_x = static_cast<int>(pill.CenterPoint().x());
  const int center_y = static_cast<int>(pill.CenterPoint().y());
  const SkColor center = bitmap.getColor(center_x, center_y);
  const int alpha = MahoSidebarScrollBar::kIdleAlpha;
  EXPECT_NEAR(AlphaOf(center), alpha, 2);
  // SkBitmap::getColor() unpremultiplies, so the tint keeps the palette RGB.
  EXPECT_NEAR(SkColorGetR(center), SkColorGetR(kNeutral), 3);
  EXPECT_NEAR(SkColorGetB(center), SkColorGetB(kNeutral), 3);

  EXPECT_LT(AlphaOf(bitmap.getColor(static_cast<int>(pill.x()),
                                    static_cast<int>(pill.y()))),
            MahoSidebarScrollBar::kIdleAlpha / 2)
      << "the pill's bounding-box corner must stay mostly outside the rounded "
         "shape; a square thumb would cover it fully";
  EXPECT_EQ(AlphaOf(bitmap.getColor(0, center_y)), 0)
      << "the strip outside the pill must stay transparent";

  std::optional<std::string> dump_dir =
      base::Environment::Create()->GetVar("MAHO_SCROLLBAR_DUMP_DIR");
  if (dump_dir && !dump_dir->empty()) {
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                          /*discard_transparency=*/false);
    ASSERT_TRUE(png.has_value());
    ASSERT_TRUE(base::WriteFile(
        base::FilePath(*dump_dir).Append("maho-scrollbar-pill-idle.png"),
        *png));
  }
}

}  // namespace
}  // namespace maho