// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_board_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_column_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_tab_row_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

// ---------------------------------------------------------------------------
// Helpers — model builders
// ---------------------------------------------------------------------------

SpaceBoardModel MakeEmptyModel() {
  return SpaceBoardModel{};
}

SpaceBoardModel MakeModelWithColumns(
    std::vector<SpaceBoardColumn> columns,
    const std::string& active_space_id = std::string()) {
  SpaceBoardModel model;
  model.columns = std::move(columns);
  model.active_space_id = active_space_id;
  return model;
}

SpaceBoardColumn MakeColumn(const std::string& id,
                             const std::u16string& name,
                             int tab_count,
                             bool is_active = false) {
  SpaceBoardColumn col;
  col.space_id = id;
  col.name = name;
  col.tab_count = tab_count;
  col.is_active = is_active;
  return col;
}

// ---------------------------------------------------------------------------
// Board view — empty-state rendering
// ---------------------------------------------------------------------------

class MahoSpacesOverlayBoardViewTest : public views::ViewsTestBase {
 protected:
  MahoSpacesOverlayBoardViewTest() = default;
  ~MahoSpacesOverlayBoardViewTest() override = default;

  MahoSpacesOverlayBoardView* CreateBoardView(SpaceBoardModel model) {
    host_widget_ = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget_->Show();

    auto view = std::make_unique<MahoSpacesOverlayBoardView>(
        nullptr, SpacesBoardRenderMode::kFullViewport, model, base::DoNothing());
    auto* raw = view.get();
    host_widget_->SetContentsView(std::move(view));
    return raw;
  }

  void TearDown() override {
    host_widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 private:
  std::unique_ptr<views::Widget> host_widget_;
};

TEST_F(MahoSpacesOverlayBoardViewTest,
       EmptyModelRendersOneChildWithEmptyStateText) {
  auto* board = CreateBoardView(MakeEmptyModel());

  ASSERT_EQ(1u, board->children().size())
      << "Empty model must render exactly one child (empty-state label)";

  auto* label = views::AsViewClass<views::Label>(board->children().front());
  ASSERT_NE(label, nullptr) << "Only child must be a views::Label";
  EXPECT_EQ(u"No spaces available.", label->GetText());
}

TEST_F(MahoSpacesOverlayBoardViewTest,
       NonEmptyModelRendersOneChildPerColumn) {
  SpaceBoardModel model = MakeModelWithColumns(
      {MakeColumn("s1", u"A", 0), MakeColumn("s2", u"B", 0)});

  auto* board = CreateBoardView(std::move(model));

  EXPECT_EQ(2u, board->children().size())
      << "Each column must produce exactly one child view";
}

// ---------------------------------------------------------------------------
// Column view — custom rendering tests
// ---------------------------------------------------------------------------

class MahoSpacesOverlayColumnViewTest : public views::ViewsTestBase {
 protected:
  MahoSpacesOverlayColumnViewTest() = default;
  ~MahoSpacesOverlayColumnViewTest() override = default;

  MahoSpacesOverlayColumnView* CreateColumnView(const SpaceBoardColumn& column,
                                                 bool is_active) {
    host_widget_ = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget_->Show();

    auto view = std::make_unique<MahoSpacesOverlayColumnView>(
        nullptr, SpacesBoardRenderMode::kFullViewport, column, is_active, base::DoNothing());
    auto* raw = view.get();
    host_widget_->SetContentsView(std::move(view));
    return raw;
  }

  void TearDown() override {
    host_widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 private:
  std::unique_ptr<views::Widget> host_widget_;
};

TEST_F(MahoSpacesOverlayColumnViewTest,
       ColumnTitleMatchesModelName) {
  SpaceBoardColumn col = MakeColumn("s1", u"My Space", 0);
  auto* view = CreateColumnView(col, /*is_active=*/false);

  EXPECT_EQ(u"My Space", view->GetTitleTextForTesting());
}

TEST_F(MahoSpacesOverlayColumnViewTest,
       PinnedAndRegularTabsRenderSeparateSections) {
  SpaceBoardColumn col = MakeColumn("s1", u"Grouped", 3);

  SpaceBoardTabItem pinned_tab;
  pinned_tab.title = u"Pinned tab";
  pinned_tab.is_pinned = true;
  col.tabs.push_back(pinned_tab);

  SpaceBoardTabItem regular_tab;
  regular_tab.title = u"Regular tab";
  col.tabs.push_back(regular_tab);

  auto* view = CreateColumnView(col, /*is_active=*/false);

  EXPECT_EQ((std::vector<std::u16string>{u"Clear"}),
            view->GetSectionTitlesForTesting());
}

TEST_F(MahoSpacesOverlayColumnViewTest,
       RegularOnlyTabsRenderNoSections) {
  SpaceBoardColumn col = MakeColumn("s1", u"Grouped", 1);

  SpaceBoardTabItem regular_tab;
  regular_tab.title = u"Regular tab";
  col.tabs.push_back(regular_tab);

  auto* view = CreateColumnView(col, /*is_active=*/false);

  EXPECT_TRUE(view->GetSectionTitlesForTesting().empty());
}

// ---------------------------------------------------------------------------
// Tab row view — click activation and callback execution
// ---------------------------------------------------------------------------

class MahoSpacesOverlayTabRowViewTest : public views::ViewsTestBase {
 protected:
  MahoSpacesOverlayTabRowViewTest() = default;
  ~MahoSpacesOverlayTabRowViewTest() override = default;

  void SetUp() override {
    views::ViewsTestBase::SetUp();
    host_widget_ = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget_->Show();
  }

  void TearDown() override {
    host_widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  std::unique_ptr<views::Widget> host_widget_;
};

TEST_F(MahoSpacesOverlayTabRowViewTest, ClickTabRowFiresCallback) {
  SpaceBoardTabItem tab;
  tab.id = "tab1";
  tab.title = u"Tab 1";

  std::string clicked_space;
  std::string clicked_tab;

  auto view = std::make_unique<MahoSpacesOverlayTabRowView>(
      nullptr, tab, "space1",
      base::BindRepeating(
          [](std::string* out_space, std::string* out_tab,
             const std::string& space_id, const std::string& tab_id) {
            *out_space = space_id;
            *out_tab = tab_id;
          },
          &clicked_space, &clicked_tab));

  auto* raw_view = host_widget_->SetContentsView(std::move(view));

  // Simulate mouse click
  ui::MouseEvent click_event(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                             base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                             ui::EF_LEFT_MOUSE_BUTTON);
  raw_view->OnMousePressed(click_event);

  EXPECT_EQ("space1", clicked_space);
  EXPECT_EQ("tab1", clicked_tab);
}

// ---------------------------------------------------------------------------
// Overlay view - palette paint-mode and theme-refresh contract
//
// The Spaces overlay is one of three surfaces that paint the sidebar palette
// (docked rail, Downloads/Archive library overlay, Spaces overlay). It lives in
// its own top-level widget, so it used to disagree with the rail in two ways:
// it hardcoded "opaque" for its gradient regardless of the palette it was handed,
// and its OnThemeChanged() only repainted from whatever snapshot it already had.
// ---------------------------------------------------------------------------

class MahoSpacesOverlayViewPaletteTest : public views::ViewsTestBase {
 protected:
  MahoSpacesOverlayViewPaletteTest() = default;
  ~MahoSpacesOverlayViewPaletteTest() override = default;

  MahoSpacesOverlayView* CreateOverlayView() {
    host_widget_ = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget_->Show();

    auto view = std::make_unique<MahoSpacesOverlayView>(
        MahoSpacesOverlayView::ForTestingTag{}, MakeEmptyModel(),
        base::DoNothing(), base::DoNothing());
    auto* raw = view.get();
    host_widget_->SetContentsView(std::move(view));
    return raw;
  }

  void TearDown() override {
    host_widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 private:
  std::unique_ptr<views::Widget> host_widget_;
};

TEST_F(MahoSpacesOverlayViewPaletteTest,
       PaintModeComesFromTheSnapshotNotAHardcode) {
  auto* view = CreateOverlayView();

  MahoSidebarPalette opaque_palette;
  opaque_palette.opaque = true;
  view->SetPalette(opaque_palette);
  EXPECT_TRUE(view->palette_opaque_for_testing())
      << "the rail's opaque decision must reach the overlay";

  // While the overlay is up the rail resolves opaque, but the overlay must be
  // able to represent and paint the translucent snapshot too instead of forcing
  // opaque stops the rail never resolved.
  MahoSidebarPalette translucent_palette;
  translucent_palette.opaque = false;
  view->SetPalette(translucent_palette);
  EXPECT_FALSE(view->palette_opaque_for_testing());
}

TEST_F(MahoSpacesOverlayViewPaletteTest,
       ThemeChangePullsTheRailSnapshotBeforeRepainting) {
  auto* view = CreateOverlayView();

  int refresh_calls = 0;
  view->set_palette_refresh_callback(base::BindRepeating(
      [](int* calls, MahoSpacesOverlayView* target) {
        ++*calls;
        MahoSidebarPalette pulled;
        pulled.opaque = true;
        pulled.primary_text = SkColorSetRGB(0x01, 0x02, 0x03);
        target->SetPalette(pulled);
      },
      &refresh_calls, view));

  view->OnThemeChanged();

  EXPECT_EQ(refresh_calls, 1)
      << "a theme change must pull the rail's authoritative snapshot";
  EXPECT_TRUE(view->palette_opaque_for_testing());
}

// The shared span is what makes the rail, the library overlay, and this overlay
// agree. Painting the same palette across two very different widths must put the
// same color at the same offset from the top-left; the pre-fix per-surface spans
// (the rail's own variable width vs the overlay's fixed column) stretched the
// identical stops differently and read as two shades of one theme.
TEST_F(MahoSpacesOverlayViewPaletteTest,
       SharedGradientSpanRendersIdenticallyAcrossSurfaceWidths) {
  // Three distinct opaque stops so any span difference is visible in the pixel.
  MahoSidebarPalette palette;
  palette.opaque = true;
  palette.opaque_contrast_stops = {
      SkColorSetRGB(0x10, 0x10, 0x10),
      SkColorSetRGB(0x80, 0x80, 0x80),
      SkColorSetRGB(0xF0, 0xF0, 0xF0),
  };

  const int rail_width = sidebar_layout::kDefaultRailWidthDp;
  const int overlay_width = sidebar_layout::kLibraryOverlayContentWidthDp;
  ASSERT_GT(overlay_width, rail_width);

  const auto paint = [&palette](int width, int span) {
    gfx::Canvas canvas(gfx::Size(width, 64), 1.0f, /*is_opaque=*/false);
    const SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    PaintMahoSidebarThemedBackground(
        &canvas, palette, gfx::Rect(0, 0, width, 64), /*opaque=*/true, radii,
        span);
    return canvas.GetBitmap().getColor(4, 4);
  };

  EXPECT_EQ(paint(rail_width, sidebar_layout::kThemedBackgroundGradientSpanDp),
            paint(overlay_width, sidebar_layout::kThemedBackgroundGradientSpanDp))
      << "the shared span must resolve one tint across surface widths";

  // Regression witness: with the old full-bounds span (0) the two widths land on
  // different colors, which is exactly the reported rail/overlay mismatch.
  EXPECT_NE(paint(rail_width, /*span=*/0), paint(overlay_width, /*span=*/0))
      << "a per-surface span is the defect this token removes";
}

}  // namespace
}  // namespace maho
