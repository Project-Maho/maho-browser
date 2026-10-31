// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_llm_client.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "testing/gtest/include/gtest/gtest.h"

class MahoAiLlmClientTestHelper {
 public:
  static void SetIsRetry(MahoAiLlmClient* client, bool value) {
    client->is_retry_ = value;
  }
  static bool GetIsRetry(const MahoAiLlmClient* client) {
    return client->is_retry_;
  }
  static void FinishResponse(MahoAiLlmClient* client) {
    client->OnResponseComplete(true);
  }
  static void ReceiveResponseData(MahoAiLlmClient* client, std::string chunk) {
    client->OnResponseDataReceived(std::move(chunk));
  }
  static void SetTokenCallback(MahoAiLlmClient* client,
                               MahoAiLlmClient::TokenCallback on_token) {
    client->on_token_ = std::move(on_token);
  }
  static void SetCompletion(MahoAiLlmClient* client,
                            MahoAiLlmClient::CompletionResult result) {
    client->completion_result_ = std::move(result);
  }
  static void AttachRequest(MahoAiLlmClient* client) {
    client->url_loader_ = network::SimpleURLLoader::Create(
        std::make_unique<network::ResourceRequest>(),
        TRAFFIC_ANNOTATION_FOR_TESTS);
  }
  static void CompleteRefresh(MahoAiLlmClient* client,
                              maho::auth::RefreshedTokens tokens) {
    client->OnRefreshComplete(true, 200, std::move(tokens));
  }
  static bool HasRequest(const MahoAiLlmClient* client) {
    return client->url_loader_ != nullptr;
  }
  static void FinishSuccess(MahoAiLlmClient* client) {
    client->FinishWithSuccess();
  }
  static void FinishError(MahoAiLlmClient* client) {
    client->FinishWithError("request failed");
  }
  static void SetCallbacks(MahoAiLlmClient* client,
                           MahoAiLlmClient::CompleteCallback on_complete,
                           MahoAiLlmClient::ErrorCallback on_error) {
    client->on_complete_ = std::move(on_complete);
    client->on_error_ = std::move(on_error);
  }
};

namespace {

constexpr char kTidyPayload[] = R"({"folders":[{"name":"Work","tab_ids":[1]}]})";
constexpr char kTidySseResponse[] =
    R"(data: {"choices":[{"delta":{"content":"{\"folders\":[{\"name\":\"Work\",\"tab_ids\":[1]}]}"}}]})"
    "\n\ndata: [DONE]\n\n";

class MahoAiLlmClientTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kProvider, "");
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kBaseUrl, "");
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kApiKey, "");
    prefs_.registry()->RegisterStringPref(maho::ai_prefs::kModel, "");
  }

  void ExpectSseCompletionWithoutReplay(const std::vector<std::string>& chunks) {
    MahoAiLlmClient client(&prefs_, nullptr, nullptr,
                          /*provider_adapter_v2_enabled=*/false);
    std::vector<std::string> tokens;
    int completion_count = 0;
    int error_count = 0;
    std::string full_text;
    MahoAiLlmClientTestHelper::SetTokenCallback(
        &client, base::BindRepeating(
                     [](std::vector<std::string>* tokens,
                        const std::string& token) { tokens->push_back(token); },
                     &tokens));
    MahoAiLlmClientTestHelper::SetCallbacks(
        &client,
        base::BindOnce(
            [](int* completion_count, std::string* full_text,
               MahoAiLlmClient::CompletionResult result) {
              ++*completion_count;
              *full_text = std::move(result.full_text);
            },
            &completion_count, &full_text),
        base::BindOnce(
            [](int* error_count, const std::string&) { ++*error_count; },
            &error_count));

    for (const auto& chunk : chunks) {
      MahoAiLlmClientTestHelper::ReceiveResponseData(&client, chunk);
    }
    const std::vector<std::string> expected_tokens = {kTidyPayload};
    EXPECT_EQ(tokens, expected_tokens);
    EXPECT_EQ(completion_count, 0);
    EXPECT_EQ(error_count, 0);

    // Completion flushes the real parser again; consumed events must not replay.
    MahoAiLlmClientTestHelper::FinishResponse(&client);

    EXPECT_EQ(tokens, expected_tokens);
    EXPECT_EQ(full_text, kTidyPayload);
    EXPECT_EQ(completion_count, 1);
    EXPECT_EQ(error_count, 0);
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple prefs_;
};

TEST_F(MahoAiLlmClientTest, SsePayloadAndDoneInSameChunkDoNotReplayOnCompletion) {
  ExpectSseCompletionWithoutReplay({kTidySseResponse});
}

TEST_F(MahoAiLlmClientTest,
       SsePayloadAndDoneSplitAcrossChunksDoNotReplayOnCompletion) {
  const std::string response = kTidySseResponse;
  // Include splits inside JSON, delimiters, and [DONE], as well as the event
  // boundary. Direct delivery makes every chunk boundary deterministic.
  for (size_t split = 1; split < response.size(); ++split) {
    SCOPED_TRACE(split);
    ExpectSseCompletionWithoutReplay(
        {response.substr(0, split), response.substr(split)});
  }
}

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

// R-9: a denying kAI gate stops StartCompletion before any network request;
// it reports an error and never fires the completion path.
TEST_F(MahoAiLlmClientTest, OtrTokenStopsBeforeRequest) {
  MahoAiLlmClient client(&prefs_, nullptr, nullptr,
                         /*provider_adapter_v2_enabled=*/false,
                         base::BindRepeating([]() { return false; }));

  bool errored = false;
  bool completed = false;
  client.StartCompletion(
      MahoAiLlmClient::CompletionRequest(), MahoAiLlmClient::TokenCallback(),
      base::BindOnce([](bool* c, MahoAiLlmClient::CompletionResult) { *c = true; },
                     &completed),
      base::BindOnce([](bool* e, const std::string&) { *e = true; }, &errored));

  EXPECT_TRUE(errored);
  EXPECT_FALSE(completed);
}

TEST_F(MahoAiLlmClientTest, EmptyResponseIsAnErrorNotSyntheticCompletion) {
  MahoAiLlmClient client(&prefs_, nullptr, nullptr,
                        /*provider_adapter_v2_enabled=*/false);
  bool completed = false;
  bool errored = false;
  MahoAiLlmClientTestHelper::SetCallbacks(
      &client,
      base::BindOnce(
          [](bool* completed, MahoAiLlmClient::CompletionResult) {
            *completed = true;
          },
          &completed),
      base::BindOnce(
          [](bool* errored, const std::string&) { *errored = true; },
          &errored));

  MahoAiLlmClientTestHelper::FinishResponse(&client);

  EXPECT_TRUE(errored);
  EXPECT_FALSE(completed);
}

TEST_F(MahoAiLlmClientTest, ToolCallOnlyResponseIsSuccessful) {
  MahoAiLlmClient client(&prefs_, nullptr, nullptr, false);
  MahoAiLlmClient::ToolCall call;
  call.index = 0;
  call.id = "call-1";
  call.name = "list_tabs";
  call.arguments_json = "{}";
  MahoAiLlmClient::CompletionResult result;
  result.tool_calls.push_back(std::move(call));
  result.finish_reason = "tool_calls";
  MahoAiLlmClientTestHelper::SetCompletion(&client, std::move(result));
  bool completed = false;
  bool errored = false;
  MahoAiLlmClientTestHelper::SetCallbacks(
      &client,
      base::BindOnce(
          [](bool* completed, MahoAiLlmClient::CompletionResult result) {
            *completed = true;
            ASSERT_EQ(result.tool_calls.size(), 1u);
            EXPECT_EQ(result.tool_calls[0].id, "call-1");
            EXPECT_TRUE(result.full_text.empty());
          },
          &completed),
      base::BindOnce(
          [](bool* errored, const std::string&) { *errored = true; },
          &errored));

  MahoAiLlmClientTestHelper::FinishSuccess(&client);

  EXPECT_TRUE(completed);
  EXPECT_FALSE(errored);
}

TEST_F(MahoAiLlmClientTest, TidyCompletionMayDestroyOwningClient) {
  auto client =
      std::make_unique<MahoAiLlmClient>(&prefs_, nullptr, nullptr, false);
  MahoAiLlmClientTestHelper::AttachRequest(client.get());
  ASSERT_TRUE(MahoAiLlmClientTestHelper::HasRequest(client.get()));
  MahoAiLlmClient::CompletionResult result;
  result.full_text = R"({"folders":[]})";
  MahoAiLlmClientTestHelper::SetCompletion(client.get(), std::move(result));
  bool completed = false;
  MahoAiLlmClientTestHelper::SetCallbacks(
      client.get(),
      base::BindOnce(
          [](std::unique_ptr<MahoAiLlmClient>* owner, bool* completed,
             MahoAiLlmClient::CompletionResult) {
            EXPECT_FALSE(MahoAiLlmClientTestHelper::HasRequest(owner->get()));
            owner->reset();
            *completed = true;
          },
          &client, &completed),
      {});

  MahoAiLlmClientTestHelper::FinishSuccess(client.get());

  EXPECT_TRUE(completed);
  EXPECT_FALSE(client);
}

TEST_F(MahoAiLlmClientTest, ErrorCompletionMayDestroyOwningClient) {
  auto client =
      std::make_unique<MahoAiLlmClient>(&prefs_, nullptr, nullptr, false);
  MahoAiLlmClientTestHelper::AttachRequest(client.get());
  ASSERT_TRUE(MahoAiLlmClientTestHelper::HasRequest(client.get()));
  bool errored = false;
  MahoAiLlmClientTestHelper::SetCallbacks(
      client.get(), {},
      base::BindOnce(
          [](std::unique_ptr<MahoAiLlmClient>* owner, bool* errored,
             const std::string&) {
            EXPECT_FALSE(MahoAiLlmClientTestHelper::HasRequest(owner->get()));
            owner->reset();
            *errored = true;
          },
          &client, &errored));

  MahoAiLlmClientTestHelper::FinishError(client.get());

  EXPECT_TRUE(errored);
  EXPECT_FALSE(client);
}

TEST_F(MahoAiLlmClientTest, PerRequestProviderSelection) {
  prefs_.registry()->RegisterStringPref(maho::ai_prefs::kByokOpenAIEncryptedB64, "");
  prefs_.registry()->RegisterStringPref(maho::ai_prefs::kByokAnthropicEncryptedB64, "");

  MahoAiLlmClient client(&prefs_, nullptr, nullptr, false);

  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "key-anthropic");
  EXPECT_TRUE(client.HasConfiguredCredential("anthropic"));
  EXPECT_FALSE(client.HasConfiguredCredential("openai"));

  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  EXPECT_TRUE(client.HasConfiguredCredential("openai"));
}

// The relay rotates the refresh token on every /auth/refresh and invalidates
// the one it consumed. Dropping the rotated pair left the profile holding a
// dead refresh token, so the next refresh failed and the user was signed out.
TEST_F(MahoAiLlmClientTest, RefreshPersistsRotatedRelayTokens) {
  namespace account_prefs = maho::account_prefs;
  auto* registry = prefs_.registry();
  registry->RegisterStringPref(account_prefs::kRelayAccessTokenEncryptedB64,
                               "");
  registry->RegisterStringPref(account_prefs::kRelayRefreshTokenEncryptedB64,
                               "");
  registry->RegisterInt64Pref(account_prefs::kRelayAccessTokenExpiresAt, 0);
  registry->RegisterInt64Pref(account_prefs::kRelayRefreshTokenExpiresAt, 0);
  registry->RegisterStringPref(account_prefs::kRelayUserEmail, "");
  registry->RegisterStringPref(account_prefs::kRelayUserId, "");
  registry->RegisterStringPref(account_prefs::kRelayUserDisplayName, "");
  registry->RegisterStringPref(account_prefs::kRelayUserTier, "");
  registry->RegisterStringPref(account_prefs::kRelayOAuthProvider, "");
  registry->RegisterStringPref(account_prefs::kRelayOAuthProviderSub, "");
  prefs_.SetString(account_prefs::kRelayUserEmail, "user@example.com");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, "google");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "subject-123");

  const auto encryptor =
      os_crypt_async::GetTestEncryptorForTesting();
  MahoAiLlmClient client(&prefs_, nullptr, encryptor.get(),
                         /*provider_adapter_v2_enabled=*/false);
  MahoAiLlmClientTestHelper::SetCallbacks(
      &client, {}, base::BindOnce([](const std::string&) {}));

  maho::auth::RefreshedTokens tokens;
  tokens.access_token = "rotated-access";
  tokens.refresh_token = "rotated-refresh";
  tokens.access_expires_at = 1'900'000'000;
  tokens.refresh_expires_at = 1'990'000'000;
  MahoAiLlmClientTestHelper::CompleteRefresh(&client, std::move(tokens));

  EXPECT_EQ("rotated-access",
            maho::auth::GetRelayAccessToken(&prefs_, *encryptor));
  EXPECT_EQ("rotated-refresh",
            maho::auth::GetRelayRefreshToken(&prefs_, *encryptor));
  EXPECT_EQ(1'900'000'000,
            prefs_.GetInt64(account_prefs::kRelayAccessTokenExpiresAt));
  EXPECT_EQ(1'990'000'000,
            prefs_.GetInt64(account_prefs::kRelayRefreshTokenExpiresAt));
  EXPECT_EQ("user@example.com",
            prefs_.GetString(account_prefs::kRelayUserEmail));
  EXPECT_EQ("subject-123",
            prefs_.GetString(account_prefs::kRelayOAuthProviderSub));
}

}  // namespace
