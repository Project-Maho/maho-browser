// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "base/containers/flat_map.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_remote_search_suggestions.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

class Browser;
class BrowserWindowInterface;
class GlobalBrowserCollection;
class TabStripModel;

namespace favicon_base {
struct LargeIconResult;
}

namespace maho {

enum class CommandOverlayMode {
  kSearch,       // Default generic search.
  kCurrentTab,   // Cmd+L: pre-fill URL, "Search or Enter URL..."
  kNewTab,       // Cmd+T: "Search in new tab..."
  kSiteSearch,   // @prefix + Tab: site-specific search.
  kCommandsOnly, // ">" command mode.
};

enum class PaletteAction {
  kSearch,
  kAskMaho,
};

enum class CommandSuggestionType {
  kTab,
  kBookmark,
  kHistory,
  kAction,
  kNavigation,
  kSearch,
  kCalculator,
  kUnitConversion,
  kArchivedTab,
  kClosedTab,
  kFolder,
  kRecentSearch,
};

struct ImageData {
  std::vector<uint8_t> data;
  int width = 0;
  int height = 0;
  std::string format;

  ImageData();
  ImageData(const ImageData&);
  ImageData(ImageData&&);
  ImageData& operator=(const ImageData&);
  ImageData& operator=(ImageData&&);
  ~ImageData();
};

struct CommandSuggestion {
  CommandSuggestion();
  CommandSuggestion(const CommandSuggestion&);
  CommandSuggestion(CommandSuggestion&&);
  CommandSuggestion& operator=(const CommandSuggestion&);
  CommandSuggestion& operator=(CommandSuggestion&&);
  ~CommandSuggestion();

  CommandSuggestionType type = CommandSuggestionType::kHistory;
  std::string key;
  std::string title;
  std::string subtitle;
  std::string execution_payload;
  std::optional<ImageData> icon;
  std::string action_id;
  std::string shortcut_label;
  std::string folder_id;
  int32_t tab_index = -1;
  std::string stable_tab_id;
  std::string browser_session_id;
  std::string tab_session_id;
  // Core TabId (UUID) for a recently-opened suspended tab that lives only in
  // the Rust core (no live WebContents). Set when |is_suspended| is true.
  std::string tab_core_id;
  // True for recent/suspended tab suggestions emitted on an empty query.
  bool is_suspended = false;
  // Each pair is (start, end) character offset for match highlighting.
  std::vector<std::pair<int, int>> match_ranges;
};

// Owns search state and calls Rust FFI on a thread pool.
// Observes all open TabStripModels and GlobalBrowserCollection to keep the tab
// JSON cache fresh. Cache is invalidated on any tab or browser change and
// rebuilt lazily.
class MahoCommandModel : public TabStripModelObserver,
                         public BrowserCollectionObserver {
  friend class MahoCommandModelTest;
  friend class MahoCommandModelTabObserverTest;
 public:
  using ResultsCallback =
      base::RepeatingCallback<void(std::vector<CommandSuggestion>)>;
  using RemoteSourceFactory = base::RepeatingCallback<
      std::unique_ptr<MahoRemoteSearchSuggestionSource>(Profile*)>;

  struct SiteSearchEngine {
    std::string prefix;
    std::string name;
    std::string url_template;
  };

  explicit MahoCommandModel(Browser* browser);
  MahoCommandModel(const MahoCommandModel&) = delete;
  MahoCommandModel& operator=(const MahoCommandModel&) = delete;
  ~MahoCommandModel() override;

  void Search(const std::string& query,
             CommandOverlayMode mode,
             ResultsCallback callback);

  void Cancel();

  void SaveRecentSearch(const std::string& query);
  std::vector<CommandSuggestion> GetRecentSearches();
  const SiteSearchEngine* MatchSiteSearch(const std::string& query) const;
  const SiteSearchEngine* MatchExactSiteSearchKeyword(const std::string& trimmed) const;
  // Expands the site-search engine registered under |prefix| for |query|.
  // Returns an invalid GURL when no engine matches or the template does not
  // produce an http(s) URL.
  GURL BuildSiteSearchUrl(const std::string& prefix,
                          const std::string& query) const;
  // Substitutes the URL-escaped |query| into a {query}, {searchTerms} or %s
  // placeholder of |url_template|.
  static GURL ExpandSiteSearchTemplate(const std::string& url_template,
                                       const std::string& query);
  const std::vector<SiteSearchEngine>& site_search_engines() const {
    return site_search_engines_;
  }
  void AddSiteSearchEngineForTest(SiteSearchEngine engine) {
    site_search_engines_.push_back(std::move(engine));
  }

  const std::string& query() const { return query_; }
  const std::vector<CommandSuggestion>& results() const { return results_; }
  int selected_index() const { return selected_index_; }
  void set_selected_index(int index) { selected_index_ = index; }
  void set_results(std::vector<CommandSuggestion> results) {
    results_ = std::move(results);
  }
  Browser* browser() const { return browser_; }
  void RefetchCurrentQuery();

  // Narrow lifecycle seams used to exercise generation-bound source replies
  // without enabling production remote starts before Task 5.
  void SetRemoteSearchSourceForTesting(
      std::unique_ptr<MahoRemoteSearchSuggestionSource> source);
  void SetRemoteSearchSourceFactoryForTesting(RemoteSourceFactory factory);
  void StartRemoteSearchForTesting(uint32_t generation,
                                   const std::string& query);
  void AcceptLocalResultsForTesting(
      uint32_t generation,
      std::vector<CommandSuggestion> results);
  void ApplyFaviconForTesting(uint32_t generation, const std::string& key);
  uint32_t search_generation_for_testing() const {
    return search_generation_;
  }
  const std::vector<RemoteSearchSuggestion>& remote_snapshot_for_testing()
      const {
    return remote_snapshot_;
  }
  int favicon_request_pass_count_for_testing() const {
    return favicon_request_pass_count_for_testing_;
  }

  static constexpr int kMaxVisibleRows = 5;

  static CommandSuggestionType ParseType(const std::string& kind);
  static void CapResults(std::vector<CommandSuggestion>& results, CommandOverlayMode mode, const std::string& query = std::string());
  static void ApplyQuickActionGate(std::vector<CommandSuggestion>& results,
                                   CommandOverlayMode mode,
                                   const std::string& query);
  static void ApplyMailAvailabilityGate(
      std::vector<CommandSuggestion>& results,
      bool mail_available);
  static std::vector<CommandSuggestion> PrepareLocalCandidates(
      std::vector<CommandSuggestion> results,
      bool actions_only,
      CommandOverlayMode mode,
      const std::string& query);
  static std::vector<CommandSuggestion> NormalizeRemoteSuggestions(
      const std::string& query,
      const std::vector<CommandSuggestion>& prepared_local_results,
      const std::vector<RemoteSearchSuggestion>& remote_suggestions);
  static std::vector<CommandSuggestion> AllocateFinalResults(
      std::vector<CommandSuggestion> prepared_local_results,
      const std::vector<CommandSuggestion>& remote_results,
      CommandOverlayMode mode,
      const std::string& query);
  static bool FinalSnapshotsEqual(
      const std::vector<CommandSuggestion>& lhs,
      const std::vector<CommandSuggestion>& rhs);
  static bool IsRemoteSearchEligible(
      Profile* profile,
      MahoPrivateContextClass context_class,
      CommandOverlayMode mode,
      const std::string& trimmed_query,
      bool matches_site_search);
  static base::Value BuildTabEntryDict(const std::string& stable_id,
                                       const std::string& browser_session_id,
                                       const std::string& tab_session_id,
                                       const std::string& title,
                                       const std::string& url);

  // Private-context contract helpers (VerificationContract 4). Exposed for the
  // task-10 unit tests; all are pure and take a context class or plain inputs.
  //
  // Saved sources (bookmarks, history, recent searches, site-search picker,
  // Maho search engines) may only be read for kRegular.
  static bool ShouldReadSavedSourcesForContext(MahoPrivateContextClass klass);
  // The frozen 13 ephemeral action IDs, in the exact order emitted by the Rust
  // CommandBarEngine::search_incognito (command_bar.rs). Must stay identical.
  static std::vector<std::string> PrivateActionAllowlist();
  // In primary Incognito, web search must derive solely from the OTR
  // TemplateURLService default provider (read-only); regular uses MahoCore.
  static bool PrivateSearchUsesDefaultProviderOnly(MahoPrivateContextClass klass);
  static CommandSuggestion BuildDefaultProviderSearchSuggestion(
      const std::string& query,
      const std::string& search_url,
      const std::string& engine_name);
  static std::optional<CommandSuggestion> BuildNavigationSuggestionForQuery(
      const std::string& query);

  // How typed palette text should navigate, classified with the omnibox's
  // AutocompleteInput rules (TLD/IP/port heuristics). Scheme-less input
  // canonicalizes to http://; HTTPS-Upgrades then upgrades unique hosts with
  // an automatic http fallback and leaves localhost/IP/intranet hosts on http,
  // exactly like omnibox navigations. std::nullopt means the text is a search.
  struct TypedNavigation {
    GURL url;
    // True when the user explicitly typed an http:// scheme; the navigation
    // must then set NavigateParams::url_typed_with_http_scheme so
    // HTTPS-Upgrades does not override the explicit choice.
    bool typed_http_scheme = false;
  };
  static std::optional<TypedNavigation> ClassifyTypedNavigation(
      const std::string& text);

  static std::vector<CommandSuggestion> ParseSearchResultsJson(
      const std::string& json_str);

 private:
  // Returns cached JSON (rebuilds lazily when tabs_cache_dirty_ is true).
  // Must be called on the UI sequence.
  std::string BuildTabsJson();
  static std::vector<CommandSuggestion> SearchOnPool(
      const std::string& query,
      const std::string& tabs_json,
      CommandOverlayMode mode,
      bool is_incognito);

  void FireSearch(uint32_t generation);
  // Exact-primary-Incognito search: builds only ephemeral sources (direct URL,
  // OTR TemplateURLService default-provider search, current-window OTR tabs,
  // and the frozen 13 actions) with no MahoCore call and no saved-source read.
  void FireSearchPrivate(uint32_t generation);
  std::vector<CommandSuggestion> BuildPrivateResults(const std::string& query);
  std::optional<std::pair<std::string, std::string>>
  ResolveDefaultProviderSearch(const std::string& query) const;
  void OnResults(uint32_t generation,
                 std::vector<CommandSuggestion> results);
  void OnRemoteSnapshot(
      uint32_t generation,
      std::vector<RemoteSearchSuggestion> remote_snapshot);
  bool PublishMergedResultsIfChanged(uint32_t generation);
  void RequestBookmarkFavicons(uint32_t generation);
  void OnBookmarkLargeIconLoaded(uint32_t search_generation,
                                  std::string bookmark_url,
                                  const favicon_base::LargeIconResult& result);
  void RequestTabFavicons(uint32_t generation);
  // CoreReadyCallbackList handler: re-runs the current query once maho-core
  // finishes its asynchronous initialization, so a search that raced ahead of
  // the core (and observed GetCore() == null) does not leave the palette
  // showing the stale empty snapshot.
  void OnCoreReady();
  void HydrateSiteSearchEngines();
  void RequestSearchEngineFavicons(uint32_t generation);
  void OnSearchEngineLargeIconLoaded(uint32_t search_generation,
                                     std::string engine_id,
                                     const favicon_base::LargeIconResult& result);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  raw_ptr<Browser> browser_;
  MahoPrivateContextClass context_class_ = MahoPrivateContextClass::kNull;
  std::string query_;
  CommandOverlayMode mode_ = CommandOverlayMode::kSearch;
  std::vector<CommandSuggestion> results_;
  std::vector<SiteSearchEngine> site_search_engines_;
  int selected_index_ = 0;
  bool actions_only_ = false;
  ResultsCallback pending_callback_;
  base::OneShotTimer debounce_timer_;
  base::CancelableTaskTracker favicon_task_tracker_;
  uint32_t search_generation_ = 0;
  uint32_t traced_generation_ = 0;
  base::TimeTicks search_start_ticks_;
  RemoteSourceFactory remote_source_factory_;
  std::unique_ptr<MahoRemoteSearchSuggestionSource> remote_search_source_;
  std::vector<CommandSuggestion> local_snapshot_;
  std::vector<RemoteSearchSuggestion> remote_snapshot_;
  bool has_published_results_ = false;
  std::optional<base::TimeTicks> first_results_publication_ticks_;
  bool has_published_remote_results_ = false;
  int favicon_request_pass_count_for_testing_ = 0;

  base::flat_map<std::string, gfx::Image> engine_favicon_cache_;

  // Tab JSON cache — invalidated whenever any observed TabStripModel or the
  // browser collection changes. Rebuilt lazily in BuildTabsJson().
  mutable std::string cached_tabs_json_;
  bool tabs_cache_dirty_ = true;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  // Re-runs the pending query when the maho-core FFI becomes available.
  // Models created after the core is ready never observe a notification
  // (SetCore notifies only on non-null installs), so this is inert for
  // palettes opened later in the session.
  base::CallbackListSubscription core_ready_subscription_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoCommandModel> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
