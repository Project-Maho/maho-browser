// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_control_activity_service.h"

#include <string>
#include <vector>

#include "base/test/task_environment.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho::ai {
namespace {

constexpr char kSecret[] = "MAHO_SECRET_SENTINEL_9f2c";

ActivityReceipt Receipt(ActivityReceiptKind kind, std::string code,
                        std::string summary) {
  return {.kind = kind, .code = std::move(code), .summary = std::move(summary)};
}

ControlTarget Target(int64_t window_id, int64_t tab_id, const char *origin) {
  return {.window_id = window_id,
          .tab_id = tab_id,
          .origin = url::Origin::Create(GURL(origin))};
}

StartControlActivityParams StartParams(std::string session_id,
                                       int64_t tab_id = 7) {
  StartControlActivityParams params;
  params.session_id = std::move(session_id);
  params.controller_display_name = "Maho Agent";
  params.controller_type = ControllerType::kUserAgent;
  params.control_plane = ControlPlane::kLocalAgent;
  params.target = Target(3, tab_id, "https://example.test");
  params.category = ActivityCategory::kFormEntry;
  params.sensitivity = ActivitySensitivity::kMedium;
  params.lease_duration = base::Seconds(30);
  params.start_receipt = Receipt(ActivityReceiptKind::kStart, "navigation",
                                 "Opening account settings");
  return params;
}

class RecordingObserver : public MahoControlActivityService::Observer {
 public:
  void OnControlActivityChanged(const ControlActivity& activity) override {
    events.push_back(activity);
  }

  std::vector<ControlActivity> events;
};

class MahoControlActivityServiceTest : public testing::Test {
 protected:
  MahoControlActivityServiceTest()
      : task_environment_(
            content::BrowserTaskEnvironment::TimeSource::MOCK_TIME),
        service_(&profile_, task_environment_.GetMockTickClock()) {}

  bool Update(const char* session_id,
              uint64_t revision,
              ControlActivityState state,
              ApprovalOutcome approval = ApprovalOutcome::kNotRequested,
              std::optional<ActivityReceipt> receipt = std::nullopt) {
    ControlActivityUpdate update;
    update.event_revision = revision;
    update.state = state;
    update.approval_outcome = approval;
    update.lease_duration = base::Seconds(30);
    update.receipt = std::move(receipt);
    return service_.ApplyUpdate(session_id, std::move(update));
  }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
  MahoControlActivityService service_;
};

TEST_F(MahoControlActivityServiceTest,
       BrowserContextFactoryOwnsOneServicePerRegularProfile) {
  EnsureMahoControlActivityServiceFactoryBuilt();
  TestingProfile first;
  TestingProfile second;
  auto *first_service = MahoControlActivityService::GetForProfile(&first);
  EXPECT_NE(first_service, nullptr);
  EXPECT_EQ(first_service, MahoControlActivityService::GetForProfile(&first));
  EXPECT_NE(first_service, MahoControlActivityService::GetForProfile(&second));
  EXPECT_EQ(MahoControlActivityService::GetForProfile(nullptr), nullptr);
  EXPECT_EQ(
      MahoControlActivityService::GetForProfile(first.GetOffTheRecordProfile(
          Profile::OTRProfileID::PrimaryID(), true)),
      nullptr);
}

TEST_F(MahoControlActivityServiceTest, AcceptsLegalEndToEndTransitions) {
  ASSERT_TRUE(service_.StartActivity(StartParams("fixture")));
  EXPECT_TRUE(Update("fixture", 2, ControlActivityState::kActing));
  EXPECT_TRUE(Update("fixture", 3, ControlActivityState::kWaitingApproval,
                     ApprovalOutcome::kPending));
  EXPECT_TRUE(Update("fixture", 4, ControlActivityState::kActing,
                     ApprovalOutcome::kApproved));
  EXPECT_TRUE(Update("fixture", 5, ControlActivityState::kCompleted,
                     ApprovalOutcome::kApproved,
                     Receipt(ActivityReceiptKind::kResult, "done",
                             "Account settings opened")));

  const auto activity = service_.GetActivity("fixture");
  ASSERT_TRUE(activity.has_value());
  EXPECT_EQ(activity->state, ControlActivityState::kCompleted);
  EXPECT_EQ(activity->approval_outcome, ApprovalOutcome::kApproved);
  ASSERT_EQ(activity->receipts.size(), 2u);
  EXPECT_EQ(activity->receipts.back().code, "done");
}

TEST_F(MahoControlActivityServiceTest,
       RejectsStaleOutOfOrderAndIllegalUpdates) {
  ASSERT_TRUE(service_.StartActivity(StartParams("stale")));
  ASSERT_TRUE(Update("stale", 2, ControlActivityState::kActing));
  EXPECT_FALSE(Update("stale", 2, ControlActivityState::kPaused));
  EXPECT_FALSE(Update("stale", 1, ControlActivityState::kPaused));
  EXPECT_FALSE(Update("stale", 4, ControlActivityState::kCompleted));
  EXPECT_TRUE(Update("stale", 3, ControlActivityState::kPaused));
  EXPECT_FALSE(Update("stale", 4, ControlActivityState::kReading));
}

TEST_F(MahoControlActivityServiceTest,
       PublishesExactlyOnceWithMonotonicObserverRevisions) {
  RecordingObserver observer;
  service_.AddObserver(&observer);
  ASSERT_TRUE(service_.StartActivity(StartParams("observed")));
  ASSERT_TRUE(Update("observed", 2, ControlActivityState::kActing));
  EXPECT_FALSE(Update("observed", 2, ControlActivityState::kPaused));
  ASSERT_TRUE(Update("observed", 3, ControlActivityState::kCompleted));
  service_.OnTargetClosed(7);

  ASSERT_EQ(observer.events.size(), 3u);
  EXPECT_EQ(observer.events[0].observer_revision, 1u);
  EXPECT_EQ(observer.events[1].observer_revision, 2u);
  EXPECT_EQ(observer.events[2].observer_revision, 3u);
  service_.RemoveObserver(&observer);
}

TEST_F(MahoControlActivityServiceTest,
       LeaseExpiryUsesDeterministicBoundaryAndDisconnects) {
  RecordingObserver observer;
  service_.AddObserver(&observer);
  auto params = StartParams("expiry");
  params.lease_duration = base::Seconds(10);
  ASSERT_TRUE(service_.StartActivity(std::move(params)));

  task_environment_.FastForwardBy(base::Seconds(9));
  EXPECT_EQ(service_.GetActivity("expiry")->state,
            ControlActivityState::kReading);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(service_.GetActivity("expiry")->state,
            ControlActivityState::kDisconnected);
  ASSERT_EQ(observer.events.size(), 2u);
  EXPECT_EQ(observer.events.back().event_revision, 2u);
  service_.RemoveObserver(&observer);
}

TEST_F(MahoControlActivityServiceTest,
       DisconnectBeforeTargetBindingThenBindingIsRejected) {
  auto params = StartParams("unbound");
  params.target.reset();
  ASSERT_TRUE(service_.StartActivity(std::move(params)));
  ASSERT_TRUE(service_.Disconnect("unbound", 2));
  EXPECT_FALSE(service_.BindTarget(
      "unbound", 3, Target(3, 8, "https://example.test"), base::Seconds(30)));
}

TEST_F(MahoControlActivityServiceTest, TracksMultipleControllersIndependently) {
  auto first = StartParams("local");
  auto second = StartParams("remote", 8);
  second.controller_display_name = "QA Controller";
  second.controller_type = ControllerType::kRemoteClient;
  second.control_plane = ControlPlane::kMcp;
  ASSERT_TRUE(service_.StartActivity(std::move(first)));
  ASSERT_TRUE(service_.StartActivity(std::move(second)));
  ASSERT_TRUE(Update("local", 2, ControlActivityState::kActing));

  const auto activities = service_.GetActivities();
  ASSERT_EQ(activities.size(), 2u);
  EXPECT_EQ(service_.GetActivity("local")->state,
            ControlActivityState::kActing);
  EXPECT_EQ(service_.GetActivity("remote")->state,
            ControlActivityState::kReading);
  EXPECT_EQ(service_.GetActivity("remote")->controller_display_name,
            "QA Controller");
}

TEST_F(MahoControlActivityServiceTest, TargetClosureDisconnectsMidActionOnce) {
  RecordingObserver observer;
  service_.AddObserver(&observer);
  ASSERT_TRUE(service_.StartActivity(StartParams("closing")));
  ASSERT_TRUE(Update("closing", 2, ControlActivityState::kActing));
  service_.OnTargetClosed(7);
  service_.OnTargetClosed(7);

  const auto activity = service_.GetActivity("closing");
  ASSERT_TRUE(activity.has_value());
  EXPECT_EQ(activity->state, ControlActivityState::kDisconnected);
  ASSERT_EQ(observer.events.size(), 3u);
  EXPECT_EQ(observer.events.back().event_revision, 3u);
  service_.RemoveObserver(&observer);
}

TEST_F(MahoControlActivityServiceTest, ReadCanCompleteWithTerminalReceipt) {
  ASSERT_TRUE(service_.StartActivity(StartParams("read")));
  ASSERT_TRUE(Update("read", 2, ControlActivityState::kCompleted,
                     ApprovalOutcome::kNotRequested,
                     Receipt(ActivityReceiptKind::kResult, "completed", "")));

  const auto activity = service_.GetActivity("read");
  ASSERT_TRUE(activity.has_value());
  EXPECT_EQ(activity->state, ControlActivityState::kCompleted);
  ASSERT_EQ(activity->receipts.size(), 2u);
  EXPECT_EQ(activity->receipts.back().code, "completed");
}

TEST_F(MahoControlActivityServiceTest,
       RedactsSecretsAndRejectsDuplicateTerminalEvent) {
  auto params = StartParams("secret");
  params.start_receipt.summary = std::string("token=") + kSecret;
  ASSERT_TRUE(service_.StartActivity(std::move(params)));
  ASSERT_TRUE(Update("secret", 2, ControlActivityState::kFailed,
                     ApprovalOutcome::kNotRequested,
                     Receipt(ActivityReceiptKind::kFailure, "provider_error",
                             std::string("credential ") + kSecret)));
  EXPECT_FALSE(Update("secret", 3, ControlActivityState::kCompleted));

  const auto activity = service_.GetActivity("secret");
  ASSERT_TRUE(activity.has_value());
  ASSERT_EQ(activity->receipts.size(), 2u);
  for (const auto &receipt : activity->receipts) {
    EXPECT_EQ(receipt.summary.find(kSecret), std::string::npos);
    EXPECT_EQ(receipt.summary, "[redacted]");
  }
}

} // namespace
} // namespace maho::ai
