// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_provider_utils.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_reader.h"
#include "build/build_config.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho::passwords {

namespace {

constexpr char kDefaultProfileKey[] = "default";
constexpr char kDisabledProviderMode[] = "disabled";
constexpr char kNativeProviderMode[] = "maho_native";
constexpr char kBitwardenProviderMode[] = "bitwarden";
constexpr char kOnePasswordProviderMode[] = "onepassword";

// Test-only override for the kill-switch PrefService. Browser-process code
// always resolves local state instead.
PrefService* g_native_write_prefs_for_testing = nullptr;

std::optional<EffectivePasswordProvider> ParseProviderMode(
    std::string_view mode) {
  if (mode == kDisabledProviderMode) {
    return EffectivePasswordProvider::kDisabled;
  }
  if (mode == kNativeProviderMode || mode == "chromium" ||
      mode == "builtin" || mode == "system") {
    return EffectivePasswordProvider::kMahoNative;
  }
  if (mode == kBitwardenProviderMode) {
    return EffectivePasswordProvider::kBitwarden;
  }
  if (mode == kOnePasswordProviderMode || mode == "1password" || mode == "one_password") {
    return EffectivePasswordProvider::kOnePassword;
  }
  return std::nullopt;
}

std::string_view ProviderMode(EffectivePasswordProvider provider) {
  switch (provider) {
    case EffectivePasswordProvider::kDisabled:
      return kDisabledProviderMode;
    case EffectivePasswordProvider::kMahoNative:
      return kNativeProviderMode;
    case EffectivePasswordProvider::kBitwarden:
      return kBitwardenProviderMode;
    case EffectivePasswordProvider::kOnePassword:
      return kOnePasswordProviderMode;
  }
}

bool ParseProviderRegistry(std::string_view registry_json,
                           EffectivePasswordProvider selected_provider,
                           std::vector<std::string>* extension_ids) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(registry_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }

  const std::string_view selected_mode = ProviderMode(selected_provider);
  bool found_selected_provider = false;
  for (const base::Value& item : parsed->GetList()) {
    const base::DictValue* provider = item.GetIfDict();
    if (!provider) {
      return false;
    }
    const std::string* provider_id = provider->FindString("providerId");
    const base::ListValue* registered_extension_ids =
        provider->FindList("extensionIds");
    if (!provider_id || provider_id->empty() || !registered_extension_ids) {
      return false;
    }

    std::vector<std::string> parsed_extension_ids;
    parsed_extension_ids.reserve(registered_extension_ids->size());
    for (const base::Value& extension_id : *registered_extension_ids) {
      if (!extension_id.is_string() || extension_id.GetString().empty()) {
        return false;
      }
      parsed_extension_ids.push_back(extension_id.GetString());
    }

    if (*provider_id != selected_mode) {
      continue;
    }
    if (found_selected_provider) {
      return false;
    }
    found_selected_provider = true;
    *extension_ids = std::move(parsed_extension_ids);
  }

  return found_selected_provider;
}

bool IsRegisteredProviderExtensionAvailable(
    std::string_view installed_extensions_json,
    const std::vector<std::string>& registered_extension_ids) {
  if (registered_extension_ids.empty()) {
    return false;
  }

  std::optional<base::Value> parsed = base::JSONReader::Read(
      installed_extensions_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }

  bool available = false;
  for (const base::Value& item : parsed->GetList()) {
    const base::DictValue* extension = item.GetIfDict();
    if (!extension) {
      return false;
    }
    const std::string* id = extension->FindString("id");
    const std::optional<bool> enabled = extension->FindBool("enabled");
    if (!id || id->empty() || !enabled.has_value()) {
      return false;
    }
    if (*enabled &&
        std::find(registered_extension_ids.begin(),
                  registered_extension_ids.end(), *id) !=
            registered_extension_ids.end()) {
      available = true;
    }
  }
  return available;
}

EffectivePasswordProvider ComputeEffectivePasswordProvider(
    std::string_view settings_json,
    std::string_view registry_json,
    std::string_view installed_extensions_json) {
  std::optional<base::Value> settings =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!settings || !settings->is_dict()) {
    return EffectivePasswordProvider::kDisabled;
  }

  const base::DictValue* autofill = settings->GetDict().FindDict("autofill");
  if (!autofill) {
    return EffectivePasswordProvider::kDisabled;
  }
  const std::optional<bool> passwords_enabled =
      autofill->FindBool("passwordsEnabled");
  const std::string* selected_mode =
      autofill->FindString("passwordProvider");
  if (!passwords_enabled.has_value() || !*passwords_enabled ||
      !selected_mode) {
    return EffectivePasswordProvider::kDisabled;
  }

  const std::optional<EffectivePasswordProvider> selected_provider =
      ParseProviderMode(*selected_mode);
  if (!selected_provider ||
      *selected_provider == EffectivePasswordProvider::kDisabled) {
    return EffectivePasswordProvider::kDisabled;
  }

  std::vector<std::string> registered_extension_ids;
  if (!ParseProviderRegistry(registry_json, *selected_provider,
                             &registered_extension_ids)) {
    return EffectivePasswordProvider::kDisabled;
  }

  if (*selected_provider == EffectivePasswordProvider::kMahoNative) {
    return EffectivePasswordProvider::kMahoNative;
  }
  if (!IsRegisteredProviderExtensionAvailable(installed_extensions_json,
                                               registered_extension_ids)) {
    return EffectivePasswordProvider::kDisabled;
  }
  return *selected_provider;
}

std::optional<std::string> TakeMahoString(char* value) {
  if (!value) {
    return std::nullopt;
  }
  std::string result(value);
  maho_string_free(value);
  return result;
}

std::optional<std::string> GetInstalledExtensionsForProfileKey(
    MahoCore* core,
    std::string_view profile_key) {
  if (!core || profile_key.empty()) {
    return std::nullopt;
  }
  const std::string key(profile_key);
  return TakeMahoString(
      maho_core_get_installed_extensions_for_profile(core, key.c_str()));
}

bool IsProviderAvailableForProfileKey(std::string_view mode,
                                      std::string_view profile_key) {
  const std::optional<EffectivePasswordProvider> provider =
      ParseProviderMode(mode);
  if (!provider || *provider == EffectivePasswordProvider::kDisabled) {
    return false;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  std::vector<std::string> extension_ids;
  if (!ParseProviderRegistry(maho::core::GetPasswordProviderRegistry(),
                             *provider, &extension_ids)) {
    return false;
  }
  if (*provider == EffectivePasswordProvider::kMahoNative) {
    return true;
  }

  std::optional<std::string> installed_extensions =
      GetInstalledExtensionsForProfileKey(core, profile_key);
  return installed_extensions && IsRegisteredProviderExtensionAvailable(
                                     *installed_extensions, extension_ids);
}

// Legacy no-argument predicates below are keyed by the profile that owns the
// browser-global core, not by the literal "default" key: extension snapshots
// are stored per space-id (see MahoExtensionStateBridge, which keys on
// MahoSpaceProfileBridge::GetSpaceIdForProfile). Falling back to
// kDefaultProfileKey only when no owner profile is available keeps the
// pre-existing fail-closed behaviour (an unknown key resolves to a snapshot
// with no provider extensions, i.e. disabled).
std::string GetCoreOwnerProfileKey() {
  Profile* owner_profile = maho::GetCoreOwnerProfile();
  if (!owner_profile) {
    return std::string(kDefaultProfileKey);
  }
  std::string space_id =
      MahoSpaceProfileBridge::GetInstance()->GetSpaceIdForProfile(
          owner_profile);
  if (space_id.empty()) {
    return std::string(kDefaultProfileKey);
  }
  return space_id;
}

EffectivePasswordProviderDisabledReason VaultPreflightDisabledReason() {
  switch (maho::core::GetVaultPreflightState()) {
    case maho::core::VaultPreflightState::kUnrecoverableKey:
      return EffectivePasswordProviderDisabledReason::kVaultUnrecoverableKey;
    case maho::core::VaultPreflightState::kStructuralCorruption:
      return EffectivePasswordProviderDisabledReason::
          kVaultStructuralCorruption;
    case maho::core::VaultPreflightState::kPlaintextResidue:
      return EffectivePasswordProviderDisabledReason::kVaultPlaintextResidue;
    case maho::core::VaultPreflightState::kUnavailable:
    case maho::core::VaultPreflightState::kHealthy:
    case maho::core::VaultPreflightState::kLocked:
      return EffectivePasswordProviderDisabledReason::kNone;
  }
}

}  // namespace

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kMahoNativePasswordWriteEnabled, true);
}

bool IsNativePasswordWriteEnabled(PrefService* prefs) {
  if (!prefs) {
    return false;
  }
  // The kill switch is only honored once the pref is registered; an unknown
  // pref means the switch has not been provisioned, so fail closed.
  if (!prefs->FindPreference(prefs::kMahoNativePasswordWriteEnabled)) {
    return false;
  }
  return prefs->GetBoolean(prefs::kMahoNativePasswordWriteEnabled);
}

PrefService* GetNativePasswordWritePrefs() {
  if (g_native_write_prefs_for_testing) {
    return g_native_write_prefs_for_testing;
  }
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

ScopedNativePasswordWritePrefsForTesting::
    ScopedNativePasswordWritePrefsForTesting(PrefService* prefs)
    : previous_(g_native_write_prefs_for_testing) {
  g_native_write_prefs_for_testing = prefs;
}

ScopedNativePasswordWritePrefsForTesting::
    ~ScopedNativePasswordWritePrefsForTesting() {
  g_native_write_prefs_for_testing = previous_;
}

std::string NormalizePasswordProviderMode(std::string_view mode) {
  const std::optional<EffectivePasswordProvider> provider =
      ParseProviderMode(mode);
  return std::string(ProviderMode(
      provider.value_or(EffectivePasswordProvider::kDisabled)));
}

EffectivePasswordProviderResult
GetEffectivePasswordProviderResultForProfileKey(std::string_view profile_key) {
  EffectivePasswordProviderResult result;
  const EffectivePasswordProviderDisabledReason vault_reason =
      VaultPreflightDisabledReason();
  result.disabled_reason =
      vault_reason == EffectivePasswordProviderDisabledReason::kNone
          ? EffectivePasswordProviderDisabledReason::kGenericDisabled
          : vault_reason;

  MahoCore* core = maho::GetCore();
  if (!core) {
    return result;
  }

  std::optional<std::string> settings =
      TakeMahoString(maho_core_get_settings(core));
  std::optional<std::string> installed_extensions =
      GetInstalledExtensionsForProfileKey(core, profile_key);
  if (!settings || !installed_extensions) {
    return result;
  }
  result.provider = ComputeEffectivePasswordProvider(
      *settings, maho::core::GetPasswordProviderRegistry(),
      *installed_extensions);
  if (result.provider != EffectivePasswordProvider::kMahoNative) {
    // Preserve available external providers exactly as Task 2 selected them;
    // only enrich an already-disabled result with Vault-specific diagnosis.
    if (result.provider != EffectivePasswordProvider::kDisabled) {
      result.disabled_reason = EffectivePasswordProviderDisabledReason::kNone;
      return result;
    }
  }

  if (vault_reason != EffectivePasswordProviderDisabledReason::kNone) {
    result.provider = EffectivePasswordProvider::kDisabled;
  } else if (result.provider != EffectivePasswordProvider::kDisabled) {
    result.disabled_reason = EffectivePasswordProviderDisabledReason::kNone;
  }
  return result;
}

EffectivePasswordProvider GetEffectivePasswordProviderForProfileKey(
    std::string_view profile_key) {
  return GetEffectivePasswordProviderResultForProfileKey(profile_key).provider;
}

EffectivePasswordProvider ComputeEffectivePasswordProviderForTesting(
    std::string_view settings_json,
    std::string_view registry_json,
    std::string_view installed_extensions_json) {
  return ComputeEffectivePasswordProvider(
      settings_json, registry_json, installed_extensions_json);
}

std::string GetActivePasswordProviderMode() {
  return GetActivePasswordProviderModeForProfileKey(GetCoreOwnerProfileKey());
}

std::string GetActivePasswordProviderModeForProfileKey(
    std::string_view profile_key) {
  return std::string(
      ProviderMode(GetEffectivePasswordProviderForProfileKey(profile_key)));
}

bool IsNativePasswordProviderActive() {
  return GetEffectivePasswordProviderForProfileKey(GetCoreOwnerProfileKey()) ==
         EffectivePasswordProvider::kMahoNative;
}

bool IsVaultDeviceReauthRequired() {
#if BUILDFLAG(IS_LINUX)
  // Linux desktop has no platform DeviceAuthenticator (the factory returns
  // null), so device reauth can never succeed there. The Vault unlock is the
  // authorization boundary; requiring reauth would reject every Vault write.
  return false;
#else
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }
  std::optional<std::string> settings =
      TakeMahoString(maho_core_get_settings(core));
  if (!settings) {
    return true;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*settings, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return true;
  }
  const base::DictValue* autofill = parsed->GetDict().FindDict("autofill");
  const std::optional<bool> required =
      autofill ? autofill->FindBool("vaultRequireDeviceAuth") : std::nullopt;
  return required.value_or(true);
#endif
}

bool ShouldEnableChromiumPasswordManager() {
  return GetEffectivePasswordProviderForProfileKey(GetCoreOwnerProfileKey()) ==
         EffectivePasswordProvider::kMahoNative;
}

bool IsBitwardenExtensionAvailable() {
  return IsProviderAvailableForProfileKey(kBitwardenProviderMode,
                                          GetCoreOwnerProfileKey());
}

bool IsOnePasswordExtensionAvailable() {
  return IsProviderAvailableForProfileKey(kOnePasswordProviderMode,
                                          GetCoreOwnerProfileKey());
}

bool IsPasswordProviderAvailable(std::string_view mode) {
  return IsProviderAvailableForProfileKey(mode, GetCoreOwnerProfileKey());
}

bool IsChromiumAccountStoreDisabled() {
  return IsNativePasswordProviderActive();
}

bool IsGooglePasswordsSyncDisabled() {
  return IsNativePasswordProviderActive();
}

}  // namespace maho::passwords
