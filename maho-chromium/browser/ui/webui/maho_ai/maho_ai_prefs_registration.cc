// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_prefs_registration.h"

#include <string>

#include "components/prefs/pref_registry_simple.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"

namespace {
constexpr char kDefaultModel[] = "gpt-4o-mini";
}  // namespace

namespace maho::ai {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(ai_prefs::kProvider, std::string());
  registry->RegisterStringPref(ai_prefs::kBaseUrl, std::string());
  registry->RegisterStringPref(ai_prefs::kApiKey, std::string());
  registry->RegisterStringPref(ai_prefs::kModel, kDefaultModel);
  registry->RegisterBooleanPref(ai_prefs::kStructuredContextV2Enabled, false);
  registry->RegisterBooleanPref(ai_prefs::kBrowserToolsV1Enabled, true);
  registry->RegisterBooleanPref(ai_prefs::kChatOrchestratorV2Enabled, false);
  registry->RegisterBooleanPref(ai_prefs::kProviderAdapterV2Enabled, false);
  registry->RegisterBooleanPref(ai_prefs::kUiThinModeEnabled, false);
  registry->RegisterStringPref(ai_prefs::kPendingInitialQuery, std::string());
  registry->RegisterBooleanPref(ai_prefs::kOpenCodeRuntimeEnabled, true);
  registry->RegisterStringPref(ai_prefs::kSessionListJson, std::string());
  registry->RegisterStringPref(ai_prefs::kApprovalPolicy, "prompt");
  registry->RegisterBooleanPref(ai_prefs::kSessionPersistenceEnabled, true);
  registry->RegisterDictionaryPref(ai_prefs::kSessionEventLogs);
  registry->RegisterStringPref(ai_prefs::kRuntimeCommand, std::string());
}

}  // namespace maho::ai
