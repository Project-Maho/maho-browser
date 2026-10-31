// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_prefs_registration.h"

#include <string>

#include "components/prefs/pref_registry_simple.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"

namespace {
constexpr char kDefaultModel[] = "gpt-4o-mini";
constexpr char kDefaultReasoningEffort[] = "medium";
}  // namespace

namespace maho::ai {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(ai_prefs::kProvider, std::string());
  registry->RegisterStringPref(ai_prefs::kBaseUrl, std::string());
  registry->RegisterStringPref(ai_prefs::kApiKey, std::string());
  registry->RegisterStringPref(ai_prefs::kModel, kDefaultModel);
  registry->RegisterStringPref(ai_prefs::kReasoningEffort,
                               kDefaultReasoningEffort);
  registry->RegisterBooleanPref(ai_prefs::kStructuredContextV2Enabled, false);
  registry->RegisterBooleanPref(ai_prefs::kBrowserToolsV1Enabled, true);
  registry->RegisterBooleanPref(ai_prefs::kChatOrchestratorV2Enabled, false);
  registry->RegisterBooleanPref(ai_prefs::kProviderAdapterV2Enabled, false);
  registry->RegisterBooleanPref(ai_prefs::kUiThinModeEnabled, false);
  registry->RegisterStringPref(ai_prefs::kSessionListJson, std::string());
  registry->RegisterStringPref(ai_prefs::kApprovalPolicy, "prompt");
  registry->RegisterStringPref(ai_prefs::kPermissionTier, "guard");
  registry->RegisterBooleanPref(ai_prefs::kFinalConfirm, true);
  registry->RegisterBooleanPref(ai_prefs::kProactiveMode, false);
  registry->RegisterBooleanPref(ai_prefs::kSessionPersistenceEnabled, true);
  registry->RegisterDictionaryPref(ai_prefs::kSessionEventLogs);
  registry->RegisterStringPref(ai_prefs::kAiViewMode, "sidebar");
  registry->RegisterIntegerPref(ai_prefs::kPendingSurface, 0);
  registry->RegisterInt64Pref(ai_prefs::kPendingSurfaceGeneration, 0);
  registry->RegisterBooleanPref(ai_prefs::kAttachBrowserContext, true);
  registry->RegisterBooleanPref(ai_prefs::kMailReadAllowed, false);
  registry->RegisterStringPref(ai_prefs::kTranslationProvider, std::string());
  registry->RegisterStringPref(ai_prefs::kByokOpenAIEncryptedB64,
                                std::string());
  registry->RegisterStringPref(ai_prefs::kByokAnthropicEncryptedB64,
                                std::string());
  registry->RegisterStringPref(ai_prefs::kOAuthOpenAIClientId, std::string());
  registry->RegisterStringPref(ai_prefs::kOAuthOpenAIRefreshEncryptedB64,
                               std::string());
  registry->RegisterInt64Pref(ai_prefs::kOAuthOpenAIExpiresAt, 0);
  registry->RegisterStringPref(ai_prefs::kOAuthAnthropicClientId, std::string());
  registry->RegisterStringPref(ai_prefs::kOAuthAnthropicRefreshEncryptedB64,
                               std::string());
  registry->RegisterInt64Pref(ai_prefs::kOAuthAnthropicExpiresAt, 0);
  registry->RegisterDictionaryPref(ai_prefs::kAgentToolGrants);
  registry->RegisterDictionaryPref(ai_prefs::kMcpCredentials);

  // Models settings v1 prefs.
  registry->RegisterDictionaryPref(ai_prefs::kProviderConfigs);
  registry->RegisterDictionaryPref(ai_prefs::kTaskModels);
  registry->RegisterStringPref(
      ai_prefs::kByokOpenAICompatibleEncryptedB64, std::string());
  registry->RegisterBooleanPref(
      ai_prefs::kModelsSettingsMigratedV1, false);

  registry->RegisterBooleanPref(
      ai_prefs::kSettingsMigratedFromMacOSDefaults, false);
  registry->RegisterBooleanPref(
      ai_prefs::kSettingsMigratedFromGoogleProvider, false);
  registry->RegisterDictionaryPref(ai_prefs::kModelCache);

  // Account / relay auth prefs.
  registry->RegisterStringPref(
      account_prefs::kRelayAccessTokenEncryptedB64, std::string());
  registry->RegisterStringPref(
      account_prefs::kRelayRefreshTokenEncryptedB64, std::string());
  registry->RegisterInt64Pref(account_prefs::kRelayAccessTokenExpiresAt, 0);
  registry->RegisterInt64Pref(account_prefs::kRelayRefreshTokenExpiresAt, 0);
  registry->RegisterStringPref(account_prefs::kRelayUserEmail, std::string());
  registry->RegisterStringPref(account_prefs::kRelayUserId, std::string());
  registry->RegisterStringPref(
      account_prefs::kRelayUserDisplayName, std::string());
  registry->RegisterStringPref(account_prefs::kRelayUserTier, std::string());
  registry->RegisterStringPref(
      account_prefs::kRelayOAuthProvider, std::string());
  registry->RegisterStringPref(
      account_prefs::kRelayOAuthProviderSub, std::string());
  registry->RegisterStringPref(account_prefs::kRelayDeviceId, std::string());
  registry->RegisterStringPref(
      account_prefs::kRelaySubscriptionStatus, std::string());
  registry->RegisterInt64Pref(
      account_prefs::kRelaySubscriptionExpiresAt, 0);
}

}  // namespace maho::ai
