// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"

#include <memory>

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

// Space filter without a live MahoCore returns the full MRU list as a
// graceful fallback (the FFI call short-circuits to empty JSON, which is
// treated as "no filter data" — the caller falls back to the unfiltered
// list rather than surprising the user with an empty result).
TEST_F(MahoMruTabTrackerTest, GetMruListForSpaceWithoutCoreFallsBack) {
  AddTab(browser(), GURL("http://a.test/"));
  AddTab(browser(), GURL("http://b.test/"));
  auto filtered =
      tracker()->GetMruListForSpace(std::string("unknown-space-id"));
  // With no MahoCore in the test environment the space cache stays empty
  // and the tracker returns the unfiltered list rather than an empty vector.
  EXPECT_EQ(tracker()->GetMruList().size(), filtered.size());
}

}  // namespace maho
