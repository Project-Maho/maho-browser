// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_board_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_column_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_tab_row_view.h"
#include "testing/gtest/include/gtest/gtest.h"
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

}  // namespace
}  // namespace maho
