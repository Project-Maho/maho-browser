// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"

#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/security_state/core/security_state.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/permissions/permission_decision_auto_blocker.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"

namespace maho {

namespace internal {

std::u16string ToUtf16(const std::string& text) {
  return base::UTF8ToUTF16(text);
}

std::u16string BuildAccessibleName(const std::u16string& title,
                                   const std::u16string& subtitle) {
  if (subtitle.empty()) {
    return title;
  }
  return title + u" — " + subtitle;
}

std::u16string DescribeSecurityLevel(int security_level) {
  switch (static_cast<security_state::SecurityLevel>(security_level)) {
    case security_state::NONE:
      return u"Security state unavailable";
    case security_state::SECURE:
      return u"Secure";
    case security_state::DANGEROUS:
      return u"Dangerous";
    case security_state::WARNING:
      return u"Warning";
    case security_state::SECURITY_LEVEL_COUNT:
      break;
  }
  return u"Security state unavailable";
}

MahoLocationBarUtilityPanelAction MakeAction(
    std::u16string title,
    std::u16string subtitle,
    MahoLocationBarUtilityPanelDataState data_state,
    bool enabled) {
  MahoLocationBarUtilityPanelAction action;
  action.title = std::move(title);
  action.subtitle = std::move(subtitle);
  action.accessible_name = BuildAccessibleName(action.title, action.subtitle);
  action.data_state = data_state;
  action.enabled = enabled;
  return action;
}

content::WebContents* GetActiveWebContents(Browser* browser);
const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
}

std::optional<base::Value> ReadCoreSettings() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }

  char* json_str = maho_core_get_settings(core);
  if (!json_str) {
    return std::nullopt;
  }

  std::string json(json_str);
  maho_string_free(json_str);
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

MahoLocationBarUtilityPanelAction BuildDeveloperModeAction(
    const base::Value* parsed_settings) {
  if (!parsed_settings || !parsed_settings->is_dict()) {
    return MakeAction(u"Developer mode", u"Developer mode unavailable",
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  const base::DictValue& root = parsed_settings->GetDict();
  const base::DictValue* advanced = root.FindDict("advanced");
  const std::optional<bool> developer_mode =
      advanced ? advanced->FindBool("developer_mode") : std::nullopt;
  if (!developer_mode.has_value()) {
    return MakeAction(u"Developer mode", u"Developer mode unavailable",
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  return MakeAction(u"Developer mode",
                    *developer_mode ? u"Enabled" : u"Disabled",
                    MahoLocationBarUtilityPanelDataState::kLive, false);
}

std::u16string DescribeAutoPipState(ContentSetting setting, bool embargoed) {
  if (embargoed) {
    return u"Blocked for this site";
  }

  switch (setting) {
    case CONTENT_SETTING_ALLOW:
      return u"Allowed on every visit";
    case CONTENT_SETTING_BLOCK:
      return u"Blocked for this site";
    case CONTENT_SETTING_ASK:
      return u"Ask to allow";
    default:
      return u"Automatic PiP unavailable";
  }
}

MahoLocationBarUtilityPanelAction BuildAutoPipActionForContext(
    content::WebContents* web_contents,
    Profile* profile,
    bool unsupported_without_context) {
  if (!web_contents || !profile) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      unsupported_without_context
                          ? u"Unsupported in this window"
                          : u"No active page available",
                      unsupported_without_context
                          ? MahoLocationBarUtilityPanelDataState::kUnsupported
                          : MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  if (!url.is_valid()) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Current page unavailable",
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  if (!settings_map) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Auto PiP settings unavailable",
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  ContentSetting setting = settings_map->GetContentSetting(
      url, GURL(), ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  bool embargoed = false;

  permissions::PermissionDecisionAutoBlocker* auto_blocker =
      PermissionDecisionAutoBlockerFactory::GetForProfile(profile);
  if (setting == CONTENT_SETTING_ASK && auto_blocker) {
    embargoed = auto_blocker->IsEmbargoed(
        url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
    if (embargoed) {
      setting = CONTENT_SETTING_BLOCK;
    }
  }

  return MakeAction(u"Automatic Picture-In-Picture",
                    DescribeAutoPipState(setting, embargoed),
                    MahoLocationBarUtilityPanelDataState::kLive, false);
}

content::WebContents* GetActiveWebContents(Browser* browser) {
  if (!browser) {
    return nullptr;
  }

  TabStripModel* tab_strip_model = browser->tab_strip_model();
  if (!tab_strip_model) {
    return nullptr;
  }

  const int active_index = tab_strip_model->active_index();
  if (active_index == TabStripModel::kNoTab ||
      !tab_strip_model->ContainsIndex(active_index)) {
    return nullptr;
  }

  return tab_strip_model->GetWebContentsAt(active_index);
}

MahoLocationBarUtilityPanelAction BuildAutoPipAction(LocationBar* location_bar) {
  if (!location_bar) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Unsupported in this window",
                      MahoLocationBarUtilityPanelDataState::kUnsupported,
                      false);
  }

  return BuildAutoPipActionForContext(location_bar->GetWebContents(),
                                      location_bar->GetProfile(), false);
}

MahoLocationBarUtilityPanelAction BuildAutoPipAction(Browser* browser) {
  if (!browser) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Unsupported in this window",
                      MahoLocationBarUtilityPanelDataState::kUnsupported,
                      false);
  }

  return BuildAutoPipActionForContext(GetActiveWebContents(browser),
                                      browser->profile(), false);
}

MahoLocationBarUtilityPanelAction BuildExtensionsSummaryAction(
    const std::vector<MahoLocationBarUtilityPanelAction>& extensions) {
  if (extensions.size() == 1 && extensions[0].title == u"Extensions") {
    return MakeAction(extensions[0].title, extensions[0].subtitle,
                      extensions[0].data_state, false);
  }

  return MakeAction(u"Extensions",
                    base::NumberToString16(extensions.size()) + u" installed",
                    MahoLocationBarUtilityPanelDataState::kLive, false);
}

MahoLocationBarUtilityPanelFooter BuildFooter(LocationBar* location_bar) {
  MahoLocationBarUtilityPanelFooter footer;
  footer.title = u"Security state unavailable";
  footer.subtitle = u"Page security";
  footer.data_state = MahoLocationBarUtilityPanelDataState::kUnavailable;
  footer.security_level = static_cast<int>(security_state::NONE);
  footer.primary_action_label = u"Close";
  footer.close_after_primary_action = true;

  if (!location_bar) {
    return footer;
  }

  LocationBarModel* model = location_bar->GetLocationBarModel();
  if (!model) {
    return footer;
  }

  footer.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  footer.security_level = static_cast<int>(model->GetSecurityLevel());
  footer.title = DescribeSecurityLevel(footer.security_level);
  return footer;
}

MahoLocationBarUtilityPanelFooter BuildFooter(Browser* browser) {
  MahoLocationBarUtilityPanelFooter footer;
  footer.title = u"Security state unavailable";
  footer.subtitle = u"Page security";
  footer.data_state = MahoLocationBarUtilityPanelDataState::kUnavailable;
  footer.security_level = static_cast<int>(security_state::NONE);
  footer.primary_action_label = u"Close";
  footer.close_after_primary_action = true;

  if (!browser) {
    return footer;
  }

  BrowserWindow* window = browser->window();
  if (!window) {
    return footer;
  }

  LocationBar* location_bar = window->GetLocationBar();
  if (!location_bar) {
    return footer;
  }

  LocationBarModel* model = location_bar->GetLocationBarModel();
  if (!model) {
    return footer;
  }

  footer.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  footer.security_level = static_cast<int>(model->GetSecurityLevel());
  footer.title = DescribeSecurityLevel(footer.security_level);

  return footer;
}

void PopulateCommonModelData(MahoLocationBarUtilityPanelModel& model) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    model.extensions.push_back(MakeAction(
        u"Extensions", u"Core unavailable",
        MahoLocationBarUtilityPanelDataState::kUnavailable, false));
  } else {
    char* json_str = maho_core_get_installed_extensions(core);
    if (!json_str) {
      model.extensions.push_back(MakeAction(
          u"Extensions", u"Extension list unavailable",
          MahoLocationBarUtilityPanelDataState::kUnavailable, false));
    } else {
      std::string json(json_str);
      maho_string_free(json_str);
      model.extensions = BuildInstalledExtensions(json);
    }
  }

  std::optional<base::Value> parsed_settings = ReadCoreSettings();
  model.quick_actions.push_back(BuildExtensionsSummaryAction(model.extensions));
  model.quick_actions.push_back(
      BuildDeveloperModeAction(parsed_settings ? &*parsed_settings : nullptr));

  model.settings.push_back(
      MakeAction(u"Site settings", u"Permissions for this site",
                 MahoLocationBarUtilityPanelDataState::kLive, false));
  model.settings.push_back(MakeAction(
      u"Cookies & site data", u"Manage stored data",
      MahoLocationBarUtilityPanelDataState::kLive, false));
}

std::vector<MahoLocationBarUtilityPanelAction> BuildInstalledExtensions(
    const std::string& json) {
  std::vector<MahoLocationBarUtilityPanelAction> items;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    items.push_back(MakeAction(u"Extensions", u"Extension list unavailable",
                               MahoLocationBarUtilityPanelDataState::kUnavailable,
                               false));
    return items;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    const std::string* name = dict->FindString("name");
    const std::string* version = dict->FindString("version");
    const std::string* id = dict->FindString("id");
    const bool enabled = dict->FindBool("enabled").value_or(true);

    std::u16string title = ToUtf16(name ? *name : (id ? *id : std::string()));
    if (title.empty()) {
      title = u"Unnamed extension";
    }

    std::u16string subtitle = enabled ? u"Enabled" : u"Disabled";
    if (version && !version->empty()) {
      subtitle = u"v" + ToUtf16(*version) + u" · " + subtitle;
    }

    items.push_back(MakeAction(std::move(title), std::move(subtitle),
                               MahoLocationBarUtilityPanelDataState::kLive,
                               false));
  }

  if (items.empty()) {
    items.push_back(MakeAction(u"Extensions", u"No installed extensions",
                               MahoLocationBarUtilityPanelDataState::kLive,
                               false));
  }

  return items;
}

}  // namespace internal

MahoLocationBarUtilityPanelAction::MahoLocationBarUtilityPanelAction() = default;
MahoLocationBarUtilityPanelAction::MahoLocationBarUtilityPanelAction(
    const MahoLocationBarUtilityPanelAction&) = default;
MahoLocationBarUtilityPanelAction&
MahoLocationBarUtilityPanelAction::operator=(
    const MahoLocationBarUtilityPanelAction&) = default;
MahoLocationBarUtilityPanelAction::~MahoLocationBarUtilityPanelAction() = default;

MahoLocationBarUtilityPanelFooter::MahoLocationBarUtilityPanelFooter() = default;
MahoLocationBarUtilityPanelFooter::MahoLocationBarUtilityPanelFooter(
    const MahoLocationBarUtilityPanelFooter&) = default;
MahoLocationBarUtilityPanelFooter& MahoLocationBarUtilityPanelFooter::operator=(
    const MahoLocationBarUtilityPanelFooter&) = default;
MahoLocationBarUtilityPanelFooter::~MahoLocationBarUtilityPanelFooter() = default;

MahoLocationBarUtilityPanelModel::MahoLocationBarUtilityPanelModel() = default;
MahoLocationBarUtilityPanelModel::MahoLocationBarUtilityPanelModel(
    const MahoLocationBarUtilityPanelModel&) = default;
MahoLocationBarUtilityPanelModel& MahoLocationBarUtilityPanelModel::operator=(
    const MahoLocationBarUtilityPanelModel&) = default;
MahoLocationBarUtilityPanelModel::~MahoLocationBarUtilityPanelModel() = default;

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar) {
  MahoLocationBarUtilityPanelModel model;
  internal::PopulateCommonModelData(model);
  model.quick_actions.push_back(internal::BuildAutoPipAction(location_bar));

  model.footer = internal::BuildFooter(location_bar);
  return model;
}

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModelForBrowser(
    Browser* browser) {
  MahoLocationBarUtilityPanelModel model;
  internal::PopulateCommonModelData(model);
  model.quick_actions.push_back(internal::BuildAutoPipAction(browser));

  model.footer = internal::BuildFooter(browser);
  return model;
}

}  // namespace maho
