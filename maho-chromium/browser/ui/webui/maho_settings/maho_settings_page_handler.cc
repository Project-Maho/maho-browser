// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/base64.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/native_ui_types.h"

namespace {

namespace ai = maho::ai_prefs;

constexpr char kSettingsSyncRelayUrl[] = "https://sync.maho.app";
constexpr char kThemeStoreCacheFileName[] = "maho_theme_store_manifest_cache.json";
constexpr char kPasswordProviderModeMahoKey[] = "autofill.password_provider";
constexpr char kPasswordsEnabledMahoKey[] = "autofill.passwords_enabled";

// ── Pref-based settings mapping ──
constexpr char kHardwareAccelerationMahoKey[] =
    "advanced.hardware_acceleration";
constexpr char kPrivacyBlockThirdPartyCookiesKey[] =
    "privacy.block_third_party_cookies";

struct SettingMapping {
  const char* maho_key;
  const char* pref_path;
  enum class Type { kString, kBool, kInt } type;
};

constexpr int kCookieControlsBlockThirdPartyValue =
    static_cast<int>(content_settings::CookieControlsMode::kBlockThirdParty);
constexpr int kCookieControlsOffValue =
    static_cast<int>(content_settings::CookieControlsMode::kOff);
constexpr int kWebKitMinimumFontSizeMinValue = 1;
constexpr int kWebKitMinimumFontSizeMaxValue = 24;
constexpr int kNetworkPredictionOptionsMinValue = 0;
constexpr int kNetworkPredictionOptionsMaxValue = 3;
constexpr int kNetworkPredictionOptionsStandardValue =
    static_cast<int>(prefetch::NetworkPredictionOptions::kStandard);
constexpr int kNetworkPredictionOptionsWifiOnlyDeprecatedValue =
    static_cast<int>(prefetch::NetworkPredictionOptions::kWifiOnlyDeprecated);

constexpr SettingMapping kSettingsMap[] = {
    {"general.homepage", prefs::kHomePage, SettingMapping::Type::kString},
    {"appearance.show_home_button", prefs::kShowHomeButton,
     SettingMapping::Type::kBool},
    {"general.restore_on_startup", prefs::kRestoreOnStartup,
     SettingMapping::Type::kInt},
    {"appearance.font_size", prefs::kWebKitDefaultFontSize,
     SettingMapping::Type::kInt},
    {"appearance.minimum_font_size", prefs::kWebKitMinimumFontSize,
     SettingMapping::Type::kInt},
    {"appearance.theme", prefs::kBrowserColorScheme,
     SettingMapping::Type::kInt},
    {"search.suggestions", prefs::kSearchSuggestEnabled,
     SettingMapping::Type::kBool},
    {"privacy.do_not_track", prefs::kEnableDoNotTrack,
     SettingMapping::Type::kBool},
    {"privacy.block_third_party_cookies", prefs::kCookieControlsMode,
     SettingMapping::Type::kInt},
    {"privacy.safe_browsing", prefs::kSafeBrowsingEnabled,
     SettingMapping::Type::kBool},
    {kHardwareAccelerationMahoKey, prefs::kHardwareAccelerationModeEnabled,
     SettingMapping::Type::kBool},
    {"advanced.preload_pages", prefs::kNetworkPredictionOptions,
     SettingMapping::Type::kInt},
    {"general.prompt_for_download", prefs::kPromptForDownload,
     SettingMapping::Type::kBool},
    {"notifications.quiet_permission_ui",
      prefs::kEnableQuietNotificationPermissionUi,
      SettingMapping::Type::kBool},
};

maho_settings::mojom::PasswordProviderKind ToPasswordProviderKind(
    const std::string& mode) {
  if (mode == "bitwarden") {
    return maho_settings::mojom::PasswordProviderKind::kBitwarden;
  }
  if (mode == "onepassword") {
    return maho_settings::mojom::PasswordProviderKind::kOnePassword;
  }
  return maho_settings::mojom::PasswordProviderKind::kMahoNative;
}

std::string NormalizePasswordProviderMode(const std::string& mode) {
  return maho::passwords::NormalizePasswordProviderMode(mode);
}

maho_settings::mojom::PasswordProviderCapabilitiesPtr
MakePasswordProviderCapabilities(maho_settings::mojom::PasswordProviderKind kind) {
  auto capabilities = maho_settings::mojom::PasswordProviderCapabilities::New();
  const bool native = kind == maho_settings::mojom::PasswordProviderKind::kMahoNative;
  capabilities->can_list_saved_passwords = native;
  capabilities->can_search_saved_passwords = native;
  capabilities->can_delete_saved_passwords = native;
  capabilities->can_add_saved_passwords = native;
  capabilities->can_edit_saved_passwords = native;
  capabilities->can_reveal_passwords = native;
  return capabilities;
}

std::vector<maho_settings::mojom::SavedPasswordPtr> ParseSavedPasswordsJson(
    const std::string& json) {
  std::vector<maho_settings::mojom::SavedPasswordPtr> passwords;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return passwords;
  }

  for (const base::Value& value : parsed->GetList()) {
    if (!value.is_dict()) {
      continue;
    }
    const auto& dict = value.GetDict();
    auto password = maho_settings::mojom::SavedPassword::New();
    if (const std::string* id = dict.FindString("id")) {
      password->id = *id;
    }
    if (const std::string* domain = dict.FindString("domain")) {
      password->domain = *domain;
    }
    if (const std::string* username = dict.FindString("username")) {
      password->username = *username;
    }
    if (const std::string* created_at = dict.FindString("createdAt")) {
      password->created_at = *created_at;
    }
    if (const std::string* last_used = dict.FindString("lastUsed")) {
      password->last_used = *last_used;
    }
    if (const std::string* password_value = dict.FindString("password")) {
      password->password = *password_value;
    }
    passwords.push_back(std::move(password));
  }

  return passwords;
}

std::vector<maho_settings::mojom::SavedPasswordPtr> GetSavedPasswordsFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return {};
  }

  char* json_str = maho_core_search_passwords(core, "");
  if (!json_str) {
    return {};
  }

  std::string json(json_str);
  maho_string_free(json_str);
  return ParseSavedPasswordsJson(json);
}

bool UpdateAutofillSettingsInCore(bool passwords_enabled,
                                  const std::string& mode) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }

  base::DictValue autofill;
  autofill.Set("passwordsEnabled", passwords_enabled);
  autofill.Set("passwordProvider", mode);

  base::DictValue settings;
  settings.Set("autofill", std::move(autofill));

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(settings)), &json);
  maho_core_update_settings(core, json.c_str());
  return true;
}

const SettingMapping* FindMapping(const std::string& maho_key) {
  for (const auto& m : kSettingsMap) {
    if (maho_key == m.maho_key) {
      return &m;
    }
  }
  return nullptr;
}

std::string ReadPasswordProviderModeFromCore() {
  return maho::passwords::GetActivePasswordProviderMode();
}

bool ReadPasswordsEnabledFromCore() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return true;
  }

  char* json_str = maho_core_get_settings(core);
  if (!json_str) {
    return true;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return true;
  }

  const base::DictValue& root = parsed->GetDict();
  const base::DictValue* autofill = root.FindDict("autofill");
  if (!autofill) {
    return true;
  }

  const std::string* mode = autofill->FindString("passwordProvider");
  if (mode && *mode == "disabled") {
    return false;
  }

  std::optional<bool> passwords_enabled = autofill->FindBool("passwordsEnabled");
  if (passwords_enabled) {
    return *passwords_enabled;
  }

  return true;
}

PrefService* GetPrefServiceForKey(Profile* profile,
                                  const std::string& maho_key) {
  if (maho_key == kHardwareAccelerationMahoKey) {
    return g_browser_process->local_state();
  }
  return profile->GetPrefs();
}

// ── Profile domain helpers (thread pool) ──

std::vector<maho_settings::mojom::ProfileInfoPtr> BuildProfilesOnPool() {
  std::vector<maho_settings::mojom::ProfileInfoPtr> profiles;
  MahoCore* core = maho::GetCore();
  if (!core) return profiles;

  char* json_str = maho_core_list_profiles(core);
  if (!json_str) return profiles;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return profiles;

  char* active_id_str = maho_core_get_active_profile_id(core);
  std::string active_id;
  if (active_id_str) {
    active_id = active_id_str;
    maho_string_free(active_id_str);
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    std::optional<bool> is_default = dict->FindBool("is_default");
    if (!id || !name) continue;

    auto profile = maho_settings::mojom::ProfileInfo::New();
    profile->id = *id;
    profile->name = *name;
    profile->is_default = is_default.value_or(false);
    profile->is_active = (*id == active_id);
    profiles.push_back(std::move(profile));
  }

  char* spaces_json_str = maho_core_get_space_view_models(core);
  if (spaces_json_str) {
    std::string spaces_json(spaces_json_str);
    maho_string_free(spaces_json_str);
    std::optional<base::Value> spaces_parsed =
        base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
    if (spaces_parsed && spaces_parsed->is_list()) {
      for (auto& p : profiles) {
        for (const auto& space_item : spaces_parsed->GetList()) {
          const auto* space_dict = space_item.GetIfDict();
          if (!space_dict) continue;
          const std::string* space_id = space_dict->FindString("id");
          const std::string* space_profile =
              space_dict->FindString("profile_id");
          if (!space_id) continue;
          std::string effective_profile =
              (space_profile && !space_profile->empty()) ? *space_profile
                                                         : "default";
          if (effective_profile == p->id) {
            p->space_ids.push_back(*space_id);
          }
        }
      }
    }
  }

  return profiles;
}

maho_settings::mojom::ProfileInfoPtr CreateProfileOnPool(
    const std::string& name) {
  MahoCore* core = maho::GetCore();
  if (!core) return nullptr;

  char* result_str = maho_core_create_profile(core, name.c_str());
  if (!result_str) return nullptr;
  std::string result(result_str);
  maho_string_free(result_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(result, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return nullptr;

  const auto& dict = parsed->GetDict();
  const std::string* id = dict.FindString("id");
  if (!id) return nullptr;

  auto profile = maho_settings::mojom::ProfileInfo::New();
  profile->id = *id;
  profile->name = name;
  profile->is_default = false;
  profile->is_active = false;
  return profile;
}

bool DeleteProfileOnPool(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_delete_profile(core, profile_id.c_str());
}

bool SwitchProfileOnPool(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_switch_profile(core, profile_id.c_str());
}

// ── ATC domain helpers (thread pool) ──

std::vector<maho_settings::mojom::ATCRulePtr> BuildATCRulesOnPool() {
  std::vector<maho_settings::mojom::ATCRulePtr> rules;
  MahoCore* core = maho::GetCore();
  if (!core) return rules;

  char* json_str = maho_core_get_atc_rules(core);
  if (!json_str) return rules;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return rules;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* id = dict->FindString("id");
    const std::string* pattern = dict->FindString("url_pattern");
    const std::string* target = dict->FindString("target_space_id");
    std::optional<bool> enabled = dict->FindBool("enabled");
    if (!id || !pattern || !target) continue;

    auto rule = maho_settings::mojom::ATCRule::New();
    rule->id = *id;
    rule->url_pattern = *pattern;
    rule->target_space_id = *target;
    rule->enabled = enabled.value_or(true);
    rules.push_back(std::move(rule));
  }

  return rules;
}

maho_settings::mojom::ATCRulePtr AddATCRuleOnPool(
    const std::string& url_pattern,
    const std::string& target_space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return nullptr;

  base::DictValue rule_dict;
  rule_dict.Set("url_pattern", url_pattern);
  rule_dict.Set("target_space_id", target_space_id);
  std::string rule_json;
  base::JSONWriter::Write(rule_dict, &rule_json);

  char* result_str = maho_core_add_atc_rule(core, rule_json.c_str());
  if (!result_str) return nullptr;
  std::string result(result_str);
  maho_string_free(result_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(result, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return nullptr;

  const auto& dict = parsed->GetDict();
  const std::string* id = dict.FindString("id");
  if (!id) return nullptr;

  auto rule = maho_settings::mojom::ATCRule::New();
  rule->id = *id;
  rule->url_pattern = url_pattern;
  rule->target_space_id = target_space_id;
  rule->enabled = true;
  return rule;
}

void RemoveATCRuleOnPool(const std::string& rule_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  char* result = maho_core_remove_atc_rule(core, rule_id.c_str());
  if (result) maho_string_free(result);
}

void ToggleATCRuleOnPool(const std::string& rule_id, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_toggle_atc_rule(core, rule_id.c_str(), enabled);
}

// ── Shortcuts domain helpers (thread pool) ──

std::vector<maho_settings::mojom::ShortcutBindingPtr>
BuildShortcutsOnPool() {
  std::vector<maho_settings::mojom::ShortcutBindingPtr> shortcuts;
  MahoCore* core = maho::GetCore();
  if (!core) return shortcuts;

  char* json_str = maho_core_get_shortcuts(core);
  if (!json_str) return shortcuts;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return shortcuts;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* action = dict->FindString("action");
    const std::string* label = dict->FindString("label");
    const std::string* key_combo = dict->FindString("key_combo");
    std::optional<bool> is_default = dict->FindBool("is_default");
    std::optional<bool> enabled = dict->FindBool("enabled");
    if (!action) continue;

    auto shortcut = maho_settings::mojom::ShortcutBinding::New();
    shortcut->action = *action;
    shortcut->label = label ? *label : *action;
    shortcut->key_combo = key_combo ? *key_combo : "";
    shortcut->is_default = is_default.value_or(true);
    shortcut->enabled = enabled.value_or(true);
    shortcuts.push_back(std::move(shortcut));
  }

  return shortcuts;
}

bool SetShortcutOnPool(const std::string& action,
                       const std::string& key_combo) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  char* result = maho_core_set_shortcut(core, action.c_str(),
                                        key_combo.c_str());
  bool ok = result != nullptr;
  if (result) maho_string_free(result);
  return ok;
}

void ResetShortcutOnPool(const std::string& action) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_reset_shortcut(core, action.c_str());
}

void ResetAllShortcutsOnPool() {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_reset_all_shortcuts(core);
}

void ToggleShortcutOnPool(const std::string& action, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_toggle_shortcut(core, action.c_str(), enabled);
}

// ── Sync domain helpers (thread pool) ──

maho_settings::mojom::SyncStatusPtr BuildSyncStatusOnPool() {
  auto status = maho_settings::mojom::SyncStatus::New();
  MahoCore* core = maho::GetCore();
  if (!core) {
    status->is_syncing = false;
    status->status_label = "offline";
    return status;
  }

  char* json_str = maho_core_get_sync_status(core);
  if (!json_str) {
    status->is_syncing = false;
    status->status_label = "offline";
    return status;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    status->is_syncing = false;
    status->status_label = "offline";
    return status;
  }

  const auto& dict = parsed->GetDict();
  status->is_syncing = dict.FindBool("is_syncing").value_or(false);
  if (const std::string* url = dict.FindString("relay_url"))
    status->relay_url = *url;
  if (const std::string* room = dict.FindString("room_id"))
    status->room_id = *room;
  if (const std::string* label = dict.FindString("status_label"))
    status->status_label = *label;
  else
    status->status_label = "idle";
  if (const std::string* err = dict.FindString("error_message"))
    status->error_message = *err;
  status->last_sync_timestamp =
      static_cast<int64_t>(dict.FindDouble("last_sync_timestamp").value_or(0));

  return status;
}

std::vector<maho_settings::mojom::SyncDeviceInfoPtr>
BuildSyncDevicesOnPool() {
  std::vector<maho_settings::mojom::SyncDeviceInfoPtr> devices;
  MahoCore* core = maho::GetCore();
  if (!core) return devices;

  char* json_str = maho_core_get_connected_devices(core);
  if (!json_str) return devices;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return devices;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id || !name) continue;

    auto device = maho_settings::mojom::SyncDeviceInfo::New();
    device->id = *id;
    device->name = *name;
    if (const std::string* dtype = dict->FindString("device_type"))
      device->device_type = *dtype;
    device->is_online = dict->FindBool("is_online").value_or(false);
    devices.push_back(std::move(device));
  }

  return devices;
}

struct GenerateSyncKeyResult {
  std::string sync_key;
  std::string room_id;
  std::string recovery_phrase;
};

GenerateSyncKeyResult GenerateSyncKeyOnPool() {
  GenerateSyncKeyResult result;
  MahoCore* core = maho::GetCore();
  if (!core) return result;

  char* json_str = maho_core_generate_sync_key(core);
  if (!json_str) return result;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return result;

  const auto& dict = parsed->GetDict();
  if (const std::string* key = dict.FindString("syncKey"))
    result.sync_key = *key;
  if (const std::string* room = dict.FindString("roomId"))
    result.room_id = *room;
  if (const std::string* phrase = dict.FindString("recoveryPhrase"))
    result.recovery_phrase = *phrase;

  return result;
}

void StartSyncOnPool(const std::string& sync_key) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_start_sync(core, kSettingsSyncRelayUrl,
                       sync_key.c_str());
}

maho_settings::mojom::SyncStatusPtr JoinSyncOnPool(
    const std::string& recovery_phrase) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    auto s = maho_settings::mojom::SyncStatus::New();
    s->status_label = "error";
    s->error_message = "Core not available";
    return s;
  }
  char* result = maho_core_join_sync(core, recovery_phrase.c_str());
  if (result) maho_string_free(result);
  return BuildSyncStatusOnPool();
}

void StopSyncOnPool() {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_stop_sync(core);
}

// ── Theme / Spaces domain helpers (thread pool) ──

bool ApplyThemeToSpaceOnPool(const std::string& space_id,
                             const std::string& theme_json) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;

  std::optional<base::Value> theme_value =
      base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
  if (!theme_value) return false;

  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("theme", std::move(*theme_value));
  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (result) maho_string_free(result);
  return true;
}

std::vector<maho_settings::mojom::SpaceBasicInfoPtr> BuildSpacesOnPool() {
  std::vector<maho_settings::mojom::SpaceBasicInfoPtr> spaces;
  MahoCore* core = maho::GetCore();
  if (!core) return spaces;

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) return spaces;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return spaces;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id) continue;

    auto space = maho_settings::mojom::SpaceBasicInfo::New();
    space->id = *id;
    space->name = name ? *name : *id;
    spaces.push_back(std::move(space));
  }

  return spaces;
}

// ── Autofill domain (read-only) ──

std::vector<maho_settings::mojom::AutofillAddressPtr>
BuildAutofillAddressesOnPool() {
  std::vector<maho_settings::mojom::AutofillAddressPtr> addresses;
  MahoCore* core = maho::GetCore();
  if (!core) return addresses;

  char* json_str = maho_core_get_autofill_addresses(core);
  if (!json_str) return addresses;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return addresses;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    auto addr = maho_settings::mojom::AutofillAddress::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    addr->id = s("id");
    addr->name = s("name");
    addr->address_line1 = s("address_line1");
    addr->address_line2 = s("address_line2");
    addr->city = s("city");
    addr->state = s("state");
    addr->postal_code = s("postal_code");
    addr->country = s("country");
    addr->phone = s("phone");
    addr->email = s("email");
    addresses.push_back(std::move(addr));
  }

  return addresses;
}

std::vector<maho_settings::mojom::AutofillPaymentPtr>
BuildAutofillPaymentsOnPool() {
  std::vector<maho_settings::mojom::AutofillPaymentPtr> payments;
  MahoCore* core = maho::GetCore();
  if (!core) return payments;

  char* json_str = maho_core_get_autofill_payments(core);
  if (!json_str) return payments;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return payments;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    auto pay = maho_settings::mojom::AutofillPayment::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    pay->id = s("id");
    pay->card_network = "";
    pay->last_four = s("lastFour");
    pay->expiration = s("expiry");
    pay->cardholder_name = s("cardName");
    payments.push_back(std::move(pay));
  }

  return payments;
}

// ── Content Blocker domain ──

std::vector<maho_settings::mojom::FilterListInfoPtr>
BuildFilterListsOnPool() {
  std::vector<maho_settings::mojom::FilterListInfoPtr> lists;
  MahoCore* core = maho::GetCore();
  if (!core) return lists;

  char* json_str = maho_core_get_filter_lists(core);
  if (!json_str) return lists;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return lists;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    auto fl = maho_settings::mojom::FilterListInfo::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    fl->id = s("id");
    fl->name = s("name");
    fl->url = s("url");
    fl->enabled = dict->FindBool("enabled").value_or(true);
    fl->rule_count = static_cast<uint32_t>(
        dict->FindInt("rule_count").value_or(0));
    lists.push_back(std::move(fl));
  }

  return lists;
}

maho_settings::mojom::ContentBlockerStatsPtr BuildContentBlockerStatsOnPool() {
  auto stats = maho_settings::mojom::ContentBlockerStats::New();
  MahoCore* core = maho::GetCore();
  if (!core) return stats;

  stats->total_rule_count =
      static_cast<uint32_t>(maho_core_get_content_rule_count(core));

  char* json_str = maho_core_get_filter_lists(core);
  if (json_str) {
    std::string json(json_str);
    maho_string_free(json_str);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_list()) {
      stats->filter_list_count =
          static_cast<uint32_t>(parsed->GetList().size());
    }
  }

  return stats;
}

maho_settings::mojom::ContentBlockerStatsPtr RebuildContentRulesOnPool() {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_rebuild_content_rules(core);
  }
  return BuildContentBlockerStatsOnPool();
}

bool AddFilterListOnPool(const std::string& id,
                          const std::string& name,
                          const std::string& url) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  maho_core_add_filter_list(core, id.c_str(), name.c_str(), url.c_str());
  return true;
}

void ToggleFilterListOnPool(const std::string& id, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_toggle_filter_list(core, id.c_str(), enabled);
}

void RemoveFilterListOnPool(const std::string& id) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_remove_filter_list(core, id.c_str());
}

// ── Extensions domain (read-only) ──

std::vector<maho_settings::mojom::InstalledExtensionPtr>
BuildInstalledExtensionsOnPool() {
  std::vector<maho_settings::mojom::InstalledExtensionPtr> exts;
  MahoCore* core = maho::GetCore();
  if (!core) return exts;

  char* json_str = maho_core_get_installed_extensions(core);
  if (!json_str) return exts;
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return exts;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    auto ext = maho_settings::mojom::InstalledExtension::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    ext->id = s("id");
    ext->name = s("name");
    ext->version = s("version");
    ext->description = s("description");
    ext->enabled = dict->FindBool("enabled").value_or(true);
    exts.push_back(std::move(ext));
  }

  return exts;
}

std::optional<std::string> ReadCachedThemeStoreFromDiskOnPool(
    const base::FilePath& cache_path) {
  if (cache_path.empty()) {
    return std::nullopt;
  }

  std::string cached_json;
  if (!base::ReadFileToString(cache_path, &cached_json) || cached_json.empty()) {
    return std::nullopt;
  }

  return cached_json;
}

void WriteCachedThemeStoreToDiskOnPool(base::FilePath cache_path,
                                       std::string cached_theme_store_json) {
  if (cache_path.empty() || cached_theme_store_json.empty()) {
    return;
  }

  base::WriteFile(cache_path, cached_theme_store_json);
}

std::optional<maho_settings::mojom::AutofillAddressPtr>
AddAutofillAddressOnPool(const std::string& id,
                         const std::string& name,
                         const std::string& address_line1,
                         const std::string& address_line2,
                         const std::string& city,
                         const std::string& state,
                         const std::string& postal_code,
                         const std::string& country,
                         const std::string& phone,
                         const std::string& email) {
  MahoCore* core = maho::GetCore();
  if (!core) return std::nullopt;
  base::DictValue address;
  address.Set("id", id);
  address.Set("name", name);
  address.Set("street", address_line1);
  address.Set("city", city);
  address.Set("state", state);
  address.Set("zip", postal_code);
  address.Set("country", country);
  address.Set("phone", phone);
  address.Set("email", email);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(address)), &json);
  char* result = maho_core_add_autofill_address_persisted(core, json.c_str());
  if (!result) return std::nullopt;
  std::string result_json(result);
  maho_string_free(result);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return std::nullopt;
  const auto& dict = parsed->GetDict();
  auto out = maho_settings::mojom::AutofillAddress::New();
  auto s = [&](const char* k) -> std::string {
    const std::string* v = dict.FindString(k);
    return v ? *v : "";
  };
  out->id = s("id");
  out->name = s("name");
  out->address_line1 = s("street");
  out->address_line2 = "";
  out->city = s("city");
  out->state = s("state");
  out->postal_code = s("zip");
  out->country = s("country");
  out->phone = s("phone");
  out->email = s("email");
  return out;
}

bool DeleteAutofillAddressOnPool(const std::string& id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_delete_autofill_address_persisted(core, id.c_str());
}

std::optional<maho_settings::mojom::AutofillPaymentPtr>
AddAutofillPaymentOnPool(const std::string& id,
                         const std::string& card_network,
                         const std::string& last_four,
                         const std::string& expiration,
                         const std::string& cardholder_name) {
  MahoCore* core = maho::GetCore();
  if (!core) return std::nullopt;
  base::DictValue payment;
  payment.Set("id", id);
  payment.Set("cardNetwork", card_network);
  payment.Set("cardName", cardholder_name);
  payment.Set("lastFour", last_four);
  payment.Set("expiry", expiration);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(payment)), &json);
  char* result = maho_core_add_autofill_payment_persisted(core, json.c_str());
  if (!result) return std::nullopt;
  std::string result_json(result);
  maho_string_free(result);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return std::nullopt;
  const auto& dict = parsed->GetDict();
  auto out = maho_settings::mojom::AutofillPayment::New();
  auto s = [&](const char* k) -> std::string {
    const std::string* v = dict.FindString(k);
    return v ? *v : "";
  };
  out->id = s("id");
  out->card_network = "";
  out->last_four = s("lastFour");
  out->expiration = s("expiry");
  out->cardholder_name = s("cardName");
  return out;
}

bool DeleteAutofillPaymentOnPool(const std::string& id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_delete_autofill_payment_persisted(core, id.c_str());
}

}  // namespace

// ── Constructor / Destructor ──

MahoSettingsPageHandler::MahoSettingsPageHandler(
    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_settings::mojom::Page> page,
    Profile* profile)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile),
      prefs_(profile->GetPrefs()),
      local_state_(g_browser_process->local_state()) {
  pref_registrar_.Init(prefs_);
  if (local_state_) {
    local_state_pref_registrar_.Init(local_state_);
  }
  for (const auto& m : kSettingsMap) {
    PrefChangeRegistrar* registrar =
        (std::string_view(m.pref_path) ==
             prefs::kHardwareAccelerationModeEnabled &&
         local_state_)
            ? &local_state_pref_registrar_
            : &pref_registrar_;
    registrar->Add(m.pref_path,
                   base::BindRepeating(&MahoSettingsPageHandler::OnPrefChanged,
                                       weak_factory_.GetWeakPtr()));
  }

  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoSettingsPageHandler::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()),
        os_crypt_async::Encryptor::Option::kEncryptSyncCompat);
  }

  LoadCachedThemeStoreFromDisk();


  if (auto* bridge = maho::MahoExtensionStateBridge::FromProfile(profile_)) {
    ext_bridge_observation_.Observe(bridge);
  }
}

MahoSettingsPageHandler::~MahoSettingsPageHandler() = default;

base::FilePath MahoSettingsPageHandler::GetThemeStoreCachePath() const {
  if (!profile_) {
    return base::FilePath();
  }
  return profile_->GetPath().AppendASCII(kThemeStoreCacheFileName);
}

void MahoSettingsPageHandler::LoadCachedThemeStoreFromDisk() {
  const base::FilePath cache_path = GetThemeStoreCachePath();
  if (cache_path.empty()) {
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ReadCachedThemeStoreFromDiskOnPool, cache_path),
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> self,
             std::optional<std::string> cached_json) {
            if (!self || !cached_json || cached_json->empty()) {
              return;
            }
            self->cached_theme_store_json_ = std::move(*cached_json);
            self->last_theme_store_fetch_ = base::TimeTicks::Now();
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSettingsPageHandler::PersistCachedThemeStoreToDisk() const {
  if (cached_theme_store_json_.empty()) {
    return;
  }

  const base::FilePath cache_path = GetThemeStoreCachePath();
  if (cache_path.empty()) {
    return;
  }

  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&WriteCachedThemeStoreToDiskOnPool, cache_path,
                     cached_theme_store_json_));
}

// ── Key-value pref settings ──

// static
const char* MahoSettingsPageHandler::GetPrefPath(
    const std::string& maho_key) {
  const SettingMapping* m = FindMapping(maho_key);
  return m ? m->pref_path : nullptr;
}

std::string MahoSettingsPageHandler::ReadSetting(
    const std::string& maho_key) {
  if (maho_key == kPasswordProviderModeMahoKey) {
    return ReadPasswordProviderModeFromCore();
  }
  if (maho_key == kPasswordsEnabledMahoKey) {
    return ReadPasswordsEnabledFromCore() ? "true" : "false";
  }
  const SettingMapping* m = FindMapping(maho_key);
  if (m) {
    PrefService* prefs = GetPrefServiceForKey(profile_, maho_key);
    if (!prefs) return std::string();
    if (maho_key == kPrivacyBlockThirdPartyCookiesKey) {
      return prefs->GetInteger(m->pref_path) ==
                     kCookieControlsBlockThirdPartyValue
                 ? "true"
                 : "false";
    }
    switch (m->type) {
      case SettingMapping::Type::kString:
        return prefs->GetString(m->pref_path);
      case SettingMapping::Type::kBool:
        return prefs->GetBoolean(m->pref_path) ? "true" : "false";
      case SettingMapping::Type::kInt: {
        int int_value = prefs->GetInteger(m->pref_path);
        if (maho_key == "advanced.preload_pages" &&
            int_value == kNetworkPredictionOptionsWifiOnlyDeprecatedValue) {
          int_value = kNetworkPredictionOptionsStandardValue;
        }
        return base::NumberToString(int_value);
      }
    }
  }
  return std::string();
}

void MahoSettingsPageHandler::GetSettings(GetSettingsCallback callback) {
  std::move(callback).Run(BuildSettingsSnapshot());
}

void MahoSettingsPageHandler::SetSetting(const std::string& key,
                                          const std::string& value,
                                          SetSettingCallback callback) {
  if (HandleFfiSetting(key, value)) {
    std::move(callback).Run(true);
    ScheduleNotify();
    return;
  }

  if (key == kPasswordProviderModeMahoKey) {
    bool passwords_enabled = ReadPasswordsEnabledFromCore();
    std::string mode = "maho_native";
    if (value == "disabled") {
      passwords_enabled = false;
    } else {
      mode = NormalizePasswordProviderMode(value);
    }
    std::move(callback).Run(
        UpdateAutofillSettingsInCore(passwords_enabled, mode));
    ScheduleNotify();
    return;
  }

  if (key == kPasswordsEnabledMahoKey) {
    bool passwords_enabled = value == "true";
    std::move(callback).Run(UpdateAutofillSettingsInCore(
        passwords_enabled, ReadPasswordProviderModeFromCore()));
    ScheduleNotify();
    return;
  }

  const SettingMapping* m = FindMapping(key);
  if (m) {
    PrefService* prefs = GetPrefServiceForKey(profile_, key);
    if (!prefs) {
      std::move(callback).Run(false);
      return;
    }
    if (key == kPrivacyBlockThirdPartyCookiesKey) {
      prefs->SetInteger(m->pref_path,
                        value == "true" ? kCookieControlsBlockThirdPartyValue
                                        : kCookieControlsOffValue);
      std::move(callback).Run(true);
      return;
    }

    switch (m->type) {
      case SettingMapping::Type::kString:
        prefs->SetString(m->pref_path, value);
        break;
      case SettingMapping::Type::kBool:
        prefs->SetBoolean(m->pref_path, value == "true");
        break;
      case SettingMapping::Type::kInt: {
        int int_value = 0;
        if (!base::StringToInt(value, &int_value)) {
          std::move(callback).Run(false);
          return;
        }
        if (key == "appearance.font_size") {
          int_value = std::clamp(int_value, 9, 72);
        } else if (key == "appearance.minimum_font_size") {
          int_value = std::clamp(int_value, kWebKitMinimumFontSizeMinValue,
                                 kWebKitMinimumFontSizeMaxValue);
        } else if (key == "advanced.preload_pages") {
          int_value = std::clamp(int_value, kNetworkPredictionOptionsMinValue,
                                 kNetworkPredictionOptionsMaxValue);
        }
        prefs->SetInteger(m->pref_path, int_value);
        break;
      }
    }
    std::move(callback).Run(true);
    return;
  }

  std::move(callback).Run(false);
}

void MahoSettingsPageHandler::OnPrefChanged(const std::string& pref_name) {
  ScheduleNotify();
}

std::vector<maho_settings::mojom::SettingValuePtr>
MahoSettingsPageHandler::BuildSettingsSnapshot() {
  std::vector<maho_settings::mojom::SettingValuePtr> settings;
  auto add = [&](const std::string& key) {
    auto s = maho_settings::mojom::SettingValue::New();
    s->key = key;
    s->value = ReadSetting(key);
    settings.push_back(std::move(s));
  };
  for (const auto& m : kSettingsMap) {
    add(m.maho_key);
  }
  add(kPasswordProviderModeMahoKey);
  add(kPasswordsEnabledMahoKey);
  AppendFfiSettings(settings);
  return settings;
}

void MahoSettingsPageHandler::ScheduleNotify() {
  notify_timer_.Start(
      FROM_HERE, base::Milliseconds(50),
      base::BindOnce(&MahoSettingsPageHandler::FlushNotify,
                     weak_factory_.GetWeakPtr()));
}

void MahoSettingsPageHandler::FlushNotify() {
  std::vector<maho_settings::mojom::SettingValuePtr> settings =
      BuildSettingsSnapshot();

  std::string snapshot_key;
  snapshot_key.reserve(settings.size() * 32);
  for (const auto& s : settings) {
    snapshot_key += s->key;
    snapshot_key += '=';
    snapshot_key += s->value;
    snapshot_key += '\n';
  }

  if (snapshot_key == last_emitted_snapshot_) {
    return;
  }
  last_emitted_snapshot_ = snapshot_key;
  page_->SettingsChanged(std::move(settings));
}

void MahoSettingsPageHandler::OnInstalledExtensionsChanged() {
  ScheduleExtensionNotify();
}

void MahoSettingsPageHandler::ScheduleExtensionNotify() {
  ext_notify_timer_.Start(
      FROM_HERE, base::Milliseconds(50),
      base::BindOnce(&MahoSettingsPageHandler::FlushExtensionNotify,
                     weak_factory_.GetWeakPtr()));
}

void MahoSettingsPageHandler::FlushExtensionNotify() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildInstalledExtensionsOnPool),
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> self,
             std::vector<maho_settings::mojom::InstalledExtensionPtr> exts) {
            if (!self) {
              return;
            }
            self->page_->InstalledExtensionsChanged(std::move(exts));
          },
          weak_factory_.GetWeakPtr()));
}

bool MahoSettingsPageHandler::HandleFfiSetting(const std::string& key,
                                                const std::string& value) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core) return false;

  bool handled = false;
  bool needs_color_reset = false;

  if (key == "appearance.accent_color") {
    maho_core_set_accent_color(core, value.c_str());
    handled = true;
    needs_color_reset = true;
  } else if (key == "appearance.density") {
    maho_core_set_density(core, value.c_str());
    handled = true;
  } else if (key == "appearance.window_transparency") {
    maho_core_set_window_transparency(core, value == "true");
    handled = true;
  } else if (key == "appearance.sidebar_width") {
    int width = 0;
    if (!base::StringToInt(value, &width)) return true;
    width = std::clamp(width, 180, 400);
    base::DictValue settings;
    base::DictValue appearance;
    appearance.Set("sidebarWidth", width);
    settings.Set("appearance", std::move(appearance));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
    handled = true;
  } else if (key == "tabs.archive_timeout") {
    int hours = 0;
    if (!base::StringToInt(value, &hours)) return true;
    base::DictValue settings;
    base::DictValue general;
    general.Set("archiveTimeoutHours", static_cast<double>(hours));
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
    handled = true;
  } else if (key == "tabs.today_tab_timeout") {
    int hours = 0;
    if (!base::StringToInt(value, &hours)) return true;
    base::DictValue settings;
    base::DictValue general;
    general.Set("todayTabTimeoutHours", static_cast<double>(hours));
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
    handled = true;
  } else if (key == "tabs.pinned_close_behavior") {
    base::DictValue settings;
    base::DictValue general;
    general.Set("pinnedCloseBehavior", value);
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
    handled = true;
  }

  if (needs_color_reset) {
    ui::ColorProviderManager::Get().ResetColorProviderCache();
  }

  return handled;
}

void MahoSettingsPageHandler::AppendFfiSettings(
    std::vector<maho_settings::mojom::SettingValuePtr>& settings) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core) return;

  char* json_str = maho_core_get_settings(core);
  if (!json_str) return;
  std::string json(json_str);
  maho_string_free(json_str);

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return;

  const base::DictValue& root = parsed->GetDict();
  const base::DictValue* appearance = root.FindDict("appearance");
  if (!appearance) return;

  auto add = [&](const std::string& key, const std::string& val) {
    auto s = maho_settings::mojom::SettingValue::New();
    s->key = key;
    s->value = val;
    settings.push_back(std::move(s));
  };

  if (const std::string* density = appearance->FindString("density")) {
    add("appearance.density", *density);
  }
  if (auto width = appearance->FindDouble("sidebarWidth")) {
    add("appearance.sidebar_width",
        base::NumberToString(static_cast<int>(*width)));
  }
  if (auto transparency = appearance->FindBool("windowTransparency")) {
    add("appearance.window_transparency", *transparency ? "true" : "false");
  }
  if (const base::DictValue* color = appearance->FindDict("accentColor")) {
    std::string color_json;
    base::JSONWriter::Write(base::Value(color->Clone()), &color_json);
    add("appearance.accent_color", color_json);
  } else {
    add("appearance.accent_color", "");
  }

  const base::DictValue* general = root.FindDict("general");
  if (general) {
    if (auto archive_timeout = general->FindDouble("archiveTimeoutHours")) {
      add("tabs.archive_timeout",
          base::NumberToString(static_cast<int>(*archive_timeout)));
    }
    if (auto today_timeout = general->FindDouble("todayTabTimeoutHours")) {
      add("tabs.today_tab_timeout",
          base::NumberToString(static_cast<int>(*today_timeout)));
    }
    if (const std::string* pinned_close =
            general->FindString("pinnedCloseBehavior")) {
      add("tabs.pinned_close_behavior", *pinned_close);
    }
  }

}

// ── Profiles domain ──

void MahoSettingsPageHandler::GetProfiles(GetProfilesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildProfilesOnPool),
      base::BindOnce(
          [](GetProfilesCallback cb,
             std::vector<maho_settings::mojom::ProfileInfoPtr> profiles) {
            std::move(cb).Run(std::move(profiles));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::CreateProfile(const std::string& name,
                                             CreateProfileCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&CreateProfileOnPool, name),
      base::BindOnce(
          [](CreateProfileCallback cb,
             maho_settings::mojom::ProfileInfoPtr profile) {
            std::move(cb).Run(std::move(profile));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::DeleteProfile(const std::string& profile_id,
                                             DeleteProfileCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&DeleteProfileOnPool, profile_id),
      base::BindOnce(
          [](DeleteProfileCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::SwitchProfile(const std::string& profile_id,
                                             SwitchProfileCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&SwitchProfileOnPool, profile_id),
      base::BindOnce(
          [](SwitchProfileCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

// ── ATC domain ──

void MahoSettingsPageHandler::GetATCRules(GetATCRulesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildATCRulesOnPool),
      base::BindOnce(
          [](GetATCRulesCallback cb,
             std::vector<maho_settings::mojom::ATCRulePtr> rules) {
            std::move(cb).Run(std::move(rules));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::AddATCRule(const std::string& url_pattern,
                                          const std::string& target_space_id,
                                          AddATCRuleCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&AddATCRuleOnPool, url_pattern, target_space_id),
      base::BindOnce(
          [](AddATCRuleCallback cb, maho_settings::mojom::ATCRulePtr rule) {
            std::move(cb).Run(std::move(rule));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::RemoveATCRule(const std::string& rule_id) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&RemoveATCRuleOnPool, rule_id));
}

void MahoSettingsPageHandler::ToggleATCRule(const std::string& rule_id,
                                             bool enabled) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ToggleATCRuleOnPool, rule_id, enabled));
}

// ── AI domain ──

void MahoSettingsPageHandler::GetAISettings(GetAISettingsCallback callback) {
  auto settings = maho_settings::mojom::AISettings::New();
  settings->has_api_key = !prefs_->GetString(ai::kApiKey).empty();
  settings->provider = prefs_->GetString(ai::kProvider);
  settings->base_url = prefs_->GetString(ai::kBaseUrl);
  settings->model = prefs_->GetString(ai::kModel);
  settings->opencode_runtime_enabled =
      prefs_->GetBoolean(ai::kOpenCodeRuntimeEnabled);
  settings->approval_policy = prefs_->GetString(ai::kApprovalPolicy);
  settings->session_persistence_enabled =
      prefs_->GetBoolean(ai::kSessionPersistenceEnabled);
  settings->has_byok_openai =
      !prefs_->GetString(ai::kByokOpenAIEncryptedB64).empty();
  settings->has_byok_anthropic =
      !prefs_->GetString(ai::kByokAnthropicEncryptedB64).empty();
  std::move(callback).Run(std::move(settings));
}

void MahoSettingsPageHandler::SetAIProvider(const std::string& provider) {
  prefs_->SetString(ai::kProvider, provider);
}

void MahoSettingsPageHandler::SetAIBaseUrl(const std::string& url) {
  prefs_->SetString(ai::kBaseUrl, url);
}

void MahoSettingsPageHandler::SetAIApiKey(const std::string& key) {
  prefs_->SetString(ai::kApiKey, key);
}

void MahoSettingsPageHandler::SetAIModel(const std::string& model) {
  prefs_->SetString(ai::kModel, model);
}

void MahoSettingsPageHandler::SetAIApprovalPolicy(const std::string& policy) {
  prefs_->SetString(ai::kApprovalPolicy, policy);
}

void MahoSettingsPageHandler::SetAISessionPersistence(bool enabled) {
  prefs_->SetBoolean(ai::kSessionPersistenceEnabled, enabled);
}

// ── Shortcuts domain ──

void MahoSettingsPageHandler::GetShortcuts(GetShortcutsCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildShortcutsOnPool),
      base::BindOnce(
          [](GetShortcutsCallback cb,
             std::vector<maho_settings::mojom::ShortcutBindingPtr> shortcuts) {
            std::move(cb).Run(std::move(shortcuts));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::SetShortcut(const std::string& action,
                                           const std::string& key_combo,
                                           SetShortcutCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&SetShortcutOnPool, action, key_combo),
      base::BindOnce(
          [](SetShortcutCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::ResetShortcut(const std::string& action) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ResetShortcutOnPool, action));
}

void MahoSettingsPageHandler::ResetAllShortcuts() {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ResetAllShortcutsOnPool));
}

void MahoSettingsPageHandler::ToggleShortcut(const std::string& action,
                                             bool enabled) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ToggleShortcutOnPool, action, enabled));
}

// ── Sync domain ──

void MahoSettingsPageHandler::GetSyncStatus(GetSyncStatusCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildSyncStatusOnPool),
      base::BindOnce(
          [](GetSyncStatusCallback cb,
             maho_settings::mojom::SyncStatusPtr status) {
            std::move(cb).Run(std::move(status));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetSyncDevices(
    GetSyncDevicesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildSyncDevicesOnPool),
      base::BindOnce(
          [](GetSyncDevicesCallback cb,
             std::vector<maho_settings::mojom::SyncDeviceInfoPtr> devices) {
            std::move(cb).Run(std::move(devices));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GenerateSyncKey(
    GenerateSyncKeyCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&GenerateSyncKeyOnPool),
      base::BindOnce(
          [](GenerateSyncKeyCallback cb, GenerateSyncKeyResult result) {
            auto info = maho_settings::mojom::SyncKeyInfo::New();
            info->sync_key = std::move(result.sync_key);
            info->room_id = std::move(result.room_id);
            info->recovery_phrase = std::move(result.recovery_phrase);
            std::move(cb).Run(std::move(info));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::StartSync(const std::string& sync_key,
                                          StartSyncCallback callback) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&StartSyncOnPool, sync_key),
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> self,
             StartSyncCallback cb) {
            if (!self) {
              auto s = maho_settings::mojom::SyncStatus::New();
              s->status_label = "error";
              std::move(cb).Run(std::move(s));
              return;
            }
            self->GetSyncStatus(std::move(cb));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSettingsPageHandler::JoinSync(const std::string& recovery_phrase,
                                        JoinSyncCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&JoinSyncOnPool, recovery_phrase),
      base::BindOnce(
          [](JoinSyncCallback cb,
             maho_settings::mojom::SyncStatusPtr status) {
            std::move(cb).Run(std::move(status));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::StopSync() {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&StopSyncOnPool));
}

void MahoSettingsPageHandler::FetchThemeStore(
    FetchThemeStoreCallback callback) {
  if (!cached_theme_store_json_.empty() &&
      (base::TimeTicks::Now() - last_theme_store_fetch_) < base::Minutes(5)) {
    OnThemeStoreFetched(std::move(callback), cached_theme_store_json_);
    return;
  }

  theme_store_loader_.reset();
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("https://themes.maho.app/manifest.json");
  request->method = "GET";
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_settings_theme_store", R"(
        semantics {
          sender: "Maho Settings Theme Store"
          description: "Fetches community theme manifest."
          trigger: "User opens the theme store settings pane."
          data: "No user data sent."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Not user-configurable."
        })");
  theme_store_loader_ = network::SimpleURLLoader::Create(
      std::move(request), traffic_annotation);
  theme_store_loader_->SetTimeoutDuration(base::Seconds(15));
  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  theme_store_loader_->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&MahoSettingsPageHandler::OnThemeStoreFetched,
                     weak_factory_.GetWeakPtr(), std::move(callback)),
      1024 * 1024);
}

void MahoSettingsPageHandler::OnThemeStoreFetched(
    FetchThemeStoreCallback callback,
    std::optional<std::string> body) {
  theme_store_loader_.reset();
  std::vector<maho_settings::mojom::ThemeStoreEntryPtr> themes;
  const bool success = body.has_value() || !cached_theme_store_json_.empty();

  std::string json = body.value_or(cached_theme_store_json_);
  if (json.empty()) {
    std::move(callback).Run(std::move(themes), success);
    return;
  }

  if (body) {
    cached_theme_store_json_ = *body;
    last_theme_store_fetch_ = base::TimeTicks::Now();
    PersistCachedThemeStoreToDisk();
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(std::move(themes), success);
    return;
  }

  const base::ListValue* theme_list =
      parsed->GetDict().FindList("themes");
  if (!theme_list) {
    std::move(callback).Run(std::move(themes), success);
    return;
  }

  for (const auto& item : *theme_list) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id || !name) continue;

    auto entry = maho_settings::mojom::ThemeStoreEntry::New();
    entry->id = *id;
    entry->name = *name;
    if (const std::string* author = dict->FindString("author"))
      entry->author = *author;
    if (const std::string* desc = dict->FindString("description"))
      entry->description = *desc;
    if (const std::string* preview = dict->FindString("preview_url"))
      entry->preview_url = *preview;

    const base::Value* theme_val = dict->Find("theme");
    if (theme_val) {
      std::string theme_json_str;
      base::JSONWriter::Write(*theme_val, &theme_json_str);
      entry->theme_json = std::move(theme_json_str);
    }

    const base::ListValue* tags = dict->FindList("tags");
    if (tags) {
      for (const auto& tag : *tags) {
        if (tag.is_string()) entry->tags.push_back(tag.GetString());
      }
    }

    themes.push_back(std::move(entry));
  }

  std::move(callback).Run(std::move(themes), success);
}

void MahoSettingsPageHandler::ApplyThemeToSpace(
    const std::string& space_id,
    const std::string& theme_json,
    ApplyThemeToSpaceCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ApplyThemeToSpaceOnPool, space_id, theme_json),
      base::BindOnce(
          [](ApplyThemeToSpaceCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetSpaces(GetSpacesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildSpacesOnPool),
      base::BindOnce(
          [](GetSpacesCallback cb,
             std::vector<maho_settings::mojom::SpaceBasicInfoPtr> spaces) {
            std::move(cb).Run(std::move(spaces));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetPasswordProviderStatus(
    GetPasswordProviderStatusCallback callback) {
  const bool passwords_enabled = ReadPasswordsEnabledFromCore();
  const std::string mode = ReadPasswordProviderModeFromCore();
  auto status = maho_settings::mojom::PasswordProviderStatus::New();
  status->provider = ToPasswordProviderKind(mode);
  status->is_enabled = passwords_enabled;
  status->is_available =
      maho::passwords::IsPasswordProviderAvailable(mode);
  switch (status->provider) {
    case maho_settings::mojom::PasswordProviderKind::kMahoNative:
      status->display_name = "Maho Native";
      status->description = "Use Maho's native password manager.";
      break;
    case maho_settings::mojom::PasswordProviderKind::kBitwarden:
      status->display_name = "Bitwarden";
      status->description = status->is_available
                                 ? "Use Bitwarden as the password provider."
                                 : "Bitwarden is selected, but the Bitwarden extension is not installed or enabled.";
      break;
    case maho_settings::mojom::PasswordProviderKind::kOnePassword:
      status->display_name = "1Password";
      status->description = status->is_available
                                 ? "Use 1Password as the password provider."
                                 : "1Password is selected, but the 1Password extension is not installed or enabled.";
      break;
  }
  status->capabilities = MakePasswordProviderCapabilities(status->provider);
  std::move(callback).Run(std::move(status));
}

void MahoSettingsPageHandler::GetSavedPasswords(
    GetSavedPasswordsCallback callback) {
  std::move(callback).Run(GetSavedPasswordsFromCore());
}

void MahoSettingsPageHandler::SearchPasswords(
    const std::string& query,
    SearchPasswordsCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run({});
    return;
  }

  char* json_str = maho_core_search_passwords(core, query.c_str());
  if (!json_str) {
    std::move(callback).Run({});
    return;
  }

  std::string json(json_str);
  maho_string_free(json_str);
  std::move(callback).Run(ParseSavedPasswordsJson(json));
}

void MahoSettingsPageHandler::AddPassword(const std::string& domain,
                                          const std::string& username,
                                          const std::string& password,
                                          AddPasswordCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false);
    return;
  }

  const bool success = maho_core_add_password(
      core, domain.c_str(), username.c_str(), password.empty() ? nullptr : password.c_str());
  if (success) {
    ScheduleNotify();
  }
  std::move(callback).Run(success);
}

void MahoSettingsPageHandler::UpdatePasswordUsername(
    const std::string& password_id,
    const std::string& username,
    UpdatePasswordUsernameCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false);
    return;
  }

  const bool success = maho_core_update_password_username(
      core, password_id.c_str(), username.c_str());
  if (success) {
    ScheduleNotify();
  }
  std::move(callback).Run(success);
}

void MahoSettingsPageHandler::DeletePassword(
    const std::string& password_id,
    DeletePasswordCallback callback) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    std::move(callback).Run(false);
    return;
  }

  const bool success = maho_core_delete_password(core, password_id.c_str());
  if (success) {
    ScheduleNotify();
  }
  std::move(callback).Run(success);
}

void MahoSettingsPageHandler::GetAutofillAddresses(
    GetAutofillAddressesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildAutofillAddressesOnPool),
      base::BindOnce(
          [](GetAutofillAddressesCallback cb,
             std::vector<maho_settings::mojom::AutofillAddressPtr> addrs) {
            std::move(cb).Run(std::move(addrs));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::AddAutofillAddress(
    const std::string& id,
    const std::string& name,
    const std::string& address_line1,
    const std::string& address_line2,
    const std::string& city,
    const std::string& state,
    const std::string& postal_code,
    const std::string& country,
    const std::string& phone,
    const std::string& email,
    AddAutofillAddressCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&AddAutofillAddressOnPool, id, name, address_line1,
                     address_line2, city, state, postal_code, country, phone,
                     email),
      base::BindOnce(
          [](AddAutofillAddressCallback cb,
             std::optional<maho_settings::mojom::AutofillAddressPtr> address) {
            std::move(cb).Run(address ? std::move(*address) : nullptr);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::DeleteAutofillAddress(
    const std::string& id,
    DeleteAutofillAddressCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&DeleteAutofillAddressOnPool, id),
      base::BindOnce(
          [](DeleteAutofillAddressCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetAutofillPayments(
    GetAutofillPaymentsCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildAutofillPaymentsOnPool),
      base::BindOnce(
          [](GetAutofillPaymentsCallback cb,
             std::vector<maho_settings::mojom::AutofillPaymentPtr> pays) {
            std::move(cb).Run(std::move(pays));
          },
           std::move(callback)));
}

void MahoSettingsPageHandler::GetFilterLists(GetFilterListsCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildFilterListsOnPool),
      base::BindOnce(
          [](GetFilterListsCallback cb,
             std::vector<maho_settings::mojom::FilterListInfoPtr> lists) {
            std::move(cb).Run(std::move(lists));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::AddAutofillPayment(
    const std::string& id,
    const std::string& card_network,
    const std::string& last_four,
    const std::string& expiration,
    const std::string& cardholder_name,
    AddAutofillPaymentCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&AddAutofillPaymentOnPool, id, card_network, last_four,
                     expiration, cardholder_name),
      base::BindOnce(
          [](AddAutofillPaymentCallback cb,
             std::optional<maho_settings::mojom::AutofillPaymentPtr> payment) {
            std::move(cb).Run(payment ? std::move(*payment) : nullptr);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::DeleteAutofillPayment(
    const std::string& id,
    DeleteAutofillPaymentCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&DeleteAutofillPaymentOnPool, id),
      base::BindOnce(
          [](DeleteAutofillPaymentCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::AddFilterList(const std::string& id,
                                            const std::string& name,
                                            const std::string& url,
                                            AddFilterListCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&AddFilterListOnPool, id, name, url),
      base::BindOnce(
          [](AddFilterListCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::ToggleFilterList(const std::string& id,
                                               bool enabled) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&ToggleFilterListOnPool, id, enabled));
}

void MahoSettingsPageHandler::RemoveFilterList(const std::string& id) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&RemoveFilterListOnPool, id));
}

void MahoSettingsPageHandler::RebuildContentRules(
    RebuildContentRulesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&RebuildContentRulesOnPool),
      base::BindOnce(
          [](RebuildContentRulesCallback cb,
             maho_settings::mojom::ContentBlockerStatsPtr stats) {
            std::move(cb).Run(std::move(stats));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetContentBlockerStats(
    GetContentBlockerStatsCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildContentBlockerStatsOnPool),
      base::BindOnce(
          [](GetContentBlockerStatsCallback cb,
             maho_settings::mojom::ContentBlockerStatsPtr stats) {
            std::move(cb).Run(std::move(stats));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetInstalledExtensions(
    GetInstalledExtensionsCallback callback) {
  if (!ext_bridge_observation_.IsObserving()) {
    if (auto* bridge = maho::MahoExtensionStateBridge::FromProfile(profile_)) {
      ext_bridge_observation_.Observe(bridge);
    }
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildInstalledExtensionsOnPool),
      base::BindOnce(
          [](GetInstalledExtensionsCallback cb,
             std::vector<maho_settings::mojom::InstalledExtensionPtr> exts) {
            std::move(cb).Run(std::move(exts));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::ToggleExtension(const std::string& id,
                                              ToggleExtensionCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  auto* bridge = maho::MahoExtensionStateBridge::FromProfile(profile_);
  if (!bridge) {
    std::move(callback).Run(
        registry && registry->enabled_extensions().Contains(id));
    return;
  }
  const bool currently_enabled =
      registry && registry->enabled_extensions().Contains(id);
  if (currently_enabled) {
    bridge->RequestDisable(id);
  } else {
    bridge->RequestEnable(id);
  }
  std::move(callback).Run(
      registry && registry->enabled_extensions().Contains(id));
}

void MahoSettingsPageHandler::RemoveExtension(const std::string& id,
                                              RemoveExtensionCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (uninstall_dialog_ || pending_remove_callback_) {
    std::move(callback).Run(
        maho_settings::mojom::RemoveExtensionResult::kBusy);
    return;
  }

  auto* bridge = maho::MahoExtensionStateBridge::FromProfile(profile_);
  if (!bridge) {
    std::move(callback).Run(
        maho_settings::mojom::RemoveExtensionResult::kNotFound);
    return;
  }

  if (!bridge->RequestUninstall(id)) {
    auto* registry = extensions::ExtensionRegistry::Get(profile_);
    const bool exists =
        registry &&
        registry->GetExtensionById(id,
                                   extensions::ExtensionRegistry::EVERYTHING);
    std::move(callback).Run(
        exists ? maho_settings::mojom::RemoveExtensionResult::kBlockedByPolicy
               : maho_settings::mojom::RemoveExtensionResult::kNotFound);
    return;
  }

  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  scoped_refptr<const extensions::Extension> ext =
      registry
          ? registry->GetExtensionById(
                id, extensions::ExtensionRegistry::EVERYTHING)
          : nullptr;
  if (!ext) {
    std::move(callback).Run(
        maho_settings::mojom::RemoveExtensionResult::kNotFound);
    return;
  }

  pending_remove_callback_ = std::move(callback);
  uninstall_dialog_ = extensions::ExtensionUninstallDialog::Create(
      profile_, /*parent=*/gfx::NativeWindow(), this);
  uninstall_dialog_->ConfirmUninstall(
      ext, extensions::UNINSTALL_REASON_USER_INITIATED,
      extensions::UNINSTALL_SOURCE_CHROME_EXTENSIONS_PAGE);
}

void MahoSettingsPageHandler::OnExtensionUninstallDialogClosed(
    bool did_start_uninstall,
    const std::u16string& error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  uninstall_dialog_.reset();
  if (pending_remove_callback_) {
    maho_settings::mojom::RemoveExtensionResult result;
    if (!did_start_uninstall) {
      result = maho_settings::mojom::RemoveExtensionResult::kCancelled;
    } else if (!error.empty()) {
      result = maho_settings::mojom::RemoveExtensionResult::kUninstallFailed;
    } else {
      result = maho_settings::mojom::RemoveExtensionResult::kSuccess;
    }
    std::move(pending_remove_callback_).Run(result);
  }
}

void MahoSettingsPageHandler::SetBYOKKey(const std::string& provider,
                                         const std::string& key,
                                         SetBYOKKeyCallback callback) {
  if (key.empty()) {
    ClearBYOKKey(provider, std::move(callback));
    return;
  }
  if (!encryptor_.has_value()) {
    std::move(callback).Run(false);
    return;
  }
  std::string encrypted_bytes;
  if (!encryptor_->EncryptString(key, &encrypted_bytes)) {
    std::move(callback).Run(false);
    return;
  }
  std::string encrypted_b64 = base::Base64Encode(encrypted_bytes);

  if (provider == "openai") {
    prefs_->SetString(ai::kByokOpenAIEncryptedB64, encrypted_b64);
    std::move(callback).Run(true);
  } else if (provider == "anthropic") {
    prefs_->SetString(ai::kByokAnthropicEncryptedB64, encrypted_b64);
    std::move(callback).Run(true);
  } else {
    std::move(callback).Run(false);
  }
}

void MahoSettingsPageHandler::ClearBYOKKey(const std::string& provider,
                                           ClearBYOKKeyCallback callback) {
  if (provider == "openai") {
    prefs_->SetString(ai::kByokOpenAIEncryptedB64, "");
    std::move(callback).Run(true);
  } else if (provider == "anthropic") {
    prefs_->SetString(ai::kByokAnthropicEncryptedB64, "");
    std::move(callback).Run(true);
  } else {
    std::move(callback).Run(false);
  }
}

void MahoSettingsPageHandler::OnOsCryptReady(os_crypt_async::Encryptor encryptor) {
  encryptor_ = std::move(encryptor);
}


