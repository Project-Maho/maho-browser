// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <string_view>

#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"

namespace {

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
     prefs::kEnableQuietNotificationPermissionUi, SettingMapping::Type::kBool},
};

const SettingMapping* FindMapping(const std::string& maho_key) {
  for (const auto& m : kSettingsMap) {
    if (maho_key == m.maho_key) {
      return &m;
    }
  }
  return nullptr;
}

PrefService* GetPrefServiceForKey(Profile* profile, const std::string& maho_key) {
  if (maho_key == kHardwareAccelerationMahoKey) {
    return g_browser_process->local_state();
  }
  return profile->GetPrefs();
}

}  // namespace

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
        (std::string_view(m.pref_path) == prefs::kHardwareAccelerationModeEnabled &&
         local_state_)
            ? &local_state_pref_registrar_
            : &pref_registrar_;
    registrar->Add(m.pref_path,
                   base::BindRepeating(&MahoSettingsPageHandler::OnPrefChanged,
                                       weak_factory_.GetWeakPtr()));
  }

}

MahoSettingsPageHandler::~MahoSettingsPageHandler() = default;

// static
const char* MahoSettingsPageHandler::GetPrefPath(
    const std::string& maho_key) {
  const SettingMapping* m = FindMapping(maho_key);
  return m ? m->pref_path : nullptr;
}

std::string MahoSettingsPageHandler::ReadSetting(
    const std::string& maho_key) {
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
  AppendFfiSettings(settings);
  std::move(callback).Run(std::move(settings));
}

void MahoSettingsPageHandler::SetSetting(const std::string& key,
                                          const std::string& value,
                                          SetSettingCallback callback) {
  if (HandleFfiSetting(key, value)) {
    std::move(callback).Run(true);
    NotifySettingsChanged();
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
      prefs->SetInteger(
          m->pref_path,
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
  NotifySettingsChanged();
}

void MahoSettingsPageHandler::NotifySettingsChanged() {
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
  AppendFfiSettings(settings);

  page_->SettingsChanged(std::move(settings));
}

bool MahoSettingsPageHandler::HandleFfiSetting(
    const std::string& key, const std::string& value) {
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
  } else if (key == "appearance.custom_chrome_css") {
    maho_core_set_custom_chrome_css(core, value.c_str());
    handled = true;
  } else if (key == "appearance.sidebar_width") {
    int width = 0;
    if (!base::StringToInt(value, &width)) {
      return true;  // Handled but invalid.
    }
    width = std::clamp(width, 180, 400);
    base::DictValue settings;
    base::DictValue appearance;
    appearance.Set("sidebarWidth", width);
    settings.Set("appearance", std::move(appearance));
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
    add("appearance.sidebar_width", base::NumberToString(static_cast<int>(*width)));
  }

  if (auto transparency = appearance->FindBool("windowTransparency")) {
    add("appearance.window_transparency", *transparency ? "true" : "false");
  }

  if (const std::string* css = appearance->FindString("customChromeCss")) {
    add("appearance.custom_chrome_css", *css);
  } else {
    add("appearance.custom_chrome_css", "");
  }

  if (const base::DictValue* color = appearance->FindDict("accentColor")) {
    std::string color_json;
    base::JSONWriter::Write(base::Value(color->Clone()), &color_json);
    add("appearance.accent_color", color_json);
  } else {
    add("appearance.accent_color", "");
  }
}
