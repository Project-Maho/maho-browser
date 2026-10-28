// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"

#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

class MahoMruTabTrackerTest : public BrowserWithTestWindowTest {
 public:
  MahoMruTabTrackerTest() = default;
  ~MahoMruTabTrackerTest() override = default;

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    tracker_ = std::make_unique<MahoMruTabTracker>(browser()->GetTabStripModel());
  }

  void TearDown() override {
    tracker_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

  // Adds a tab and activates it. Returns the created WebContents*.
  content::WebContents* AppendTabAndActivate(const GURL& url) {
    AddTab(browser(), url);
    // AddTab() activates the newly inserted tab at index 0. Re-activate
    // deterministically for clarity in tests.
    TabStripModel* strip = browser()->GetTabStripModel();
    strip->ActivateTabAt(0);
    return strip->GetActiveWebContents();
  }

  MahoMruTabTracker* tracker() { return tracker_.get(); }

 private:
  std::unique_ptr<MahoMruTabTracker> tracker_;
};

// Basic activation order: last-activated is at MRU front.
TEST_F(MahoMruTabTrackerTest, ActivationOrderIsMRU) {
  AddTab(browser(), GURL("http://a.test/"));
  AddTab(browser(), GURL("http://b.test/"));
  AddTab(browser(), GURL("http://c.test/"));

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_EQ(3, strip->count());

  strip->ActivateTabAt(2);
  strip->ActivateTabAt(0);
  strip->ActivateTabAt(1);

  auto mru = tracker()->GetMruList();
  ASSERT_EQ(3u, mru.size());
  EXPECT_EQ(strip->GetWebContentsAt(1), mru[0]);
  EXPECT_EQ(strip->GetWebContentsAt(0), mru[1]);
  EXPECT_EQ(strip->GetWebContentsAt(2), mru[2]);
}

// Closed tabs are removed from the MRU list.
TEST_F(MahoMruTabTrackerTest, RemovedTabDropsFromMru) {
  AddTab(browser(), GURL("http://a.test/"));
  AddTab(browser(), GURL("http://b.test/"));
  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_EQ(2, strip->count());

  content::WebContents* to_close = strip->GetWebContentsAt(1);
  strip->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);

  auto mru = tracker()->GetMruList();
  EXPECT_EQ(1u, mru.size());
  for (auto* wc : mru) {
    EXPECT_NE(to_close, wc);
  }
}

// Inserting a tab lands it at position [1], never displacing MRU[0].
TEST_F(MahoMruTabTrackerTest, NewTabInsertsAtPositionOne) {
  AddTab(browser(), GURL("http://a.test/"));
  TabStripModel* strip = browser()->GetTabStripModel();

  content::WebContents* first_active = strip->GetActiveWebContents();
  ASSERT_TRUE(first_active);

  // Insert a background tab (still selected by AddTab, so activate first
  // deterministically).
  AddTab(browser(), GURL("http://b.test/"));
  strip->ActivateTabAt(0);  // Pin activation to the older tab.

  auto mru = tracker()->GetMruList();
  ASSERT_GE(mru.size(), 2u);
  EXPECT_EQ(strip->GetWebContentsAt(0), mru[0]);
}

// Space filter with an empty space_id must return the full MRU list.
// (Guarantees that callers passing "" as a "no-op scope" don't accidentally
// get an empty list.)
TEST_F(MahoMruTabTrackerTest, GetMruListForSpaceEmptyIdReturnsFull) {
  AddTab(browser(), GURL("http://a.test/"));
  AddTab(browser(), GURL("http://b.test/"));
  auto full = tracker()->GetMruList();
  auto filtered = tracker()->GetMruListForSpace(std::string());
  EXPECT_EQ(full.size(), filtered.size());
}

// Space filter without a live MahoCore must not fall back to the global MRU:
// space membership is unknown, so a "current space" switch would otherwise
// jump into other spaces. It returns no candidates and the caller falls
// through to positional switching.
TEST_F(MahoMruTabTrackerTest, GetMruListForSpaceWithoutCoreReturnsEmpty) {
  AddTab(browser(), GURL("http://a.test/"));
  AddTab(browser(), GURL("http://b.test/"));
  ASSERT_GE(tracker()->GetMruList().size(), 2u);
  auto filtered =
      tracker()->GetMruListForSpace(std::string("unknown-space-id"));
  EXPECT_TRUE(filtered.empty());
}

// FilterBySpace is the rule set behind "Ctrl+Tab shows no preview": the
// switcher needs two candidates, and any tab dropped here counts against that.
TEST(MahoMruSpaceFilterTest, KeepsOnlyTabsMappedToTheSpace) {
  base::flat_map<std::string, std::string> map = {
      {"t1", "s1"}, {"t2", "s1"}, {"t3", "s2"}};
  auto r = MahoMruTabTracker::FilterBySpace({"t1", "t3", "t2"}, map, "s1");
  EXPECT_EQ((std::vector<size_t>{0, 2}), r.kept);
  EXPECT_EQ(1u, r.other_space);
  EXPECT_EQ(0u, r.unmapped);
  EXPECT_EQ(0u, r.unidentified);
}

TEST(MahoMruSpaceFilterTest, ClassifiesWhyEachDroppedTabWasDropped) {
  base::flat_map<std::string, std::string> map = {{"t1", "s1"}, {"t2", "s2"}};
  auto r = MahoMruTabTracker::FilterBySpace(
      {"t1", "", "not-in-core", "t2"}, map, "s1");
  EXPECT_EQ((std::vector<size_t>{0}), r.kept);
  EXPECT_EQ(1u, r.unidentified);
  EXPECT_EQ(1u, r.unmapped);
  EXPECT_EQ(1u, r.other_space);
}

// A tab the core has not mapped yet is dropped, so a window whose tabs are
// all unmapped yields zero candidates and the switcher declines. The cache
// is only rebuilt on tab-strip changes, which is why this is worth logging.
TEST(MahoMruSpaceFilterTest, UnmappedTabsAreNotCandidates) {
  base::flat_map<std::string, std::string> map = {{"other", "s1"}};
  auto r = MahoMruTabTracker::FilterBySpace({"a", "b", "c"}, map, "s1");
  EXPECT_TRUE(r.kept.empty());
  EXPECT_EQ(3u, r.unmapped);
}

TEST(MahoMruSpaceFilterTest, EmptyInputKeepsNothing) {
  auto r = MahoMruTabTracker::FilterBySpace({}, {}, "s1");
  EXPECT_TRUE(r.kept.empty());
}

}  // namespace maho
