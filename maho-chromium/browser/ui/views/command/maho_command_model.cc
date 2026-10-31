// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_mail_command_catalog.h"

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
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "build/build_config.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "chrome/browser/favicon/large_icon_service_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver.h"
#include "components/favicon/core/large_icon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_scheme_classifier.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/pref_names.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/mail_helper/maho_mail_service.h"  // nogncheck
#include "maho/browser/mail_helper/maho_mail_service_factory.h"  // nogncheck
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_ffi.h"


namespace maho {

namespace {
constexpr base::TimeDelta kDebounceDelay = base::Milliseconds(150);
constexpr base::TimeDelta kRemoteArrivalThreshold = base::Milliseconds(600);

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
  // Regular-path enumeration only. OTR windows are never cross-enumerated here;
  // exact-primary-Incognito builds its tab list from the single current window
  // in FireSearchPrivate, so an OTR profile matches no browser in this scan.
  return browser && browser->GetWindow() && browser->GetTabStripModel() &&
         browser->GetProfile() && profile && !profile->IsOffTheRecord() &&
         browser->GetProfile() == profile &&
         !browser->GetProfile()->IsOffTheRecord();
}

std::string BuildStableTabId(const BrowserWindowInterface* browser,
                             content::WebContents* contents) {
  if (!browser || !contents) {
    return std::string();
  }

  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (helper) {
    return helper->stable_tab_id();
  }

  auto* tab_helper = sessions::SessionTabHelper::FromWebContents(contents);
  if (!tab_helper) {
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

bool IsProtocolLike(const std::string& query) {
  if (query.find("://") != std::string::npos) {
    return true;
  }
  for (const std::string& prefix : {"http://", "https://", "chrome://", "about:", "file:", "data:", "mailto:"}) {
    if (base::StartsWith(query, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

bool IsUrlLikeQuery(const std::string& query);
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

MahoCommandModel::MahoCommandModel(Browser* browser)
    : browser_(browser),
      remote_source_factory_(base::BindRepeating(
          &CreateMahoRemoteSearchSuggestionSource)) {
  context_class_ = MahoClassifyProfile(
      browser_ ? browser_->GetProfile() : nullptr);
  browser_collection_observation_.Observe(
      GlobalBrowserCollection::GetInstance());

  if (context_class_ == MahoPrivateContextClass::kPrimaryIncognito) {
    // Current-window only: observe just this browser's strip and never read
    // saved sources (no HydrateSiteSearchEngines).
    if (browser_ && browser_->GetTabStripModel()) {
      browser_->GetTabStripModel()->AddObserver(this);
    }
    return;
  }

  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* b) {
        if (b && b->GetTabStripModel()) {
          b->GetTabStripModel()->AddObserver(this);
        }
        return true;
      });
  HydrateSiteSearchEngines();

  // The startup palette can run its first search while the maho-core FFI is
  // still initializing asynchronously (storage directory creation, vault key
  // derivation, LoadState — measured ~2s on a real profile). That search
  // observes GetCore() == null and publishes an empty snapshot, which left
  // the auto-opened startup palette blank until the user typed. Re-run the
  // current query when the core becomes ready.
  //
  // Unretained (not weak_factory_): Search() invalidates all weak pointers on
  // every call, which would silently kill this subscription after the first
  // query. The RAII CallbackListSubscription member unsubscribes during
  // destruction, so Unretained is safe here (same pattern as MahoTabRegistry).
  core_ready_subscription_ = maho::AddCoreReadyCallback(base::BindRepeating(
      &MahoCommandModel::OnCoreReady, base::Unretained(this)));
}

MahoCommandModel::~MahoCommandModel() {
  Cancel();
  if (context_class_ == MahoPrivateContextClass::kPrimaryIncognito) {
    if (browser_ && browser_->GetTabStripModel()) {
      browser_->GetTabStripModel()->RemoveObserver(this);
    }
    return;
  }
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [this](BrowserWindowInterface* b) {
        if (b && b->GetTabStripModel()) {
          b->GetTabStripModel()->RemoveObserver(this);
        }
        return true;
      });
}

void MahoCommandModel::Search(const std::string& query,
                              CommandOverlayMode mode,
                              ResultsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  debounce_timer_.Stop();
  if (traced_generation_ != 0) {
    TRACE_EVENT_NESTABLE_ASYNC_END1("ui", "MahoCommandPalette.Search",
                                    traced_generation_, "cancelled", true);
    traced_generation_ = 0;
  }
  ++search_generation_;
  const uint32_t generation = search_generation_;
  pending_callback_.Reset();
  results_.clear();
  local_snapshot_.clear();
  remote_snapshot_.clear();
  has_published_results_ = false;
  first_results_publication_ticks_.reset();
  has_published_remote_results_ = false;
  favicon_task_tracker_.TryCancelAll();
  if (remote_search_source_) {
    remote_search_source_->Cancel();
  }
  weak_factory_.InvalidateWeakPtrs();

  query_ = query;
  mode_ = mode;
  selected_index_ = 0;
  actions_only_ = false;
  pending_callback_ = std::move(callback);

  if (query.empty()) {
    if (mode == CommandOverlayMode::kNewTab ||
        mode == CommandOverlayMode::kCommandsOnly ||
        mode == CommandOverlayMode::kCurrentTab) {
      debounce_timer_.Start(
          FROM_HERE, kDebounceDelay,
          base::BindOnce(&MahoCommandModel::FireSearch,
                         weak_factory_.GetWeakPtr(), generation));
      return;
    }
    if (pending_callback_) {
      pending_callback_.Run({});
    }
    return;
  }

  debounce_timer_.Start(
      FROM_HERE, kDebounceDelay,
      base::BindOnce(&MahoCommandModel::FireSearch,
                     weak_factory_.GetWeakPtr(), generation));
}

void MahoCommandModel::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  debounce_timer_.Stop();
  if (traced_generation_ != 0) {
    TRACE_EVENT_NESTABLE_ASYNC_END1("ui", "MahoCommandPalette.Search",
                                    traced_generation_, "cancelled", true);
    traced_generation_ = 0;
  }
  ++search_generation_;
  pending_callback_.Reset();
  results_.clear();
  local_snapshot_.clear();
  remote_snapshot_.clear();
  has_published_results_ = false;
  first_results_publication_ticks_.reset();
  has_published_remote_results_ = false;
  actions_only_ = false;
  favicon_task_tracker_.TryCancelAll();
  if (remote_search_source_) {
    remote_search_source_->Cancel();
  }
  weak_factory_.InvalidateWeakPtrs();
}

void MahoCommandModel::RefetchCurrentQuery() {
  if (!pending_callback_) {
    return;
  }
  const std::string query = query_;
  const CommandOverlayMode mode = mode_;
  ResultsCallback callback = pending_callback_;
  Search(query, mode, std::move(callback));
}

void MahoCommandModel::OnCoreReady() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // No-op unless a search is live (pending_callback_ set): RefetchCurrentQuery
  // re-issues the current query/mode so FFI-backed sources (recent suspended
  // tabs, saved bookmarks/history, recent searches) replace the empty
  // snapshot published before the core existed.
  RefetchCurrentQuery();
}

void MahoCommandModel::SetRemoteSearchSourceForTesting(
    std::unique_ptr<MahoRemoteSearchSuggestionSource> source) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (remote_search_source_) {
    remote_search_source_->Cancel();
  }
  remote_search_source_ = std::move(source);
}

void MahoCommandModel::SetRemoteSearchSourceFactoryForTesting(
    RemoteSourceFactory factory) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (remote_search_source_) {
    remote_search_source_->Cancel();
    remote_search_source_.reset();
  }
  remote_source_factory_ = std::move(factory);
}

void MahoCommandModel::StartRemoteSearchForTesting(
    uint32_t generation,
    const std::string& query) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != search_generation_ || !remote_search_source_) {
    return;
  }
  remote_search_source_->Start(
      query, base::BindRepeating(&MahoCommandModel::OnRemoteSnapshot,
                                 weak_factory_.GetWeakPtr(), generation));
}

void MahoCommandModel::AcceptLocalResultsForTesting(
    uint32_t generation,
    std::vector<CommandSuggestion> results) {
  OnResults(generation, std::move(results));
}

void MahoCommandModel::ApplyFaviconForTesting(uint32_t generation,
                                              const std::string& key) {
  if (generation != search_generation_) {
    return;
  }
  for (auto& result : results_) {
    if (result.key != key || result.icon.has_value()) {
      continue;
    }
    ImageData icon;
    icon.format = "test";
    result.icon = std::move(icon);
    if (pending_callback_) {
      pending_callback_.Run(results_);
    }
    return;
  }
}

void MahoCommandModel::FireSearch(uint32_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != search_generation_) {
    return;
  }

  search_start_ticks_ = base::TimeTicks::Now();
  traced_generation_ = generation;
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN1("ui", "MahoCommandPalette.Search",
                                    generation, "gen", generation);
  std::string search_query = query_;
  actions_only_ = false;

  if (!search_query.empty() && search_query[0] == '>') {
    search_query = search_query.substr(1);
    if (!search_query.empty() && search_query[0] == ' ')
      search_query = search_query.substr(1);
    actions_only_ = true;
  }

  if (mode_ == CommandOverlayMode::kCommandsOnly) {
    actions_only_ = true;
  }

  Profile* profile = browser_ ? browser_->GetProfile() : nullptr;
  const std::string trimmed_query(
      base::TrimWhitespaceASCII(query_, base::TRIM_ALL));
  const bool remote_eligible = IsRemoteSearchEligible(
      profile, context_class_, mode_, trimmed_query,
      MatchSiteSearch(trimmed_query) != nullptr);
  if (remote_eligible) {
    if (!remote_search_source_) {
      remote_search_source_ = remote_source_factory_.Run(profile);
    }
    if (remote_search_source_) {
      base::WeakPtr<MahoCommandModel> alive = weak_factory_.GetWeakPtr();
      remote_search_source_->Start(
          trimmed_query,
          base::BindRepeating(&MahoCommandModel::OnRemoteSnapshot,
                              weak_factory_.GetWeakPtr(), generation));
      if (!alive || generation != search_generation_) {
        return;
      }
    }
  }

  if (context_class_ == MahoPrivateContextClass::kPrimaryIncognito) {
    FireSearchPrivate(generation);
    return;
  }
  if (!profile) {
    OnResults(generation, {});
    return;
  }

  std::string tabs_json = actions_only_ ? std::string() : BuildTabsJson();
  const bool is_incognito = profile->IsOffTheRecord();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoCommandModel::SearchOnPool, search_query, tabs_json,
                     mode_, is_incognito),
      base::BindOnce(&MahoCommandModel::OnResults,
                     weak_factory_.GetWeakPtr(), generation));
}

// static
bool MahoCommandModel::IsRemoteSearchEligible(
    Profile* profile,
    MahoPrivateContextClass context_class,
    CommandOverlayMode mode,
    const std::string& trimmed_query,
    bool matches_site_search) {
  return profile && context_class == MahoPrivateContextClass::kRegular &&
         !profile->IsOffTheRecord() && profile->GetPrefs() &&
         profile->GetPrefs()->GetBoolean(prefs::kSearchSuggestEnabled) &&
         !trimmed_query.empty() && mode != CommandOverlayMode::kCommandsOnly &&
         !base::StartsWith(trimmed_query, ">",
                           base::CompareCase::SENSITIVE) &&
         mode != CommandOverlayMode::kSiteSearch && !matches_site_search &&
         !IsUrlLikeQuery(trimmed_query);
}

// static
base::Value MahoCommandModel::BuildTabEntryDict(
    const std::string& stable_id,
    const std::string& browser_session_id,
    const std::string& tab_session_id,
    const std::string& title,
    const std::string& url) {
  base::Value dict(base::Value::Type::DICT);
  dict.GetDict().Set("id", stable_id);
  dict.GetDict().Set("browser_session_id", browser_session_id);
  dict.GetDict().Set("tab_session_id", tab_session_id);
  dict.GetDict().Set("title", title);
  dict.GetDict().Set("url", url);
  return dict;
}

namespace {

struct PrivateAction {
  const char* id;
  const char* label;
  const char* category;
};

// Frozen 13-action allowlist, identical in id/order to the Rust
// CommandBarEngine::search_incognito ephemeral_actions (command_bar.rs).
constexpr PrivateAction kPrivateActions[] = {
    {"close_tab", "Close Tab", "Tabs"},
    {"reload_tab", "Reload Tab", "Tabs"},
    {"hard_reload", "Hard Reload", "Tabs"},
    {"copy_url", "Copy URL", "Navigation"},
    {"toggle_sidebar", "Toggle Sidebar", "Navigation"},
    {"toggle_split_view", "Toggle Split View", "Split View"},
    {"zoom_in", "Zoom In", "View"},
    {"zoom_out", "Zoom Out", "View"},
    {"reset_zoom", "Reset Zoom", "View"},
    {"find_in_page", "Find in Page", "View"},
    {"view_source", "View Source", "View"},
    {"toggle_dev_tools", "Toggle Developer Tools", "View"},
    {"print_page", "Print Page", "View"},
};

CommandSuggestion MakePrivateActionSuggestion(const PrivateAction& action) {
  CommandSuggestion suggestion;
  suggestion.type = CommandSuggestionType::kAction;
  suggestion.key = std::string("action:") + action.id;
  suggestion.action_id = action.id;
  suggestion.title = action.label;
  suggestion.subtitle = action.category;
  return suggestion;
}

bool PrivateActionMatchesQuery(const PrivateAction& action,
                               const std::string& lower_query) {
  if (lower_query.empty()) {
    return true;
  }
  const std::string target =
      base::ToLowerASCII(std::string(action.label) + " " + action.category);
  return target.find(lower_query) != std::string::npos;
}

// Scheme policy for typed palette input. http/https/file are parsed by
// AutocompleteInput itself; this only decides the remaining explicit schemes.
// javascript: and data: are never navigated from typed palette text.
class PaletteSchemeClassifier final : public AutocompleteSchemeClassifier {
 public:
  metrics::OmniboxInputType GetInputTypeForScheme(
      const std::string& scheme) const override {
    if (base::EqualsCaseInsensitiveASCII(scheme, url::kJavaScriptScheme) ||
        base::EqualsCaseInsensitiveASCII(scheme, url::kDataScheme)) {
      return metrics::OmniboxInputType::QUERY;
    }
    for (const std::string_view known :
         {"chrome", "about", "view-source", "file", "maho"}) {
      if (base::EqualsCaseInsensitiveASCII(scheme, known)) {
        return metrics::OmniboxInputType::URL;
      }
    }
    return metrics::OmniboxInputType::EMPTY;
  }
};

// Dev-server shorthands the omnibox heuristics do not all accept as URLs
// ("localhost", "localhost:3000", "devbox:3000", "123:3000").
bool IsLocalhostOrHostPort(const std::string& query) {
  const std::string lower_query = base::ToLowerASCII(query);
  if (lower_query == "localhost" ||
      base::StartsWith(lower_query, "localhost:",
                       base::CompareCase::SENSITIVE) ||
      base::StartsWith(lower_query, "localhost/",
                       base::CompareCase::SENSITIVE)) {
    return true;
  }
  const size_t colon = query.find(':');
  if (colon == std::string::npos || colon == 0 ||
      colon != query.rfind(':') || colon + 1 == query.size()) {
    return false;
  }
  const auto is_hostname_character = [](char c) {
    return base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '-';
  };
  const auto is_ascii_digit = [](char c) { return base::IsAsciiDigit(c); };
  return base::IsAsciiAlphaNumeric(query.front()) &&
         base::IsAsciiAlphaNumeric(query[colon - 1]) &&
         std::ranges::all_of(query.substr(0, colon), is_hostname_character) &&
         std::ranges::all_of(query.substr(colon + 1), is_ascii_digit);
}

bool IsUrlLikeQuery(const std::string& query) {
  if (query.empty()) {
    return false;
  }
  if (IsProtocolLike(query)) {
    return true;
  }
  if (query.find_first_of(" \t\r\n") != std::string::npos) {
    return false;
  }
  return MahoCommandModel::ClassifyTypedNavigation(query).has_value();
}

// Mirrors IsSidebarEligibleUrl (maho_sidebar_tab_list_view.cc): the NTP,
// downloads, history and about: pages are not user-facing "Switch to Tab"
// targets, so they are excluded from the command palette to match the sidebar.
bool IsCommandBarEligibleTabUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return false;
  }
  if (url.SchemeIs("about")) {
    return false;
  }
  if (url.SchemeIs("chrome")) {
    if (url.host() == "newtab" || url.host() == "downloads" ||
        url.host() == "history" || url.host() == "new-tab-page") {
      return false;
    }
  }
  if (url.SchemeIs("chrome-search")) {
    return false;
  }
  return true;
}

bool IsApprovedMahoAliasUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs("maho") || url.has_username() ||
      url.has_password() || url.has_port()) {
    return false;
  }
  for (const auto& record : kMahoUrlAliases) {
    if (url.host() == record.alias_host) {
      return true;
    }
  }
  return false;
}

bool IsNavigableCommandUrl(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("chrome") ||
         IsApprovedMahoAliasUrl(url);
}

// Typed text may additionally open local files and about:/view-source: pages,
// matching what the omnibox accepts.
bool IsTypedNavigableUrl(const GURL& url) {
  return IsNavigableCommandUrl(url) ||
         (url.is_valid() && (url.SchemeIsFile() || url.SchemeIs("about") ||
                             url.SchemeIs("view-source")));
}

}  // namespace

std::string MahoCommandModel::BuildTabsJson() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!tabs_cache_dirty_) {
    return cached_tabs_json_;
  }

  base::Value tabs(base::Value::Type::LIST);
  if (!browser_ || !browser_->GetWindow() || !browser_->GetTabStripModel()) {
    cached_tabs_json_ = std::string();
    tabs_cache_dirty_ = false;
    return cached_tabs_json_;
  }

  const Profile* profile = browser_->GetProfile();
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

          if (!IsCommandBarEligibleTabUrl(wc->GetVisibleURL()))
            continue;

          std::string title = base::UTF16ToUTF8(wc->GetTitle());
          std::string url = wc->GetVisibleURL().spec();
          if (title.empty() && url.empty())
            continue;
          if (title.empty()) {
            title = url;
          }

          std::string stable_id = BuildStableTabId(candidate, wc);
          if (stable_id.empty())
            continue;

          tabs.GetList().Append(BuildTabEntryDict(
              stable_id,
              base::NumberToString(candidate->GetSessionID().id()),
              base::NumberToString(tab_helper->session_id().id()),
              title,
              url));
        }
        return true;
      });

  base::JSONWriter::Write(tabs, &cached_tabs_json_);
  tabs_cache_dirty_ = false;
  return cached_tabs_json_;
}

// static
bool MahoCommandModel::ShouldReadSavedSourcesForContext(
    MahoPrivateContextClass klass) {
  return klass == MahoPrivateContextClass::kRegular;
}

// static
std::vector<std::string> MahoCommandModel::PrivateActionAllowlist() {
  std::vector<std::string> ids;
  ids.reserve(std::size(kPrivateActions));
  for (const auto& action : kPrivateActions) {
    ids.emplace_back(action.id);
  }
  return ids;
}

// static
bool MahoCommandModel::PrivateSearchUsesDefaultProviderOnly(
    MahoPrivateContextClass klass) {
  return klass == MahoPrivateContextClass::kPrimaryIncognito;
}

// static
CommandSuggestion MahoCommandModel::BuildDefaultProviderSearchSuggestion(
    const std::string& query,
    const std::string& search_url,
    const std::string& engine_name) {
  CommandSuggestion suggestion;
  suggestion.type = CommandSuggestionType::kSearch;
  suggestion.key = "search:" + query;
  suggestion.title = query + " \u2014 Search with " +
                     (engine_name.empty() ? std::string("Search") : engine_name);
  suggestion.execution_payload = search_url;
  return suggestion;
}

// static
std::optional<MahoCommandModel::TypedNavigation>
MahoCommandModel::ClassifyTypedNavigation(const std::string& text) {
  const std::string trimmed(base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (trimmed.empty()) {
    return std::nullopt;
  }

  const PaletteSchemeClassifier classifier;
  const AutocompleteInput input(base::UTF8ToUTF16(trimmed),
                                metrics::OmniboxEventProto::OTHER, classifier);
  GURL url;
  if (input.type() == metrics::OmniboxInputType::URL) {
    url = input.canonicalized_url();
  } else if (trimmed.find_first_of(" \t\r\n") == std::string::npos &&
             IsLocalhostOrHostPort(trimmed)) {
    url = GURL("http://" + trimmed);
  }
  if (!IsTypedNavigableUrl(url)) {
    return std::nullopt;
  }

  TypedNavigation navigation;
  navigation.url = std::move(url);
  navigation.typed_http_scheme = input.typed_url_had_http_scheme();
  return navigation;
}

std::optional<CommandSuggestion> MahoCommandModel::BuildNavigationSuggestionForQuery(
    const std::string& query) {
  if (!IsUrlLikeQuery(query)) {
    return std::nullopt;
  }

  std::optional<TypedNavigation> navigation = ClassifyTypedNavigation(query);
  if (!navigation || !IsNavigableCommandUrl(navigation->url)) {
    return std::nullopt;
  }
  const GURL& url = navigation->url;

  CommandSuggestion suggestion;
  suggestion.type = CommandSuggestionType::kNavigation;
  suggestion.key = "nav:" + url.spec();
  // Scheme-less input keeps the user's spelling; the navigation itself relies
  // on HTTPS-Upgrades (with http fallback) instead of a forced https://.
  suggestion.title =
      "Go to " + (IsProtocolLike(query)
                      ? url.spec()
                      : std::string(base::TrimWhitespaceASCII(
                            query, base::TRIM_ALL)));
  suggestion.subtitle = "Navigate";
  suggestion.execution_payload = url.spec();
  return suggestion;
}

std::optional<std::pair<std::string, std::string>>
MahoCommandModel::ResolveDefaultProviderSearch(const std::string& query) const {
  if (!browser_ || !browser_->GetProfile() || query.empty()) {
    return std::nullopt;
  }
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(browser_->GetProfile());
  if (!service) {
    return std::nullopt;
  }
  const TemplateURL* default_provider = service->GetDefaultSearchProvider();
  if (!default_provider) {
    return std::nullopt;
  }
  const SearchTermsData& term_data = service->search_terms_data();
  if (!default_provider->url_ref().SupportsReplacement(term_data)) {
    return std::nullopt;
  }
  TemplateURLRef::SearchTermsArgs args(base::UTF8ToUTF16(query));
  std::string url =
      default_provider->url_ref().ReplaceSearchTerms(args, term_data);
  if (url.empty()) {
    return std::nullopt;
  }
  return std::make_pair(std::move(url),
                        base::UTF16ToUTF8(default_provider->short_name()));
}

std::vector<CommandSuggestion> MahoCommandModel::BuildPrivateResults(
    const std::string& query) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<CommandSuggestion> results;
  const std::string lower_query = base::ToLowerASCII(query);

  if (actions_only_) {
    for (const auto& action : kPrivateActions) {
      if (PrivateActionMatchesQuery(action, lower_query)) {
        results.push_back(MakePrivateActionSuggestion(action));
      }
    }
    return results;
  }

  if (!query.empty()) {
    if (std::optional<CommandSuggestion> nav =
            BuildNavigationSuggestionForQuery(query)) {
      results.push_back(std::move(*nav));
    }

    if (std::optional<std::pair<std::string, std::string>> search =
            ResolveDefaultProviderSearch(query)) {
      results.push_back(BuildDefaultProviderSearchSuggestion(
          query, search->first, search->second));
    }
  }

  if (browser_ && browser_->GetTabStripModel()) {
    TabStripModel* model = browser_->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      tabs::TabInterface* tab = model->GetTabAtIndex(i);
      if (!wc) {
        continue;
      }
      if (!IsCommandBarEligibleTabUrl(wc->GetVisibleURL())) {
        continue;
      }
      const std::string title = base::UTF16ToUTF8(wc->GetTitle());
      const std::string url = wc->GetVisibleURL().spec();
      if (!lower_query.empty()) {
        const std::string target = base::ToLowerASCII(title + " " + url);
        if (target.find(lower_query) == std::string::npos) {
          continue;
        }
      }
      std::string stable_id = BuildStableTabId(browser_, wc);
      if (stable_id.empty()) {
        continue;
      }
      auto* tab_helper = sessions::SessionTabHelper::FromWebContents(wc);
      CommandSuggestion suggestion;
      suggestion.type = CommandSuggestionType::kTab;
      suggestion.key = "tab:" + stable_id;
      suggestion.title = title;
      suggestion.subtitle = url;
      suggestion.execution_payload = url;
      suggestion.stable_tab_id = stable_id;
      suggestion.browser_session_id =
          base::NumberToString(browser_->GetSessionID().id());
      if (tab_helper) {
        suggestion.tab_session_id =
            base::NumberToString(tab_helper->session_id().id());
      }
      if (tab) {
        suggestion.icon = BuildImageDataFromTab(tab);
      }
      results.push_back(std::move(suggestion));
    }
  }

  if (!lower_query.empty()) {
    for (const auto& action : kPrivateActions) {
      if (PrivateActionMatchesQuery(action, lower_query)) {
        results.push_back(MakePrivateActionSuggestion(action));
      }
    }
  }

  return results;
}

void MahoCommandModel::FireSearchPrivate(uint32_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != search_generation_) {
    return;
  }
  std::string prepared = query_;
  if (!prepared.empty() && prepared[0] == '>') {
    prepared = prepared.substr(1);
    if (!prepared.empty() && prepared[0] == ' ') {
      prepared = prepared.substr(1);
    }
  }
  OnResults(generation, BuildPrivateResults(prepared));
}

// static
std::vector<CommandSuggestion> MahoCommandModel::SearchOnPool(
    const std::string& query,
    const std::string& tabs_json,
    CommandOverlayMode mode,
    bool is_incognito) {
  MahoCore* core = maho::GetCore();
  if (!core)
    return std::vector<CommandSuggestion>();

  char* json = maho_core_command_bar_search(core, query.c_str(),
                                            GetBackendSearchMode(mode),
                                            tabs_json.c_str(), is_incognito);
  if (!json)
    return std::vector<CommandSuggestion>();

  std::string json_str(json);
  maho_string_free(json);

  return ParseSearchResultsJson(json_str);
}

// static
std::vector<CommandSuggestion> MahoCommandModel::ParseSearchResultsJson(
    const std::string& json_str) {
  std::vector<CommandSuggestion> results;

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list())
    return results;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict)
      continue;

    const std::string* kind = dict->FindString("kind");
    if (kind && *kind == "aiAnswer")
      continue;

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

    if (suggestion.type == CommandSuggestionType::kTab ||
        suggestion.type == CommandSuggestionType::kArchivedTab ||
        suggestion.type == CommandSuggestionType::kClosedTab) {
      if (suggestion.title.empty() && suggestion.subtitle.empty() &&
          suggestion.execution_payload.empty()) {
        continue;
      }
      if (suggestion.title.empty()) {
        suggestion.title = !suggestion.subtitle.empty()
                               ? suggestion.subtitle
                               : suggestion.execution_payload;
      }
    }

    const base::Value* icon_value = dict->Find("icon");
    if (icon_value)
      suggestion.icon = ParseImageData(*icon_value);

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

    const std::string* stable_id_field = dict->FindString("id");
    if (stable_id_field && !stable_id_field->empty() &&
        suggestion.type == CommandSuggestionType::kTab) {
      suggestion.stable_tab_id = *stable_id_field;
    }

    const std::string* tab_core_id = dict->FindString("tabCoreId");
    if (tab_core_id)
      suggestion.tab_core_id = *tab_core_id;
    suggestion.is_suspended = dict->FindBool("isSuspended").value_or(false);

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
  if (kind == "search")
    return CommandSuggestionType::kSearch;
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

void MahoCommandModel::OnResults(
    uint32_t generation,
    std::vector<CommandSuggestion> results) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != search_generation_) {
    return;
  }

  if (traced_generation_ == generation) {
    const base::TimeDelta elapsed =
        base::TimeTicks::Now() - search_start_ticks_;
    TRACE_EVENT_INSTANT("ui", "MahoCommandPalette.SearchComplete",
                        "elapsed_ms", elapsed.InMilliseconds(),
                        "result_count", results.size(), "gen", generation);
    UMA_HISTOGRAM_TIMES("Maho.CommandPalette.SearchLatency", elapsed);
  }

  MahoMailService* mail_service =
      browser_ && browser_->GetProfile()
          ? MahoMailServiceFactory::GetForProfileIfExists(browser_->GetProfile())
          : nullptr;
  constexpr bool kMailPlatformSupported =
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
      true;
#else
      false;
#endif
  ApplyMailAvailabilityGate(results,
                            kMailPlatformSupported && mail_service &&
                                mail_service->IsCommandAvailable());
  local_snapshot_ = PrepareLocalCandidates(
      std::move(results), actions_only_, mode_, query_);
  base::WeakPtr<MahoCommandModel> alive = weak_factory_.GetWeakPtr();
  PublishMergedResultsIfChanged(generation);
  if (!alive) {
    return;
  }

  if (traced_generation_ == generation) {
    TRACE_EVENT_NESTABLE_ASYNC_END1("ui", "MahoCommandPalette.Search",
                                    generation, "final_count",
                                    results_.size());
    traced_generation_ = 0;
  }
}

void MahoCommandModel::OnRemoteSnapshot(
    uint32_t generation,
    std::vector<RemoteSearchSuggestion> remote_snapshot) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != search_generation_) {
    return;
  }
  if (first_results_publication_ticks_.has_value() &&
      !has_published_remote_results_ &&
      base::TimeTicks::Now() - *first_results_publication_ticks_ >
          kRemoteArrivalThreshold) {
    return;
  }
  remote_snapshot_ = std::move(remote_snapshot);
  PublishMergedResultsIfChanged(generation);
}

bool MahoCommandModel::PublishMergedResultsIfChanged(uint32_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != search_generation_) {
    return false;
  }

  std::vector<CommandSuggestion> remote_results =
      NormalizeRemoteSuggestions(query_, local_snapshot_, remote_snapshot_);
  std::vector<CommandSuggestion> merged = AllocateFinalResults(
      local_snapshot_, remote_results, mode_, query_);
  if (has_published_results_ && FinalSnapshotsEqual(results_, merged)) {
    return false;
  }

  std::string previous_key;
  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(results_.size())) {
    previous_key = results_[selected_index_].key;
  }

  results_ = std::move(merged);
  has_published_results_ = true;
  if (!first_results_publication_ticks_.has_value()) {
    first_results_publication_ticks_ = base::TimeTicks::Now();
  }
  if (!has_published_remote_results_) {
    has_published_remote_results_ = std::ranges::any_of(
        results_, [](const CommandSuggestion& result) {
          return base::StartsWith(result.key, "remote-search:",
                                  base::CompareCase::SENSITIVE);
        });
  }

  int restored_index = -1;
  if (!previous_key.empty()) {
    for (int i = 0; i < static_cast<int>(results_.size()); ++i) {
      if (results_[i].key == previous_key) {
        restored_index = i;
        break;
      }
    }
  }
  selected_index_ =
      restored_index >= 0 ? restored_index : (results_.empty() ? -1 : 0);

  ResultsCallback callback = pending_callback_;
  base::WeakPtr<MahoCommandModel> alive = weak_factory_.GetWeakPtr();
  if (callback) {
    callback.Run(results_);
  }
  if (!alive || generation != search_generation_) {
    return true;
  }

  // Saved-source-backed favicon loaders (bookmarks, search engines) and the
  // cross-window tab scan must never run in exact-primary-Incognito.
  if (context_class_ != MahoPrivateContextClass::kPrimaryIncognito) {
    ++favicon_request_pass_count_for_testing_;
    RequestBookmarkFavicons(generation);
    if (!alive) {
      return true;
    }
    RequestTabFavicons(generation);
    if (!alive) {
      return true;
    }
    RequestSearchEngineFavicons(generation);
    if (!alive) {
      return true;
    }
  }
  return true;
}

void MahoCommandModel::RequestBookmarkFavicons(uint32_t generation) {
  if (generation != search_generation_) {
    return;
  }
  favicon_task_tracker_.TryCancelAll();
  if (!browser_ || !browser_->GetProfile()) {
    return;
  }

  favicon::LargeIconService* large_icon_service =
      LargeIconServiceFactory::GetForBrowserContext(browser_->GetProfile());
  if (!large_icon_service) {
    return;
  }

  std::set<std::string> requested_urls;
  for (const auto& result : results_) {
    const bool eligible_type =
        result.type == CommandSuggestionType::kBookmark ||
        result.type == CommandSuggestionType::kHistory;
    if (!eligible_type || result.icon.has_value() ||
        result.execution_payload.empty() ||
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
                       weak_factory_.GetWeakPtr(), generation,
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
    const bool eligible_type =
        suggestion.type == CommandSuggestionType::kBookmark ||
        suggestion.type == CommandSuggestionType::kHistory;
    if (eligible_type && suggestion.execution_payload == bookmark_url &&
        !suggestion.icon.has_value()) {
      suggestion.icon = *icon;
      updated = true;
    }
  }

  if (updated && search_generation == search_generation_ &&
      pending_callback_) {
    pending_callback_.Run(results_);
  }
}

void MahoCommandModel::RequestTabFavicons(uint32_t generation) {
  if (generation != search_generation_) {
    return;
  }
  if (!browser_) {
    return;
  }

  const Profile* profile = browser_->GetProfile();
  bool updated = false;

  for (auto& result : results_) {
    if (result.type != CommandSuggestionType::kTab || result.icon.has_value()) {
      continue;
    }

    // Primary: match by stable tab ID via MahoTabIdHelper.
    if (!result.stable_tab_id.empty()) {
      ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
          [&result, &updated, profile](BrowserWindowInterface* candidate) {
            if (result.icon.has_value()) {
              return false;
            }
            if (!IsEligibleBrowserForTabSearch(candidate, profile)) {
              return true;
            }

            TabStripModel* model = candidate->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              auto* tab = model->GetTabAtIndex(i);
              auto* wc = model->GetWebContentsAt(i);
              if (!tab || !wc) {
                continue;
              }
              auto* id_helper = MahoTabIdHelper::FromWebContents(wc);
              if (!id_helper ||
                  id_helper->stable_tab_id() != result.stable_tab_id) {
                continue;
              }
              result.icon = BuildImageDataFromTab(tab);
              if (!result.icon.has_value()) {
                auto* tab_ui_helper = TabUIHelper::From(tab);
                if (tab_ui_helper) {
                  result.icon =
                      BuildImageDataFromFavicon(tab_ui_helper->GetFavicon());
                }
              }
              if (result.icon.has_value()) {
                updated = true;
              }
              return false;
            }
            return true;
          });
      if (result.icon.has_value()) {
        continue;
      }
    }

    // Fallback: match by browser/tab session IDs for backward compatibility.
    if (result.browser_session_id.empty() || result.tab_session_id.empty()) {
      continue;
    }

    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&result, &updated, profile](BrowserWindowInterface* candidate) {
          if (result.icon.has_value()) {
            return false;
          }
          if (!IsEligibleBrowserForTabSearch(candidate, profile)) {
            return true;
          }
          if (base::NumberToString(candidate->GetSessionID().id()) !=
              result.browser_session_id) {
            return true;
          }

          TabStripModel* model = candidate->GetTabStripModel();
          for (int i = 0; i < model->count(); ++i) {
            auto* tab = model->GetTabAtIndex(i);
            auto* wc = model->GetWebContentsAt(i);
            if (!tab || !wc) {
              continue;
            }
            auto* tab_helper = sessions::SessionTabHelper::FromWebContents(wc);
            if (!tab_helper) {
              continue;
            }
            if (base::NumberToString(tab_helper->session_id().id()) !=
                result.tab_session_id) {
              continue;
            }
            result.icon = BuildImageDataFromTab(tab);
            if (!result.icon.has_value()) {
              auto* tab_ui_helper = TabUIHelper::From(tab);
              if (tab_ui_helper) {
                result.icon =
                    BuildImageDataFromFavicon(tab_ui_helper->GetFavicon());
              }
            }
            if (result.icon.has_value()) {
              updated = true;
            }
            return false;
          }
          return true;
        });
  }

  if (updated && generation == search_generation_ && pending_callback_) {
    pending_callback_.Run(results_);
  }
}

// static
std::vector<CommandSuggestion> MahoCommandModel::PrepareLocalCandidates(
    std::vector<CommandSuggestion> results,
    bool actions_only,
    CommandOverlayMode mode,
    const std::string& query) {
  if (actions_only) {
    std::erase_if(results, [](const CommandSuggestion& suggestion) {
      return suggestion.type != CommandSuggestionType::kAction;
    });
  } else {
    // The desktop palette owns the "Go to" row: the core's row uses a looser
    // any-dot URL heuristic and a different spelling of the same URL, which
    // produced duplicate or bogus rows ("node.js").
    std::erase_if(results, [](const CommandSuggestion& suggestion) {
      return suggestion.type == CommandSuggestionType::kNavigation;
    });
    if (std::optional<CommandSuggestion> navigation =
            BuildNavigationSuggestionForQuery(query)) {
      results.insert(results.begin(), std::move(*navigation));
    }
  }

  ApplyQuickActionGate(results, mode, query);
  return results;
}

// static
std::vector<CommandSuggestion> MahoCommandModel::NormalizeRemoteSuggestions(
    const std::string& query,
    const std::vector<CommandSuggestion>& prepared_local_results,
    const std::vector<RemoteSearchSuggestion>& remote_suggestions) {
  auto normalize_terms = [](std::string_view value) {
    return base::ToLowerASCII(base::UTF16ToUTF8(base::TrimWhitespace(
        base::UTF8ToUTF16(value), base::TRIM_ALL)));
  };
  auto canonical_url = [](const std::string& value) {
    const GURL url(value);
    return url.is_valid() ? url.spec() : std::string();
  };

  std::set<std::string> seen_terms;
  std::set<std::string> seen_destinations;
  seen_terms.insert(normalize_terms(query));
  for (const CommandSuggestion& local : prepared_local_results) {
    if (local.type == CommandSuggestionType::kSearch &&
        base::StartsWith(local.key, "search:",
                         base::CompareCase::SENSITIVE)) {
      const std::string terms = normalize_terms(local.key.substr(7));
      if (!terms.empty()) {
        seen_terms.insert(terms);
      }
    }
    const std::string destination = canonical_url(local.execution_payload);
    if (!destination.empty()) {
      seen_destinations.insert(destination);
    }
  }

  std::vector<CommandSuggestion> normalized;
  normalized.reserve(remote_suggestions.size());
  for (const RemoteSearchSuggestion& remote : remote_suggestions) {
    const std::string terms =
        normalize_terms(remote.normalized_search_terms);
    const std::string destination = remote.destination_url.is_valid()
                                        ? remote.destination_url.spec()
                                        : std::string();
    if (terms.empty() || destination.empty() || seen_terms.contains(terms) ||
        seen_destinations.contains(destination)) {
      continue;
    }

    seen_terms.insert(terms);
    seen_destinations.insert(destination);

    CommandSuggestion suggestion;
    suggestion.type = CommandSuggestionType::kSearch;
    suggestion.key = "remote-search:" + terms;
    suggestion.title = base::UTF16ToUTF8(base::TrimWhitespace(
        base::UTF8ToUTF16(remote.display_title), base::TRIM_ALL));
    if (suggestion.title.empty()) {
      suggestion.title = terms;
    }
    if (remote.subtitle.has_value()) {
      suggestion.subtitle = base::UTF16ToUTF8(base::TrimWhitespace(
          base::UTF8ToUTF16(*remote.subtitle), base::TRIM_ALL));
    }
    if (suggestion.subtitle.empty()) {
      suggestion.subtitle = remote.destination_url.host();
      if (base::StartsWith(suggestion.subtitle, "www.",
                           base::CompareCase::SENSITIVE)) {
        suggestion.subtitle.erase(0, 4);
      }
    }
    suggestion.execution_payload = destination;
    normalized.push_back(std::move(suggestion));
  }
  return normalized;
}

// static
std::vector<CommandSuggestion> MahoCommandModel::AllocateFinalResults(
    std::vector<CommandSuggestion> prepared_local_results,
    const std::vector<CommandSuggestion>& remote_results,
    CommandOverlayMode mode,
    const std::string& query) {
  if (remote_results.empty()) {
    CapResults(prepared_local_results, mode, query);
    return prepared_local_results;
  }

  const size_t remote_count = std::min<size_t>(2, remote_results.size());
  const size_t local_slots = kMaxVisibleRows - remote_count;
  if (prepared_local_results.size() > local_slots) {
    prepared_local_results.resize(local_slots);
  }
  auto insertion_point = std::ranges::find_if(
      prepared_local_results, [](const CommandSuggestion& result) {
        return result.type == CommandSuggestionType::kSearch &&
               base::StartsWith(result.key, "search:",
                                base::CompareCase::SENSITIVE);
      });
  if (insertion_point != prepared_local_results.end()) {
    ++insertion_point;
  }
  prepared_local_results.insert(insertion_point, remote_results.begin(),
                                remote_results.begin() + remote_count);
  return prepared_local_results;
}

// static
bool MahoCommandModel::FinalSnapshotsEqual(
    const std::vector<CommandSuggestion>& lhs,
    const std::vector<CommandSuggestion>& rhs) {
  return lhs.size() == rhs.size() &&
         std::ranges::equal(
             lhs, rhs,
             [](const CommandSuggestion& left,
                const CommandSuggestion& right) {
               return left.key == right.key && left.title == right.title &&
                      left.subtitle == right.subtitle &&
                      left.execution_payload == right.execution_payload;
             });
}

// static
void MahoCommandModel::ApplyQuickActionGate(std::vector<CommandSuggestion>& results,
                                             CommandOverlayMode mode,
                                             const std::string& query) {
  if (mode == CommandOverlayMode::kCurrentTab || mode == CommandOverlayMode::kNewTab) {
    if (query.length() <= 2 || IsProtocolLike(query)) {
      std::erase_if(results, [](const CommandSuggestion& s) {
        return s.type == CommandSuggestionType::kAction;
      });
    }
  }
}

// static
void MahoCommandModel::ApplyMailAvailabilityGate(
    std::vector<CommandSuggestion>& results,
    bool mail_available) {
  if (mail_available) {
    return;
  }
  std::erase_if(results, [](const CommandSuggestion& suggestion) {
    const std::string action_id =
        !suggestion.action_id.empty()
            ? suggestion.action_id
            : (base::StartsWith(suggestion.key, "action:",
                                base::CompareCase::SENSITIVE)
                   ? suggestion.key.substr(7)
                   : std::string());
    return suggestion.type == CommandSuggestionType::kAction &&
           FindMahoMailCommand(action_id);
  });
}

// static
void MahoCommandModel::CapResults(std::vector<CommandSuggestion>& results, CommandOverlayMode mode, const std::string& query) {
  const size_t max_visible = kMaxVisibleRows;

  // Empty-query bucket mode: guarantees a tab+history+bookmark mix so recent
  // history isn't starved when all entries share equal base fuzzy score.
  const bool is_empty_query = query.empty();
  const bool bucket_mode = is_empty_query &&
                           (mode == CommandOverlayMode::kNewTab ||
                            mode == CommandOverlayMode::kCurrentTab);
  if (bucket_mode) {
    constexpr size_t kTabBucket = 2;
    constexpr size_t kHistoryBucket = 2;
    constexpr size_t kBookmarkBucket = 1;
    std::vector<CommandSuggestion> tabs, history, bookmarks, other, leftovers;
    for (auto& r : results) {
      switch (r.type) {
        case CommandSuggestionType::kTab:
          if (tabs.size() < kTabBucket) {
            tabs.push_back(std::move(r));
          } else {
            leftovers.push_back(std::move(r));
          }
          break;
        case CommandSuggestionType::kHistory:
          if (history.size() < kHistoryBucket) {
            history.push_back(std::move(r));
          } else {
            leftovers.push_back(std::move(r));
          }
          break;
        case CommandSuggestionType::kBookmark:
          if (bookmarks.size() < kBookmarkBucket) {
            bookmarks.push_back(std::move(r));
          } else {
            leftovers.push_back(std::move(r));
          }
          break;
        default:
          other.push_back(std::move(r));
          break;
      }
    }
    results.clear();
    for (auto& r : tabs) results.push_back(std::move(r));
    for (auto& r : history) results.push_back(std::move(r));
    for (auto& r : bookmarks) results.push_back(std::move(r));
    for (auto& r : other) results.push_back(std::move(r));
    for (auto& r : leftovers) results.push_back(std::move(r));
    if (results.size() > max_visible) {
      results.resize(max_visible);
    }
    return;
  }

  if (results.size() > max_visible) {
    results.resize(max_visible);
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

const MahoCommandModel::SiteSearchEngine* MahoCommandModel::MatchExactSiteSearchKeyword(
    const std::string& trimmed) const {
  std::string keyword = trimmed;
  if (!keyword.empty() && keyword[0] == '@') {
    keyword = keyword.substr(1);
  }
  if (keyword.empty() || keyword.find(' ') != std::string::npos) {
    return nullptr;
  }
  for (const auto& engine : site_search_engines_) {
    if (engine.prefix == keyword) {
      return &engine;
    }
  }
  return nullptr;
}

// static
GURL MahoCommandModel::ExpandSiteSearchTemplate(const std::string& url_template,
                                                const std::string& query) {
  if (url_template.empty() || query.empty()) {
    return GURL();
  }
  const std::string escaped = base::EscapeQueryParamValue(query, true);
  std::string expanded = url_template;
  bool substituted = false;
  for (const std::string_view placeholder : {"{query}", "{searchTerms}", "%s"}) {
    size_t pos = expanded.find(placeholder);
    while (pos != std::string::npos) {
      expanded.replace(pos, placeholder.size(), escaped);
      substituted = true;
      pos = expanded.find(placeholder, pos + escaped.size());
    }
  }
  if (!substituted) {
    return GURL();
  }
  GURL url(expanded);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return GURL();
  }
  return url;
}

GURL MahoCommandModel::BuildSiteSearchUrl(const std::string& prefix,
                                          const std::string& query) const {
  for (const auto& engine : site_search_engines_) {
    if (engine.prefix == prefix) {
      return ExpandSiteSearchTemplate(engine.url_template, query);
    }
  }
  return GURL();
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
  if (results.size() > static_cast<size_t>(kMaxVisibleRows))
    results.resize(kMaxVisibleRows);
  return results;
}

void MahoCommandModel::RequestSearchEngineFavicons(uint32_t generation) {
  if (generation != search_generation_) {
    return;
  }
  MahoCore* core = maho::GetCore();
  if (!core || !browser_ || !browser_->GetProfile()) {
    return;
  }

  std::string default_engine_id;
  GURL homepage_url;

  char* json = maho_core_get_search_engines(core);
  if (json) {
    std::string json_str(json);
    maho_string_free(json);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_list()) {
      for (const auto& item : parsed->GetList()) {
        const auto* dict = item.GetIfDict();
        if (!dict)
          continue;
        const std::string* id = dict->FindString("id");
        const std::optional<bool> is_default = dict->FindBool("isDefault");
        if (id && is_default.value_or(false)) {
          default_engine_id = *id;
          if (default_engine_id == "google") {
            homepage_url = GURL("https://www.google.com");
          } else if (default_engine_id == "duckduckgo") {
            homepage_url = GURL("https://duckduckgo.com");
          } else if (default_engine_id == "bing") {
            homepage_url = GURL("https://www.bing.com");
          } else if (default_engine_id == "brave") {
            homepage_url = GURL("https://search.brave.com");
          } else if (default_engine_id == "ecosia") {
            homepage_url = GURL("https://www.ecosia.org");
          } else {
            homepage_url = GURL("https://www.google.com");
          }
          break;
        }
      }
    }
  }

  if (default_engine_id.empty() || !homepage_url.is_valid()) {
    return;
  }

  auto it = engine_favicon_cache_.find(default_engine_id);
  if (it != engine_favicon_cache_.end()) {
    std::optional<ImageData> icon = BuildImageDataFromImage(it->second);
    if (icon.has_value()) {
      bool updated = false;
      for (auto& suggestion : results_) {
        if (suggestion.type == CommandSuggestionType::kSearch && !suggestion.icon.has_value()) {
          suggestion.icon = *icon;
          updated = true;
        }
      }
      if (updated && generation == search_generation_ && pending_callback_) {
        pending_callback_.Run(results_);
      }
    }
    return;
  }

  favicon::LargeIconService* large_icon_service =
      LargeIconServiceFactory::GetForBrowserContext(browser_->GetProfile());
  if (!large_icon_service) {
    return;
  }

  large_icon_service->GetLargeIconFromCacheFallbackToGoogleServer(
      homepage_url,
      favicon::LargeIconService::StandardIconSize::k16x16,
      favicon::LargeIconService::StandardIconSize::k16x16,
      favicon::LargeIconService::NoBigEnoughIconBehavior::kReturnEmpty,
      /*should_trim_page_url_path=*/false,
      kBookmarkFaviconTrafficAnnotation,
      base::BindOnce(&MahoCommandModel::OnSearchEngineLargeIconLoaded,
                     weak_factory_.GetWeakPtr(), generation,
                     default_engine_id),
      &favicon_task_tracker_);
}

void MahoCommandModel::OnSearchEngineLargeIconLoaded(
    uint32_t search_generation,
    std::string engine_id,
    const favicon_base::LargeIconResult& result) {
  if (search_generation != search_generation_) {
    return;
  }

  if (!result.bitmap.is_valid()) {
    return;
  }

  gfx::Image favicon_image = gfx::Image::CreateFrom1xPNGBytes(
      base::span(*result.bitmap.bitmap_data));
  if (favicon_image.IsEmpty()) {
    return;
  }

  engine_favicon_cache_[engine_id] = favicon_image;

  std::optional<ImageData> icon = BuildImageDataFromImage(favicon_image);
  if (!icon.has_value()) {
    return;
  }

  bool updated = false;
  for (auto& suggestion : results_) {
    if (suggestion.type == CommandSuggestionType::kSearch && !suggestion.icon.has_value()) {
      suggestion.icon = *icon;
      updated = true;
    }
  }

  if (updated && search_generation == search_generation_ &&
      pending_callback_) {
    pending_callback_.Run(results_);
  }
}

void MahoCommandModel::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tabs_cache_dirty_ = true;
}

void MahoCommandModel::OnTabChangedAt(tabs::TabInterface* tab,
                                    TabChangeType change_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tabs_cache_dirty_ = true;
}

void MahoCommandModel::OnBrowserCreated(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Exact-primary-Incognito observes only its own window; never attach to a
  // newly created (potentially other-OTR) window.
  if (context_class_ == MahoPrivateContextClass::kPrimaryIncognito) {
    return;
  }
  if (browser && browser->GetTabStripModel()) {
    browser->GetTabStripModel()->AddObserver(this);
  }
  tabs_cache_dirty_ = true;
}

void MahoCommandModel::OnBrowserClosed(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (context_class_ == MahoPrivateContextClass::kPrimaryIncognito) {
    // Our exact-primary-Incognito window is going away. Cancel any pending
    // debounced search and drop the browser pointer so a late-firing
    // FireSearchPrivate() cannot query a KeyedService (e.g. TemplateURLService)
    // on the shutting-down OTR profile after KeyedService::Shutdown() has run.
    if (browser_ && browser && browser->GetTabStripModel() &&
        browser->GetTabStripModel() == browser_->GetTabStripModel()) {
      Cancel();
      browser_->GetTabStripModel()->RemoveObserver(this);
      browser_ = nullptr;
    }
    return;
  }
  if (browser && browser->GetTabStripModel()) {
    browser->GetTabStripModel()->RemoveObserver(this);
  }
  tabs_cache_dirty_ = true;
}

}  // namespace maho
