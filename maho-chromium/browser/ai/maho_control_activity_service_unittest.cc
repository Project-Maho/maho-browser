// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_control_activity_service.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/bind.h"
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

TEST(MahoControlActivityStateTest, TerminalStatesNoLongerControlTarget) {
  EXPECT_FALSE(IsTerminalControlActivityState(ControlActivityState::kReading));
  EXPECT_FALSE(IsTerminalControlActivityState(ControlActivityState::kActing));
  EXPECT_FALSE(
      IsTerminalControlActivityState(ControlActivityState::kWaitingApproval));
  EXPECT_FALSE(IsTerminalControlActivityState(ControlActivityState::kPaused));
  EXPECT_TRUE(
      IsTerminalControlActivityState(ControlActivityState::kDisconnected));
  EXPECT_TRUE(IsTerminalControlActivityState(ControlActivityState::kCompleted));
  EXPECT_TRUE(IsTerminalControlActivityState(ControlActivityState::kFailed));
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

AgentActionJournalRecord JournalRecord(std::string action,
                                       std::string outcome = "completed") {
  AgentActionJournalRecord record;
  record.timestamp_seconds = 1700000000;
  record.session_id = "journal-session";
  record.actor = "panel";
  record.action = std::move(action);
  record.origin = "https://shop.example";
  record.summary = "agent action";
  record.outcome = std::move(outcome);
  return record;
}

class ScopedJournalSinks {
 public:
  ScopedJournalSinks(MahoControlActivityService::AgentActionJournalWriter w,
                     MahoControlActivityService::AgentActionNoticePresenter p) {
    MahoControlActivityService::SetAgentActionJournalWriter(std::move(w));
    MahoControlActivityService::SetAgentActionNoticePresenter(std::move(p));
  }
  ~ScopedJournalSinks() {
    MahoControlActivityService::SetAgentActionJournalWriter({});
    MahoControlActivityService::SetAgentActionNoticePresenter({});
  }
};

std::optional<std::string> Classify(std::string_view tool,
                                    base::DictValue args,
                                    std::string_view consequence = {},
                                    const char* current = nullptr) {
  JournalActionInput input;
  input.tool_name = tool;
  input.arguments = &args;
  input.action_consequence = consequence;
  if (current) {
    input.current_origin = url::Origin::Create(GURL(current));
  }
  return ClassifyJournalAction(input);
}

TEST(MahoAgentActionJournalMappingTest, ClassifiesRealCatalogToolsByArgs) {
  EXPECT_EQ(Classify("mail_queue_email", base::DictValue()), "mail_send");
  EXPECT_EQ(Classify("vault_fill_totp", base::DictValue()),
            "credential_typing");
  EXPECT_EQ(Classify("browser_type", base::DictValue()
                                         .Set("allow_credentials", true)
                                         .Set("submit", true)),
            "credential_typing");
  EXPECT_EQ(Classify("browser_type", base::DictValue().Set("submit", true)),
            "form_submit");
  EXPECT_FALSE(Classify("browser_type", base::DictValue()).has_value());
  EXPECT_EQ(Classify("browser_act_and_observe",
                     base::DictValue().Set("action", "type").Set("submit", true)),
            "form_submit");
  EXPECT_EQ(Classify("browser_click", base::DictValue(), "submit"),
            "form_submit");
  EXPECT_FALSE(
      Classify("browser_click", base::DictValue(), "ordinary").has_value());
  EXPECT_EQ(Classify("browser_navigate",
                     base::DictValue().Set("url", "https://other.test/x"), {},
                     "https://example.test"),
            "cross_origin_navigation");
  EXPECT_FALSE(Classify("browser_navigate",
                        base::DictValue().Set("url", "https://example.test/x"),
                        {}, "https://example.test")
                   .has_value());
  EXPECT_FALSE(Classify("browser_navigate",
                        base::DictValue().Set("url", "https://other.test"))
                   .has_value())
      << "unknown current origin is not guessed";
  EXPECT_EQ(Classify("browser_act_and_observe",
                     base::DictValue()
                         .Set("action", "navigate")
                         .Set("url", "https://other.test"),
                     {}, "https://example.test"),
            "cross_origin_navigation");
  for (const char* phantom :
       {"browser_form_submit", "browser_submit", "browser_purchase",
        "browser_checkout", "browser_navigate_cross_origin",
        "browser_file_upload", "mail_delete", "mail_delete_email",
        "mail_trash"}) {
    EXPECT_FALSE(JournalActionForTool(phantom).has_value()) << phantom;
  }
  EXPECT_TRUE(IsReservedJournalAction("purchase"));
  EXPECT_TRUE(IsReservedJournalAction("mail_delete"));
  EXPECT_FALSE(IsReservedJournalAction("mail_send"));
}

TEST(MahoAgentActionJournalMappingTest, MapsConsequentialToolsOnly) {
  EXPECT_EQ(JournalActionForTool("mail_send"), "mail_send");
  EXPECT_EQ(JournalActionForTool("vault_fill_credential"),
            "credential_typing");
  EXPECT_EQ(JournalActionForTool("browser_file_upload_select"),
            "file_upload");
  EXPECT_FALSE(JournalActionForTool("browser_scroll").has_value());
  EXPECT_TRUE(ShouldShowAgentActionNotice("purchase"));
  EXPECT_TRUE(ShouldShowAgentActionNotice("credential_typing"));
  EXPECT_TRUE(ShouldShowAgentActionNotice("mail_send"));
  EXPECT_FALSE(ShouldShowAgentActionNotice("form_submit"));
  EXPECT_EQ(JournalActorFor(ControlPlane::kLocalAgent,
                            ControllerType::kUserAgent),
            "panel");
  EXPECT_EQ(JournalActorFor(ControlPlane::kMcp, ControllerType::kAutomation),
            "cli");
  EXPECT_EQ(JournalActorFor(ControlPlane::kMcp, ControllerType::kRemoteClient),
            "mcp");
}

TEST_F(MahoControlActivityServiceTest, RecordsJournalAndRaisesNotice) {
  std::vector<AgentActionJournalRecord> written;
  int notices = 0;
  ScopedJournalSinks sinks(
      base::BindLambdaForTesting(
          [&](const AgentActionJournalRecord& r) {
            written.push_back(r);
            return true;
          }),
      base::BindLambdaForTesting(
          [&](Profile*, const AgentActionJournalRecord&) { ++notices; }));
  EXPECT_TRUE(service_.RecordAgentAction(JournalRecord("purchase")));
  EXPECT_TRUE(service_.RecordAgentAction(JournalRecord("form_submit")));
  ASSERT_EQ(written.size(), 2u);
  EXPECT_EQ(notices, 1) << "only purchase/credential/mail-send raise a notice";
}

TEST_F(MahoControlActivityServiceTest, JournalWriteFailureIsNonBlocking) {
  int notices = 0;
  ScopedJournalSinks sinks(
      base::BindRepeating([](const AgentActionJournalRecord&) { return false; }),
      base::BindLambdaForTesting(
          [&](Profile*, const AgentActionJournalRecord&) { ++notices; }));
  ASSERT_TRUE(service_.StartActivity(StartParams("journal-fail")));
  EXPECT_FALSE(service_.RecordAgentAction(JournalRecord("mail_send")));
  // The agent action still completes and the user is still told about it.
  EXPECT_TRUE(Update("journal-fail", 2, ControlActivityState::kCompleted,
                     ApprovalOutcome::kNotRequested,
                     Receipt(ActivityReceiptKind::kResult, "completed", "")));
  EXPECT_EQ(notices, 1);
}

TEST(MahoAgentActionJournalOtrTest, NothingRecordedFromOffTheRecordProfile) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  Profile* otr = profile.GetPrimaryOTRProfile(/*create_if_needed=*/true);
  MahoControlActivityService service(otr);
  int writes = 0;
  int notices = 0;
  ScopedJournalSinks sinks(
      base::BindLambdaForTesting([&](const AgentActionJournalRecord&) {
        ++writes;
        return true;
      }),
      base::BindLambdaForTesting(
          [&](Profile*, const AgentActionJournalRecord&) { ++notices; }));
  EXPECT_FALSE(service.RecordAgentAction(JournalRecord("purchase")));
  EXPECT_EQ(writes, 0);
  EXPECT_EQ(notices, 0);
}

} // namespace
} // namespace maho::ai
