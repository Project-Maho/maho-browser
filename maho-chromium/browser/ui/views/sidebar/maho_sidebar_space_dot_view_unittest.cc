// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h"

#include <memory>
#include <vector>

#include "cc/paint/paint_record.h"
#include "cc/paint/paint_recorder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/gfx/canvas.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/controls/label.h"
#include "ui/compositor/layer.h"

namespace maho {

// Fixture is outside the anonymous namespace so the `friend class
// MahoSidebarSpaceDotViewTest` declaration in the header resolves to this
// class (maho:: scope). Mirrors the pattern in
// maho_command_overlay_controller_unittest.cc and
// maho_spaces_overlay_controller_unittest.cc.
class MahoSidebarSpaceDotViewTest : public views::ViewsTestBase {
 protected:
  std::unique_ptr<MahoSidebarSpaceDotView> CreateDotView() {
    return std::make_unique<MahoSidebarSpaceDotView>();
  }

  float GetDotOpacity(MahoSidebarSpaceDotView* view) {
    return view->dot_opacity_;
  }

  views::Label* GetIconLabel(MahoSidebarSpaceDotView* view) {
    return view->icon_label_;
  }

  void SetHovered(MahoSidebarSpaceDotView* view, bool hovered) {
    view->is_hovered_ = hovered;
  }
};

namespace {

class MockCanvas : public SkCanvas {
 public:
  struct DrawOvalCall {
    SkRect oval;
    SkPaint paint;
  };

  MockCanvas(int width, int height) : SkCanvas(width, height) {}
  MockCanvas(const MockCanvas&) = delete;
  MockCanvas& operator=(const MockCanvas&) = delete;

  const std::vector<DrawOvalCall>& draw_oval_calls() const {
    return draw_oval_calls_;
  }

  void onDrawOval(const SkRect& oval, const SkPaint& paint) override {
    draw_oval_calls_.push_back({oval, paint});
  }

 private:
  std::vector<DrawOvalCall> draw_oval_calls_;
};

TEST_F(MahoSidebarSpaceDotViewTest, ConfigureWithIconInactive) {
  auto view = CreateDotView();
  view->Configure("space-1", "Space 1", "🌟", SK_ColorBLUE, /*is_active=*/false);

  EXPECT_EQ(GetIconLabel(view.get())->layer()->GetTargetOpacity(), 0.0f);
  EXPECT_EQ(GetDotOpacity(view.get()), 1.0f);

  cc::PaintRecorder recorder;
  gfx::Canvas canvas(recorder.beginRecording(), 1.0f);
  view->OnPaint(&canvas);

  cc::PaintRecord record = recorder.finishRecordingAsPicture();
  MockCanvas mock_canvas(15, 15);
  record.Playback(&mock_canvas);

  // Dot circle should paint when idle.
  ASSERT_EQ(mock_canvas.draw_oval_calls().size(), 1u);
  SkRect expected_rect = SkRect::MakeLTRB(3.5f, 3.5f, 11.5f, 11.5f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fLeft, expected_rect.fLeft, 0.01f);
}

TEST_F(MahoSidebarSpaceDotViewTest, ConfigureNoIconInactive) {
  auto view = CreateDotView();
  view->Configure("space-2", "Space 2", "", SK_ColorRED, /*is_active=*/false);

  EXPECT_EQ(GetIconLabel(view.get())->layer()->GetTargetOpacity(), 0.0f);
  EXPECT_EQ(GetDotOpacity(view.get()), 1.0f);

  cc::PaintRecorder recorder;
  gfx::Canvas canvas(recorder.beginRecording(), 1.0f);
  view->OnPaint(&canvas);

  cc::PaintRecord record = recorder.finishRecordingAsPicture();
  MockCanvas mock_canvas(15, 15);
  record.Playback(&mock_canvas);

  // Dot circle should paint, but no active halo.
  ASSERT_EQ(mock_canvas.draw_oval_calls().size(), 1u);
  // Radius of default dot is 8.0 / 2 = 4.0. Center is 15 / 2 = 7.5.
  // Expecting a circle at (7.5, 7.5) with radius 4.0, meaning rect is LTRB 3.5, 3.5, 11.5, 11.5.
  SkRect expected_rect = SkRect::MakeLTRB(3.5f, 3.5f, 11.5f, 11.5f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fLeft, expected_rect.fLeft, 0.01f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fTop, expected_rect.fTop, 0.01f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fRight, expected_rect.fRight, 0.01f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fBottom, expected_rect.fBottom, 0.01f);
}

TEST_F(MahoSidebarSpaceDotViewTest, ConfigureWithIconActive) {
  auto view = CreateDotView();
  view->Configure("space-1", "Space 1", "🌟", SK_ColorBLUE, /*is_active=*/true);

  EXPECT_EQ(GetIconLabel(view.get())->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_EQ(GetDotOpacity(view.get()), 0.0f);

  cc::PaintRecorder recorder;
  gfx::Canvas canvas(recorder.beginRecording(), 1.0f);
  view->OnPaint(&canvas);

  cc::PaintRecord record = recorder.finishRecordingAsPicture();
  MockCanvas mock_canvas(15, 15);
  record.Playback(&mock_canvas);

  // Under the corrective plan: dot opacity is 0 (so dot circle doesn't paint)
  // but since it's active, the outer halo paints.
  ASSERT_EQ(mock_canvas.draw_oval_calls().size(), 1u);

  // For active dot, dot size is kDotSizeActive (10.0f). Radius is 5.0f.
  // Halo radius is 5.0f + 0.8f = 5.8f. Center is 7.5f.
  // Rect is LTRB 1.7, 1.7, 13.3, 13.3.
  SkRect expected_halo_rect = SkRect::MakeLTRB(1.7f, 1.7f, 13.3f, 13.3f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fLeft, expected_halo_rect.fLeft, 0.01f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fTop, expected_halo_rect.fTop, 0.01f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fRight, expected_halo_rect.fRight, 0.01f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fBottom, expected_halo_rect.fBottom, 0.01f);
  EXPECT_EQ(mock_canvas.draw_oval_calls()[0].paint.getStyle(), SkPaint::kStroke_Style);
}

TEST_F(MahoSidebarSpaceDotViewTest, ConfigureNoIconActive) {
  auto view = CreateDotView();
  view->Configure("space-2", "Space 2", "", SK_ColorRED, /*is_active=*/true);

  EXPECT_EQ(GetIconLabel(view.get())->layer()->GetTargetOpacity(), 0.0f);
  EXPECT_EQ(GetDotOpacity(view.get()), 1.0f);

  cc::PaintRecorder recorder;
  gfx::Canvas canvas(recorder.beginRecording(), 1.0f);
  view->OnPaint(&canvas);

  cc::PaintRecord record = recorder.finishRecordingAsPicture();
  MockCanvas mock_canvas(15, 15);
  record.Playback(&mock_canvas);

  // Both fallback dot circle AND active halo paint.
  ASSERT_EQ(mock_canvas.draw_oval_calls().size(), 2u);

  // Dot: size is 10.0f, radius 5.0f, center 7.5f.
  // Halo: radius 5.0f + 0.8f = 5.8f, center 7.5f.
  SkRect expected_dot_rect = SkRect::MakeLTRB(2.5f, 2.5f, 12.5f, 12.5f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[0].oval.fLeft, expected_dot_rect.fLeft, 0.01f);
  EXPECT_EQ(mock_canvas.draw_oval_calls()[0].paint.getStyle(), SkPaint::kFill_Style);

  SkRect expected_halo_rect = SkRect::MakeLTRB(1.7f, 1.7f, 13.3f, 13.3f);
  EXPECT_NEAR(mock_canvas.draw_oval_calls()[1].oval.fLeft, expected_halo_rect.fLeft, 0.01f);
  EXPECT_EQ(mock_canvas.draw_oval_calls()[1].paint.getStyle(), SkPaint::kStroke_Style);
}

TEST_F(MahoSidebarSpaceDotViewTest,
       HoveredNoIconInactiveUsesBrighterPaintThanIdle) {
  auto view = CreateDotView();
  view->Configure("space-2", "Space 2", "", SK_ColorRED, /*is_active=*/false);

  cc::PaintRecorder idle_recorder;
  gfx::Canvas idle_canvas(idle_recorder.beginRecording(), 1.0f);
  view->OnPaint(&idle_canvas);

  cc::PaintRecord idle_record = idle_recorder.finishRecordingAsPicture();
  MockCanvas idle_mock_canvas(15, 15);
  idle_record.Playback(&idle_mock_canvas);
  ASSERT_EQ(idle_mock_canvas.draw_oval_calls().size(), 1u);
  const SkColor idle_color = idle_mock_canvas.draw_oval_calls()[0].paint.getColor();

  SetHovered(view.get(), true);

  cc::PaintRecorder hovered_recorder;
  gfx::Canvas hovered_canvas(hovered_recorder.beginRecording(), 1.0f);
  view->OnPaint(&hovered_canvas);

  cc::PaintRecord hovered_record = hovered_recorder.finishRecordingAsPicture();
  MockCanvas hovered_mock_canvas(15, 15);
  hovered_record.Playback(&hovered_mock_canvas);
  ASSERT_EQ(hovered_mock_canvas.draw_oval_calls().size(), 1u);
  const SkColor hovered_color =
      hovered_mock_canvas.draw_oval_calls()[0].paint.getColor();

  EXPECT_EQ(SkColorGetA(hovered_color), SkColorGetA(idle_color));
  EXPECT_GT(SkColorGetR(hovered_color), SkColorGetR(idle_color));
  EXPECT_GT(SkColorGetG(hovered_color), SkColorGetG(idle_color));
  EXPECT_GT(SkColorGetB(hovered_color), SkColorGetB(idle_color));
}

TEST_F(MahoSidebarSpaceDotViewTest, SetActiveToggle) {
  auto view = CreateDotView();
  view->Configure("space-1", "Space 1", "🌟", SK_ColorBLUE, /*is_active=*/false);

  EXPECT_EQ(GetIconLabel(view.get())->layer()->GetTargetOpacity(), 0.0f);

  view->SetActive(true);
  EXPECT_TRUE(view->is_active());
  EXPECT_EQ(GetIconLabel(view.get())->layer()->GetTargetOpacity(), 1.0f);
}

}  // namespace
}  // namespace maho
