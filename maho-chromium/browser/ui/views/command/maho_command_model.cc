// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_model.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

namespace {
constexpr base::TimeDelta kDebounceDelay = base::Milliseconds(120);
}  // namespace

CommandSuggestion::CommandSuggestion() = default;
CommandSuggestion::CommandSuggestion(const CommandSuggestion&) = default;
CommandSuggestion::CommandSuggestion(CommandSuggestion&&) = default;
CommandSuggestion& CommandSuggestion::operator=(const CommandSuggestion&) = default;
CommandSuggestion& CommandSuggestion::operator=(CommandSuggestion&&) = default;
CommandSuggestion::~CommandSuggestion() = default;

MahoCommandModel::MahoCommandModel(Browser* browser) : browser_(browser) {}

MahoCommandModel::~MahoCommandModel() = default;

void MahoCommandModel::Search(const std::string& query,
                              ResultsCallback callback) {
  query_ = query;
  selected_index_ = 0;
  pending_callback_ = callback;

  if (query.empty()) {
    debounce_timer_.Stop();
    results_.clear();
    callback.Run({});
    return;
  }

  debounce_timer_.Start(FROM_HERE, kDebounceDelay,
                        base::BindOnce(&MahoCommandModel::FireSearch,
                                       weak_factory_.GetWeakPtr()));
}

void MahoCommandModel::Cancel() {
  debounce_timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
}

void MahoCommandModel::FireSearch() {
  std::string search_query = query_;
  actions_only_ = false;

  if (!search_query.empty() && search_query[0] == '>') {
    search_query = search_query.substr(1);
    if (!search_query.empty() && search_query[0] == ' ')
      search_query = search_query.substr(1);
    actions_only_ = true;
  }

  std::string tabs_json = BuildTabsJson(browser_->tab_strip_model());

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoCommandModel::SearchOnPool, search_query, tabs_json),
      base::BindOnce(&MahoCommandModel::OnResults, weak_factory_.GetWeakPtr(),
                     pending_callback_));
}

// static
std::string MahoCommandModel::BuildTabsJson(TabStripModel* model) {
  base::ListValue tabs;
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
std::vector<CommandSuggestion> MahoCommandModel::SearchOnPool(
    const std::string& query,
    const std::string& tabs_json) {
  std::vector<CommandSuggestion> results;

  MahoCore* core = maho::GetCore();
  if (!core)
    return results;

  char* json = maho_core_command_bar_search(core, query.c_str(),
                                            tabs_json.c_str());
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

    CommandSuggestion suggestion;
    suggestion.type = ParseType(*kind);
    suggestion.key = *key;
    suggestion.title = *title;

    const std::string* subtitle = dict->FindString("subtitle");
    if (subtitle)
      suggestion.subtitle = *subtitle;

    const std::string* payload = dict->FindString("executionPayload");
    if (payload)
      suggestion.execution_payload = *payload;

    if (key->starts_with("tab:tab_")) {
      int32_t index = -1;
      base::StringToInt(key->substr(8), &index);
      suggestion.tab_index = index;
    }

    if (key->starts_with("action:"))
      suggestion.action_id = key->substr(7);

    const auto* ranges = dict->FindList("matchRanges");
    if (ranges) {
      for (const auto& range : *ranges) {
        const auto* pair = range.GetIfList();
        if (pair && pair->size() == 2) {
          suggestion.match_ranges.emplace_back(
              (*pair)[0].GetIfInt().value_or(0),
              (*pair)[1].GetIfInt().value_or(0));
        }
      }
    }

    results.push_back(std::move(suggestion));
  }

  return results;
}

// static
CommandSuggestionType MahoCommandModel::ParseType(const std::string& kind) {
  if (kind == "tab")
    return CommandSuggestionType::kTab;
  if (kind == "bookmark")
    return CommandSuggestionType::kBookmark;
  if (kind == "history")
    return CommandSuggestionType::kHistory;
  if (kind == "action")
    return CommandSuggestionType::kAction;
  if (kind == "navigation")
    return CommandSuggestionType::kNavigation;
  if (kind == "calculator")
    return CommandSuggestionType::kCalculator;
  if (kind == "unitconversion")
    return CommandSuggestionType::kUnitConversion;
  if (kind == "archivedtab")
    return CommandSuggestionType::kArchivedTab;
  if (kind == "closedtab")
    return CommandSuggestionType::kClosedTab;
  return CommandSuggestionType::kHistory;
}

void MahoCommandModel::OnResults(ResultsCallback callback,
                                 std::vector<CommandSuggestion> results) {
  if (actions_only_) {
    std::erase_if(results, [](const CommandSuggestion& s) {
      return s.type != CommandSuggestionType::kAction;
    });
  }
  results_ = std::move(results);
  selected_index_ = results_.empty() ? -1 : 0;
  callback.Run(results_);
}

}  // namespace maho
