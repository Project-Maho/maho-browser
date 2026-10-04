// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_result_row_view.h"

#include <memory>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

views::View* GetContentView(MahoCommandResultRowView* row) {
  EXPECT_EQ(1u, row->children().size());
  return row->children().front().get();
}

ImageData MakeTestFaviconImageData() {
  constexpr uint8_t kPngBytes[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00,
      0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
      0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63,
      0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89,
      0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
      0xAE, 0x42, 0x60, 0x82,
  };

  ImageData image_data;
  image_data.data.assign(std::begin(kPngBytes), std::end(kPngBytes));
  image_data.width = 16;
  image_data.height = 16;
  image_data.format = "png";
  return image_data;
}

class MahoCommandResultRowViewTest : public views::ViewsTestBase {
 protected:
  MahoCommandResultRowViewTest() = default;
  ~MahoCommandResultRowViewTest() override = default;

  MahoCommandResultRowView* CreateRow(
      const CommandSuggestion& suggestion,
      MahoCommandResultRowView::PresentationStyle presentation_style =
          MahoCommandResultRowView::PresentationStyle::kDefault) {
    auto host_widget = CreateTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    host_widget->Show();

    auto row = std::make_unique<MahoCommandResultRowView>(
        suggestion, 0, 1, base::DoNothing(), base::DoNothing(),
        presentation_style);
    auto* raw = row.get();
    host_widget->SetContentsView(std::move(row));
    host_widgets_.push_back(std::move(host_widget));
    return raw;
  }

  void TearDown() override {
    host_widgets_.clear();
    views::ViewsTestBase::TearDown();
  }

 private:
  std::vector<std::unique_ptr<views::Widget>> host_widgets_;
};

TEST_F(MahoCommandResultRowViewTest,
       DefaultRowsRenderInlineCompactMetadataInsteadOfSubtitleStack) {
  CommandSuggestion suggestion;
  suggestion.title = "Example Page";
  suggestion.subtitle = "https://docs.maho.test/path";
  suggestion.type = CommandSuggestionType::kHistory;

  auto* row = CreateRow(suggestion);

  auto* content_view = GetContentView(row);
  ASSERT_GE(content_view->children().size(), 2u);

  auto* text_container = content_view->children()[1].get();
  ASSERT_GE(text_container->children().size(), 1u);
  auto* text_stack = text_container->children().front().get();

  ASSERT_EQ(3u, text_stack->children().size())
      << "Default rows should use title, separator, and compact metadata labels";

  auto* separator_label =
      views::AsViewClass<views::Label>(text_stack->children()[1].get());
  auto* metadata_label =
      views::AsViewClass<views::Label>(text_stack->children()[2].get());

  ASSERT_NE(separator_label, nullptr);
  ASSERT_NE(metadata_label, nullptr);
  EXPECT_EQ(u"—", separator_label->GetText());
  EXPECT_TRUE(separator_label->GetVisible());
  EXPECT_EQ(u"docs.maho.test", metadata_label->GetText());
  EXPECT_TRUE(metadata_label->GetVisible());
}

TEST_F(MahoCommandResultRowViewTest,
       DefaultRowsUseTighterPreferredHeightAndGlyphTileSize) {
  CommandSuggestion suggestion;
  suggestion.title = "Example Page";
  suggestion.subtitle = "https://docs.maho.test/path";
  suggestion.type = CommandSuggestionType::kHistory;

  auto* row = CreateRow(suggestion);

  EXPECT_EQ(gfx::Size(0, MahoCommandResultRowView::kRowHeightDp),
            row->GetPreferredSize());

  auto* content_view = GetContentView(row);
  ASSERT_FALSE(content_view->children().empty());
  auto* glyph_container = content_view->children().front().get();
  EXPECT_EQ(gfx::Size(MahoCommandResultRowView::kGlyphTileSizeDp,
                      MahoCommandResultRowView::kGlyphTileSizeDp),
            glyph_container->GetPreferredSize());
}

TEST_F(MahoCommandResultRowViewTest,
       CurrentTabSeededRowsKeepInlineCompactMetadataTreatment) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://current.maho.test/section";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);

  auto* content_view = GetContentView(row);
  ASSERT_GE(content_view->children().size(), 2u);

  auto* text_container = content_view->children()[1].get();
  ASSERT_GE(text_container->children().size(), 1u);
  auto* text_stack = text_container->children().front().get();

  ASSERT_EQ(3u, text_stack->children().size());
  auto* metadata_label =
      views::AsViewClass<views::Label>(text_stack->children()[2].get());

  ASSERT_NE(metadata_label, nullptr);
  EXPECT_EQ(u"current.maho.test/section", metadata_label->GetText());
  EXPECT_TRUE(metadata_label->GetVisible());
}

TEST_F(MahoCommandResultRowViewTest,
       CurrentTabSeededRowsKeepTitleVisibleAndElideMetadataFirst) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle =
      "https://current.maho.test/very/long/path/that/should/elide/first";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);
  row->SetBoundsRect(gfx::Rect(0, 0, 280, row->GetPreferredSize().height()));
  row->DeprecatedLayoutImmediately();

  auto* title_label = row->GetTitleLabelForTesting();
  auto* metadata_label = row->GetCompactMetadataLabelForTesting();
  ASSERT_NE(nullptr, title_label);
  ASSERT_NE(nullptr, metadata_label);

  EXPECT_GT(title_label->width(), 0);
  EXPECT_EQ(title_label->GetPreferredSize().width(), title_label->width());
  EXPECT_LT(metadata_label->width(), metadata_label->GetPreferredSize().width());
}

TEST_F(MahoCommandResultRowViewTest,
       SeededRow_SelectedHighlightUsesExpectedInsetAndRadius) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://current.maho.test/section";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);
  row->SetSelected(true);
  row->SetBoundsRect(gfx::Rect(0, 0, 480, row->GetPreferredSize().height()));
  row->DeprecatedLayoutImmediately();

  auto* content_view = row->GetContentViewForTesting();
  ASSERT_NE(nullptr, content_view);
  const gfx::Insets expected_insets =
      MahoCommandResultRowView::GetSelectionInsetsForTesting(
          MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);
  EXPECT_EQ(expected_insets.left(), content_view->bounds().x());
  EXPECT_EQ(expected_insets.top(), content_view->bounds().y());
  EXPECT_EQ(row->width() - expected_insets.right(),
            content_view->bounds().right());
  EXPECT_EQ(row->height() - expected_insets.bottom(),
            content_view->bounds().bottom());
  EXPECT_NE(nullptr, content_view->GetBackground());
  EXPECT_EQ(nullptr, content_view->GetBorder());
  EXPECT_EQ(6, MahoCommandResultRowView::GetSelectionCornerRadiusForTesting(
                   MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded));
}

TEST_F(MahoCommandResultRowViewTest,
       SeededRow_SubtitlePreservesUrl) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://example.com/some/path?q=1";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);

  EXPECT_EQ(u"example.com/some/path?q=1",
            row->GetCompactMetadataTextForTesting());
  EXPECT_NE(std::u16string::npos,
            row->GetCompactMetadataTextForTesting().find(u"/some/path"));
}

TEST_F(MahoCommandResultRowViewTest,
       SeededRow_SubtitleFallsBackToHostForEmptyPath) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://example.com/";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);

  EXPECT_EQ(u"example.com", row->GetCompactMetadataTextForTesting());
  EXPECT_FALSE(row->GetCompactMetadataTextForTesting().empty());
}

TEST_F(MahoCommandResultRowViewTest,
       SeededRow_TitleUsesSeededWeightConstant) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://example.com/some/path?q=1";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* seeded_row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);
  auto* seeded_title = seeded_row->GetTitleLabelForTesting();
  ASSERT_NE(nullptr, seeded_title);

  EXPECT_EQ(MahoCommandResultRowView::GetSeededRowTitleFontWeightForTesting(),
            seeded_title->font_list().GetFontWeight());

  auto* default_row = CreateRow(suggestion);
  auto* default_title = default_row->GetTitleLabelForTesting();
  ASSERT_NE(nullptr, default_title);
  EXPECT_EQ(gfx::Font::Weight::MEDIUM,
            default_title->font_list().GetFontWeight());
}

TEST_F(MahoCommandResultRowViewTest,
       SeededRow_SelectedFaviconUsesContainerTreatment) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://example.com/some/path?q=1";
  suggestion.type = CommandSuggestionType::kNavigation;
  suggestion.icon = MakeTestFaviconImageData();

  auto* selected_seeded_row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);
  selected_seeded_row->SetSelected(true);

  auto* selected_seeded_content = GetContentView(selected_seeded_row);
  ASSERT_FALSE(selected_seeded_content->children().empty());
  auto* selected_seeded_glyph_container =
      selected_seeded_content->children().front().get();
  ASSERT_EQ(1u, selected_seeded_glyph_container->children().size());
  auto* selected_seeded_favicon = views::AsViewClass<views::ImageView>(
      selected_seeded_glyph_container->children().front().get());

  ASSERT_NE(nullptr, selected_seeded_favicon);
  EXPECT_EQ(nullptr, selected_seeded_glyph_container->GetBackground());
  EXPECT_EQ(nullptr, selected_seeded_glyph_container->GetBorder());
  EXPECT_EQ(gfx::Size(20, 20),
            selected_seeded_glyph_container->GetPreferredSize());
  EXPECT_NE(nullptr, selected_seeded_favicon->GetBackground());
  EXPECT_EQ(nullptr, selected_seeded_favicon->GetBorder());
  EXPECT_EQ(gfx::Size(16, 16), selected_seeded_favicon->GetPreferredSize());

  auto* unselected_seeded_row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);

  auto* unselected_seeded_content = GetContentView(unselected_seeded_row);
  ASSERT_FALSE(unselected_seeded_content->children().empty());
  auto* unselected_seeded_glyph_container =
      unselected_seeded_content->children().front().get();
  ASSERT_EQ(1u, unselected_seeded_glyph_container->children().size());
  auto* unselected_seeded_favicon = views::AsViewClass<views::ImageView>(
      unselected_seeded_glyph_container->children().front().get());

  EXPECT_EQ(nullptr, unselected_seeded_glyph_container->GetBackground());
  EXPECT_EQ(nullptr, unselected_seeded_glyph_container->GetBorder());
  ASSERT_NE(nullptr, unselected_seeded_favicon);
  EXPECT_EQ(nullptr, unselected_seeded_favicon->GetBackground());
  EXPECT_EQ(nullptr, unselected_seeded_favicon->GetBorder());

  auto* default_row = CreateRow(suggestion);
  auto* default_content = GetContentView(default_row);
  ASSERT_FALSE(default_content->children().empty());
  auto* default_glyph_container = default_content->children().front().get();

  EXPECT_EQ(nullptr, default_glyph_container->GetBackground());
  EXPECT_EQ(nullptr, default_glyph_container->GetBorder());
}

TEST_F(MahoCommandResultRowViewTest,
       SeededRow_SetIconUpgradesFallbackGlyphToFaviconView) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://example.com/some/path?q=1";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);

  EXPECT_TRUE(row->UsesFallbackGlyphForTesting());
  EXPECT_EQ(nullptr, row->GetFaviconImageForTesting());

  row->SetIcon(MakeTestFaviconImageData());

  EXPECT_FALSE(row->UsesFallbackGlyphForTesting());
  ASSERT_NE(nullptr, row->GetFaviconImageForTesting());
  EXPECT_EQ(gfx::Size(14, 14),
            row->GetFaviconImageForTesting()->GetPreferredSize());
}

TEST_F(MahoCommandResultRowViewTest,
       SearchRowsRenderFallbackGlyphWithoutIconAndProvidedFavicon) {
  CommandSuggestion iconless;
  iconless.title = "Remote suggestion";
  iconless.subtitle = "Search suggestion";
  iconless.type = CommandSuggestionType::kSearch;

  auto* iconless_row = CreateRow(iconless);
  EXPECT_TRUE(iconless_row->UsesFallbackGlyphForTesting());
  EXPECT_EQ(nullptr, iconless_row->GetFaviconImageForTesting());

  CommandSuggestion with_icon = iconless;
  with_icon.icon = MakeTestFaviconImageData();
  auto* icon_row = CreateRow(with_icon);
  EXPECT_FALSE(icon_row->UsesFallbackGlyphForTesting());
  ASSERT_NE(nullptr, icon_row->GetFaviconImageForTesting());
  EXPECT_EQ(gfx::Size(16, 16),
            icon_row->GetFaviconImageForTesting()->GetPreferredSize());
}

TEST_F(MahoCommandResultRowViewTest,
       SelectedDefaultRowsUseFillOnlyTreatmentWithoutBorder) {
  CommandSuggestion suggestion;
  suggestion.title = "Example Page";
  suggestion.subtitle = "https://docs.maho.test/path";
  suggestion.type = CommandSuggestionType::kHistory;

  auto* row = CreateRow(suggestion);
  row->SetSelected(true);

  auto* content_view = GetContentView(row);
  EXPECT_NE(nullptr, content_view->GetBackground());
  EXPECT_EQ(nullptr, content_view->GetBorder());
}

TEST_F(MahoCommandResultRowViewTest,
       SelectedCurrentTabSeededRowsRemainFillOnlyWithoutBorder) {
  CommandSuggestion suggestion;
  suggestion.title = "Current Page";
  suggestion.subtitle = "https://current.maho.test/section";
  suggestion.type = CommandSuggestionType::kNavigation;

  auto* row = CreateRow(
      suggestion,
      MahoCommandResultRowView::PresentationStyle::kCurrentTabSeeded);
  row->SetSelected(true);

  auto* content_view = GetContentView(row);
  EXPECT_NE(nullptr, content_view->GetBackground());
  EXPECT_EQ(nullptr, content_view->GetBorder());
}

TEST_F(MahoCommandResultRowViewTest,
       AskAffordanceSwapsGlyphAndAddsRightSideExplanation) {
  CommandSuggestion suggestion;
  suggestion.title = "weather seoul";
  suggestion.subtitle = "Search suggestion";
  suggestion.type = CommandSuggestionType::kSearch;

  auto* plain_row = CreateRow(suggestion);
  EXPECT_FALSE(plain_row->UsesAskGlyphForTesting());
  EXPECT_EQ(nullptr, plain_row->GetAskExplanationLabelForTesting());

  auto* ask_row = CreateRow(suggestion);
  ask_row->SetAskAffordance(true);
  EXPECT_TRUE(ask_row->UsesAskGlyphForTesting());
  ASSERT_NE(nullptr, ask_row->GetAskExplanationLabelForTesting());
  EXPECT_TRUE(ask_row->GetAskExplanationLabelForTesting()->GetVisible());
  EXPECT_EQ(u"Ask Maho",
            ask_row->GetAskExplanationLabelForTesting()->GetText());

  // The explanation reuses the right-aligned affordance lane, i.e. it lives in
  // the trailing child of the row content view.
  auto* content_view = ask_row->GetContentViewForTesting();
  ASSERT_NE(nullptr, content_view);
  ASSERT_FALSE(content_view->children().empty());
  EXPECT_TRUE(content_view->children().back()->Contains(
      ask_row->GetAskExplanationLabelForTesting()));

  ask_row->SetAskAffordance(false);
  EXPECT_FALSE(ask_row->UsesAskGlyphForTesting());
  ASSERT_NE(nullptr, ask_row->GetAskExplanationLabelForTesting());
  EXPECT_FALSE(ask_row->GetAskExplanationLabelForTesting()->GetVisible());
}

TEST_F(MahoCommandResultRowViewTest,
       AskAffordanceOnTabRowReplacesSwitchToTabAffordance) {
  CommandSuggestion suggestion;
  suggestion.title = "Example Tab";
  suggestion.subtitle = "https://example.com";
  suggestion.type = CommandSuggestionType::kTab;

  auto* row = CreateRow(suggestion);
  ASSERT_NE(nullptr, row->GetSwitchTabContainerForTesting());
  EXPECT_TRUE(row->GetSwitchTabContainerForTesting()->GetVisible());

  row->SetAskAffordance(true);
  EXPECT_FALSE(row->GetSwitchTabContainerForTesting()->GetVisible());
  ASSERT_NE(nullptr, row->GetAskExplanationLabelForTesting());
  EXPECT_TRUE(row->GetAskExplanationLabelForTesting()->GetVisible());
}

TEST_F(MahoCommandResultRowViewTest,
       SwitchToTabAffordanceRendersForTabType) {
  CommandSuggestion suggestion;
  suggestion.title = "Example Tab";
  suggestion.subtitle = "https://example.com";
  suggestion.type = CommandSuggestionType::kTab;

  auto* row = CreateRow(suggestion);
  EXPECT_NE(nullptr, row);
}

}  // namespace
}  // namespace maho
