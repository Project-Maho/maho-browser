// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"
#include "maho/browser/ui/webui/maho_subscription_checkout.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/base64.h"
#include "base/check.h"
#include "base/memory/ref_counted.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/device_reauth/chrome_device_authenticator_factory.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/views/frame/browser_view.h"  // nogncheck
#include "maho/browser/importer/maho_browser_detector.h"  // nogncheck
#include "maho/browser/ui/views/importer/maho_migration_dialog_view.h"  // nogncheck
#include "ui/views/widget/widget.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/device_reauth/device_authenticator.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "maho/browser/updates/maho_product_version.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/content_switches.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
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
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_ai_provider_oauth.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_google_sign_in.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/views/view.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace {

namespace ai = maho::ai_prefs;
namespace acct = maho::account_prefs;

template <typename String>
void ClearVaultString(String& value) {
  for (auto& character : value) {
    volatile auto* byte = &character;
    *byte = 0;
  }
  value.clear();
}

// Heap ownership keeps captured secrets out of movable string small buffers,
// and wipes them even when an outstanding authorization callback is cancelled.
struct VaultRequestSecrets {
  std::optional<std::string> password;
  std::optional<std::string> notes;
  ~VaultRequestSecrets() {
    if (password) maho::core::SecureClearString(&*password);
    if (notes) maho::core::SecureClearString(&*notes);
  }
};

class VaultSecretView : public views::View {
 public:
  explicit VaultSecretView(const char* secret) {
    base::UTF8ToUTF16(secret, strlen(secret), &secret_);
    SetPreferredSize(gfx::Size(480, 140));
  }
  ~VaultSecretView() override { Clear(); }
  void Clear() { ClearVaultString(secret_); }
  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    canvas->DrawStringRectWithFlags(secret_, gfx::FontList(),
                          GetColorProvider()->GetColor(ui::kColorLabelForeground),
                          GetContentsBounds(),
                          gfx::Canvas::MULTI_LINE | gfx::Canvas::CHARACTER_BREAKABLE);
  }

 private:
  // Deliberately not a Label or Textfield: no accessible value, selection,
  // clipboard command, or persistent RenderText copy of the stored password.
  std::u16string secret_;
};

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
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* browser =
      collection ? collection->GetLastActiveBrowser() : nullptr;
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

// `atc.open_external_links_in_maho_mini` is deliberately NOT in kSettingsMap.
// maho-core owns it: ATCManager::decide_link_destination is the only consumer
// and it reads core, never the Chromium pref, so mapping this key to a pref
// produced a settings toggle that changed nothing. Reads and writes are
// special-cased onto the core FFI below, with a one-time seed from the legacy
// pref so existing users keep their choice.
constexpr char kOpenExternalLinksInMahoMiniMahoKey[] =
    "atc.open_external_links_in_maho_mini";

constexpr char kPasswordProviderModeMahoKey[] = "autofill.password_provider";
constexpr char kPasswordsEnabledMahoKey[] = "autofill.passwords_enabled";
// Vault trust settings: both are core-owned, so reads and writes route through
// the core settings FFI instead of a Chromium pref.
constexpr char kVaultAutoLockMinutesMahoKey[] =
    "autofill.vault_auto_lock_minutes";
constexpr char kVaultDeviceAuthRequiredMahoKey[] =
    "autofill.vault_require_device_auth";
constexpr char kMahoMailEnabledMahoKey[] = "mail.enabled";
constexpr char kConversationAutoArchiveAfterDaysMahoKey[] =
    "conversation.auto_archive_after_days";
constexpr char kAIMailReadAllowedMahoKey[] = "ai.mail_read_allowed";

// ── Pref-based settings mapping ──
constexpr char kHardwareAccelerationMahoKey[] =
    "advanced.hardware_acceleration";
constexpr char kMemorySaverModeMahoKey[] =
    "advanced.memory_saver_mode";
constexpr char kMemorySaverTimeoutMahoKey[] =
    "advanced.memory_saver_timeout";
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

// Normalizes a raw kNetworkPredictionOptions pref value to the set the Maho
// Settings UI exposes (Standard=0, Disabled=2, Extended=3). The deprecated
// wifi-only value (1) is collapsed to Standard so a stored legacy value maps to
// a real option instead of rendering nothing.
int NormalizeNetworkPredictionOptions(int value) {
  value = std::clamp(value, kNetworkPredictionOptionsMinValue,
                     kNetworkPredictionOptionsMaxValue);
  return value == kNetworkPredictionOptionsWifiOnlyDeprecatedValue
             ? kNetworkPredictionOptionsStandardValue
             : value;
}

constexpr SettingMapping kSettingsMap[] = {
    {kMahoMailEnabledMahoKey, maho::sidebar_prefs::kMahoMailEnabled,
     SettingMapping::Type::kBool},
    {kAIMailReadAllowedMahoKey, ai::kMailReadAllowed,
     SettingMapping::Type::kBool},
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
    {kMemorySaverModeMahoKey,
     performance_manager::user_tuning::prefs::kMemorySaverModeState,
     SettingMapping::Type::kInt},
    {kMemorySaverTimeoutMahoKey,
     performance_manager::user_tuning::prefs::
         kMemorySaverModeAggressiveness,
     SettingMapping::Type::kInt},
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
    {"atc.peek_enabled", maho::sidebar_prefs::kPeekEnabled,
     SettingMapping::Type::kBool},
    {"atc.peek_link_routing_enabled",
     maho::sidebar_prefs::kPeekLinkRoutingEnabled,
     SettingMapping::Type::kBool},
    {"atc.peek_popup_routing_enabled",
     maho::sidebar_prefs::kPeekPopupRoutingEnabled,
     SettingMapping::Type::kBool},
    // Web-page translation target language ("Translate pages into"). Bound to
    // the standard translate recent-target pref, which TranslateManager's
    // GetTargetLanguage() consults first (kTranslateRecentTarget is enabled by
    // default) and which the translate bubble also writes, so the setting and
    // the in-page bubble stay consistent.
    {"general.translate_target_language",
     translate::prefs::kPrefTranslateRecentTarget,
     SettingMapping::Type::kString},
};

struct SettingScopeMapping {
  const char* maho_key;
  maho_settings::mojom::SettingScope scope;
};

// Keep this exhaustive table aligned with SETTING_METADATA in
// browser/resources/maho_settings/schema/setting_schema.ts, the ownership
// source of truth for renderer-visible settings.
constexpr SettingScopeMapping kSettingScopes[] = {
    {kAIMailReadAllowedMahoKey,
     maho_settings::mojom::SettingScope::kProfile},
    {"advanced.hardware_acceleration",
     maho_settings::mojom::SettingScope::kDevice},
    {kMemorySaverModeMahoKey,
     maho_settings::mojom::SettingScope::kDevice},
    {kMemorySaverTimeoutMahoKey,
     maho_settings::mojom::SettingScope::kDevice},
    {"advanced.preload_pages", maho_settings::mojom::SettingScope::kProfile},
    {"appearance.density", maho_settings::mojom::SettingScope::kCoreGlobal},
    {"appearance.sidebar_width",
     maho_settings::mojom::SettingScope::kProfile},
    {"appearance.theme", maho_settings::mojom::SettingScope::kProfile},
    {"atc.maho_mini_click_override_enabled",
     maho_settings::mojom::SettingScope::kProcess},
    {"atc.maho_mini_global_shortcut_enabled",
     maho_settings::mojom::SettingScope::kProcess},
    {"atc.open_external_links_in_maho_mini",
     maho_settings::mojom::SettingScope::kProcess},
    {"atc.peek_enabled", maho_settings::mojom::SettingScope::kProfile},
    {"atc.peek_link_routing_enabled",
     maho_settings::mojom::SettingScope::kProfile},
    {"atc.peek_popup_routing_enabled",
     maho_settings::mojom::SettingScope::kProfile},
    {"autofill.password_provider",
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {"autofill.passwords_enabled",
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {kVaultAutoLockMinutesMahoKey,
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {kVaultDeviceAuthRequiredMahoKey,
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {"general.homepage", maho_settings::mojom::SettingScope::kProfile},
    {"general.prompt_for_download",
     maho_settings::mojom::SettingScope::kProfile},
    {"general.restore_on_startup",
     maho_settings::mojom::SettingScope::kProfile},
    {"mail.enabled", maho_settings::mojom::SettingScope::kProfile},
    {"notifications.quiet_permission_ui",
     maho_settings::mojom::SettingScope::kProfile},
    {"privacy.block_third_party_cookies",
     maho_settings::mojom::SettingScope::kProfile},
    {"privacy.do_not_track", maho_settings::mojom::SettingScope::kProfile},
    {"privacy.safe_browsing", maho_settings::mojom::SettingScope::kProfile},
    {"search.suggestions", maho_settings::mojom::SettingScope::kProfile},
    {"tabs.archive_timeout", maho_settings::mojom::SettingScope::kProfile},
    {"tabs.auto_delete_empty_folders_on_tidy",
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {kConversationAutoArchiveAfterDaysMahoKey,
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {"sidebar.new_tab_position",
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {"tabs.pinned_close_behavior",
     maho_settings::mojom::SettingScope::kCoreGlobal},
    {"tabs.today_tab_timeout",
     maho_settings::mojom::SettingScope::kCoreGlobal},
};

const SettingMapping* FindMapping(const std::string& maho_key) {
  for (const auto& m : kSettingsMap) {
    if (maho_key == m.maho_key) {
      return &m;
    }
  }
  return nullptr;
}

const SettingScopeMapping* FindScopeMapping(const std::string& maho_key) {
  for (const auto& mapping : kSettingScopes) {
    if (maho_key == mapping.maho_key) {
      return &mapping;
    }
  }
  return nullptr;
}

std::string PasswordAuthorizationProfileKey(Profile* profile) {
  return profile ? maho::GetProfileIdentityKey(profile->GetPath(),
                                                profile->GetPrefs())
                 : std::string();
}

std::unique_ptr<device_reauth::DeviceAuthenticator>
CreatePasswordAuthenticator(Profile* profile) {
  device_reauth::DeviceAuthParams params(
      base::Seconds(60), device_reauth::DeviceAuthSource::kPasswordManager,
      "PasswordManager.ReauthToAccessPasswordInSettings");
  return ChromeDeviceAuthenticatorFactory::GetForProfile(
      profile, GetSettingsNativeWindow(profile), params);
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
  if (maho_key == kHardwareAccelerationMahoKey ||
      maho_key == kMemorySaverModeMahoKey ||
      maho_key == kMemorySaverTimeoutMahoKey) {
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

std::optional<std::string> ReadActiveMahoProfileIdJsonOnCoreSequence() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }
  char* raw = maho_core_get_active_profile_id(core);
  if (!raw) {
    return std::nullopt;
  }
  std::string result(raw);
  maho_string_free(raw);
  return result;
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
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kFooter;
  maho::InvalidateSidebarCoreCache(invalidation);

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
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kAll;
  maho::InvalidateSidebarCoreCache(invalidation);
  return result;
}

class ProfileDeletionCoordinator
    : public ProfileAttributesStorage::Observer,
      public ProfileManagerObserver {
 public:
  static ProfileDeletionCoordinator* Get() {
    static base::NoDestructor<ProfileDeletionCoordinator> coordinator;
    return coordinator.get();
  }

  bool HasPendingDeletion() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return pending_deletion_.has_value() || core_deletion_in_flight_;
  }

  void Delete(ProfileManager* profile_manager,
              std::string canonical_profile_id,
              base::FilePath profile_path,
              MahoSettingsPageHandler::DeleteProfileCallback callback) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    CHECK(profile_manager);
    ProfileAttributesStorage* storage =
        &profile_manager->GetProfileAttributesStorage();
    if (!profile_attributes_observation_.IsObservingSource(storage)) {
      CHECK(!pending_deletion_);
      profile_attributes_observation_.Reset();
      profile_attributes_observation_.Observe(storage);
    }
    if (!profile_manager_observation_.IsObservingSource(profile_manager)) {
      CHECK(!pending_deletion_);
      profile_manager_observation_.Reset();
      profile_manager_observation_.Observe(profile_manager);
    }

    if (HasPendingDeletion()) {
      std::move(callback).Run(false);
      return;
    }

    pending_deletion_.emplace(
        std::move(canonical_profile_id), std::move(profile_path),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(callback),
                                                     false));
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    if (!bridge->SetProfileLifecycleState(
            pending_deletion_->canonical_profile_id,
            maho::ProfileLifecycleState::kDeleting)) {
      CompleteBeforeChromiumCommit(pending_deletion_->profile_path);
      return;
    }
    bridge->NotifyChanged(/*is_structural=*/false);

    const base::FilePath scheduled_path = pending_deletion_->profile_path;
    profile_manager->GetDeleteProfileHelper().MaybeScheduleProfileForDeletion(
        scheduled_path,
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(
                &ProfileDeletionCoordinator::OnChromiumDeletionSchedulingSettled,
                base::Unretained(this), scheduled_path),
            nullptr),
        ProfileMetrics::DELETE_PROFILE_SETTINGS);
  }

  void OnProfileWasRemoved(const base::FilePath& profile_path,
                           const std::u16string& profile_name) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!pending_deletion_ ||
        profile_path != pending_deletion_->profile_path) {
      return;
    }
    core_deletion_in_flight_ = true;
    PendingDeletion pending = std::move(*pending_deletion_);
    pending_deletion_.reset();
    const std::string canonical_profile_id = pending.canonical_profile_id;
    maho::PostCoreTask<std::string>(
        FROM_HERE,
        base::BindOnce(&DeleteProfileOnCoreSequence, canonical_profile_id),
        base::BindOnce(&ProfileDeletionCoordinator::OnCoreDeletionFinished,
                       base::Unretained(this), std::move(pending)));
  }

  void OnProfileManagerDestroying() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    profile_attributes_observation_.Reset();
    profile_manager_observation_.Reset();
    if (!pending_deletion_) {
      return;
    }
    PendingDeletion pending = std::move(*pending_deletion_);
    pending_deletion_.reset();
    std::move(pending.callback).Run(false);
  }

 private:
  friend class base::NoDestructor<ProfileDeletionCoordinator>;

  struct PendingDeletion {
    PendingDeletion(
        std::string canonical_profile_id,
        base::FilePath profile_path,
        MahoSettingsPageHandler::DeleteProfileCallback callback)
        : canonical_profile_id(std::move(canonical_profile_id)),
          profile_path(std::move(profile_path)),
          callback(std::move(callback)) {}
    PendingDeletion(PendingDeletion&&) = default;
    PendingDeletion& operator=(PendingDeletion&&) = default;
    ~PendingDeletion() = default;

    std::string canonical_profile_id;
    base::FilePath profile_path;
    MahoSettingsPageHandler::DeleteProfileCallback callback;
  };

  ProfileDeletionCoordinator() = default;
  ~ProfileDeletionCoordinator() override {
    if (pending_deletion_) {
      PendingDeletion pending = std::move(*pending_deletion_);
      pending_deletion_.reset();
      std::move(pending.callback).Run(false);
    }
  }

  void OnChromiumDeletionSchedulingSettled(
      base::FilePath scheduled_path,
      Profile*) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!pending_deletion_ ||
        pending_deletion_->profile_path != scheduled_path) {
      return;
    }
    if (!IsProfileDirectoryMarkedForDeletion(scheduled_path)) {
      CompleteBeforeChromiumCommit(scheduled_path);
    }
  }

  void CompleteBeforeChromiumCommit(const base::FilePath& profile_path) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!pending_deletion_ ||
        pending_deletion_->profile_path != profile_path) {
      return;
    }
    PendingDeletion pending = std::move(*pending_deletion_);
    pending_deletion_.reset();
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    bridge->SetProfileLifecycleState(pending.canonical_profile_id,
                                     maho::ProfileLifecycleState::kReady);
    bridge->NotifyChanged(/*is_structural=*/false);
    std::move(pending.callback).Run(false);
  }

  void OnCoreDeletionFinished(PendingDeletion pending,
                              std::string json_result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    bool core_success = false;
    bool core_not_found = false;
    if (std::optional<base::Value> parsed =
            base::JSONReader::Read(json_result, base::JSON_PARSE_RFC);
        parsed && parsed->is_dict()) {
      const base::DictValue& result = parsed->GetDict();
      core_success = result.FindBool("success").value_or(false);
      const std::string* outcome = result.FindString("outcome");
      const std::string* error_code = result.FindString("error_code");
      core_not_found = (outcome && *outcome == "not_found") ||
                       (error_code && *error_code == "PROFILE_NOT_FOUND");
    }

    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    const bool reconciled = bridge->ReconcileProfileRegistryFromCore();
    bool record_absent = false;
    if (reconciled) {
      record_absent = !bridge->CanonicalizeProfileId(
          pending.canonical_profile_id).has_value();
    }
    const bool success = reconciled && record_absent &&
                         (core_success || core_not_found);
    if (!success) {
      bridge->SetProfileLifecycleState(
          pending.canonical_profile_id,
          maho::ProfileLifecycleState::kRepairRequired);
    }
    bridge->NotifyChanged(/*is_structural=*/true);
    core_deletion_in_flight_ = false;
    std::move(pending.callback).Run(success);
  }

  std::optional<PendingDeletion> pending_deletion_;
  bool core_deletion_in_flight_ = false;
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorage::Observer>
      profile_attributes_observation_{this};
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      profile_manager_observation_{this};
  SEQUENCE_CHECKER(sequence_checker_);
};

bool SwitchProfileOnCoreSequence(const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  const bool switched = maho_core_switch_profile(core, profile_id.c_str());
  if (switched) {
    maho::SidebarCacheInvalidation invalidation;
    invalidation.fragments = maho::SidebarCoreFragment::kAll;
    maho::InvalidateSidebarCoreCache(invalidation);
  }
  return switched;
}

std::string UpdateProfileMetadataOnCoreSequence(const std::string& profile_id,
                                                const std::string& name,
                                                const std::string& avatar_color) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }
  char* json = maho_core_update_profile(core, profile_id.c_str(), name.c_str(),
                                        avatar_color.c_str());
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kFooter;
  maho::InvalidateSidebarCoreCache(invalidation);
  return result;
}

std::string GetProfileArchiveTimeoutOnCoreSequence(
    const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }
  char* json =
      maho_core_get_profile_archive_timeout(core, profile_id.c_str());
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

std::string SetProfileArchiveTimeoutOnCoreSequence(
    const std::string& profile_id,
    int32_t timeout_hours) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }
  char* json = maho_core_set_profile_archive_timeout(
      core, profile_id.c_str(), timeout_hours);
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
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
      core, maho::auth::GetSyncRelayUrl().c_str(), recovery_phrase.c_str());
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
  char* result = maho_core_join_sync(core, maho::auth::GetSyncRelayUrl().c_str(),
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

struct CurrentBrowserSpaceCoreResult {
  std::string spaces_json;
  std::string tabs_json;
};

CurrentBrowserSpaceCoreResult BuildCurrentBrowserSpaceCoreResult(
    const std::string& space_id,
    int64_t window_id) {
  CurrentBrowserSpaceCoreResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return result;
  }

  char* spaces_json = maho_core_get_space_view_models(core);
  if (!spaces_json) {
    return result;
  }
  result.spaces_json.assign(spaces_json);
  maho_string_free(spaces_json);

  const std::optional<std::string> space_id_json =
      base::WriteJson(base::Value(space_id));
  if (!space_id_json) {
    result.spaces_json.clear();
    return result;
  }
  char* tabs_json =
      maho_core_get_space_tabs(core, space_id_json->c_str(), &window_id);
  if (!tabs_json) {
    result.spaces_json.clear();
    return result;
  }
  result.tabs_json.assign(tabs_json);
  maho_string_free(tabs_json);
  return result;
}

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

class MahoVaultRevealDialog {
 public:
  MahoVaultRevealDialog(content::WebContents* contents, const char* secret) {
    delegate_.SetTitle(u"Saved password");
    delegate_.SetModalType(ui::mojom::ModalType::kWindow);
    delegate_.SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
    delegate_.SetButtonLabel(ui::mojom::DialogButton::kOk, u"Done");
    secret_view_ = delegate_.SetContentsView(
        std::make_unique<VaultSecretView>(secret));
    delegate_.RegisterWindowClosingCallback(base::BindOnce(
        &MahoVaultRevealDialog::Clear, base::Unretained(this)));
    auto params = views::DialogDelegate::GetDialogWidgetInitParams(
        &delegate_, contents->GetTopLevelNativeWindow(),
        contents->GetNativeView(), gfx::Rect());
    params.ownership = views::Widget::InitParams::CLIENT_OWNS_WIDGET;
    widget_ = std::make_unique<views::Widget>();
    widget_->Init(std::move(params));
    widget_->MakeCloseSynchronous(base::BindOnce(
        [](MahoVaultRevealDialog* dialog, views::Widget::ClosedReason) {
          dialog->Close();
        }, base::Unretained(this)));
    widget_->Show();
  }
  ~MahoVaultRevealDialog() { Close(); }

 private:
  void Clear() {
    if (secret_view_) {
      secret_view_->Clear();
      secret_view_ = nullptr;
    }
  }
  void Close() {
    Clear();
    widget_.reset();
  }
  views::DialogDelegate delegate_;
  raw_ptr<VaultSecretView> secret_view_ = nullptr;
  std::unique_ptr<views::Widget> widget_;
};

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

const maho::ProfileRegistryRecord* MahoSettingsPageHandler::FindRegistryRecord(
    const std::string& canonical_id) const {
  const maho::ProfileCatalogResult& catalog =
      maho::MahoSpaceProfileBridge::GetInstance()->GetProfileCatalog();
  for (const maho::ProfileRegistryRecord& record : catalog.records) {
    if (record.maho_id == canonical_id) {
      return &record;
    }
  }
  return nullptr;
}

maho_settings::mojom::SelectedProfileContextPtr
MahoSettingsPageHandler::BuildSelectedProfileContext() const {
  if (!selected_profile_) {
    return nullptr;
  }

  const maho::ProfileRegistryRecord* record =
      FindRegistryRecord(selected_profile_id_);
  if (!record) {
    return nullptr;
  }

  auto context = maho_settings::mojom::SelectedProfileContext::New();
  context->profile_id = selected_profile_id_;
  context->target_token = selected_profile_target_token_;
  context->context_revision = selected_profile_context_revision_;
  context->profile_revision = selected_profile_revision_;
  switch (record->lifecycle) {
    case maho::ProfileLifecycleState::kReady:
      context->lifecycle_state =
          maho_settings::mojom::ProfileLifecycleState::kReady;
      break;
    case maho::ProfileLifecycleState::kDeleting:
      context->lifecycle_state =
          maho_settings::mojom::ProfileLifecycleState::kDeleting;
      break;
    case maho::ProfileLifecycleState::kProvisioning:
    case maho::ProfileLifecycleState::kRepairRequired:
      context->lifecycle_state =
          maho_settings::mojom::ProfileLifecycleState::kUnavailable;
      break;
  }
  context->is_host_profile = selected_profile_ == profile_;
  context->is_active_maho_profile = record->is_active;
  return context;
}

maho_settings::mojom::ProfileTargetErrorPtr
MahoSettingsPageHandler::MakeProfileTargetError(
    maho_settings::mojom::ProfileTargetErrorCode code,
    const std::string& message) const {
  auto error = maho_settings::mojom::ProfileTargetError::New();
  error->code = code;
  error->message = message;
  error->current_context_revision = selected_profile_context_revision_;
  if (selected_profile_revision_ != 0) {
    error->current_profile_revision = selected_profile_revision_;
  }
  return error;
}

void MahoSettingsPageHandler::InvalidateSelectedProfileContext() {
  const bool had_selected_context = selected_profile_ || !selected_profile_id_.empty() ||
                                    !selected_profile_target_token_.empty() ||
                                    selected_profile_attached_;
  selected_profile_observation_.Reset();
  selected_profile_ = nullptr;
  selected_profile_id_.clear();
  selected_profile_target_token_.clear();
  selected_profile_revision_ = 0;
  selected_profile_attached_ = false;
  ++selected_profile_load_generation_;
  if (had_selected_context) {
    ++selected_profile_context_revision_;
  }
}

void MahoSettingsPageHandler::OnProfileWillBeDestroyed(Profile* profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (profile == selected_profile_) {
    InvalidateSelectedProfileContext();
  }
}

void MahoSettingsPageHandler::OnProfileMarkedForPermanentDeletion(
    Profile* profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (profile == selected_profile_) {
    InvalidateSelectedProfileContext();
  }
}

void MahoSettingsPageHandler::OnProfileManagerDestroying() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observing_profile_manager_ = false;
  InvalidateSelectedProfileContext();
}

void MahoSettingsPageHandler::OnSpaceProfileBridgeChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (selected_profile_id_.empty()) {
    return;
  }

  const maho::ProfileRegistryRecord* record =
      FindRegistryRecord(selected_profile_id_);
  if (!record || record->lifecycle != maho::ProfileLifecycleState::kReady ||
      record->revision != selected_profile_revision_) {
    InvalidateSelectedProfileContext();
  }
}

MahoSettingsPageHandler::MahoSettingsPageHandler(
    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_settings::mojom::Page> page,
    Profile* profile,
    content::WebContents* host_web_contents)
    : content::WebContentsObserver(host_web_contents),
      receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile),
      host_web_contents_(host_web_contents),
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

  profile_bridge_observation_.Observe(maho::MahoSpaceProfileBridge::GetInstance());
  if (g_browser_process && g_browser_process->profile_manager()) {
    g_browser_process->profile_manager()->AddObserver(this);
    observing_profile_manager_ = true;
  }

  password_import_job_ = maho_settings_password_helpers::CreatePasswordImportJob();
  g_active_instance = this;
  pref_registrar_.Init(prefs_);
  if (local_state_) {
    local_state_pref_registrar_.Init(local_state_);
  }
  for (const auto& m : kSettingsMap) {
    const bool is_local_state =
        (m.maho_key == kHardwareAccelerationMahoKey ||
         m.maho_key == kMemorySaverModeMahoKey ||
         m.maho_key == kMemorySaverTimeoutMahoKey);
    PrefChangeRegistrar* registrar =
        (is_local_state && local_state_) ? &local_state_pref_registrar_
                                         : &pref_registrar_;
    registrar->Add(m.pref_path,
                   base::BindRepeating(&MahoSettingsPageHandler::OnPrefChanged,
                                       weak_factory_.GetWeakPtr()));
  }
  pref_registrar_.Add(
      maho::sidebar_prefs::kSidebarWidth,
      base::BindRepeating(&MahoSettingsPageHandler::OnPrefChanged,
                          weak_factory_.GetWeakPtr()));
  // AI models-snapshot prefs: push OnAIModelsSettingsChanged so an open
  // Models page reflects composer-side Default changes (chrome://maho-ai
  // quick switcher writes the same maho.ai.provider/model pair) live.
  for (const auto* ai_pref_path :
       {ai::kProvider, ai::kModel, maho::ai_prefs::kBaseUrl,
        maho::ai_prefs::kApiKey, maho::ai_prefs::kProviderConfigs,
        maho::ai_prefs::kTaskModels,
        maho::ai_prefs::kByokOpenAIEncryptedB64,
        maho::ai_prefs::kByokAnthropicEncryptedB64,
        maho::ai_prefs::kByokOpenAICompatibleEncryptedB64}) {
    pref_registrar_.Add(
        ai_pref_path,
        base::BindRepeating(
            &MahoSettingsPageHandler::OnAIModelsSettingsPrefsChanged,
            weak_factory_.GetWeakPtr()));
  }

  MigrateAtcExternalMiniSettingToCore();

  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoSettingsPageHandler::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()));
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
        ProfileBrowserCollection* collection =
            ProfileBrowserCollection::GetForProfile(self->profile_);
        BrowserWindowInterface* window =
            collection ? collection->GetLastActiveBrowser() : nullptr;
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

  // The periodic core tick is the only thing that can auto-lock the Vault
  // without a page-initiated action, so the page cannot learn about it from a
  // reply. Subscribe to the browser-process broadcast and push it instead.
  vault_lock_state_subscription_ = maho::AddVaultLockStateChangeCallback(
      base::BindRepeating(&MahoSettingsPageHandler::OnVaultLockStateChanged,
                          weak_factory_.GetWeakPtr()));
}

MahoSettingsPageHandler::~MahoSettingsPageHandler() {
  weak_factory_.InvalidateWeakPtrs();
  ++vault_reveal_generation_;
  if (vault_reveal_authenticator_) vault_reveal_authenticator_->Cancel();
  vault_reveal_authenticator_.reset();
  vault_reveal_dialog_.reset();
  auto* password_authorization =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  password_authorization->RevokeProfile(
      PasswordAuthorizationProfileKey(profile_));
  password_authorization->ClearMahoOwnedClipboard();
  vault_lock_state_subscription_ = {};
  selected_profile_observation_.Reset();
  profile_bridge_observation_.Reset();
  if (observing_profile_manager_) {
    if (g_browser_process && g_browser_process->profile_manager()) {
      g_browser_process->profile_manager()->RemoveObserver(this);
    }
    observing_profile_manager_ = false;
  }

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

// Maho ships its own release feed on all three platforms, so the channel shown
// to the user is Maho's channel, not Chromium's. chrome::GetChannel() reads a
// GOOGLE_CHROME_BRANDING-only table and reports UNKNOWN for this unbranded
// build, which would label every shipped Maho "Developer" and hide the update
// controls.
std::string BuildChannelToString(maho::UpdateChannel channel) {
  switch (channel) {
    case maho::UpdateChannel::kStable:
      return "Stable";
    case maho::UpdateChannel::kBeta:
      return "Beta";
    case maho::UpdateChannel::kDev:
      return "Dev";
    case maho::UpdateChannel::kCanary:
      return "Canary";
  }
  return "Stable";
}

}  // namespace

void MahoSettingsPageHandler::GetBrowserVersionInfo(
    GetBrowserVersionInfoCallback callback) {
  auto info = maho_settings::mojom::BrowserVersionInfo::New();
  // The user-facing version is Maho's calendar product version (e.g.
  // 2026.9.29), not Chromium's engine version (e.g. 149.0.7795.0). Every other
  // Maho surface -- update feed, release notes, artifact names -- is keyed on
  // the product version, so showing the engine version here made the About
  // card disagree with the release the user is actually running.
  info->version = std::string(maho::updates::kMahoProductVersion);
  const maho::MahoUpdateManager* update_manager =
      maho::MahoUpdateManager::GetInstance();
  info->channel = BuildChannelToString(update_manager->GetChannel());
  info->update_state = ToMojoUpdateState(update_manager->GetState());
  // The update controls are gated on Maho's own update subsystem being live
  // (maho.update.enabled), not on Chromium's channel: every shipped Maho has a
  // real delegate and its own feed, so hiding the controls would be wrong.
  info->update_supported = update_manager->IsEnabled();
  // Windows (Store) and Linux (package/archive) installs cannot self-apply an
  // update, so their delegates describe where to get it; macOS returns empty.
  info->update_guidance = update_manager->GetUpdateGuidance();
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

void MahoSettingsPageHandler::OnVaultLockStateChanged(bool locked) {
  if (locked) {
    ++vault_reveal_generation_;
    if (vault_reveal_authenticator_) vault_reveal_authenticator_->Cancel();
    vault_reveal_authenticator_.reset();
    vault_reveal_dialog_.reset();
    maho::passwords::MahoPasswordAuthorizationService::Get()->RevokeProfile(
        PasswordAuthorizationProfileKey(profile_));
  }
  if (!page_.is_bound()) {
    return;
  }
  page_->OnVaultLockStateChanged(locked);
}

void MahoSettingsPageHandler::WebContentsDestroyed() {
  host_web_contents_ = nullptr;
  ++vault_reveal_generation_;
  maho::passwords::MahoPasswordAuthorizationService::Get()->RevokeProfile(
      PasswordAuthorizationProfileKey(profile_));
  if (vault_reveal_authenticator_) vault_reveal_authenticator_->Cancel();
  vault_reveal_authenticator_.reset();
  vault_reveal_dialog_.reset();
}

void MahoSettingsPageHandler::PrimaryPageChanged(content::Page&) {
  ++vault_reveal_generation_;
  maho::passwords::MahoPasswordAuthorizationService::Get()->RevokeProfile(
      PasswordAuthorizationProfileKey(profile_));
  if (vault_reveal_authenticator_) vault_reveal_authenticator_->Cancel();
  vault_reveal_authenticator_.reset();
  vault_reveal_dialog_.reset();
}

void MahoSettingsPageHandler::MigrateAtcExternalMiniSettingToCore() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!prefs_ || !local_state_) {
    return;
  }
  // The marker lives in LOCAL STATE because the maho-core value it guards is
  // process-global. Reading it from profile prefs would let a second profile
  // re-seed from its own legacy pref and overwrite the first profile's value.
  if (!local_state_->FindPreference(
          maho::sidebar_prefs::kOpenExternalLinksInMahoMiniMigrated)) {
    // Marker not registered in this local state, so a seed could not be
    // recorded and would therefore re-run on every startup, clobbering the
    // user's core value each time. Skipping is the safe failure: core simply
    // keeps whatever it already holds.
    return;
  }
  RunAtcExternalMiniMigration(
      maho::GetCore(),
      [this] {
        return local_state_->GetBoolean(
            maho::sidebar_prefs::kOpenExternalLinksInMahoMiniMigrated);
      },
      [this] {
        return prefs_->GetBoolean(
            maho::sidebar_prefs::kOpenExternalLinksInMahoMini);
      },
      [this] {
        local_state_->SetBoolean(
            maho::sidebar_prefs::kOpenExternalLinksInMahoMiniMigrated, true);
      });
}

// ── Key-value pref settings ──

// static
const char* MahoSettingsPageHandler::GetPrefPath(const std::string& maho_key) {
  const SettingMapping* m = FindMapping(maho_key);
  return m ? m->pref_path : nullptr;
}

std::string MahoSettingsPageHandler::ReadSetting(const std::string& maho_key) {
  if (maho_key == kOpenExternalLinksInMahoMiniMahoKey) {
    MahoCore* core = maho::GetCore();
    if (!core) {
      return std::string();
    }
    return maho_core_get_open_external_links_in_maho_mini(core) ? "true"
                                                               : "false";
  }
  if (maho_key == kPasswordProviderModeMahoKey) {
    return maho_settings_password_helpers::ReadPasswordProviderModeFromCore();
  }
  if (maho_key == kPasswordsEnabledMahoKey) {
    return maho_settings_password_helpers::ReadPasswordsEnabledFromCore()
               ? "true"
               : "false";
  }
  if (maho_key == kVaultAutoLockMinutesMahoKey) {
    const std::optional<unsigned int> minutes =
        maho_settings_password_helpers::ReadVaultAutoLockMinutesFromCore();
    return minutes.has_value() ? base::NumberToString(*minutes)
                               : std::string();
  }
  if (maho_key == kVaultDeviceAuthRequiredMahoKey) {
    return maho_settings_password_helpers::ReadVaultDeviceAuthRequiredFromCore()
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
    if (maho_key == kMemorySaverModeMahoKey) {
      return prefs->GetInteger(m->pref_path) !=
                     static_cast<int>(performance_manager::user_tuning::prefs::
                                          MemorySaverModeState::kDisabled)
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

void MahoSettingsPageHandler::GetGlobalSettingsSnapshot(
    GetGlobalSettingsSnapshotCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<maho_settings::mojom::SettingValuePtr> settings =
      BuildSettingsSnapshot();
  std::vector<maho_settings::mojom::ScopedSettingValuePtr> scoped_settings;
  scoped_settings.reserve(settings.size());
  for (auto& setting : settings) {
    const SettingScopeMapping* scope = FindScopeMapping(setting->key);
    CHECK(scope) << "Missing ownership scope for " << setting->key;
    auto scoped_setting = maho_settings::mojom::ScopedSettingValue::New();
    scoped_setting->key = std::move(setting->key);
    scoped_setting->value = std::move(setting->value);
    scoped_setting->scope = scope->scope;
    scoped_settings.push_back(std::move(scoped_setting));
  }
  std::move(callback).Run(std::move(scoped_settings));
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
    if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
      std::move(callback).Run(false);
      return;
    }
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

  if (key == kOpenExternalLinksInMahoMiniMahoKey) {
    MahoCore* core = maho::GetCore();
    if (!core) {
      std::move(callback).Run(false);
      return;
    }
    maho_core_set_open_external_links_in_maho_mini(core, value == "true");
    std::move(callback).Run(true);
    ScheduleNotify();
    return;
  }

  if (key == kPasswordsEnabledMahoKey) {
    if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
      std::move(callback).Run(false);
      return;
    }
    bool passwords_enabled = value == "true";
    std::move(callback).Run(
        maho_settings_password_helpers::UpdateAutofillSettingsInCore(
            passwords_enabled, maho_settings_password_helpers::
                                   ReadPasswordProviderModeFromCore()));
    ScheduleNotify();
    return;
  }

  if (key == kVaultAutoLockMinutesMahoKey) {
    if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
      std::move(callback).Run(false);
      return;
    }
    unsigned int minutes = 0;
    if (!base::StringToUint(value, &minutes) ||
        !maho_settings_password_helpers::IsSupportedVaultAutoLockMinutes(
            minutes)) {
      std::move(callback).Run(false);
      return;
    }
    const bool success =
        maho_settings_password_helpers::UpdateVaultAutoLockMinutesInCore(minutes);
    std::move(callback).Run(success);
    if (success) {
      ScheduleNotify();
    }
    return;
  }

  if (key == kVaultDeviceAuthRequiredMahoKey) {
    if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
      std::move(callback).Run(false);
      return;
    }
    const bool success =
        maho_settings_password_helpers::UpdateVaultDeviceAuthRequiredInCore(
            value == "true");
    std::move(callback).Run(success);
    if (success) {
      ScheduleNotify();
    }
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
    if (key == kMemorySaverModeMahoKey) {
      prefs->SetInteger(
          m->pref_path,
          value == "true"
              ? static_cast<int>(performance_manager::user_tuning::prefs::
                                     MemorySaverModeState::kEnabled)
              : static_cast<int>(performance_manager::user_tuning::prefs::
                                     MemorySaverModeState::kDisabled));
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
          int_value = NormalizeNetworkPredictionOptions(int_value);
        }
        if (key == kMemorySaverTimeoutMahoKey) {
          if (int_value < 0 || int_value > 2) {
            std::move(callback).Run(false);
            return;
          }
        }
        if (key == "general.restore_on_startup") {
          if (int_value != 1 && int_value != 4 && int_value != 5) {
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

void MahoSettingsPageHandler::GetSelectedProfileContext(
    maho_settings::mojom::ProfileTargetPtr target,
    GetSelectedProfileContextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto result = maho_settings::mojom::SelectedProfileContextResult::New();
  if (!target || target->profile_id.empty()) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kInvalidProfileId,
        "Selected profile ID is invalid.");
    std::move(callback).Run(std::move(result));
    return;
  }

  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  const uint64_t registry_revision = bridge->GetProfileRegistryRevision();
  const std::optional<std::string> canonical_id =
      bridge->CanonicalizeProfileId(target->profile_id);
  if (!canonical_id) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileNotFound,
        "Selected profile was not found.");
    std::move(callback).Run(std::move(result));
    return;
  }

  const maho::ProfileRegistryPathResult resolved =
      bridge->ResolveProfilePath(*canonical_id, registry_revision);
  if (resolved.error != maho::ProfileRegistryLookupError::kNone ||
      !resolved.path) {
    result->error = MakeLookupErrorForTesting(
        resolved.error, selected_profile_context_revision_,
        selected_profile_revision_ == 0
            ? std::nullopt
            : std::optional<uint64_t>(selected_profile_revision_));
    std::move(callback).Run(std::move(result));
    return;
  }

  std::string target_token = target->target_token;
  if (target_token.empty()) {
    target_token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  if (selected_profile_attached_ && selected_profile_ &&
      selected_profile_id_ == *canonical_id &&
      selected_profile_target_token_ == target_token) {
    result->context = BuildSelectedProfileContext();
    std::move(callback).Run(std::move(result));
    return;
  }

  ProfileManager* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!profile_manager) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Profile manager is unavailable.");
    std::move(callback).Run(std::move(result));
    return;
  }

  const base::FilePath expected_path =
      profile_manager->user_data_dir().Append(*resolved.path);
  if (!profile_manager->IsAllowedProfilePath(expected_path) ||
      IsProfileDirectoryMarkedForDeletion(expected_path)) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileDeleting,
        "Selected profile is being deleted.");
    std::move(callback).Run(std::move(result));
    return;
  }

  const maho::ProfileRegistryRecord* record = FindRegistryRecord(*canonical_id);
  if (!record || record->lifecycle != maho::ProfileLifecycleState::kReady) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Selected profile is unavailable.");
    std::move(callback).Run(std::move(result));
    return;
  }

  const uint64_t load_generation = ++selected_profile_load_generation_;
  selected_profile_id_ = *canonical_id;
  selected_profile_target_token_ = target_token;
  if (Profile* loaded_profile =
          profile_manager->GetProfileByPath(expected_path)) {
    OnSelectedProfileLoaded(load_generation, *canonical_id,
                            std::move(target_token), expected_path,
                            record->revision, std::move(callback),
                            loaded_profile);
    return;
  }

  profile_manager->LoadProfileByPath(
      expected_path, false,
      base::BindOnce(&MahoSettingsPageHandler::OnSelectedProfileLoaded,
                     weak_factory_.GetWeakPtr(), load_generation,
                     *canonical_id, std::move(target_token), expected_path,
                     record->revision, std::move(callback)));
}

void MahoSettingsPageHandler::OnSelectedProfileLoaded(
    uint64_t load_generation,
    std::string canonical_id,
    std::string target_token,
    base::FilePath expected_path,
    uint64_t record_revision,
    GetSelectedProfileContextCallback callback,
    Profile* loaded_profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto result = maho_settings::mojom::SelectedProfileContextResult::New();
  if (load_generation != selected_profile_load_generation_ ||
      canonical_id != selected_profile_id_ ||
      target_token != selected_profile_target_token_) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile context is stale.");
    std::move(callback).Run(std::move(result));
    return;
  }

  ProfileManager* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  const maho::ProfileRegistryRecord* record = FindRegistryRecord(canonical_id);
  if (!profile_manager || !loaded_profile ||
      !profile_manager->IsValidProfile(loaded_profile) ||
      loaded_profile->GetPath() != expected_path ||
      IsProfileDirectoryMarkedForDeletion(expected_path) || !record ||
      record->revision != record_revision ||
      record->lifecycle != maho::ProfileLifecycleState::kReady) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Selected profile became unavailable while loading.");
    std::move(callback).Run(std::move(result));
    return;
  }

  AttachSelectedProfile(canonical_id, target_token, record_revision,
                        loaded_profile);
  result->context = BuildSelectedProfileContext();
  if (!result->context) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kInternal,
        "Selected profile context could not be created.");
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::AttachSelectedProfile(
    const std::string& canonical_id,
    const std::string& target_token,
    uint64_t record_revision,
    Profile* selected_profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool same_context =
      selected_profile_attached_ && selected_profile_ == selected_profile &&
      selected_profile_id_ == canonical_id &&
      selected_profile_target_token_ == target_token;
  if (!same_context) {
    selected_profile_observation_.Reset();
    selected_profile_ = selected_profile;
    selected_profile_observation_.Observe(selected_profile_);
    ++selected_profile_context_revision_;
  }
  selected_profile_id_ = canonical_id;
  selected_profile_target_token_ = target_token;
  selected_profile_revision_ = record_revision;
  selected_profile_attached_ = true;
}

void MahoSettingsPageHandler::GetSearchEngines(
    GetSearchEnginesCallback callback) {
  std::vector<maho_settings::mojom::SearchEngineInfoPtr> engines;
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(std::move(engines));
    return;
  }

  const TemplateURL* default_engine = service->GetDefaultSearchProvider();
  const std::u16string default_keyword =
      default_engine ? default_engine->keyword() : std::u16string();

  for (const TemplateURL* engine : service->GetTemplateURLs()) {
    if (!engine->prepopulate_id()) {
      continue;
    }

    const std::string name = base::UTF16ToUTF8(engine->short_name());
    const std::string name_lower = base::ToLowerASCII(name);
    if (name_lower.find("wikipedia") != std::string::npos ||
        name_lower.find("ebay") != std::string::npos) {
      continue;
    }

    auto info = maho_settings::mojom::SearchEngineInfo::New();
    info->keyword = base::UTF16ToUTF8(engine->keyword());
    info->name = name;
    const GURL& favicon_url = engine->favicon_url();
    if (favicon_url.is_valid()) {
      info->icon_url =
          "chrome://favicon2/?size=32&scaleFactor=1x&iconUrl=" +
          base::EscapeQueryParamValue(favicon_url.spec(), false);
    }
    info->is_default = engine->keyword() == default_keyword;
    engines.push_back(std::move(info));
  }

  std::move(callback).Run(std::move(engines));
}

void MahoSettingsPageHandler::SetDefaultSearchEngine(
    const std::string& keyword,
    SetDefaultSearchEngineCallback callback) {
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false);
    return;
  }

  for (TemplateURL* engine : service->GetTemplateURLs()) {
    if (base::UTF16ToUTF8(engine->keyword()) == keyword) {
      service->SetUserSelectedDefaultSearchProvider(engine);
      std::move(callback).Run(true);
      return;
    }
  }

  std::move(callback).Run(false);
}

maho_settings::mojom::ProfileTargetErrorPtr
MahoSettingsPageHandler::ValidateSelectedProfileTarget(
    const maho_settings::mojom::ProfileTargetPtr& target,
    std::optional<uint64_t> expected_context_revision,
    std::optional<uint64_t> expected_profile_revision) const {
  if (!target || target->profile_id.empty()) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kInvalidProfileId,
        "Selected profile ID is invalid.");
  }
  if (!selected_profile_ || !selected_profile_attached_) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Selected profile is not loaded.");
  }
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  const std::optional<std::string> canonical_id =
      bridge->CanonicalizeProfileId(target->profile_id);
  if (!canonical_id) {
    const maho::ProfileRegistryPathResult lookup = bridge->ResolveProfilePath(
        target->profile_id, bridge->GetProfileRegistryRevision());
    return MakeLookupErrorForTesting(
        lookup.error, selected_profile_context_revision_,
        selected_profile_revision_ == 0
            ? std::nullopt
            : std::optional<uint64_t>(selected_profile_revision_));
  }
  if (*canonical_id != selected_profile_id_) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile target does not match the current context.");
  }
  if (target->target_token.empty() ||
      target->target_token != selected_profile_target_token_) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile target token is stale.");
  }
  if (expected_context_revision &&
      *expected_context_revision != selected_profile_context_revision_) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile context revision is stale.");
  }
  const maho::ProfileRegistryRecord* record =
      FindRegistryRecord(selected_profile_id_);
  if (!record) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileNotFound,
        "Selected profile was not found.");
  }
  if (record->lifecycle == maho::ProfileLifecycleState::kDeleting) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileDeleting,
        "Selected profile is being deleted.");
  }
  if (record->lifecycle != maho::ProfileLifecycleState::kReady ||
      record->revision != selected_profile_revision_) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Selected profile is unavailable.");
  }
  if (expected_profile_revision &&
      *expected_profile_revision != selected_profile_revision_) {
    return MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision,
        "Selected profile revision is stale.");
  }
  return nullptr;
}

namespace {

std::vector<maho_settings::mojom::SearchEngineInfoPtr>
BuildSearchEnginesForProfile(Profile* profile) {
  std::vector<maho_settings::mojom::SearchEngineInfoPtr> engines;
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile);
  if (!service) {
    return engines;
  }
  const TemplateURL* default_engine = service->GetDefaultSearchProvider();
  const std::u16string default_keyword =
      default_engine ? default_engine->keyword() : std::u16string();
  for (const TemplateURL* engine : service->GetTemplateURLs()) {
    if (!engine->prepopulate_id()) {
      continue;
    }
    const std::string name = base::UTF16ToUTF8(engine->short_name());
    const std::string name_lower = base::ToLowerASCII(name);
    if (name_lower.find("wikipedia") != std::string::npos ||
        name_lower.find("ebay") != std::string::npos) {
      continue;
    }
    auto info = maho_settings::mojom::SearchEngineInfo::New();
    info->keyword = base::UTF16ToUTF8(engine->keyword());
    info->name = name;
    const GURL& favicon_url = engine->favicon_url();
    if (favicon_url.is_valid()) {
      info->icon_url =
          "chrome://favicon2/?size=32&scaleFactor=1x&iconUrl=" +
          base::EscapeQueryParamValue(favicon_url.spec(), false);
    }
    info->is_default = engine->keyword() == default_keyword;
    engines.push_back(std::move(info));
  }
  return engines;
}

}  // namespace

void MahoSettingsPageHandler::GetSelectedProfileMetadata(
    maho_settings::mojom::ProfileTargetPtr target,
    GetSelectedProfileMetadataCallback callback) {
  auto result = maho_settings::mojom::ProfileMetadataResult::New();
  if (auto error = ValidateSelectedProfileTarget(target, std::nullopt,
                                                  std::nullopt)) {
    result->error = std::move(error);
  } else if (const maho::ProfileRegistryRecord* record =
                 FindRegistryRecord(selected_profile_id_)) {
    result->metadata = maho_settings::mojom::ProfileMetadata::New();
    result->metadata->name = record->name;
    result->metadata->avatar_color = record->avatar_color;
    result->context = BuildSelectedProfileContext();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::UpdateSelectedProfileMetadata(
    maho_settings::mojom::ProfileTargetPtr target,
    uint64_t expected_context_revision,
    maho_settings::mojom::ProfileMetadataUpdatePtr update,
    UpdateSelectedProfileMetadataCallback callback) {
  auto result = maho_settings::mojom::ProfileMetadataResult::New();
  if (!update) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kInvalidArgument,
        "Profile metadata update is missing.");
  } else if (auto error = ValidateSelectedProfileTarget(
                 target, expected_context_revision,
                 update->expected_profile_revision)) {
    result->error = std::move(error);
  } else {
    maho::PostCoreTask(
        FROM_HERE,
        base::BindOnce(&UpdateProfileMetadataOnCoreSequence,
                       selected_profile_id_, update->name,
                       update->avatar_color),
        base::BindOnce(&MahoSettingsPageHandler::OnProfileMetadataUpdated,
                       weak_factory_.GetWeakPtr(), selected_profile_id_,
                       selected_profile_context_revision_,
                       selected_profile_revision_, std::move(callback)));
    return;
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::OnProfileMetadataUpdated(
    std::string profile_id,
    uint64_t context_revision,
    uint64_t profile_revision,
    UpdateSelectedProfileMetadataCallback callback,
    std::string json_result) {
  auto result = maho_settings::mojom::ProfileMetadataResult::New();
  if (selected_profile_id_ != profile_id ||
      selected_profile_context_revision_ != context_revision) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile context changed during the update.");
  } else if (selected_profile_revision_ != profile_revision) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision,
        "Selected profile changed during the update.");
  } else {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json_result, base::JSON_PARSE_RFC);
    const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;
    const base::DictValue* profile = dict ? dict->FindDict("profile") : nullptr;
    const std::string* name = profile ? profile->FindString("name") : nullptr;
    const std::string* avatar_color =
        profile ? profile->FindString("avatarColor") : nullptr;
    if (dict && dict->FindBool("ok").value_or(false) && name &&
        avatar_color) {
      maho::MahoSpaceProfileBridge* bridge =
          maho::MahoSpaceProfileBridge::GetInstance();
      if (!bridge->UpdateProfileSettings(profile_id, name, avatar_color,
                                         nullptr)) {
        result->error = MakeProfileTargetError(
            maho_settings::mojom::ProfileTargetErrorCode::kInternal,
            "Profile registry update failed.");
      } else if (const maho::ProfileRegistryRecord* record =
                     FindRegistryRecord(profile_id)) {
        selected_profile_revision_ = record->revision;
        result->metadata = maho_settings::mojom::ProfileMetadata::New();
        result->metadata->name = record->name;
        result->metadata->avatar_color = record->avatar_color;
        result->context = BuildSelectedProfileContext();
      }
    } else {
      const base::DictValue* error = dict ? dict->FindDict("error") : nullptr;
      const std::string* code = error ? error->FindString("code") : nullptr;
      const std::string* message = error ? error->FindString("message") : nullptr;
      const auto mojo_code = code
                                 ? MapProfileUpdateErrorForTesting(*code)
                                 : maho_settings::mojom::ProfileTargetErrorCode::kInternal;
      result->error = MakeProfileTargetError(
          mojo_code, message ? *message : "Profile metadata update failed.");
    }
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::GetSelectedProfileSearchSettings(
    maho_settings::mojom::ProfileTargetPtr target,
    GetSelectedProfileSearchSettingsCallback callback) {
  auto result = maho_settings::mojom::ProfileSearchSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(target, std::nullopt,
                                                  std::nullopt)) {
    result->error = std::move(error);
  } else if (!TemplateURLServiceFactory::GetForProfile(selected_profile_)) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Search service is unavailable for the selected profile.");
  } else {
    result->search = maho_settings::mojom::ProfileSearchSettings::New();
    result->search->engines = BuildSearchEnginesForProfile(selected_profile_);
    result->search->suggestions_enabled =
        selected_profile_->GetPrefs()->GetBoolean(prefs::kSearchSuggestEnabled);
    result->context = BuildSelectedProfileContext();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::SetSelectedProfileDefaultSearchEngine(
    maho_settings::mojom::ProfileTargetPtr target,
    uint64_t expected_context_revision,
    const std::string& keyword,
    uint64_t expected_profile_revision,
    SetSelectedProfileDefaultSearchEngineCallback callback) {
  auto result = maho_settings::mojom::ProfileSearchSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(
          target, expected_context_revision, expected_profile_revision)) {
    result->error = std::move(error);
  } else if (TemplateURLService* service =
                 TemplateURLServiceFactory::GetForProfile(selected_profile_)) {
    TemplateURL* match = nullptr;
    for (TemplateURL* engine : service->GetTemplateURLs()) {
      if (base::UTF16ToUTF8(engine->keyword()) == keyword) {
        match = engine;
        break;
      }
    }
    if (!match) {
      result->error = MakeProfileTargetError(
          maho_settings::mojom::ProfileTargetErrorCode::kInvalidArgument,
          "Search engine was not found.");
    } else {
      service->SetUserSelectedDefaultSearchProvider(match);
      result->search = maho_settings::mojom::ProfileSearchSettings::New();
      result->search->engines = BuildSearchEnginesForProfile(selected_profile_);
      result->search->suggestions_enabled = selected_profile_->GetPrefs()->GetBoolean(
          prefs::kSearchSuggestEnabled);
      result->context = BuildSelectedProfileContext();
    }
  } else {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Search service is unavailable for the selected profile.");
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::SetSelectedProfileSearchSuggestionsEnabled(
    maho_settings::mojom::ProfileTargetPtr target,
    uint64_t expected_context_revision,
    bool enabled,
    uint64_t expected_profile_revision,
    SetSelectedProfileSearchSuggestionsEnabledCallback callback) {
  auto result = maho_settings::mojom::ProfileSearchSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(
          target, expected_context_revision, expected_profile_revision)) {
    result->error = std::move(error);
  } else if (!TemplateURLServiceFactory::GetForProfile(selected_profile_)) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "Search service is unavailable for the selected profile.");
  } else {
    selected_profile_->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled,
                                              enabled);
    result->search = maho_settings::mojom::ProfileSearchSettings::New();
    result->search->engines = BuildSearchEnginesForProfile(selected_profile_);
    result->search->suggestions_enabled = enabled;
    result->context = BuildSelectedProfileContext();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::GetSelectedProfileDownloadSettings(
    maho_settings::mojom::ProfileTargetPtr target,
    GetSelectedProfileDownloadSettingsCallback callback) {
  auto result = maho_settings::mojom::ProfileDownloadSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(target, std::nullopt,
                                                  std::nullopt)) {
    result->error = std::move(error);
  } else {
    PrefService* target_prefs = selected_profile_->GetPrefs();
    result->download = maho_settings::mojom::ProfileDownloadSettings::New();
    result->download->directory_display_path =
        base::UTF16ToUTF8(
            target_prefs->GetFilePath(prefs::kDownloadDefaultDirectory)
                .LossyDisplayName());
    result->download->prompt_for_download =
        target_prefs->GetBoolean(prefs::kPromptForDownload);
    result->context = BuildSelectedProfileContext();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::SetSelectedProfileDownloadPrompt(
    maho_settings::mojom::ProfileTargetPtr target,
    uint64_t expected_context_revision,
    bool prompt_for_download,
    uint64_t expected_profile_revision,
    SetSelectedProfileDownloadPromptCallback callback) {
  auto result = maho_settings::mojom::ProfileDownloadSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(
          target, expected_context_revision, expected_profile_revision)) {
    result->error = std::move(error);
  } else {
    PrefService* target_prefs = selected_profile_->GetPrefs();
    target_prefs->SetBoolean(prefs::kPromptForDownload, prompt_for_download);
    result->download = maho_settings::mojom::ProfileDownloadSettings::New();
    result->download->directory_display_path =
        base::UTF16ToUTF8(
            target_prefs->GetFilePath(prefs::kDownloadDefaultDirectory)
                .LossyDisplayName());
    result->download->prompt_for_download = prompt_for_download;
    result->context = BuildSelectedProfileContext();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::SelectSelectedProfileDownloadDirectory(
    maho_settings::mojom::ProfileTargetPtr target,
    uint64_t expected_context_revision,
    uint64_t expected_profile_revision,
    SelectSelectedProfileDownloadDirectoryCallback callback) {
  auto result = maho_settings::mojom::ProfileDownloadSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(
          target, expected_context_revision, expected_profile_revision)) {
    result->error = std::move(error);
    std::move(callback).Run(std::move(result));
    return;
  }
  if (download_directory_select_callback_) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kProfileUnavailable,
        "A download directory chooser is already open.");
    std::move(callback).Run(std::move(result));
    return;
  }
  download_directory_profile_id_ = selected_profile_id_;
  download_directory_target_token_ = selected_profile_target_token_;
  download_directory_context_revision_ = selected_profile_context_revision_;
  download_directory_profile_revision_ = selected_profile_revision_;
  download_directory_select_callback_ = std::move(callback);
  download_directory_select_file_dialog_ =
      ui::SelectFileDialog::Create(this, nullptr);
  download_directory_select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_FOLDER, u"Choose download directory",
      selected_profile_->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory),
      nullptr, 0, base::FilePath::StringType(), GetSettingsNativeWindow(profile_),
      nullptr);
}

void MahoSettingsPageHandler::GetSelectedProfileArchiveSettings(
    maho_settings::mojom::ProfileTargetPtr target,
    GetSelectedProfileArchiveSettingsCallback callback) {
  auto result = maho_settings::mojom::ProfileArchiveSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(target, std::nullopt,
                                                  std::nullopt)) {
    result->error = std::move(error);
    std::move(callback).Run(std::move(result));
    return;
  }
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&GetProfileArchiveTimeoutOnCoreSequence,
                     selected_profile_id_),
      base::BindOnce(&MahoSettingsPageHandler::OnProfileArchiveSettingsLoaded,
                     weak_factory_.GetWeakPtr(), selected_profile_id_,
                     selected_profile_context_revision_,
                     selected_profile_revision_, std::move(callback)));
}

void MahoSettingsPageHandler::OnProfileArchiveSettingsLoaded(
    std::string profile_id,
    uint64_t context_revision,
    uint64_t profile_revision,
    GetSelectedProfileArchiveSettingsCallback callback,
    std::string json_result) {
  auto result = maho_settings::mojom::ProfileArchiveSettingsResult::New();
  if (selected_profile_id_ != profile_id ||
      selected_profile_context_revision_ != context_revision) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile context changed during the read.");
  } else if (selected_profile_revision_ != profile_revision) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision,
        "Selected profile changed during the read.");
  } else {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json_result, base::JSON_PARSE_RFC);
    const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;
    std::optional<double> timeout =
        dict ? dict->FindDouble("timeout_hours") : std::nullopt;
    if (dict && dict->FindBool("ok").value_or(false) && timeout) {
      result->archive = maho_settings::mojom::ProfileArchiveSettings::New();
      result->archive->timeout_hours = static_cast<int32_t>(*timeout);
      result->context = BuildSelectedProfileContext();
    } else {
      const base::DictValue* error = dict ? dict->FindDict("error") : nullptr;
      const std::string* code = error ? error->FindString("code") : nullptr;
      const std::string* message = error ? error->FindString("message") : nullptr;
      result->error = MakeProfileTargetError(
          code ? MapProfileUpdateErrorForTesting(*code)
               : maho_settings::mojom::ProfileTargetErrorCode::kInternal,
          message ? *message : "Profile archive settings read failed.");
    }
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::SetSelectedProfileArchiveTimeout(
    maho_settings::mojom::ProfileTargetPtr target,
    uint64_t expected_context_revision,
    int32_t timeout_hours,
    uint64_t expected_profile_revision,
    SetSelectedProfileArchiveTimeoutCallback callback) {
  auto result = maho_settings::mojom::ProfileArchiveSettingsResult::New();
  if (auto error = ValidateSelectedProfileTarget(
          target, expected_context_revision, expected_profile_revision)) {
    result->error = std::move(error);
  } else {
    maho::PostCoreTask(
        FROM_HERE,
        base::BindOnce(&SetProfileArchiveTimeoutOnCoreSequence,
                       selected_profile_id_, timeout_hours),
        base::BindOnce(&MahoSettingsPageHandler::OnProfileArchiveTimeoutUpdated,
                       weak_factory_.GetWeakPtr(), selected_profile_id_,
                       selected_profile_context_revision_,
                       selected_profile_revision_, std::move(callback)));
    return;
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::OnProfileArchiveTimeoutUpdated(
    std::string profile_id,
    uint64_t context_revision,
    uint64_t profile_revision,
    SetSelectedProfileArchiveTimeoutCallback callback,
    std::string json_result) {
  auto result = maho_settings::mojom::ProfileArchiveSettingsResult::New();
  if (selected_profile_id_ != profile_id ||
      selected_profile_context_revision_ != context_revision) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleContext,
        "Selected profile context changed during the update.");
  } else if (selected_profile_revision_ != profile_revision) {
    result->error = MakeProfileTargetError(
        maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision,
        "Selected profile changed during the update.");
  } else {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json_result, base::JSON_PARSE_RFC);
    const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;
    const base::DictValue* profile = dict ? dict->FindDict("profile") : nullptr;
    const base::Value* archive_timeout =
        profile ? profile->Find("archiveTimeoutHours") : nullptr;
    if (dict && dict->FindBool("ok").value_or(false) && profile &&
        archive_timeout) {
      std::optional<int32_t> timeout_hours;
      if (!archive_timeout->is_none()) {
        std::optional<double> timeout = archive_timeout->GetIfDouble();
        if (!timeout) {
          result->error = MakeProfileTargetError(
              maho_settings::mojom::ProfileTargetErrorCode::kInternal,
              "Profile archive response was invalid.");
          std::move(callback).Run(std::move(result));
          return;
        }
        timeout_hours = static_cast<int32_t>(*timeout);
      }
      maho::MahoSpaceProfileBridge* bridge =
          maho::MahoSpaceProfileBridge::GetInstance();
      if (!bridge->UpdateProfileSettings(profile_id, nullptr, nullptr,
                                         &timeout_hours)) {
        result->error = MakeProfileTargetError(
            maho_settings::mojom::ProfileTargetErrorCode::kInternal,
            "Profile registry update failed.");
      } else if (const maho::ProfileRegistryRecord* record =
                     FindRegistryRecord(profile_id)) {
        selected_profile_revision_ = record->revision;
        result->archive = maho_settings::mojom::ProfileArchiveSettings::New();
        result->archive->timeout_hours =
            record->archive_timeout_hours.value_or(0);
        result->context = BuildSelectedProfileContext();
      }
    } else {
      const base::DictValue* error = dict ? dict->FindDict("error") : nullptr;
      const std::string* code = error ? error->FindString("code") : nullptr;
      const std::string* message = error ? error->FindString("message") : nullptr;
      const auto mojo_code = code
                                 ? MapProfileUpdateErrorForTesting(*code)
                                 : maho_settings::mojom::ProfileTargetErrorCode::kInternal;
      result->error = MakeProfileTargetError(
          mojo_code, message ? *message : "Profile archive update failed.");
    }
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::OnSelectedProfileDownloadDirectoryChosen(
    const base::FilePath& directory) {
  auto result = maho_settings::mojom::ProfileDownloadSettingsResult::New();
  auto target = maho_settings::mojom::ProfileTarget::New();
  target->profile_id = download_directory_profile_id_;
  target->target_token = download_directory_target_token_;
  if (auto error = ValidateSelectedProfileTarget(
          target, download_directory_context_revision_,
          download_directory_profile_revision_)) {
    result->error = std::move(error);
  } else {
    PrefService* target_prefs = selected_profile_->GetPrefs();
    if (!directory.empty()) {
      target_prefs->SetFilePath(prefs::kDownloadDefaultDirectory, directory);
    }
    result->download = maho_settings::mojom::ProfileDownloadSettings::New();
    result->download->directory_display_path =
        target_prefs->GetFilePath(prefs::kDownloadDefaultDirectory)
            .AsUTF8Unsafe();
    result->download->prompt_for_download =
        target_prefs->GetBoolean(prefs::kPromptForDownload);
    result->context = BuildSelectedProfileContext();
  }
  download_directory_select_file_dialog_.reset();
  download_directory_profile_id_.clear();
  download_directory_target_token_.clear();
  download_directory_context_revision_ = 0;
  download_directory_profile_revision_ = 0;
  std::move(download_directory_select_callback_).Run(std::move(result));
}

void MahoSettingsPageHandler::OnPrefChanged(const std::string& pref_name) {
  if (pref_name == ai::kMailReadAllowed) {
    MahoUnifiedAgentAdapter::NotifyMailReadConsentChanged(prefs_);
  }
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
  add(kOpenExternalLinksInMahoMiniMahoKey);
  add(kPasswordProviderModeMahoKey);
  add(kPasswordsEnabledMahoKey);
  add(kVaultAutoLockMinutesMahoKey);
  add(kVaultDeviceAuthRequiredMahoKey);
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
  } else if (key == "sidebar.new_tab_position") {
    // maho-core decides where a new tab lands (event_dispatcher CreateTab), so
    // this is core-global like the other tab-lifecycle settings. Reject values
    // outside the contract instead of letting core silently fall back to top.
    if (value != "top" && value != "bottom") {
      return FfiResult::kInvalid;
    }
    base::DictValue settings;
    base::DictValue general;
    general.Set("newTabPosition", value);
    settings.Set("general", std::move(general));
    std::string json;
    base::JSONWriter::Write(base::Value(std::move(settings)), &json);
    maho_core_update_settings(core, json.c_str());
  } else if (key == kConversationAutoArchiveAfterDaysMahoKey) {
    int days = 0;
    if (!base::StringToInt(value, &days) ||
        (days != -1 && days != 3 && days != 7 && days != 30)) {
      return FfiResult::kInvalid;
    }
    if (!maho_core_set_conversation_auto_archive_policy(core, days)) {
      return FfiResult::kInvalid;
    }
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

  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kFooter;
  maho::InvalidateSidebarCoreCache(invalidation);
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

  const int32_t policy =
      maho_core_get_conversation_auto_archive_policy(core);
  add("conversation.auto_archive_after_days",
      base::NumberToString(policy < 0 ? -1 : policy));

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
    if (const std::string* new_tab_position =
            general->FindString("newTabPosition")) {
      add("sidebar.new_tab_position", *new_tab_position);
    }
  }
}

// ── Profiles domain ──

void MahoSettingsPageHandler::GetProfiles(GetProfilesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<maho_settings::mojom::ProfileInfoPtr> profiles;
  const maho::ProfileCatalogResult& catalog =
      maho::MahoSpaceProfileBridge::GetInstance()->GetProfileCatalog();
  if (catalog.error != maho::ProfileCatalogError::kNone) {
    std::move(callback).Run(std::move(profiles));
    return;
  }
  profiles.reserve(catalog.records.size());
  for (const maho::ProfileRegistryRecord& record : catalog.records) {
    auto profile = maho_settings::mojom::ProfileInfo::New();
    profile->id = record.maho_id;
    profile->name = record.name;
    profile->is_default = record.is_default;
    profile->is_active = record.is_active;
    profile->space_ids = record.space_ids;
    profiles.push_back(std::move(profile));
  }
  std::move(callback).Run(std::move(profiles));
}

void MahoSettingsPageHandler::GetProfileObservablesSnapshot(
    GetProfileObservablesSnapshotCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StartProfileObservablesSnapshotRead(std::move(callback), 0);
}

namespace maho_settings::testing {

ProfileObservablesDecision::ProfileObservablesDecision(
    ProfileObservablesDecisionKind kind,
    maho_settings::mojom::ProfileObservablesSnapshotPtr snapshot)
    : kind(kind), snapshot(std::move(snapshot)) {}
ProfileObservablesDecision::ProfileObservablesDecision(
    ProfileObservablesDecision&&) = default;
ProfileObservablesDecision& ProfileObservablesDecision::operator=(
    ProfileObservablesDecision&&) = default;
ProfileObservablesDecision::~ProfileObservablesDecision() = default;

}  // namespace maho_settings::testing

MahoSettingsPageHandler::SelectedProfileRequestState::
    SelectedProfileRequestState() = default;
MahoSettingsPageHandler::SelectedProfileRequestState::
    SelectedProfileRequestState(SelectedProfileRequestState&&) = default;
MahoSettingsPageHandler::SelectedProfileRequestState&
MahoSettingsPageHandler::SelectedProfileRequestState::operator=(
    SelectedProfileRequestState&&) = default;
MahoSettingsPageHandler::SelectedProfileRequestState::
    ~SelectedProfileRequestState() = default;

namespace maho_settings::testing {

ProfileObservablesDecision DecideProfileObservablesSnapshotForTesting(
    const maho::ProfileCatalogResult& captured_catalog,
    uint64_t current_registry_revision,
    const base::FilePath& handler_profile_basename,
    const std::optional<std::string>& active_profile_id_json,
    uint32_t attempt) {
  if (current_registry_revision != captured_catalog.revision) {
    return {attempt == 0 ? ProfileObservablesDecisionKind::kRetry
                         : ProfileObservablesDecisionKind::kNull,
            nullptr};
  }

  if (captured_catalog.error != maho::ProfileCatalogError::kNone) {
    return {ProfileObservablesDecisionKind::kNull, nullptr};
  }

  std::set<std::string> profile_ids;
  const maho::ProfileRegistryRecord* browser_profile_record = nullptr;
  const maho::ProfileRegistryRecord* active_profile_record = nullptr;
  size_t browser_profile_match_count = 0;
  size_t active_profile_count = 0;
  for (const maho::ProfileRegistryRecord& record : captured_catalog.records) {
    if (record.maho_id.empty() || !profile_ids.insert(record.maho_id).second) {
      return {ProfileObservablesDecisionKind::kNull, nullptr};
    }
    if (record.chromium_basename == handler_profile_basename) {
      browser_profile_record = &record;
      ++browser_profile_match_count;
    }
    if (record.is_active) {
      active_profile_record = &record;
      ++active_profile_count;
    }
  }

  if (browser_profile_match_count > 1u || active_profile_count != 1u ||
      !active_profile_record) {
    return {ProfileObservablesDecisionKind::kNull, nullptr};
  }

  std::optional<std::string> canonical_active_profile_id;
  if (active_profile_id_json) {
    std::optional<base::Value> decoded = base::JSONReader::Read(
        *active_profile_id_json, base::JSON_PARSE_RFC);
    if (!decoded || !decoded->is_string() || decoded->GetString().empty()) {
      return {ProfileObservablesDecisionKind::kNull, nullptr};
    }
    canonical_active_profile_id = maho::CanonicalizeProfileRegistryId(
        captured_catalog, decoded->GetString());
    if (!canonical_active_profile_id ||
        *canonical_active_profile_id != active_profile_record->maho_id) {
      return {ProfileObservablesDecisionKind::kNull, nullptr};
    }
  }

  auto snapshot = maho_settings::mojom::ProfileObservablesSnapshot::New();
  if (browser_profile_record) {
    snapshot->active_browser_profile_id = browser_profile_record->maho_id;
  }
  if (canonical_active_profile_id) {
    snapshot->active_maho_profile_id = *canonical_active_profile_id;
  }
  snapshot->registry_revision = captured_catalog.revision;
  snapshot->lifecycle_entries.reserve(captured_catalog.records.size());
  for (const maho::ProfileRegistryRecord& record : captured_catalog.records) {
    auto entry = maho_settings::mojom::ProfileObservableLifecycleEntry::New();
    entry->profile_id = record.maho_id;
    switch (record.lifecycle) {
      case maho::ProfileLifecycleState::kProvisioning:
        entry->lifecycle_state = maho_settings::mojom::
            ProfileObservableLifecycleState::kProvisioning;
        break;
      case maho::ProfileLifecycleState::kReady:
        entry->lifecycle_state =
            maho_settings::mojom::ProfileObservableLifecycleState::kReady;
        break;
      case maho::ProfileLifecycleState::kDeleting:
        entry->lifecycle_state = maho_settings::mojom::
            ProfileObservableLifecycleState::kDeleting;
        snapshot->pending_deletion_profile_ids.push_back(record.maho_id);
        break;
      case maho::ProfileLifecycleState::kRepairRequired:
        entry->lifecycle_state = maho_settings::mojom::
            ProfileObservableLifecycleState::kRepairRequired;
        break;
    }
    snapshot->lifecycle_entries.push_back(std::move(entry));
  }
  snapshot->deleted_history_availability =
      maho_settings::mojom::DeletedProfileHistoryAvailability::kUnavailable;
  return {ProfileObservablesDecisionKind::kSnapshot, std::move(snapshot)};
}

}  // namespace maho_settings::testing

void MahoSettingsPageHandler::StartProfileObservablesSnapshotRead(
    GetProfileObservablesSnapshotCallback callback,
    uint32_t attempt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  const maho::ProfileCatalogResult& current = bridge->GetProfileCatalog();
  if (current.error != maho::ProfileCatalogError::kNone) {
    std::move(callback).Run(nullptr);
    return;
  }
  maho::ProfileCatalogResult captured_catalog = current;
  const base::FilePath handler_profile_basename =
      profile_->GetPath().BaseName();
  maho::PostCoreTask<std::optional<std::string>>(
      FROM_HERE,
      base::BindOnce(&ReadActiveMahoProfileIdJsonOnCoreSequence),
      base::BindOnce(&MahoSettingsPageHandler::OnActiveMahoProfileIdRead,
                     weak_factory_.GetWeakPtr(), std::move(callback), attempt,
                     std::move(captured_catalog), handler_profile_basename));
}

void MahoSettingsPageHandler::OnActiveMahoProfileIdRead(
    GetProfileObservablesSnapshotCallback callback,
    uint32_t attempt,
    maho::ProfileCatalogResult captured_catalog,
    base::FilePath handler_profile_basename,
    std::optional<std::string> active_profile_id_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  maho_settings::testing::ProfileObservablesDecision decision =
      maho_settings::testing::DecideProfileObservablesSnapshotForTesting(
          captured_catalog, bridge->GetProfileCatalog().revision,
          handler_profile_basename, active_profile_id_json, attempt);
  switch (decision.kind) {
    case maho_settings::testing::ProfileObservablesDecisionKind::kRetry:
      if (attempt == 0) {
        StartProfileObservablesSnapshotRead(std::move(callback), 1);
      } else {
        std::move(callback).Run(nullptr);
      }
      return;
    case maho_settings::testing::ProfileObservablesDecisionKind::kNull:
      std::move(callback).Run(nullptr);
      return;
    case maho_settings::testing::ProfileObservablesDecisionKind::kSnapshot:
      std::move(callback).Run(std::move(decision.snapshot));
      return;
  }
}

void MahoSettingsPageHandler::CreateProfile(const std::string& name,
                                            CreateProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (base::TrimWhitespaceASCII(name, base::TRIM_ALL).empty()) {
    std::move(callback).Run(nullptr);
    return;
  }
  maho::PostCoreTask(
      FROM_HERE, base::BindOnce(&CreateProfileOnCoreSequence, name),
      base::BindOnce(&MahoSettingsPageHandler::OnProfileCreatedInCore,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSettingsPageHandler::OnProfileCreatedInCore(
    CreateProfileCallback callback,
    maho_settings::mojom::ProfileInfoPtr profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ProfileManager* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!profile || !profile_manager) {
    std::move(callback).Run(nullptr);
    return;
  }
  const std::optional<base::FilePath> basename =
      maho::MahoSpaceProfileBridge::ProfileBasenameForId(profile->id);
  if (!basename) {
    maho::PostCoreTask(
        FROM_HERE,
        base::BindOnce(&DeleteProfileOnCoreSequence, profile->id),
        base::BindOnce([](CreateProfileCallback cb, std::string) {
          std::move(cb).Run(nullptr);
        }, std::move(callback)));
    return;
  }
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  bridge->ReconcileProfileRegistryFromCore();
  bridge->SetProfileLifecycleState(
      profile->id, maho::ProfileLifecycleState::kProvisioning);
  profile_manager->CreateProfileAsync(
      profile_manager->user_data_dir().Append(*basename),
      base::BindOnce(&MahoSettingsPageHandler::OnChromiumProfileCreated,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(profile)));
}

void MahoSettingsPageHandler::OnChromiumProfileCreated(
    CreateProfileCallback callback,
    maho_settings::mojom::ProfileInfoPtr profile,
    Profile* chromium_profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  if (!chromium_profile || !bridge->ReconcileProfileRegistryFromCore()) {
    const std::string profile_id = profile->id;
    maho::PostCoreTask(
        FROM_HERE, base::BindOnce(&DeleteProfileOnCoreSequence, profile_id),
        base::BindOnce(
            [](CreateProfileCallback cb, std::string id,
               std::string rollback_result) {
              maho::MahoSpaceProfileBridge* bridge =
                  maho::MahoSpaceProfileBridge::GetInstance();
              if (!bridge->ReconcileProfileRegistryFromCore()) {
                bridge->SetProfileLifecycleState(
                    id, maho::ProfileLifecycleState::kRepairRequired);
              }
              std::move(cb).Run(nullptr);
            },
            std::move(callback), profile_id));
    return;
  }
  bridge->SetProfileLifecycleState(profile->id,
                                   maho::ProfileLifecycleState::kReady);
  if (const maho::ProfileRegistryRecord* record =
          FindRegistryRecord(profile->id)) {
    profile->name = record->name;
    profile->is_default = record->is_default;
    profile->is_active = record->is_active;
    profile->space_ids = record->space_ids;
  }
  std::move(callback).Run(std::move(profile));
}

void MahoSettingsPageHandler::DeleteProfile(const std::string& profile_id,
                                            DeleteProfileCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  const std::optional<std::string> canonical_id =
      bridge->CanonicalizeProfileId(profile_id);
  const maho::ProfileRegistryRecord* record =
      canonical_id ? FindRegistryRecord(*canonical_id) : nullptr;
  if (!record || record->is_default || !record->space_ids.empty() ||
      record->lifecycle != maho::ProfileLifecycleState::kReady ||
      record->chromium_basename == profile_->GetPath().BaseName()) {
    std::move(callback).Run(false);
    return;
  }
  size_t ready_count = 0;
  for (const maho::ProfileRegistryRecord& candidate :
       bridge->GetProfileCatalog().records) {
    ready_count +=
        candidate.lifecycle == maho::ProfileLifecycleState::kReady ? 1u : 0u;
  }
  if (ready_count <= 1u) {
    std::move(callback).Run(false);
    return;
  }
  ProfileManager* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!profile_manager || !profiles::IsMultipleProfilesEnabled()) {
    std::move(callback).Run(false);
    return;
  }
  const base::FilePath path =
      profile_manager->user_data_dir().Append(record->chromium_basename);
  if (!profile_manager->IsAllowedProfilePath(path) ||
      IsProfileDirectoryMarkedForDeletion(path)) {
    std::move(callback).Run(false);
    return;
  }
  ProfileDeletionCoordinator* deletion_coordinator =
      ProfileDeletionCoordinator::Get();
  if (deletion_coordinator->HasPendingDeletion()) {
    std::move(callback).Run(false);
    return;
  }
  if (selected_profile_id_ == *canonical_id) {
    InvalidateSelectedProfileContext();
  }
  deletion_coordinator->Delete(profile_manager, *canonical_id, path,
                               std::move(callback));
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
  settings->mail_read_allowed = prefs_->GetBoolean(ai::kMailReadAllowed);
  settings->has_byok_openai =
      !prefs_->GetString(ai::kByokOpenAIEncryptedB64).empty();
  settings->has_byok_anthropic =
      !prefs_->GetString(ai::kByokAnthropicEncryptedB64).empty();
  settings->has_oauth_openai =
      maho::ai_oauth::HasOAuthSession(prefs_, "openai");
  settings->has_oauth_anthropic =
      maho::ai_oauth::HasOAuthSession(prefs_, "anthropic");
  settings->oauth_client_id_openai =
      maho::ai_oauth::GetOAuthClientId(prefs_, "openai");
  settings->oauth_client_id_anthropic =
      maho::ai_oauth::GetOAuthClientId(prefs_, "anthropic");

  settings->has_relay_session = maho::auth::HasValidRelaySession(prefs_);
  settings->relay_tier = prefs_->GetString(maho::account_prefs::kRelayUserTier);
  settings->relay_subscription_status =
      prefs_->GetString(maho::account_prefs::kRelaySubscriptionStatus);
  settings->credit_balance_usd = last_known_credit_balance_;
  settings->tier_ceiling_usd = last_known_tier_ceiling_;

  // Server-side coercion (single source of truth; client is read-only).
  if (settings->has_relay_session && settings->provider.empty()) {
    prefs_->SetString(ai::kProvider, "maho-managed");
    prefs_->CommitPendingWrite();
    settings->provider = "maho-managed";
  }

  std::move(callback).Run(std::move(settings));
}

namespace {

bool IsValidModelForProvider(const std::string& provider_id,
                             const std::string& model_id) {
  if (model_id.empty() || model_id == "default") {
    return false;
  }
  if (model_id.find_first_of(" \t\r\n") != std::string::npos) {
    return false;
  }
  std::vector<std::string> known =
      maho::ai::MahoModelListFetcher::GetHardcodedFallback(provider_id);
  if (!known.empty()) {
    for (const auto& k : known) {
      if (k == model_id) {
        return true;
      }
    }
    if (provider_id == "openai") {
      if (base::StartsWith(model_id, "gpt-", base::CompareCase::INSENSITIVE_ASCII) ||
          base::StartsWith(model_id, "o1", base::CompareCase::INSENSITIVE_ASCII) ||
          base::StartsWith(model_id, "o3", base::CompareCase::INSENSITIVE_ASCII)) {
        return true;
      }
    } else if (provider_id == "anthropic") {
      if (base::StartsWith(model_id, "claude-", base::CompareCase::INSENSITIVE_ASCII)) {
        return true;
      }
    } else if (provider_id == "maho-managed") {
      return false;
    }
    return false;
  }
  return true;
}

}  // namespace

void MahoSettingsPageHandler::GetAIModelsSettings(
    GetAIModelsSettingsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildAIModelsSettings());
}

maho_settings::mojom::AIModelsSettingsPtr
MahoSettingsPageHandler::BuildAIModelsSettings() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto settings = maho_settings::mojom::AIModelsSettings::New();
  settings->has_relay_session = maho::auth::HasValidRelaySession(prefs_);

  auto default_route_res =
      maho::ai::ResolveMahoAiModelRoute(prefs_, maho::ai::MahoAiTask::kChat);
  std::string default_provider =
      default_route_res.is_ok() ? default_route_res.route.provider_id : "";
  std::string default_model =
      default_route_res.is_ok() ? default_route_res.route.model_id : "";

  settings->default_provider_id = default_provider;
  settings->default_model_id = default_model;

  const std::vector<std::string> provider_ids = {
      "maho-managed", "openai", "anthropic", "openai-compatible", "local-server"};

  const base::DictValue& provider_configs =
      prefs_->GetDict(maho::ai_prefs::kProviderConfigs);

  for (const auto& pid : provider_ids) {
    auto desc = maho_settings::mojom::AIProviderDescriptor::New();
    desc->id = pid;
    desc->is_default_provider = (pid == default_provider);
    desc->models_loading = false;

    const base::DictValue* cfg = provider_configs.FindDict(pid);
    if (cfg) {
      if (const std::string* m = cfg->FindString("last_model")) {
        desc->last_model_id = *m;
      } else if (const std::string* m_id = cfg->FindString("last_model_id")) {
        desc->last_model_id = *m_id;
      }
      if (const std::string* u = cfg->FindString("base_url")) {
        desc->base_url = *u;
      }
    }

    if (pid == "maho-managed") {
      desc->label = "Maho Managed";
      desc->auth_label = "Maho account";
      desc->state = settings->has_relay_session
                        ? maho_settings::mojom::AIConnectionState::kConnected
                        : maho_settings::mojom::AIConnectionState::kDisconnected;
    } else if (pid == "openai") {
      desc->label = "OpenAI";
      bool has_oauth = maho::ai_oauth::HasOAuthSession(prefs_, "openai");
      bool has_byok =
          !prefs_->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64).empty();
      desc->auth_label = has_oauth ? "OAuth" : "API key";
      desc->state = (has_oauth || has_byok)
                        ? maho_settings::mojom::AIConnectionState::kConnected
                        : maho_settings::mojom::AIConnectionState::kDisconnected;
    } else if (pid == "anthropic") {
      desc->label = "Anthropic";
      bool has_oauth = maho::ai_oauth::HasOAuthSession(prefs_, "anthropic");
      bool has_byok =
          !prefs_->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64).empty();
      desc->auth_label = has_oauth ? "OAuth" : "API key";
      desc->state = (has_oauth || has_byok)
                        ? maho_settings::mojom::AIConnectionState::kConnected
                        : maho_settings::mojom::AIConnectionState::kDisconnected;
    } else if (pid == "openai-compatible") {
      desc->label = "OpenAI-compatible";
      desc->auth_label = "Custom API";
      std::string base_url = desc->base_url.value_or("");
      if (base_url.empty()) {
        base_url = prefs_->GetString(maho::ai_prefs::kBaseUrl);
        if (!base_url.empty()) {
          desc->base_url = base_url;
        }
      }
      desc->state = !base_url.empty()
                        ? maho_settings::mojom::AIConnectionState::kConnected
                        : maho_settings::mojom::AIConnectionState::kDisconnected;
    } else if (pid == "local-server") {
      desc->label = "Local Server (Ollama)";
      desc->auth_label = "Ollama";
      std::string base_url = desc->base_url.value_or("");
      if (base_url.empty()) {
        base_url = prefs_->GetString(maho::ai_prefs::kBaseUrl);
        if (base_url.empty()) {
          base_url = "http://localhost:11434";
        }
        desc->base_url = base_url;
      }
      desc->state =
          (cfg != nullptr ||
           prefs_->GetString(maho::ai_prefs::kProvider) == "local-server")
              ? maho_settings::mojom::AIConnectionState::kConnected
              : maho_settings::mojom::AIConnectionState::kDisconnected;
    }

    std::vector<std::string> fallback_models =
        maho::ai::MahoModelListFetcher::GetHardcodedFallback(pid);
    for (const auto& mid : fallback_models) {
      auto mdesc = maho_settings::mojom::AIModelDescriptor::New();
      mdesc->id = mid;
      mdesc->label = mid;
      mdesc->vision = maho_settings::mojom::AIModelCapability::kUnknown;
      mdesc->tools = maho_settings::mojom::AIModelCapability::kUnknown;
      desc->models.push_back(std::move(mdesc));
    }

    settings->providers.push_back(std::move(desc));
  }

  struct TaskInfo {
    std::string task_id;
    maho::ai::MahoAiTask task;
  };
  const std::vector<TaskInfo> tasks = {
      {"tab_tidy", maho::ai::MahoAiTask::kTabTidy},
      {"inline_edit", maho::ai::MahoAiTask::kInlineEdit},
      {"page_preview", maho::ai::MahoAiTask::kPagePreview},
      {"tab_title", maho::ai::MahoAiTask::kTabTitle},
      {"download_tidy", maho::ai::MahoAiTask::kDownloadTidy},
      {"memory", maho::ai::MahoAiTask::kMemory},
  };

  const base::DictValue& task_models =
      prefs_->GetDict(maho::ai_prefs::kTaskModels);

  for (const auto& t : tasks) {
    auto route = maho_settings::mojom::AITaskModelRoute::New();
    route->task_id = t.task_id;
    const base::DictValue* task_entry = task_models.FindDict(t.task_id);
    route->inherits_default = (task_entry == nullptr);
    if (task_entry) {
      const std::string* prov = task_entry->FindString("provider");
      if (!prov) {
        prov = task_entry->FindString("provider_id");
      }
      const std::string* mod = task_entry->FindString("model");
      if (!mod) {
        mod = task_entry->FindString("model_id");
      }
      if (prov) {
        route->configured_provider_id = *prov;
      }
      if (mod) {
        route->configured_model_id = *mod;
      }
    }

    auto route_res = maho::ai::ResolveMahoAiModelRoute(prefs_, t.task);
    if (route_res.is_ok()) {
      route->effective_provider_id = route_res.route.provider_id;
      route->effective_model_id = route_res.route.model_id;
      route->available = true;
    } else {
      route->available = false;
      switch (route_res.error) {
        case maho::ai::MahoAiRouteError::kNoDefault:
          route->unavailable_reason = "No default model configured";
          break;
        case maho::ai::MahoAiRouteError::kProviderDisconnected:
          route->unavailable_reason = "Provider disconnected";
          break;
        case maho::ai::MahoAiRouteError::kModelMissing:
          route->unavailable_reason = "Model not specified";
          break;
        case maho::ai::MahoAiRouteError::kVisionUnsupported:
          route->unavailable_reason = "Vision unsupported";
          break;
        case maho::ai::MahoAiRouteError::kUnknownProvider:
          route->unavailable_reason = "Unknown provider";
          break;
        default:
          route->unavailable_reason = "Unavailable";
          break;
      }
    }
    settings->task_routes.push_back(std::move(route));
  }

  return settings;
}

void MahoSettingsPageHandler::OnAIModelsSettingsPrefsChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (page_.is_bound()) {
    page_->OnAIModelsSettingsChanged(BuildAIModelsSettings());
  }
}

void MahoSettingsPageHandler::SetDefaultAIModel(
    const std::string& provider_id,
    const std::string& model_id,
    SetDefaultAIModelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (provider_id.empty() || model_id.empty()) {
    std::move(callback).Run(false, "Provider and model must not be empty.");
    return;
  }
  if (model_id == "default") {
    std::move(callback).Run(false, "Invalid model selection.");
    return;
  }
  if (!maho::ai::IsProviderConnected(prefs_, provider_id)) {
    std::move(callback).Run(false, "Provider is not connected.");
    return;
  }
  if (!IsValidModelForProvider(provider_id, model_id)) {
    std::move(callback).Run(false, "Selected model is not valid for this provider.");
    return;
  }
  prefs_->SetString(ai::kProvider, provider_id);
  prefs_->SetString(ai::kModel, model_id);

  {
    ScopedDictPrefUpdate update(prefs_, maho::ai_prefs::kProviderConfigs);
    base::DictValue* pcfg = update->EnsureDict(provider_id);
    pcfg->Set("last_model", model_id);
    pcfg->Set("last_model_id", model_id);
  }

  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  std::move(callback).Run(true, std::nullopt);
}

void MahoSettingsPageHandler::SetTaskAIModel(
    const std::string& task_id,
    const std::optional<std::string>& provider_id,
    const std::optional<std::string>& model_id,
    SetTaskAIModelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (task_id == "chat") {
    std::move(callback).Run(false,
                            "Chat & Agent task must use SetDefaultAIModel.");
    return;
  }

  ScopedDictPrefUpdate update(prefs_, maho::ai_prefs::kTaskModels);
  if (!provider_id.has_value() || !model_id.has_value() ||
      provider_id->empty() || model_id->empty()) {
    update->Remove(task_id);
  } else {
    if (!IsValidModelForProvider(*provider_id, *model_id)) {
      std::move(callback).Run(false, "Selected model is not valid for this provider.");
      return;
    }
    base::DictValue* entry = update->EnsureDict(task_id);
    entry->Set("provider", *provider_id);
    entry->Set("provider_id", *provider_id);
    entry->Set("model", *model_id);
    entry->Set("model_id", *model_id);
  }

  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  std::move(callback).Run(true, std::nullopt);
}

void MahoSettingsPageHandler::SetAIProviderBaseUrl(
    const std::string& provider_id,
    const std::string& base_url,
    SetAIProviderBaseUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  {
    ScopedDictPrefUpdate update(prefs_, maho::ai_prefs::kProviderConfigs);
    base::DictValue* pcfg = update->EnsureDict(provider_id);
    pcfg->Set("base_url", base_url);
  }

  if (prefs_->GetString(ai::kProvider) == provider_id) {
    prefs_->SetString(ai::kBaseUrl, base_url);
  }

  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  std::move(callback).Run(true, std::nullopt);
}

void MahoSettingsPageHandler::TestAIProvider(
    const std::string& provider_id,
    TestAIProviderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto result = maho_settings::mojom::AIProviderTestResult::New();
  if (provider_id == "maho-managed") {
    bool ok = maho::auth::HasValidRelaySession(prefs_);
    result->ok = ok;
    result->message = ok ? "Maho relay session is active and healthy."
                         : "No active Maho account session.";
  } else if (provider_id == "openai") {
    bool ok =
        maho::ai_oauth::HasOAuthSession(prefs_, "openai") ||
        !prefs_->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64).empty();
    result->ok = ok;
    result->message =
        ok ? "OpenAI credential configured." : "OpenAI credential missing.";
  } else if (provider_id == "anthropic") {
    bool ok =
        maho::ai_oauth::HasOAuthSession(prefs_, "anthropic") ||
        !prefs_->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64).empty();
    result->ok = ok;
    result->message =
        ok ? "Anthropic credential configured." : "Anthropic credential missing.";
  } else if (provider_id == "openai-compatible") {
    std::string url = prefs_->GetString(maho::ai_prefs::kBaseUrl);
    const base::DictValue& configs =
        prefs_->GetDict(maho::ai_prefs::kProviderConfigs);
    if (const base::DictValue* c = configs.FindDict("openai-compatible")) {
      if (const std::string* u = c->FindString("base_url")) {
        url = *u;
      }
    }
    bool ok = !url.empty();
    result->ok = ok;
    result->message = ok ? "Custom OpenAI-compatible endpoint configured."
                         : "Base URL is required.";
  } else if (provider_id == "local-server") {
    result->ok = true;
    result->message = "Local server endpoint ready.";
  } else {
    result->ok = false;
    result->message = "Unknown provider: " + provider_id;
  }
  std::move(callback).Run(std::move(result));
}

bool MahoSettingsPageHandler::DisconnectAIProviderInternal(
    PrefService* prefs,
    const std::string& provider_id,
    const std::optional<std::string>& replacement_default_provider_id,
    const std::optional<std::string>& replacement_default_model_id,
    std::vector<maho_settings::mojom::AITaskReplacementPtr> task_replacements,
    std::vector<std::string>& affected_tasks,
    std::string& error_out) {
  if (!prefs) {
    error_out = "Pref service is null.";
    return false;
  }

  bool is_default = (prefs->GetString(ai::kProvider) == provider_id);
  if (is_default) {
    affected_tasks.push_back("chat");
    if (!replacement_default_provider_id.has_value() ||
        replacement_default_provider_id->empty()) {
      error_out = "Replacement default provider must be specified.";
      return false;
    }
    if (*replacement_default_provider_id == provider_id) {
      error_out = "Replacement default provider cannot be the provider being disconnected.";
      return false;
    }
    if (!maho::ai::IsProviderConnected(prefs, *replacement_default_provider_id)) {
      error_out = "Replacement default provider is not connected.";
      return false;
    }
    if (!replacement_default_model_id.has_value() ||
        !IsValidModelForProvider(*replacement_default_provider_id,
                                 *replacement_default_model_id)) {
      error_out = "Replacement default model is not valid for the replacement provider.";
      return false;
    }
  }

  const base::DictValue& task_models =
      prefs->GetDict(maho::ai_prefs::kTaskModels);
  std::vector<std::string> task_route_names;
  for (const auto item : task_models) {
    if (const base::DictValue* dict = item.second.GetIfDict()) {
      const std::string* pid = dict->FindString("provider");
      if (!pid) {
        pid = dict->FindString("provider_id");
      }
      if (pid && *pid == provider_id) {
        affected_tasks.push_back(item.first);
        task_route_names.push_back(item.first);
      }
    }
  }

  // Validate task replacements: ensure no duplicates, exact match with affected
  // tasks set, and destinations are connected and valid.
  std::set<std::string> affected_set(task_route_names.begin(), task_route_names.end());
  std::set<std::string> seen_task_ids;
  for (const auto& repl : task_replacements) {
    if (repl->task_id == "chat") {
      error_out = "Chat & Agent task cannot be modified via task replacements.";
      return false;
    }
    if (!affected_set.contains(repl->task_id)) {
      error_out = "Task replacement ID is not an affected explicit route: " + repl->task_id;
      return false;
    }
    if (seen_task_ids.count(repl->task_id)) {
      error_out = "Duplicate task replacement entry for: " + repl->task_id;
      return false;
    }
    seen_task_ids.insert(repl->task_id);

    if (!repl->inherit_default) {
      if (!repl->provider_id.has_value() || repl->provider_id->empty()) {
        error_out = "Replacement provider must be specified for task: " + repl->task_id;
        return false;
      }
      if (*repl->provider_id == provider_id) {
        error_out = "Replacement provider for task " + repl->task_id +
                    " cannot be the provider being disconnected.";
        return false;
      }
      if (!maho::ai::IsProviderConnected(prefs, *repl->provider_id)) {
        error_out = "Replacement provider for task " + repl->task_id + " is not connected.";
        return false;
      }
      if (!repl->model_id.has_value() ||
          !IsValidModelForProvider(*repl->provider_id, *repl->model_id)) {
        error_out = "Replacement model is not valid for task: " + repl->task_id;
        return false;
      }
    }
  }

  if (seen_task_ids != affected_set) {
    error_out = "All affected tasks must be explicitly accounted for in task replacements.";
    return false;
  }

  if (is_default) {
    prefs->SetString(ai::kProvider, *replacement_default_provider_id);
    prefs->SetString(ai::kModel, *replacement_default_model_id);
  }

  if (!task_replacements.empty()) {
    ScopedDictPrefUpdate task_update(prefs, maho::ai_prefs::kTaskModels);
    for (const auto& repl : task_replacements) {
      if (repl->inherit_default || !repl->provider_id.has_value() ||
          repl->provider_id->empty()) {
        task_update->Remove(repl->task_id);
      } else {
        base::DictValue* entry = task_update->EnsureDict(repl->task_id);
        entry->Set("provider", *repl->provider_id);
        entry->Set("provider_id", *repl->provider_id);
        entry->Set("model", repl->model_id.value_or(""));
        entry->Set("model_id", repl->model_id.value_or(""));
      }
    }
  }

  {
    ScopedDictPrefUpdate config_update(prefs, maho::ai_prefs::kProviderConfigs);
    config_update->Remove(provider_id);
  }

  if (provider_id == "openai") {
    prefs->SetString(maho::ai_prefs::kByokOpenAIEncryptedB64, "");
    maho::ai_oauth::ClearProviderOAuth(prefs, "openai");
  } else if (provider_id == "anthropic") {
    prefs->SetString(maho::ai_prefs::kByokAnthropicEncryptedB64, "");
    maho::ai_oauth::ClearProviderOAuth(prefs, "anthropic");
  } else if (provider_id == "openai-compatible" || provider_id == "custom") {
    prefs->SetString(maho::ai_prefs::kByokOpenAICompatibleEncryptedB64, "");
    if (prefs->GetString(ai::kProvider) == "openai-compatible") {
      prefs->SetString(ai::kBaseUrl, "");
      prefs->SetString(ai::kApiKey, "");
    }
  }

  return true;
}

bool MahoSettingsPageHandler::DisconnectAIProviderForTesting(
    PrefService* prefs,
    const std::string& provider_id,
    const std::optional<std::string>& replacement_default_provider_id,
    const std::optional<std::string>& replacement_default_model_id,
    std::vector<maho_settings::mojom::AITaskReplacementPtr> task_replacements,
    std::string* error_out) {
  std::vector<std::string> affected_tasks;
  std::string error;
  bool ok = DisconnectAIProviderInternal(
      prefs, provider_id, replacement_default_provider_id,
      replacement_default_model_id, std::move(task_replacements),
      affected_tasks, error);
  if (error_out) {
    *error_out = error;
  }
  return ok;
}

void MahoSettingsPageHandler::DisconnectAIProvider(
    const std::string& provider_id,
    const std::optional<std::string>& replacement_default_provider_id,
    const std::optional<std::string>& replacement_default_model_id,
    std::vector<maho_settings::mojom::AITaskReplacementPtr> task_replacements,
    DisconnectAIProviderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> affected_tasks;
  std::string error;
  bool ok = DisconnectAIProviderInternal(
      prefs_, provider_id, replacement_default_provider_id,
      replacement_default_model_id, std::move(task_replacements),
      affected_tasks, error);
  if (!ok) {
    std::move(callback).Run(false, affected_tasks, error);
    return;
  }

  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  std::move(callback).Run(true, affected_tasks, std::nullopt);
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
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
}

void MahoSettingsPageHandler::SetAIBaseUrl(const std::string& url) {
  prefs_->SetString(ai::kBaseUrl, url);
  // provider/api_key commit immediately; base_url and model must too. Leaving
  // them on the lazy flush lets a shutdown persist a new api_key alongside a
  // stale base_url/model, which authenticates against the wrong endpoint.
  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
}

void MahoSettingsPageHandler::SetAIApiKey(const std::string& key) {
  prefs_->SetString(ai::kApiKey, key);
  prefs_->CommitPendingWrite();
  if (key.empty()) {
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys("openai-compatible");
  }
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
}

void MahoSettingsPageHandler::SetAIModel(const std::string& model) {
  prefs_->SetString(ai::kModel, model);
  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
}

void MahoSettingsPageHandler::SetAIApprovalPolicy(const std::string& policy) {
  prefs_->SetString(ai::kApprovalPolicy, policy);
}

void MahoSettingsPageHandler::SetAISessionPersistence(bool enabled) {
  prefs_->SetBoolean(ai::kSessionPersistenceEnabled, enabled);
}

void MahoSettingsPageHandler::SetAIMailReadAllowed(
    bool allowed,
    SetAIMailReadAllowedCallback callback) {
  if (!prefs_ || !prefs_->FindPreference(ai::kMailReadAllowed)) {
    std::move(callback).Run(false);
    return;
  }
  prefs_->SetBoolean(ai::kMailReadAllowed, allowed);
  prefs_->CommitPendingWrite();
  std::move(callback).Run(
      prefs_->GetBoolean(ai::kMailReadAllowed) == allowed);
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

void MahoSettingsPageHandler::GetCurrentBrowserSpaceSnapshot(
    GetCurrentBrowserSpaceSnapshotCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  GlobalBrowserCollection* collection = GlobalBrowserCollection::GetInstance();
  BrowserWindowInterface* browser_window =
      collection ? collection->GetActiveBrowser() : nullptr;
  Browser* browser = static_cast<Browser*>(browser_window);
  Profile* focused_profile = browser ? browser->GetProfile() : nullptr;
  const SessionID session_id =
      browser ? browser->GetSessionID() : SessionID::InvalidValue();
  if (!browser || !session_id.is_valid() || !focused_profile ||
      !IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
          focused_profile->IsRegularProfile(), focused_profile->IsOffTheRecord(),
          focused_profile->IsGuestSession(), focused_profile->IsSystemProfile())) {
    std::move(callback).Run(nullptr);
    return;
  }

  const std::string space_id =
      maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser);
  if (space_id.empty()) {
    std::move(callback).Run(nullptr);
    return;
  }

  const int32_t focused_browser_session_id = session_id.id();
  maho::PostCoreTask(
      FROM_HERE,
      base::BindOnce(&BuildCurrentBrowserSpaceCoreResult, space_id,
                     static_cast<int64_t>(focused_browser_session_id)),
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> self,
             GetCurrentBrowserSpaceSnapshotCallback cb,
             int32_t browser_session_id, std::string selected_space_id,
             CurrentBrowserSpaceCoreResult core_result) {
            if (!self) {
              return;
            }
            DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
            GlobalBrowserCollection* collection =
                GlobalBrowserCollection::GetInstance();
            BrowserWindowInterface* browser_window =
                collection ? collection->GetActiveBrowser() : nullptr;
            Browser* browser = static_cast<Browser*>(browser_window);
            Profile* focused_profile = browser ? browser->GetProfile() : nullptr;
            if (!browser || browser->GetSessionID().id() != browser_session_id ||
                !focused_profile ||
                !IsCurrentBrowserSpaceSnapshotProfileAllowedForTesting(
                    focused_profile->IsRegularProfile(),
                    focused_profile->IsOffTheRecord(),
                    focused_profile->IsGuestSession(),
                    focused_profile->IsSystemProfile()) ||
                maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(
                    browser) != selected_space_id) {
              std::move(cb).Run(nullptr);
              return;
            }
            std::move(cb).Run(ParseCurrentBrowserSpaceSnapshotForTesting(
                browser_session_id, selected_space_id, core_result.spaces_json,
                core_result.tabs_json));
          },
          weak_factory_.GetWeakPtr(), std::move(callback),
          focused_browser_session_id, space_id));
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
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run({});
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::GetSavedPasswordsFromCore());
}

void MahoSettingsPageHandler::SearchPasswords(
    const std::string& query,
    SearchPasswordsCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run({});
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::SearchSavedPasswordsFromCore(query));
}

void MahoSettingsPageHandler::AddPassword(const std::string& domain,
                                          const std::string& username,
                                          const std::string& password,
                                          AddPasswordCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kAdd,
      CreatePasswordAuthenticator(profile_), u"Add password",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthAddComplete,
                     weak_factory_.GetWeakPtr(), domain, username, password,
                     std::unique_ptr<device_reauth::DeviceAuthenticator>(),
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthAddComplete(
    const std::string& domain,
    const std::string& username,
    const std::string& password,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    AddPasswordCallback callback,
    bool success) {
  if (!success || !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }

  const bool ok = maho_settings_password_helpers::
      AddSavedPasswordCompatibilityShimInCore(
          PasswordAuthorizationProfileKey(profile_), domain, username,
          password);
  if (ok) {
    ScheduleNotify();
  }
  std::move(callback).Run(ok);
}

void MahoSettingsPageHandler::UpdatePasswordUsername(
    const std::string& password_id,
    const std::string& username,
    UpdatePasswordUsernameCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      CreatePasswordAuthenticator(profile_), u"Edit password",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthUpdateComplete,
                     weak_factory_.GetWeakPtr(), password_id, username,
                     std::unique_ptr<device_reauth::DeviceAuthenticator>(),
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthUpdateComplete(
    const std::string& password_id,
    const std::string& username,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    UpdatePasswordUsernameCallback callback,
    bool success) {
  if (!success || !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }

  const bool ok = maho_settings_password_helpers::
      UpdateSavedPasswordUsernameCompatibilityShimInCore(
          PasswordAuthorizationProfileKey(profile_), password_id, username);
  if (ok) {
    ScheduleNotify();
  }
  std::move(callback).Run(ok);
}

void MahoSettingsPageHandler::DeletePassword(const std::string& password_id,
                                             DeletePasswordCallback callback) {
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kDelete,
      CreatePasswordAuthenticator(profile_), u"Delete password",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthDeleteComplete,
                     weak_factory_.GetWeakPtr(), password_id,
                     std::unique_ptr<device_reauth::DeviceAuthenticator>(),
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthDeleteComplete(
    const std::string& password_id,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    DeletePasswordCallback callback,
    bool success) {
  if (!success || !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false);
    return;
  }

  const bool ok = maho_settings_password_helpers::
      DeleteSavedPasswordCompatibilityShimInCore(
          PasswordAuthorizationProfileKey(profile_), password_id);
  if (ok) {
    ScheduleNotify();
  }
  std::move(callback).Run(ok);
}

void MahoSettingsPageHandler::GetVaultStatus(GetVaultStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultStatusFromCore());
}

void MahoSettingsPageHandler::GetVaultPreflightState(
    GetVaultPreflightStateCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Key loss can prevent MahoCore from being created, so the ordinary
  // core-owner predicate cannot be the sole gate here. The preflight is
  // process-wide diagnosis only (no Vault metadata or secrets): admit a
  // regular non-OTR profile while no core owner exists, then restore the
  // single-owner check as soon as one does.
  if (!profile_ || !profile_->IsRegularProfile() || profile_->IsOffTheRecord() ||
      (maho::GetCoreOwnerProfile() &&
       !maho::IsPasswordManagerAllowedForProfile(profile_))) {
    std::move(callback).Run(
        maho_settings::mojom::VaultPreflightState::kUnavailable);
    return;
  }

  maho_settings::mojom::VaultPreflightState state =
      maho_settings::mojom::VaultPreflightState::kUnavailable;
  switch (maho::core::GetVaultPreflightState()) {
    case maho::core::VaultPreflightState::kHealthy:
      state = maho_settings::mojom::VaultPreflightState::kHealthy;
      break;
    case maho::core::VaultPreflightState::kLocked:
      state = maho_settings::mojom::VaultPreflightState::kLocked;
      break;
    case maho::core::VaultPreflightState::kUnrecoverableKey:
      state = maho_settings::mojom::VaultPreflightState::kUnrecoverableKey;
      break;
    case maho::core::VaultPreflightState::kStructuralCorruption:
      state =
          maho_settings::mojom::VaultPreflightState::kStructuralCorruption;
      break;
    case maho::core::VaultPreflightState::kPlaintextResidue:
      state = maho_settings::mojom::VaultPreflightState::kPlaintextResidue;
      break;
    case maho::core::VaultPreflightState::kUnavailable:
      break;
  }
  std::move(callback).Run(state);
}

void MahoSettingsPageHandler::GetVaultProviderStatus(
    GetVaultProviderStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultProviderStatus());
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::BuildVaultProviderStatus());
}

void MahoSettingsPageHandler::InitializeVault(
    const std::string& master_passphrase,
    const std::string& recovery_secret,
    InitializeVaultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto result = maho_settings_password_helpers::InitializeVaultInCore(
      master_passphrase, recovery_secret);
  if (result->success) {
    result = FinalizeVaultLifecycleOperation(
        std::move(result),
        maho_settings_password_helpers::GetVaultStatusFromCore(),
        [](bool locked) { maho::NotifyVaultLockStateChanged(locked); });
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::UnlockVault(const std::string& master_passphrase,
                                          UnlockVaultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto result =
      maho_settings_password_helpers::UnlockVaultInCore(master_passphrase);
  if (result->success) {
    result = FinalizeVaultLifecycleOperation(
        std::move(result),
        maho_settings_password_helpers::GetVaultStatusFromCore(),
        [](bool locked) { maho::NotifyVaultLockStateChanged(locked); });
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::UnlockVaultWithRecovery(
    const std::string& recovery_secret,
    UnlockVaultWithRecoveryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto result =
      maho_settings_password_helpers::UnlockVaultWithRecoveryInCore(
          recovery_secret);
  if (result->success) {
    result = FinalizeVaultLifecycleOperation(
        std::move(result),
        maho_settings_password_helpers::GetVaultStatusFromCore(),
        [](bool locked) { maho::NotifyVaultLockStateChanged(locked); });
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::LockVault(LockVaultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto result = maho_settings_password_helpers::LockVaultInCore();
  if (result->success) {
    result = FinalizeVaultLifecycleOperation(
        std::move(result),
        maho_settings_password_helpers::GetVaultStatusFromCore(),
        [](bool locked) { maho::NotifyVaultLockStateChanged(locked); });
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::ListVaultItems(
    std::optional<maho_settings::mojom::PasswordProviderKind> provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
    const std::optional<std::string>& cursor,
    uint32_t limit,
    bool trash_only,
    bool favorites_only,
    ListVaultItemsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultItemListResult());
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::ListVaultItemsFromCore(provider, kinds,
          cursor, limit, trash_only, favorites_only));
}

void MahoSettingsPageHandler::SearchVaultItems(
    const std::string& origin,
    std::optional<maho_settings::mojom::PasswordProviderKind> provider,
    const std::vector<maho_settings::mojom::VaultItemKind>& kinds,
    SearchVaultItemsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultItemListResult());
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::SearchVaultItemsFromCore(origin, provider,
                                                               kinds));
}

void MahoSettingsPageHandler::AddVaultLogin(
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::string& password,
    const std::optional<std::string>& notes,
    AddVaultLoginCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto secrets = std::make_unique<VaultRequestSecrets>();
  secrets->password = password;
  secrets->notes = notes;
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kAdd,
      CreatePasswordAuthenticator(profile_), u"Add password",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string title, std::vector<std::string> origins,
             std::string username, std::unique_ptr<VaultRequestSecrets> secrets,
             AddVaultLoginCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::AddVaultLoginInCore(
                PasswordAuthorizationProfileKey(handler->profile_), title,
                origins, username, *secrets->password, secrets->notes);
            secrets.reset();
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), title, origins, username, std::move(secrets),
          std::move(callback)));
}

void MahoSettingsPageHandler::UpdateVaultLogin(
    const std::string& item_id,
    uint64_t expected_revision,
    const std::string& title,
    const std::vector<std::string>& origins,
    const std::string& username,
    const std::optional<std::string>& password,
    const std::optional<std::string>& notes,
    UpdateVaultLoginCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto secrets = std::make_unique<VaultRequestSecrets>();
  secrets->password = password;
  secrets->notes = notes;
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      CreatePasswordAuthenticator(profile_), u"Edit password",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision,
             std::string title, std::vector<std::string> origins,
             std::string username, std::unique_ptr<VaultRequestSecrets> secrets,
             UpdateVaultLoginCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result =
                maho_settings_password_helpers::UpdateVaultLoginInCore(
                    PasswordAuthorizationProfileKey(handler->profile_), item_id,
                    expected_revision, title, origins, username,
                    secrets->password, secrets->notes);
            secrets.reset();
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision, title,
          origins, username, std::move(secrets), std::move(callback)));
}

void MahoSettingsPageHandler::TrashVaultItem(
    const std::string& item_id, uint64_t expected_revision,
    TrashVaultItemCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kDelete,
      CreatePasswordAuthenticator(profile_), u"Move password to trash",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision,
             TrashVaultItemCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::TrashVaultItemInCore(
                PasswordAuthorizationProfileKey(handler->profile_),
                item_id, expected_revision);
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision, std::move(callback)));
}

void MahoSettingsPageHandler::RestoreVaultItem(
    const std::string& item_id, uint64_t expected_revision,
    RestoreVaultItemCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      CreatePasswordAuthenticator(profile_), u"Restore password",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision,
             RestoreVaultItemCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::RestoreVaultItemInCore(
                PasswordAuthorizationProfileKey(handler->profile_),
                item_id, expected_revision);
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision, std::move(callback)));
}

void MahoSettingsPageHandler::EmptyVaultTrash(
    EmptyVaultTrashCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kDelete,
      CreatePasswordAuthenticator(profile_), u"Empty password trash",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             EmptyVaultTrashCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::EmptyVaultTrashInCore(
                PasswordAuthorizationProfileKey(handler->profile_));
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSettingsPageHandler::SetVaultItemFavorite(
    const std::string& item_id, uint64_t expected_revision, bool favorite,
    SetVaultItemFavoriteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      CreatePasswordAuthenticator(profile_), u"Change password favorite",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision, bool favorite,
             SetVaultItemFavoriteCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::SetVaultItemFavoriteInCore(
                PasswordAuthorizationProfileKey(handler->profile_),
                item_id, expected_revision, favorite);
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision, favorite, std::move(callback)));
}

void MahoSettingsPageHandler::AddVaultSecureNote(
    const std::string& title, const std::string& notes,
    AddVaultSecureNoteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto secrets = std::make_unique<VaultRequestSecrets>();
  secrets->notes = notes;
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kAdd,
      CreatePasswordAuthenticator(profile_), u"Add secure note",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string title, std::unique_ptr<VaultRequestSecrets> secrets,
             AddVaultSecureNoteCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::AddVaultSecureNoteInCore(
                PasswordAuthorizationProfileKey(handler->profile_),
                title, *secrets->notes);
            secrets.reset();
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), title, std::move(secrets), std::move(callback)));
}

void MahoSettingsPageHandler::UpdateVaultSecureNote(
    const std::string& item_id, uint64_t expected_revision, const std::string& title, const std::string& notes,
    UpdateVaultSecureNoteCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto secrets = std::make_unique<VaultRequestSecrets>();
  secrets->notes = notes;
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      CreatePasswordAuthenticator(profile_), u"Edit secure note",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision, std::string title, std::unique_ptr<VaultRequestSecrets> secrets,
             UpdateVaultSecureNoteCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::UpdateVaultSecureNoteInCore(
                PasswordAuthorizationProfileKey(handler->profile_),
                item_id, expected_revision, title, *secrets->notes);
            secrets.reset();
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision, title, std::move(secrets), std::move(callback)));
}

void MahoSettingsPageHandler::SetVaultLoginTotp(
    const std::string& item_id, uint64_t expected_revision, const std::string& secret,
    SetVaultLoginTotpCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto secrets = std::make_unique<VaultRequestSecrets>();
  secrets->password = secret;
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kUpdate,
      CreatePasswordAuthenticator(profile_), u"Change authenticator",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision, std::unique_ptr<VaultRequestSecrets> secrets,
             SetVaultLoginTotpCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::SetVaultLoginTotpInCore(
                PasswordAuthorizationProfileKey(handler->profile_),
                item_id, expected_revision, *secrets->password);
            secrets.reset();
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision, std::move(secrets), std::move(callback)));
}


void MahoSettingsPageHandler::LockNow(LockNowCallback callback) {
  LockVault(std::move(callback));
}

void MahoSettingsPageHandler::GetVaultItemNotes(
    const std::string& item_id, GetVaultItemNotesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(false, std::nullopt, "profile_not_allowed",
                            std::nullopt);
    return;
  }
  auto result = maho_settings_password_helpers::GetVaultItemNotesFromCore(item_id);
  std::move(callback).Run(result.success, std::move(result.notes),
                          std::move(result.error_code),
                          std::move(result.username));
}

void MahoSettingsPageHandler::GetVaultTotpCode(
    const std::string& item_id, GetVaultTotpCodeCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    auto result = maho_settings::mojom::VaultTotpCodeResult::New();
    result->success = false;
    result->error_code = "profile_not_allowed";
    std::move(callback).Run(std::move(result));
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultTotpCodeFromCore(item_id));
}

void MahoSettingsPageHandler::GeneratePassword(
    maho_settings::mojom::PasswordGeneratorOptionsPtr options,
    GeneratePasswordCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    auto result = maho_settings::mojom::GeneratedPasswordResult::New();
    result->success = false;
    std::move(callback).Run(std::move(result));
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::GeneratePasswordInCore(*options));
}

void MahoSettingsPageHandler::EstimatePasswordStrength(
    const std::string& password, EstimatePasswordStrengthCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(0, 0.0);
    return;
  }
  auto [score, entropy_bits] =
      maho_settings_password_helpers::EstimatePasswordStrengthInCore(password);
  std::move(callback).Run(score, entropy_bits);
}

void MahoSettingsPageHandler::GetVaultHealthReport(
    GetVaultHealthReportCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    auto result = maho_settings::mojom::VaultHealthReport::New();
    result->success = false;
    result->error_code = "profile_not_allowed";
    std::move(callback).Run(std::move(result));
    return;
  }
  std::move(callback).Run(
      maho_settings_password_helpers::GetVaultHealthReportFromCore());
}

void MahoSettingsPageHandler::DeleteVaultItem(
    const std::string& item_id,
    uint64_t expected_revision,
    DeleteVaultItemCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kDelete,
      CreatePasswordAuthenticator(profile_), u"Delete password",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string item_id, uint64_t expected_revision,
             DeleteVaultItemCallback callback, bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
              return;
            }
            auto result = maho_settings_password_helpers::DeleteVaultItemInCore(
                PasswordAuthorizationProfileKey(handler->profile_), item_id,
                expected_revision);
            if (result->success) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), item_id, expected_revision,
          std::move(callback)));
}

void MahoSettingsPageHandler::UseVaultSecret(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action,
    UseVaultSecretCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure("profile_not_allowed"));
    return;
  }
  auto authenticator = CreatePasswordAuthenticator(profile_);
  // Platforms without a device authenticator (Linux) treat the Vault unlock as
  // the reveal boundary; asking for OS auth there could only ever fail.
  const bool device_reauth_available =
      maho::passwords::IsVaultDeviceReauthRequired();
  const bool fresh_os_auth =
      action == maho_settings::mojom::SecretAction::kReveal &&
      device_reauth_available &&
      maho_settings_password_helpers::ReadVaultDeviceAuthRequiredFromCore();
  if (fresh_os_auth) {
    device_reauth::DeviceAuthParams params(
        base::Seconds(0), device_reauth::DeviceAuthSource::kPasswordManager,
        "PasswordManager.ReauthToAccessPasswordInSettings");
    authenticator = ChromeDeviceAuthenticatorFactory::GetForProfile(
        profile_, GetSettingsNativeWindow(profile_), params);
    if (!authenticator ||
        !authenticator->CanAuthenticateWithBiometricOrScreenLock()) {
      std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
      return;
    }
    maho::passwords::MahoPasswordAuthorizationService::Get()->RevokeProfile(
        PasswordAuthorizationProfileKey(profile_));
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kCopy,
      std::move(authenticator), u"Use saved password",
      base::BindOnce(&MahoSettingsPageHandler::OnReauthUseVaultSecretComplete,
                     weak_factory_.GetWeakPtr(), item_id, expected_revision,
                     action, fresh_os_auth,
                     std::unique_ptr<device_reauth::DeviceAuthenticator>(),
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthUseVaultSecretComplete(
    const std::string& item_id,
    uint64_t expected_revision,
    maho_settings::mojom::SecretAction action,
    bool fresh_os_auth,
    std::unique_ptr<device_reauth::DeviceAuthenticator> authenticator,
    UseVaultSecretCallback callback,
    bool success) {
  if (!success || !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(MakeVaultUseFailure(
        success ? "profile_not_allowed" : "reauth_failed"));
    return;
  }
  if (action == maho_settings::mojom::SecretAction::kReveal && !fresh_os_auth &&
      maho::passwords::IsVaultDeviceReauthRequired()) {
    if (!host_web_contents_ || maho::IsVaultLockedForUi()) {
      std::move(callback).Run(MakeVaultUseFailure("vault_locked"));
      return;
    }
    if (vault_reveal_authenticator_) {
      std::move(callback).Run(MakeVaultUseFailure("reauth_in_progress"));
      return;
    }
    vault_reveal_dialog_.reset();
    device_reauth::DeviceAuthParams params(
        base::Seconds(0), device_reauth::DeviceAuthSource::kPasswordManager,
        "PasswordManager.ReauthToAccessPasswordInSettings");
    vault_reveal_authenticator_ = ChromeDeviceAuthenticatorFactory::GetForProfile(
        profile_, host_web_contents_->GetTopLevelNativeWindow(), params);
    if (!vault_reveal_authenticator_ ||
        !vault_reveal_authenticator_->CanAuthenticateWithBiometricOrScreenLock()) {
      vault_reveal_authenticator_.reset();
      std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
      return;
    }
    const uint64_t generation = ++vault_reveal_generation_;
    vault_reveal_authenticator_->AuthenticateWithMessage(
        u"Reveal saved password",
        base::BindOnce(
            [](base::WeakPtr<MahoSettingsPageHandler> handler,
               base::WeakPtr<content::WebContents> contents,
               uint64_t generation, std::string item_id, uint64_t revision,
               UseVaultSecretCallback callback, bool authenticated) {
              if (!handler || !contents ||
                  generation != handler->vault_reveal_generation_ ||
                  !authenticated || maho::IsVaultLockedForUi() ||
                  !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
                if (handler && generation == handler->vault_reveal_generation_) {
                  handler->vault_reveal_authenticator_.reset();
                }
                std::move(callback).Run(MakeVaultUseFailure("reauth_failed"));
                return;
              }
              handler->vault_reveal_authenticator_.reset();
              auto result = maho_settings_password_helpers::UseVaultSecretInCore(
                  PasswordAuthorizationProfileKey(handler->profile_), item_id,
                  revision, maho_settings::mojom::SecretAction::kReveal,
                  base::BindOnce(
                      [](base::WeakPtr<MahoSettingsPageHandler> handler,
                         const char* secret) {
                        if (!handler || !handler->host_web_contents_ ||
                            maho::IsVaultLockedForUi()) {
                          return false;
                        }
                        handler->vault_reveal_dialog_ =
                            std::make_unique<MahoVaultRevealDialog>(
                                handler->host_web_contents_, secret);
                        return true;
                      }, handler));
              if (result->success) handler->ScheduleNotify();
              std::move(callback).Run(std::move(result));
            }, weak_factory_.GetWeakPtr(), host_web_contents_->GetWeakPtr(),
            generation, item_id,
            expected_revision, std::move(callback)));
    return;
  }
  auto result = maho_settings_password_helpers::UseVaultSecretInCore(
      PasswordAuthorizationProfileKey(profile_), item_id, expected_revision,
      action, base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler, const char* secret) {
            if (!handler || !handler->host_web_contents_ ||
                maho::IsVaultLockedForUi()) {
              return false;
            }
            handler->vault_reveal_dialog_.reset();
            handler->vault_reveal_dialog_ = std::make_unique<MahoVaultRevealDialog>(
                handler->host_web_contents_, secret);
            return true;
          }, weak_factory_.GetWeakPtr()));
  if (result->success) {
    ScheduleNotify();
  }
  std::move(callback).Run(std::move(result));
}

void MahoSettingsPageHandler::GetVaultPolicyStatus(
    GetVaultPolicyStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultPolicyStatus());
    return;
  }
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
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultPolicyStatus());
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kPolicyUpdate,
      CreatePasswordAuthenticator(profile_), u"Change password access policy",
      base::BindOnce(
          &MahoSettingsPageHandler::OnReauthSetVaultPolicyComplete,
          weak_factory_.GetWeakPtr(), policy, item_id, origin, expires_at,
          std::move(callback)));
}

void MahoSettingsPageHandler::OnReauthSetVaultPolicyComplete(
    maho_settings::mojom::VaultAgentPolicy policy,
    const std::optional<std::string>& item_id,
    const std::optional<std::string>& origin,
    const std::optional<std::string>& expires_at,
    SetVaultPolicyCallback callback,
    bool success) {
  if (!success || !maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultPolicyStatus());
    return;
  }
  auto status = maho_settings_password_helpers::SetVaultPolicyInCore(
      PasswordAuthorizationProfileKey(profile_), policy, item_id, origin,
      expires_at);
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
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(
        maho_settings_password_helpers::BuildUnavailableVaultAuditPage(cursor));
    return;
  }
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
  if (!maho::IsPasswordManagerAllowedForProfile(profile_)) {
    std::move(callback).Run(maho_settings_password_helpers::
                                BuildUnavailablePasswordImportOperationResult());
    return;
  }
  maho::passwords::MahoPasswordAuthorizationService::Get()->Authorize(
      PasswordAuthorizationProfileKey(profile_),
      maho::passwords::MahoPasswordAuthorizationService::kAllOriginsScope,
      maho::passwords::PasswordAuthorizationAction::kImportCommit,
      CreatePasswordAuthenticator(profile_), u"Import passwords",
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             std::string preview_token, CommitPasswordImportCallback callback,
             bool success) {
            if (!handler || !success ||
                !maho::IsPasswordManagerAllowedForProfile(handler->profile_)) {
              std::move(callback).Run(
                  maho_settings_password_helpers::
                      BuildUnavailablePasswordImportOperationResult());
              return;
            }
            auto result =
                maho_settings_password_helpers::CommitPasswordImportInCore(
                    PasswordAuthorizationProfileKey(handler->profile_),
                    handler->password_import_job_, preview_token);
            if (result->success && result->committed > 0) {
              handler->ScheduleNotify();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), preview_token, std::move(callback)));
}

void MahoSettingsPageHandler::FileSelected(const ui::SelectedFileInfo& file,
                                           int index) {
  if (download_directory_select_callback_) {
    OnSelectedProfileDownloadDirectoryChosen(file.file_path);
    return;
  }
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
  if (download_directory_select_callback_) {
    OnSelectedProfileDownloadDirectoryChosen(base::FilePath());
    return;
  }
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
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  BrowserWindowInterface* browser =
      collection ? collection->GetLastActiveBrowser() : nullptr;
  if (browser) {
    chrome::ShowExtensions(browser);
  }
}

void MahoSettingsPageHandler::OpenChromiumSettingsPage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  BrowserWindowInterface* browser =
      collection ? collection->GetLastActiveBrowser() : nullptr;
  if (browser) {
    chrome::ShowSettings(browser);
  }
}

void MahoSettingsPageHandler::OpenMigrationDialog() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&maho::DetectInstalledBrowsers),
      base::BindOnce(&MahoSettingsPageHandler::OnOpenMigrationDialogBrowsersDetected,
                     weak_factory_.GetWeakPtr()));
}

void MahoSettingsPageHandler::OnOpenMigrationDialogBrowsersDetected(
    std::vector<maho::DetectedBrowser> browsers) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Resolve parent widget at dialog-show time, not before the async hop,
  // to avoid a dangling Widget* if the window closes during detection.
  views::Widget* parent_widget = nullptr;
  if (host_web_contents_) {
    parent_widget = views::Widget::GetWidgetForNativeWindow(
        host_web_contents_->GetTopLevelNativeWindow());
  }
  if (!parent_widget) {
    ProfileBrowserCollection* collection =
        ProfileBrowserCollection::GetForProfile(profile_);
    BrowserWindowInterface* bwi =
        collection ? collection->GetLastActiveBrowser() : nullptr;
    if (bwi && bwi->GetWindow()) {
      parent_widget = views::Widget::GetWidgetForNativeWindow(
          bwi->GetWindow()->GetNativeWindow());
    }
  }

  maho::MahoMigrationDialogView::Show(
      parent_widget, profile_, std::move(browsers),
      base::BindOnce(
          [](bool was_cancelled, uint32_t imported_items_bitmask) {
            VLOG(1) << "MahoSettingsPageHandler: Migration dialog closed, cancelled="
                    << was_cancelled << " bitmask=" << imported_items_bitmask;
          }));
}

void MahoSettingsPageHandler::SetBYOKKey(const std::string& provider,
                                         const std::string& key,
                                         SetBYOKKeyCallback callback) {
  if (key.empty()) {
    ClearBYOKKey(provider, std::move(callback));
    return;
  }
  if (!encryptor_) {
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
    MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
    std::move(callback).Run(true);
  } else if (provider == "anthropic") {
    prefs_->SetString(ai::kByokAnthropicEncryptedB64, encrypted_b64);
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
    std::move(callback).Run(true);
  } else if (provider == "openai-compatible" || provider == "custom") {
    prefs_->SetString(maho::ai_prefs::kByokOpenAICompatibleEncryptedB64, encrypted_b64);
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
    std::move(callback).Run(true);
  } else {
    LOG(ERROR) << "[SetBYOKKey] unknown provider: " << provider;
    std::move(callback).Run(false);
  }
}

void MahoSettingsPageHandler::SetAIProviderOAuthClientId(
    const std::string& provider,
    const std::string& client_id,
    SetAIProviderOAuthClientIdCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::ai_oauth::IsOAuthSupported(provider)) {
    std::move(callback).Run(false);
    return;
  }
  maho::ai_oauth::SetOAuthClientId(prefs_, provider, client_id);
  std::move(callback).Run(true);
}

void MahoSettingsPageHandler::SignInToAIProvider(
    const std::string& provider,
    SignInToAIProviderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!encryptor_) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  Browser* peek_host_browser =
      host_web_contents_
          ? static_cast<Browser*>(GlobalBrowserCollection::GetInstance()
                                      ->FindBrowserWithTab(host_web_contents_))
          : nullptr;
  if (!maho::IsPeekEligible(peek_host_browser) ||
      peek_host_browser->GetProfile() != profile_ ||
      host_web_contents_->GetBrowserContext() != profile_) {
    peek_host_browser = nullptr;
  }

  maho::ai_oauth::StartProviderOAuth(
      profile_, peek_host_browser, provider,
      maho::ai_oauth::GetOAuthClientId(prefs_, provider),
      base::BindOnce(&MahoSettingsPageHandler::OnProviderOAuthCompleted,
                     weak_factory_.GetWeakPtr(), provider,
                     std::move(callback)));
}

void MahoSettingsPageHandler::OnProviderOAuthCompleted(
    const std::string& provider,
    SignInToAIProviderCallback callback,
    bool ok,
    maho::ai_oauth::ProviderTokens tokens,
    const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ok) {
    std::move(callback).Run(false, error_message);
    return;
  }
  if (!encryptor_) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }
  if (!maho::ai_oauth::StoreProviderOAuthTokens(prefs_, *encryptor_, provider,
                                                tokens)) {
    std::move(callback).Run(false, "Could not store the provider credential.");
    return;
  }
  prefs_->SetString(ai::kProvider, provider);
  prefs_->CommitPendingWrite();
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  std::move(callback).Run(true, std::string());
}

void MahoSettingsPageHandler::SignOutOfAIProvider(
    const std::string& provider,
    SignOutOfAIProviderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::ai_oauth::IsOAuthSupported(provider)) {
    std::move(callback).Run(false);
    return;
  }
  maho::ai_oauth::ClearProviderOAuth(prefs_, provider);
  MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(provider);
  MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
  std::move(callback).Run(true);
}

void MahoSettingsPageHandler::ClearBYOKKey(const std::string& provider,
                                           ClearBYOKKeyCallback callback) {
  if (provider == "openai") {
    prefs_->SetString(ai::kByokOpenAIEncryptedB64, "");
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(provider);
    MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
    std::move(callback).Run(true);
  } else if (provider == "anthropic") {
    prefs_->SetString(ai::kByokAnthropicEncryptedB64, "");
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(provider);
    MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
    std::move(callback).Run(true);
  } else if (provider == "openai-compatible" || provider == "custom") {
    prefs_->SetString(maho::ai_prefs::kByokOpenAICompatibleEncryptedB64, "");
    prefs_->CommitPendingWrite();
    MahoUnifiedAgentAdapter::ClearActiveBYOKKeys(provider);
    MahoUnifiedAgentAdapter::NotifyAISettingsChanged(prefs_);
    std::move(callback).Run(true);
  } else {
    LOG(ERROR) << "[ClearBYOKKey] unknown provider: " << provider;
    std::move(callback).Run(false);
  }
}

void MahoSettingsPageHandler::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
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

  if (!encryptor_) {
    std::move(callback).Run(false, "Encryption not ready. Please try again.");
    return;
  }

  Browser* peek_host_browser =
      host_web_contents_
          ? static_cast<Browser*>(GlobalBrowserCollection::GetInstance()
                                      ->FindBrowserWithTab(host_web_contents_))
          : nullptr;
  if (!maho::IsPeekEligible(peek_host_browser) ||
      peek_host_browser->GetProfile() != profile_ ||
      host_web_contents_->GetBrowserContext() != profile_) {
    peek_host_browser = nullptr;
  }

  // Shares OnRelayLoginCompleted with password login: an established session
  // must refresh account status and billing prefs the same way regardless of
  // which credential produced it.
  maho::auth::StartGoogleSignIn(
      profile_, peek_host_browser, *encryptor_,
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

  if (!encryptor_) {
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
  if (encryptor_) {
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

  if (!encryptor_) {
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

  if (http_status == 401 && !refreshed_once && encryptor_) {
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
              if (!success || !tokens || !handler->encryptor_) {
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
              maho::auth::PreserveRelayIdentityMetadata(handler->prefs_,
                                                        &params);
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

  if (!encryptor_) {
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

  if (!encryptor_) {
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

  // Single "Maho AI Pay-as-you-go credits" pay-what-you-want product: the buyer
  // chooses the amount in the LemonSqueezy checkout, so pack_size_usd no longer
  // selects a fixed pack. The relay webhook credits the amount actually paid.
  // user_id is attached as checkout custom data (ADR 0010 §Webhook Handling) so
  // the relay can attribute the purchase; empty user_id yields an empty URL.
  const std::string user_id =
      profile_ && profile_->GetPrefs()
          ? profile_->GetPrefs()->GetString(maho::account_prefs::kRelayUserId)
          : std::string();
  std::move(callback).Run(maho::webui::BuildPaygCreditsCheckoutUrl(user_id));
}

void MahoSettingsPageHandler::GetSubscriptionCheckoutUrl(
    const std::string& tier,
    GetSubscriptionCheckoutUrlCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::string_view checkout_url_template =
      maho::webui::SubscriptionCheckoutUrlForTier(tier);
  const std::string user_id =
      profile_ && profile_->GetPrefs()
          ? profile_->GetPrefs()->GetString(maho::account_prefs::kRelayUserId)
          : std::string();
  std::move(callback).Run(
      maho::webui::BuildSubscriptionCheckoutUrl(checkout_url_template, user_id));
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

void MahoSettingsPageHandler::MailTestConnection(
    const std::string& request_json,
    MailTestConnectionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  service->TestConnection(
      request_json,
      base::BindOnce(
          [](MailTestConnectionCallback cb, bool ok, std::string result) {
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

void MahoSettingsPageHandler::MailBeginOAuth(
    const std::string& provider,
    MailBeginOAuthCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  const std::string options_json =
      provider == "gmail"
          ? maho::auth::BuildMailOAuthStartOptionsJson(profile_->GetPrefs())
          : "{}";
  service->OAuthLoopbackSignIn(
      provider, options_json,
      base::BindOnce([](MailBeginOAuthCallback cb, bool success,
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
namespace {

// Canonical calendar preference rows. The Mail calendar UI
// (resources/maho_mail/react/hooks/useCalendarClient.ts and the event dialogs)
// reads these exact app-setting rows and the mail-core migration seeds them,
// so Settings composes its calendar pane from the same rows. The previous
// private `calendar_prefs` blob was seeded by nobody and read by nobody else,
// so every toggle here was invisible to the calendar it configured.
constexpr char kCalendarHideWeekendsKey[] = "calendar.hide_weekends";
constexpr char kCalendarWeekStartKey[] = "calendar.week_start";
constexpr char kCalendarWorkingHoursStartKey[] = "calendar.working_hours_start";
constexpr char kCalendarWorkingHoursEndKey[] = "calendar.working_hours_end";
constexpr char kCalendarDefaultReminderKey[] =
    "calendar.default_reminder_minutes";

constexpr const char* kCalendarPrefKeys[] = {
    kCalendarHideWeekendsKey, kCalendarWeekStartKey,
    kCalendarWorkingHoursStartKey, kCalendarWorkingHoursEndKey,
    kCalendarDefaultReminderKey};

using CalendarPrefRows =
    base::RefCountedData<std::map<std::string, std::string>>;

std::string CalendarRowOr(const std::map<std::string, std::string>& rows,
                          const std::string& key,
                          const std::string& fallback) {
  const auto row = rows.find(key);
  return row == rows.end() || row->second.empty() ? fallback : row->second;
}

int CalendarRowIntOr(const std::map<std::string, std::string>& rows,
                     const std::string& key,
                     int fallback) {
  int parsed = 0;
  return base::StringToInt(CalendarRowOr(rows, key, std::string()), &parsed)
             ? parsed
             : fallback;
}

// Always answers with a complete object so the pane can never receive a null
// or partial payload, whatever the profile has stored.
std::string SerializeCalendarPrefs(
    const std::map<std::string, std::string>& rows) {
  base::DictValue prefs;
  prefs.Set("hide_weekends",
            CalendarRowOr(rows, kCalendarHideWeekendsKey, "false") == "true");
  prefs.Set("week_start", CalendarRowIntOr(rows, kCalendarWeekStartKey, 0));
  prefs.Set("working_hours_start",
            CalendarRowOr(rows, kCalendarWorkingHoursStartKey, "09:00"));
  prefs.Set("working_hours_end",
            CalendarRowOr(rows, kCalendarWorkingHoursEndKey, "18:00"));
  prefs.Set("default_reminder_minutes",
            CalendarRowIntOr(rows, kCalendarDefaultReminderKey, 10));
  std::string json;
  base::JSONWriter::Write(base::Value(std::move(prefs)), &json);
  return json;
}

std::optional<std::string> DecodeCalendarSettingRead(
    const std::string& result_json) {
  if (result_json.empty()) {
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_string()) {
    return std::nullopt;
  }
  return parsed->GetString();
}

}  // namespace

void MahoSettingsPageHandler::MailGetCalendarPrefs(
    MailGetCalendarPrefsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(false, "Mail service unavailable");
    return;
  }
  auto rows = base::MakeRefCounted<CalendarPrefRows>();
  base::RepeatingClosure row_done = base::BarrierClosure(
      static_cast<int>(std::size(kCalendarPrefKeys)),
      base::BindOnce(
          [](scoped_refptr<CalendarPrefRows> rows,
             MailGetCalendarPrefsCallback callback) {
            std::move(callback).Run(true, SerializeCalendarPrefs(rows->data));
          },
          rows, std::move(callback)));
  for (const char* key : kCalendarPrefKeys) {
    service->GetAppSetting(
        key,
        base::BindOnce(
            [](scoped_refptr<CalendarPrefRows> rows, std::string key,
               base::RepeatingClosure row_done, bool ok, std::string result) {
              std::optional<std::string> value =
                  ok ? DecodeCalendarSettingRead(result) : std::nullopt;
              if (value) {
                rows->data[std::move(key)] = std::move(*value);
              }
              row_done.Run();
            },
            rows, std::string(key), row_done));
  }
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
  std::optional<base::Value> parsed =
      base::JSONReader::Read(prefs_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(false);
    return;
  }
  const base::DictValue& prefs = parsed->GetDict();
  std::vector<std::pair<std::string, std::string>> writes;
  if (std::optional<bool> hide_weekends = prefs.FindBool("hide_weekends")) {
    writes.emplace_back(kCalendarHideWeekendsKey,
                        *hide_weekends ? "true" : "false");
  }
  if (std::optional<int> week_start = prefs.FindInt("week_start")) {
    writes.emplace_back(kCalendarWeekStartKey,
                        base::NumberToString(*week_start));
  }
  if (const std::string* start = prefs.FindString("working_hours_start")) {
    writes.emplace_back(kCalendarWorkingHoursStartKey, *start);
  }
  if (const std::string* end = prefs.FindString("working_hours_end")) {
    writes.emplace_back(kCalendarWorkingHoursEndKey, *end);
  }
  if (std::optional<int> reminder = prefs.FindInt("default_reminder_minutes")) {
    writes.emplace_back(kCalendarDefaultReminderKey,
                        base::NumberToString(*reminder));
  }
  if (writes.empty()) {
    std::move(callback).Run(false);
    return;
  }
  auto all_ok = base::MakeRefCounted<base::RefCountedData<bool>>(true);
  base::RepeatingClosure write_done = base::BarrierClosure(
      static_cast<int>(writes.size()),
      base::BindOnce(
          [](scoped_refptr<base::RefCountedData<bool>> all_ok,
             MailSetCalendarPrefsCallback callback) {
            std::move(callback).Run(all_ok->data);
          },
          all_ok, std::move(callback)));
  for (const auto& [key, value] : writes) {
    service->SetAppSetting(
        key, value,
        base::BindOnce(
            [](scoped_refptr<base::RefCountedData<bool>> all_ok,
               base::RepeatingClosure write_done, bool ok, std::string) {
              if (!ok) {
                all_ok->data = false;
              }
              write_done.Run();
            },
            all_ok, write_done));
  }
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
namespace {

maho_settings::mojom::MailNotificationPermission ToMojoPermission(
    maho::MailOsNotificationPermission permission) {
  switch (permission) {
    case maho::MailOsNotificationPermission::kNotDetermined:
      return maho_settings::mojom::MailNotificationPermission::kNotDetermined;
    case maho::MailOsNotificationPermission::kPromptPending:
      return maho_settings::mojom::MailNotificationPermission::kPromptPending;
    case maho::MailOsNotificationPermission::kDenied:
      return maho_settings::mojom::MailNotificationPermission::kDenied;
    case maho::MailOsNotificationPermission::kGranted:
      return maho_settings::mojom::MailNotificationPermission::kGranted;
    case maho::MailOsNotificationPermission::kUnsupported:
      return maho_settings::mojom::MailNotificationPermission::kUnsupported;
  }
}

maho_settings::mojom::MailBehaviorSnapshotPtr ToMojoBehaviorSnapshot(
    const maho::MailBehaviorSnapshot& snapshot,
    maho::MailOsNotificationPermission permission) {
  auto result = maho_settings::mojom::MailBehaviorSnapshot::New();
  result->version = snapshot.version;
  result->revision = snapshot.revision;
  result->value_json = maho::SerializeMailBehaviorSnapshot(snapshot);
  result->notification_permission = ToMojoPermission(permission);
  return result;
}

maho_settings::mojom::MailBehaviorUpdateStatus ToMojoUpdateStatus(
    maho::MailBehaviorUpdateStatus status) {
  switch (status) {
    case maho::MailBehaviorUpdateStatus::kApplied:
      return maho_settings::mojom::MailBehaviorUpdateStatus::kApplied;
    case maho::MailBehaviorUpdateStatus::kConflict:
      return maho_settings::mojom::MailBehaviorUpdateStatus::kConflict;
    case maho::MailBehaviorUpdateStatus::kRejected:
      return maho_settings::mojom::MailBehaviorUpdateStatus::kRejected;
    case maho::MailBehaviorUpdateStatus::kInvalid:
      return maho_settings::mojom::MailBehaviorUpdateStatus::kInvalid;
  }
}

}  // namespace

void MahoSettingsPageHandler::MailGetBehaviorPrefs(
    MailGetBehaviorPrefsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    std::move(callback).Run(
        false, ToMojoBehaviorSnapshot(
                   {}, maho::MailOsNotificationPermission::kUnsupported));
    return;
  }
  service->GetBehaviorSnapshot(base::BindOnce(
      [](MailGetBehaviorPrefsCallback callback, bool ok,
         maho::MailBehaviorSnapshot snapshot) {
        maho::GetMahoMailNotificationPermission(base::BindOnce(
            [](MailGetBehaviorPrefsCallback callback, bool ok,
               maho::MailBehaviorSnapshot snapshot,
               maho::MailOsNotificationPermission permission) {
              std::move(callback).Run(
                  ok, ToMojoBehaviorSnapshot(snapshot, permission));
            },
            std::move(callback), ok, std::move(snapshot)));
      },
      std::move(callback)));
}

void MahoSettingsPageHandler::MailSetBehaviorPref(
    uint64_t expected_revision,
    const std::string& key,
    const std::string& value,
    MailSetBehaviorPrefCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile_);
  if (!service) {
    auto result = maho_settings::mojom::MailBehaviorUpdateResult::New();
    result->status = maho_settings::mojom::MailBehaviorUpdateStatus::kRejected;
    result->snapshot = ToMojoBehaviorSnapshot(
        {}, maho::MailOsNotificationPermission::kUnsupported);
    std::move(callback).Run(std::move(result));
    return;
  }
  service->UpdateBehavior(
      expected_revision, key, value,
      base::BindOnce(
          [](base::WeakPtr<MahoSettingsPageHandler> handler,
             MailSetBehaviorPrefCallback callback,
             maho::MailBehaviorUpdateResult update) {
            auto result =
                maho_settings::mojom::MailBehaviorUpdateResult::New();
            result->status = ToMojoUpdateStatus(update.status);
            maho::GetMahoMailNotificationPermission(base::BindOnce(
                [](base::WeakPtr<MahoSettingsPageHandler> handler,
                   MailSetBehaviorPrefCallback callback,
                   maho::MailBehaviorUpdateResult update,
                   maho_settings::mojom::MailBehaviorUpdateResultPtr result,
                   maho::MailOsNotificationPermission permission) {
                  result->snapshot =
                      ToMojoBehaviorSnapshot(update.snapshot, permission);
                  if (update.status ==
                          maho::MailBehaviorUpdateStatus::kApplied &&
                      handler && handler->page_.is_bound()) {
                    handler->page_->OnMailBehaviorChanged();
                  }
                  std::move(callback).Run(std::move(result));
                },
                handler, std::move(callback), std::move(update),
                std::move(result)));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSettingsPageHandler::MailRequestNotificationPermission(
    MailRequestNotificationPermissionCallback callback) {
  maho::RequestMahoMailNotificationPermission(base::BindOnce(
      [](MailRequestNotificationPermissionCallback callback,
         maho::MailOsNotificationPermission permission) {
        std::move(callback).Run(ToMojoPermission(permission));
      },
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
  } else {
    page_->OnMailSignaturesChanged();
    page_->OnMailTemplatesChanged();
    page_->OnMailLabelsChanged();
    page_->OnMailRulesChanged();
    page_->OnMailSecurityChanged();
    page_->OnMailBehaviorChanged();
  }
}

void MahoSettingsPageHandler::OnCalendar(const std::string& account_id,
                                         const std::string& payload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_->OnMailCalendarChanged();
}
