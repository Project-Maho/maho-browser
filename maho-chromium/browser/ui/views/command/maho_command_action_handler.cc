// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_mail_command_catalog.h"

#include <string>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_ai_popup_lifetime_tracker.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/mail_helper/maho_mail_service.h"  // nogncheck
#include "maho/browser/mail_helper/maho_mail_service_factory.h"  // nogncheck
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/maho_ai_ingress_coordinator.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"  // nogncheck
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
// maho_location_bar_views links into //chrome/browser:browser; a direct dep
// would add the same command -> chrome/browser/ui cycle noted in BUILD.gn.
#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_handler.h"  // nogncheck
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"  // nogncheck
#include "maho/browser/ui/views/peek/maho_peek_controller.h"  // nogncheck
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr char kShortcutTracePrefix[] = "[maho-shortcut-trace]";

void CopyTextToClipboard(const std::string& text) {
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::UTF8ToUTF16(text));
}

// ShowLinkCopiedToast() moved to maho/browser/ui/notifications/
// maho_notification_overlay.cc so every copy surface (including the
// favorites-menu Copy Link) links one shared helper; declared in that header.

bool IsApprovedMahoAliasUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs("maho") || url.has_username() ||
      url.has_password() || url.has_port()) {
    return false;
  }
  const std::string lower_host = base::ToLowerASCII(url.host());
  for (const auto& record : maho::kMahoUrlAliases) {
    if (record.alias_host == lower_host) {
      return true;
    }
  }
  return false;
}

BrowserWindowInterface* FindBrowserForStableTab(
    const CommandSuggestion& suggestion) {
  if (suggestion.browser_session_id.empty() ||
      suggestion.tab_session_id.empty()) {
    return nullptr;
  }

  int64_t browser_id = 0;
  if (!base::StringToInt64(suggestion.browser_session_id, &browser_id)) {
    return nullptr;
  }

  BrowserWindowInterface* browser = BrowserWindowInterface::FromSessionID(
      SessionID::FromSerializedValue(browser_id));
  if (!browser || !browser->GetWindow() || !browser->GetTabStripModel()) {
    return nullptr;
  }

  return browser;
}

int FindTabIndexForStableId(BrowserWindowInterface* browser,
                            const std::string& tab_session_id) {
  if (!browser || tab_session_id.empty()) {
    return TabStripModel::kNoTab;
  }

  int64_t desired_tab_id = 0;
  if (!base::StringToInt64(tab_session_id, &desired_tab_id)) {
    return TabStripModel::kNoTab;
  }

  TabStripModel* tab_strip = browser->GetTabStripModel();
  for (int i = 0; i < tab_strip->count(); ++i) {
    auto* contents = tab_strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = sessions::SessionTabHelper::FromWebContents(contents);
    if (helper && helper->session_id().id() == desired_tab_id) {
      return i;
    }
  }

  return TabStripModel::kNoTab;
}

// Locate any same-profile browser whose TabStripModel contains a WebContents
// whose MahoTabIdHelper carries the given stable_id. Used for the Rust-emitted
// `tab:{stable_id}` key shape where browser/tab session IDs are absent.
BrowserWindowInterface* FindBrowserForHelperStableId(
    Browser* origin_browser,
    const std::string& stable_id) {
  if (stable_id.empty()) {
    return nullptr;
  }
  Profile* origin_profile =
      origin_browser ? origin_browser->GetProfile() : nullptr;
  BrowserWindowInterface* match = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&match, &stable_id, origin_profile](BrowserWindowInterface* candidate) {
        if (!candidate || !candidate->GetTabStripModel()) {
          return true;
        }
        if (origin_profile && candidate->GetProfile() != origin_profile) {
          return true;
        }
        TabStripModel* model = candidate->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          auto* contents = model->GetWebContentsAt(i);
          if (!contents) {
            continue;
          }
          auto* helper = MahoTabIdHelper::FromWebContents(contents);
          if (helper && helper->stable_tab_id() == stable_id) {
            match = candidate;
            return false;  // stop iteration
          }
        }
        return true;
      });
  return match;
}

int FindTabIndexForHelperStableId(BrowserWindowInterface* browser,
                                  const std::string& stable_id) {
  if (!browser || stable_id.empty()) {
    return TabStripModel::kNoTab;
  }
  TabStripModel* model = browser->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    auto* contents = model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == stable_id) {
      return i;
    }
  }
  return TabStripModel::kNoTab;
}

void ToggleMahoAiPanel(Browser* browser) {
  if (!browser) {
    LOG(WARNING) << kShortcutTracePrefix
                 << " ai_panel toggle skipped: browser was null";
    return;
  }

  auto* side_panel_ui = browser->GetFeatures().side_panel_ui();
  if (!side_panel_ui) {
    LOG(WARNING) << kShortcutTracePrefix
                 << " ai_panel toggle skipped: side_panel_ui was null";
    return;
  }

  const bool ai_panel_showing =
      side_panel_ui->GetCurrentEntryId() ==
      SidePanelEntryId::kMahoAiPanel;
  LOG(WARNING) << kShortcutTracePrefix
               << " ai_panel toggle requested current_visible="
               << ai_panel_showing;

  if (ai_panel_showing) {
    LOG(WARNING) << kShortcutTracePrefix << " ai_panel closing";
    side_panel_ui->Close(
                         SidePanelEntryHideReason::kSidePanelClosed,
                         /*suppress_animations=*/true);
    return;
  }

  LOG(WARNING) << kShortcutTracePrefix << " ai_panel showing";
  side_panel_ui->Show(SidePanelEntryKey(SidePanelEntryId::kMahoAiPanel),
                      SidePanelOpenTrigger::kToolbarButton);
}

void ActivatePartnerSplitPane(Browser* browser) {
  TabStripModel* model = browser ? browser->GetTabStripModel() : nullptr;
  if (!model) {
    return;
  }
  tabs::TabInterface* active_tab = model->GetActiveTab();
  if (!active_tab) {
    return;
  }
  std::optional<split_tabs::SplitTabId> split_id = active_tab->GetSplit();
  if (!split_id.has_value()) {
    return;
  }
  split_tabs::SplitTabData* split_data = model->GetSplitData(split_id.value());
  if (!split_data) {
    return;
  }
  // Two-pane splits only: activate the one member that is not already active.
  // Routing through TabStripModel::ActivateTabAt makes the model the single
  // source of truth and drives the canonical
  // MultiContentsViewDelegateImpl::WebContentsFocused ->
  // TabStripModel::ActivateTabAt -> BrowserView::UpdateActiveTabInSplitView ->
  // MultiContentsView::SetActiveIndex path, so both next and prev converge the
  // active WebContents, focus, and derived MultiContentsView state.
  for (tabs::TabInterface* member : split_data->ListTabs()) {
    if (member == active_tab) {
      continue;
    }
    const int partner_index = model->GetIndexOfTab(member);
    if (partner_index != TabStripModel::kNoTab) {
      model->ActivateTabAt(
          partner_index, TabStripUserGestureDetails(
                             TabStripUserGestureDetails::GestureType::kOther));
    }
    return;
  }
}

}  // namespace

bool IsPrimaryIncognitoAllowedActionId(const std::string& action_id) {
  for (const std::string& id : MahoCommandModel::PrivateActionAllowlist()) {
    if (id == action_id) {
      return true;
    }
  }
  return false;
}

bool ShouldPersistSidebarToggle(MahoPrivateContextClass klass) {
  return klass == MahoPrivateContextClass::kRegular;
}

bool CanExecuteMailCommandForTesting(bool feature_enabled,
                                     bool helper_available,
                                     bool platform_supported) {
  return feature_enabled && helper_available && platform_supported;
}

namespace {

// Exact-primary-Incognito dispatcher: accepts ONLY the frozen 13 IDs, each
// gated by a fresh token revalidation. toggle_sidebar never writes prefs and
// toggle_split_view uses the nonpersistent split mode (no MahoCore dispatch).
void ExecutePrivateCommandAction(Browser* browser,
                                 const std::string& action_id) {
  if (!IsPrimaryIncognitoAllowedActionId(action_id)) {
    return;
  }

  content::WebContents* active =
      browser->GetTabStripModel()->GetActiveWebContents();
  MahoPrivateContextToken token(browser, active);
  if (!token.Revalidate(MahoPrivateCapability::kWindowLocalTabs)) {
    return;
  }

  if (action_id == "close_tab") {
    chrome::CloseTab(browser);
  } else if (action_id == "reload_tab") {
    chrome::Reload(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "hard_reload") {
    chrome::ReloadBypassingCache(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "copy_url") {
    if (active) {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(active->GetVisibleURL().spec()));
    }
  } else if (action_id == "toggle_sidebar") {
    // Local-only: must NOT persist sidebar prefs. The in-memory container width
    // toggle is owned/wired by R-11 (MahoSidebarContainerView); until then this
    // deliberately performs no pref mutation.
  } else if (action_id == "toggle_split_view") {
    MahoSplitViewController(browser, MahoSplitPersistence::kPrivateLocal)
        .ToggleSplit();
  } else if (action_id == "zoom_in") {
    chrome::Zoom(browser, content::PAGE_ZOOM_IN);
  } else if (action_id == "zoom_out") {
    chrome::Zoom(browser, content::PAGE_ZOOM_OUT);
  } else if (action_id == "reset_zoom") {
    chrome::Zoom(browser, content::PAGE_ZOOM_RESET);
  } else if (action_id == "find_in_page") {
    chrome::Find(browser);
  } else if (action_id == "view_source") {
    if (active) {
      active->GetPrimaryMainFrame()->ViewSource();
    }
  } else if (action_id == "toggle_dev_tools") {
    chrome::ToggleDevToolsWindow(browser, DevToolsToggleAction::Toggle(),
                                 DevToolsOpenedByAction::kUnknown);
  } else if (action_id == "print_page") {
    chrome::Print(browser);
  }
}

}  // namespace

void ExecuteCommandAction(Browser* browser, const std::string& action_id) {
  DCHECK(browser);
  DCHECK(browser->GetTabStripModel());

  const MahoPrivateContextClass klass = MahoClassifyProfile(browser->GetProfile());
  if (klass == MahoPrivateContextClass::kPrimaryIncognito) {
    ExecutePrivateCommandAction(browser, action_id);
    return;
  }
  if (klass != MahoPrivateContextClass::kRegular) {
    // Guest/system/DevTools/other OTR/null windows own no Maho command model;
    // deny defensively.
    return;
  }

  if (action_id == "new_tab") {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (browser_view) {
      auto* container = views::AsViewClass<MahoSidebarContainerView>(
          browser_view->maho_sidebar_container());
      if (container) {
        container->ShowCommandOverlayForNewTab();
      }
    }
  } else if (action_id == "archive_tab") {
    if (auto* web_contents =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      if (auto* helper = MahoTabIdHelper::FromWebContents(web_contents);
          helper && !helper->stable_tab_id().empty()) {
        MahoTabRegistry::Get()->ArchiveTabsAndRemoveFromStrip(
            browser, {helper->stable_tab_id()});
      }
    }
  } else if (action_id == "close_tab") {
    // Cmd+W reaches here via MahoShortcutInterceptor, bypassing IDC_CLOSE_TAB;
    // honor the sidebar multi-selection the same way that command does.
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (!browser_view || !browser_view->MaybeHandleMultiTabClose()) {
      chrome::CloseTab(browser);
    }
  } else if (action_id == "reload_tab") {
    chrome::Reload(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "hard_reload") {
    chrome::ReloadBypassingCache(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "duplicate_tab") {
    chrome::DuplicateTab(browser);
  } else if (action_id == "restore_tab") {
    chrome::RestoreTab(browser);
  } else if (action_id == "pin_tab") {
    chrome::PinTab(browser);
  } else if (action_id == "find_in_page") {
    chrome::Find(browser);
  } else if (action_id == "zoom_in") {
    chrome::Zoom(browser, content::PAGE_ZOOM_IN);
  } else if (action_id == "zoom_out") {
    chrome::Zoom(browser, content::PAGE_ZOOM_OUT);
  } else if (action_id == "reset_zoom") {
    chrome::Zoom(browser, content::PAGE_ZOOM_RESET);
  } else if (action_id == "toggle_dev_tools") {
    chrome::ToggleDevToolsWindow(browser, DevToolsToggleAction::Toggle(),
                                 DevToolsOpenedByAction::kUnknown);
  } else if (action_id == "print_page") {
    chrome::Print(browser);
  } else if (action_id == "view_source") {
    if (auto* web_contents =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      web_contents->GetPrimaryMainFrame()->ViewSource();
    }
  } else if (action_id == "settings") {
    maho::OpenMahoSettingsPane(browser, "");
  } else if (action_id == "open_history") {
    chrome::ShowHistory(browser);
  } else if (action_id == "open_downloads") {
    chrome::ShowDownloads(browser);
  } else if (action_id == "new_incognito") {
    chrome::NewIncognitoWindow(browser->GetProfile());
  } else if (action_id == "focus_url_bar") {
    chrome::FocusLocationBar(browser);
  } else if (action_id == "add_split_view" ||
             action_id == "add_split_view_arc") {
    MahoSplitViewController(browser).AddSplit();
  } else if (action_id == "increase_split_view") {
    MahoSplitViewController(browser).ResizeSplit(0.1);
  } else if (action_id == "decrease_split_view") {
    MahoSplitViewController(browser).ResizeSplit(-0.1);
  } else if (action_id == "remove_split_view") {
    MahoSplitViewController(browser).RemoveSplit();
  } else if (action_id == "next_split_view") {
    ActivatePartnerSplitPane(browser);
  } else if (action_id == "prev_split_view") {
    ActivatePartnerSplitPane(browser);
  } else if (action_id == "swap_split_view") {
    MahoSplitViewController(browser).SwapSplit();
  } else if (action_id == "toggle_split_view") {
    MahoSplitViewController(browser).ToggleSplit();
  } else if (action_id == "toggle_split_orientation") {
    // No-op when the active tab is not split: ToggleSplitOrientation() returns
    // nullopt rather than mutating anything.
    MahoSplitViewController(browser).ToggleSplitOrientation();
  } else if (action_id == "toggle_sidebar") {
    maho::sidebar_prefs::ToggleSidebarPanelExpanded(
        browser->GetProfile()->GetPrefs());
  } else if (action_id == "navigate_back") {
    chrome::GoBack(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "navigate_forward") {
    chrome::GoForward(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "stop_loading") {
    chrome::Stop(browser);
  } else if (action_id == "fullscreen") {
    chrome::ToggleFullscreenMode(browser);
  } else if (action_id == "minimize") {
    browser->GetWindow()->Minimize();
  } else if (action_id == "new_window") {
    chrome::NewWindow(browser);
  } else if (action_id == "close_window") {
    chrome::CloseWindow(browser);
  } else if (action_id == "next_tab" || action_id == "prev_tab") {
    const bool forward = (action_id == "next_tab");
    bool handled = false;
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    auto* container = browser_view ? static_cast<MahoSidebarContainerView*>(
                                         browser_view->maho_sidebar_container())
                                   : nullptr;
    if (container) {
      auto* sidebar =
          views::AsViewClass<maho::MahoSidebarView>(container->sidebar_view());
      if (sidebar && sidebar->tab_list_view()) {
        handled =
            sidebar->tab_list_view()->ActivateAdjacentTabInVisualOrder(forward);
      }
    }
    if (!handled) {
      if (forward) {
        chrome::SelectNextTab(browser);
      } else {
        chrome::SelectPreviousTab(browser);
      }
    }
  } else if (action_id == "mru_tab_switch_next" ||
             action_id == "mru_tab_switch_prev") {
    // Ctrl+Tab / Ctrl+Shift+Tab: dispatch through the MRU switcher overlay.
    // Both the key-event interceptor and the FocusManager accelerator path
    // resolve these bindings and route here; without this case the event was
    // swallowed and tab switching dead-ended.
    const bool forward = (action_id == "mru_tab_switch_next");
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (!browser_view || !browser_view->MaybeHandleMruTabSwitch(forward)) {
      // Switcher declined (fewer than two MRU tabs, or MRU ordering disabled
      // in prefs): fall back to positional switching.
      if (forward) {
        chrome::SelectNextTab(browser);
      } else {
        chrome::SelectPreviousTab(browser);
      }
    }
  } else if (action_id == "js_console") {
    auto* web_contents = browser->GetTabStripModel()->GetActiveWebContents();
    if (web_contents) {
      DevToolsWindow::OpenDevToolsWindow(
          web_contents, DevToolsToggleAction::ShowConsolePanel(),
          DevToolsOpenedByAction::kUnknown);
    }
  } else if (action_id.starts_with("select_tab_")) {
    int tab_num = 0;
    if (action_id.length() > 11) {
      tab_num = action_id[11] - '0';
    }
    if (tab_num >= 1 && tab_num <= 9) {
      auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
      auto* container = browser_view
                            ? static_cast<MahoSidebarContainerView*>(
                                  browser_view->maho_sidebar_container())
                            : nullptr;
      if (container && tab_num <= 8) {
        // Maho windows address the sidebar favorites with Cmd+1..8. Asking for
        // a favorite that does not exist is a no-op: the old fallback to the
        // Chromium tab strip moved focus to an unrelated tab (Cmd+2 on a
        // profile with a single favorite).
        container->TryActivateFavoriteByIndex(static_cast<size_t>(tab_num - 1));
      } else if (tab_num == 9) {
        chrome::SelectLastTab(browser);
      } else {
        chrome::SelectNumberedTab(browser, tab_num - 1);
      }
    }
  } else if (action_id == "copy_url") {
    if (auto* web_contents =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(web_contents->GetVisibleURL().spec()));
      ShowLinkCopiedToast(browser);
    }
  } else if (action_id == "copy_url_markdown") {
    if (auto* web_contents =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      std::string url = web_contents->GetVisibleURL().spec();
      std::string title = base::UTF16ToUTF8(web_contents->GetTitle());
      std::string markdown = "[" + title + "](" + url + ")";
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(markdown));
      ShowLinkCopiedToast(browser);
    }
  } else if (action_id == "command_bar" || action_id == "command_bar_alt") {
    LOG(WARNING) << kShortcutTracePrefix << " execute action=" << action_id;
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (browser_view) {
      browser_view->ShowMahoCommandOverlayForCurrentTab();
    } else {
      LOG(WARNING) << kShortcutTracePrefix
                   << " command_bar skipped: browser_view missing";
    }
  } else if (action_id == "ai_panel") {
    LOG(WARNING) << kShortcutTracePrefix << " execute action=ai_panel";
    ToggleMahoAiPanel(browser);
  } else if (action_id.starts_with("open_space:")) {
    std::string space_id = action_id.substr(std::string("open_space:").size());
    if (!space_id.empty()) {
      MahoSidebarView::ActivateSpaceAndTab(browser, space_id);
    }
  } else if (action_id == "maho_mini") {
    maho::MahoMiniRequest request;
    GURL active_url;
    if (browser && browser->GetTabStripModel() &&
        browser->GetTabStripModel()->GetActiveWebContents()) {
      active_url = browser->GetTabStripModel()
                       ->GetActiveWebContents()
                       ->GetLastCommittedURL();
    }
    request.url = active_url;
    maho::LaunchMahoMini(browser ? browser->GetProfile() : nullptr, request);
  } else if (action_id == "peek") {
    GURL active_url;
    if (browser && browser->GetTabStripModel() &&
        browser->GetTabStripModel()->GetActiveWebContents()) {
      active_url = browser->GetTabStripModel()
                       ->GetActiveWebContents()
                       ->GetLastCommittedURL();
    }
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (browser_view && active_url.is_valid()) {
      if (auto* controller = browser_view->GetOrCreateMahoPeekController()) {
        controller->ShowUrl(nullptr, active_url);
      }
    }
  } else if (action_id == "next_space" || action_id == "prev_space") {
    auto* core = maho::GetCore();
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    if (core && bridge) {
      char* spaces_json = maho_core_get_space_view_models(core);
      if (spaces_json) {
        std::optional<base::Value> spaces =
            base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
        maho_string_free(spaces_json);
        const std::string active_id = bridge->GetActiveSpaceId(browser);
        if (spaces && spaces->is_list() && !active_id.empty()) {
          const auto& list = spaces->GetList();
          for (size_t i = 0; i < list.size(); ++i) {
            const auto* dict = list[i].GetIfDict();
            if (!dict) {
              continue;
            }
            const std::string* id = dict->FindString("id");
            if (id && *id == active_id) {
              size_t target;
              if (action_id == "next_space") {
                target = (i + 1) % list.size();
              } else {
                target = (i == 0) ? list.size() - 1 : i - 1;
              }
              const auto* target_dict = list[target].GetIfDict();
              if (target_dict) {
                const std::string* target_id = target_dict->FindString("id");
                MahoSidebarView::ActivateSpaceAndTab(browser, *target_id);
              }
              break;
            }
          }
        }
      }
    }
  } else if (action_id.starts_with("select_space_")) {
    int space_num = 0;
    if (action_id.length() > 13) {
      space_num = action_id[13] - '0';
    }
    if (space_num >= 1 && space_num <= 9) {
      auto* core = maho::GetCore();
      auto* bridge = MahoSpaceProfileBridge::GetInstance();
      if (core && bridge) {
        char* spaces_json = maho_core_get_space_view_models(core);
        if (spaces_json) {
          std::optional<base::Value> spaces =
              base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
          maho_string_free(spaces_json);
          if (spaces && spaces->is_list()) {
            const auto& list = spaces->GetList();
            int idx = space_num - 1;
            if (idx < static_cast<int>(list.size())) {
              const auto* dict = list[idx].GetIfDict();
              if (dict) {
                const std::string* id = dict->FindString("id");
                MahoSidebarView::ActivateSpaceAndTab(browser, *id);
              }
            }
          }
        }
      }
    }
  } else if (action_id == "new_space") {
    if (browser) {
      BrowserView* browser_view =
          BrowserView::GetBrowserViewForBrowser(browser);
      if (browser_view) {
        for (views::View* child : browser_view->children()) {
          auto* container =
              views::AsViewClass<maho::MahoSidebarContainerView>(child);
          if (container) {
            auto* sidebar = views::AsViewClass<maho::MahoSidebarView>(
                container->sidebar_view());
            if (sidebar) {
              sidebar->ShowCreateSpace();
            }
            break;
          }
        }
      }
    }
  } else if (action_id == "show_archive") {
    // Routes through the sidebar so the Archive opens exactly like a library
    // rail selection (body mode, rail highlight, overlay, OTR guard). Toggling
    // the sidebar panel here would just duplicate `toggle_sidebar` and leave
    // the Archive unreachable from the keyboard.
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    auto* container = browser_view ? static_cast<MahoSidebarContainerView*>(
                                         browser_view->maho_sidebar_container())
                                   : nullptr;
    if (container) {
      auto* sidebar =
          views::AsViewClass<maho::MahoSidebarView>(container->sidebar_view());
      if (sidebar) {
        sidebar->ToggleArchiveMode();
      }
    }
  } else if (action_id == "toggle_spaces_overlay") {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    auto* container = browser_view ? static_cast<MahoSidebarContainerView*>(
                                         browser_view->maho_sidebar_container())
                                   : nullptr;
    if (container) {
      if (browser->GetProfile() && browser->GetProfile()->IsOffTheRecord()) {
        return;
      }
      auto* sidebar =
          views::AsViewClass<maho::MahoSidebarView>(container->sidebar_view());
      const bool is_spaces_open =
          container->IsSpacesOverlayVisible() ||
          (sidebar && sidebar->IsInLibraryMode() &&
           sidebar->GetActiveLibraryCategory() ==
               MahoSidebarLibraryRailView::Category::kSpaces);
      if (is_spaces_open) {
        if (sidebar && sidebar->IsInLibraryMode()) {
          sidebar->ExitLibraryToTabs();
        }
        if (container->IsSpacesOverlayVisible()) {
          container->DismissSpacesOverlay();
        }
      } else if (sidebar) {
        sidebar->OpenLibraryCategory(
            MahoSidebarLibraryRailView::Category::kSpaces);
      } else {
        container->ToggleSpacesOverlay();
      }
    }
  } else if (action_id == "open_boost_editor") {
    MahoBoostWindowController::GetForBrowser(browser, browser->GetProfile())
        .ShowForActiveDomain(
            browser->GetTabStripModel()->GetActiveWebContents());
  } else if (action_id == "capture_screenshot") {
    if (auto* web_contents =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      MahoDomScreenshotHandler::TriggerCapture(web_contents);
    }
  } else if (action_id == "open_atc_rules") {
    // "maho-mini" is the pane that owns the URL routing rules (ATC). An empty
    // pane key is not targeted at all (see OpenMahoSettingsPane) and would drop
    // the user on the settings root.
    maho::OpenMahoSettingsPane(browser, "maho-mini");
  } else if (const MahoMailCommand* mail_command =
                 FindMahoMailCommand(action_id)) {
    MahoMailService* mail_service =
        MahoMailServiceFactory::GetForProfileIfExists(browser->GetProfile());
    constexpr bool kMailPlatformSupported =
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
        true;
#else
        false;
#endif
    const bool mail_available =
        mail_service && mail_service->IsCommandAvailable();
    if (!CanExecuteMailCommandForTesting(
            /*feature_enabled=*/mail_available,
            /*helper_available=*/mail_available, kMailPlatformSupported)) {
      return;
    }
    const GURL mail_url(std::string(maho::kMahoMailURL) +
                        std::string(mail_command->path));
    TabStripModel* tab_strip = browser->GetTabStripModel();
    if (tab_strip) {
      for (int i = 0; i < tab_strip->count(); ++i) {
        content::WebContents* contents = tab_strip->GetWebContentsAt(i);
        if (contents &&
            contents->GetLastCommittedURL().host() == maho::kMahoMailHost) {
          tab_strip->ActivateTabAt(i);
          contents->GetController().LoadURL(
              mail_url, content::Referrer(), ui::PAGE_TRANSITION_GENERATED,
              std::string());
          return;
        }
      }
    }
    NavigateParams params(browser, mail_url, ui::PAGE_TRANSITION_GENERATED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  } else if (action_id == "clear_unpinned_tabs") {
    TabStripModel* model = browser->GetTabStripModel();
    if (model) {
      std::vector<int> indices;
      for (int i = model->count() - 1; i >= 0; --i) {
        if (!model->IsTabPinned(i)) {
          indices.push_back(i);
        }
      }
      for (int i : indices) {
        model->CloseWebContentsAt(i,
                                  TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      }
    }
  } else {
    LOG(WARNING) << "Unknown shortcut action: " << action_id;
  }
}

void OpenCommandSuggestion(Browser* browser,
                           const CommandSuggestion& suggestion,
                           WindowOpenDisposition disposition) {
  DCHECK(browser);
  if (!suggestion.action_id.empty()) {
    ExecuteCommandAction(browser, suggestion.action_id);
    return;
  }

  if (suggestion.type == CommandSuggestionType::kRecentSearch) {
    if (suggestion.title.empty()) {
      return;
    }

    auto* core = maho::GetCore();
    if (!core) {
      return;
    }

    char* search_url = maho_core_get_search_url(core, suggestion.title.c_str());
    if (!search_url) {
      return;
    }

    GURL gurl(search_url);
    maho_string_free(search_url);

    NavigateParams params(browser, gurl, ui::PAGE_TRANSITION_GENERATED);
    params.disposition = disposition;
    Navigate(&params);
    return;
  }

  if (suggestion.type == CommandSuggestionType::kSearch) {
    if (suggestion.execution_payload.empty()) {
      return;
    }
    GURL gurl(suggestion.execution_payload);
    if (!gurl.SchemeIsHTTPOrHTTPS()) {
      return;
    }
    NavigateParams params(browser, gurl, ui::PAGE_TRANSITION_GENERATED);
    params.disposition = disposition;
    Navigate(&params);
    return;
  }

  if (suggestion.type == CommandSuggestionType::kCalculator ||
      suggestion.type == CommandSuggestionType::kUnitConversion) {
    if (suggestion.title.empty()) {
      return;
    }

    std::string text_to_copy = suggestion.title;
    if (suggestion.type == CommandSuggestionType::kCalculator) {
      constexpr char kCalculatorSeparator[] = " = ";
      const size_t separator = text_to_copy.rfind(kCalculatorSeparator);
      if (separator != std::string::npos) {
        text_to_copy =
            text_to_copy.substr(separator + sizeof(kCalculatorSeparator) - 1);
      }
    }

    CopyTextToClipboard(text_to_copy);
    return;
  }

  if (!suggestion.browser_session_id.empty() &&
      !suggestion.tab_session_id.empty()) {
    BrowserWindowInterface* target_browser =
        FindBrowserForStableTab(suggestion);
    if (target_browser) {
      if (target_browser != browser) {
        target_browser->GetWindow()->Show();
        target_browser->GetWindow()->Activate();
      }
      int tab_index =
          FindTabIndexForStableId(target_browser, suggestion.tab_session_id);
      if (tab_index >= 0) {
        target_browser->GetTabStripModel()->ActivateTabAt(tab_index);
        return;
      }
    }
  }

  // Fallback: kTab suggestions emitted by Rust use key="tab:{stable_id}" with
  // no browser/tab session IDs. Resolve via MahoTabIdHelper UUID match.
  if (suggestion.type == CommandSuggestionType::kTab &&
      !suggestion.stable_tab_id.empty()) {
    BrowserWindowInterface* target_browser =
        FindBrowserForHelperStableId(browser, suggestion.stable_tab_id);
    if (target_browser) {
      if (target_browser != browser) {
        target_browser->GetWindow()->Show();
        target_browser->GetWindow()->Activate();
      }
      int tab_index = FindTabIndexForHelperStableId(target_browser,
                                                    suggestion.stable_tab_id);
      if (tab_index >= 0) {
        target_browser->GetTabStripModel()->ActivateTabAt(tab_index);
        return;
      }
    }
  }

  if (suggestion.tab_index >= 0) {
    TabStripModel* model = browser->GetTabStripModel();
    DCHECK(model);
    if (suggestion.tab_index < model->count()) {
      model->ActivateTabAt(suggestion.tab_index);
      return;
    }
  }

  if (suggestion.type == CommandSuggestionType::kTab &&
      suggestion.is_suspended && !suggestion.tab_core_id.empty()) {
    // A previously-woken suspended tab adopts tab_core_id as its
    // MahoTabIdHelper stable id, so check for a live instance first.
    BrowserWindowInterface* live =
        FindBrowserForHelperStableId(browser, suggestion.tab_core_id);
    if (live) {
      if (live != browser) {
        live->GetWindow()->Show();
        live->GetWindow()->Activate();
      }
      int idx = FindTabIndexForHelperStableId(live, suggestion.tab_core_id);
      if (idx >= 0) {
        live->GetTabStripModel()->ActivateTabAt(idx);
        return;
      }
    }
    // Not live: wake/restore the persisted tab via the sidebar tab list view,
    // reusing WakeSuspendedTab so scroll/pin state is restored and no duplicate
    // tab is created.
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    auto* container = browser_view ? static_cast<MahoSidebarContainerView*>(
                                         browser_view->maho_sidebar_container())
                                   : nullptr;
    if (container) {
      auto* sidebar =
          views::AsViewClass<maho::MahoSidebarView>(container->sidebar_view());
      if (sidebar && sidebar->tab_list_view()) {
        sidebar->tab_list_view()->WakeSuspendedTab(suggestion.tab_core_id);
      }
    }
    return;
  }

  GURL gurl(suggestion.execution_payload);
  if (!gurl.SchemeIsHTTPOrHTTPS() && !gurl.SchemeIs("chrome") &&
      !IsApprovedMahoAliasUrl(gurl)) {
    return;
  }

  if (gurl.spec() == maho::kMahoAIURL ||
      gurl.spec() == maho::kMahoAIPublicURL) {
    browser->GetFeatures().side_panel_ui()->Show(
        SidePanelEntryId::kMahoAiPanel);
    return;
  }

  if (gurl.spec() == maho::kMahoSettingsURL ||
      gurl.spec() == maho::kMahoSettingsPublicURL) {
    maho::OpenMahoSettingsPane(browser, "");
    return;
  }

  if (suggestion.type == CommandSuggestionType::kFolder) {
    if (suggestion.folder_id.empty()) {
      return;
    }
    auto* bridge = MahoSpaceProfileBridge::GetInstance();
    const std::string space_id =
        bridge ? bridge->GetActiveSpaceId() : std::string();
    if (space_id.empty()) {
      return;
    }
    if (browser->GetProfile() && !maho::sidebar_prefs::IsSidebarPanelExpanded(
                                  browser->GetProfile()->GetPrefs())) {
      maho::sidebar_prefs::ToggleSidebarPanelExpanded(
          browser->GetProfile()->GetPrefs());
    }
    DispatchShellEvent(
        "toggle_folder_expanded",
        {{"space_id", space_id}, {"folder_id", suggestion.folder_id}});
    return;
  }

  NavigateParams params(browser, gurl, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = disposition;
  Navigate(&params);
}

bool MahoCommandActionHandler::OnQuerySubmitted(
    Browser* browser,
    const std::string& query,
    PaletteAction action,
    MahoPrivateContextClass context_class) {
  if (action != PaletteAction::kAskMaho || !browser) {
    return false;
  }
  if (browser->GetProfile()->IsOffTheRecord()) {
    return false;
  }
  if (context_class != MahoPrivateContextClass::kRegular) {
    return false;
  }

  const std::string trimmed_query(
      base::TrimWhitespaceASCII(query, base::TRIM_ALL));
  if (trimmed_query.empty()) {
    return false;
  }

  auto dispatch = maho_ai::mojom::AskMahoDispatch::New();
  dispatch->request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  dispatch->query = trimmed_query;
  dispatch->source = maho_ai::mojom::AskMahoSource::kCommandPalette;
  dispatch->submit = true;
  dispatch->mode = maho_ai::mojom::InteractionMode::kAssistant;
  dispatch->context_intent = maho_ai::mojom::AskMahoContextIntent::kNone;
  dispatch->target_session_id = std::nullopt;

  MahoAiIngressCoordinator::GetOrCreateForBrowser(browser)->Dispatch(
      std::move(dispatch));

  bool has_popup = maho::MahoAiPopupLifetimeTracker::Get()->GetPopupForOpener(
                       browser) != nullptr;
  if (!has_popup) {
    auto* side_panel_ui = browser->GetFeatures().side_panel_ui();
    const bool ai_panel_showing =
        side_panel_ui &&
        side_panel_ui->GetCurrentEntryId() ==
            SidePanelEntryId::kMahoAiPanel;
    if (!ai_panel_showing && side_panel_ui) {
      side_panel_ui->Show(SidePanelEntryKey(SidePanelEntryId::kMahoAiPanel),
                          SidePanelOpenTrigger::kToolbarButton);
    }
  }

  return true;
}

}  // namespace maho
