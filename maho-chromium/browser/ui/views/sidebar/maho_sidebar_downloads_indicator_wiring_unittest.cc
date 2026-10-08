// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_profile.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/mock_download_item.h"
#include "content/public/browser/download_manager.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;

// Swaps the process-global MahoCore for a test-owned instance for the test's
// lifetime, mirroring maho_download_bridge_service_unittest.cc.
class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(GetCore()) {
    SetCore(core);
  }
  ~ScopedCoreOverride() { SetCore(saved_); }

 private:
  raw_ptr<MahoCore> saved_;
};

void ConfigureItem(NiceMock<download::MockDownloadItem>& item,
                   const std::string& guid,
                   const std::string& url,
                   const std::string& report_name,
                   download::DownloadItem::DownloadState state,
                   int64_t total_bytes,
                   int64_t received_bytes,
                   bool can_resume) {
  ON_CALL(item, GetGuid()).WillByDefault(ReturnRefOfCopy(guid));
  ON_CALL(item, GetURL()).WillByDefault(ReturnRefOfCopy(GURL(url)));
  ON_CALL(item, GetTargetFilePath())
      .WillByDefault(ReturnRefOfCopy(base::FilePath()));
  ON_CALL(item, GetFileNameToReportUser())
      .WillByDefault(Return(base::FilePath::FromUTF8Unsafe(report_name)));
  ON_CALL(item, GetState()).WillByDefault(Return(state));
  ON_CALL(item, GetTotalBytes()).WillByDefault(Return(total_bytes));
  ON_CALL(item, GetReceivedBytes()).WillByDefault(Return(received_bytes));
  ON_CALL(item, GetMimeType()).WillByDefault(Return(std::string()));
  ON_CALL(item, CanResume()).WillByDefault(Return(can_resume));
}

// Drives the real sidebar against the real per-profile download bridge. The
// Downloads pane is deliberately never opened: the rail icon must track
// progress on its own notifications.
class MahoSidebarDownloadsIndicatorWiringTest
    : public BrowserWithTestWindowTest {
 public:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    core_ = maho_core_new();
    core_override_ = std::make_unique<ScopedCoreOverride>(core_);

    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.context = GetContext();
    params.bounds = gfx::Rect(0, 0, 280, 900);
    widget_->Init(std::move(params));
    sidebar_ = widget_->SetContentsView(
        std::make_unique<MahoSidebarView>(browser()));
    sidebar_->SetBounds(0, 0, 280, 900);
    sidebar_->DeprecatedLayoutImmediately();
  }

  void TearDown() override {
    sidebar_ = nullptr;
    widget_.reset();
    DrainCoreTasks();
    BrowserWithTestWindowTest::TearDown();
    core_override_.reset();
    maho_core_free(core_);
  }

 protected:
  // Waits for sidebar work already queued on the core sequence, so the
  // test-owned core is never released while a background build still reads it.
  void DrainCoreTasks() {
    base::test::TestFuture<void> drained;
    GetCoreTaskRunner()->PostTaskAndReply(FROM_HERE, base::DoNothing(),
                                          drained.GetCallback());
    EXPECT_TRUE(drained.Wait());
  }
  MahoDownloadBridgeService* bridge() {
    return MahoDownloadBridgeServiceFactory::GetForProfile(
        browser()->GetProfile());
  }

  content::DownloadManager* download_manager() {
    return browser()->GetProfile()->GetDownloadManager();
  }

  MahoSidebarLibraryRailView* rail() {
    return sidebar_->library_rail_view_for_testing();
  }

  const DownloadsIndicatorState& indicator_state() {
    return rail()->downloads_indicator_state_for_testing();
  }

  MahoCore* core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> core_override_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoSidebarView> sidebar_ = nullptr;
};

TEST_F(MahoSidebarDownloadsIndicatorWiringTest,
       RailIndicatorTracksDownloadsWhilePaneClosed) {
  ASSERT_NE(nullptr, bridge());
  ASSERT_NE(nullptr, rail());
  EXPECT_EQ(nullptr, sidebar_->downloads_view_for_testing());
  EXPECT_FALSE(indicator_state().visible);

  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "guid-rail-1", "https://example.com/rail.bin", "rail.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 0,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &item);

  EXPECT_TRUE(indicator_state().visible);
  EXPECT_EQ(1, indicator_state().active_count);
  EXPECT_EQ(1000u, indicator_state().total_bytes);
  EXPECT_EQ(0u, indicator_state().received_bytes);
  EXPECT_NE(nullptr, rail()->downloads_indicator_for_testing());
  EXPECT_EQ(nullptr, sidebar_->downloads_view_for_testing());
}

TEST_F(MahoSidebarDownloadsIndicatorWiringTest,
       ByteWeightedProgressArrivesThroughTheThrottledNotification) {
  NiceMock<download::MockDownloadItem> first;
  NiceMock<download::MockDownloadItem> second;
  ConfigureItem(first, "guid-a", "https://example.com/a.bin", "a.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 0,
                /*can_resume=*/false);
  ConfigureItem(second, "guid-b", "https://example.com/b.bin", "b.bin",
                download::DownloadItem::IN_PROGRESS, 3000, 0,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &first);
  bridge()->OnDownloadCreated(download_manager(), &second);
  ASSERT_EQ(2, indicator_state().active_count);

  // Progress-only updates are throttled, so await the aggregate instead of
  // sleeping on the bridge's timer.
  ConfigureItem(first, "guid-a", "https://example.com/a.bin", "a.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 250,
                /*can_resume=*/false);
  ConfigureItem(second, "guid-b", "https://example.com/b.bin", "b.bin",
                download::DownloadItem::IN_PROGRESS, 3000, 1500,
                /*can_resume=*/false);
  bridge()->OnDownloadUpdated(download_manager(), &first);
  bridge()->OnDownloadUpdated(download_manager(), &second);

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return indicator_state().received_bytes == 1750;
  }));
  EXPECT_TRUE(indicator_state().visible);
  EXPECT_EQ(2, indicator_state().active_count);
  EXPECT_EQ(4000u, indicator_state().total_bytes);
  EXPECT_DOUBLE_EQ(0.4375, indicator_state().fraction);
}

TEST_F(MahoSidebarDownloadsIndicatorWiringTest,
       IndicatorClearsWhenTheLastActiveDownloadEnds) {
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "guid-done", "https://example.com/done.bin", "done.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 500,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &item);
  ASSERT_TRUE(indicator_state().visible);

  ConfigureItem(item, "guid-done", "https://example.com/done.bin", "done.bin",
                download::DownloadItem::COMPLETE, 1000, 1000,
                /*can_resume=*/false);
  bridge()->OnDownloadUpdated(download_manager(), &item);

  EXPECT_FALSE(indicator_state().visible);
  EXPECT_EQ(0, indicator_state().active_count);
}

TEST_F(MahoSidebarDownloadsIndicatorWiringTest,
       CancellationAndFailureClearTheIndicator) {
  NiceMock<download::MockDownloadItem> cancelled;
  ConfigureItem(cancelled, "guid-cancel", "https://example.com/c.bin", "c.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 100,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &cancelled);
  ASSERT_TRUE(indicator_state().visible);

  ConfigureItem(cancelled, "guid-cancel", "https://example.com/c.bin", "c.bin",
                download::DownloadItem::CANCELLED, 1000, 100,
                /*can_resume=*/false);
  bridge()->OnDownloadUpdated(download_manager(), &cancelled);
  EXPECT_FALSE(indicator_state().visible);

  // An unrecoverable interruption is surfaced as a cancelled transfer.
  NiceMock<download::MockDownloadItem> interrupted;
  ConfigureItem(interrupted, "guid-fail", "https://example.com/f.bin", "f.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 100,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &interrupted);
  ASSERT_TRUE(indicator_state().visible);

  ConfigureItem(interrupted, "guid-fail", "https://example.com/f.bin", "f.bin",
                download::DownloadItem::INTERRUPTED, 1000, 100,
                /*can_resume=*/false);
  bridge()->OnDownloadUpdated(download_manager(), &interrupted);
  EXPECT_FALSE(indicator_state().visible);
}

TEST_F(MahoSidebarDownloadsIndicatorWiringTest,
       OffTheRecordSidebarNeverReadsRegularDownloadProgress) {
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "guid-otr", "https://example.com/otr.bin", "otr.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 250,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &item);
  ASSERT_TRUE(indicator_state().visible);

  TestingProfile* incognito_profile =
      TestingProfile::Builder().BuildIncognito(browser()->GetProfile());
  ASSERT_TRUE(incognito_profile->IsOffTheRecord());
  EXPECT_FALSE(MahoIsCapabilityAllowed(
      incognito_profile, MahoPrivateCapability::kMahoDownloadMetadata));
  EXPECT_EQ(nullptr, MahoDownloadBridgeServiceFactory::GetForProfileIfExists(
                         incognito_profile));

  sidebar_->SetPrivateFlagsForTesting(/*is_private=*/true, /*is_otr=*/true);
  sidebar_->OnMahoDownloadsChanged();
  EXPECT_FALSE(indicator_state().visible);
}

TEST_F(MahoSidebarDownloadsIndicatorWiringTest,
       ObserverTeardownSurvivesSidebarDestruction) {
  ASSERT_NE(nullptr, bridge());

  {
    auto extra_widget = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.context = GetContext();
    params.bounds = gfx::Rect(0, 0, 280, 900);
    extra_widget->Init(std::move(params));
    auto* extra_sidebar = extra_widget->SetContentsView(
        std::make_unique<MahoSidebarView>(browser()));
    ASSERT_NE(nullptr, extra_sidebar->library_rail_view_for_testing());
    EXPECT_FALSE(extra_sidebar->library_rail_view_for_testing()
                     ->downloads_indicator_state_for_testing()
                     .visible);
  }

  // Notifying after the extra sidebar is gone must reach only the survivor.
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "guid-teardown", "https://example.com/t.bin", "t.bin",
                download::DownloadItem::IN_PROGRESS, 1000, 250,
                /*can_resume=*/false);
  bridge()->OnDownloadCreated(download_manager(), &item);

  EXPECT_TRUE(indicator_state().visible);
  EXPECT_EQ(1, indicator_state().active_count);
}

}  // namespace
}  // namespace maho
