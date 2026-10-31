#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"

#include "maho/browser/ui/webui/maho_subscription_checkout.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::welcome {
namespace {

TEST(MahoWelcomeCompletionPolicyTest,
     RequiresPasswordSetupOnlyBeforeFirstCompletion) {
  EXPECT_TRUE(RequiresPasswordSetup(false));
  EXPECT_FALSE(RequiresPasswordSetup(true));
}

TEST(MahoWelcomeCompletionPolicyTest, StoredValidSessionSkipsOnboarding) {
  for (bool completed : {false, true}) {
    const bool reconciled = ReconcileWelcomeCompleted(completed, true, true);
    EXPECT_TRUE(reconciled);
    EXPECT_FALSE(RequiresPasswordSetup(reconciled));
    EXPECT_FALSE(IsLoginGateActive(false, true, reconciled));
  }
}

TEST(MahoWelcomeCompletionPolicyTest, StoredExpiredSessionRequiresOnlyLogin) {
  const bool reconciled = ReconcileWelcomeCompleted(false, false, true);
  EXPECT_TRUE(reconciled);
  EXPECT_FALSE(RequiresPasswordSetup(reconciled));
  EXPECT_TRUE(IsLoginGateActive(false, false, reconciled));
}

TEST(MahoWelcomeCompletionPolicyTest, NoCredentialsStillRequiresFullOnboarding) {
  const bool reconciled = ReconcileWelcomeCompleted(false, false, false);
  EXPECT_FALSE(reconciled);
  EXPECT_TRUE(RequiresPasswordSetup(reconciled));
  EXPECT_TRUE(IsLoginGateActive(false, false, reconciled));
}

TEST(MahoWelcomeCompletionPolicyTest, IsLoginGateActivePolicyMatrix) {
  // Authentication does not imply that required setup was completed.
  EXPECT_TRUE(IsLoginGateActive(/*login_gate_bypassed=*/false,
                                /*has_valid_relay_session=*/false,
                                /*welcome_completed=*/false));
  EXPECT_TRUE(IsLoginGateActive(/*login_gate_bypassed=*/false,
                                /*has_valid_relay_session=*/false,
                                /*welcome_completed=*/true));
  EXPECT_TRUE(IsLoginGateActive(/*login_gate_bypassed=*/false,
                                /*has_valid_relay_session=*/true,
                                /*welcome_completed=*/false));
  EXPECT_FALSE(IsLoginGateActive(/*login_gate_bypassed=*/false,
                                 /*has_valid_relay_session=*/true,
                                 /*welcome_completed=*/true));
  EXPECT_FALSE(IsLoginGateActive(/*login_gate_bypassed=*/true,
                                 /*has_valid_relay_session=*/false,
                                 /*welcome_completed=*/false));
  EXPECT_FALSE(IsLoginGateActive(/*login_gate_bypassed=*/true,
                                 /*has_valid_relay_session=*/false,
                                 /*welcome_completed=*/true));
  EXPECT_FALSE(IsLoginGateActive(/*login_gate_bypassed=*/true,
                                 /*has_valid_relay_session=*/true,
                                 /*welcome_completed=*/false));
  EXPECT_FALSE(IsLoginGateActive(/*login_gate_bypassed=*/true,
                                 /*has_valid_relay_session=*/true,
                                 /*welcome_completed=*/true));
}

TEST(MahoWelcomeSyncKeyTest, ParseGenerateSyncKeyJsonSuccessAndFailure) {
  std::string valid_json = R"({
    "syncKey": "sk_test_123",
    "roomId": "room_456",
    "recoveryPhrase": "word1 word2 word3"
  })";
  GeneratedSyncKeyResult result = ParseGenerateSyncKeyJsonForTesting(valid_json);
  EXPECT_EQ(result.sync_key, "sk_test_123");
  EXPECT_EQ(result.room_id, "room_456");
  EXPECT_EQ(result.recovery_phrase, "word1 word2 word3");

  std::string invalid_json = "{ invalid json }";
  GeneratedSyncKeyResult err_result = ParseGenerateSyncKeyJsonForTesting(invalid_json);
  EXPECT_TRUE(err_result.sync_key.empty());
  EXPECT_TRUE(err_result.room_id.empty());
  EXPECT_TRUE(err_result.recovery_phrase.empty());
}

TEST(MahoWelcomeSyncKeyTest, ParseStartSyncJsonSuccessAndFailure) {
  std::string empty_response = "";
  StartSyncResult ok_empty = ParseStartSyncJsonForTesting(empty_response);
  EXPECT_TRUE(ok_empty.ok);
  EXPECT_TRUE(ok_empty.error_message.empty());

  std::string success_json = R"({"success": true})";
  StartSyncResult ok_json = ParseStartSyncJsonForTesting(success_json);
  EXPECT_TRUE(ok_json.ok);
  EXPECT_TRUE(ok_json.error_message.empty());

  std::string failure_json = R"({"success": false, "error": "Invalid phrase"})";
  StartSyncResult fail_json = ParseStartSyncJsonForTesting(failure_json);
  EXPECT_FALSE(fail_json.ok);
  EXPECT_EQ(fail_json.error_message, "Invalid phrase");

  std::string invalid_json = "not json";
  StartSyncResult err_json = ParseStartSyncJsonForTesting(invalid_json);
  EXPECT_FALSE(err_json.ok);
  EXPECT_EQ(err_json.error_message, "Invalid JSON response");
}

TEST(MahoSubscriptionCheckoutTest, MapsOnlyPaidTiersToCheckoutTemplates) {
  EXPECT_EQ(maho::webui::SubscriptionCheckoutUrlForTier("pro"),
            maho::webui::kProMonthlyCheckoutUrl);
  EXPECT_EQ(maho::webui::SubscriptionCheckoutUrlForTier("max"),
            maho::webui::kMaxMonthlyCheckoutUrl);
  EXPECT_TRUE(maho::webui::SubscriptionCheckoutUrlForTier("free").empty());
  EXPECT_TRUE(
      maho::webui::SubscriptionCheckoutUrlForTier("unexpected").empty());
}

TEST(MahoSubscriptionCheckoutTest, IdentifiesUnconfiguredCheckoutTemplates) {
  // Both paid tiers are configured with real buy links.
  EXPECT_FALSE(maho::webui::IsSubscriptionCheckoutPlaceholder(
      maho::webui::kProMonthlyCheckoutUrl));
  EXPECT_FALSE(maho::webui::IsSubscriptionCheckoutPlaceholder(
      maho::webui::kMaxMonthlyCheckoutUrl));
  EXPECT_FALSE(maho::webui::IsSubscriptionCheckoutPlaceholder(""));
  // A TODO_REPLACE_WITH_ template is still detected as unconfigured.
  EXPECT_TRUE(maho::webui::IsSubscriptionCheckoutPlaceholder(
      "TODO_REPLACE_WITH_MAX_MONTHLY_LEMONSQUEEZY_BUY_LINK"));
}

TEST(MahoSubscriptionCheckoutTest, BuildsEscapedCheckoutUrlForRelayUser) {
  EXPECT_EQ(maho::webui::BuildSubscriptionCheckoutUrl(
                "https://maho.lemonsqueezy.com/buy/pro", "user+a@example.com"),
            "https://maho.lemonsqueezy.com/buy/pro"
            "?checkout[custom][user_id]=user%2Ba%40example.com");
  EXPECT_TRUE(maho::webui::BuildSubscriptionCheckoutUrl(
                  "https://maho.lemonsqueezy.com/buy/max", "")
                  .empty());
  // Both configured tiers build a real URL with the escaped relay user id.
  EXPECT_EQ(maho::webui::BuildSubscriptionCheckoutUrl(
                maho::webui::kProMonthlyCheckoutUrl, "user_123"),
            std::string(maho::webui::kProMonthlyCheckoutUrl) +
                "?checkout[custom][user_id]=user_123");
  EXPECT_EQ(maho::webui::BuildSubscriptionCheckoutUrl(
                maho::webui::kMaxMonthlyCheckoutUrl, "user_123"),
            std::string(maho::webui::kMaxMonthlyCheckoutUrl) +
                "?checkout[custom][user_id]=user_123");
  // A TODO placeholder template is still guarded to empty.
  EXPECT_TRUE(maho::webui::BuildSubscriptionCheckoutUrl(
                  "TODO_REPLACE_WITH_MAX_MONTHLY_LEMONSQUEEZY_BUY_LINK",
                  "user_123")
                  .empty());
}

}  // namespace
}  // namespace maho::welcome
