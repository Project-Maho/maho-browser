// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_command/maho_command_page_handler.h"

#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace {

maho_command::mojom::SuggestionType ParseSuggestionType(
    const std::string& kind) {
  if (kind == "tab")
    return maho_command::mojom::SuggestionType::kTab;
  if (kind == "bookmark")
    return maho_command::mojom::SuggestionType::kBookmark;
  if (kind == "history")
    return maho_command::mojom::SuggestionType::kHistory;
  if (kind == "action")
    return maho_command::mojom::SuggestionType::kAction;
  if (kind == "navigation")
    return maho_command::mojom::SuggestionType::kNavigation;
  if (kind == "calculator")
    return maho_command::mojom::SuggestionType::kCalculator;
  if (kind == "unitconversion")
    return maho_command::mojom::SuggestionType::kUnitConversion;
  if (kind == "archivedtab")
    return maho_command::mojom::SuggestionType::kArchivedTab;
  if (kind == "closedtab")
    return maho_command::mojom::SuggestionType::kClosedTab;
  return maho_command::mojom::SuggestionType::kHistory;
}

int32_t ExtractTabIndex(const std::string& key) {
  // key format: "tab:tab_N" where N is the TabStripModel index
  if (!key.starts_with("tab:tab_"))
    return -1;
  int32_t index = -1;
  base::StringToInt(key.substr(8), &index);
  return index;
}

std::optional<std::string> ExtractActionId(const std::string& key) {
  if (!key.starts_with("action:"))
    return std::nullopt;
  return key.substr(7);
}

}  // namespace

MahoCommandPageHandler::MahoCommandPageHandler(
    mojo::PendingReceiver<maho_command::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_command::mojom::Page> page,
    Browser* browser)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser) {}

MahoCommandPageHandler::~MahoCommandPageHandler() = default;

std::string MahoCommandPageHandler::BuildTabsJson() {
  base::ListValue tabs;
  TabStripModel* model = browser_->tab_strip_model();
  for (int i = 0; i < model->count(); ++i) {
    auto* wc = model->GetWebContentsAt(i);
    base::DictValue tab;
    tab.Set("id", "tab_" + base::NumberToString(i));
    tab.Set("title", base::UTF16ToUTF8(wc->GetTitle()));
    tab.Set("url", wc->GetVisibleURL().spec());
    tabs.Append(std::move(tab));
  }
  std::string json;
  base::JSONWriter::Write(tabs, &json);
  return json;
}

// static
std::vector<maho_command::mojom::SuggestionPtr>
MahoCommandPageHandler::SearchOnPool(const std::string& query,
                                     const std::string& mode,
                                     const std::string& tabs_json,
                                     bool is_incognito) {
  std::vector<maho_command::mojom::SuggestionPtr> results;

  MahoCore* core = maho::GetCore();
  if (!core)
    return results;

  char* json = maho_core_command_bar_search(core, query.c_str(),
                                            mode.c_str(), tabs_json.c_str(),
                                            is_incognito);
  if (!json)
    return results;

  std::string json_str(json);
  maho_string_free(json);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list())
    return results;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict)
      continue;

    const std::string* kind = dict->FindString("kind");
    const std::string* title = dict->FindString("title");
    const std::string* key = dict->FindString("key");
    if (!kind || !title || !key)
      continue;

    auto suggestion = maho_command::mojom::Suggestion::New();
    suggestion->title = *title;
    suggestion->key = *key;
    suggestion->type = ParseSuggestionType(*kind);

    const std::string* subtitle = dict->FindString("subtitle");
    suggestion->subtitle = subtitle ? *subtitle : "";

    const std::string* payload = dict->FindString("executionPayload");
    suggestion->url = payload ? *payload : "";

    suggestion->tab_index = ExtractTabIndex(*key);

    auto action_id = ExtractActionId(*key);
    if (action_id.has_value())
      suggestion->action_id = *action_id;

    const auto* ranges = dict->FindList("matchRanges");
    if (ranges) {
      for (const auto& range : *ranges) {
        const auto* pair = range.GetIfList();
        if (pair && pair->size() == 2) {
          auto mr = maho_command::mojom::MatchRange::New();
          mr->start = (*pair)[0].GetIfInt().value_or(0);
          mr->end = (*pair)[1].GetIfInt().value_or(0);
          suggestion->match_ranges.push_back(std::move(mr));
        }
      }
    }

    results.push_back(std::move(suggestion));
  }

  return results;
}

void MahoCommandPageHandler::OnSearchResults(
    QueryCallback callback,
    std::vector<maho_command::mojom::SuggestionPtr> results) {
  std::move(callback).Run(std::move(results));
}

void MahoCommandPageHandler::Query(const std::string& input,
                                   QueryCallback callback) {
  if (input.empty()) {
    std::move(callback).Run({});
    return;
  }

  std::string tabs_json = BuildTabsJson();
  const std::string mode = "normal";
  const bool is_incognito = browser_->profile()->IsOffTheRecord();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoCommandPageHandler::SearchOnPool, input, mode,
                     tabs_json, is_incognito),
      base::BindOnce(&MahoCommandPageHandler::OnSearchResults,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoCommandPageHandler::OpenSuggestion(int32_t /*index*/,
                                            const std::string& url,
                                            int32_t tab_index) {
  if (tab_index >= 0) {
    TabStripModel* model = browser_->tab_strip_model();
    if (tab_index < model->count()) {
      model->ActivateTabAt(tab_index);
      return;
    }
  }

  GURL gurl(url);
  if (!gurl.SchemeIsHTTPOrHTTPS() && !gurl.SchemeIs("chrome"))
    return;

  if (gurl.spec() == maho::kMahoAIURL) {
    browser_->GetFeatures().side_panel_ui()->Show(
        SidePanelEntryId::kMahoAiPanel);
    return;
  }

  if (gurl.spec() == maho::kMahoSettingsURL) {
    if (!chrome::ActivateMahoSettingsTabIfOpen(browser_)) {
      content::OpenURLParams params(gurl, content::Referrer(),
                                    WindowOpenDisposition::NEW_WINDOW,
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK, false);
      browser_->OpenURL(params, /*navigation_handle_callback=*/{});
    }
    return;
  }

  NavigateParams params(browser_, gurl, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&params);
}

void MahoCommandPageHandler::ExecuteAction(const std::string& action_id) {
  if (action_id == "new_tab") {
    chrome::NewTab(browser_);
  } else if (action_id == "close_tab") {
    chrome::CloseTab(browser_);
  } else if (action_id == "reload_tab") {
    chrome::Reload(browser_, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "hard_reload") {
    chrome::ReloadBypassingCache(browser_,
                                 WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "duplicate_tab") {
    chrome::DuplicateTab(browser_);
  } else if (action_id == "restore_tab") {
    chrome::RestoreTab(browser_);
  } else if (action_id == "pin_tab") {
    chrome::PinTab(browser_);
  } else if (action_id == "find_in_page") {
    chrome::Find(browser_);
  } else if (action_id == "zoom_in") {
    chrome::Zoom(browser_, content::PAGE_ZOOM_IN);
  } else if (action_id == "zoom_out") {
    chrome::Zoom(browser_, content::PAGE_ZOOM_OUT);
  } else if (action_id == "reset_zoom") {
    chrome::Zoom(browser_, content::PAGE_ZOOM_RESET);
  } else if (action_id == "toggle_dev_tools") {
    chrome::ToggleDevToolsWindow(browser_,
                                 DevToolsToggleAction::Toggle(),
                                 DevToolsOpenedByAction::kUnknown);
  } else if (action_id == "print_page") {
    chrome::Print(browser_);
  } else if (action_id == "view_source") {
    if (auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents()) {
      web_contents->GetPrimaryMainFrame()->ViewSource();
    }
  } else if (action_id == "settings") {
    GURL settings_url(maho::kMahoSettingsURL);
    NavigateParams params(browser_, settings_url,
                          ui::PAGE_TRANSITION_GENERATED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  } else if (action_id == "open_history") {
    chrome::ShowHistory(browser_);
  } else if (action_id == "open_bookmarks") {
    chrome::ShowBookmarkManager(browser_);
  } else if (action_id == "open_downloads") {
    chrome::ShowDownloads(browser_);
  } else if (action_id == "new_incognito") {
    chrome::NewIncognitoWindow(browser_->profile());
  } else if (action_id == "focus_url_bar") {
    chrome::FocusLocationBar(browser_);
  } else {
    MahoCore* core = maho::GetCore();
    if (core) {
      std::string event_json = "{\"type\":\"CommandBarAction\",\"action\":\"" +
                               action_id + "\"}";
      char* result = maho_core_handle_event(core, event_json.c_str());
      if (result)
        maho_string_free(result);
    }
  }
}
