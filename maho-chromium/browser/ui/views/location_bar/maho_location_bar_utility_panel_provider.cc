// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/share/share_attempt.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_model.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_action_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/permissions/permission_decision_auto_blocker.h"
#include "components/security_state/core/security_state.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/net/maho_shield_site_state.h"
#include "maho/browser/net/maho_shield_site_state_factory.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/site_control/maho_page_info_ui.h"
#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_handler.h"
#include "maho/browser/ui/views/location_bar/maho_full_page_capture_client.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace maho {

namespace internal {

const gfx::VectorIcon& kExtensionsIcon = maho_lucide_icons::kPuzzleIcon;
const gfx::VectorIcon& kAutoPipIcon = maho_lucide_icons::kPictureInPicture2Icon;
const gfx::VectorIcon& kShareIcon = maho_lucide_icons::kShareIcon;
const gfx::VectorIcon& kCameraIcon = maho_lucide_icons::kCameraIcon;
const gfx::VectorIcon& kViewfinderIcon = maho_lucide_icons::kScanIcon;
const gfx::VectorIcon& kBlockAdsAndTrackersIcon =
    maho_lucide_icons::kShieldHalfIcon;
const gfx::VectorIcon& kSiteSettingsIcon = maho_lucide_icons::kSettingsIcon;

content::WebContents* GetActiveWebContents(Browser* browser);
const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents);
void OpenExtensionsSettingsPaneForBrowser(Browser* browser);
void InvokeExtensionActionForBrowser(base::WeakPtr<BrowserWindowInterface> browser_weak,
                                     const std::string& extension_id);
void ShowExtensionContextMenuForBrowser(base::WeakPtr<BrowserWindowInterface> browser_weak,
                                        const std::string& extension_id,
                                        views::View* anchor);

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
    const gfx::VectorIcon& icon,
    MahoLocationBarUtilityPanelDataState data_state,
    bool enabled) {
  MahoLocationBarUtilityPanelAction action;
  action.title = std::move(title);
  action.subtitle = std::move(subtitle);
  action.accessible_name = BuildAccessibleName(action.title, action.subtitle);
  action.icon = &icon;
  action.data_state = data_state;
  action.enabled = enabled;
  return action;
}

MahoLocationBarUtilityPanelAction MakeDisabledStubAction(
    std::u16string title,
    const gfx::VectorIcon& icon) {
  return MakeAction(std::move(title), std::u16string(), icon,
                    MahoLocationBarUtilityPanelDataState::kUnavailable, false);
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

bool HasActivePageForSiteControls(Browser* browser) {
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return false;
  }

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  return url.is_valid() && !url.is_empty();
}

const gfx::VectorIcon& GetSecurityIconForLevel(int security_level) {
  switch (static_cast<security_state::SecurityLevel>(security_level)) {
    case security_state::SECURE:
      return maho_lucide_icons::kShieldCheckIcon;
    case security_state::WARNING:
      return maho_lucide_icons::kShieldQuestionIcon;
    case security_state::DANGEROUS:
      return maho_lucide_icons::kShieldXIcon;
    case security_state::NONE:
    case security_state::SECURITY_LEVEL_COUNT:
      break;
  }
  return maho_lucide_icons::kShieldQuestionIcon;
}
const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
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

void SetAutoPipEnabledForOrigin(const GURL& url,
                                Profile* profile,
                                bool enabled,
                                bool& out_toggle_is_on,
                                std::u16string& out_subtitle);

MahoLocationBarUtilityPanelAction BuildAutoPipActionForContext(
    content::WebContents* web_contents,
    Profile* profile,
    bool unsupported_without_context) {
  if (!web_contents || !profile) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      unsupported_without_context
                          ? u"Unsupported in this window"
                          : u"No active page available",
                      kAutoPipIcon,
                      unsupported_without_context
                          ? MahoLocationBarUtilityPanelDataState::kUnsupported
                          : MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  if (!url.is_valid()) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Current page unavailable", kAutoPipIcon,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  if (!settings_map) {
    return MakeAction(u"Automatic Picture-In-Picture",
                      u"Auto PiP settings unavailable", kAutoPipIcon,
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
  }

  const bool toggle_is_on = (setting == CONTENT_SETTING_ALLOW);

  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Automatic Picture-In-Picture", DescribeAutoPipState(setting, embargoed),
      kAutoPipIcon, MahoLocationBarUtilityPanelDataState::kLive, true);
  action.has_toggle = true;
  action.toggle_is_on = toggle_is_on;
  action.toggle_on_subtitle = u"Allowed on every visit";
  action.toggle_off_subtitle =
      embargoed ? u"Blocked automatically" : u"Ask to allow";
  action.toggle_callback =
      base::BindRepeating(&SetAutoPipEnabledForOrigin, url, profile);
  return action;
}

content::WebContents* GetActiveWebContents(Browser* browser) {
  if (!browser) {
    return nullptr;
  }

  TabStripModel* tab_strip_model = browser->GetTabStripModel();
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
      kShareIcon,
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

std::u16string BuildShieldLauncherSubtitle(uint32_t blocked_count,
                                            bool blocking_enabled) {
  return base::NumberToString16(blocked_count) + u" blocked · Shields " +
         (blocking_enabled ? u"on" : u"off");
}

uint32_t GetBlockedCountForWebContents(
    base::WeakPtr<content::WebContents> target_web_contents) {
  if (!target_web_contents) {
    return 0;
  }
  auto* helper =
      MahoAdBlockTabHelper::FromWebContents(target_web_contents.get());
  return helper ? helper->blocked_count() : 0;
}

void ShowShieldBubbleForUtilityRow(
    base::WeakPtr<BrowserWindowInterface> browser_weak,
    base::WeakPtr<content::WebContents> target_web_contents,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    views::View* source_view) {
  Browser* browser = static_cast<Browser*>(browser_weak.get());
  if (!browser || !target_web_contents || !source_view || origin.empty()) {
    return;
  }

  ui::BaseWindow* window = browser->GetWindow();
  views::Widget* anchor_widget =
      window ? views::Widget::GetWidgetForNativeWindow(window->GetNativeWindow())
             : nullptr;
  const gfx::Rect anchor_rect = source_view->GetBoundsInScreen();
  if (!anchor_widget || anchor_rect.IsEmpty()) {
    return;
  }

  MahoPageInfoUI::ShowShieldBubble(
      anchor_widget, anchor_rect, browser, origin, blocked_count, is_excepted,
      target_web_contents.get());
}

Profile* ResolveProfileForShieldSiteState(
    base::WeakPtr<content::WebContents> target_web_contents) {
  if (!target_web_contents) {
    return nullptr;
  }
  return Profile::FromBrowserContext(target_web_contents->GetBrowserContext());
}

MahoShieldSiteState* ResolveShieldSiteState(
    base::WeakPtr<content::WebContents> target_web_contents) {
  return MahoShieldSiteStateFactory::GetForProfile(
      ResolveProfileForShieldSiteState(target_web_contents));
}

void SetAutoPipEnabledForOrigin(const GURL& url,
                                Profile* profile,
                                bool enabled,
                                bool& out_toggle_is_on,
                                std::u16string& out_subtitle) {
  if (!profile || !url.is_valid()) {
    return;
  }
  HostContentSettingsMap* settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  if (!settings_map) {
    return;
  }

  if (enabled) {
    if (auto* auto_blocker =
            PermissionDecisionAutoBlockerFactory::GetForProfile(profile)) {
      auto_blocker->RemoveEmbargoAndResetCounts(
          url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
    }
  }

  settings_map->SetContentSettingDefaultScope(
      url, url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE,
      enabled ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_DEFAULT);

  ContentSetting setting = settings_map->GetContentSetting(
      url, GURL(), ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  bool embargoed = false;
  permissions::PermissionDecisionAutoBlocker* auto_blocker =
      PermissionDecisionAutoBlockerFactory::GetForProfile(profile);
  if (setting == CONTENT_SETTING_ASK && auto_blocker) {
    embargoed = auto_blocker->IsEmbargoed(
        url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  }

  out_toggle_is_on = (setting == CONTENT_SETTING_ALLOW);
  out_subtitle = DescribeAutoPipState(setting, embargoed);
}

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRow(
    Browser* browser) {
  const std::optional<std::string> origin = GetActiveOrigin(browser);
  if (!origin.has_value()) {
    return MakeAction(u"Block Ads & Trackers", u"Current page unavailable",
                      kBlockAdsAndTrackersIcon,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
        false);
  }

  content::WebContents* target_web_contents = GetActiveWebContents(browser);
  return BuildBlockAdsAndTrackersRowForOrigin(
      browser,
      target_web_contents ? target_web_contents->GetWeakPtr()
                          : base::WeakPtr<content::WebContents>(),
      *origin);
}

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRowForOrigin(
    Browser* browser,
    base::WeakPtr<content::WebContents> target_web_contents,
    const std::string& origin) {
  if (origin.empty()) {
    return MakeAction(u"Block Ads & Trackers", u"Current page unavailable",
                      kBlockAdsAndTrackersIcon,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  MahoShieldSiteState* state =
      ResolveShieldSiteState(target_web_contents);
  if (!state) {
    return MakeAction(u"Block Ads & Trackers", u"Content blocker unavailable",
                      kBlockAdsAndTrackersIcon,
                      MahoLocationBarUtilityPanelDataState::kUnavailable,
                      false);
  }

  const int mode = state->GetEffectiveContentBlockingMode();
  if (mode != 0) {
    const std::u16string subtitle = mode == 1 ? u"Managed by extension"
                                    : mode == 2
                                        ? u"Content blocking off"
                                        : u"Content blocker unavailable";
    return MakeAction(
        u"Block Ads & Trackers", subtitle, kBlockAdsAndTrackersIcon,
        MahoLocationBarUtilityPanelDataState::kUnavailable, false);
  }

  const uint32_t blocked_count =
      GetBlockedCountForWebContents(target_web_contents);
  const bool is_excepted = state->IsSiteExceptedForOrigin(origin);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Block Ads & Trackers",
      BuildShieldLauncherSubtitle(blocked_count, !is_excepted),
      kBlockAdsAndTrackersIcon, MahoLocationBarUtilityPanelDataState::kLive,
      browser != nullptr);
  action.close_after_activate = true;
  if (browser) {
    action.anchor_callback = base::BindRepeating(
        &ShowShieldBubbleForUtilityRow, browser->GetWeakPtr(),
        target_web_contents, origin, blocked_count, is_excepted);
    action.show_disclosure_indicator = true;
  }
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
  chrome::ShowExtensions(browser);
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
      kCameraIcon,
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
      kViewfinderIcon,
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
  zone.items.push_back(BuildCameraAction(browser));
  zone.items.push_back(BuildViewfinderAction(browser));
  return zone;
}

void InvokeExtensionActionForBrowser(base::WeakPtr<BrowserWindowInterface> browser_weak,
                                     const std::string& extension_id) {
  Browser* browser = static_cast<Browser*>(browser_weak.get());
  if (!browser) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->toolbar()) {
    return;
  }
  ExtensionsToolbarDesktop* extensions_container =
      browser_view->toolbar()->extensions_container();
  if (!extensions_container) {
    return;
  }
  ToolbarActionView* action_view =
      extensions_container->GetViewForId(extension_id);
  if (!action_view || !action_view->view_model()) {
    return;
  }
  action_view->view_model()->ExecuteUserAction(
      ToolbarActionViewModel::InvocationSource::kMenuEntry);
}

void SetExtensionPinnedForBrowser(base::WeakPtr<BrowserWindowInterface> browser_weak,
                                  const std::string& extension_id,
                                  bool pinned) {
  Browser* browser = static_cast<Browser*>(browser_weak.get());
  if (!browser || browser->GetProfile()->IsOffTheRecord()) {
    return;
  }
  auto* toolbar_model = ToolbarActionsModel::Get(browser->GetProfile());
  if (!toolbar_model || toolbar_model->IsActionForcePinned(extension_id) ||
      toolbar_model->IsActionPinned(extension_id) == pinned) {
    return;
  }
  toolbar_model->SetActionVisibility(extension_id, pinned);
}

void ShowExtensionContextMenuForBrowser(base::WeakPtr<BrowserWindowInterface> browser_weak,
                                        const std::string& extension_id,
                                        views::View* anchor) {
  Browser* browser = static_cast<Browser*>(browser_weak.get());
  if (!browser || !anchor) {
    return;
  }
  Profile* profile = browser->GetProfile();
  if (!profile) {
    return;
  }
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  const extensions::Extension* extension =
      registry ? registry->GetInstalledExtension(extension_id) : nullptr;
  if (!extension) {
    return;
  }

  bool is_pinned = false;
  if (auto* model = ToolbarActionsModel::Get(profile)) {
    is_pinned = model->IsActionPinned(extension_id);
  }

  auto menu_model = std::make_unique<extensions::ExtensionContextMenuModel>(
      extension, browser, is_pinned, nullptr, true,
      extensions::ExtensionContextMenuModel::ContextMenuSource::kToolbarAction);

  views::Widget* widget = anchor->GetWidget();
  if (!widget) {
    return;
  }
  views::WidgetDelegate* delegate = widget->widget_delegate();
  if (!delegate) {
    return;
  }
  views::BubbleDialogDelegate* bubble_delegate =
      delegate->AsBubbleDialogDelegate();
  if (!bubble_delegate) {
    return;
  }
  auto* panel_view =
      static_cast<MahoLocationBarUtilityPanelView*>(bubble_delegate);
  CHECK(panel_view);
  panel_view->RunExtensionContextMenu(std::move(menu_model), anchor);
}

std::vector<MahoLocationBarUtilityPanelAction> BuildExtensionIconActions(
    Browser* browser) {
  std::vector<MahoLocationBarUtilityPanelAction> actions;
  if (!browser) {
    return actions;
  }

  Profile* profile = browser->GetProfile();
  if (!profile) {
    return actions;
  }

  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return actions;
  }
  auto* toolbar_model = ToolbarActionsModel::Get(profile);

  auto append_set = [&](const extensions::ExtensionSet& set, bool enabled) {
    const bool is_otr = profile->IsOffTheRecord();
    for (const auto& ext : set) {
      if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*ext)) {
        continue;
      }
      // C2: in an incognito/OTR window only surface extensions the user has
      // explicitly enabled in incognito; otherwise the grid exposes and can run
      // every installed extension against incognito tabs.
      if (is_otr &&
          !extensions::util::IsIncognitoEnabled(ext->id(), profile)) {
        continue;
      }
      MahoLocationBarUtilityPanelAction action;
      action.title = base::UTF8ToUTF16(ext->name());
      action.accessible_name =
          enabled ? action.title : action.title + u" (Disabled)";
      action.enabled = enabled;
      action.data_state = MahoLocationBarUtilityPanelDataState::kLive;
      action.extension_id = ext->id();
      action.close_after_activate = enabled;

      if (enabled) {
        action.callback =
            base::BindRepeating(&InvokeExtensionActionForBrowser,
                                browser->GetWeakPtr(), ext->id());
        action.is_pinned =
            toolbar_model && toolbar_model->IsActionPinned(ext->id());
        action.pin_enabled =
            toolbar_model && !is_otr &&
            !toolbar_model->IsActionForcePinned(ext->id());
        if (action.pin_enabled) {
          action.pin_callback =
              base::BindRepeating(&SetExtensionPinnedForBrowser,
                                  browser->GetWeakPtr(), ext->id());
        }
      }

      action.context_menu_callback =
          base::BindRepeating(&ShowExtensionContextMenuForBrowser,
                              browser->GetWeakPtr(), ext->id());
      action.context_menu_enabled = true;

      actions.push_back(std::move(action));
    }
  };

  append_set(registry->enabled_extensions(), /*enabled=*/true);
  append_set(registry->disabled_extensions(), /*enabled=*/false);
  return actions;
}

MahoLocationBarUtilityPanelAction BuildExtensionsManageAction(
    Browser* browser) {
  MahoLocationBarUtilityPanelAction action;
  action.title = u"Manage";
  action.accessible_name = u"Manage extensions";
  action.enabled = browser != nullptr;
  action.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  action.close_after_activate = true;
  if (browser) {
    action.callback = base::BindRepeating(&OpenExtensionsSettingsPaneForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

MahoUtilityPanelZone BuildExtensionsZone(Browser* browser) {
  MahoUtilityPanelZone zone;
  zone.id = MahoUtilityPanelZoneId::kExtensions;
  zone.style = MahoUtilityPanelZoneStyle::kChipRow;
  zone.header_label = u"Extensions";
  zone.header_trailing_action = BuildExtensionsManageAction(browser);

  std::vector<MahoLocationBarUtilityPanelAction> items =
      BuildExtensionIconActions(browser);

  if (items.empty()) {
    MahoLocationBarUtilityPanelAction empty_chip;
    empty_chip.title = u"No extensions";
    empty_chip.enabled = false;
    empty_chip.data_state = MahoLocationBarUtilityPanelDataState::kLive;
    empty_chip.icon = &kExtensionsIcon;
    zone.items.push_back(std::move(empty_chip));
  } else {
    for (auto& item : items) {
      zone.items.push_back(std::move(item));
    }
  }

  return zone;
}

void ShowSiteSettingsForBrowser(Browser* browser) {
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return;
  }
  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  if (!url.is_valid() || url.is_empty()) {
    return;
  }
  chrome::ShowSiteSettings(browser, url);
}

MahoLocationBarUtilityPanelAction BuildSiteSettingsRow(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Site settings",
      has_active_page ? u"Permissions & data" : u"Current page unavailable",
      kSiteSettingsIcon,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&ShowSiteSettingsForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

MahoUtilityPanelZone BuildSettingsZone(Browser* browser) {
  MahoUtilityPanelZone zone;
  zone.id = MahoUtilityPanelZoneId::kSettings;
  zone.style = MahoUtilityPanelZoneStyle::kSettingsList;
  zone.header_label = u"Site controls";

  zone.items.push_back(BuildBlockAdsAndTrackersRow(browser));
  zone.items.push_back(BuildAutoPipSettingsRow(
      GetActiveWebContents(browser), browser ? browser->GetProfile() : nullptr));
  zone.items.push_back(BuildSiteSettingsRow(browser));
  return zone;
}

MahoLocationBarUtilityPanelFooter BuildFooterFromWebContents(
    content::WebContents* web_contents) {
  MahoLocationBarUtilityPanelFooter footer;
  footer.title = u"Security state unavailable";
  footer.subtitle = u"Page security";
  footer.data_state = MahoLocationBarUtilityPanelDataState::kUnavailable;
  footer.security_level = static_cast<int>(security_state::NONE);
  footer.icon = &GetSecurityIconForLevel(footer.security_level);

  if (!web_contents) {
    return footer;
  }

  const security_state::SecurityLevel level =
      chrome_security_state::GetSecurityLevel(web_contents);

  footer.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  footer.security_level = static_cast<int>(level);
  footer.icon = &GetSecurityIconForLevel(footer.security_level);
  footer.title = DescribeSecurityLevel(footer.security_level);

  const GURL& url = GetVisibleOrCommittedUrl(web_contents);
  const std::string host(url.host());
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
  model.zones.push_back(BuildToolbarZone(browser));
  model.zones.push_back(BuildExtensionsZone(browser));
  model.zones.push_back(BuildSettingsZone(browser));
}

}  // namespace internal

MahoLocationBarUtilityPanelAction::MahoLocationBarUtilityPanelAction() =
    default;
MahoLocationBarUtilityPanelAction::MahoLocationBarUtilityPanelAction(
    const MahoLocationBarUtilityPanelAction&) = default;
MahoLocationBarUtilityPanelAction& MahoLocationBarUtilityPanelAction::operator=(
    const MahoLocationBarUtilityPanelAction&) = default;
MahoLocationBarUtilityPanelAction::~MahoLocationBarUtilityPanelAction() =
    default;

MahoLocationBarUtilityPanelFooter::MahoLocationBarUtilityPanelFooter() =
    default;
MahoLocationBarUtilityPanelFooter::MahoLocationBarUtilityPanelFooter(
    const MahoLocationBarUtilityPanelFooter&) = default;
MahoLocationBarUtilityPanelFooter& MahoLocationBarUtilityPanelFooter::operator=(
    const MahoLocationBarUtilityPanelFooter&) = default;
MahoLocationBarUtilityPanelFooter::~MahoLocationBarUtilityPanelFooter() =
    default;

MahoLocationBarUtilityPanelModel::MahoLocationBarUtilityPanelModel() = default;
MahoLocationBarUtilityPanelModel::MahoLocationBarUtilityPanelModel(
    const MahoLocationBarUtilityPanelModel&) = default;
MahoLocationBarUtilityPanelModel& MahoLocationBarUtilityPanelModel::operator=(
    const MahoLocationBarUtilityPanelModel&) = default;
MahoLocationBarUtilityPanelModel::~MahoLocationBarUtilityPanelModel() = default;

MahoUtilityPanelZone::MahoUtilityPanelZone() = default;
MahoUtilityPanelZone::MahoUtilityPanelZone(const MahoUtilityPanelZone&) =
    default;
MahoUtilityPanelZone& MahoUtilityPanelZone::operator=(
    const MahoUtilityPanelZone&) = default;
MahoUtilityPanelZone::~MahoUtilityPanelZone() = default;

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar) {
  MahoLocationBarUtilityPanelModel model;
  // Desktop browser windows are backed by Browser (CreateBrowserWindow's
  // non-Android implementation returns Browser::Create()).
  Browser* browser =
      location_bar ? static_cast<Browser*>(location_bar->GetBrowser()) : nullptr;
  model.owning_browser = browser;
  internal::PopulateArcShapeModelData(model, browser);
  model.footer = internal::BuildFooter(location_bar);
  return model;
}

MahoLocationBarUtilityPanelModel
BuildMahoLocationBarUtilityPanelModelForBrowser(Browser* browser) {
  MahoLocationBarUtilityPanelModel model;
  model.owning_browser = browser;
  internal::PopulateArcShapeModelData(model, browser);
  model.footer = internal::BuildFooter(browser);
  return model;
}

}  // namespace maho
