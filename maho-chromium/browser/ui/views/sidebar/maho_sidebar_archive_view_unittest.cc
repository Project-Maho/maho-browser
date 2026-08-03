// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"

namespace maho {

// Fixture outside the anonymous namespace so it can be befriended if needed and
// to mirror the sibling view unittests' convention.
class MahoSidebarArchiveViewTest : public views::ViewsTestBase {
 protected:
  ArchivedTabItem MakeTab(const std::string& id) {
    ArchivedTabItem tab;
    tab.tab_id = id;
    tab.title = u"Archived Tab";
    tab.url = "https://example.com/" + id;
    tab.host = u"example.com";
    tab.archived_at = base::Time::Now();  // groups under "Today"
    return tab;
  }

  int ListPreferredHeightWith(int tab_count) {
    // browser=nullptr is safe: the ctor only stores it and builds UI; the
    // test-injection path (SetArchivedTabsForTesting) never touches the browser.
    auto view = std::make_unique<MahoSidebarArchiveView>(/*browser=*/nullptr);
    std::vector<ArchivedTabItem> tabs;
    for (int i = 0; i < tab_count; ++i) {
      tabs.push_back(MakeTab("t" + base::NumberToString(i)));
    }
    view->SetArchivedTabsForTesting(std::move(tabs));
    return view->list_container_for_testing()->GetPreferredSize().height();
  }
};

// Regression for A1: ArchiveRowView previously called SetPreferredSize({0,0}) in
// its constructor, forcing every row to zero height. The section container then
// collapsed to zero, so the list showed only the "Today N" header with no rows.
// After removing that call, rows take their natural (~48dp) height, so adding
// more rows must increase the list's preferred height.
TEST_F(MahoSidebarArchiveViewTest, ArchivedRowsHaveNonZeroHeight) {
  const int one_row = ListPreferredHeightWith(1);
  const int four_rows = ListPreferredHeightWith(4);

  EXPECT_GT(one_row, 0) << "a single archived row must contribute height";
  // With the 0-height bug both are equal (only the shared 'Today' header).
  EXPECT_GT(four_rows, one_row)
      << "more archived rows must make the list taller (A1 regression)";
}

}  // namespace maho
