// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_llm_client.h"

#include <memory>

#include "base/test/task_environment.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

class MahoAiLlmClientTestHelper {
 public:
  static void SetIsRetry(MahoAiLlmClient* client, bool value) {
    client->is_retry_ = value;
  }
  static bool GetIsRetry(const MahoAiLlmClient* client) {
    return client->is_retry_;
  }
};

namespace {

class MahoAiLlmClientTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kProvider, "");
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kBaseUrl, "");
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kApiKey, "");
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kModel, "");
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple prefs_;
};

TEST_F(MahoAiLlmClientTest, ResetClearsIsRetryFlag) {
  MahoAiLlmClient client(&prefs_, nullptr, nullptr,
                         /*provider_adapter_v2_enabled=*/false);

  MahoAiLlmClientTestHelper::SetIsRetry(&client, true);
  ASSERT_TRUE(MahoAiLlmClientTestHelper::GetIsRetry(&client));

  client.Reset();
  EXPECT_FALSE(MahoAiLlmClientTestHelper::GetIsRetry(&client))
      << "Reset() must clear is_retry_ so subsequent 401 responses can "
         "trigger token refresh; regression lock for NEW-1.";
}

TEST_F(MahoAiLlmClientTest, ConstructorInitializesIsRetryToFalse) {
  MahoAiLlmClient client(&prefs_, nullptr, nullptr,
                         /*provider_adapter_v2_enabled=*/false);
  EXPECT_FALSE(MahoAiLlmClientTestHelper::GetIsRetry(&client));
}

TEST_F(MahoAiLlmClientTest, ResetIsIdempotentForIsRetry) {
  MahoAiLlmClient client(&prefs_, nullptr, nullptr,
                         /*provider_adapter_v2_enabled=*/false);
  client.Reset();
  EXPECT_FALSE(MahoAiLlmClientTestHelper::GetIsRetry(&client));
  client.Reset();
  EXPECT_FALSE(MahoAiLlmClientTestHelper::GetIsRetry(&client));
}

}  // namespace
