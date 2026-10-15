#include "maho/browser/ai/maho_browse_episode_observer.h"

#include "base/test/simple_test_tick_clock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using EventResult = MahoBrowseEpisodeDwellPolicy::EventResult;

class MahoBrowseEpisodeObserverTest : public testing::Test {
 protected:
  void Advance(base::TimeDelta delta) { clock_.Advance(delta); }

  base::SimpleTestTickClock clock_;
  MahoBrowseEpisodeDwellPolicy policy_;
};

TEST_F(MahoBrowseEpisodeObserverTest, DwellReachedRecordsOnce) {
  EXPECT_EQ(EventResult::kStartDwell,
            policy_.OnNavigation("doc-1", "https://example.com/", false,
                                 clock_.NowTicks()));
  Advance(base::Seconds(20));
  EXPECT_EQ(EventResult::kRecord, policy_.OnClock(clock_.NowTicks()));
  EXPECT_EQ(EventResult::kIgnore, policy_.OnClock(clock_.NowTicks()));
}

TEST_F(MahoBrowseEpisodeObserverTest, LeavingBeforeDwellRecordsNothing) {
  policy_.OnNavigation("doc-1", "https://example.com/", false,
                       clock_.NowTicks());
  Advance(base::Seconds(19));
  EXPECT_EQ(EventResult::kIgnore,
            policy_.OnVisibilityChanged(false, clock_.NowTicks()));
  Advance(base::Seconds(5));
  EXPECT_EQ(EventResult::kIgnore, policy_.OnClock(clock_.NowTicks()));
}

TEST_F(MahoBrowseEpisodeObserverTest, IncognitoRecordsNothing) {
  EXPECT_EQ(EventResult::kIgnore,
            policy_.OnNavigation("doc-1", "https://example.com/", true,
                                 clock_.NowTicks()));
  Advance(base::Seconds(25));
  EXPECT_EQ(EventResult::kIgnore, policy_.OnClock(clock_.NowTicks()));
}

TEST_F(MahoBrowseEpisodeObserverTest, NonHttpRecordsNothing) {
  EXPECT_EQ(EventResult::kIgnore,
            policy_.OnNavigation("doc-1", "file:///tmp/page.html", false,
                                 clock_.NowTicks()));
  Advance(base::Seconds(25));
  EXPECT_EQ(EventResult::kIgnore, policy_.OnClock(clock_.NowTicks()));
}

TEST_F(MahoBrowseEpisodeObserverTest, SameDocumentNavigationRecordsOnce) {
  policy_.OnNavigation("doc-1", "https://example.com/", false,
                       clock_.NowTicks());
  Advance(base::Seconds(20));
  EXPECT_EQ(EventResult::kRecord, policy_.OnClock(clock_.NowTicks()));
  EXPECT_EQ(EventResult::kIgnore,
            policy_.OnNavigation("doc-1", "https://example.com/#section",
                                 false, clock_.NowTicks()));
  EXPECT_EQ(EventResult::kIgnore, policy_.OnClock(clock_.NowTicks()));
}

}
