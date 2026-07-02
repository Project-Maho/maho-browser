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
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/native_ui_types.h"

namespace {

namespace ai = maho::ai_prefs;
namespace acct = maho::account_prefs;

constexpr char kSettingsSyncRelayUrl[] = "https://sync.maho.app";

// ── Auth traffic annotations ──

const net::NetworkTrafficAnnotationTag kAuthLoginTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_auth_login", R"(
      semantics {
        sender: "Maho Auth Login"
        description:
          "Authenticates the user with the Maho relay using email and password."
        trigger:
          "User submits the sign-in form in the Account settings pane."
        data: "User email and password in the request body."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by an explicit user action; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. The user explicitly submits "
          "credentials in the Account settings pane to authenticate with the "
          "Maho relay service. No ambient or background network activity."
      }
    )");

const net::NetworkTrafficAnnotationTag kAuthLogoutTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_auth_logout", R"(
      semantics {
        sender: "Maho Auth Logout"
        description: "Invalidates the user's relay session."
        trigger: "User clicks Sign Out in the Account settings pane."
        data: "Bearer access token in the Authorization header."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by an explicit user action; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. The user explicitly clicks "
          "Sign Out. A single best-effort POST invalidates the server session. "
          "No ambient or background network activity."
      }
    )");

const net::NetworkTrafficAnnotationTag kSubscriptionTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_billing_subscription", R"(
      semantics {
        sender: "Maho Billing Subscription"
        description:
          "Fetches the user's plan, credits, and renewal info from the Maho relay."
        trigger: "User opens the Billing settings pane."
        data: "Bearer access token in the Authorization header."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by opening the Billing pane; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. Triggered when the user "
          "opens the Billing pane to view plan and credit balance. "
          "No ambient or background network activity."
      }
    )");

const net::NetworkTrafficAnnotationTag kInvoicesTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_billing_invoices", R"(
      semantics {
        sender: "Maho Billing Invoices"
        description:
          "Fetches the user's billing invoice history from the Maho relay."
        trigger: "User opens the Billing settings pane and views invoice history."
        data: "Bearer access token in the Authorization header."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by viewing the Billing pane; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. Triggered when the user "
          "opens the Billing pane to view invoices. "
          "No ambient or background network activity."
      }
    )");

const net::NetworkTrafficAnnotationTag kBillingPortalTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_billing_portal", R"(
      semantics {
        sender: "Maho Billing Portal"
        description:
          "Requests a self-service billing portal session URL from the Maho relay."
        trigger: "User clicks the Manage or Adjust Plan button in the Billing pane."
        data: "Bearer access token in the Authorization header."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "Triggered by clicking the Manage or Adjust Plan button; not background."
        policy_exception_justification:
          "Not controlled by enterprise policy. Triggered when the user "
          "requests a customer billing portal link. "
          "No ambient or background network activity."
      }
    )");

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
constexpr int kNetworkPredictionOptionsMinValue = 0;
constexpr int kNetworkPredictionOptionsMaxValue = 3;
constexpr int kNetworkPredictionOptionsStandardValue =
    static_cast<int>(prefetch::NetworkPredictionOptions::kStandard);
constexpr int kNetworkPredictionOptionsWifiOnlyDeprecatedValue =
    static_cast<int>(prefetch::NetworkPredictionOptions::kWifiOnlyDeprecated);

constexpr SettingMapping kSettingsMap[] = {
    {"general.homepage", prefs::kHomePage, SettingMapping::Type::kString},
    {"general.restore_on_startup", prefs::kRestoreOnStartup,
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

// ── Profile domain helpers (MahoCore sequence) ──

std::vector<maho_settings::mojom::ProfileInfoPtr> BuildProfilesOnCoreSequence() {
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

maho_settings::mojom::ProfileInfoPtr CreateProfileOnCoreSequence(
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

bool DeleteProfileOnCoreSequence(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_delete_profile(core, profile_id.c_str());
}

bool SwitchProfileOnCoreSequence(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_switch_profile(core, profile_id.c_str());
}

// ── ATC domain helpers (MahoCore sequence) ──

std::vector<maho_settings::mojom::ATCRulePtr> BuildATCRulesOnCoreSequence() {
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

maho_settings::mojom::ATCRulePtr AddATCRuleOnCoreSequence(
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

void RemoveATCRuleOnCoreSequence(const std::string& rule_id) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  char* result = maho_core_remove_atc_rule(core, rule_id.c_str());
  if (result) maho_string_free(result);
}

void ToggleATCRuleOnCoreSequence(const std::string& rule_id, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_toggle_atc_rule(core, rule_id.c_str(), enabled);
}

// ── Shortcuts domain helpers (MahoCore sequence) ──

std::vector<maho_settings::mojom::ShortcutBindingPtr>
BuildShortcutsOnCoreSequence() {
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

bool SetShortcutOnCoreSequence(const std::string& action,
                       const std::string& key_combo) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  char* result = maho_core_set_shortcut(core, action.c_str(),
                                        key_combo.c_str());
  bool ok = result != nullptr;
  if (result) maho_string_free(result);
  return ok;
}

void ResetShortcutOnCoreSequence(const std::string& action) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_reset_shortcut(core, action.c_str());
}

void ResetAllShortcutsOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_reset_all_shortcuts(core);
}

void ToggleShortcutOnCoreSequence(const std::string& action, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_toggle_shortcut(core, action.c_str(), enabled);
}

// ── Sync domain helpers (MahoCore sequence) ──

maho_settings::mojom::SyncStatusPtr BuildSyncStatusOnCoreSequence() {
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
BuildSyncDevicesOnCoreSequence() {
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

GenerateSyncKeyResult GenerateSyncKeyOnCoreSequence() {
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

void StartSyncOnCoreSequence(const std::string& sync_key) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_start_sync(core, kSettingsSyncRelayUrl,
                       sync_key.c_str());
}

maho_settings::mojom::SyncStatusPtr JoinSyncOnCoreSequence(
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
  return BuildSyncStatusOnCoreSequence();
}

void StopSyncOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_stop_sync(core);
}

// ── Theme / Spaces domain helpers (MahoCore sequence) ──

bool ApplyThemeToSpaceOnCoreSequence(const std::string& space_id,
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

std::vector<maho_settings::mojom::SpaceBasicInfoPtr> BuildSpacesOnCoreSequence() {
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
BuildAutofillAddressesOnCoreSequence() {
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
BuildAutofillPaymentsOnCoreSequence() {
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
BuildFilterListsOnCoreSequence() {
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

maho_settings::mojom::ContentBlockerStatsPtr BuildContentBlockerStatsOnCoreSequence() {
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

maho_settings::mojom::ContentBlockerStatsPtr RebuildContentRulesOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_rebuild_content_rules(core);
  }
  return BuildContentBlockerStatsOnCoreSequence();
}

bool AddFilterListOnCoreSequence(const std::string& id,
                          const std::string& name,
                          const std::string& url) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  maho_core_add_filter_list(core, id.c_str(), name.c_str(), url.c_str());
  return true;
}

void ToggleFilterListOnCoreSequence(const std::string& id, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_toggle_filter_list(core, id.c_str(), enabled);
}

void RemoveFilterListOnCoreSequence(const std::string& id) {
  MahoCore* core = maho::GetCore();
  if (!core) return;
  maho_core_remove_filter_list(core, id.c_str());
}

// ── Extensions domain (read-only) ──

std::vector<maho_settings::mojom::InstalledExtensionPtr>
BuildInstalledExtensionsOnCoreSequence() {
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
AddAutofillAddressOnCoreSequence(const std::string& id,
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

bool DeleteAutofillAddressOnCoreSequence(const std::string& id) {
  MahoCore* core = maho::GetCore();
  if (!core) return false;
  return maho_core_delete_autofill_address_persisted(core, id.c_str());
}

std::optional<maho_settings::mojom::AutofillPaymentPtr>
AddAutofillPaymentOnCoreSequence(const std::string& id,
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

bool DeleteAutofillPaymentOnCoreSequence(const std::string& id) {
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
        if (key == "advanced.preload_pages") {
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
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildInstalledExtensionsOnCoreSequence),
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
    // Chromium desktop authoritative source: sidebar_prefs::kSidebarWidth is
    // observed by MahoSidebarContainerView for live width updates. Also
    // mirror to maho-core so iOS/Android shells stay in sync.
    prefs_->SetInteger(maho::sidebar_prefs::kSidebarWidth, width);
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
  // Sidebar width: prefer the Chromium pref (authoritative for desktop, observed
  // by MahoSidebarContainerView). Fall back to maho-core if pref is unset.
  {
    int chromium_width =
        prefs_->GetInteger(maho::sidebar_prefs::kSidebarWidth);
    if (chromium_width > 0) {
      add("appearance.sidebar_width", base::NumberToString(chromium_width));
    } else if (auto width = appearance->FindDouble("sidebarWidth")) {
      add("appearance.sidebar_width",
          base::NumberToString(static_cast<int>(*width)));
    }
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildProfilesOnCoreSequence),
      base::BindOnce(
          [](GetProfilesCallback cb,
             std::vector<maho_settings::mojom::ProfileInfoPtr> profiles) {
            std::move(cb).Run(std::move(profiles));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::CreateProfile(const std::string& name,
                                             CreateProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&CreateProfileOnCoreSequence, name),
      base::BindOnce(
          [](CreateProfileCallback cb,
             maho_settings::mojom::ProfileInfoPtr profile) {
            std::move(cb).Run(std::move(profile));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::DeleteProfile(const std::string& profile_id,
                                             DeleteProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&DeleteProfileOnCoreSequence, profile_id),
      base::BindOnce(
          [](DeleteProfileCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::SwitchProfile(const std::string& profile_id,
                                             SwitchProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&SwitchProfileOnCoreSequence, profile_id),
      base::BindOnce(
          [](SwitchProfileCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

// ── ATC domain ──

void MahoSettingsPageHandler::GetATCRules(GetATCRulesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildATCRulesOnCoreSequence),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&AddATCRuleOnCoreSequence, url_pattern, target_space_id),
      base::BindOnce(
          [](AddATCRuleCallback cb, maho_settings::mojom::ATCRulePtr rule) {
            std::move(cb).Run(std::move(rule));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::RemoveATCRule(const std::string& rule_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&RemoveATCRuleOnCoreSequence, rule_id));
}

void MahoSettingsPageHandler::ToggleATCRule(const std::string& rule_id,
                                             bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&ToggleATCRuleOnCoreSequence, rule_id, enabled));
}

// ── AI domain ──

void MahoSettingsPageHandler::GetAISettings(GetAISettingsCallback callback) {
  auto settings = maho_settings::mojom::AISettings::New();
  settings->has_api_key = !prefs_->GetString(ai::kApiKey).empty();
  settings->provider = prefs_->GetString(ai::kProvider);
  settings->base_url = prefs_->GetString(ai::kBaseUrl);
  settings->model = prefs_->GetString(ai::kModel);
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildShortcutsOnCoreSequence),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&SetShortcutOnCoreSequence, action, key_combo),
      base::BindOnce(
          [](SetShortcutCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::ResetShortcut(const std::string& action) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&ResetShortcutOnCoreSequence, action));
}

void MahoSettingsPageHandler::ResetAllShortcuts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&ResetAllShortcutsOnCoreSequence));
}

void MahoSettingsPageHandler::ToggleShortcut(const std::string& action,
                                             bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&ToggleShortcutOnCoreSequence, action, enabled));
}

// ── Sync domain ──

void MahoSettingsPageHandler::GetSyncStatus(GetSyncStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildSyncStatusOnCoreSequence),
      base::BindOnce(
          [](GetSyncStatusCallback cb,
             maho_settings::mojom::SyncStatusPtr status) {
            std::move(cb).Run(std::move(status));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetSyncDevices(
    GetSyncDevicesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildSyncDevicesOnCoreSequence),
      base::BindOnce(
          [](GetSyncDevicesCallback cb,
             std::vector<maho_settings::mojom::SyncDeviceInfoPtr> devices) {
            std::move(cb).Run(std::move(devices));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GenerateSyncKey(
    GenerateSyncKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&GenerateSyncKeyOnCoreSequence),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTaskAndReply(
      FROM_HERE,
      base::BindOnce(&StartSyncOnCoreSequence, sync_key),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&JoinSyncOnCoreSequence, recovery_phrase),
      base::BindOnce(
          [](JoinSyncCallback cb,
             maho_settings::mojom::SyncStatusPtr status) {
            std::move(cb).Run(std::move(status));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::StopSync() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&StopSyncOnCoreSequence));
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&ApplyThemeToSpaceOnCoreSequence, space_id, theme_json),
      base::BindOnce(
          [](ApplyThemeToSpaceCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetSpaces(GetSpacesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildSpacesOnCoreSequence),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildAutofillAddressesOnCoreSequence),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&AddAutofillAddressOnCoreSequence, id, name, address_line1,
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&DeleteAutofillAddressOnCoreSequence, id),
      base::BindOnce(
          [](DeleteAutofillAddressCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetAutofillPayments(
    GetAutofillPaymentsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildAutofillPaymentsOnCoreSequence),
      base::BindOnce(
          [](GetAutofillPaymentsCallback cb,
             std::vector<maho_settings::mojom::AutofillPaymentPtr> pays) {
            std::move(cb).Run(std::move(pays));
          },
           std::move(callback)));
}

void MahoSettingsPageHandler::GetFilterLists(GetFilterListsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildFilterListsOnCoreSequence),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&AddAutofillPaymentOnCoreSequence, id, card_network, last_four,
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&DeleteAutofillPaymentOnCoreSequence, id),
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&AddFilterListOnCoreSequence, id, name, url),
      base::BindOnce(
          [](AddFilterListCallback cb, bool success) {
            std::move(cb).Run(success);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::ToggleFilterList(const std::string& id,
                                               bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&ToggleFilterListOnCoreSequence, id, enabled));
}

void MahoSettingsPageHandler::RemoveFilterList(const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&RemoveFilterListOnCoreSequence, id));
}

void MahoSettingsPageHandler::RebuildContentRules(
    RebuildContentRulesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&RebuildContentRulesOnCoreSequence),
      base::BindOnce(
          [](RebuildContentRulesCallback cb,
             maho_settings::mojom::ContentBlockerStatsPtr stats) {
            std::move(cb).Run(std::move(stats));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetContentBlockerStats(
    GetContentBlockerStatsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildContentBlockerStatsOnCoreSequence),
      base::BindOnce(
          [](GetContentBlockerStatsCallback cb,
             maho_settings::mojom::ContentBlockerStatsPtr stats) {
            std::move(cb).Run(std::move(stats));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetInstalledExtensions(
    GetInstalledExtensionsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ext_bridge_observation_.IsObserving()) {
    if (auto* bridge = maho::MahoExtensionStateBridge::FromProfile(profile_)) {
      ext_bridge_observation_.Observe(bridge);
    }
  }
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildInstalledExtensionsOnCoreSequence),
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

// ═══════════════════════════════════════════════════════════════════════════════
// Auth — Login / Logout / GetAccountStatus / GetBillingInfo
// ═══════════════════════════════════════════════════════════════════════════════

void MahoSettingsPageHandler::Login(const std::string& email,
                                    const std::string& password,
                                    LoginCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (email.empty() || password.empty()) {
    std::move(callback).Run(false, "Email and password are required.");
    return;
  }

  if (!encryptor_.has_value()) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  base::DictValue body;
  body.Set("email", email);
  body.Set("password", password);
#if BUILDFLAG(IS_MAC)
  body.Set("device_name", "Maho macOS");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_WIN)
  body.Set("device_name", "Maho Windows");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_LINUX)
  body.Set("device_name", "Maho Linux");
  body.Set("device_type", "desktop");
#elif BUILDFLAG(IS_IOS)
  body.Set("device_name", "Maho iOS");
  body.Set("device_type", "mobile");
#elif BUILDFLAG(IS_ANDROID)
  body.Set("device_name", "Maho Android");
  body.Set("device_type", "mobile");
#else
  body.Set("device_name", "Maho");
  body.Set("device_type", "unknown");
#endif
  std::string body_json;
  base::JSONWriter::Write(body, &body_json);

  std::string relay_url = maho::auth::GetRelayBaseUrl();
  GURL request_url(relay_url + "/auth/login");
  if (!request_url.is_valid()) {
    std::move(callback).Run(false, "Invalid relay URL.");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Content-Type", "application/json");
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode =
      network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kAuthLoginTrafficAnnotation);
  url_loader->AttachStringForUpload(body_json, "application/json");
  url_loader->SetTimeoutDuration(base::Seconds(30));

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto* raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&MahoSettingsPageHandler::OnLoginResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(url_loader)),
      /*max_body_size=*/64 * 1024);
}

void MahoSettingsPageHandler::OnLoginResponse(
    LoginCallback callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int net_error = loader->NetError();
  const auto* response_info = loader->ResponseInfo();
  int http_status =
      response_info ? response_info->headers->response_code() : 0;

  if (net_error != net::OK || !response_body.has_value()) {
    std::move(callback).Run(false, "Network error");
    return;
  }

  auto parsed =
      base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(false, "Invalid server response.");
    return;
  }

  const base::DictValue& dict = parsed->GetDict();

  if (http_status != 200) {
    const std::string* error_msg = dict.FindString("error");
    std::move(callback).Run(
        false,
        error_msg ? *error_msg : "Authentication failed (HTTP " +
                                      base::NumberToString(http_status) + ")");
    return;
  }

  const std::string* access_token = dict.FindString("access_token");
  const std::string* refresh_token = dict.FindString("refresh_token");
  if (!access_token || !refresh_token) {
    std::move(callback).Run(false, "Missing tokens in response.");
    return;
  }

  int64_t expires_at = static_cast<int64_t>(
      dict.FindDouble("expires_at").value_or(0));
  int64_t refresh_expires_at = static_cast<int64_t>(
      dict.FindDouble("refresh_expires_at").value_or(0));

  const base::DictValue* account = dict.FindDict("account");
  std::string user_email;
  std::string user_id;
  std::string user_display_name;
  std::string user_tier;
  if (account) {
    if (const std::string* v = account->FindString("email"))
      user_email = *v;
    if (std::optional<int> v_int = account->FindInt("id"))
      user_id = base::NumberToString(*v_int);
    else if (const std::string* v_str = account->FindString("id"))
      user_id = *v_str;
    if (const std::string* v = account->FindString("display_name"))
      user_display_name = *v;
  }

  maho::auth::StoreTokensParams params;
  params.access_token = *access_token;
  params.refresh_token = *refresh_token;
  params.access_expires_at = expires_at;
  params.refresh_expires_at = refresh_expires_at;
  params.user_email = user_email;
  params.user_id = user_id;
  params.user_display_name = user_display_name;
  params.user_tier = user_tier;

  if (!maho::auth::StoreRelayTokens(prefs_, *encryptor_, params)) {
    std::move(callback).Run(false, "Failed to store credentials.");
    return;
  }

  // Fire ShellEvent::SignIn to maho-core on the thread pool.
  {
    base::DictValue sign_in_event;
    base::DictValue sign_in_data;
    sign_in_data.Set("email", user_email);
    sign_in_data.Set("password", "");  // Don't pass password to core.
    sign_in_data.Set("display_name", user_display_name);
    sign_in_data.Set("access_token", *access_token);
    sign_in_data.Set("user_id", user_id);
    sign_in_event.Set("SignIn", std::move(sign_in_data));

    std::string event_json;
    base::JSONWriter::Write(sign_in_event, &event_json);

    maho::PostCoreClosure(
        FROM_HERE,
        base::BindOnce(
            [](std::string json) {
              MahoCore* core = maho::GetCore();
              if (!core) return;
              char* result = maho_core_sign_in(core, json.c_str());
              if (result) maho_string_free(result);
            },
            std::move(event_json)));
  }

  BroadcastAccountStatus();

  // Populate tier + subscription prefs from /auth/subscription.
  // Fire-and-forget — failure leaves tier empty until user opens Billing pane.
  GetBillingInfo(base::BindOnce([](maho_settings::mojom::BillingInfoPtr) {
    // Result persisted to prefs inside OnSubscriptionResponse; discard here.
  }));

  std::move(callback).Run(true, "");
}

void MahoSettingsPageHandler::Logout(LogoutCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Best-effort POST /auth/logout — fire-and-forget.
  if (encryptor_.has_value()) {
    std::string access_token =
        maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
    if (!access_token.empty()) {
      std::string relay_url = maho::auth::GetRelayBaseUrl();
      GURL request_url(relay_url + "/auth/logout");
      if (request_url.is_valid()) {
        auto resource_request = std::make_unique<network::ResourceRequest>();
        resource_request->url = request_url;
        resource_request->method = "POST";
        resource_request->headers.SetHeader(
            "Authorization", "Bearer " + access_token);
        resource_request->credentials_mode =
            network::mojom::CredentialsMode::kOmit;

        auto url_loader = network::SimpleURLLoader::Create(
            std::move(resource_request), kAuthLogoutTrafficAnnotation);
        url_loader->SetTimeoutDuration(base::Seconds(10));

        auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                      ->GetURLLoaderFactoryForBrowserProcess();
        // Fire-and-forget: transfer loader ownership into the callback.
        auto* raw_loader = url_loader.get();
        raw_loader->DownloadToString(
            url_loader_factory.get(),
            base::BindOnce(
                [](std::unique_ptr<network::SimpleURLLoader>,
                   std::optional<std::string>) {
                  // Intentionally empty — fire-and-forget.
                },
                std::move(url_loader)),
            /*max_body_size=*/1024);
      }
    }
  }

  maho::auth::ClearRelayTokens(prefs_);

  // ShellEvent::SignOut to maho-core (unit variant → JSON: "\"SignOut\"")
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce([]() {
        MahoCore* core = maho::GetCore();
        if (!core) return;
        const char* event_json = "\"SignOut\"";
        char* result = maho_core_dispatch_shell_event(core, event_json);
        if (result) maho_string_free(result);
      }));

  // Broadcast empty AccountStatus to the page.
  BroadcastAccountStatus();

  std::move(callback).Run();
}

void MahoSettingsPageHandler::GetAccountStatus(
    GetAccountStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto status = maho_settings::mojom::AccountStatus::New();

  std::string encrypted_b64 =
      prefs_->GetString(acct::kRelayAccessTokenEncryptedB64);
  if (encrypted_b64.empty()) {
    status->signed_in = false;
    status->email = "";
    status->display_name = "";
    status->user_id = "";
    status->tier = "";
    status->subscription_status = "";
    status->subscription_expires_at = 0;
    std::move(callback).Run(std::move(status));
    return;
  }

  status->signed_in = true;
  status->email = prefs_->GetString(acct::kRelayUserEmail);
  status->display_name = prefs_->GetString(acct::kRelayUserDisplayName);
  status->user_id = prefs_->GetString(acct::kRelayUserId);
  status->tier = prefs_->GetString(acct::kRelayUserTier);
  status->subscription_status = prefs_->GetString(acct::kRelaySubscriptionStatus);
  status->subscription_expires_at = prefs_->GetInt64(acct::kRelaySubscriptionExpiresAt);

  std::move(callback).Run(std::move(status));
}

void MahoSettingsPageHandler::GetBillingInfo(GetBillingInfoCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!encryptor_.has_value()) {
    auto info = maho_settings::mojom::BillingInfo::New();
    info->tier = "";
    info->subscription_status = "";
    info->subscription_expires_at = 0;
    info->period_start_at = 0;
    info->anniversary_reset_day = 0;
    info->tier_ceiling_usd = 0.0f;
    info->credit_balance_usd = 0.0f;
    info->lifetime_purchased_usd = 0.0f;
    info->has_payment_method = false;
    std::move(callback).Run(std::move(info));
    return;
  }

  std::string access_token =
      maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  if (access_token.empty()) {
    auto info = maho_settings::mojom::BillingInfo::New();
    info->tier = "";
    info->subscription_status = "";
    info->subscription_expires_at = 0;
    info->period_start_at = 0;
    info->anniversary_reset_day = 0;
    info->tier_ceiling_usd = 0.0f;
    info->credit_balance_usd = 0.0f;
    info->lifetime_purchased_usd = 0.0f;
    info->has_payment_method = false;
    std::move(callback).Run(std::move(info));
    return;
  }

  std::string relay_url = maho::auth::GetRelayBaseUrl();
  GURL request_url(relay_url + "/auth/subscription");
  if (!request_url.is_valid()) {
    auto info = maho_settings::mojom::BillingInfo::New();
    info->tier = "";
    info->subscription_status = "";
    info->subscription_expires_at = 0;
    info->period_start_at = 0;
    info->anniversary_reset_day = 0;
    info->tier_ceiling_usd = 0.0f;
    info->credit_balance_usd = 0.0f;
    info->lifetime_purchased_usd = 0.0f;
    info->has_payment_method = false;
    std::move(callback).Run(std::move(info));
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "GET";
  resource_request->headers.SetHeader(
      "Authorization", "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode =
      network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kSubscriptionTrafficAnnotation);
  url_loader->SetTimeoutDuration(base::Seconds(15));

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto* raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&MahoSettingsPageHandler::OnSubscriptionResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     /*refreshed_once=*/false, std::move(url_loader)),
      /*max_body_size=*/64 * 1024);
}

void MahoSettingsPageHandler::OnSubscriptionResponse(
    GetBillingInfoCallback callback,
    bool refreshed_once,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto make_empty_info = []() {
    auto info = maho_settings::mojom::BillingInfo::New();
    info->tier = "";
    info->subscription_status = "";
    info->subscription_expires_at = 0;
    info->period_start_at = 0;
    info->anniversary_reset_day = 0;
    info->tier_ceiling_usd = 0.0f;
    info->credit_balance_usd = 0.0f;
    info->lifetime_purchased_usd = 0.0f;
    info->has_payment_method = false;
    return info;
  };

  int net_error = loader->NetError();
  const auto* response_info = loader->ResponseInfo();
  int http_status =
      response_info ? response_info->headers->response_code() : 0;

  if (net_error != net::OK || !response_body.has_value()) {
    std::move(callback).Run(make_empty_info());
    return;
  }

  if (http_status == 401 && !refreshed_once && encryptor_.has_value()) {
    auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess();
    const std::string current_refresh =
        maho::auth::GetRelayRefreshToken(prefs_, *encryptor_);
    maho::auth::RefreshAccessToken(
        current_refresh, url_loader_factory,
        base::BindOnce(
            [](base::WeakPtr<MahoSettingsPageHandler> handler,
               GetBillingInfoCallback cb, bool success, int refresh_http_status,
               std::optional<maho::auth::RefreshedTokens> tokens) {
              auto make_empty = []() {
                auto info = maho_settings::mojom::BillingInfo::New();
                info->tier = "";
                info->subscription_status = "";
                info->subscription_expires_at = 0;
                info->period_start_at = 0;
                info->anniversary_reset_day = 0;
                info->tier_ceiling_usd = 0.0f;
                info->credit_balance_usd = 0.0f;
                info->lifetime_purchased_usd = 0.0f;
                info->has_payment_method = false;
                return info;
              };
              if (!handler) {
                std::move(cb).Run(make_empty());
                return;
              }
              if (!success || !tokens || !handler->encryptor_.has_value()) {
                if (refresh_http_status == 401) {
                  maho::auth::ClearRelayTokens(handler->prefs_);
                  handler->BroadcastAccountStatus();
                } else {
                  DLOG(WARNING) << "Billing refresh failed (HTTP "
                                << refresh_http_status
                                << "); preserving auth state.";
                }
                std::move(cb).Run(make_empty());
                return;
              }
              maho::auth::StoreTokensParams params;
              params.access_token = tokens->access_token;
              params.refresh_token = tokens->refresh_token;
              params.access_expires_at = tokens->access_expires_at;
              params.refresh_expires_at = tokens->refresh_expires_at;
              params.user_email = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserEmail);
              params.user_id = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserId);
              params.user_display_name = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserDisplayName);
              params.user_tier = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserTier);
              if (!maho::auth::StoreRelayTokens(
                      handler->prefs_, *handler->encryptor_, params)) {
                maho::auth::ClearRelayTokens(handler->prefs_);
                handler->BroadcastAccountStatus();
                std::move(cb).Run(make_empty());
                return;
              }
              handler->GetBillingInfo(std::move(cb));
            },
            weak_factory_.GetWeakPtr(), std::move(callback)));
    return;
  }

  if (http_status != 200) {
    std::move(callback).Run(make_empty_info());
    return;
  }

  auto parsed =
      base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(make_empty_info());
    return;
  }

  const base::DictValue& dict = parsed->GetDict();

  auto info = maho_settings::mojom::BillingInfo::New();
  if (const std::string* v = dict.FindString("tier"))
    info->tier = *v;
  else
    info->tier = "";
  if (const std::string* v = dict.FindString("subscription_status"))
    info->subscription_status = *v;
  else
    info->subscription_status = "";

  info->subscription_expires_at = static_cast<int64_t>(
      dict.FindDouble("subscription_expires_at").value_or(0));
  info->period_start_at = static_cast<int64_t>(
      dict.FindDouble("period_start_at").value_or(0));
  info->anniversary_reset_day = static_cast<int32_t>(
      dict.FindInt("anniversary_reset_day").value_or(0));
  info->tier_ceiling_usd = static_cast<float>(
      dict.FindDouble("tier_ceiling_usd").value_or(0));
  info->credit_balance_usd = static_cast<float>(
      dict.FindDouble("credit_balance_usd").value_or(0));
  info->lifetime_purchased_usd = static_cast<float>(
      dict.FindDouble("lifetime_purchased_usd").value_or(0));
  info->has_payment_method = dict.FindBool("has_payment_method").value_or(false);

  // Persist subscription fields to prefs for GetAccountStatus:
  prefs_->SetString(maho::account_prefs::kRelaySubscriptionStatus, info->subscription_status);
  prefs_->SetInt64(maho::account_prefs::kRelaySubscriptionExpiresAt, info->subscription_expires_at);

  // Also store tier from subscription response (login response doesn't carry tier):
  if (!info->tier.empty()) {
    prefs_->SetString(maho::account_prefs::kRelayUserTier, info->tier);
  }

  // Re-broadcast so account pane updates without page reload:
  BroadcastAccountStatus();

  std::move(callback).Run(std::move(info));
}

void MahoSettingsPageHandler::BroadcastAccountStatus() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto status = maho_settings::mojom::AccountStatus::New();

  std::string encrypted_b64 =
      prefs_->GetString(acct::kRelayAccessTokenEncryptedB64);
  if (encrypted_b64.empty()) {
    status->signed_in = false;
    status->email = "";
    status->display_name = "";
    status->user_id = "";
    status->tier = "";
    status->subscription_status = "";
    status->subscription_expires_at = 0;
  } else {
    status->signed_in = true;
    status->email = prefs_->GetString(acct::kRelayUserEmail);
    status->display_name = prefs_->GetString(acct::kRelayUserDisplayName);
    status->user_id = prefs_->GetString(acct::kRelayUserId);
    status->tier = prefs_->GetString(acct::kRelayUserTier);
    status->subscription_status = prefs_->GetString(acct::kRelaySubscriptionStatus);
    status->subscription_expires_at = prefs_->GetInt64(acct::kRelaySubscriptionExpiresAt);
  }

  page_->AccountStatusChanged(std::move(status));
}

void MahoSettingsPageHandler::GetInvoices(GetInvoicesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!encryptor_.has_value()) {
    std::move(callback).Run(std::vector<maho_settings::mojom::InvoicePtr>());
    return;
  }

  std::string access_token =
      maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  if (access_token.empty()) {
    std::move(callback).Run(std::vector<maho_settings::mojom::InvoicePtr>());
    return;
  }

  std::string relay_url = maho::auth::GetRelayBaseUrl();
  GURL request_url(relay_url + "/billing/invoices");
  if (!request_url.is_valid()) {
    std::move(callback).Run(std::vector<maho_settings::mojom::InvoicePtr>());
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "GET";
  resource_request->headers.SetHeader(
      "Authorization", "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode =
      network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kInvoicesTrafficAnnotation);
  url_loader->SetTimeoutDuration(base::Seconds(15));

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto* raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> loader,
             GetInvoicesCallback cb,
             std::optional<std::string> response_body) {
            const int net_error = loader->NetError();
            const int http_status =
                (loader->ResponseInfo() && loader->ResponseInfo()->headers)
                    ? loader->ResponseInfo()->headers->response_code()
                    : 0;

            std::vector<maho_settings::mojom::InvoicePtr> invoices;
            if (net_error != net::OK || http_status != 200 || !response_body.has_value()) {
              std::move(cb).Run(std::move(invoices));
              return;
            }

            auto parsed =
                base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict()) {
              std::move(cb).Run(std::move(invoices));
              return;
            }

            const base::DictValue& dict = parsed->GetDict();
            const base::ListValue* invoices_list = dict.FindList("invoices");
            if (invoices_list) {
              for (const auto& item : *invoices_list) {
                if (item.is_dict()) {
                  const base::DictValue& inv_dict = item.GetDict();
                  auto inv = maho_settings::mojom::Invoice::New();
                  if (const std::string* id = inv_dict.FindString("id")) {
                    inv->id = *id;
                  }
                  inv->created_at = static_cast<int64_t>(
                      inv_dict.FindDouble("created_at").value_or(0));
                  inv->amount_usd = static_cast<float>(
                      inv_dict.FindDouble("amount_usd").value_or(0));
                  if (const std::string* currency = inv_dict.FindString("currency")) {
                    inv->currency = *currency;
                  }
                  if (const std::string* status = inv_dict.FindString("status")) {
                    inv->status = *status;
                  }
                  if (const std::string* pdf_url = inv_dict.FindString("pdf_url")) {
                    inv->pdf_url = *pdf_url;
                  }
                  invoices.push_back(std::move(inv));
                }
              }
            }

            std::move(cb).Run(std::move(invoices));
          },
          std::move(url_loader), std::move(callback)),
      /*max_body_size=*/65536);
}

void MahoSettingsPageHandler::GetBillingPortalUrl(GetBillingPortalUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!encryptor_.has_value()) {
    std::move(callback).Run("");
    return;
  }

  std::string access_token =
      maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  if (access_token.empty()) {
    std::move(callback).Run("");
    return;
  }

  std::string relay_url = maho::auth::GetRelayBaseUrl();
  GURL request_url(relay_url + "/billing/portal_session");
  if (!request_url.is_valid()) {
    std::move(callback).Run("");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "POST";
  resource_request->headers.SetHeader(
      "Authorization", "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode =
      network::mojom::CredentialsMode::kOmit;

  auto url_loader = network::SimpleURLLoader::Create(
      std::move(resource_request), kBillingPortalTrafficAnnotation);
  url_loader->SetTimeoutDuration(base::Seconds(15));

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  auto* raw_loader = url_loader.get();
  raw_loader->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader> loader,
             GetBillingPortalUrlCallback cb,
             std::optional<std::string> response_body) {
            const int net_error = loader->NetError();
            const int http_status =
                (loader->ResponseInfo() && loader->ResponseInfo()->headers)
                    ? loader->ResponseInfo()->headers->response_code()
                    : 0;

            if (net_error != net::OK || http_status != 200 || !response_body.has_value()) {
              std::move(cb).Run("");
              return;
            }

            auto parsed =
                base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict()) {
              std::move(cb).Run("");
              return;
            }

            const base::DictValue& dict = parsed->GetDict();
            if (const std::string* url = dict.FindString("url")) {
              std::move(cb).Run(*url);
              return;
            }

            std::move(cb).Run("");
          },
          std::move(url_loader), std::move(callback)),
      /*max_body_size=*/16384);
}

