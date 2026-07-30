// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/device_reauth/device_authenticator.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/version_info/channel.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "maho/browser/ai/maho_managed_connection_test.h"
#include "maho/browser/ai/maho_model_list_fetcher.h"
#include "maho/browser/ai/maho_unified_agent_adapter.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/mail_helper/maho_mail_service_factory.h"
#include "maho/browser/net/maho_atc_state.h"
#include "maho/browser/net/maho_content_blocker_update_service.h"
#include "maho/browser/net/maho_content_blocker_update_service_factory.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_google_sign_in.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace {

namespace ai = maho::ai_prefs;
namespace acct = maho::account_prefs;

std::u16string PasswordImportPickerTitle(
    maho_settings::mojom::PasswordImportSourceFormat source_format) {
  switch (source_format) {
    case maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordCsv:
      return u"Choose 1Password CSV export";
    case maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordPux:
      return u"Choose 1Password 1PUX export";
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenIndividualCsv:
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenOrganizationCsv:
      return u"Choose Bitwarden CSV export";
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenJson:
      return u"Choose Bitwarden JSON export";
    case maho_settings::mojom::PasswordImportSourceFormat::kApplePasswordsCsv:
      return u"Choose Apple Passwords CSV export";
    case maho_settings::mojom::PasswordImportSourceFormat::kKeePassXcCsv:
    case maho_settings::mojom::PasswordImportSourceFormat::kKeePassClassicCsv:
      return u"Choose KeePass CSV export";
  }
}

base::FilePath::StringType BuildPasswordImportFileTypeInfo(
    maho_settings::mojom::PasswordImportSourceFormat source_format,
    ui::SelectFileDialog::FileTypeInfo* file_type_info) {
  switch (source_format) {
    case maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordPux:
      file_type_info->extensions.push_back({FILE_PATH_LITERAL("1pux")});
      file_type_info->extension_description_overrides.push_back(
          u"1Password Unencrypted Export (.1pux)");
      return FILE_PATH_LITERAL("1pux");
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenJson:
      file_type_info->extensions.push_back({FILE_PATH_LITERAL("json")});
      file_type_info->extension_description_overrides.push_back(
          u"Bitwarden JSON export (.json)");
      return FILE_PATH_LITERAL("json");
    case maho_settings::mojom::PasswordImportSourceFormat::kOnePasswordCsv:
      file_type_info->extensions.push_back({FILE_PATH_LITERAL("csv")});
      file_type_info->extension_description_overrides.push_back(
          u"1Password CSV export (.csv)");
      return FILE_PATH_LITERAL("csv");
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenIndividualCsv:
    case maho_settings::mojom::PasswordImportSourceFormat::kBitwardenOrganizationCsv:
      file_type_info->extensions.push_back({FILE_PATH_LITERAL("csv")});
      file_type_info->extension_description_overrides.push_back(
          u"Bitwarden CSV export (.csv)");
      return FILE_PATH_LITERAL("csv");
    case maho_settings::mojom::PasswordImportSourceFormat::kApplePasswordsCsv:
      file_type_info->extensions.push_back({FILE_PATH_LITERAL("csv")});
      file_type_info->extension_description_overrides.push_back(
          u"Apple Passwords CSV export (.csv)");
      return FILE_PATH_LITERAL("csv");
    case maho_settings::mojom::PasswordImportSourceFormat::kKeePassXcCsv:
    case maho_settings::mojom::PasswordImportSourceFormat::kKeePassClassicCsv:
      file_type_info->extensions.push_back({FILE_PATH_LITERAL("csv")});
      file_type_info->extension_description_overrides.push_back(
          u"KeePass CSV export (.csv)");
      return FILE_PATH_LITERAL("csv");
  }
}

gfx::NativeWindow GetSettingsNativeWindow(Profile* profile) {
  BrowserWindowInterface* browser = chrome::FindLastActiveWithProfile(profile);
  if (!browser || !browser->GetWindow()) {
    return gfx::NativeWindow();
  }
  return browser->GetWindow()->GetNativeWindow();
}

// ── Auth traffic annotations ──

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

constexpr char kPasswordProviderModeMahoKey[] = "autofill.password_provider";
constexpr char kPasswordsEnabledMahoKey[] = "autofill.passwords_enabled";
constexpr char kMahoMailEnabledMahoKey[] = "mail.enabled";

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
     prefs::kEnableQuietNotificationPermissionUi, SettingMapping::Type::kBool},
    {"atc.maho_mini_global_shortcut_enabled",
     maho::sidebar_prefs::kMahoMiniGlobalShortcutEnabled,
     SettingMapping::Type::kBool},
    {"atc.maho_mini_click_override_enabled",
     maho::sidebar_prefs::kMahoMiniClickOverrideEnabled,
     SettingMapping::Type::kBool},
    {"atc.open_external_links_in_maho_mini",
     maho::sidebar_prefs::kOpenExternalLinksInMahoMini,
     SettingMapping::Type::kBool},
};

const SettingMapping* FindMapping(const std::string& maho_key) {
  for (const auto& m : kSettingsMap) {
    if (maho_key == m.maho_key) {
      return &m;
    }
  }
  return nullptr;
}

bool ShouldBypassPasswordReauthForAutomation() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableAutomation);
}

maho_settings::mojom::VaultOperationResultPtr MakeVaultUseFailure(
    const std::string& code) {
  auto result = maho_settings::mojom::VaultOperationResult::New();
  result->success = false;
  result->error_code = code;
  result->error_message = "Vault operation failed.";
  return result;
}

PrefService* GetPrefServiceForKey(Profile* profile,
                                  const std::string& maho_key) {
  if (maho_key == kHardwareAccelerationMahoKey) {
    return g_browser_process->local_state();
  }
  return profile->GetPrefs();
}

void CheckHasEnabledRulesAndUpdate() {
  maho::PostCoreTask<bool>(
      FROM_HERE, base::BindOnce([]() {
        MahoCore* core = maho::GetCore();
        if (!core) {
          return false;
        }
        char* json_str = maho_core_get_atc_rules(core);
        if (!json_str) {
          return false;
        }
        std::string json(json_str);
        maho_string_free(json_str);
        return maho::HasEnabledTrafficRuleInJson(json);
      }),
      base::BindOnce([](bool has_enabled_rules) {
        maho::MahoAtcState::SetHasEnabledRules(has_enabled_rules);
      }));
}

// ── Profile domain helpers (MahoCore sequence) ──

std::vector<maho_settings::mojom::ProfileInfoPtr>
BuildProfilesOnCoreSequence() {
  std::vector<maho_settings::mojom::ProfileInfoPtr> profiles;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return profiles;
  }

  char* json_str = maho_core_list_profiles(core);
  if (!json_str) {
    return profiles;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return profiles;
  }

  char* active_id_str = maho_core_get_active_profile_id(core);
  std::string active_id;
  if (active_id_str) {
    active_id = active_id_str;
    maho_string_free(active_id_str);
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    std::optional<bool> is_default = dict->FindBool("is_default");
    if (!id || !name) {
      continue;
    }

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
          if (!space_dict) {
            continue;
          }
          const std::string* space_id = space_dict->FindString("id");
          const std::string* space_profile =
              space_dict->FindString("profile_id");
          if (!space_id) {
            continue;
          }
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
  if (!core) {
    return nullptr;
  }

  char* result_str = maho_core_create_profile(core, name.c_str());
  if (!result_str) {
    return nullptr;
  }
  std::string result(result_str);
  maho_string_free(result_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(result, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }

  const auto& dict = parsed->GetDict();
  const std::string* id = dict.FindString("id");
  if (!id) {
    return nullptr;
  }

  auto profile = maho_settings::mojom::ProfileInfo::New();
  profile->id = *id;
  profile->name = name;
  profile->is_default = false;
  profile->is_active = false;
  return profile;
}

std::string DeleteProfileOnCoreSequence(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }
  char* json_str = maho_core_delete_profile(core, profile_id.c_str());
  if (!json_str) {
    return std::string();
  }
  std::string result(json_str);
  maho_string_free(json_str);
  return result;
}

bool SwitchProfileOnCoreSequence(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  return maho_core_switch_profile(core, profile_id.c_str());
}

// ── ATC domain helpers (MahoCore sequence) ──

std::vector<maho_settings::mojom::ATCRulePtr> BuildATCRulesOnCoreSequence() {
  std::vector<maho_settings::mojom::ATCRulePtr> rules;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return rules;
  }

  char* json_str = maho_core_get_atc_rules(core);
  if (!json_str) {
    return rules;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* pattern = dict->FindString("url_pattern");
    if (!pattern || pattern->empty()) {
      pattern = dict->FindString("name");
    }
    const std::string* target = dict->FindString("space_id");
    std::optional<bool> enabled = dict->FindBool("enabled");
    if (!id || !pattern || !target) {
      continue;
    }

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
  if (!core) {
    return nullptr;
  }

  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();

  base::DictValue rule_dict;
  rule_dict.Set("id", id);
  rule_dict.Set("urlPattern", url_pattern);
  rule_dict.Set("matchType", "glob");
  rule_dict.Set("targetSpaceId", target_space_id);
  rule_dict.Set("enabled", true);
  std::string rule_json;
  base::JSONWriter::Write(rule_dict, &rule_json);

  char* result_str = maho_core_add_traffic_rule(core, rule_json.c_str());
  if (!result_str) {
    return nullptr;
  }
  std::string returned_id(result_str);
  maho_string_free(result_str);
  if (returned_id.empty()) {
    return nullptr;
  }

  auto rule = maho_settings::mojom::ATCRule::New();
  rule->id = returned_id;
  rule->url_pattern = url_pattern;
  rule->target_space_id = target_space_id;
  rule->enabled = true;
  return rule;
}

void RemoveATCRuleOnCoreSequence(const std::string& rule_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  char* result = maho_core_remove_traffic_rule_persisted(core, rule_id.c_str());
  if (result) {
    maho_string_free(result);
  }
}

void ToggleATCRuleOnCoreSequence(const std::string& rule_id, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_toggle_traffic_rule_persisted(core, rule_id.c_str(), enabled);
}

// ── Shortcuts domain helpers (MahoCore sequence) ──

std::string KeyComboToJson(
    const maho_settings::mojom::MojoKeyComboPtr& key_combo) {
  base::DictValue dict;
  dict.Set("key", key_combo->key);
  base::ListValue modifiers;
  for (const auto& mod : key_combo->modifiers) {
    modifiers.Append(mod);
  }
  dict.Set("modifiers", std::move(modifiers));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(dict)), &json);
  return json;
}

std::vector<maho_settings::mojom::ShortcutBindingPtr>
BuildShortcutsOnCoreSequence() {
  std::vector<maho_settings::mojom::ShortcutBindingPtr> shortcuts;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return shortcuts;
  }

  char* json_str = maho_core_get_shortcuts(core);
  if (!json_str) {
    return shortcuts;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return shortcuts;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* action = dict->FindString("action");
    const std::string* label = dict->FindString("label");
    const std::string* category = dict->FindString("category");
    const base::DictValue* key_combo_dict = dict->FindDict("keyCombo");
    std::optional<bool> is_custom = dict->FindBool("isCustom");
    std::optional<bool> enabled = dict->FindBool("enabled");
    if (!action) {
      continue;
    }

    auto shortcut = maho_settings::mojom::ShortcutBinding::New();
    shortcut->action = *action;
    shortcut->label = label ? *label : *action;
    shortcut->category = category ? *category : "navigation";
    shortcut->key_combo = maho_settings::mojom::MojoKeyCombo::New();
    if (key_combo_dict) {
      if (const std::string* key = key_combo_dict->FindString("key")) {
        shortcut->key_combo->key = *key;
      }
      if (const base::ListValue* modifiers =
              key_combo_dict->FindList("modifiers")) {
        for (const auto& mod : *modifiers) {
          if (mod.is_string()) {
            shortcut->key_combo->modifiers.push_back(mod.GetString());
          }
        }
      }
    }
    shortcut->default_key_combo = maho_settings::mojom::MojoKeyCombo::New();
    const base::DictValue* default_combo_dict =
        dict->FindDict("defaultKeyCombo");
    if (default_combo_dict) {
      if (const std::string* key = default_combo_dict->FindString("key")) {
        shortcut->default_key_combo->key = *key;
      }
      if (const base::ListValue* modifiers =
              default_combo_dict->FindList("modifiers")) {
        for (const auto& mod : *modifiers) {
          if (mod.is_string()) {
            shortcut->default_key_combo->modifiers.push_back(mod.GetString());
          }
        }
      }
    }
    shortcut->is_custom = is_custom.value_or(false);
    shortcut->enabled = enabled.value_or(true);
    shortcuts.push_back(std::move(shortcut));
  }

  return shortcuts;
}

struct SetShortcutResultInternal {
  bool success;
  std::string conflict_action;
};

SetShortcutResultInternal SetShortcutOnCoreSequence(
    const std::string& action,
    const std::string& key_combo_json) {
  SetShortcutResultInternal res;
  res.success = false;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return res;
  }
  char* result =
      maho_core_set_shortcut(core, action.c_str(), key_combo_json.c_str());
  if (result) {
    std::string result_str(result);
    maho_string_free(result);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(result_str, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      if (parsed->GetDict().FindBool("success").value_or(false)) {
        res.success = true;
      } else if (const base::DictValue* err =
                     parsed->GetDict().FindDict("error")) {
        if (const std::string* existing = err->FindString("existingAction")) {
          res.conflict_action = *existing;
        }
      }
    }
  }
  return res;
}

std::optional<std::string> CheckShortcutConflictOnCoreSequence(
    const std::string& key_combo_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  char* result = maho_core_check_conflict(core, key_combo_json.c_str());
  if (result) {
    std::string res_str(result);
    maho_string_free(result);
    return res_str;
  }
  return std::nullopt;
}

void ResetShortcutOnCoreSequence(const std::string& action) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_reset_shortcut(core, action.c_str());
}

void ResetAllShortcutsOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_reset_all_shortcuts(core);
}

void ToggleShortcutOnCoreSequence(const std::string& action, bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_toggle_shortcut(core, action.c_str(), enabled);
}

std::string ExportShortcutsOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return {};
  }
  char* result = maho_core_export_shortcuts(core);
  if (result) {
    std::string res_str(result);
    maho_string_free(result);
    return res_str;
  }
  return {};
}

bool ImportShortcutsOnCoreSequence(const std::string& json_data) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  return maho_core_import_shortcuts(core, json_data.c_str());
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
  if (const std::string* url = dict.FindString("relay_url")) {
    status->relay_url = *url;
  }
  if (const std::string* room = dict.FindString("room_id")) {
    status->room_id = *room;
  }
  if (const std::string* label = dict.FindString("status_label")) {
    status->status_label = *label;
  } else {
    status->status_label = "idle";
  }
  if (const std::string* err = dict.FindString("error_message")) {
    status->error_message = *err;
  }
  status->last_sync_timestamp =
      static_cast<int64_t>(dict.FindDouble("last_sync_timestamp").value_or(0));

  return status;
}

std::vector<maho_settings::mojom::SyncDeviceInfoPtr>
BuildSyncDevicesOnCoreSequence() {
  std::vector<maho_settings::mojom::SyncDeviceInfoPtr> devices;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return devices;
  }

  char* json_str = maho_core_get_connected_devices(core);
  if (!json_str) {
    return devices;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return devices;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id || !name) {
      continue;
    }

    auto device = maho_settings::mojom::SyncDeviceInfo::New();
    device->id = *id;
    device->name = *name;
    if (const std::string* dtype = dict->FindString("device_type")) {
      device->device_type = *dtype;
    }
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
  if (!core) {
    return result;
  }

  char* json_str = maho_core_generate_sync_key(core);
  if (!json_str) {
    return result;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return result;
  }

  const auto& dict = parsed->GetDict();
  if (const std::string* key = dict.FindString("syncKey")) {
    result.sync_key = *key;
  }
  if (const std::string* room = dict.FindString("roomId")) {
    result.room_id = *room;
  }
  if (const std::string* phrase = dict.FindString("recoveryPhrase")) {
    result.recovery_phrase = *phrase;
  }

  return result;
}

maho_settings::mojom::SyncStatusPtr ConfigureSyncEncryptionOnCoreSequence(
    const std::string& recovery_phrase) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    auto s = maho_settings::mojom::SyncStatus::New();
    s->status_label = "error";
    s->error_message = "Core not available";
    return s;
  }
  char* result = maho_core_configure_sync_encryption(
      core, maho::auth::kSyncRelayUrl, recovery_phrase.c_str());
  if (result) {
    std::string json(result);
    maho_string_free(result);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const auto& dict = parsed->GetDict();
      std::optional<bool> success = dict.FindBool("success");
      if (success && !*success) {
        auto s = maho_settings::mojom::SyncStatus::New();
        s->is_syncing = false;
        s->status_label = "error";
        const std::string* error = dict.FindString("error");
        s->error_message = error ? *error : "Unknown sync configuration error";
        return s;
      }
    }
  }
  return BuildSyncStatusOnCoreSequence();
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
  char* result = maho_core_join_sync(core, maho::auth::kSyncRelayUrl,
                                     recovery_phrase.c_str());
  if (result) {
    std::string json(result);
    maho_string_free(result);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const auto& dict = parsed->GetDict();
      std::optional<bool> success = dict.FindBool("success");
      if (success && !*success) {
        auto s = maho_settings::mojom::SyncStatus::New();
        s->is_syncing = false;
        s->status_label = "error";
        const std::string* error = dict.FindString("error");
        s->error_message = error ? *error : "Unknown sync join error";
        return s;
      }
    }
  }
  return BuildSyncStatusOnCoreSequence();
}

void StopSyncOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_stop_sync(core);
}

bool DisconnectSyncDeviceOnCoreSequence(const std::string& device_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  return maho_core_disconnect_sync_device(core, device_id.c_str());
}

bool RenameSyncDeviceOnCoreSequence(const std::string& device_id,
                                    const std::string& new_name) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  return maho_core_rename_sync_device(core, device_id.c_str(),
                                      new_name.c_str());
}

// ── Theme / Spaces domain helpers (MahoCore sequence) ──

std::vector<maho_settings::mojom::SpaceBasicInfoPtr>
BuildSpacesOnCoreSequence() {
  std::vector<maho_settings::mojom::SpaceBasicInfoPtr> spaces;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return spaces;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return spaces;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return spaces;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id) {
      continue;
    }

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
  if (!core) {
    return addresses;
  }

  char* json_str = maho_core_get_autofill_addresses(core);
  if (!json_str) {
    return addresses;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return addresses;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    auto addr = maho_settings::mojom::AutofillAddress::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    addr->id = s("id");
    addr->name = s("name");
    addr->address_line1 = s("street");
    addr->address_line2 = s("addressLine2");
    addr->city = s("city");
    addr->state = s("state");
    addr->postal_code = s("zip");
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
  if (!core) {
    return payments;
  }

  char* json_str = maho_core_get_autofill_payments(core);
  if (!json_str) {
    return payments;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return payments;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    auto pay = maho_settings::mojom::AutofillPayment::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    pay->id = s("id");
    pay->card_network = s("cardNetwork");
    pay->last_four = s("lastFour");
    pay->expiration = s("expiry");
    pay->cardholder_name = s("cardName");
    payments.push_back(std::move(pay));
  }

  return payments;
}

// ── Content Blocker domain ──

std::optional<int64_t> FindInt64(const base::DictValue& dict,
                                 std::string_view key) {
  const std::optional<double> value = dict.FindDouble(key);
  return value ? std::optional<int64_t>(static_cast<int64_t>(*value))
               : std::nullopt;
}

std::optional<uint16_t> FindUint16(const base::DictValue& dict,
                                   std::string_view key) {
  const std::optional<int> value = dict.FindInt(key);
  if (!value || *value < 0 || *value > 65535) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(*value);
}

maho_settings::mojom::ContentBlockerStatsPtr BuildContentBlockerStats(
    const base::DictValue* state) {
  auto stats = maho_settings::mojom::ContentBlockerStats::New();
  stats->mode = maho_settings::mojom::ContentBlockingMode::kUnknown;
  if (!state) {
    return stats;
  }

  const std::string* mode = state->FindString("mode");
  if (mode && *mode == "native") {
    stats->mode = maho_settings::mojom::ContentBlockingMode::kNative;
  } else if (mode && *mode == "extension") {
    stats->mode = maho_settings::mojom::ContentBlockingMode::kExtension;
  } else if (mode && *mode == "disabled") {
    stats->mode = maho_settings::mojom::ContentBlockingMode::kDisabled;
  }

  stats->engine_generation =
      static_cast<uint64_t>(state->FindDouble("engineGeneration").value_or(0));
  const base::DictValue* health = state->FindDict("health");
  if (!health) {
    return stats;
  }

  stats->total_rule_count =
      static_cast<uint32_t>(health->FindInt("totalRules").value_or(0));
  stats->filter_list_count =
      static_cast<uint32_t>(health->FindInt("activeLists").value_or(0));
  if (const std::string* status = health->FindString("healthStatus")) {
    stats->health_status = *status;
  }
  if (const std::string* error = health->FindString("overallError")) {
    stats->overall_error = *error;
  }
  stats->last_update_timestamp =
      FindInt64(*health, "lastUpdateTimestamp").value_or(0);
  return stats;
}

maho_settings::mojom::ContentBlockerStatsPtr BuildCurrentContentBlockerStats() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return BuildContentBlockerStats(nullptr);
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(
      maho::core::GetContentBlockerStateJson(core), base::JSON_PARSE_RFC);
  return BuildContentBlockerStats(
      parsed && parsed->is_dict() ? &parsed->GetDict() : nullptr);
}

std::vector<maho_settings::mojom::FilterListInfoPtr>
BuildFilterListsOnUIThread() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<maho_settings::mojom::FilterListInfoPtr> lists;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return lists;
  }

  std::optional<base::Value> parsed = base::JSONReader::Read(
      maho::core::GetContentBlockerStateJson(core), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return lists;
  }
  const base::ListValue* state_lists = parsed->GetDict().FindList("lists");
  if (!state_lists) {
    return lists;
  }

  for (const auto& item : *state_lists) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    auto fl = maho_settings::mojom::FilterListInfo::New();
    auto s = [&](const char* k) -> std::string {
      const std::string* v = dict->FindString(k);
      return v ? *v : "";
    };
    fl->id = s("id");
    fl->name = s("name");
    fl->url = s("url");
    fl->enabled = dict->FindBool("enabled").value_or(false);
    fl->rule_count =
        static_cast<uint32_t>(dict->FindInt("ruleCount").value_or(0));
    if (const std::string* etag = dict->FindString("etag")) {
      fl->etag = *etag;
    }
    if (const std::string* last_modified = dict->FindString("lastModified")) {
      fl->last_modified = *last_modified;
    }
    if (const std::string* sha256 = dict->FindString("sha256")) {
      fl->sha256 = *sha256;
    }
    fl->last_attempt_timestamp = FindInt64(*dict, "lastAttemptTimestamp");
    fl->last_success_timestamp = FindInt64(*dict, "lastSuccessTimestamp");
    fl->failure_count =
        static_cast<uint32_t>(dict->FindInt("failureCount").value_or(0));
    fl->last_status = FindUint16(*dict, "lastStatus");
    if (const std::string* last_error = dict->FindString("lastError")) {
      fl->last_error = *last_error;
    }
    fl->next_retry_timestamp = FindInt64(*dict, "nextRetryTimestamp");
    lists.push_back(std::move(fl));
  }

  return lists;
}

maho_settings::mojom::ContentBlockerMutationResultPtr MutationFailure(
    std::string error_code,
    std::string error_message) {
  auto result = maho_settings::mojom::ContentBlockerMutationResult::New();
  result->error_code = std::move(error_code);
  result->error_message = std::move(error_message);
  result->stats = BuildCurrentContentBlockerStats();
  return result;
}

maho_settings::mojom::ContentBlockerMutationResultPtr ParseMutationResult(
    std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return MutationFailure("malformed_bridge_response",
                           "content blocker bridge returned invalid JSON");
  }

  const base::DictValue& dict = parsed->GetDict();
  const std::optional<bool> success = dict.FindBool("success");
  const std::optional<bool> compile_required = dict.FindBool("compileRequired");
  const base::DictValue* state = dict.FindDict("state");
  if (!success || !compile_required || (*success && !state)) {
    return MutationFailure("malformed_bridge_response",
                           "content blocker bridge response was incomplete");
  }

  auto result = maho_settings::mojom::ContentBlockerMutationResult::New();
  result->success = *success;
  result->compile_required = *compile_required;
  result->stats = BuildContentBlockerStats(state);
  if (!result->success) {
    const base::DictValue* error = dict.FindDict("error");
    const std::string* code = error ? error->FindString("code") : nullptr;
    const std::string* message = error ? error->FindString("message") : nullptr;
    if (!code || !message) {
      return MutationFailure("malformed_bridge_response",
                             "content blocker bridge error was incomplete");
    }
    result->error_code = *code;
    result->error_message = *message;
  }
  return result;
}

void ScheduleMutationCompile(
    maho_settings::mojom::ContentBlockerMutationResult* result) {
  if (!result->success || !result->compile_required) {
    return;
  }
  if (!maho::GetCore() || maho::IsBlockerWorkQuiesced()) {
    result->success = false;
    result->error_code = "compile_unavailable";
    result->error_message = "content blocker compile is unavailable";
    return;
  }
  maho::PostBlockerEngineCompileAndInstall(FROM_HERE);
  result->compile_scheduled = true;
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
  if (!core) {
    return std::nullopt;
  }
  base::DictValue address;
  address.Set("id", id);
  address.Set("name", name);
  address.Set("street", address_line1);
  address.Set("addressLine2", address_line2);
  address.Set("city", city);
  address.Set("state", state);
  address.Set("zip", postal_code);
  address.Set("country", country);
  address.Set("phone", phone);
  address.Set("email", email);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(address)), &json);
  char* result = maho_core_add_autofill_address_persisted(core, json.c_str());
  if (!result) {
    return std::nullopt;
  }
  std::string result_json(result);
  maho_string_free(result);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const auto& dict = parsed->GetDict();
  auto out = maho_settings::mojom::AutofillAddress::New();
  auto s = [&](const char* k) -> std::string {
    const std::string* v = dict.FindString(k);
    return v ? *v : "";
  };
  out->id = s("id");
  out->name = s("name");
  out->address_line1 = s("street");
  out->address_line2 = s("addressLine2");
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
  if (!core) {
    return false;
  }
  return maho_core_delete_autofill_address_persisted(core, id.c_str());
}

std::optional<maho_settings::mojom::AutofillPaymentPtr>
AddAutofillPaymentOnCoreSequence(const std::string& id,
                                 const std::string& card_network,
                                 const std::string& last_four,
                                 const std::string& expiration,
                                 const std::string& cardholder_name) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  base::DictValue payment;
  payment.Set("id", id);
  payment.Set("cardNetwork", card_network);
  payment.Set("cardName", cardholder_name);
  payment.Set("lastFour", last_four);
  payment.Set("expiry", expiration);
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(payment)), &json);
  char* result = maho_core_add_autofill_payment_persisted(core, json.c_str());
  if (!result) {
    return std::nullopt;
  }
  std::string result_json(result);
  maho_string_free(result);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const auto& dict = parsed->GetDict();
  auto out = maho_settings::mojom::AutofillPayment::New();
  auto s = [&](const char* k) -> std::string {
    const std::string* v = dict.FindString(k);
    return v ? *v : "";
  };
  out->id = s("id");
  out->card_network = s("cardNetwork");
  out->last_four = s("lastFour");
  out->expiration = s("expiry");
  out->cardholder_name = s("cardName");
  return out;
}

bool DeleteAutofillPaymentOnCoreSequence(const std::string& id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  return maho_core_delete_autofill_payment_persisted(core, id.c_str());
}

}  // namespace

// ── Constructor / Destructor ──

MahoSettingsPageHandler* MahoSettingsPageHandler::g_active_instance = nullptr;

// static
MahoSettingsPageHandler* MahoSettingsPageHandler::GetActiveInstance() {
  return g_active_instance;
}

void MahoSettingsPageHandler::NotifyShortcutRecorded(
    maho_settings::mojom::MojoKeyComboPtr key_combo) {
  if (page_.is_bound()) {
    page_->OnShortcutRecorded(std::move(key_combo));
  }
}

MahoSettingsPageHandler::MahoSettingsPageHandler(
    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_settings::mojom::Page> page,
    Profile* profile)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile),
      prefs_(profile->GetPrefs()),
      local_state_(g_browser_process->local_state()) {
  // R-4 security guard: a forged Mojo receiver can construct this handler even
  // when IsWebUIEnabled already denies OTR/Guest/system controller creation.
  // Fail closed before any prefs/OSCrypt/network/bridge init and disconnect so
  // no method is reachable in a denied context. Do not remove.
  if (!MahoIsWebUIEnabled(profile)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  password_import_job_ = maho_settings_password_helpers::CreatePasswordImportJob();
  g_active_instance = this;
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
  pref_registrar_.Add(
      maho::sidebar_prefs::kSidebarWidth,
      base::BindRepeating(&MahoSettingsPageHandler::OnPrefChanged,
                          weak_factory_.GetWeakPtr()));

  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoSettingsPageHandler::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()),
        os_crypt_async::Encryptor::Option::kEncryptSyncCompat);
  }

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  // R-9: gate the AI model-list network path on kAI. The settings surface is
  // already regular-only via MahoIsWebUIEnabled above; this revalidates the
  // AI-specific read/write path against the profile's window before any
  // provider/model request leaves the browser.
  auto ai_gate = base::BindRepeating(
      [](base::WeakPtr<MahoSettingsPageHandler> self) {
        if (!self) {
          return false;
        }
        BrowserWindowInterface* window =
            chrome::FindLastActiveWithProfile(self->profile_);
        if (!window) {
          return false;
        }
        MahoPrivateContextToken token(window, /*web_contents=*/nullptr);
        return token.Revalidate(MahoPrivateCapability::kAI);
      },
      weak_factory_.GetWeakPtr());
  model_list_fetcher_ = std::make_unique<maho::ai::MahoModelListFetcher>(
      prefs_, url_loader_factory, ai_gate);
  model_list_fetcher_->SetRefreshedCallback(
      base::BindRepeating(&MahoSettingsPageHandler::OnProviderModelsRefreshed,
                          weak_factory_.GetWeakPtr()));
  managed_connection_test_ =
      std::make_unique<maho::ai::MahoManagedConnectionTest>(prefs_,
                                                            url_loader_factory);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (service) {
    service->AddObserver(this);
  }
  maho::MahoUpdateManager::GetInstance()->AddObserver(this);
}

MahoSettingsPageHandler::~MahoSettingsPageHandler() {
  if (!password_import_file_callback_.is_null()) {
    std::move(password_import_file_callback_).Run(std::nullopt);
  }
  password_import_select_file_dialog_.reset();
  maho::MahoUpdateManager::GetInstance()->RemoveObserver(this);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (service) {
    service->RemoveObserver(this);
  }
  maho_settings_password_helpers::FreePasswordImportJob(password_import_job_);
  password_import_job_ = nullptr;
  if (g_active_instance == this) {
    g_active_instance = nullptr;
  }
}

namespace {

maho_settings::mojom::BrowserUpdateState ToMojoUpdateState(
    maho::UpdateState state) {
  switch (state) {
    case maho::UpdateState::kIdle:
      return maho_settings::mojom::BrowserUpdateState::kIdle;
    case maho::UpdateState::kChecking:
      return maho_settings::mojom::BrowserUpdateState::kChecking;
    case maho::UpdateState::kUpdateAvailable:
      return maho_settings::mojom::BrowserUpdateState::kUpdateAvailable;
    case maho::UpdateState::kDownloading:
      return maho_settings::mojom::BrowserUpdateState::kDownloading;
    case maho::UpdateState::kVerifying:
      return maho_settings::mojom::BrowserUpdateState::kVerifying;
    case maho::UpdateState::kReadyToInstall:
      return maho_settings::mojom::BrowserUpdateState::kReadyToInstall;
    case maho::UpdateState::kUpToDate:
      return maho_settings::mojom::BrowserUpdateState::kUpToDate;
    case maho::UpdateState::kError:
      return maho_settings::mojom::BrowserUpdateState::kError;
  }
  return maho_settings::mojom::BrowserUpdateState::kIdle;
}

std::string BuildChannelToString(version_info::Channel channel) {
  switch (channel) {
    case version_info::Channel::UNKNOWN:
      return "Developer";
    case version_info::Channel::CANARY:
      return "Canary";
    case version_info::Channel::DEV:
      return "Dev";
    case version_info::Channel::BETA:
      return "Beta";
    case version_info::Channel::STABLE:
      return "Stable";
  }
  return "Developer";
}

}  // namespace

void MahoSettingsPageHandler::GetBrowserVersionInfo(
    GetBrowserVersionInfoCallback callback) {
  auto info = maho_settings::mojom::BrowserVersionInfo::New();
  info->version = std::string(version_info::GetVersionNumber());
  const version_info::Channel build_channel = chrome::GetChannel();
  info->channel = BuildChannelToString(build_channel);
  info->update_state =
      ToMojoUpdateState(maho::MahoUpdateManager::GetInstance()->GetState());
  // Local/unbranded developer builds cannot self-update from the release feed,
  // so the UI suppresses the update controls instead of showing a misleading
  // "up to date" state or a Check button.
  info->update_supported = build_channel != version_info::Channel::UNKNOWN;
  std::move(callback).Run(std::move(info));
}

void MahoSettingsPageHandler::CheckForBrowserUpdates() {
  maho::MahoUpdateManager::GetInstance()->CheckForUpdates(
      /*manual_check=*/true);
}

void MahoSettingsPageHandler::ApplyBrowserUpdateAndRestart() {
  maho::MahoUpdateManager::GetInstance()->ApplyUpdateAndRestart();
}

void MahoSettingsPageHandler::OnUpdateStateChanged(maho::UpdateState state) {
  page_->OnBrowserUpdateStateChanged(ToMojoUpdateState(state));
}

void MahoSettingsPageHandler::OnUpdateProgress(double percent) {}

// ── Key-value pref settings ──

// static
const char* MahoSettingsPageHandler::GetPrefPath(const std::string& maho_key) {
  const SettingMapping* m = FindMapping(maho_key);
  return m ? m->pref_path : nullptr;
}

std::string MahoSettingsPageHandler::ReadSetting(const std::string& maho_key) {
  if (maho_key == kPasswordProviderModeMahoKey) {
    return maho_settings_password_helpers::ReadPasswordProviderModeFromCore();
  }
  if (maho_key == kPasswordsEnabledMahoKey) {
    return maho_settings_password_helpers::ReadPasswordsEnabledFromCore()
               ? "true"
               : "false";
  }
  const SettingMapping* m = FindMapping(maho_key);
  if (m) {
    PrefService* prefs = GetPrefServiceForKey(profile_, maho_key);
    if (!prefs) {
      return std::string();
    }
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
  FfiResult ffi_result = HandleFfiSetting(key, value);
  if (ffi_result != FfiResult::kNotHandled) {
    std::move(callback).Run(ffi_result == FfiResult::kOk);
    if (ffi_result == FfiResult::kOk) {
      ScheduleNotify();
    }
    return;
  }

  if (key == kPasswordProviderModeMahoKey) {
    bool passwords_enabled =
        maho_settings_password_helpers::ReadPasswordsEnabledFromCore();
    std::string mode = "maho_native";
    if (value == "disabled") {
      passwords_enabled = false;
    } else {
      mode =
          maho_settings_password_helpers::NormalizePasswordProviderMode(value);
    }
    std::move(callback).Run(
        maho_settings_password_helpers::UpdateAutofillSettingsInCore(
            passwords_enabled, mode));
    ScheduleNotify();
    return;
  }

  if (key == kPasswordsEnabledMahoKey) {
    bool passwords_enabled = value == "true";
    std::move(callback).Run(
        maho_settings_password_helpers::UpdateAutofillSettingsInCore(
            passwords_enabled, maho_settings_password_helpers::
                                   ReadPasswordProviderModeFromCore()));
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
      prefs->SetInteger(m->pref_path, value == "true"
                                          ? kCookieControlsBlockThirdPartyValue
                                          : kCookieControlsOffValue);
      std::move(callback).Run(true);
      return;
    }

    switch (m->type) {
      case SettingMapping::Type::kString:
        if (key == "general.homepage") {
          GURL gurl(value);
          if (!gurl.is_valid() ||
              (!gurl.SchemeIsHTTPOrHTTPS() && !gurl.SchemeIs("chrome"))) {
            std::move(callback).Run(false);
            return;
          }
          if (gurl.SchemeIs("chrome")) {
            std::string host(gurl.host());
            if (host != "newtab" && host != "settings" &&
                host != "maho-settings" && host != "maho-ai") {
              std::move(callback).Run(false);
              return;
            }
          }
        }
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
        if (key == "general.restore_on_startup") {
          if (int_value != 1 && int_value != 4) {
            std::move(callback).Run(false);
            return;
          }
        }
        if (key == "appearance.theme") {
          if (int_value < 0 || int_value > 2) {
            std::move(callback).Run(false);
            return;
          }
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
  // Mail feature flag. The nav schema needs this at runtime to decide whether
  // the "Maho Mail" section exists, and it is the same gate the sidebar button,
  // chrome://maho-mail, and the helper subprocess read.
  {
    auto mail = maho_settings::mojom::SettingValue::New();
    mail->key = kMahoMailEnabledMahoKey;
    mail->value = maho::sidebar_prefs::IsMahoMailEnabled(profile_->GetPrefs())
                      ? "true"
                      : "false";
    settings.push_back(std::move(mail));
  }
  AppendFfiSettings(settings);
  return settings;
}

void MahoSettingsPageHandler::ScheduleNotify() {
  notify_timer_.Start(FROM_HERE, base::Milliseconds(50),
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

MahoSettingsPageHandler::FfiResult MahoSettingsPageHandler::HandleFfiSetting(
    const std::string& key,
    const std::string& value) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core) {
    return FfiResult::kNotHandled;
  }

  if (key == "appearance.density") {
    maho_core_set_density(core, value.c_str());
  } else if (key == "appearance.sidebar_width") {
    int width = 0;
    if (!base::StringToInt(value, &width)) {
      return FfiResult::kInvalid;
    }
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
  } else if (key == "tabs.archive_timeout") {
    int hours = 0;
    if (!base::StringToInt(value, &hours)) {
      return FfiResult::kInvalid;
    }
    base::DictValue settings;
    base::DictValue general;
    general.Set("archiveTimeoutHours", static_cast<double>(hours));
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
  } else if (key == "tabs.today_tab_timeout") {
    int hours = 0;
    if (!base::StringToInt(value, &hours)) {
      return FfiResult::kInvalid;
    }
    base::DictValue settings;
    base::DictValue general;
    general.Set("todayTabTimeoutHours", static_cast<double>(hours));
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
  } else if (key == "tabs.pinned_close_behavior") {
    base::DictValue settings;
    base::DictValue general;
    general.Set("pinnedCloseBehavior", value);
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
  } else if (key == "tabs.auto_delete_empty_folders_on_tidy") {
    base::DictValue settings;
    base::DictValue general;
    general.Set("autoDeleteEmptyFoldersOnTidy", value == "true");
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
  } else {
    return FfiResult::kNotHandled;
  }

  return FfiResult::kOk;
}

void MahoSettingsPageHandler::AppendFfiSettings(
    std::vector<maho_settings::mojom::SettingValuePtr>& settings) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  char* json_str = maho_core_get_settings(core);
  if (!json_str) {
    return;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  const base::DictValue& root = parsed->GetDict();

  auto add = [&](const std::string& key, const std::string& val) {
    auto s = maho_settings::mojom::SettingValue::New();
    s->key = key;
    s->value = val;
    settings.push_back(std::move(s));
  };

  const base::DictValue* appearance = root.FindDict("appearance");
  if (appearance) {
    if (const std::string* density = appearance->FindString("density")) {
      add("appearance.density", *density);
    }
    // Sidebar width: prefer the Chromium pref (authoritative for desktop,
    // observed by MahoSidebarContainerView). Fall back to maho-core if pref is
    // unset.
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
    if (auto auto_delete = general->FindBool("autoDeleteEmptyFoldersOnTidy")) {
      add("tabs.auto_delete_empty_folders_on_tidy",
          *auto_delete ? "true" : "false");
    }
  }
}

// ── Profiles domain ──

void MahoSettingsPageHandler::GetProfiles(GetProfilesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildProfilesOnCoreSequence),
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
  maho::PostCoreTask(FROM_HERE,
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
      FROM_HERE, base::BindOnce(&DeleteProfileOnCoreSequence, profile_id),
      base::BindOnce(&MahoSettingsPageHandler::OnProfileDeleted,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSettingsPageHandler::OnProfileDeleted(DeleteProfileCallback callback,
                                               std::string json_res) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (json_res.empty()) {
    std::move(callback).Run(false);
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_res, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(false);
    return;
  }

  const auto& dict = parsed->GetDict();
  std::optional<bool> success = dict.FindBool("success");
  const std::string* profile_id = dict.FindString("profile_id");

  bool is_success = success.value_or(false);

  if (is_success && profile_id && !profile_id->empty()) {
    auto basename =
        maho::MahoSpaceProfileBridge::ProfileBasenameForId(*profile_id);
    if (basename && *basename != base::FilePath::FromASCII("Default")) {
      base::FilePath abs =
          g_browser_process->profile_manager()->user_data_dir().Append(
              *basename);
      if (profiles::IsMultipleProfilesEnabled() &&
          !IsProfileDirectoryMarkedForDeletion(abs)) {
        g_browser_process->profile_manager()
            ->GetDeleteProfileHelper()
            .MaybeScheduleProfileForDeletion(
                abs, base::DoNothing(),
                ProfileMetrics::DELETE_PROFILE_SETTINGS);
      }
    }
  }

  std::move(callback).Run(is_success);
}

void MahoSettingsPageHandler::SwitchProfile(const std::string& profile_id,
                                            SwitchProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&SwitchProfileOnCoreSequence, profile_id),
      base::BindOnce([](SwitchProfileCallback cb,
                        bool success) { std::move(cb).Run(success); },
                     std::move(callback)));
}

// ── ATC domain ──

void MahoSettingsPageHandler::GetATCRules(GetATCRulesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildATCRulesOnCoreSequence),
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
            if (rule && rule->enabled) {
              maho::MahoAtcState::SetHasEnabledRules(true);
            }
            std::move(cb).Run(std::move(rule));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::RemoveATCRule(const std::string& rule_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTaskAndReply(
      FROM_HERE, base::BindOnce(&RemoveATCRuleOnCoreSequence, rule_id),
      base::BindOnce([]() { CheckHasEnabledRulesAndUpdate(); }));
}

void MahoSettingsPageHandler::ToggleATCRule(const std::string& rule_id,
                                            bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTaskAndReply(
      FROM_HERE, base::BindOnce(&ToggleATCRuleOnCoreSequence, rule_id, enabled),
      base::BindOnce([]() { CheckHasEnabledRulesAndUpdate(); }));
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

  settings->has_relay_session = maho::auth::HasValidRelaySession(prefs_);
  settings->relay_tier = prefs_->GetString(maho::account_prefs::kRelayUserTier);
  settings->relay_subscription_status =
      prefs_->GetString(maho::account_prefs::kRelaySubscriptionStatus);
  settings->credit_balance_usd = last_known_credit_balance_;
  settings->tier_ceiling_usd = last_known_tier_ceiling_;

  // Legacy migration
  if (!prefs_->GetBoolean(ai::kSettingsMigratedFromGoogleProvider)) {
    if (settings->provider == "google") {
      prefs_->SetString(ai::kProvider, "");
      settings->provider = "";
    }
    prefs_->SetBoolean(ai::kSettingsMigratedFromGoogleProvider, true);
    prefs_->CommitPendingWrite();
  }

  // Server-side coercion (single source of truth; client is read-only).
  if (settings->has_relay_session && settings->provider.empty()) {
    prefs_->SetString(ai::kProvider, "maho-managed");
    prefs_->CommitPendingWrite();
    settings->provider = "maho-managed";
  }

  std::move(callback).Run(std::move(settings));
}

void MahoSettingsPageHandler::SetAIProvider(const std::string& provider) {
  if (provider != "" && provider != "maho-managed" && provider != "openai" &&
      provider != "anthropic" && provider != "openai-compatible" &&
      provider != "local-server") {
    DLOG(WARNING) << "Rejecting invalid AI provider: " << provider;
    return;
  }
  prefs_->SetString(ai::kProvider, provider);
  prefs_->CommitPendingWrite();
}

void MahoSettingsPageHandler::SetAIBaseUrl(const std::string& url) {
  prefs_->SetString(ai::kBaseUrl, url);
  // provider/api_key commit immediately; base_url and model must too. Leaving
  // them on the lazy flush lets a shutdown persist a new api_key alongside a
  // stale base_url/model, which authenticates against the wrong endpoint.
  prefs_->CommitPendingWrite();
}

void MahoSettingsPageHandler::SetAIApiKey(const std::string& key) {
  prefs_->SetString(ai::kApiKey, key);
  prefs_->CommitPendingWrite();
  if (key.empty()) {
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys("openai-compatible");
  }
}

void MahoSettingsPageHandler::SetAIModel(const std::string& model) {
  prefs_->SetString(ai::kModel, model);
  prefs_->CommitPendingWrite();
}

void MahoSettingsPageHandler::SetAIApprovalPolicy(const std::string& policy) {
  prefs_->SetString(ai::kApprovalPolicy, policy);
}

void MahoSettingsPageHandler::SetAISessionPersistence(bool enabled) {
  prefs_->SetBoolean(ai::kSessionPersistenceEnabled, enabled);
}

void MahoSettingsPageHandler::FetchProviderModels(
    const std::string& provider_id,
    FetchProviderModelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!model_list_fetcher_) {
    std::move(callback).Run(std::vector<std::string>(), false);
    return;
  }
  model_list_fetcher_->FetchModels(provider_id, std::move(callback));
}

void MahoSettingsPageHandler::RefreshProviderModels(
    const std::string& provider_id,
    RefreshProviderModelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!model_list_fetcher_) {
    std::move(callback).Run(std::vector<std::string>());
    return;
  }
  model_list_fetcher_->RefreshModels(
      provider_id,
      base::BindOnce([](RefreshProviderModelsCallback cb,
                        const std::vector<std::string>& models,
                        bool from_cache) { std::move(cb).Run(models); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnProviderModelsRefreshed(
    const std::string& provider_id,
    const std::vector<std::string>& model_ids) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_) {
    page_->ProviderModelsRefreshed(provider_id, model_ids);
  }
}

void MahoSettingsPageHandler::TestManagedConnection(
    TestManagedConnectionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!managed_connection_test_) {
    std::move(callback).Run(false, "Connection test helper not ready.");
    return;
  }
  managed_connection_test_->RunTest(encryptor(), std::move(callback));
}

// ── Shortcuts domain ──

void MahoSettingsPageHandler::GetShortcuts(GetShortcutsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildShortcutsOnCoreSequence),
      base::BindOnce(
          [](GetShortcutsCallback cb,
             std::vector<maho_settings::mojom::ShortcutBindingPtr> shortcuts) {
            std::move(cb).Run(std::move(shortcuts));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::SetShortcut(
    const std::string& action,
    maho_settings::mojom::MojoKeyComboPtr key_combo,
    SetShortcutCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string json_str = KeyComboToJson(key_combo);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&SetShortcutOnCoreSequence, action, json_str),
      base::BindOnce(
          [](SetShortcutCallback cb, SetShortcutResultInternal internal_res) {
            auto res = maho_settings::mojom::SetShortcutResult::New();
            res->success = internal_res.success;
            if (!internal_res.conflict_action.empty()) {
              res->conflict_action = internal_res.conflict_action;
            }
            std::move(cb).Run(std::move(res));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::CheckShortcutConflict(
    maho_settings::mojom::MojoKeyComboPtr key_combo,
    CheckShortcutConflictCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string json_str = KeyComboToJson(key_combo);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&CheckShortcutConflictOnCoreSequence, json_str),
      base::BindOnce(
          [](CheckShortcutConflictCallback cb,
             std::optional<std::string> conflict_action) {
            std::move(cb).Run(conflict_action);
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::ResetShortcut(const std::string& action) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(FROM_HERE,
                        base::BindOnce(&ResetShortcutOnCoreSequence, action));
}

void MahoSettingsPageHandler::ResetAllShortcuts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(FROM_HERE,
                        base::BindOnce(&ResetAllShortcutsOnCoreSequence));
}

void MahoSettingsPageHandler::ToggleShortcut(const std::string& action,
                                             bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(FROM_HERE, base::BindOnce(&ToggleShortcutOnCoreSequence,
                                                  action, enabled));
}

void MahoSettingsPageHandler::SetRecordingMode(bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoShortcutInterceptor::SetRecordingMode(enabled);
}

void MahoSettingsPageHandler::ExportShortcuts(
    ExportShortcutsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(FROM_HERE, base::BindOnce(&ExportShortcutsOnCoreSequence),
                     base::BindOnce(
                         [](ExportShortcutsCallback cb, std::string json_data) {
                           std::move(cb).Run(json_data);
                         },
                         std::move(callback)));
}

void MahoSettingsPageHandler::ImportShortcuts(
    const std::string& json_data,
    ImportShortcutsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&ImportShortcutsOnCoreSequence, json_data),
      base::BindOnce([](ImportShortcutsCallback cb,
                        bool success) { std::move(cb).Run(success); },
                     std::move(callback)));
}

// ── Sync domain ──

void MahoSettingsPageHandler::GetSyncStatus(GetSyncStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(FROM_HERE, base::BindOnce(&BuildSyncStatusOnCoreSequence),
                     base::BindOnce(
                         [](GetSyncStatusCallback cb,
                            maho_settings::mojom::SyncStatusPtr status) {
                           std::move(cb).Run(std::move(status));
                         },
                         std::move(callback)));
}

void MahoSettingsPageHandler::GetSyncDevices(GetSyncDevicesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildSyncDevicesOnCoreSequence),
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
      FROM_HERE, base::BindOnce(&GenerateSyncKeyOnCoreSequence),
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

void MahoSettingsPageHandler::ConfigureSyncEncryption(
    const std::string& recovery_phrase,
    ConfigureSyncEncryptionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&ConfigureSyncEncryptionOnCoreSequence, recovery_phrase),
      base::BindOnce(
          [](ConfigureSyncEncryptionCallback cb,
             maho_settings::mojom::SyncStatusPtr status) {
            std::move(cb).Run(std::move(status));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::JoinSync(const std::string& recovery_phrase,
                                       JoinSyncCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&JoinSyncOnCoreSequence, recovery_phrase),
      base::BindOnce(
          [](JoinSyncCallback cb, maho_settings::mojom::SyncStatusPtr status) {
            std::move(cb).Run(std::move(status));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::StopSync() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(FROM_HERE, base::BindOnce(&StopSyncOnCoreSequence));
}

void MahoSettingsPageHandler::DisconnectSyncDevice(
    const std::string& device_id,
    DisconnectSyncDeviceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&DisconnectSyncDeviceOnCoreSequence, device_id),
      base::BindOnce([](DisconnectSyncDeviceCallback cb,
                        bool success) { std::move(cb).Run(success); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::RenameSyncDevice(
    const std::string& device_id,
    const std::string& new_name,
    RenameSyncDeviceCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&RenameSyncDeviceOnCoreSequence, device_id, new_name),
      base::BindOnce([](RenameSyncDeviceCallback cb,
                        bool success) { std::move(cb).Run(success); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::GetSpaces(GetSpacesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildSpacesOnCoreSequence),
      base::BindOnce(
          [](GetSpacesCallback cb,
             std::vector<maho_settings::mojom::SpaceBasicInfoPtr> spaces) {
            std::move(cb).Run(std::move(spaces));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetPasswordProviderStatus(
    GetPasswordProviderStatusCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::BuildPasswordProviderStatus());
}

void MahoSettingsPageHandler::GetPasswordProviderOptions(
    GetPasswordProviderOptionsCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::BuildPasswordProviderOptions());
}

void MahoSettingsPageHandler::GetSavedPasswords(
    GetSavedPasswordsCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::GetSavedPasswordsFromCore());
}

void MahoSettingsPageHandler::SearchPasswords(
    const std::string& query,
    SearchPasswordsCallback callback) {
  std::move(callback).Run(
      maho_settings_password_helpers::SearchSavedPasswordsFromCore(query));
}

void MahoSettingsPageHandler::AddPassword(const std::string& domain,
                                          const std::string& username,
                                          const std::string& password,
                                          AddPasswordCallback callback) {
  if (ShouldBypassPasswordReauthForAutomation()) {
    OnReauthAddComplete(domain, username, password, nullptr,
                        std::move(callback), true);
    return;
  }
  Browser* browser = chrome::FindBrowserWithProfile(profile_);
  gfx::NativeWindow window =
      browser ? browser->window()->GetNativeWindow() : gfx::NativeWindow();

  device_reauth::DeviceAuthParams params(
      base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager,
      "PasswordManager.ReauthToAccessPasswordInSettings");

  auto authenticator =
      ChromeDeviceAuthenticatorFactory::GetForProfile(profile_, window, params);
  if (!authenticator) {
    std::move(callback).Run(false);
    return;
  }

  auto* authenticator_ptr = authenticator.get();
  authenticator_ptr->AuthenticateWithMessage(
      u"비밀번호 추가",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthAddComplete,
                     weak_factory_.GetWeakPtr(), domain, username, password,
                     std::move(authenticator), std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthAddComplete(
    const std::string& domain,
    const std::string& username,
    const std::string& password,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    AddPasswordCallback callback,
    bool success) {
  if (!success) {
    std::move(callback).Run(false);
    return;
  }

  const bool ok = maho_settings_password_helpers::
      AddSavedPasswordCompatibilityShimInCore(domain, username, password);
  if (ok) {
    ScheduleNotify();
  }
  std::move(callback).Run(ok);
}

void MahoSettingsPageHandler::UpdatePasswordUsername(
    const std::string& password_id,
    const std::string& username,
    UpdatePasswordUsernameCallback callback) {
  if (ShouldBypassPasswordReauthForAutomation()) {
    OnReauthUpdateComplete(password_id, username, nullptr, std::move(callback),
                           true);
    return;
  }
  Browser* browser = chrome::FindBrowserWithProfile(profile_);
  gfx::NativeWindow window =
      browser ? browser->window()->GetNativeWindow() : gfx::NativeWindow();

  device_reauth::DeviceAuthParams params(
      base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager,
      "PasswordManager.ReauthToAccessPasswordInSettings");

  auto authenticator =
      ChromeDeviceAuthenticatorFactory::GetForProfile(profile_, window, params);
  if (!authenticator) {
    std::move(callback).Run(false);
    return;
  }

  auto* authenticator_ptr = authenticator.get();
  authenticator_ptr->AuthenticateWithMessage(
      u"비밀번호 수정",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthUpdateComplete,
                     weak_factory_.GetWeakPtr(), password_id, username,
                     std::move(authenticator), std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthUpdateComplete(
    const std::string& password_id,
    const std::string& username,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    UpdatePasswordUsernameCallback callback,
    bool success) {
  if (!success) {
    std::move(callback).Run(false);
    return;
  }

  const bool ok = maho_settings_password_helpers::
      UpdateSavedPasswordUsernameCompatibilityShimInCore(password_id, username);
  if (ok) {
    ScheduleNotify();
  }
  std::move(callback).Run(ok);
}

void MahoSettingsPageHandler::DeletePassword(const std::string& password_id,
                                             DeletePasswordCallback callback) {
  if (ShouldBypassPasswordReauthForAutomation()) {
    OnReauthDeleteComplete(password_id, nullptr, std::move(callback), true);
    return;
  }
  Browser* browser = chrome::FindBrowserWithProfile(profile_);
  gfx::NativeWindow window =
      browser ? browser->window()->GetNativeWindow() : gfx::NativeWindow();

  device_reauth::DeviceAuthParams params(
      base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager,
      "PasswordManager.ReauthToAccessPasswordInSettings");

  auto authenticator =
      ChromeDeviceAuthenticatorFactory::GetForProfile(profile_, window, params);
  if (!authenticator) {
    std::move(callback).Run(false);
    return;
  }

  auto* authenticator_ptr = authenticator.get();
  authenticator_ptr->AuthenticateWithMessage(
      u"비밀번호 삭제",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthDeleteComplete,
                     weak_factory_.GetWeakPtr(), password_id,
                     std::move(authenticator), std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthDeleteComplete(
    const std::string& password_id,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    DeletePasswordCallback callback,
    bool success) {
  if (!success) {
    std::move(callback).Run(false);
    return;
  }

  const bool ok = maho_settings_password_helpers::
      DeleteSavedPasswordCompatibilityShimInCore(password_id);
  if (ok) {
    ScheduleNotify();
  }
  std::move(callback).Run(ok);
}

void MahoSettingsPageHandler::GetVaultStatus(GetVaultStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultStatusFromCore());
}

void MahoSettingsPageHandler::GetVaultProviderStatus(
    GetVaultProviderStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::BuildVaultProviderStatus());
}

void MahoSettingsPageHandler::InitializeVault(
    const std::string& master_passphrase,
    const std::string& recovery_secret,
    InitializeVaultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(maho_settings_password_helpers::InitializeVaultInCore(
      master_passphrase, recovery_secret));
}

void MahoSettingsPageHandler::UnlockVault(const std::string& master_passphrase,
                                          UnlockVaultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::UnlockVaultInCore(master_passphrase));
}

void MahoSettingsPageHandler::UnlockVaultWithRecovery(
    const std::string& recovery_secret,
    UnlockVaultWithRecoveryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::UnlockVaultWithRecoveryInCore(
          recovery_secret));
}

void MahoSettingsPageHandler::LockVault(LockVaultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(maho_settings_password_helpers::LockVaultInCore());
}

void MahoSettingsPageHandler::ListVaultItems(
    std::optional<maho_settings::mojom::PasswordProviderKind> provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
    const std::optional<std::string>& cursor,
    uint32_t limit,
    ListVaultItemsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::ListVaultItemsFromCore(provider, kinds,
                                                             cursor, limit));
}

void MahoSettingsPageHandler::SearchVaultItems(
    const std::string& origin,
    std::optional<maho_settings::mojom::PasswordProviderKind> provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
    SearchVaultItemsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::SearchVaultItemsFromCore(origin, provider,
                                                               kinds));
}

void MahoSettingsPageHandler::AddVaultLogin(
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password,
    AddVaultLoginCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(maho_settings_password_helpers::AddVaultLoginInCore(
      title, origins, username, password));
}

void MahoSettingsPageHandler::UpdateVaultLogin(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password,
    UpdateVaultLoginCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::UpdateVaultLoginInCore(
          item_id, expected_revision, title, origins, username, password));
}

void MahoSettingsPageHandler::DeleteVaultItem(
    const std::string& item_id,
    uint64_t expected_revision,
    DeleteVaultItemCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(maho_settings_password_helpers::DeleteVaultItemInCore(
      item_id, expected_revision));
}

void MahoSettingsPageHandler::UseVaultSecret(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action,
    UseVaultSecretCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (ShouldBypassPasswordReauthForAutomation()) {
    OnReauthUseVaultSecretComplete(item_id, expected_revision, action, nullptr,
                                   std::move(callback), true);
    return;
  }
  Browser* browser = chrome::FindBrowserWithProfile(profile_);
  gfx::NativeWindow window =
      browser ? browser->window()->GetNativeWindow() : gfx::NativeWindow();

  device_reauth::DeviceAuthParams params(
      base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager,
      "PasswordManager.ReauthToAccessPasswordInSettings");

  auto authenticator =
      ChromeDeviceAuthenticatorFactory::GetForProfile(profile_, window, params);
  if (!authenticator) {
    std::move(callback).Run(MakeVaultUseFailure("reauth_unavailable"));
    return;
  }

  auto* authenticator_ptr = authenticator.get();
  authenticator_ptr->AuthenticateWithMessage(
      u"저장된 비밀번호 사용",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthUseVaultSecretComplete,
                     weak_factory_.GetWeakPtr(), item_id, expected_revision,
                     action, std::move(authenticator), std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthUseVaultSecretComplete(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    UseVaultSecretCallback callback,
    bool success) {
  if (!success) {
    std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
    return;
  }
  auto result = maho_settings_password_helpers::UseVaultSecretInCore(
      item_id, expected_revision, action);
  if (result->success) {
    ScheduleNotify();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::GetVaultPolicyStatus(
    GetVaultPolicyStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultPolicyStatusFromCore());
}

void MahoSettingsPageHandler::SetVaultPolicy(
    maho_settings::mojom::VaultAgentPolicy policy,
    const std::optional<std::string>& item_id,
    const std::optional<std::string>& origin,
    const std::optional<std::string>& expires_at,
    SetVaultPolicyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto status = maho_settings_password_helpers::SetVaultPolicyInCore(
      policy, item_id, origin, expires_at);
  if (status->is_available) {
    ScheduleNotify();
  }
  std::move(callback).Run(std::move(status));
}

void MahoSettingsPageHandler::GetVaultAuditPage(
    const std::optional<std::string>& cursor,
    uint32_t limit,
    GetVaultAuditPageCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultAuditPageFromCore(cursor, limit));
}

void MahoSettingsPageHandler::SelectPasswordImportFile(
    maho_settings::mojom::PasswordImportSourceFormat source_format,
    SelectPasswordImportFileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!password_import_file_callback_.is_null()) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  ui::SelectFileDialog::FileTypeInfo file_type_info;
  const base::FilePath::StringType default_extension =
      BuildPasswordImportFileTypeInfo(source_format, &file_type_info);
  password_import_file_callback_ = std::move(callback);
  password_import_select_file_dialog_ = ui::SelectFileDialog::Create(this, nullptr);
  password_import_select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE,
      PasswordImportPickerTitle(source_format), base::FilePath(),
      &file_type_info, 1, default_extension, GetSettingsNativeWindow(profile_));
}

void MahoSettingsPageHandler::PreviewPasswordImport(
    maho_settings::mojom::PasswordImportSourceFormat source_format,
    const std::string& file_path,
    PreviewPasswordImportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::PreviewPasswordImportFromPath(
          password_import_job_, source_format, file_path));
}

void MahoSettingsPageHandler::CancelPasswordImport(
    CancelPasswordImportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(
      maho_settings_password_helpers::CancelPasswordImportJob(
          password_import_job_));
}

void MahoSettingsPageHandler::CommitPasswordImport(
    const std::string& preview_token,
    CommitPasswordImportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto result = maho_settings_password_helpers::CommitPasswordImportInCore(
      password_import_job_, preview_token);
  if (result->success && result->committed > 0) {
    ScheduleNotify();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::FileSelected(const ui::SelectedFileInfo& file,
                                           int index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  password_import_select_file_dialog_.reset();
  if (password_import_file_callback_.is_null()) {
    return;
  }
  const std::string path = file.path().AsUTF8Unsafe();
  std::move(password_import_file_callback_)
      .Run(path.empty() ? std::nullopt : std::make_optional(path));
}

void MahoSettingsPageHandler::FileSelectionCanceled() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  password_import_select_file_dialog_.reset();
  if (!password_import_file_callback_.is_null()) {
    std::move(password_import_file_callback_).Run(std::nullopt);
  }
}

void MahoSettingsPageHandler::GetAutofillAddresses(
    GetAutofillAddressesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildAutofillAddressesOnCoreSequence),
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
      FROM_HERE, base::BindOnce(&DeleteAutofillAddressOnCoreSequence, id),
      base::BindOnce([](DeleteAutofillAddressCallback cb,
                        bool success) { std::move(cb).Run(success); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::GetAutofillPayments(
    GetAutofillPaymentsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&BuildAutofillPaymentsOnCoreSequence),
      base::BindOnce(
          [](GetAutofillPaymentsCallback cb,
             std::vector<maho_settings::mojom::AutofillPaymentPtr> pays) {
            std::move(cb).Run(std::move(pays));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::GetFilterLists(GetFilterListsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildFilterListsOnUIThread());
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
      base::BindOnce(&AddAutofillPaymentOnCoreSequence, id, card_network,
                     last_four, expiration, cardholder_name),
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
      FROM_HERE, base::BindOnce(&DeleteAutofillPaymentOnCoreSequence, id),
      base::BindOnce([](DeleteAutofillPaymentCallback cb,
                        bool success) { std::move(cb).Run(success); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::AddFilterList(const std::string& id,
                                            const std::string& name,
                                            const std::string& url,
                                            AddFilterListCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  auto mutation = core
                      ? ParseMutationResult(maho::core::AddFilterListResultJson(
                            core, id.c_str(), name.c_str(), url.c_str()))
                      : MutationFailure("core_unavailable",
                                        "content blocker core is unavailable");
  ScheduleMutationCompile(mutation.get());
  const bool success = mutation->success;
  std::move(callback).Run(success, std::move(mutation));
}

void MahoSettingsPageHandler::ToggleFilterList(
    const std::string& id,
    bool enabled,
    ToggleFilterListCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  auto mutation =
      core ? ParseMutationResult(maho::core::ToggleFilterListResultJson(
                 core, id.c_str(), enabled))
           : MutationFailure("core_unavailable",
                             "content blocker core is unavailable");
  ScheduleMutationCompile(mutation.get());
  const bool success = mutation->success;
  std::move(callback).Run(success, std::move(mutation));
}

void MahoSettingsPageHandler::RemoveFilterList(
    const std::string& id,
    RemoveFilterListCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  auto mutation =
      core ? ParseMutationResult(
                 maho::core::RemoveFilterListResultJson(core, id.c_str()))
           : MutationFailure("core_unavailable",
                             "content blocker core is unavailable");
  ScheduleMutationCompile(mutation.get());
  const bool success = mutation->success;
  std::move(callback).Run(success, std::move(mutation));
}

void MahoSettingsPageHandler::RebuildContentRules(
    RebuildContentRulesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho_settings::mojom::ContentBlockerMutationResultPtr mutation;
  if (!maho::GetCore()) {
    mutation = MutationFailure("core_unavailable",
                               "content blocker core is unavailable");
  } else if (maho::core::GetContentBlockingMode(maho::GetCore()) != 0) {
    mutation = MutationFailure("non_native_mode",
                               "content blocker rebuild requires native mode");
  } else if (maho::IsBlockerWorkQuiesced()) {
    mutation = MutationFailure("compile_unavailable",
                               "content blocker compile is unavailable");
  } else {
    mutation = maho_settings::mojom::ContentBlockerMutationResult::New();
    mutation->success = true;
    mutation->compile_required = true;
    maho::PostBlockerEngineCompileAndInstall(FROM_HERE);
    mutation->compile_scheduled = true;
    mutation->stats = BuildCurrentContentBlockerStats();
  }
  const bool success = mutation->success;
  auto stats = BuildCurrentContentBlockerStats();
  std::move(callback).Run(success, std::move(stats), std::move(mutation));
}

void MahoSettingsPageHandler::GetContentBlockerStats(
    GetContentBlockerStatsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildCurrentContentBlockerStats());
}

void MahoSettingsPageHandler::SetContentBlockingMode(
    maho_settings::mojom::ContentBlockingMode mode,
    SetContentBlockingModeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<int> mode_code;
  switch (mode) {
    case maho_settings::mojom::ContentBlockingMode::kNative:
      mode_code = 0;
      break;
    case maho_settings::mojom::ContentBlockingMode::kExtension:
      mode_code = 1;
      break;
    case maho_settings::mojom::ContentBlockingMode::kDisabled:
      mode_code = 2;
      break;
    case maho_settings::mojom::ContentBlockingMode::kUnknown:
      break;
  }

  MahoCore* core = maho::GetCore();
  maho_settings::mojom::ContentBlockerMutationResultPtr mutation;
  if (!mode_code) {
    mutation = MutationFailure("invalid_mode",
                               "unknown content blocking mode is display-only");
  } else if (!core) {
    mutation = MutationFailure("core_unavailable",
                               "content blocker core is unavailable");
  } else if (!maho::core::SetContentBlockingMode(core, *mode_code)) {
    mutation = MutationFailure("mode_rejected",
                               "content blocking mode change was rejected");
  } else {
    mutation = maho_settings::mojom::ContentBlockerMutationResult::New();
    mutation->success = true;
    mutation->stats = BuildCurrentContentBlockerStats();
    if (profile_) {
      // Route the global mode change to the sole process-wide designated
      // updater (not this profile's service): the core/updater are
      // process-global, so a secondary profile's own service would be inert.
      maho::MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(
          *mode_code == 0);
      for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
        if (!browser || browser->IsDeleteScheduled() ||
            browser->GetProfile() != profile_ ||
            browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
            !browser->GetTabStripModel()) {
          continue;
        }
        content::WebContents* active_contents =
            browser->GetTabStripModel()->GetActiveWebContents();
        if (active_contents) {
          active_contents->GetController().Reload(content::ReloadType::NORMAL,
                                                  true);
        }
      }
    }
  }
  const bool success = mutation->success;
  std::move(callback).Run(success, std::move(mutation));
}

void MahoSettingsPageHandler::TriggerFilterUpdate(
    const std::optional<std::string>& list_id,
    TriggerFilterUpdateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Route to the process-wide designated updater; never construct a secondary
  // profile service. Returns false when there is no designated updater or the
  // current mode is non-native (no manual network work outside native mode).
  const std::string id =
      (list_id && !list_id->empty()) ? *list_id : std::string();
  MahoCore* core = maho::GetCore();
  const bool native_mode =
      core && maho::core::GetContentBlockingMode(core) == 0;
  const bool accepted =
      native_mode &&
      maho::MahoContentBlockerUpdateService::TriggerDesignatedUpdate(id);
  auto mutation = maho_settings::mojom::ContentBlockerMutationResult::New();
  mutation->success = accepted;
  mutation->stats = BuildCurrentContentBlockerStats();
  if (!accepted) {
    if (!core) {
      mutation->error_code = "core_unavailable";
      mutation->error_message = "content blocker core is unavailable";
    } else if (!native_mode) {
      mutation->error_code = "non_native_mode";
      mutation->error_message = "filter update requires native mode";
    } else {
      mutation->error_code = "update_not_accepted";
      mutation->error_message =
          "filter update was not accepted by the designated updater";
    }
  }
  std::move(callback).Run(accepted, std::move(mutation));
}

void MahoSettingsPageHandler::OpenExtensionsPage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::UmaHistogramBoolean("Maho.Settings.Extensions.AdvancedControlsClicked",
                            true);
  BrowserWindowInterface* browser = chrome::FindLastActiveWithProfile(profile_);
  if (browser) {
    chrome::ShowExtensions(browser);
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
    // OSCrypt is initialized asynchronously; queue the request and drain it in
    // OnOsCryptReady. Without this queue, saves attempted before OSCrypt is
    // ready were silently rejected with success=false and the user perceived
    // the BYOK key as "not persisting".
    LOG(WARNING) << "[SetBYOKKey] encryptor not ready; queuing " << provider
                 << " save for retry on OnOsCryptReady";
    pending_byok_saves_.push_back(
        PendingByokSave(provider, key, std::move(callback)));
    return;
  }
  std::string encrypted_bytes;
  if (!encryptor_->EncryptString(key, &encrypted_bytes)) {
    LOG(ERROR) << "[SetBYOKKey] EncryptString failed for provider=" << provider;
    std::move(callback).Run(false);
    return;
  }
  std::string encrypted_b64 = base::Base64Encode(encrypted_bytes);

  if (provider == "openai") {
    prefs_->SetString(ai::kByokOpenAIEncryptedB64, encrypted_b64);
    prefs_->CommitPendingWrite();
    std::move(callback).Run(true);
  } else if (provider == "anthropic") {
    prefs_->SetString(ai::kByokAnthropicEncryptedB64, encrypted_b64);
    prefs_->CommitPendingWrite();
    std::move(callback).Run(true);
  } else {
    LOG(ERROR) << "[SetBYOKKey] unknown provider: " << provider;
    std::move(callback).Run(false);
  }
}

void MahoSettingsPageHandler::ClearBYOKKey(const std::string& provider,
                                           ClearBYOKKeyCallback callback) {
  if (provider == "openai") {
    prefs_->SetString(ai::kByokOpenAIEncryptedB64, "");
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(provider);
    std::move(callback).Run(true);
  } else if (provider == "anthropic") {
    prefs_->SetString(ai::kByokAnthropicEncryptedB64, "");
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(provider);
    std::move(callback).Run(true);
  } else {
    LOG(ERROR) << "[ClearBYOKKey] unknown provider: " << provider;
    std::move(callback).Run(false);
  }
}

void MahoSettingsPageHandler::OnOsCryptReady(
    os_crypt_async::Encryptor encryptor) {
  encryptor_ = std::move(encryptor);
  if (!pending_byok_saves_.empty()) {
    LOG(WARNING) << "[OnOsCryptReady] draining " << pending_byok_saves_.size()
                 << " pending BYOK save(s) queued during encryptor init race";
    std::vector<PendingByokSave> drained;
    drained.swap(pending_byok_saves_);
    for (auto& pending : drained) {
      SetBYOKKey(pending.provider, pending.key, std::move(pending.callback));
    }
  }
}

MahoSettingsPageHandler::PendingByokSave::PendingByokSave(
    std::string provider_in,
    std::string key_in,
    SetBYOKKeyCallback callback_in)
    : provider(std::move(provider_in)),
      key(std::move(key_in)),
      callback(std::move(callback_in)) {}
MahoSettingsPageHandler::PendingByokSave::~PendingByokSave() = default;
MahoSettingsPageHandler::PendingByokSave::PendingByokSave(PendingByokSave&&) =
    default;
MahoSettingsPageHandler::PendingByokSave&
MahoSettingsPageHandler::PendingByokSave::operator=(PendingByokSave&&) =
    default;

// ═══════════════════════════════════════════════════════════════════════════════
// Auth — Login / Logout / GetAccountStatus / GetBillingInfo
// ═══════════════════════════════════════════════════════════════════════════════

void MahoSettingsPageHandler::SignInWithGoogle(
    SignInWithGoogleCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!encryptor_.has_value()) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  // Shares OnRelayLoginCompleted with password login: an established session
  // must refresh account status and billing prefs the same way regardless of
  // which credential produced it.
  maho::auth::StartGoogleSignIn(
      profile_, *encryptor_,
      base::BindOnce(&MahoSettingsPageHandler::OnRelayLoginCompleted,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

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

  auto url_loader_factory = profile_->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  maho::auth::MahoRelayLogin(
      prefs_, url_loader_factory, *encryptor_, email, password,
      base::BindOnce(&MahoSettingsPageHandler::OnRelayLoginCompleted,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSettingsPageHandler::OnRelayLoginCompleted(
    LoginCallback callback,
    bool ok,
    const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ok) {
    std::move(callback).Run(false, error_message);
    return;
  }

  BroadcastAccountStatus();

  // Populate tier + subscription prefs from /auth/subscription.
  // Fire-and-forget — failure leaves tier empty until user opens Billing pane.
  GetBillingInfo(base::BindOnce(
      [](maho_settings::mojom::BillingInfoPtr, const std::string&) {
        // Result persisted to prefs inside OnSubscriptionResponse; discard
        // here.
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
        resource_request->headers.SetHeader("Authorization",
                                            "Bearer " + access_token);
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
  maho::PostCoreClosure(FROM_HERE, base::BindOnce([]() {
                          MahoCore* core = maho::GetCore();
                          if (!core) {
                            return;
                          }
                          const char* event_json = "\"SignOut\"";
                          char* result =
                              maho_core_dispatch_shell_event(core, event_json);
                          if (result) {
                            maho_string_free(result);
                          }
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
  status->subscription_status =
      prefs_->GetString(acct::kRelaySubscriptionStatus);
  status->subscription_expires_at =
      prefs_->GetInt64(acct::kRelaySubscriptionExpiresAt);

  std::move(callback).Run(std::move(status));
}

void MahoSettingsPageHandler::GetBillingInfo(GetBillingInfoCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!encryptor_.has_value()) {
    std::move(callback).Run(nullptr, "Encryptor not ready");
    return;
  }

  std::string access_token =
      maho::auth::GetRelayAccessToken(prefs_, *encryptor_);
  if (access_token.empty()) {
    std::move(callback).Run(nullptr, "Not logged in");
    return;
  }

  std::string relay_url = maho::auth::GetRelayBaseUrl();
  GURL request_url(relay_url + "/auth/subscription");
  if (!request_url.is_valid()) {
    std::move(callback).Run(nullptr, "Invalid relay URL");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_url;
  resource_request->method = "GET";
  resource_request->headers.SetHeader("Authorization",
                                      "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

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

  int net_error = loader->NetError();
  const auto* response_info = loader->ResponseInfo();
  int http_status = response_info ? response_info->headers->response_code() : 0;

  if (net_error != net::OK) {
    std::move(callback).Run(
        nullptr, "Network error: " + base::NumberToString(net_error));
    return;
  }
  if (!response_body.has_value()) {
    std::move(callback).Run(nullptr, "Empty response from relay");
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
              if (!handler) {
                std::move(cb).Run(nullptr, "Handler destroyed during refresh");
                return;
              }
              if (!success || !tokens || !handler->encryptor_.has_value()) {
                if (refresh_http_status == 401) {
                  maho::auth::ClearRelayTokens(handler->prefs_);
                  handler->BroadcastAccountStatus();
                  std::move(cb).Run(nullptr, "Session expired");
                } else {
                  std::move(cb).Run(
                      nullptr, "Refresh failed (HTTP " +
                                   base::NumberToString(refresh_http_status) +
                                   ")");
                }
                return;
              }
              maho::auth::StoreTokensParams params;
              params.access_token = tokens->access_token;
              params.refresh_token = tokens->refresh_token;
              params.access_expires_at = tokens->access_expires_at;
              params.refresh_expires_at = tokens->refresh_expires_at;
              params.user_email = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserEmail);
              params.user_id =
                  handler->prefs_->GetString(maho::account_prefs::kRelayUserId);
              params.user_display_name = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserDisplayName);
              params.user_tier = handler->prefs_->GetString(
                  maho::account_prefs::kRelayUserTier);
              if (!maho::auth::StoreRelayTokens(handler->prefs_,
                                                *handler->encryptor_, params)) {
                maho::auth::ClearRelayTokens(handler->prefs_);
                handler->BroadcastAccountStatus();
                std::move(cb).Run(nullptr, "Failed to store session tokens");
                return;
              }
              handler->GetBillingInfo(std::move(cb));
            },
            weak_factory_.GetWeakPtr(), std::move(callback)));
    return;
  }

  if (http_status != 200) {
    std::move(callback).Run(nullptr,
                            "HTTP error: " + base::NumberToString(http_status));
    return;
  }

  auto parsed = base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(nullptr, "Failed to parse billing JSON");
    return;
  }

  const base::DictValue& dict = parsed->GetDict();

  auto info = maho_settings::mojom::BillingInfo::New();
  if (const std::string* v = dict.FindString("tier")) {
    info->tier = *v;
  } else {
    info->tier = "";
  }
  if (const std::string* v = dict.FindString("subscription_status")) {
    info->subscription_status = *v;
  } else {
    info->subscription_status = "";
  }

  info->subscription_expires_at = static_cast<int64_t>(
      dict.FindDouble("subscription_expires_at").value_or(0));
  info->period_start_at =
      static_cast<int64_t>(dict.FindDouble("period_start_at").value_or(0));
  info->anniversary_reset_day =
      static_cast<int32_t>(dict.FindInt("anniversary_reset_day").value_or(0));
  info->tier_ceiling_usd =
      static_cast<float>(dict.FindDouble("tier_ceiling_usd").value_or(0));
  info->credit_balance_usd =
      static_cast<float>(dict.FindDouble("credit_balance_usd").value_or(0));
  info->lifetime_purchased_usd =
      static_cast<float>(dict.FindDouble("lifetime_purchased_usd").value_or(0));
  info->has_payment_method =
      dict.FindBool("has_payment_method").value_or(false);

  // Persist subscription fields to prefs for GetAccountStatus:
  prefs_->SetString(maho::account_prefs::kRelaySubscriptionStatus,
                    info->subscription_status);
  prefs_->SetInt64(maho::account_prefs::kRelaySubscriptionExpiresAt,
                   info->subscription_expires_at);

  // Also store tier from subscription response (login response doesn't carry
  // tier):
  if (!info->tier.empty()) {
    prefs_->SetString(maho::account_prefs::kRelayUserTier, info->tier);
  }

  last_known_credit_balance_ = info->credit_balance_usd;
  last_known_tier_ceiling_ = info->tier_ceiling_usd;

  // Re-broadcast so account pane updates without page reload:
  BroadcastAccountStatus();

  std::move(callback).Run(std::move(info), "");
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
    status->subscription_status =
        prefs_->GetString(acct::kRelaySubscriptionStatus);
    status->subscription_expires_at =
        prefs_->GetInt64(acct::kRelaySubscriptionExpiresAt);
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
  resource_request->headers.SetHeader("Authorization",
                                      "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

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
             GetInvoicesCallback cb, std::optional<std::string> response_body) {
            const int net_error = loader->NetError();
            const int http_status =
                (loader->ResponseInfo() && loader->ResponseInfo()->headers)
                    ? loader->ResponseInfo()->headers->response_code()
                    : 0;

            std::vector<maho_settings::mojom::InvoicePtr> invoices;
            if (net_error != net::OK || http_status != 200 ||
                !response_body.has_value()) {
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
                  if (const std::string* currency =
                          inv_dict.FindString("currency")) {
                    inv->currency = *currency;
                  }
                  if (const std::string* status =
                          inv_dict.FindString("status")) {
                    inv->status = *status;
                  }
                  if (const std::string* pdf_url =
                          inv_dict.FindString("pdf_url")) {
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

void MahoSettingsPageHandler::GetBillingPortalUrl(
    GetBillingPortalUrlCallback callback) {
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
  resource_request->headers.SetHeader("Authorization",
                                      "Bearer " + access_token);
  resource_request->headers.SetHeader("Accept", "application/json");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;

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

            if (net_error != net::OK || http_status != 200 ||
                !response_body.has_value()) {
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

void MahoSettingsPageHandler::GetBuyCreditsUrl(
    int32_t pack_size_usd,
    GetBuyCreditsUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string checkout_url;
  switch (pack_size_usd) {
    case 10:
      checkout_url = "https://maho.lemonsqueezy.com/buy/credit-pack-10";
      break;
    case 50:
      checkout_url = "https://maho.lemonsqueezy.com/buy/credit-pack-50";
      break;
    case 100:
      checkout_url = "https://maho.lemonsqueezy.com/buy/credit-pack-100";
      break;
    default:
      checkout_url = "";
      break;
  }

  // Append custom_data.user_id (ADR 0010 §Webhook Handling) so the relay's
  // LS webhook handler can attribute the purchase to the correct account.
  // Reads prefs synchronously via account_prefs::kRelayUserId.
  if (!checkout_url.empty() && profile_ && profile_->GetPrefs()) {
    const std::string user_id =
        profile_->GetPrefs()->GetString(maho::account_prefs::kRelayUserId);
    if (!user_id.empty()) {
      checkout_url += "?checkout[custom][user_id]=";
      checkout_url += base::EscapeQueryParamValue(user_id, /*use_plus=*/false);
    }
  }

  std::move(callback).Run(checkout_url);
}

void MahoSettingsPageHandler::MailListAccounts(
    MailListAccountsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListAccounts(base::BindOnce(
      [](MailListAccountsCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoSettingsPageHandler::MailAddAccount(const std::string& request_json,
                                             MailAddAccountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->AddAccount(
      request_json,
      base::BindOnce(
          [](MailAddAccountCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailDeleteAccount(
    const std::string& account_id,
    MailDeleteAccountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->DeleteAccount(
      account_id,
      base::BindOnce(
          [](MailDeleteAccountCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailOAuthStartUrl(
    const std::string& provider,
    const std::string& client_id,
    const std::string& redirect_uri,
    MailOAuthStartUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->OAuthLoopbackSignIn(
      provider,
      base::BindOnce([](MailOAuthStartUrlCallback cb, bool success,
                        std::string msg) { std::move(cb).Run(success, msg); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::MailOAuthComplete(
    const std::string& state,
    const std::string& code,
    MailOAuthCompleteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->OAuthComplete(
      state, code,
      base::BindOnce(
          [](MailOAuthCompleteCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailReconnectAccount(
    const std::string& account_id,
    MailReconnectAccountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ReconnectAccount(
      account_id,
      base::BindOnce(
          [](MailReconnectAccountCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailOAuthCancel(
    const std::string& state,
    MailOAuthCancelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->OAuthCancel(state, std::move(callback));
}

// Signatures
void MahoSettingsPageHandler::MailListSignatures(
    const std::optional<std::string>& account_id,
    MailListSignaturesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListSignatures(
      account_id.value_or(""),
      base::BindOnce(
          [](MailListSignaturesCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailUpsertSignature(
    const std::string& request_json,
    MailUpsertSignatureCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  std::optional<base::Value> val =
      base::JSONReader::Read(request_json, base::JSON_PARSE_RFC);
  bool is_update = false;
  std::string id;
  if (val && val->is_dict()) {
    const std::string* id_ptr = val->GetDict().FindString("id");
    if (id_ptr && !id_ptr->empty()) {
      is_update = true;
      id = *id_ptr;
    }
  }
  if (is_update) {
    service->UpdateSignature(
        id, request_json,
        base::BindOnce(
            [](MailUpsertSignatureCallback cb, bool ok, std::string result) {
              std::move(cb).Run(ok, result.empty()
                                        ? std::nullopt
                                        : std::optional<std::string>(result));
            },
            std::move(callback)));
  } else {
    service->CreateSignature(
        request_json,
        base::BindOnce(
            [](MailUpsertSignatureCallback cb, bool ok, std::string result) {
              std::move(cb).Run(ok, result.empty()
                                        ? std::nullopt
                                        : std::optional<std::string>(result));
            },
            std::move(callback)));
  }
}

void MahoSettingsPageHandler::MailDeleteSignature(
    const std::string& id,
    MailDeleteSignatureCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeleteSignature(
      id, base::BindOnce([](MailDeleteSignatureCallback cb, bool ok,
                            std::string result) { std::move(cb).Run(ok); },
                         std::move(callback)));
}

// Templates
void MahoSettingsPageHandler::MailListTemplates(
    MailListTemplatesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListTemplates(base::BindOnce(
      [](MailListTemplatesCallback cb, bool ok, std::string result) {
        std::move(cb).Run(ok, std::move(result));
      },
      std::move(callback)));
}

void MahoSettingsPageHandler::MailUpsertTemplate(
    const std::string& request_json,
    MailUpsertTemplateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  std::optional<base::Value> val =
      base::JSONReader::Read(request_json, base::JSON_PARSE_RFC);
  bool is_update = false;
  std::string id;
  if (val && val->is_dict()) {
    const std::string* id_ptr = val->GetDict().FindString("id");
    if (id_ptr && !id_ptr->empty()) {
      is_update = true;
      id = *id_ptr;
    }
  }
  if (is_update) {
    service->UpdateTemplate(
        id, request_json,
        base::BindOnce(
            [](MailUpsertTemplateCallback cb, bool ok, std::string result) {
              std::move(cb).Run(ok, result.empty()
                                        ? std::nullopt
                                        : std::optional<std::string>(result));
            },
            std::move(callback)));
  } else {
    service->CreateTemplate(
        request_json,
        base::BindOnce(
            [](MailUpsertTemplateCallback cb, bool ok, std::string result) {
              std::move(cb).Run(ok, result.empty()
                                        ? std::nullopt
                                        : std::optional<std::string>(result));
            },
            std::move(callback)));
  }
}

void MahoSettingsPageHandler::MailDeleteTemplate(
    const std::string& id,
    MailDeleteTemplateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeleteTemplate(
      id, base::BindOnce([](MailDeleteTemplateCallback cb, bool ok,
                            std::string result) { std::move(cb).Run(ok); },
                         std::move(callback)));
}

// Labels
void MahoSettingsPageHandler::MailListLabels(const std::string& account_id,
                                             MailListLabelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListLabels(
      account_id,
      base::BindOnce(
          [](MailListLabelsCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailCreateLabel(
    const std::string& account_id,
    const std::string& name,
    const std::string& color,
    MailCreateLabelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  base::DictValue dict;
  dict.Set("account_id", account_id);
  dict.Set("name", name);
  dict.Set("color", color);
  std::string request_json;
  base::JSONWriter::Write(dict, &request_json);
  service->CreateLabel(
      request_json,
      base::BindOnce(
          [](MailCreateLabelCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, result.empty()
                                      ? std::nullopt
                                      : std::optional<std::string>(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailDeleteLabel(
    const std::string& id,
    MailDeleteLabelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeleteLabel(
      id, base::BindOnce([](MailDeleteLabelCallback cb, bool ok,
                            std::string result) { std::move(cb).Run(ok); },
                         std::move(callback)));
}

// Rules
void MahoSettingsPageHandler::MailListRules(const std::string& account_id,
                                            MailListRulesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListMailRules(
      account_id,
      base::BindOnce(
          [](MailListRulesCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailUpsertRule(const std::string& request_json,
                                             MailUpsertRuleCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  std::optional<base::Value> val =
      base::JSONReader::Read(request_json, base::JSON_PARSE_RFC);
  bool is_update = false;
  if (val && val->is_dict()) {
    const std::string* id_ptr = val->GetDict().FindString("id");
    if (id_ptr && !id_ptr->empty()) {
      is_update = true;
    }
  }
  if (is_update) {
    service->UpdateMailRule(
        request_json,
        base::BindOnce(
            [](MailUpsertRuleCallback cb, bool ok, std::string result) {
              std::move(cb).Run(ok, result.empty()
                                        ? std::nullopt
                                        : std::optional<std::string>(result));
            },
            std::move(callback)));
  } else {
    service->CreateMailRule(
        request_json,
        base::BindOnce(
            [](MailUpsertRuleCallback cb, bool ok, std::string result) {
              std::move(cb).Run(ok, result.empty()
                                        ? std::nullopt
                                        : std::optional<std::string>(result));
            },
            std::move(callback)));
  }
}

void MahoSettingsPageHandler::MailDeleteRule(const std::string& id,
                                             MailDeleteRuleCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeleteMailRule(
      id, base::BindOnce([](MailDeleteRuleCallback cb, bool ok,
                            std::string result) { std::move(cb).Run(ok); },
                         std::move(callback)));
}

void MahoSettingsPageHandler::MailReorderRules(
    const std::string& account_id,
    const std::string& rule_ids_json,
    MailReorderRulesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->ReorderMailRules(
      account_id, rule_ids_json,
      base::BindOnce([](MailReorderRulesCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

// Calendar
void MahoSettingsPageHandler::MailGetCalendarPrefs(
    MailGetCalendarPrefsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAppSetting(
      "calendar_prefs",
      base::BindOnce(
          [](MailGetCalendarPrefsCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailSetCalendarPrefs(
    const std::string& prefs_json,
    MailSetCalendarPrefsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->SetAppSetting(
      "calendar_prefs", prefs_json,
      base::BindOnce([](MailSetCalendarPrefsCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::MailListCalendarCategories(
    const std::string& account_id,
    MailListCalendarCategoriesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListCalendarCategories(
      account_id,
      base::BindOnce(
          [](MailListCalendarCategoriesCallback cb, bool ok,
             std::string result) { std::move(cb).Run(ok, std::move(result)); },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailCreateCalendarCategory(
    const std::string& account_id,
    const std::string& name,
    const std::string& color,
    MailCreateCalendarCategoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->CreateCalendarCategory(
      account_id, name, color,
      base::BindOnce(
          [](MailCreateCalendarCategoryCallback cb, bool ok,
             std::string result) {
            std::move(cb).Run(ok, result.empty()
                                      ? std::nullopt
                                      : std::optional<std::string>(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailUpdateCalendarCategory(
    const std::string& id,
    const std::string& name,
    const std::string& color,
    MailUpdateCalendarCategoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->UpdateCalendarCategory(
      id, name, color,
      base::BindOnce(
          [](MailUpdateCalendarCategoryCallback cb, bool ok,
             std::string result) {
            std::move(cb).Run(ok, result.empty()
                                      ? std::nullopt
                                      : std::optional<std::string>(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailDeleteCalendarCategory(
    const std::string& id,
    MailDeleteCalendarCategoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeleteCalendarCategory(
      id, base::BindOnce([](MailDeleteCalendarCategoryCallback cb, bool ok,
                            std::string result) { std::move(cb).Run(ok); },
                         std::move(callback)));
}

void MahoSettingsPageHandler::MailListAccountCalendars(
    const std::string& account_id,
    MailListAccountCalendarsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListAccountCalendars(
      account_id,
      base::BindOnce(
          [](MailListAccountCalendarsCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailSetCalendarVisibility(
    const std::string& calendar_row_id,
    bool visible,
    MailSetCalendarVisibilityCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->SetCalendarVisibility(
      calendar_row_id, visible,
      base::BindOnce([](MailSetCalendarVisibilityCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

// Behavior
void MahoSettingsPageHandler::MailGetBehaviorPrefs(
    MailGetBehaviorPrefsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->GetAppSetting(
      "mail_behavior_prefs",
      base::BindOnce(
          [](MailGetBehaviorPrefsCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailSetBehaviorPref(
    const std::string& key,
    const std::string& value,
    MailSetBehaviorPrefCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  // Read existing mail_behavior_prefs first to update the specific key inside
  // JSON
  service->GetAppSetting(
      "mail_behavior_prefs",
      base::BindOnce(
          [](maho::MahoMailService* svc, std::string k, std::string v,
             MailSetBehaviorPrefCallback cb, bool ok, std::string result) {
            base::DictValue update_dict;
            if (ok && !result.empty()) {
              auto parsed =
                  base::JSONReader::Read(result, base::JSON_PARSE_RFC);
              if (parsed && parsed->is_dict()) {
                for (auto item : parsed->GetDict()) {
                  update_dict.Set(item.first, item.second.Clone());
                }
              }
            }
            update_dict.Set(k, base::Value(v));
            std::string updated_json;
            base::JSONWriter::Write(update_dict, &updated_json);
            svc->SetAppSetting("mail_behavior_prefs", updated_json,
                               base::BindOnce(
                                   [](MailSetBehaviorPrefCallback cb_inner,
                                      bool ok_set, std::string result_set) {
                                     std::move(cb_inner).Run(ok_set);
                                   },
                                   std::move(cb)));
          },
          service, key, value, std::move(callback)));
}

// AI
void MahoSettingsPageHandler::MailGetAIConfig(
    const std::string& feature,
    MailGetAIConfigCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  // Pass-through: the FFI AiConfigResponse owns the full config (incl.
  // use_browser_key); do not rewrite.
  service->GetAiConfig(
      feature, base::BindOnce(
                   [](MailGetAIConfigCallback cb, bool ok, std::string result) {
                     std::move(cb).Run(ok, std::move(result));
                   },
                   std::move(callback)));
}

void MahoSettingsPageHandler::MailSetAIConfig(
    const std::string& feature,
    const std::string& config_json,
    MailSetAIConfigCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  // Pass-through: forward raw config JSON (incl. use_browser_key) unchanged;
  // mail-core AiConfig owns persistence.
  service->SaveAiConfig(
      config_json,
      base::BindOnce([](MailSetAIConfigCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::MailSetAIKey(const std::string& provider,
                                           const std::string& api_key,
                                           MailSetAIKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  std::string ffi_provider = provider;
  std::string ffi_model = "gpt-4o";
  if (provider == "openai") {
    ffi_provider = "OpenAi";
    ffi_model = "gpt-4o";
  } else if (provider == "anthropic") {
    ffi_provider = "Anthropic";
    ffi_model = "claude-3-5-sonnet";
  }
  base::DictValue dict;
  dict.Set("feature", "default");
  dict.Set("provider", ffi_provider);
  dict.Set("model", ffi_model);
  dict.Set("api_key", api_key);
  dict.Set("base_url", "");
  std::string config_json;
  base::JSONWriter::Write(dict, &config_json);
  service->SaveAiConfig(
      config_json,
      base::BindOnce([](MailSetAIKeyCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

// Security (PGP/S-MIME)
void MahoSettingsPageHandler::MailGeneratePgpKey(
    const std::string& account_id,
    const std::string& email,
    const std::string& passphrase,
    MailGeneratePgpKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  base::DictValue dict;
  dict.Set("account_id", account_id);
  dict.Set("email", email);
  dict.Set("name", "Maho User");
  dict.Set("key_type", "rsa2048");
  std::string request_json;
  base::JSONWriter::Write(dict, &request_json);
  service->GeneratePgpKey(
      request_json,
      base::BindOnce(
          [](MailGeneratePgpKeyCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, result.empty()
                                      ? std::nullopt
                                      : std::optional<std::string>(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailImportPgpKey(
    const std::string& account_id,
    const std::string& key_block,
    const std::optional<std::string>& passphrase,
    MailImportPgpKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  base::DictValue dict;
  dict.Set("account_id", account_id);
  dict.Set("key_data", key_block);
  std::string request_json;
  base::JSONWriter::Write(dict, &request_json);
  service->ImportPgpKey(
      request_json,
      base::BindOnce(
          [](MailImportPgpKeyCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, result.empty()
                                      ? std::nullopt
                                      : std::optional<std::string>(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailListPgpKeys(
    const std::string& account_id,
    MailListPgpKeysCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListPgpKeys(
      account_id,
      base::BindOnce(
          [](MailListPgpKeysCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailDeletePgpKey(
    const std::string& key_id,
    MailDeletePgpKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeletePgpKey(
      key_id, base::BindOnce([](MailDeletePgpKeyCallback cb, bool ok,
                                std::string result) { std::move(cb).Run(ok); },
                             std::move(callback)));
}

void MahoSettingsPageHandler::MailSetDefaultPgpKey(
    const std::string& account_id,
    const std::string& key_id,
    MailSetDefaultPgpKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->SetDefaultPgpKey(
      account_id, key_id,
      base::BindOnce([](MailSetDefaultPgpKeyCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::MailImportSmimeIdentity(
    const std::string& account_id,
    const std::string& p12_data_base64,
    const std::string& passphrase,
    MailImportSmimeIdentityCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  base::DictValue dict;
  dict.Set("account_id", account_id);
  dict.Set("p12_data", p12_data_base64);
  dict.Set("password", passphrase);
  std::string request_json;
  base::JSONWriter::Write(dict, &request_json);
  service->ImportSmimeIdentity(
      request_json,
      base::BindOnce(
          [](MailImportSmimeIdentityCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, result.empty()
                                      ? std::nullopt
                                      : std::optional<std::string>(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailListSmimeIdentities(
    const std::string& account_id,
    MailListSmimeIdentitiesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->ListSmimeIdentities(
      account_id,
      base::BindOnce(
          [](MailListSmimeIdentitiesCallback cb, bool ok, std::string result) {
            std::move(cb).Run(ok, std::move(result));
          },
          std::move(callback)));
}

void MahoSettingsPageHandler::MailDeleteSmimeIdentity(
    const std::string& id,
    MailDeleteSmimeIdentityCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->DeleteSmimeIdentity(
      id, base::BindOnce([](MailDeleteSmimeIdentityCallback cb, bool ok,
                            std::string result) { std::move(cb).Run(ok); },
                         std::move(callback)));
}

void MahoSettingsPageHandler::MailSetDefaultSmimeIdentity(
    const std::string& account_id,
    const std::string& identity_id,
    MailSetDefaultSmimeIdentityCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }
  service->SetDefaultSmimeIdentity(
      account_id, identity_id,
      base::BindOnce([](MailSetDefaultSmimeIdentityCallback cb, bool ok,
                        std::string result) { std::move(cb).Run(ok); },
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnAccountsChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnMailAccountsChanged();
}

void MahoSettingsPageHandler::OnMutation(const std::string& account_id,
                                         const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (payload == "signatures") {
    page_->OnMailSignaturesChanged();
  } else if (payload == "templates") {
    page_->OnMailTemplatesChanged();
  } else if (payload == "labels") {
    page_->OnMailLabelsChanged();
  } else if (payload == "rules") {
    page_->OnMailRulesChanged();
  } else if (payload == "security") {
    page_->OnMailSecurityChanged();
  } else if (payload == "behavior") {
    page_->OnMailBehaviorChanged();
  } else if (payload == "ai") {
    page_->OnMailAIConfigChanged();
  } else {
    page_->OnMailSignaturesChanged();
    page_->OnMailTemplatesChanged();
    page_->OnMailLabelsChanged();
    page_->OnMailRulesChanged();
    page_->OnMailSecurityChanged();
    page_->OnMailBehaviorChanged();
    page_->OnMailAIConfigChanged();
  }
}

void MahoSettingsPageHandler::OnCalendar(const std::string& account_id,
                                         const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnMailCalendarChanged();
}
