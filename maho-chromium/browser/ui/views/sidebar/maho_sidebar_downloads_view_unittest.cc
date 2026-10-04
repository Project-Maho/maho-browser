// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/render_text_test_api.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_test_api.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

class MahoSidebarDownloadsViewTest : public views::ViewsTestBase {
 protected:
  DownloadItem MakeDownload(const std::string& id, const std::string& state) {
    DownloadItem item;
    item.id = id;
    item.filename = u"file_" + base::ASCIIToUTF16(id) + u".txt";
    item.url = "https://example.com/download/" + id;
    item.state = state;
    item.total_bytes = 1024 * 1024;
    item.received_bytes = state == "completed" ? item.total_bytes : 512 * 1024;
    item.started_at = "Saturday, July 18, 2026 6:00:00 PM";
    if (state == "completed") {
      item.completed_at = "Saturday, July 18, 2026 6:01:00 PM";
    }
    return item;
  }

  DownloadItem MakeNamed(const std::string& id,
                         const std::u16string& filename,
                         const std::string& url) {
    DownloadItem item;
    item.id = id;
    item.filename = filename;
    item.url = url;
    item.state = "completed";
    item.total_bytes = 1024 * 1024;
    item.received_bytes = item.total_bytes;
    item.started_at = "Saturday, July 18, 2026 6:00:00 PM";
    item.completed_at = "Saturday, July 18, 2026 6:01:00 PM";
    return item;
  }

  std::vector<DownloadItem> MakeFiveCompleted() {
    std::vector<DownloadItem> items;
    for (int i = 1; i <= 5; ++i) {
      items.push_back(MakeDownload(base::NumberToString(i), "completed"));
    }
    return items;
  }

  int CountDownloadRows(MahoSidebarDownloadsView* view) {
    views::View* list_container = view->list_container_for_testing();
    int row_count = 0;
    for (views::View* child : list_container->children()) {
      for (views::View* grandchild : child->children()) {
        if (grandchild->GetClassName() == "DownloadRowView") {
          row_count++;
        }
      }
    }
    return row_count;
  }
};

TEST_F(MahoSidebarDownloadsViewTest, SearchUsesSidebarPaletteColors) {
  // Keep the field attached so theme and enabled-state refreshes exercise
  // the same color resolution path as a live search field.
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                 views::Widget::InitParams::TYPE_POPUP);
  auto* view = widget->SetContentsView(
      std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr));
  MahoSidebarPalette palette;
  palette.row_active = SkColorSetRGB(0x21, 0x32, 0x43);
  palette.outline = SkColorSetRGB(0x54, 0x65, 0x76);
  palette.primary_text = SkColorSetRGB(0x87, 0x98, 0xA9);
  palette.secondary_text = SkColorSetRGB(0xBA, 0xCB, 0xDC);

  view->SetSidebarPalette(palette);

  ASSERT_NE(nullptr, view->search_shell_for_testing()->background());
  EXPECT_EQ(palette.row_active,
            view->search_shell_for_testing()
                ->background()
                ->color()
                .ResolveToSkColor(/*color_provider=*/nullptr));
  EXPECT_EQ(palette.primary_text,
            view->search_field_for_testing()->GetTextColor());
  auto* field = view->search_field_for_testing();
  gfx::test::RenderTextTestApi render_text_api(
      views::TextfieldTestApi(field).GetRenderText());
  EXPECT_EQ(palette.secondary_text, field->GetPlaceholderTextColor());
  field->SetText(u"typed after palette assignment");
  EXPECT_EQ(palette.primary_text, render_text_api.colors().GetBreak(0)->second);
  field->OnThemeChanged();
  field->SetEnabled(false);
  field->SetEnabled(true);
  EXPECT_EQ(palette.primary_text, field->GetTextColor());
  EXPECT_EQ(palette.primary_text, render_text_api.colors().GetBreak(0)->second);
  EXPECT_EQ(palette.secondary_text, field->GetPlaceholderTextColor());
  const ui::ImageModel search_image =
      view->search_icon_for_testing()->GetImageModel();
  ASSERT_TRUE(search_image.IsVectorIcon());
  EXPECT_EQ(palette.secondary_text,
            search_image.GetVectorIcon().color().ResolveToSkColor(
                /*color_provider=*/nullptr));

  palette.forced_colors = true;
  palette.primary_text = SK_ColorWHITE;
  palette.secondary_text = SK_ColorYELLOW;
  view->SetSidebarPalette(palette);
  field->SetText(u"");
  field->OnThemeChanged();
  EXPECT_EQ(palette.primary_text, field->GetTextColor());
  EXPECT_EQ(palette.secondary_text, field->GetPlaceholderTextColor());
  field->SetText(u"typed after forced palette assignment");
  EXPECT_EQ(palette.primary_text, render_text_api.colors().GetBreak(0)->second);
}

TEST_F(MahoSidebarDownloadsViewTest, ContentLabelsUseSidebarPaletteColors) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);
  MahoSidebarPalette palette;
  palette.primary_text = SkColorSetRGB(0x12, 0x34, 0x56);
  palette.secondary_text = SkColorSetRGB(0x65, 0x43, 0x21);
  view->SetSidebarPalette(palette);

  std::vector<views::Label*> empty_labels;
  for (views::View* child :
       view->empty_state_view_for_testing()->children()) {
    if (auto* label = views::AsViewClass<views::Label>(child)) {
      empty_labels.push_back(label);
    }
  }
  ASSERT_EQ(2u, empty_labels.size());
  EXPECT_EQ(palette.primary_text, empty_labels[0]->GetEnabledColor());
  EXPECT_EQ(palette.secondary_text, empty_labels[1]->GetEnabledColor());

  view->SetDownloadsForTesting({MakeDownload("palette", "completed")});
  views::Label* section_title = nullptr;
  views::Label* section_count = nullptr;
  for (views::View* child : view->list_container_for_testing()->children()) {
    if (child->GetClassName() != "DownloadSectionHeaderView") {
      continue;
    }
    ASSERT_EQ(2u, child->children().size());
    section_title = views::AsViewClass<views::Label>(child->children()[0]);
    section_count = views::AsViewClass<views::Label>(child->children()[1]);
  }
  ASSERT_NE(nullptr, section_title);
  ASSERT_NE(nullptr, section_count);
  EXPECT_EQ(palette.primary_text, section_title->GetEnabledColor());
  EXPECT_EQ(palette.primary_text, section_count->GetEnabledColor());
}

TEST_F(MahoSidebarDownloadsViewTest, SearchMatchesFilenameSubstring) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);
  view->SetDownloadsForTesting(MakeFiveCompleted());

  view->SetSearchQueryForTesting(u"file_2");
  EXPECT_EQ(1, CountDownloadRows(view.get()));
}

TEST_F(MahoSidebarDownloadsViewTest, SearchIsCaseInsensitive) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);
  view->SetDownloadsForTesting(MakeFiveCompleted());

  view->SetSearchQueryForTesting(u"FILE_3");
  EXPECT_EQ(1, CountDownloadRows(view.get()));
}

TEST_F(MahoSidebarDownloadsViewTest, SearchMatchesUrlSubstring) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);
  view->SetDownloadsForTesting(MakeFiveCompleted());

  view->SetSearchQueryForTesting(u"download/4");
  EXPECT_EQ(1, CountDownloadRows(view.get()));
}

TEST_F(MahoSidebarDownloadsViewTest, EmptyQueryShowsAll) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);
  view->SetDownloadsForTesting(MakeFiveCompleted());

  view->SetSearchQueryForTesting(u"");
  EXPECT_EQ(5, CountDownloadRows(view.get()));
  EXPECT_TRUE(view->scroll_view_for_testing()->GetVisible());
  EXPECT_FALSE(view->empty_state_view_for_testing()->GetVisible());
}

TEST_F(MahoSidebarDownloadsViewTest, NoMatchShowsEmptyState) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);
  view->SetDownloadsForTesting(MakeFiveCompleted());

  view->SetSearchQueryForTesting(u"zzz-no-match");
  EXPECT_EQ(0, CountDownloadRows(view.get()));
  EXPECT_TRUE(view->empty_state_view_for_testing()->GetVisible());
  EXPECT_FALSE(view->scroll_view_for_testing()->GetVisible());
}

TEST_F(MahoSidebarDownloadsViewTest, SearchMatchesUnicodeAccentFoldedQuery) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);

  std::vector<DownloadItem> items;
  items.push_back(
      MakeNamed("cafe", u"Café Menu.pdf", "https://example.com/a"));
  items.push_back(
      MakeNamed("cjk", u"카페 사진.png", "https://example.com/b"));
  view->SetDownloadsForTesting(items);

  view->SetSearchQueryForTesting(u"café");
  EXPECT_EQ(1, CountDownloadRows(view.get()));
  EXPECT_NE(nullptr,
            view->download_row_for_download_id_for_testing("cafe"));
  EXPECT_EQ(nullptr,
            view->download_row_for_download_id_for_testing("cjk"));
}

TEST_F(MahoSidebarDownloadsViewTest, SearchMatchesUnicodeCjkQuery) {
  auto view = std::make_unique<MahoSidebarDownloadsView>(/*browser=*/nullptr);

  std::vector<DownloadItem> items;
  items.push_back(
      MakeNamed("cafe", u"Café Menu.pdf", "https://example.com/a"));
  items.push_back(
      MakeNamed("cjk", u"카페 사진.png", "https://example.com/b"));
  view->SetDownloadsForTesting(items);

  view->SetSearchQueryForTesting(u"카페");
  EXPECT_EQ(1, CountDownloadRows(view.get()));
  EXPECT_NE(nullptr,
            view->download_row_for_download_id_for_testing("cjk"));
  EXPECT_EQ(nullptr,
            view->download_row_for_download_id_for_testing("cafe"));
}

}  // namespace maho
