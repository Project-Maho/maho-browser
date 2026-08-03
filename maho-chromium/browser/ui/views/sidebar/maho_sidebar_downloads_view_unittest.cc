// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/layout/box_layout.h"

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
