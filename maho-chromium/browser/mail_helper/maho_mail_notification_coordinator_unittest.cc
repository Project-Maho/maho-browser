// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_notification_coordinator.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/task_environment.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class CoordinatorTest : public testing::Test {
 protected:
  void SetUp() override {
    sidebar_prefs::RegisterProfilePrefs(prefs_.registry());
    MahoMailService::RegisterProfilePrefs(prefs_.registry());

    prefs_.SetBoolean(sidebar_prefs::kMahoMailEnabled, true);
    behavior_.desktop_notifications = true;
    behavior_.notification_preview = MailNotificationPreview::kSenderSubject;
    permission_ = MailOsNotificationPermission::kGranted;
    coordinator_ = MakeCoordinator();
  }

  std::unique_ptr<MahoMailNotificationCoordinator> MakeCoordinator() {
    return std::make_unique<MahoMailNotificationCoordinator>(
        &prefs_, nullptr,
        base::BindRepeating(
            [](CoordinatorTest* self,
               const MahoMailNotificationPayload& payload,
               base::OnceCallback<void(bool)> completion) {
              self->displayed_.push_back(payload);
              if (self->auto_complete_displays_) {
                std::move(completion).Run(self->display_succeeds_);
              } else {
                self->display_completions_.push_back(std::move(completion));
              }
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](CoordinatorTest* self, const std::string& id) {
              self->withdrawn_.push_back(id);
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](CoordinatorTest* self, const std::string& account_id,
               const std::string& email_id) {
              self->routes_.emplace_back(account_id, email_id);
            },
            base::Unretained(this)),
        base::BindRepeating([](CoordinatorTest* self) {
          return self->behavior_;
        }, base::Unretained(this)),
        base::BindRepeating(
            [](CoordinatorTest* self,
               base::OnceCallback<void(MailOsNotificationPermission)>
                   callback) {
              if (self->auto_complete_permissions_) {
                std::move(callback).Run(self->permission_);
              } else {
                self->permission_callbacks_.push_back(std::move(callback));
              }
            },
            base::Unretained(this)));
  }

  MahoMailNotificationEvent Event(std::string account = "account-a",
                                  std::string email = "email-1",
                                  std::string message = "thread-1",
                                  uint64_t cursor = 1,
                                  uint64_t epoch = 100) {
    return {std::move(account), std::move(email), std::move(message),
            "Ada", "Deterministic subject", cursor, epoch, 0};
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple prefs_;
  MailBehaviorSnapshot behavior_;
  MailOsNotificationPermission permission_;
  bool auto_complete_permissions_ = true;
  bool auto_complete_displays_ = true;
  bool display_succeeds_ = true;
  std::vector<MahoMailNotificationPayload> displayed_;
  std::vector<base::OnceCallback<void(bool)>> display_completions_;
  std::vector<base::OnceCallback<void(MailOsNotificationPermission)>>
      permission_callbacks_;
  std::vector<std::string> withdrawn_;
  std::vector<std::pair<std::string, std::string>> routes_;
  std::unique_ptr<MahoMailNotificationCoordinator> coordinator_;
};

TEST_F(CoordinatorTest, AppliesPolicyDurabilityRoutingAndWithdrawal) {
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(displayed_.size(), 1u);
  EXPECT_EQ(displayed_[0].account_id, "account-a");
  EXPECT_EQ(displayed_[0].email_id, "email-1");
  EXPECT_EQ(displayed_[0].title, "Ada");
  EXPECT_EQ(displayed_[0].body, "Deterministic subject");

  displayed_[0].click_callback.Run();
  EXPECT_EQ(routes_,
            (std::vector<std::pair<std::string, std::string>>{
                {"account-a", "email-1"}}));

  coordinator_->HandleEventForTesting(Event("account-a", "email-2", "thread-1", 2));
  EXPECT_EQ(displayed_.size(), 1u) << "message ids dedupe across cursors";

  coordinator_->HandleEventForTesting(
      Event("account-b", "email-same-id", "thread-1", 1));
  EXPECT_EQ(displayed_.size(), 2u)
      << "dedupe identity is account plus message id";

  coordinator_->HandleEventForTesting(
      Event("account-a", "email-old", "thread-old", 1, 101));
  EXPECT_EQ(displayed_.size(), 3u) << "UID resets do not suppress new messages";

  behavior_.muted_thread_ids = {"muted"};
  coordinator_->HandleEventForTesting(Event("account-a", "email-muted", "muted", 3));
  EXPECT_EQ(displayed_.size(), 3u);
  behavior_.muted_thread_ids.clear();

  coordinator_->HandleEventForTesting(
      Event("account-a", "email-denied", "denied", 3, 101));
  EXPECT_EQ(displayed_.size(), 4u);

  behavior_.notification_preview = MailNotificationPreview::kSenderOnly;
  coordinator_->HandleEventForTesting(
      Event("account-b", "email-2", "thread-2", 2));
  ASSERT_EQ(displayed_.size(), 5u);
  EXPECT_EQ(displayed_[4].title, "Ada");
  EXPECT_EQ(displayed_[4].body, "New message");

  behavior_.notification_preview = MailNotificationPreview::kGeneric;
  coordinator_->HandleEventForTesting(Event("account-c", "email-3", "thread-3", 1));
  ASSERT_EQ(displayed_.size(), 6u);
  EXPECT_EQ(displayed_[5].title, "New mail");
  EXPECT_EQ(displayed_[5].body, "You have a new message");

  auto stale_click = displayed_[4].click_callback;
  coordinator_->RemoveAccount("account-b");
  EXPECT_FALSE(withdrawn_.empty());
  stale_click.Run();
  EXPECT_EQ(routes_.size(), 1u) << "removed-account click is stale";

  auto restarted = MakeCoordinator();
  restarted->HandleEventForTesting(
      Event("account-c", "email-restart", "thread-restart", 2));
  EXPECT_EQ(displayed_.size(), 7u);

  prefs_.SetBoolean(sidebar_prefs::kMahoMailEnabled, false);
  EXPECT_FALSE(withdrawn_.empty());
  displayed_[5].click_callback.Run();
  EXPECT_EQ(routes_.size(), 1u) << "disabled click is stale";
}

TEST_F(CoordinatorTest, NotificationOffAndDestructionWithdraw) {
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(displayed_.size(), 1u);
  prefs_.SetBoolean("maho.mail.notifications_enabled", false);
  ASSERT_EQ(withdrawn_.size(), 1u);

  prefs_.SetBoolean("maho.mail.notifications_enabled", true);
  behavior_.desktop_notifications = true;
  coordinator_->HandleEventForTesting(Event("account-b", "email-2", "thread-2", 1));
  ASSERT_EQ(displayed_.size(), 2u);
  auto stale_click = displayed_.back().click_callback;
  coordinator_.reset();
  EXPECT_EQ(withdrawn_.size(), 2u);
  stale_click.Run();
  EXPECT_TRUE(routes_.empty());
}

TEST_F(CoordinatorTest, RestartHonorsPersistedNotificationOptOut) {
  coordinator_.reset();
  prefs_.SetBoolean("maho.mail.notifications_enabled", false);
  // The service snapshot is default-constructed until the first behavior read.
  behavior_ = MailBehaviorSnapshot();
  ASSERT_TRUE(behavior_.desktop_notifications);
  coordinator_ = MakeCoordinator();

  coordinator_->HandleEventForTesting(Event());

  EXPECT_TRUE(displayed_.empty());
}

TEST_F(CoordinatorTest, PersistedMuteEventsConvergePerAccount) {
  coordinator_->OnThreadMuteChanged("account-a", "shared-thread", true);
  coordinator_->HandleEventForTesting(
      Event("account-a", "email-muted", "shared-thread", 1));
  EXPECT_TRUE(displayed_.empty());

  coordinator_->HandleEventForTesting(
      Event("account-b", "email-other-account", "shared-thread", 1));
  ASSERT_EQ(displayed_.size(), 1u)
      << "persisted mute is scoped to its account";

  auto restarted = MakeCoordinator();
  restarted->HandleEventForTesting(
      Event("account-a", "email-still-muted", "shared-thread", 2));
  EXPECT_EQ(displayed_.size(), 1u) << "mute convergence survives restart";

  restarted->OnThreadMuteChanged("account-a", "shared-thread", false);
  restarted->HandleEventForTesting(
      Event("account-a", "email-unmuted", "shared-thread", 2));
  EXPECT_EQ(displayed_.size(), 2u);
}

TEST_F(CoordinatorTest, RechecksOsPermissionBeforeDisplayAndClick) {
  permission_ = MailOsNotificationPermission::kDenied;
  coordinator_->HandleEventForTesting(Event());
  EXPECT_TRUE(displayed_.empty());

  permission_ = MailOsNotificationPermission::kGranted;
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(displayed_.size(), 1u);

  permission_ = MailOsNotificationPermission::kDenied;
  displayed_[0].click_callback.Run();
  EXPECT_TRUE(routes_.empty());
}

TEST_F(CoordinatorTest, DeniedThenAuthorizedUsesFreshPermission) {
  auto_complete_permissions_ = false;
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(permission_callbacks_.size(), 1u);
  std::move(permission_callbacks_.front())
      .Run(MailOsNotificationPermission::kDenied);
  permission_callbacks_.clear();
  EXPECT_TRUE(displayed_.empty());

  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(permission_callbacks_.size(), 1u);
  std::move(permission_callbacks_.front())
      .Run(MailOsNotificationPermission::kGranted);
  permission_callbacks_.clear();
  EXPECT_EQ(displayed_.size(), 1u);
}

TEST_F(CoordinatorTest, AuthorizedThenDeniedUsesFreshPermission) {
  auto_complete_permissions_ = false;
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(permission_callbacks_.size(), 1u);
  std::move(permission_callbacks_.front())
      .Run(MailOsNotificationPermission::kGranted);
  permission_callbacks_.clear();
  ASSERT_EQ(displayed_.size(), 1u);

  displayed_.front().click_callback.Run();
  ASSERT_EQ(permission_callbacks_.size(), 1u);
  std::move(permission_callbacks_.front())
      .Run(MailOsNotificationPermission::kDenied);
  permission_callbacks_.clear();
  EXPECT_TRUE(routes_.empty());
}

TEST_F(CoordinatorTest,
       PermissionReplyAfterDisableReenableCannotDisplayStaleMail) {
  auto_complete_permissions_ = false;
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(permission_callbacks_.size(), 1u);

  prefs_.SetBoolean(sidebar_prefs::kMahoMailEnabled, false);
  prefs_.SetBoolean(sidebar_prefs::kMahoMailEnabled, true);
  std::move(permission_callbacks_.front())
      .Run(MailOsNotificationPermission::kGranted);
  permission_callbacks_.clear();

  EXPECT_TRUE(displayed_.empty());
}

TEST_F(CoordinatorTest,
       PermissionReplyAfterAccountRemovalCannotDisplayStaleMail) {
  auto_complete_permissions_ = false;
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(permission_callbacks_.size(), 1u);

  coordinator_->OnAccountRemoved("account-a");
  std::move(permission_callbacks_.front())
      .Run(MailOsNotificationPermission::kGranted);
  permission_callbacks_.clear();

  EXPECT_TRUE(displayed_.empty());
}

TEST_F(CoordinatorTest, DurableOrderingSurvivesRestartAndUidValidityReset) {
  coordinator_->HandleEventForTesting(
      Event("account-a", "email-9", "thread-9", 9, 100));
  ASSERT_EQ(displayed_.size(), 1u);

  auto restarted = MakeCoordinator();
  restarted->HandleEventForTesting(
      Event("account-a", "email-8", "thread-8", 8, 100));
  EXPECT_EQ(displayed_.size(), 1u)
      << "an older UID in the same UIDVALIDITY epoch stays suppressed";

  restarted->HandleEventForTesting(
      Event("account-a", "email-reset", "thread-9", 1, 200));
  ASSERT_EQ(displayed_.size(), 2u)
      << "a new UIDVALIDITY epoch must not inherit the old UID watermark";

  restarted->HandleEventForTesting(
      Event("account-a", "email-stale-epoch", "thread-stale", 99, 100));
  EXPECT_EQ(displayed_.size(), 2u)
      << "a delayed event from the prior epoch must not replace the watermark";

  auto restarted_again = MakeCoordinator();
  restarted_again->HandleEventForTesting(
      Event("account-a", "email-reset-duplicate", "other-thread", 1, 200));
  EXPECT_EQ(displayed_.size(), 2u)
      << "the new epoch watermark must itself survive restart";
}

TEST_F(CoordinatorTest, DisplayFailureDoesNotConsumeDurableOrdering) {
  display_succeeds_ = false;
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(displayed_.size(), 1u);

  display_succeeds_ = true;
  coordinator_->HandleEventForTesting(Event());
  EXPECT_EQ(displayed_.size(), 2u)
      << "a failed platform display must leave the event retryable";

  auto restarted = MakeCoordinator();
  restarted->HandleEventForTesting(
      Event("account-a", "email-old", "thread-old", 1, 100));
  EXPECT_EQ(displayed_.size(), 2u)
      << "only the successful retry may advance the durable watermark";
}

TEST_F(CoordinatorTest, PendingDisplayReservesEventUntilCallback) {
  auto_complete_displays_ = false;
  coordinator_->HandleEventForTesting(Event());
  coordinator_->HandleEventForTesting(Event());
  ASSERT_EQ(displayed_.size(), 1u)
      << "an in-flight platform display reserves its event";
  ASSERT_EQ(display_completions_.size(), 1u);

  std::move(display_completions_.front()).Run(false);
  display_completions_.clear();
  coordinator_->HandleEventForTesting(Event());
  EXPECT_EQ(displayed_.size(), 2u)
      << "display rejection releases the reservation for retry";
}

TEST_F(CoordinatorTest, SameGenerationLifecycleExitWithdrawsAndRegates) {
  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kReady, 7);
  coordinator_->HandleEventForTesting(
      MahoMailNotificationEvent("account-a", "email-1", "thread-1", "Ada",
                                "subject", 1, 100, 7));
  ASSERT_EQ(displayed_.size(), 1u);
  auto stale_click = displayed_.front().click_callback;

  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kStarting,
                                   7);
  ASSERT_EQ(withdrawn_.size(), 1u);
  stale_click.Run();
  EXPECT_TRUE(routes_.empty());

  coordinator_->HandleEventForTesting(
      MahoMailNotificationEvent("account-b", "email-2", "thread-2", "Ada",
                                "subject", 1, 100, 7));
  EXPECT_EQ(displayed_.size(), 1u)
      << "same-generation crash recovery must re-gate delivery";

  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kReady, 7);
  coordinator_->HandleEventForTesting(
      MahoMailNotificationEvent("account-b", "email-2", "thread-2", "Ada",
                                "subject", 1, 100, 7));
  EXPECT_EQ(displayed_.size(), 2u);

  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kDraining,
                                   7);
  EXPECT_EQ(withdrawn_.size(), 2u);
  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kFailed,
                                   7);
  EXPECT_EQ(withdrawn_.size(), 2u)
      << "repeated non-ready transitions withdraw each active id once";
}

TEST_F(CoordinatorTest, StaleGenerationAndLateDisplaySuccessCannotReactivate) {
  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kReady, 9);
  coordinator_->HandleEventForTesting(
      MahoMailNotificationEvent("account-a", "stale", "stale-thread", "Ada",
                                "subject", 1, 100, 8));
  EXPECT_TRUE(displayed_.empty());

  auto_complete_displays_ = false;
  coordinator_->HandleEventForTesting(
      MahoMailNotificationEvent("account-a", "email-1", "thread-1", "Ada",
                                "subject", 1, 100, 9));
  ASSERT_EQ(display_completions_.size(), 1u);
  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kStarting,
                                   9);
  coordinator_->OnLifecycleChanged(MahoMailService::LifecycleState::kReady, 9);
  std::move(display_completions_.front()).Run(true);
  EXPECT_EQ(withdrawn_.size(), 1u)
      << "a pre-crash display cannot reactivate after same-generation recovery";
  displayed_.front().click_callback.Run();
  EXPECT_TRUE(routes_.empty());
}

TEST(MahoMailNotificationCoordinatorRegistryTest,
     OwnsOneCoordinatorForEveryProfileIdentity) {
  MahoMailNotificationCoordinatorRegistry registry;
  auto* profile_a = reinterpret_cast<Profile*>(0x1);
  auto* profile_b = reinterpret_cast<Profile*>(0x2);
  int factory_calls = 0;
  const auto factory = base::BindRepeating(
      [](int* calls, Profile*) {
        ++*calls;
        return std::make_unique<MahoMailNotificationCoordinator>(
            static_cast<PrefService*>(nullptr), nullptr,
            MahoMailNotificationCoordinator::DisplayCallback(),
            MahoMailNotificationCoordinator::WithdrawCallback(),
            MahoMailNotificationCoordinator::RouteCallback(),
            base::BindRepeating([]() { return MailBehaviorSnapshot(); }),
            base::BindRepeating(
                [](base::OnceCallback<void(MailOsNotificationPermission)>
                       callback) {
                  std::move(callback).Run(
                      MailOsNotificationPermission::kGranted);
                }));
      },
      &factory_calls);

  EXPECT_TRUE(registry.Ensure(profile_a, factory));
  EXPECT_FALSE(registry.Ensure(profile_a, factory));
  EXPECT_TRUE(registry.Ensure(profile_b, factory));
  EXPECT_EQ(registry.size(), 2u);
  EXPECT_EQ(factory_calls, 2);
  registry.Clear();
  EXPECT_EQ(registry.size(), 0u);
}

}  // namespace
}  // namespace maho
