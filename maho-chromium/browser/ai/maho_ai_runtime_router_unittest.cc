// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <memory>
#include <string>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ai/maho_ai_settings_migration.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

void RegisterTestAiPrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(maho::ai_prefs::kProvider, "");
  registry->RegisterStringPref(maho::ai_prefs::kModel, "gpt-4o-mini");
  registry->RegisterStringPref(maho::ai_prefs::kBaseUrl, "");
  registry->RegisterStringPref(maho::ai_prefs::kApiKey, "");
  registry->RegisterStringPref(maho::ai_prefs::kByokOpenAIEncryptedB64, "");
  registry->RegisterStringPref(maho::ai_prefs::kByokAnthropicEncryptedB64, "");
  registry->RegisterStringPref(
      maho::ai_prefs::kOAuthOpenAIRefreshEncryptedB64, "");
  registry->RegisterStringPref(
      maho::ai_prefs::kOAuthAnthropicRefreshEncryptedB64, "");
  registry->RegisterDictionaryPref(maho::ai_prefs::kProviderConfigs);
  registry->RegisterDictionaryPref(maho::ai_prefs::kTaskModels);
  registry->RegisterStringPref(
      maho::ai_prefs::kByokOpenAICompatibleEncryptedB64, "");
  registry->RegisterBooleanPref(
      maho::ai_prefs::kModelsSettingsMigratedV1, false);
  registry->RegisterBooleanPref(
      maho::ai_prefs::kSettingsMigratedFromGoogleProvider, false);
  registry->RegisterBooleanPref(
      maho::ai_prefs::kSettingsMigratedFromMacOSDefaults, false);
  registry->RegisterDictionaryPref(maho::ai_prefs::kModelCache);
  registry->RegisterStringPref(
      maho::account_prefs::kRelayAccessTokenEncryptedB64, "");
}

}  // namespace

class MahoAiRuntimeRouterTest : public testing::Test {
 protected:
  void SetUp() override {
    RegisterTestAiPrefs(prefs_.registry());
  }

  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple prefs_;
};

TEST_F(MahoAiRuntimeRouterTest, UsesUnifiedAgentByDefault) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  EXPECT_EQ(router.GetActiveAdapterName(), "unified-agent");
  EXPECT_TRUE(router.IsAvailable());
}

TEST_F(MahoAiRuntimeRouterTest, GetActiveAdapterReturnsNonNull) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  EXPECT_NE(router.GetActiveAdapter(), nullptr);
}

TEST_F(MahoAiRuntimeRouterTest, RebuildIsIdempotent) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  MahoAiRuntimeAdapter* first = router.GetActiveAdapter();
  MahoAiRuntimeAdapter* second = router.GetActiveAdapter();
  EXPECT_EQ(first, second);
}

// R-9: an allowing kAI gate (regular class) builds the adapter.
TEST_F(MahoAiRuntimeRouterTest, RegularTokenBuildsAdapter) {
  MahoAiRuntimeRouter router(&prefs_, nullptr,
                             base::BindRepeating([]() { return true; }));
  EXPECT_NE(router.GetActiveAdapter(), nullptr);
}

// R-9: a denying kAI gate (every OTR/non-regular class) builds nothing.
TEST_F(MahoAiRuntimeRouterTest, OtrTokenBuildsNothing) {
  MahoAiRuntimeRouter router(&prefs_, nullptr,
                             base::BindRepeating([]() { return false; }));
  EXPECT_EQ(router.GetActiveAdapter(), nullptr);
}

// R-9: a gate that flips to deny (e.g. an async profile/window switch that
// invalidates the token) rejects a subsequent resolution and drops the adapter.
TEST_F(MahoAiRuntimeRouterTest, AsyncRevalidationRejectsProfileSwitch) {
  auto allowed = std::make_shared<bool>(true);
  MahoAiRuntimeRouter router(
      &prefs_, nullptr,
      base::BindRepeating([](std::shared_ptr<bool> a) { return *a; }, allowed));
  EXPECT_NE(router.GetActiveAdapter(), nullptr);

  *allowed = false;
  EXPECT_EQ(router.GetActiveAdapter(), nullptr);
}

// --- Route Resolver Tests (Section 7.2) ---

TEST_F(MahoAiRuntimeRouterTest, ChatAlwaysResolvesDefaultAndNeverConsultsTaskModels) {
  // Connect OpenAI and Anthropic
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "encrypted-openai-key");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "encrypted-anthropic-key");

  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  // Attempt to set task_models for "chat" manually in prefs
  {
    ScopedDictPrefUpdate update(&prefs_, maho::ai_prefs::kTaskModels);
    base::DictValue chat_entry;
    chat_entry.Set("provider", maho::ai::kProviderAnthropic);
    chat_entry.Set("model", "claude-3-5-sonnet");
    update->Set("chat", std::move(chat_entry));
  }

  maho::ai::MahoAiRouteResult result =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.route.provider_id, maho::ai::kProviderOpenAI);
  EXPECT_EQ(result.route.model_id, "gpt-4o");
  EXPECT_FALSE(result.route.inherited_from_default);
  EXPECT_EQ(result.route.endpoint, "https://api.openai.com/v1");
}

TEST_F(MahoAiRuntimeRouterTest, NoOverrideInheritsDefault) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "encrypted-openai-key");
  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  maho::ai::MahoAiRouteResult result =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.route.provider_id, maho::ai::kProviderOpenAI);
  EXPECT_EQ(result.route.model_id, "gpt-4o");
  EXPECT_TRUE(result.route.inherited_from_default);
}

TEST_F(MahoAiRuntimeRouterTest, ExplicitTaskOverrideRoutesToReferencedProvider) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "encrypted-openai-key");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "encrypted-anthropic-key");

  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");

  maho::ai::MahoAiRouteResult result =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.route.provider_id, maho::ai::kProviderAnthropic);
  EXPECT_EQ(result.route.model_id, "claude-3-5-sonnet");
  EXPECT_FALSE(result.route.inherited_from_default);
  EXPECT_EQ(result.route.endpoint, "https://api.anthropic.com/v1");
}

TEST_F(MahoAiRuntimeRouterTest, ClearingOverrideReturnsToDefault) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "encrypted-openai-key");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "encrypted-anthropic-key");

  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");
  maho::ai::ClearTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy);

  maho::ai::MahoAiRouteResult result =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.route.provider_id, maho::ai::kProviderOpenAI);
  EXPECT_EQ(result.route.model_id, "gpt-4o");
  EXPECT_TRUE(result.route.inherited_from_default);
}

TEST_F(MahoAiRuntimeRouterTest, ThreeDistinctProvidersConnectedSimultaneously) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "key-anthropic");
  maho::ai::SetProviderBaseUrl(&prefs_, maho::ai::kProviderOpenAICompatible,
                               "http://127.0.0.1:18801");

  EXPECT_TRUE(maho::ai::IsProviderConnected(&prefs_, maho::ai::kProviderOpenAI));
  EXPECT_TRUE(maho::ai::IsProviderConnected(&prefs_, maho::ai::kProviderAnthropic));
  EXPECT_TRUE(
      maho::ai::IsProviderConnected(&prefs_, maho::ai::kProviderOpenAICompatible));

  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kInlineEdit,
                            maho::ai::kProviderOpenAICompatible,
                            "gpt-oss-120b-medium");

  auto chat_res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  EXPECT_TRUE(chat_res.is_ok());
  EXPECT_EQ(chat_res.route.provider_id, maho::ai::kProviderOpenAI);

  auto tidy_res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_TRUE(tidy_res.is_ok());
  EXPECT_EQ(tidy_res.route.provider_id, maho::ai::kProviderAnthropic);

  auto edit_res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kInlineEdit);
  EXPECT_TRUE(edit_res.is_ok());
  EXPECT_EQ(edit_res.route.provider_id, maho::ai::kProviderOpenAICompatible);
  EXPECT_EQ(edit_res.route.endpoint, "http://127.0.0.1:18801");
}

TEST_F(MahoAiRuntimeRouterTest, EmptyDefaultWithValidRelayDerivesManaged) {
  prefs_.SetString(maho::ai_prefs::kProvider, "");
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o-mini");
  prefs_.SetString(maho::account_prefs::kRelayAccessTokenEncryptedB64,
                   "valid-token");

  auto res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  EXPECT_TRUE(res.is_ok());
  EXPECT_EQ(res.route.provider_id, maho::ai::kProviderMahoManaged);
  EXPECT_TRUE(res.route.inherited_from_default);
}

TEST_F(MahoAiRuntimeRouterTest, ExplicitDisconnectedDefaultReturnsErrorNoFallback) {
  // Provider is set to OpenAI, but no OpenAI credentials are set
  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  // Valid relay exists, but explicit provider must fail closed without fallback
  prefs_.SetString(maho::account_prefs::kRelayAccessTokenEncryptedB64,
                   "valid-token");

  auto res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  EXPECT_FALSE(res.is_ok());
  EXPECT_EQ(res.error, maho::ai::MahoAiRouteError::kProviderDisconnected);
}

TEST_F(MahoAiRuntimeRouterTest, ExplicitDisconnectedTaskProviderReturnsErrorNoFallback) {
  // Default is connected OpenAI
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  // Task override is Anthropic, which is disconnected
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");

  auto res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_FALSE(res.is_ok());
  EXPECT_EQ(res.error, maho::ai::MahoAiRouteError::kProviderDisconnected);
}

TEST_F(MahoAiRuntimeRouterTest, TaskReferencesUnknownProviderReturnsError) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);

  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy,
                            "nonexistent-provider", "model-x");

  auto res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_FALSE(res.is_ok());
  EXPECT_EQ(res.error, maho::ai::MahoAiRouteError::kUnknownProvider);
}

TEST_F(MahoAiRuntimeRouterTest, CustomEndpointResolutionUsesProviderConfig) {
  maho::ai::SetProviderBaseUrl(&prefs_, maho::ai::kProviderOpenAICompatible,
                               "http://custom-llm:8080/");
  prefs_.SetString(maho::ai_prefs::kProvider,
                   maho::ai::kProviderOpenAICompatible);
  prefs_.SetString(maho::ai_prefs::kModel, "local-m");

  auto res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  EXPECT_TRUE(res.is_ok());
  EXPECT_EQ(res.route.endpoint, "http://custom-llm:8080");
}

TEST_F(MahoAiRuntimeRouterTest, InlineEditCanDifferFromChat) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "key-anthropic");

  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kInlineEdit,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");

  auto chat_res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  auto edit_res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kInlineEdit);

  EXPECT_EQ(chat_res.route.provider_id, maho::ai::kProviderOpenAI);
  EXPECT_EQ(edit_res.route.provider_id, maho::ai::kProviderAnthropic);
}

TEST_F(MahoAiRuntimeRouterTest, DisconnectTransitionFailsClosed) {
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "key-anthropic");

  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kMemory,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");

  // Disconnect Anthropic
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "");

  auto res =
      maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kMemory);
  EXPECT_FALSE(res.is_ok());
  EXPECT_EQ(res.error, maho::ai::MahoAiRouteError::kProviderDisconnected);
}

// --- Migration Tests (Section 7.1) ---

TEST_F(MahoAiRuntimeRouterTest, Migration_EmptyProfileLeavesTaskMapEmpty) {
  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  EXPECT_TRUE(prefs_.GetBoolean(maho::ai_prefs::kModelsSettingsMigratedV1));
  EXPECT_TRUE(prefs_.GetDict(maho::ai_prefs::kTaskModels).empty());
  EXPECT_TRUE(prefs_.GetString(maho::ai_prefs::kProvider).empty());
}

TEST_F(MahoAiRuntimeRouterTest, Migration_ExistingOpenAIDefaultPreserved) {
  prefs_.SetString(maho::ai_prefs::kProvider, "openai");
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kProvider), "openai");
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kModel), "gpt-4o");
  EXPECT_EQ(maho::ai::GetProviderLastModel(&prefs_, "openai"), "gpt-4o");
  EXPECT_TRUE(prefs_.GetDict(maho::ai_prefs::kTaskModels).empty());
}

TEST_F(MahoAiRuntimeRouterTest, Migration_ExistingAnthropicDefaultPreserved) {
  prefs_.SetString(maho::ai_prefs::kProvider, "anthropic");
  prefs_.SetString(maho::ai_prefs::kModel, "claude-3-5-haiku");

  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kProvider), "anthropic");
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kModel), "claude-3-5-haiku");
  EXPECT_EQ(maho::ai::GetProviderLastModel(&prefs_, "anthropic"),
            "claude-3-5-haiku");
}

TEST_F(MahoAiRuntimeRouterTest, Migration_OpenAICompatibleGatewayProfileMigrated) {
  prefs_.SetString(maho::ai_prefs::kProvider, "openai-compatible");
  prefs_.SetString(maho::ai_prefs::kBaseUrl, "http://127.0.0.1:18801");
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-oss-120b-medium");

  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kProvider), "openai-compatible");
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kModel), "gpt-oss-120b-medium");
  EXPECT_EQ(maho::ai::GetProviderBaseUrl(&prefs_, "openai-compatible"),
            "http://127.0.0.1:18801");
  EXPECT_EQ(maho::ai::GetProviderLastModel(&prefs_, "openai-compatible"),
            "gpt-oss-120b-medium");
  EXPECT_TRUE(prefs_.GetDict(maho::ai_prefs::kTaskModels).empty());
}

TEST_F(MahoAiRuntimeRouterTest, Migration_CustomPlaintextKeyEncryptedAndCleared) {
  prefs_.SetString(maho::ai_prefs::kProvider, "openai-compatible");
  prefs_.SetString(maho::ai_prefs::kBaseUrl, "http://127.0.0.1:18801");
  prefs_.SetString(maho::ai_prefs::kApiKey, "secret-test-key");

  auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, encryptor.get()));

  EXPECT_TRUE(prefs_.GetBoolean(maho::ai_prefs::kModelsSettingsMigratedV1));
  std::string encrypted_b64 =
      prefs_.GetString(maho::ai_prefs::kByokOpenAICompatibleEncryptedB64);
  EXPECT_FALSE(encrypted_b64.empty());
  EXPECT_TRUE(prefs_.GetString(maho::ai_prefs::kApiKey).empty());

  // Verify decryption roundtrip
  std::string encrypted_bytes;
  EXPECT_TRUE(base::Base64Decode(encrypted_b64, &encrypted_bytes));
  std::string decrypted;
  EXPECT_TRUE(encryptor->DecryptString(encrypted_bytes, &decrypted));
  EXPECT_EQ(decrypted, "secret-test-key");
}

TEST_F(MahoAiRuntimeRouterTest, Migration_EncryptionUnavailableRetainsPlaintextKey) {
  prefs_.SetString(maho::ai_prefs::kProvider, "openai-compatible");
  prefs_.SetString(maho::ai_prefs::kBaseUrl, "http://127.0.0.1:18801");
  prefs_.SetString(maho::ai_prefs::kApiKey, "secret-test-key");

  // Null encryptor simulates encryption services not yet available
  EXPECT_FALSE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));

  EXPECT_FALSE(prefs_.GetBoolean(maho::ai_prefs::kModelsSettingsMigratedV1));
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kApiKey), "secret-test-key");
  EXPECT_TRUE(
      prefs_.GetString(maho::ai_prefs::kByokOpenAICompatibleEncryptedB64).empty());
}

TEST_F(MahoAiRuntimeRouterTest, Migration_LocalServerEndpointMigrated) {
  prefs_.SetString(maho::ai_prefs::kProvider, "local-server");
  prefs_.SetString(maho::ai_prefs::kBaseUrl, "http://localhost:11434");
  prefs_.SetString(maho::ai_prefs::kModel, "qwen3:latest");

  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  EXPECT_EQ(maho::ai::GetProviderBaseUrl(&prefs_, "local-server"),
            "http://localhost:11434");
  EXPECT_EQ(maho::ai::GetProviderLastModel(&prefs_, "local-server"),
            "qwen3:latest");
}

TEST_F(MahoAiRuntimeRouterTest, Migration_IsIdempotent) {
  prefs_.SetString(maho::ai_prefs::kProvider, "openai-compatible");
  prefs_.SetString(maho::ai_prefs::kBaseUrl, "http://127.0.0.1:18801");
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-oss-120b-medium");

  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  EXPECT_TRUE(prefs_.GetBoolean(maho::ai_prefs::kModelsSettingsMigratedV1));

  // Modify task_models manually post-migration
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy, "openai",
                            "gpt-4o");

  // Second run must be a no-op and preserve post-migration overrides
  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));
  auto tidy_override =
      maho::ai::GetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  ASSERT_TRUE(tidy_override.has_value());
  EXPECT_EQ(tidy_override->first, "openai");
}

TEST_F(MahoAiRuntimeRouterTest, Migration_LegacyGoogleMigrationOccurs) {
  prefs_.SetString(maho::ai_prefs::kProvider, "google");
  EXPECT_FALSE(
      prefs_.GetBoolean(maho::ai_prefs::kSettingsMigratedFromGoogleProvider));

  maho::MigrateLegacyGoogleProvider(&prefs_);

  EXPECT_TRUE(
      prefs_.GetBoolean(maho::ai_prefs::kSettingsMigratedFromGoogleProvider));
  EXPECT_EQ(prefs_.GetString(maho::ai_prefs::kProvider), "");
}

TEST_F(MahoAiRuntimeRouterTest, Migration_ModelCacheInvalidatedForMismatchedEndpoint) {
  prefs_.SetString(maho::ai_prefs::kProvider, "openai-compatible");
  prefs_.SetString(maho::ai_prefs::kBaseUrl, "http://new-endpoint:8080");

  // Populate model cache with an old endpoint
  {
    ScopedDictPrefUpdate update(&prefs_, maho::ai_prefs::kModelCache);
    base::DictValue entry;
    base::ListValue models;
    models.Append("old-model-1");
    entry.Set("models", std::move(models));
    base::DictValue src;
    src.Set("base_url", "http://old-endpoint:1234");
    entry.Set("source", std::move(src));
    update->Set("openai-compatible", std::move(entry));
  }

  EXPECT_TRUE(maho::MigrateModelsSettingsV1ForTesting(&prefs_, nullptr));

  // The mismatched cache entry must have been invalidated (removed)
  const base::DictValue& cache =
      prefs_.GetDict(maho::ai_prefs::kModelCache);
  EXPECT_EQ(cache.FindDict("openai-compatible"), nullptr);
}

TEST_F(MahoAiRuntimeRouterTest, Phase2_ConcurrentConsumerRoutingWithOverrides) {
  // Connect 3 providers simultaneously: OpenAI, Anthropic, OpenAI-compatible
  prefs_.SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "key-openai");
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "key-anthropic");
  maho::ai::SetProviderBaseUrl(&prefs_, maho::ai::kProviderOpenAICompatible,
                               "http://127.0.0.1:18801/v1");

  // Set Default to OpenAI with gpt-4o
  prefs_.SetString(maho::ai_prefs::kProvider, maho::ai::kProviderOpenAI);
  prefs_.SetString(maho::ai_prefs::kModel, "gpt-4o");

  // Set overrides:
  // Tab Tidy -> Anthropic (claude-3-5-sonnet)
  // Inline Edit -> OpenAI-compatible (gpt-oss-120b-medium)
  // Memory -> Anthropic (claude-3-5-haiku)
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kTabTidy,
                            maho::ai::kProviderAnthropic, "claude-3-5-sonnet");
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kInlineEdit,
                            maho::ai::kProviderOpenAICompatible, "gpt-oss-120b-medium");
  maho::ai::SetTaskOverride(&prefs_, maho::ai::MahoAiTask::kMemory,
                            maho::ai::kProviderAnthropic, "claude-3-5-haiku");

  // Verify Chat & Agent observes Default (OpenAI / gpt-4o)
  auto chat_res = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat);
  EXPECT_TRUE(chat_res.is_ok());
  EXPECT_EQ(chat_res.route.provider_id, maho::ai::kProviderOpenAI);
  EXPECT_EQ(chat_res.route.model_id, "gpt-4o");
  EXPECT_FALSE(chat_res.route.inherited_from_default);

  // Verify Tab Tidy observes its Anthropic override
  auto tidy_res = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_TRUE(tidy_res.is_ok());
  EXPECT_EQ(tidy_res.route.provider_id, maho::ai::kProviderAnthropic);
  EXPECT_EQ(tidy_res.route.model_id, "claude-3-5-sonnet");
  EXPECT_FALSE(tidy_res.route.inherited_from_default);

  // Verify Inline Edit observes its OpenAI-compatible override
  auto edit_res = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kInlineEdit);
  EXPECT_TRUE(edit_res.is_ok());
  EXPECT_EQ(edit_res.route.provider_id, maho::ai::kProviderOpenAICompatible);
  EXPECT_EQ(edit_res.route.model_id, "gpt-oss-120b-medium");
  EXPECT_FALSE(edit_res.route.inherited_from_default);

  // Verify Memory observes its Anthropic override
  auto mem_res = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kMemory);
  EXPECT_TRUE(mem_res.is_ok());
  EXPECT_EQ(mem_res.route.provider_id, maho::ai::kProviderAnthropic);
  EXPECT_EQ(mem_res.route.model_id, "claude-3-5-haiku");
  EXPECT_FALSE(mem_res.route.inherited_from_default);

  // Non-overridden task (e.g. PagePreview) inherits Default
  auto preview_res = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kPagePreview);
  EXPECT_TRUE(preview_res.is_ok());
  EXPECT_EQ(preview_res.route.provider_id, maho::ai::kProviderOpenAI);
  EXPECT_EQ(preview_res.route.model_id, "gpt-4o");
  EXPECT_TRUE(preview_res.route.inherited_from_default);

  // Disconnect Anthropic -> Tab Tidy and Memory must fail closed without fallback
  prefs_.SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "");
  EXPECT_FALSE(maho::ai::IsProviderConnected(&prefs_, maho::ai::kProviderAnthropic));

  auto tidy_fail = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kTabTidy);
  EXPECT_FALSE(tidy_fail.is_ok());
  EXPECT_EQ(tidy_fail.error, maho::ai::MahoAiRouteError::kProviderDisconnected);

  auto mem_fail = maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kMemory);
  EXPECT_FALSE(mem_fail.is_ok());
  EXPECT_EQ(mem_fail.error, maho::ai::MahoAiRouteError::kProviderDisconnected);

  // Chat & Inline Edit remain completely unaffected
  EXPECT_TRUE(maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kChat).is_ok());
  EXPECT_TRUE(maho::ai::ResolveMahoAiModelRoute(&prefs_, maho::ai::MahoAiTask::kInlineEdit).is_ok());
}
