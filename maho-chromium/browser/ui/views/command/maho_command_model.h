// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_

#include <string>
#include <optional>
#include <cstdint>
#include <utility>
#include <vector>

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
#include "ui/gfx/image/image.h"

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
 public:
  using ResultsCallback =
      base::RepeatingCallback<void(std::vector<CommandSuggestion>)>;

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

  static constexpr int kMaxVisibleRows = 5;

  static CommandSuggestionType ParseType(const std::string& kind);
  static void CapResults(std::vector<CommandSuggestion>& results, CommandOverlayMode mode, const std::string& query = std::string());
  static void ApplyQuickActionGate(std::vector<CommandSuggestion>& results,
                                   CommandOverlayMode mode,
                                   const std::string& query);
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
  static std::vector<CommandSuggestion> ParseSearchResultsJson(
      const std::string& json_str,
      const std::vector<CommandSuggestion>& chromium_bookmarks);

 private:
  // Returns cached JSON (rebuilds lazily when tabs_cache_dirty_ is true).
  // Must be called on the UI sequence.
  std::string BuildTabsJson();
  static std::vector<CommandSuggestion> BuildChromiumBookmarkSuggestions(
      Browser* browser,
      const std::string& query);
  static std::vector<CommandSuggestion> SearchOnPool(
      const std::string& query,
      const std::string& tabs_json,
      const std::vector<CommandSuggestion>& chromium_bookmarks,
      CommandOverlayMode mode,
      bool is_incognito);

  void FireSearch();
  // Exact-primary-Incognito search: builds only ephemeral sources (direct URL,
  // OTR TemplateURLService default-provider search, current-window OTR tabs,
  // and the frozen 13 actions) with no MahoCore call and no saved-source read.
  void FireSearchPrivate();
  std::vector<CommandSuggestion> BuildPrivateResults(const std::string& query);
  std::optional<std::pair<std::string, std::string>>
  ResolveDefaultProviderSearch(const std::string& query) const;
  void OnResults(ResultsCallback callback,
                 std::vector<CommandSuggestion> results);
  void RequestBookmarkFavicons();
  void OnBookmarkLargeIconLoaded(uint32_t search_generation,
                                  std::string bookmark_url,
                                  const favicon_base::LargeIconResult& result);
  void RequestTabFavicons();
  void HydrateSiteSearchEngines();
  void RequestSearchEngineFavicons();
  void OnSearchEngineLargeIconLoaded(uint32_t search_generation,
                                     std::string engine_id,
                                     const favicon_base::LargeIconResult& result);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
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
  base::TimeTicks search_start_ticks_;

  base::flat_map<std::string, gfx::Image> engine_favicon_cache_;

  // Tab JSON cache — invalidated whenever any observed TabStripModel or the
  // browser collection changes. Rebuilt lazily in BuildTabsJson().
  mutable std::string cached_tabs_json_;
  bool tabs_cache_dirty_ = true;

  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoCommandModel> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
