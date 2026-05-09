// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_model.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/favicon/large_icon_service_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver.h"
#include "components/favicon/core/large_icon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/sessions/content/session_tab_helper.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"


namespace maho {

namespace {
constexpr base::TimeDelta kDebounceDelay = base::Milliseconds(150);

const net::NetworkTrafficAnnotationTag kBookmarkFaviconTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation(
        "maho_command_palette_bookmark_favicon", R"(
      semantics {
        sender: "Maho Command Palette"
        description:
          "Requests a favicon from Google's favicon service when a bookmark "
          "result shown in the command palette has no locally cached favicon."
        trigger:
          "The user opens the command palette and sees bookmark suggestions "
          "whose favicons are not stored in the local favicon database."
        data: "Bookmark page URL and desired favicon size."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting:
          "This request follows Chromium's existing Google favicon fallback "
          "behavior for UI surfaces when the icon is not available locally."
        policy_exception_justification:
          "A direct user-visible bookmark result needs its favicon for parity "
          "with other Chromium surfaces."
      })");

bool IsEligibleBrowserForTabSearch(const BrowserWindowInterface* browser,
                                   const Profile* profile) {
  return browser && browser->GetWindow() && browser->GetTabStripModel() &&
         browser->GetProfile() && profile &&
         browser->GetProfile() == profile &&
         browser->GetProfile()->IsOffTheRecord() ==
             profile->IsOffTheRecord();
}

std::string BuildStableTabId(const BrowserWindowInterface* browser,
                             content::WebContents* contents) {
  auto* tab_helper = sessions::SessionTabHelper::FromWebContents(contents);
  if (!browser || !contents || !tab_helper) {
    return std::string();
  }

  return base::NumberToString(browser->GetSessionID().id()) + ":" +
         base::NumberToString(tab_helper->session_id().id());
}

std::optional<ImageData> ParseImageData(const base::Value& value) {
  const auto* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }

  const auto* data_list = dict->FindList("data");
  if (!data_list) {
    return std::nullopt;
  }

  ImageData icon;
  icon.width = dict->FindInt("width").value_or(0);
  icon.height = dict->FindInt("height").value_or(0);
  const std::string* format = dict->FindString("format");
  if (format)
    icon.format = *format;

  icon.data.reserve(data_list->size());
  for (const auto& data_value : *data_list) {
    if (!data_value.is_int())
      return std::nullopt;
    const int byte = data_value.GetInt();
    if (byte < 0 || byte > 255)
      return std::nullopt;
    icon.data.push_back(static_cast<uint8_t>(byte));
  }

  return icon;
}

std::optional<ImageData> BuildImageDataFromFavicon(const ui::ImageModel& favicon) {
  gfx::Image favicon_image(favicon.Rasterize(nullptr));
  if (favicon_image.IsEmpty()) {
    return std::nullopt;
  }
  scoped_refptr<base::RefCountedMemory> favicon_bytes =
      favicon_image.As1xPNGBytes();
  if (!favicon_bytes || favicon_bytes->size() == 0u) {
    return std::nullopt;
  }

  ImageData icon;
  icon.width = favicon_image.Width();
  icon.height = favicon_image.Height();
  icon.format = "png";
  icon.data.assign(base::span(*favicon_bytes).begin(),
                   base::span(*favicon_bytes).end());
  return icon;
}

std::optional<ImageData> BuildImageDataFromImage(const gfx::Image& favicon_image) {
  if (favicon_image.IsEmpty()) {
    return std::nullopt;
  }
  scoped_refptr<base::RefCountedMemory> favicon_bytes =
      favicon_image.As1xPNGBytes();
  if (!favicon_bytes || favicon_bytes->size() == 0u) {
    return std::nullopt;
  }

  ImageData icon;
  icon.width = favicon_image.Width();
  icon.height = favicon_image.Height();
  icon.format = "png";
  icon.data.assign(base::span(*favicon_bytes).begin(),
                   base::span(*favicon_bytes).end());
  return icon;
}

std::optional<ImageData> BuildImageDataFromTab(tabs::TabInterface* tab) {
  if (!tab) {
    return std::nullopt;
  }

  if (content::WebContents* contents = tab->GetContents()) {
    content::NavigationEntry* entry = contents->GetController().GetVisibleEntry();
    if (entry) {
      std::optional<ImageData> icon =
          BuildImageDataFromImage(entry->GetFavicon().image);
      if (icon.has_value()) {
        return icon;
      }
    }
  }

  TabUIHelper* helper = TabUIHelper::From(tab);
  if (!helper) {
    return std::nullopt;
  }

  if (content::WebContents* contents = tab->GetContents()) {
    favicon::FaviconDriver* favicon_driver =
        favicon::ContentFaviconDriver::FromWebContents(contents);
    if (favicon_driver) {
      std::optional<ImageData> icon =
          BuildImageDataFromImage(favicon_driver->GetFavicon());
      if (icon.has_value()) {
        return icon;
      }
    }
  }

  return BuildImageDataFromFavicon(helper->GetFavicon());
}

std::optional<ImageData> LookupOpenTabIconForUrl(Browser* browser,
                                                 const GURL& url) {
  if (!browser || url.is_empty() || !browser->profile()) {
    return std::nullopt;
  }

  std::optional<ImageData> icon;
  const Profile* profile = browser->profile();
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&icon, profile, &url](BrowserWindowInterface* candidate) {
        if (icon.has_value()) {
          return false;
        }
        if (!IsEligibleBrowserForTabSearch(candidate, profile)) {
          return true;
        }

        TabStripModel* model = candidate->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          auto* tab = model->GetTabAtIndex(i);
          auto* contents = model->GetWebContentsAt(i);
          if (!tab || !contents || contents->GetVisibleURL() != url) {
            continue;
          }
          icon = BuildImageDataFromTab(tab);
          if (icon.has_value()) {
            return false;
          }
        }
        return true;
      });
  return icon;
}

void CollectBookmarkSuggestions(bookmarks::BookmarkModel* bookmark_model,
                                const bookmarks::BookmarkNode* node,
                                Browser* browser,
                                const std::string& query_lower,
                                std::vector<CommandSuggestion>& out) {
  if (!node) {
    return;
  }

  for (const auto& child : node->children()) {
    if (!child) {
      continue;
    }

    if (child->is_url()) {
      const GURL& url = child->url();
      const std::string title = base::UTF16ToUTF8(child->GetTitle());
      const std::string url_spec = url.spec();
      if (!query_lower.empty()) {
        const std::string title_lower = base::ToLowerASCII(title);
        const std::string url_lower = base::ToLowerASCII(url_spec);
        if (title_lower.find(query_lower) == std::string::npos &&
            url_lower.find(query_lower) == std::string::npos) {
          continue;
        }
      }

      CommandSuggestion suggestion;
      suggestion.type = CommandSuggestionType::kBookmark;
      suggestion.key = "bookmark:" + base::NumberToString(child->id());
      suggestion.title = title;
      suggestion.subtitle = url_spec;
      suggestion.execution_payload = url_spec;
      if (bookmark_model) {
        suggestion.icon =
            BuildImageDataFromImage(bookmark_model->GetFavicon(child.get()));
      }
      if (!suggestion.icon.has_value()) {
        suggestion.icon = LookupOpenTabIconForUrl(browser, url);
      }
      out.push_back(std::move(suggestion));
    }

    CollectBookmarkSuggestions(bookmark_model, child.get(), browser, query_lower,
                               out);
  }
}

const char* GetBackendSearchMode(CommandOverlayMode mode) {
  switch (mode) {
    case CommandOverlayMode::kCurrentTab:
      return "addressBar";
    case CommandOverlayMode::kNewTab:
      return "newTab";
    default:
      return "normal";
  }
}
}  // namespace

ImageData::ImageData() = default;
ImageData::ImageData(const ImageData&) = default;
ImageData::ImageData(ImageData&&) = default;
ImageData& ImageData::operator=(const ImageData&) = default;
ImageData& ImageData::operator=(ImageData&&) = default;
ImageData::~ImageData() = default;

CommandSuggestion::CommandSuggestion() = default;
CommandSuggestion::CommandSuggestion(const CommandSuggestion&) = default;
CommandSuggestion::CommandSuggestion(CommandSuggestion&&) = default;
CommandSuggestion& CommandSuggestion::operator=(const CommandSuggestion&) = default;
CommandSuggestion& CommandSuggestion::operator=(CommandSuggestion&&) = default;
CommandSuggestion::~CommandSuggestion() = default;

MahoCommandModel::MahoCommandModel(Browser* browser) : browser_(browser) {
  HydrateSiteSearchEngines();
}

MahoCommandModel::~MahoCommandModel() = default;

// static
std::vector<CommandSuggestion>
MahoCommandModel::BuildChromiumBookmarkSuggestions(Browser* browser,
                                                   const std::string& query) {
  std::vector<CommandSuggestion> suggestions;
  if (!browser || !browser->profile()) {
    return suggestions;
  }

  bookmarks::BookmarkModel* bookmark_model =
      BookmarkModelFactory::GetForBrowserContext(browser->profile());
  if (!bookmark_model || !bookmark_model->loaded()) {
    return suggestions;
  }

  const std::string query_lower = base::ToLowerASCII(query);
  CollectBookmarkSuggestions(bookmark_model, bookmark_model->bookmark_bar_node(),
                             browser,
                             query_lower, suggestions);
  CollectBookmarkSuggestions(bookmark_model, bookmark_model->other_node(),
                             browser,
                             query_lower, suggestions);
  return suggestions;
}

void MahoCommandModel::Search(const std::string& query,
                              CommandOverlayMode mode,
                              ResultsCallback callback) {
  query_ = query;
  mode_ = mode;
  selected_index_ = 0;
  pending_callback_ = callback;

  if (query.empty()) {
    debounce_timer_.Stop();
    results_.clear();
    if (mode == CommandOverlayMode::kNewTab) {
      debounce_timer_.Start(FROM_HERE, kDebounceDelay,
                            base::BindOnce(&MahoCommandModel::FireSearch,
                                           weak_factory_.GetWeakPtr()));
      return;
    }
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
  ++search_generation_;
  std::string search_query = query_;
  actions_only_ = false;

  if (!search_query.empty() && search_query[0] == '>') {
    search_query = search_query.substr(1);
    if (!search_query.empty() && search_query[0] == ' ')
      search_query = search_query.substr(1);
    actions_only_ = true;
  }

  std::string tabs_json = BuildTabsJson(browser_);
  std::vector<CommandSuggestion> chromium_bookmarks =
      BuildChromiumBookmarkSuggestions(browser_, search_query);
  const bool is_incognito = browser_->profile()->IsOffTheRecord();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoCommandModel::SearchOnPool, search_query, tabs_json,
                     chromium_bookmarks, mode_, is_incognito),
      base::BindOnce(&MahoCommandModel::OnResults, weak_factory_.GetWeakPtr(),
                     pending_callback_));
}

// static
std::string MahoCommandModel::BuildTabsJson(Browser* browser) {
  base::Value tabs(base::Value::Type::LIST);
  if (!browser || !browser->window() || !browser->tab_strip_model()) {
    return std::string();
  }

  const Profile* profile = browser->profile();
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&tabs, profile](BrowserWindowInterface* candidate) {
        if (!IsEligibleBrowserForTabSearch(candidate, profile))
          return true;

        TabStripModel* model = candidate->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          auto* wc = model->GetWebContentsAt(i);
          auto* tab = model->GetTabAtIndex(i);
          auto* tab_helper = sessions::SessionTabHelper::FromWebContents(wc);
          auto* tab_ui_helper = tab ? TabUIHelper::From(tab) : nullptr;
          if (!wc || !tab_helper || !tab_ui_helper)
            continue;

          std::string stable_id = BuildStableTabId(candidate, wc);
          if (stable_id.empty())
            continue;

          base::Value tab_json(base::Value::Type::DICT);
          tab_json.GetDict().Set("id", stable_id);
          tab_json.GetDict().Set("browser_session_id",
                            base::NumberToString(
                                candidate->GetSessionID().id()));
          tab_json.GetDict().Set("tab_session_id",
                            base::NumberToString(tab_helper->session_id().id()));
          tab_json.GetDict().Set("title", base::UTF16ToUTF8(wc->GetTitle()));
          tab_json.GetDict().Set("url", wc->GetVisibleURL().spec());

          std::optional<ImageData> image_data = BuildImageDataFromTab(tab);
          if (!image_data.has_value()) {
            image_data = BuildImageDataFromFavicon(tab_ui_helper->GetFavicon());
          }
          if (image_data.has_value()) {
            base::Value icon(base::Value::Type::DICT);
            icon.GetDict().Set("width", image_data->width);
            icon.GetDict().Set("height", image_data->height);
            icon.GetDict().Set("format", image_data->format);

            base::Value data(base::Value::Type::LIST);
            for (uint8_t byte : image_data->data) {
              data.GetList().Append(static_cast<int>(byte));
            }
            icon.GetDict().Set("data", std::move(data));
            tab_json.GetDict().Set("icon", std::move(icon));
          }
          tabs.GetList().Append(std::move(tab_json));
        }
        return true;
      });

  std::string json;
  base::JSONWriter::Write(tabs, &json);
  return json;
}

// static
std::vector<CommandSuggestion> MahoCommandModel::SearchOnPool(
    const std::string& query,
    const std::string& tabs_json,
    const std::vector<CommandSuggestion>& chromium_bookmarks,
    CommandOverlayMode mode,
    bool is_incognito) {
  std::vector<CommandSuggestion> results;

  MahoCore* core = maho::GetCore();
  if (!core)
    return results;

  char* json = maho_core_command_bar_search(core, query.c_str(),
                                            GetBackendSearchMode(mode),
                                            tabs_json.c_str(), is_incognito);
  if (!json)
    return results;

  std::string json_str(json);
  maho_string_free(json);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list())
    return chromium_bookmarks;

  const bool has_core_bookmarks = std::ranges::any_of(
      parsed->GetList(), [](const base::Value& item) {
        const auto* dict = item.GetIfDict();
        const std::string* kind = dict ? dict->FindString("kind") : nullptr;
        return kind && *kind == "bookmark";
      });

  std::map<std::string, ImageData> chromium_bookmark_icons_by_payload;
  for (const auto& bookmark : chromium_bookmarks) {
    if (!bookmark.execution_payload.empty() && bookmark.icon.has_value()) {
      chromium_bookmark_icons_by_payload.emplace(bookmark.execution_payload,
                                                 *bookmark.icon);
    }
  }

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

    const base::Value* icon_value = dict->Find("icon");
    if (icon_value)
      suggestion.icon = ParseImageData(*icon_value);

    if (suggestion.type == CommandSuggestionType::kBookmark &&
        !suggestion.icon.has_value() && !suggestion.execution_payload.empty()) {
      auto it = chromium_bookmark_icons_by_payload.find(
          suggestion.execution_payload);
      if (it != chromium_bookmark_icons_by_payload.end()) {
        suggestion.icon = it->second;
      }
    }

    const std::string* shortcut = dict->FindString("shortcut");
    if (shortcut)
      suggestion.shortcut_label = *shortcut;

    if (key->starts_with("tab:")) {
      const std::string tab_key = key->substr(4);
      const size_t separator = tab_key.find(':');
      if (separator != std::string::npos) {
        suggestion.browser_session_id = tab_key.substr(0, separator);
        suggestion.tab_session_id = tab_key.substr(separator + 1);
      } else if (tab_key.starts_with("tab_")) {
        int32_t index = -1;
        base::StringToInt(tab_key.substr(4), &index);
        suggestion.tab_index = index;
      }
    }

    if (key->starts_with("action:"))
      suggestion.action_id = key->substr(7);
    if (key->starts_with("folder:"))
      suggestion.folder_id = key->substr(7);

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

  if (!has_core_bookmarks) {
    for (auto& bookmark : chromium_bookmarks) {
      results.push_back(std::move(bookmark));
    }
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
  if (kind == "folder")
    return CommandSuggestionType::kFolder;
  if (kind == "recentsearch")
    return CommandSuggestionType::kRecentSearch;
  return CommandSuggestionType::kHistory;
}

void MahoCommandModel::OnResults(ResultsCallback callback,
                                 std::vector<CommandSuggestion> results) {
  if (actions_only_) {
    std::erase_if(results, [](const CommandSuggestion& s) {
      return s.type != CommandSuggestionType::kAction;
    });
  } else {
    CapResults(results);
  }

  std::string prev_key;
  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(results_.size())) {
    prev_key = results_[selected_index_].key;
  }

  results_ = std::move(results);

  int restored = -1;
  if (!prev_key.empty()) {
    for (int i = 0; i < static_cast<int>(results_.size()); ++i) {
      if (results_[i].key == prev_key) {
        restored = i;
        break;
      }
    }
  }
  selected_index_ = (restored >= 0) ? restored
                                     : (results_.empty() ? -1 : 0);
  callback.Run(results_);
  RequestBookmarkFavicons();
}

void MahoCommandModel::RequestBookmarkFavicons() {
  favicon_task_tracker_.TryCancelAll();
  if (!browser_ || !browser_->profile()) {
    return;
  }

  favicon::LargeIconService* large_icon_service =
      LargeIconServiceFactory::GetForBrowserContext(browser_->profile());
  if (!large_icon_service) {
    return;
  }

  std::set<std::string> requested_urls;
  for (const auto& result : results_) {
    if (result.type != CommandSuggestionType::kBookmark ||
        result.icon.has_value() || result.execution_payload.empty() ||
        !requested_urls.insert(result.execution_payload).second) {
      continue;
    }

    large_icon_service->GetLargeIconFromCacheFallbackToGoogleServer(
        GURL(result.execution_payload),
        favicon::LargeIconService::StandardIconSize::k16x16,
        favicon::LargeIconService::StandardIconSize::k16x16,
        favicon::LargeIconService::NoBigEnoughIconBehavior::kReturnEmpty,
        /*should_trim_page_url_path=*/false,
        kBookmarkFaviconTrafficAnnotation,
        base::BindOnce(&MahoCommandModel::OnBookmarkLargeIconLoaded,
                       weak_factory_.GetWeakPtr(), search_generation_,
                       result.execution_payload),
        &favicon_task_tracker_);
  }
}

void MahoCommandModel::OnBookmarkLargeIconLoaded(
    uint32_t search_generation,
    std::string bookmark_url,
    const favicon_base::LargeIconResult& result) {
  if (search_generation != search_generation_) {
    return;
  }

  if (!result.bitmap.is_valid()) {
    return;
  }

  gfx::Image favicon_image = gfx::Image::CreateFrom1xPNGBytes(
      base::span(*result.bitmap.bitmap_data));
  std::optional<ImageData> icon = BuildImageDataFromImage(favicon_image);
  if (!icon.has_value()) {
    return;
  }

  bool updated = false;
  for (auto& suggestion : results_) {
    if (suggestion.type == CommandSuggestionType::kBookmark &&
        suggestion.execution_payload == bookmark_url &&
        !suggestion.icon.has_value()) {
      suggestion.icon = *icon;
      updated = true;
    }
  }

  if (updated && pending_callback_) {
    pending_callback_.Run(results_);
  }
}

// static
void MahoCommandModel::CapResults(std::vector<CommandSuggestion>& results) {
  std::vector<CommandSuggestion> tabs, folders, history, navigation, bookmarks, other;
  for (auto& r : results) {
    switch (r.type) {
      case CommandSuggestionType::kTab:
      case CommandSuggestionType::kClosedTab:
      case CommandSuggestionType::kArchivedTab:
        tabs.push_back(std::move(r));
        break;
      case CommandSuggestionType::kFolder:
        folders.push_back(std::move(r));
        break;
      case CommandSuggestionType::kHistory:
        history.push_back(std::move(r));
        break;
      case CommandSuggestionType::kNavigation:
        navigation.push_back(std::move(r));
        break;
      case CommandSuggestionType::kBookmark:
        bookmarks.push_back(std::move(r));
        break;
      default:
        other.push_back(std::move(r));
        break;
    }
  }

  results.clear();

  constexpr size_t kMaxVisibleResults = 5;

  for (size_t i = 0; i < std::min(tabs.size(), size_t{2}) &&
                      results.size() < kMaxVisibleResults;
       ++i)
    results.push_back(std::move(tabs[i]));
  for (size_t i = 0; i < std::min(folders.size(), size_t{2}) &&
                      results.size() < kMaxVisibleResults;
       ++i)
    results.push_back(std::move(folders[i]));
  for (size_t i = 0; i < std::min(history.size(), size_t{2}) &&
                      results.size() < kMaxVisibleResults;
       ++i)
    results.push_back(std::move(history[i]));
  for (size_t i = 0; i < std::min(navigation.size(), size_t{1}) &&
                      results.size() < kMaxVisibleResults;
       ++i)
    results.push_back(std::move(navigation[i]));

  for (size_t i = 0; i < bookmarks.size() &&
                      results.size() < kMaxVisibleResults;
       ++i)
    results.push_back(std::move(bookmarks[i]));

  for (auto& o : other) {
    if (results.size() >= kMaxVisibleResults)
      break;
    results.push_back(std::move(o));
  }
}

void MahoCommandModel::HydrateSiteSearchEngines() {
  MahoCore* core = maho::GetCore();
  if (!core)
    return;

  auto hydrate_from_json = [this](const std::string& json_str) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_list())
      return false;

    size_t initial_count = site_search_engines_.size();
    for (const auto& item : parsed->GetList()) {
      const auto* dict = item.GetIfDict();
      if (!dict)
        continue;

      const std::string* keyword = dict->FindString("keyword");
      if (!keyword)
        keyword = dict->FindString("shortcut");
      const std::string* name = dict->FindString("name");
      const std::string* url_template = dict->FindString("urlTemplate");
      if (!keyword || !name || !url_template)
        continue;

      SiteSearchEngine engine;
      engine.prefix = *keyword;
      if (!engine.prefix.empty() && engine.prefix[0] == '@')
        engine.prefix.erase(0, 1);
      engine.name = *name;
      engine.url_template = *url_template;
      site_search_engines_.push_back(std::move(engine));
    }

    return site_search_engines_.size() > initial_count;
  };

  char* json = maho_core_get_site_search_entries(core);
  if (json) {
    std::string json_str(json);
    maho_string_free(json);
    if (hydrate_from_json(json_str))
      return;
  }

  json = maho_core_get_search_engines(core);
  if (!json)
    return;

  std::string json_str(json);
  maho_string_free(json);
  hydrate_from_json(json_str);
}

const MahoCommandModel::SiteSearchEngine* MahoCommandModel::MatchSiteSearch(
    const std::string& query) const {
  if (query.empty() || query[0] != '@')
    return nullptr;

  size_t space_pos = query.find(' ');
  std::string prefix = query.substr(1, space_pos == std::string::npos ? std::string::npos : space_pos - 1);

  for (const auto& engine : site_search_engines_) {
    if (engine.prefix == prefix)
      return &engine;
  }
  return nullptr;
}

void MahoCommandModel::SaveRecentSearch(const std::string& query) {
  MahoCore* core = maho::GetCore();
  if (!core || query.empty())
    return;
  maho_core_save_search(core, query.c_str());
}

std::vector<CommandSuggestion> MahoCommandModel::GetRecentSearches() {
  std::vector<CommandSuggestion> results;
  MahoCore* core = maho::GetCore();
  if (!core)
    return results;

  char* json = maho_core_get_recent_searches(core);
  if (!json)
    return results;

  std::string json_str(json);
  maho_string_free(json);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list())
    return results;

  for (const auto& item : parsed->GetList()) {
    if (!item.is_string())
      continue;

    CommandSuggestion s;
    s.type = CommandSuggestionType::kRecentSearch;
    s.title = item.GetString();
    s.key = "recent:" + item.GetString();
    results.push_back(std::move(s));
  }
  return results;
}

}  // namespace maho
