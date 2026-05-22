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
// Test fixture — MahoSpacesOverlayView
// ---------------------------------------------------------------------------

class MahoSpacesOverlayViewTest : public views::ViewsTestBase {
 protected:
  MahoSpacesOverlayViewTest() = default;
  ~MahoSpacesOverlayViewTest() override = default;

  MahoSpacesOverlayView* CreateView(SpaceBoardModel model) {
    host_widget_ = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget_->Show();

    auto view = std::make_unique<MahoSpacesOverlayView>(
        MahoSpacesOverlayView::ForTestingTag{},
        std::move(model),
        base::DoNothing(),
        base::DoNothing());

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

// ---------------------------------------------------------------------------
// Summary text — count / plural / active name
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextReflectsSingleSpaceCount) {
  auto* view = CreateView(MakeModelWithColumns({MakeColumn("s1", u"Work", 3)}));

  EXPECT_NE(std::u16string::npos,
            view->GetSummaryTextForTesting().find(u"1 space"))
      << base::UTF16ToUTF8(view->GetSummaryTextForTesting());
}

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextReflectsPluralSpaceCount) {
  auto* view = CreateView(MakeModelWithColumns(
      {MakeColumn("s1", u"Work", 2), MakeColumn("s2", u"Personal", 5)}));

  EXPECT_NE(std::u16string::npos,
            view->GetSummaryTextForTesting().find(u"2 spaces"))
      << base::UTF16ToUTF8(view->GetSummaryTextForTesting());
}

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextReflectsTotalTabCount) {
  auto* view = CreateView(MakeModelWithColumns(
      {MakeColumn("s1", u"Alpha", 3), MakeColumn("s2", u"Beta", 4)}));

  EXPECT_NE(std::u16string::npos,
            view->GetSummaryTextForTesting().find(u"7 tabs"))
      << base::UTF16ToUTF8(view->GetSummaryTextForTesting());
}

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextIncludesActiveNameWhenIsActiveSet) {
  SpaceBoardColumn active_col = MakeColumn("s1", u"Research", 2, /*is_active=*/true);
  auto* view = CreateView(MakeModelWithColumns({active_col}));

  const std::u16string text = view->GetSummaryTextForTesting();
  EXPECT_NE(std::u16string::npos, text.find(u"Active: ")) << base::UTF16ToUTF8(text);
  EXPECT_NE(std::u16string::npos, text.find(u"Research")) << base::UTF16ToUTF8(text);
}

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextIncludesActiveNameWhenResolvedViaActiveSpaceId) {
  SpaceBoardColumn col = MakeColumn("space-42", u"Focus", 1);
  auto* view = CreateView(MakeModelWithColumns({col}, "space-42"));

  const std::u16string text = view->GetSummaryTextForTesting();
  EXPECT_NE(std::u16string::npos, text.find(u"Focus")) << base::UTF16ToUTF8(text);
}

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextOmitsActiveSegmentWhenNoActiveSpace) {
  auto* view = CreateView(MakeModelWithColumns(
      {MakeColumn("s1", u"Misc", 1), MakeColumn("s2", u"Other", 0)}));

  EXPECT_EQ(std::u16string::npos,
            view->GetSummaryTextForTesting().find(u"Active:"))
      << base::UTF16ToUTF8(view->GetSummaryTextForTesting());
}

TEST_F(MahoSpacesOverlayViewTest,
       SummaryTextSingularTabWhenExactlyOne) {
  auto* view = CreateView(MakeModelWithColumns({MakeColumn("s1", u"Solo", 1)}));

  EXPECT_NE(std::u16string::npos,
            view->GetSummaryTextForTesting().find(u"1 tab"))
      << base::UTF16ToUTF8(view->GetSummaryTextForTesting());
  EXPECT_EQ(std::u16string::npos,
            view->GetSummaryTextForTesting().find(u"1 tabs"))
      << base::UTF16ToUTF8(view->GetSummaryTextForTesting());
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
        model, base::DoNothing());
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
// Column view — current-chip / action-button rendering distinction
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
        column, is_active, base::DoNothing());
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
       ActiveColumnCurrentChipIsVisible) {
  SpaceBoardColumn col = MakeColumn("s1", u"Active Space", 2, /*is_active=*/true);
  auto* view = CreateColumnView(col, /*is_active=*/true);

  EXPECT_TRUE(view->IsCurrentChipVisibleForTesting());
}

TEST_F(MahoSpacesOverlayColumnViewTest,
       InactiveColumnCurrentChipIsHidden) {
  SpaceBoardColumn col = MakeColumn("s2", u"Inactive Space", 3, /*is_active=*/false);
  auto* view = CreateColumnView(col, /*is_active=*/false);

  EXPECT_FALSE(view->IsCurrentChipVisibleForTesting());
}

TEST_F(MahoSpacesOverlayColumnViewTest,
       ActiveColumnActionButtonSaysOpen) {
  SpaceBoardColumn col = MakeColumn("s1", u"Current", 1, /*is_active=*/true);
  auto* view = CreateColumnView(col, /*is_active=*/true);

  EXPECT_EQ(u"Open", view->GetActionButtonTextForTesting());
}

TEST_F(MahoSpacesOverlayColumnViewTest,
       InactiveColumnActionButtonSaysSwitch) {
  SpaceBoardColumn col = MakeColumn("s2", u"Other", 0, /*is_active=*/false);
  auto* view = CreateColumnView(col, /*is_active=*/false);

  EXPECT_EQ(u"Switch", view->GetActionButtonTextForTesting());
}

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

  EXPECT_EQ((std::vector<std::u16string>{u"BOOKMARKS", u"TABS"}),
            view->GetSectionTitlesForTesting());
}

TEST_F(MahoSpacesOverlayColumnViewTest,
       RegularOnlyTabsRenderTabsSectionOnly) {
  SpaceBoardColumn col = MakeColumn("s1", u"Grouped", 1);

  SpaceBoardTabItem regular_tab;
  regular_tab.title = u"Regular tab";
  col.tabs.push_back(regular_tab);

  auto* view = CreateColumnView(col, /*is_active=*/false);

  EXPECT_EQ((std::vector<std::u16string>{u"TABS"}),
            view->GetSectionTitlesForTesting());
}

}  // namespace
}  // namespace maho
