// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_settings_migration.h"

#include <string>

#include "base/logging.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"

#if BUILDFLAG(IS_APPLE)
#include <CoreFoundation/CoreFoundation.h>
#include "base/strings/sys_string_conversions.h"
#endif

namespace maho {

namespace {

#if BUILDFLAG(IS_APPLE)
constexpr char kSourceProviderKey[] = "maho.ai.provider";
constexpr char kSourceLocalServerEndpointKey[] =
    "maho.ai.localServer.endpoint";
constexpr char kSourceLocalServerModelKey[] = "maho.ai.localServer.model";
constexpr char kSourceApiKeyKey[] = "maho.ai.api_key";
constexpr char kSourceCustomEndpointKey[] = "maho.ai.custom.endpoint";
constexpr char kSourceCustomModelKey[] = "maho.ai.custom.model";

std::string ReadMacOSDefault(const char* key) {
  auto key_ref = base::SysUTF8ToCFStringRef(key);
  CFPropertyListRef value_ref = CFPreferencesCopyAppValue(
      key_ref.get(), CFSTR("com.maho.browser"));
  if (!value_ref) {
    return std::string();
  }
  if (CFGetTypeID(value_ref) != CFStringGetTypeID()) {
    CFRelease(value_ref);
    return std::string();
  }
  std::string value =
      base::SysCFStringRefToUTF8(static_cast<CFStringRef>(value_ref));
  CFRelease(value_ref);
  return value;
}

void DeleteMacOSDefault(const char* key) {
  auto key_ref = base::SysUTF8ToCFStringRef(key);
  CFPreferencesSetAppValue(key_ref.get(), nullptr, CFSTR("com.maho.browser"));
}

bool IsLegacyLocalServerProvider(const std::string& provider) {
  return provider == "Local Server" || provider == "Custom";
}
#endif

}  // namespace

void MigrateAiSettingsFromMacOSDefaults(Profile* profile) {
  if (!profile) {
    return;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs) {
    return;
  }

  if (prefs->GetBoolean(ai_prefs::kSettingsMigratedFromMacOSDefaults)) {
    return;
  }

#if !BUILDFLAG(IS_APPLE)
  prefs->SetBoolean(ai_prefs::kSettingsMigratedFromMacOSDefaults, true);
  return;
#else
  int migrated_count = 0;

  const std::string source_provider = ReadMacOSDefault(kSourceProviderKey);

  if (IsLegacyLocalServerProvider(source_provider) &&
      !prefs->HasPrefPath(ai_prefs::kProvider)) {
    prefs->SetString(ai_prefs::kProvider, "local-server");
    ++migrated_count;
  }

  if (!prefs->HasPrefPath(ai_prefs::kBaseUrl) ||
      prefs->GetString(ai_prefs::kBaseUrl).empty()) {
    std::string endpoint =
        ReadMacOSDefault(kSourceLocalServerEndpointKey);
    if (endpoint.empty()) {
      endpoint = ReadMacOSDefault(kSourceCustomEndpointKey);
    }
    if (!endpoint.empty()) {
      prefs->SetString(ai_prefs::kBaseUrl, endpoint);
      ++migrated_count;
    }
  }

  if (!prefs->HasPrefPath(ai_prefs::kModel) ||
      prefs->GetString(ai_prefs::kModel).empty()) {
    std::string model = ReadMacOSDefault(kSourceLocalServerModelKey);
    if (model.empty()) {
      model = ReadMacOSDefault(kSourceCustomModelKey);
    }
    if (!model.empty()) {
      prefs->SetString(ai_prefs::kModel, model);
      ++migrated_count;
    }
  }

  if (!prefs->HasPrefPath(ai_prefs::kApiKey) ||
      prefs->GetString(ai_prefs::kApiKey).empty()) {
    std::string api_key = ReadMacOSDefault(kSourceApiKeyKey);
    if (!api_key.empty()) {
      prefs->SetString(ai_prefs::kApiKey, api_key);
      ++migrated_count;
    }
  }

  prefs->SetBoolean(ai_prefs::kSettingsMigratedFromMacOSDefaults, true);

  DeleteMacOSDefault(kSourceProviderKey);
  DeleteMacOSDefault(kSourceLocalServerEndpointKey);
  DeleteMacOSDefault(kSourceLocalServerModelKey);
  DeleteMacOSDefault(kSourceCustomEndpointKey);
  DeleteMacOSDefault(kSourceCustomModelKey);
  DeleteMacOSDefault(kSourceApiKeyKey);
  DeleteMacOSDefault("maho.ai.anthropic.model");
  DeleteMacOSDefault("maho.ai.openai.model");
  DeleteMacOSDefault("maho.ai.remoteServer.endpoint");
  DeleteMacOSDefault("maho.ai.remoteServer.model");
  CFPreferencesAppSynchronize(CFSTR("com.maho.browser"));

  if (migrated_count > 0) {
    LOG(INFO) << "[maho-ai] migrated " << migrated_count
              << " settings from macOS defaults to Chromium prefs "
              << "(source keys removed)";
  } else {
    LOG(INFO) << "[maho-ai] no macOS defaults to migrate; source keys cleared";
  }
#endif
}

}  // namespace maho
