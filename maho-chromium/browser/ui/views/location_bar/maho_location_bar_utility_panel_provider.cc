// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_handler.h"
#include "maho/browser/ui/views/location_bar/maho_full_page_capture_client.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"

#include "base/functional/bind.h"
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/share/share_attempt.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/permissions/permission_decision_auto_blocker.h"
#include "chrome/browser/ssl/chrome_security_state_tab_helper.h"
#include "components/security_state/content/security_state_tab_helper.h"
#include "components/security_state/core/security_state.h"
#include "components/zoom/page_zoom.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/site_control/maho_page_info_ui.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace maho {

namespace internal {

constexpr char kExtensionsSymbolName[] = "puzzlepiece.extension";
constexpr char kDeveloperModeSymbolName[] = "hammer";
constexpr char kAutoPipSymbolName[] = "pip.enter";
constexpr char kShieldsSymbolName[] = "shield.lefthalf.filled";
constexpr char kShareSymbolName[] = "square.and.arrow.up";
constexpr char kMagicWandSymbolName[] = "wand.and.stars";
constexpr char kCameraSymbolName[] = "camera";
constexpr char kViewfinderSymbolName[] = "viewfinder";
constexpr char kZoomInSymbolName[] = "plus.magnifyingglass";
constexpr char kZoomOutSymbolName[] = "minus.magnifyingglass";
constexpr char kZoomResetSymbolName[] = "1.magnifyingglass";
constexpr char kAddChipSymbolName[] = "plus";
constexpr char kBlockAdsAndTrackersSymbolName[] = "shield.lefthalf.filled";

content::WebContents* GetActiveWebContents(Browser* browser);
const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents);
std::vector<MahoLocationBarUtilityPanelAction> BuildInstalledExtensions(
    const std::string& json,
    Browser* browser);
void OpenExtensionsSettingsPaneForBrowser(Browser* browser);

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
    std::string sf_symbol_name,
    MahoLocationBarUtilityPanelDataState data_state,
    bool enabled) {
  MahoLocationBarUtilityPanelAction action;
  action.title = std::move(title);
  action.subtitle = std::move(subtitle);
  action.accessible_name = BuildAccessibleName(action.title, action.subtitle);
  action.sf_symbol_name = std::move(sf_symbol_name);
  action.data_state = data_state;
  action.enabled = enabled;
  return action;
}

MahoLocationBarUtilityPanelAction MakeDisabledStubAction(
    std::u16string title,
    std::string sf_symbol_name) {
  return MakeAction(std::move(title), std::u16string(), std::move(sf_symbol_name),
                    MahoLocationBarUtilityPanelDataState::kUnavailable,
                    false);
}

MahoLocationBarUtilityPanelAction MakeAddExtensionChip(Browser* browser) {
  MahoLocationBarUtilityPanelAction action =
      MakeAction(u"+", u"", kAddChipSymbolName,
                 MahoLocationBarUtilityPanelDataState::kLive,
                 browser != nullptr);
  action.accessible_name = u"Add extension";
  action.close_after_activate = true;
  if (browser) {
    action.callback = base::BindRepeating(&OpenExtensionsSettingsPaneForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

std::optional<std::string> GetActiveOrigin(Browser* browser) {
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return std::nullopt;
  }

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  if (!url.is_valid() || url.is_empty()) {
    return std::nullopt;
  }

  const std::string origin = url::Origin::Create(url).Serialize();
  if (origin.empty()) {
    return std::nullopt;
  }

  return origin;
}

std::optional<base::Value> ReadCoreSettings(MahoCore* core) {
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

std::string ReadInstalledExtensionsJsonOrFallback() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }

  char* json_str = maho_core_get_installed_extensions(core);
  if (!json_str) {
    return std::string();
  }

  std::string json(json_str);
  maho_string_free(json_str);
  return json;
}

bool IsExtensionsFallbackAction(
    const MahoLocationBarUtilityPanelAction& action) {
  return action.title == u"Extensions" &&
         action.subtitle == u"No installed extensions";
}

bool HasActivePageForSiteControls(Browser* browser) {
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return false;
  }

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  return url.is_valid() && !url.is_empty();
}

void ShowShieldBubbleForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }

  MahoPageInfoUI::ShowShieldBubble(browser);
}

void ShowBoostBubbleForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }

  MahoPageInfoUI::ShowBoostBubble(browser);
}

std::string GetSecuritySFSymbolNameForLevel(int security_level) {
  switch (static_cast<security_state::SecurityLevel>(security_level)) {
    case security_state::SECURE:
      return "checkmark.shield";
    case security_state::WARNING:
      return "exclamationmark.shield";
    case security_state::DANGEROUS:
      return "xmark.shield";
    case security_state::NONE:
    case security_state::SECURITY_LEVEL_COUNT:
      break;
  }
  return "questionmark.shield";
}
const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
}

MahoLocationBarUtilityPanelAction BuildDeveloperModeAction(
    const base::Value* parsed_settings) {
  if (!parsed_settings || !parsed_settings->is_dict()) {
    return MakeAction(u"Developer mode", u"Developer mode unavailable",
                      kDeveloperModeSymbolName,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  const base::DictValue& root = parsed_settings->GetDict();
  const base::DictValue* advanced = root.FindDict("advanced");
  const std::optional<bool> developer_mode =
      advanced ? advanced->FindBool("developer_mode") : std::nullopt;
  if (!developer_mode.has_value()) {
    return MakeAction(u"Developer mode", u"Developer mode unavailable",
                      kDeveloperModeSymbolName,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  return MakeAction(u"Developer mode",
                    *developer_mode ? u"Enabled" : u"Disabled",
                    kDeveloperModeSymbolName,
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
                      kAutoPipSymbolName,
                      unsupported_without_context
                          ? MahoLocationBarUtilityPanelDataState::kUnsupported
                          : MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  if (!url.is_valid()) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Current page unavailable",
                      kAutoPipSymbolName,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  if (!settings_map) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Auto PiP settings unavailable",
                      kAutoPipSymbolName,
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
                    kAutoPipSymbolName,
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

void ShowSharingHubForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }

  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return;
  }

  sharing_hub::SharingHubBubbleController* controller =
      sharing_hub::SharingHubBubbleController::CreateOrGetFromWebContents(
          web_contents);
  if (!controller) {
    return;
  }

  controller->ShowBubble(share::ShareAttempt(web_contents));
}

MahoLocationBarUtilityPanelAction BuildShareAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Share",
      has_active_page ? u"Share this page" : u"Current page unavailable",
      kShareSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&ShowSharingHubForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

MahoLocationBarUtilityPanelAction BuildBoostAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Boost",
      has_active_page ? u"Customize this site" : u"Current page unavailable",
      kMagicWandSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&ShowBoostBubbleForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

bool IsOriginExceptedInSiteExceptions(const std::string& exceptions_json,
                                      const std::string& origin) {
  if (origin.empty()) {
    return false;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(exceptions_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }

  for (const auto& value : parsed->GetList()) {
    if (value.is_string() && value.GetString() == origin) {
      return true;
    }
  }

  return false;
}

bool IsAtMaxZoom(zoom::ZoomController* zoom_controller) {
  if (!zoom_controller) {
    return true;
  }
  double current_level = zoom_controller->GetZoomLevel();
  double default_level = zoom_controller->GetDefaultZoomLevel();
  std::vector<double> levels = zoom::PageZoom::PresetZoomLevels(default_level);
  if (levels.empty()) {
    return false;
  }
  return blink::ZoomValuesEqual(current_level, levels.back()) ||
         current_level > levels.back();
}

bool IsAtMinZoom(zoom::ZoomController* zoom_controller) {
  if (!zoom_controller) {
    return true;
  }
  double current_level = zoom_controller->GetZoomLevel();
  double default_level = zoom_controller->GetDefaultZoomLevel();
  std::vector<double> levels = zoom::PageZoom::PresetZoomLevels(default_level);
  if (levels.empty()) {
    return false;
  }
  return blink::ZoomValuesEqual(current_level, levels.front()) ||
         current_level < levels.front();
}

MahoLocationBarUtilityPanelAction BuildZoomInActionForState(
    bool has_active_page,
    bool at_max_zoom) {
  const bool enabled = has_active_page && !at_max_zoom;
  MahoLocationBarUtilityPanelAction action =
      MakeAction(u"Zoom In", std::u16string(), kZoomInSymbolName,
                 MahoLocationBarUtilityPanelDataState::kLive, enabled);
  action.close_after_activate = false;
  return action;
}

MahoLocationBarUtilityPanelAction BuildZoomOutActionForState(
    bool has_active_page,
    bool at_min_zoom) {
  const bool enabled = has_active_page && !at_min_zoom;
  MahoLocationBarUtilityPanelAction action =
      MakeAction(u"Zoom Out", std::u16string(), kZoomOutSymbolName,
                 MahoLocationBarUtilityPanelDataState::kLive, enabled);
  action.close_after_activate = false;
  return action;
}

MahoLocationBarUtilityPanelAction BuildZoomResetActionForState(
    bool has_active_page,
    bool at_default_zoom,
    std::optional<int> zoom_percent) {
  const bool enabled = has_active_page && !at_default_zoom;
  std::u16string subtitle;
  if (zoom_percent.has_value()) {
    subtitle = base::NumberToString16(*zoom_percent) + u"%";
  }
  MahoLocationBarUtilityPanelAction action =
      MakeAction(u"Reset Zoom", std::move(subtitle), kZoomResetSymbolName,
                 MahoLocationBarUtilityPanelDataState::kLive, enabled);
  action.close_after_activate = false;
  return action;
}

MahoLocationBarUtilityPanelAction BuildShieldsAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Shields",
      has_active_page ? u"Per-site controls" : u"Current page unavailable",
      kShieldsSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&ShowShieldBubbleForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRowForState(
    bool blocking_enabled) {
  MahoLocationBarUtilityPanelAction action =
      MakeAction(u"Block Ads & Trackers",
                 blocking_enabled ? u"Blocked on every visit"
                                  : u"Allowed on this site",
                 kBlockAdsAndTrackersSymbolName,
                 MahoLocationBarUtilityPanelDataState::kLive,
                 true);
  action.has_toggle = true;
  action.toggle_is_on = blocking_enabled;
  action.toggle_on_subtitle = u"Blocked on every visit";
  action.toggle_off_subtitle = u"Allowed on this site";
  action.accessible_name = BuildAccessibleName(action.title, action.subtitle);
  action.close_after_activate = false;
  return action;
}

void SetBlockAdsAndTrackersEnabledForOrigin(const std::string& origin,
                                            bool blocking_enabled) {
  if (origin.empty()) {
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  if (blocking_enabled) {
    maho::core::RemoveSiteException(core, origin.c_str());
    return;
  }

  maho::core::AddSiteException(core, origin.c_str());
}

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRow(Browser* browser) {
  const std::optional<std::string> origin = GetActiveOrigin(browser);
  if (!origin.has_value()) {
    return MakeAction(u"Block Ads & Trackers", u"Current page unavailable",
                      kBlockAdsAndTrackersSymbolName,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return MakeAction(u"Block Ads & Trackers", u"Core unavailable",
                      kBlockAdsAndTrackersSymbolName,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  char* exceptions_json = maho_core_get_site_exceptions(core);
  const bool is_excepted =
      exceptions_json &&
      IsOriginExceptedInSiteExceptions(exceptions_json, *origin);
  if (exceptions_json) {
    maho_string_free(exceptions_json);
  }
  // is_excepted means this origin is in the exceptions list, i.e. blocking is
  // disabled for this site (ads allowed). Not excepted means blocking is on.
  MahoLocationBarUtilityPanelAction action =
      BuildBlockAdsAndTrackersRowForState(!is_excepted);
  action.enabled = true;
  action.toggle_callback = base::BindRepeating(
      &SetBlockAdsAndTrackersEnabledForOrigin, *origin);
  return action;
}

MahoLocationBarUtilityPanelAction BuildAutoPipSettingsRow(
    content::WebContents* web_contents,
    Profile* profile) {
  return BuildAutoPipActionForContext(web_contents, profile, false);
}

void OpenExtensionsSettingsPaneForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }
  maho::OpenMahoSettingsPane(browser, "extensions");
}

MahoLocationBarUtilityPanelAction BuildExtensionsSummaryAction(
    const std::vector<MahoLocationBarUtilityPanelAction>& extensions) {
  if (extensions.size() == 1 && extensions[0].title == u"Extensions") {
    return MakeAction(extensions[0].title, extensions[0].subtitle,
                      extensions[0].sf_symbol_name,
                      extensions[0].data_state, false);
  }
  return MakeAction(u"Extensions",
                    base::NumberToString16(extensions.size()) + u" installed",
                    kExtensionsSymbolName,
                    MahoLocationBarUtilityPanelDataState::kLive, false);
}

void TriggerCameraScreenshot(Browser* browser) {
  if (!browser) {
    return;
  }
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return;
  }
  MahoDomScreenshotHandler::TriggerCapture(web_contents);
}

void TriggerViewfinderCapture(Browser* browser) {
  if (!browser) {
    return;
  }
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return;
  }
  MahoFullPageCaptureClient::TriggerCapture(web_contents);
}

MahoLocationBarUtilityPanelAction BuildCameraAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Screenshot",
      has_active_page ? u"Capture visible area" : u"Current page unavailable",
      kCameraSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&TriggerCameraScreenshot,
                                          base::Unretained(browser));
  }
  return action;
}

MahoLocationBarUtilityPanelAction BuildViewfinderAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Full-Page Screenshot",
      has_active_page ? u"Capture full page" : u"Current page unavailable",
      kViewfinderSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&TriggerViewfinderCapture,
                                          base::Unretained(browser));
  }
  return action;
}

MahoUtilityPanelZone BuildToolbarZone(Browser* browser) {
  MahoUtilityPanelZone zone;
  zone.id = MahoUtilityPanelZoneId::kToolbar;
  zone.style = MahoUtilityPanelZoneStyle::kToolbarRow;
  zone.items.push_back(BuildShareAction(browser));
  zone.items.push_back(BuildBoostAction(browser));
  zone.items.push_back(BuildCameraAction(browser));
  zone.items.push_back(BuildViewfinderAction(browser));
  return zone;
}

MahoUtilityPanelZone BuildExtensionsZone(const std::string& json,
                                         Browser* browser) {
  MahoUtilityPanelZone zone;
  zone.id = MahoUtilityPanelZoneId::kExtensions;
  zone.style = MahoUtilityPanelZoneStyle::kChipRow;
  zone.header_label = u"Extensions";

  std::vector<MahoLocationBarUtilityPanelAction> items =
      BuildInstalledExtensions(json, browser);

  if (items.size() == 1 && IsExtensionsFallbackAction(items.front())) {
    zone.items.push_back(MakeAction(u"No extensions", u"",
                                    kExtensionsSymbolName,
                                    items.front().data_state, false));
  } else {
    for (auto& item : items) {
      zone.items.push_back(std::move(item));
    }
  }

  zone.items.push_back(MakeAddExtensionChip(browser));
  return zone;
}

MahoUtilityPanelZone BuildSettingsZone(Browser* browser) {
  MahoUtilityPanelZone zone;
  zone.id = MahoUtilityPanelZoneId::kSettings;
  zone.style = MahoUtilityPanelZoneStyle::kSettingsList;
  zone.header_label = u"Settings";

  MahoCore* core = maho::GetCore();
  std::optional<base::Value> settings = ReadCoreSettings(core);
  zone.items.push_back(BuildDeveloperModeAction(
      settings ? &*settings : nullptr));
  zone.items.push_back(BuildBlockAdsAndTrackersRow(browser));
  zone.items.push_back(BuildAutoPipSettingsRow(
      GetActiveWebContents(browser), browser ? browser->profile() : nullptr));
  return zone;
}

MahoLocationBarUtilityPanelFooter BuildFooterFromWebContents(
    content::WebContents* web_contents) {
  MahoLocationBarUtilityPanelFooter footer;
  footer.title = u"Security state unavailable";
  footer.subtitle = u"Page security";
  footer.data_state = MahoLocationBarUtilityPanelDataState::kUnavailable;
  footer.security_level = static_cast<int>(security_state::NONE);
  footer.sf_symbol_name =
      GetSecuritySFSymbolNameForLevel(footer.security_level);
  footer.primary_action_label = u"Close";
  footer.close_after_primary_action = true;

  if (!web_contents) {
    return footer;
  }

  ChromeSecurityStateTabHelper::CreateForWebContents(web_contents);
  SecurityStateTabHelper* helper =
      SecurityStateTabHelper::FromWebContents(web_contents);
  const security_state::SecurityLevel level =
      helper ? helper->GetSecurityLevel() : security_state::NONE;

  footer.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  footer.security_level = static_cast<int>(level);
  footer.sf_symbol_name = GetSecuritySFSymbolNameForLevel(footer.security_level);
  footer.title = DescribeSecurityLevel(footer.security_level);

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  const std::string host(url.host());
  footer.subtitle = host.empty() ? u"Page security" : base::UTF8ToUTF16(host);
  return footer;
}

MahoLocationBarUtilityPanelFooter BuildFooterFromModel(
    LocationBarModel* model) {
  MahoLocationBarUtilityPanelFooter footer;
  footer.title = u"Security state unavailable";
  footer.subtitle = u"Page security";
  footer.data_state = MahoLocationBarUtilityPanelDataState::kUnavailable;
  footer.security_level = static_cast<int>(security_state::NONE);
  footer.sf_symbol_name =
      GetSecuritySFSymbolNameForLevel(footer.security_level);
  footer.primary_action_label = u"Close";
  footer.close_after_primary_action = true;

  if (!model) {
    return footer;
  }

  footer.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  footer.security_level = static_cast<int>(model->GetSecurityLevel());
  footer.sf_symbol_name =
      GetSecuritySFSymbolNameForLevel(footer.security_level);
  footer.title = DescribeSecurityLevel(footer.security_level);
  const std::string host(model->GetURL().host());
  footer.subtitle = host.empty() ? u"Page security" : base::UTF8ToUTF16(host);
  return footer;
}

MahoLocationBarUtilityPanelFooter BuildFooter(LocationBar* location_bar) {
  return BuildFooterFromWebContents(
      location_bar ? location_bar->GetWebContents() : nullptr);
}

MahoLocationBarUtilityPanelFooter BuildFooter(Browser* browser) {
  if (!browser) {
    return BuildFooterFromWebContents(nullptr);
  }

  return BuildFooterFromWebContents(GetActiveWebContents(browser));
}

void PopulateArcShapeModelData(MahoLocationBarUtilityPanelModel& model,
                               Browser* browser) {
  const std::string extensions_json = ReadInstalledExtensionsJsonOrFallback();
  model.zones.push_back(BuildToolbarZone(browser));
  model.zones.push_back(BuildExtensionsZone(extensions_json, browser));
  model.zones.push_back(BuildSettingsZone(browser));
}

std::vector<MahoLocationBarUtilityPanelAction> BuildInstalledExtensions(
    const std::string& json) {
  return BuildInstalledExtensions(json, nullptr);
}

std::vector<MahoLocationBarUtilityPanelAction> BuildInstalledExtensions(
    const std::string& json,
    Browser* browser) {
  std::vector<MahoLocationBarUtilityPanelAction> items;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    items.push_back(MakeAction(u"Extensions", u"Extension list unavailable",
                               kExtensionsSymbolName,
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
    const bool ext_enabled = dict->FindBool("enabled").value_or(true);

    std::u16string title = ToUtf16(name ? *name : (id ? *id : std::string()));
    if (title.empty()) {
      title = u"Unnamed extension";
    }

    std::u16string subtitle = ext_enabled ? u"Enabled" : u"Disabled";
    if (version && !version->empty()) {
      subtitle = u"v" + ToUtf16(*version) + u" · " + subtitle;
    }

    MahoLocationBarUtilityPanelAction action =
        MakeAction(std::move(title), std::move(subtitle),
                   kExtensionsSymbolName,
                   MahoLocationBarUtilityPanelDataState::kLive,
                   browser != nullptr);
    action.close_after_activate = true;
    if (browser) {
      action.callback = base::BindRepeating(
          &OpenExtensionsSettingsPaneForBrowser, base::Unretained(browser));
    }
    items.push_back(std::move(action));
  }

  if (items.empty()) {
    MahoLocationBarUtilityPanelAction action =
        MakeAction(u"Extensions", u"No installed extensions",
                   kExtensionsSymbolName,
                   MahoLocationBarUtilityPanelDataState::kLive,
                   browser != nullptr);
    action.close_after_activate = true;
    if (browser) {
      action.callback = base::BindRepeating(
          &OpenExtensionsSettingsPaneForBrowser, base::Unretained(browser));
    }
    items.push_back(std::move(action));
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

MahoUtilityPanelZone::MahoUtilityPanelZone() = default;
MahoUtilityPanelZone::MahoUtilityPanelZone(const MahoUtilityPanelZone&) = default;
MahoUtilityPanelZone& MahoUtilityPanelZone::operator=(
    const MahoUtilityPanelZone&) = default;
MahoUtilityPanelZone::~MahoUtilityPanelZone() = default;

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar) {
  MahoLocationBarUtilityPanelModel model;
  Browser* browser = location_bar ? location_bar->GetBrowser() : nullptr;
  internal::PopulateArcShapeModelData(model, browser);
  model.footer = internal::BuildFooter(location_bar);
  return model;
}

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModelForBrowser(
    Browser* browser) {
  MahoLocationBarUtilityPanelModel model;
  internal::PopulateArcShapeModelData(model, browser);
  model.footer = internal::BuildFooter(browser);
  return model;
}

}  // namespace maho
