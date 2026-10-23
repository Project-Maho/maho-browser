// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"

#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

DownloadItem MakeDownload(const std::string& id,
                          const std::string& state,
                          uint64_t total_bytes,
                          uint64_t received_bytes) {
  DownloadItem item;
  item.id = id;
  item.filename = base::UTF8ToUTF16(id + ".bin");
  item.url = "https://example.com/" + id;
  item.state = state;
  item.total_bytes = total_bytes;
  item.received_bytes = received_bytes;
  return item;
}

TEST(MahoDownloadsIndicatorStateTest, NoDownloadsIsCleared) {
  const DownloadsIndicatorState state = ComputeDownloadsIndicatorState({});

  EXPECT_FALSE(state.visible);
  EXPECT_FALSE(state.indeterminate);
  EXPECT_EQ(0, state.active_count);
  EXPECT_EQ(0u, state.total_bytes);
  EXPECT_EQ(0.0, state.fraction);
  EXPECT_TRUE(DownloadsIndicatorAccessibleDescription(state).empty());
}

// Every terminal row must stop contributing, including the states the download
// rows already treat as finished.
TEST(MahoDownloadsIndicatorStateTest, TerminalDownloadsDoNotShowTheIndicator) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("done", "completed", 1000, 1000),
      MakeDownload("cancelled", "cancelled", 1000, 10),
      MakeDownload("failed", "failed", 1000, 10),
      MakeDownload("interrupted", "interrupted", 1000, 10),
  };

  const DownloadsIndicatorState state =
      ComputeDownloadsIndicatorState(downloads);

  EXPECT_FALSE(state.visible);
  EXPECT_EQ(0, state.active_count);
  EXPECT_EQ(0u, state.received_bytes);
  EXPECT_EQ(0u, state.total_bytes);
}

TEST(MahoDownloadsIndicatorStateTest, ByteWeightsMultipleActiveDownloads) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("a", "downloading", 200, 100),
      MakeDownload("b", "downloading", 400, 300),
  };

  const DownloadsIndicatorState state =
      ComputeDownloadsIndicatorState(downloads);

  EXPECT_TRUE(state.visible);
  EXPECT_FALSE(state.indeterminate);
  EXPECT_EQ(2, state.active_count);
  EXPECT_EQ(400u, state.received_bytes);
  EXPECT_EQ(600u, state.total_bytes);
  EXPECT_DOUBLE_EQ(400.0 / 600.0, state.fraction);
}

// A zero total means the size is not known yet: the aggregate must not pretend
// to have a fraction, and the unknown row must not weight the byte sums.
TEST(MahoDownloadsIndicatorStateTest, UnknownTotalIsIndeterminate) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("unknown", "downloading", 0, 4096),
  };

  const DownloadsIndicatorState state =
      ComputeDownloadsIndicatorState(downloads);

  EXPECT_TRUE(state.visible);
  EXPECT_TRUE(state.indeterminate);
  EXPECT_EQ(1, state.active_count);
  EXPECT_EQ(0u, state.received_bytes);
  EXPECT_EQ(0u, state.total_bytes);
  EXPECT_EQ(0.0, state.fraction);
}

TEST(MahoDownloadsIndicatorStateTest,
     MixedKnownAndUnknownTotalIsIndeterminate) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("known", "downloading", 1000, 500),
      MakeDownload("unknown", "downloading", 0, 250),
  };

  const DownloadsIndicatorState state =
      ComputeDownloadsIndicatorState(downloads);

  EXPECT_TRUE(state.visible);
  EXPECT_TRUE(state.indeterminate);
  EXPECT_EQ(2, state.active_count);
  // The known row is still byte-weighted; only the fraction is withheld.
  EXPECT_EQ(500u, state.received_bytes);
  EXPECT_EQ(1000u, state.total_bytes);
  EXPECT_EQ(0.0, state.fraction);
}

TEST(MahoDownloadsIndicatorStateTest, FractionIsClampedWhenReceivedExceedsTotal) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("over", "downloading", 100, 400),
  };

  const DownloadsIndicatorState state =
      ComputeDownloadsIndicatorState(downloads);

  EXPECT_TRUE(state.visible);
  EXPECT_DOUBLE_EQ(1.0, state.fraction);
}

TEST(MahoDownloadsIndicatorStateTest, PausedDownloadStillCountsAsActive) {
  const DownloadsIndicatorState paused = ComputeDownloadsIndicatorState(
      {MakeDownload("p", "paused", 1000, 250)});

  EXPECT_TRUE(paused.visible);
  EXPECT_FALSE(paused.indeterminate);
  EXPECT_EQ(1, paused.active_count);
  EXPECT_DOUBLE_EQ(0.25, paused.fraction);

  const DownloadsIndicatorState resumed = ComputeDownloadsIndicatorState(
      {MakeDownload("p", "downloading", 1000, 250)});

  EXPECT_EQ(paused.active_count, resumed.active_count);
  EXPECT_DOUBLE_EQ(paused.fraction, resumed.fraction);
}

TEST(MahoDownloadsIndicatorStateTest, CompletionClearsTheIndicator) {
  const DownloadsIndicatorState active = ComputeDownloadsIndicatorState(
      {MakeDownload("d", "downloading", 1000, 900)});
  ASSERT_TRUE(active.visible);

  const DownloadsIndicatorState completed = ComputeDownloadsIndicatorState(
      {MakeDownload("d", "completed", 1000, 1000)});

  EXPECT_FALSE(completed.visible);
  EXPECT_EQ(0, completed.active_count);
}

TEST(MahoDownloadsIndicatorStateTest, CancellationClearsTheIndicator) {
  const DownloadsIndicatorState cancelled = ComputeDownloadsIndicatorState(
      {MakeDownload("d", "cancelled", 1000, 10)});

  EXPECT_FALSE(cancelled.visible);
}

TEST(MahoDownloadsIndicatorStateTest, FailureClearsTheIndicator) {
  const DownloadsIndicatorState failed = ComputeDownloadsIndicatorState(
      {MakeDownload("d", "failed", 1000, 10)});

  EXPECT_FALSE(failed.visible);
}

TEST(MahoDownloadsIndicatorStateTest, OneRemainingActiveDownloadKeepsItVisible) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("done", "completed", 1000, 1000),
      MakeDownload("live", "downloading", 1000, 500),
      MakeDownload("gone", "cancelled", 1000, 10),
  };

  const DownloadsIndicatorState state =
      ComputeDownloadsIndicatorState(downloads);

  EXPECT_TRUE(state.visible);
  EXPECT_EQ(1, state.active_count);
  EXPECT_EQ(1000u, state.total_bytes);
  EXPECT_EQ(500u, state.received_bytes);
  EXPECT_DOUBLE_EQ(0.5, state.fraction);
}

TEST(MahoDownloadsIndicatorAccessibleDescriptionTest,
     NamesDeterminateProgress) {
  const DownloadsIndicatorState state = ComputeDownloadsIndicatorState(
      {MakeDownload("a", "downloading", 1000, 420)});

  const std::u16string description =
      DownloadsIndicatorAccessibleDescription(state);

  EXPECT_NE(std::u16string::npos, description.find(u"Download in progress"));
  EXPECT_NE(std::u16string::npos, description.find(u"42% complete"));
  EXPECT_EQ(std::u16string::npos, description.find(u"size unknown"));
}

TEST(MahoDownloadsIndicatorAccessibleDescriptionTest,
     CountsMultipleDownloads) {
  const std::vector<DownloadItem> downloads = {
      MakeDownload("a", "downloading", 500, 250),
      MakeDownload("b", "paused", 500, 250),
  };

  const std::u16string description = DownloadsIndicatorAccessibleDescription(
      ComputeDownloadsIndicatorState(downloads));

  EXPECT_NE(std::u16string::npos, description.find(u"2 downloads in progress"));
  EXPECT_NE(std::u16string::npos, description.find(u"50% complete"));
}

TEST(MahoDownloadsIndicatorAccessibleDescriptionTest, NamesUnknownSize) {
  const DownloadsIndicatorState state = ComputeDownloadsIndicatorState(
      {MakeDownload("a", "downloading", 0, 512)});

  const std::u16string description =
      DownloadsIndicatorAccessibleDescription(state);

  EXPECT_NE(std::u16string::npos, description.find(u"Download in progress"));
  EXPECT_NE(std::u16string::npos, description.find(u"size unknown"));
  EXPECT_EQ(std::u16string::npos, description.find(u"% complete"));
}

TEST(MahoDownloadsIndicatorAccessibleDescriptionTest, EmptyWhenCleared) {
  EXPECT_TRUE(DownloadsIndicatorAccessibleDescription(DownloadsIndicatorState())
                  .empty());
  EXPECT_TRUE(
      DownloadsIndicatorAccessibleDescription(ComputeDownloadsIndicatorState(
          {MakeDownload("a", "completed", 1000, 1000)}))
          .empty());
}

}  // namespace
}  // namespace maho
