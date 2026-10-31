// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_settings_migration.h"

#include <string>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
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

namespace {

bool MigrateModelsSettingsV1Sync(PrefService* prefs,
                                 const os_crypt_async::Encryptor* encryptor) {
  if (!prefs) {
    return false;
  }
  if (prefs->GetBoolean(ai_prefs::kModelsSettingsMigratedV1)) {
    return true;
  }

  const std::string legacy_provider = prefs->GetString(ai_prefs::kProvider);
  const std::string legacy_model = prefs->GetString(ai_prefs::kModel);

  // 4. Initialize task_models to empty dictionary if absent.
  if (!prefs->HasPrefPath(ai_prefs::kTaskModels)) {
    prefs->SetDict(ai_prefs::kTaskModels, base::DictValue());
  }

  // 5 & 6. Seed provider_configs
  {
    ScopedDictPrefUpdate update(prefs, ai_prefs::kProviderConfigs);
    if (!legacy_provider.empty() && !legacy_model.empty()) {
      base::DictValue* prov_entry = update->EnsureDict(legacy_provider);
      prov_entry->Set("last_model", legacy_model);
    }
    if (legacy_provider == "openai-compatible") {
      std::string base_url = prefs->GetString(ai_prefs::kBaseUrl);
      if (!base_url.empty()) {
        base::DictValue* prov_entry = update->EnsureDict("openai-compatible");
        prov_entry->Set("base_url", base_url);
      }
    } else if (legacy_provider == "local-server") {
      std::string base_url = prefs->GetString(ai_prefs::kBaseUrl);
      if (!base_url.empty()) {
        base::DictValue* prov_entry = update->EnsureDict("local-server");
        prov_entry->Set("base_url", base_url);
      }
    }
  }

  // 8. Custom API key encryption for openai-compatible
  if (legacy_provider == "openai-compatible" &&
      !prefs->GetString(ai_prefs::kApiKey).empty()) {
    const std::string raw_key = prefs->GetString(ai_prefs::kApiKey);
    if (!encryptor) {
      // Cannot complete encryption now: leave migration flag false and return.
      return false;
    }
    std::string encrypted_bytes;
    if (!encryptor->EncryptString(raw_key, &encrypted_bytes)) {
      LOG(ERROR) << "[MigrateModelsSettingsV1] failed to encrypt custom API key";
      return false;
    }
    std::string encrypted_b64 = base::Base64Encode(encrypted_bytes);
    prefs->SetString(ai_prefs::kByokOpenAICompatibleEncryptedB64, encrypted_b64);
    if (prefs->GetString(ai_prefs::kByokOpenAICompatibleEncryptedB64) == encrypted_b64) {
      prefs->ClearPref(ai_prefs::kApiKey);
    } else {
      LOG(ERROR) << "[MigrateModelsSettingsV1] encrypted key write verification failed";
      return false;
    }
  }

  // 10. Model cache invalidation: check custom/local endpoint matching
  if (prefs->HasPrefPath(ai_prefs::kModelCache)) {
    ScopedDictPrefUpdate cache_update(prefs, ai_prefs::kModelCache);
    for (const auto* prov : {"openai-compatible", "local-server"}) {
      base::DictValue* entry = cache_update->FindDict(prov);
      if (entry) {
        const base::DictValue* src = entry->FindDict("source");
        const std::string* cached_url = src ? src->FindString("base_url") : nullptr;
        std::string configured_url = prefs->GetString(ai_prefs::kBaseUrl);
        if (!cached_url || *cached_url != configured_url) {
          cache_update->Remove(prov);
        }
      }
    }
  }

  // 11. Set migrated flag
  prefs->SetBoolean(ai_prefs::kModelsSettingsMigratedV1, true);
  return true;
}

void OnEncryptorReadyForMigration(
    base::WeakPtr<Profile> weak_profile,
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  if (!weak_profile) {
    return;
  }
  PrefService* prefs = weak_profile->GetPrefs();
  if (!prefs) {
    return;
  }
  MigrateModelsSettingsV1Sync(prefs, encryptor.get());
}

}  // namespace

void MigrateLegacyGoogleProvider(PrefService* prefs) {
  if (!prefs) {
    return;
  }
  if (!prefs->GetBoolean(ai_prefs::kSettingsMigratedFromGoogleProvider)) {
    if (prefs->GetString(ai_prefs::kProvider) == "google") {
      prefs->SetString(ai_prefs::kProvider, "");
    }
    prefs->SetBoolean(ai_prefs::kSettingsMigratedFromGoogleProvider, true);
  }
}

void MigrateModelsSettingsV1(Profile* profile) {
  if (!profile) {
    return;
  }
  PrefService* prefs = profile->GetPrefs();
  if (!prefs || prefs->GetBoolean(ai_prefs::kModelsSettingsMigratedV1)) {
    return;
  }

  const bool needs_encryptor =
      (prefs->GetString(ai_prefs::kProvider) == "openai-compatible" &&
       !prefs->GetString(ai_prefs::kApiKey).empty());

  if (!needs_encryptor) {
    MigrateModelsSettingsV1Sync(prefs, nullptr);
    return;
  }

  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&OnEncryptorReadyForMigration, profile->GetWeakPtr()));
  }
}

void MigrateAiSettings(Profile* profile) {
  if (!profile) {
    return;
  }
  MigrateAiSettingsFromMacOSDefaults(profile);
  if (profile->GetPrefs()) {
    MigrateLegacyGoogleProvider(profile->GetPrefs());
  }
  MigrateModelsSettingsV1(profile);
}

bool MigrateModelsSettingsV1ForTesting(
    PrefService* prefs,
    const os_crypt_async::Encryptor* encryptor) {
  return MigrateModelsSettingsV1Sync(prefs, encryptor);
}

}  // namespace maho
