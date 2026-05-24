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
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"

class Browser;

namespace favicon_base {
struct LargeIconResult;
}

namespace maho {

enum class CommandOverlayMode {
  kSearch,       // Default generic search.
  kCurrentTab,   // Cmd+L: pre-fill URL, "Search or Enter URL..."
  kNewTab,       // Cmd+T: "Search in new tab..."
  kSiteSearch,   // @prefix + Tab: site-specific search.
  kAI,           // "ai" + Tab: AI chat mode.
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
  // Each pair is (start, end) character offset for match highlighting.
  std::vector<std::pair<int, int>> match_ranges;
};

// Owns search state and calls Rust FFI on a thread pool.
class MahoCommandModel {
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
  ~MahoCommandModel();

  void Search(const std::string& query,
             CommandOverlayMode mode,
             ResultsCallback callback);

  void Cancel();

  void SaveRecentSearch(const std::string& query);
  std::vector<CommandSuggestion> GetRecentSearches();
  const SiteSearchEngine* MatchSiteSearch(const std::string& query) const;
  const std::vector<SiteSearchEngine>& site_search_engines() const {
    return site_search_engines_;
  }

  const std::string& query() const { return query_; }
  const std::vector<CommandSuggestion>& results() const { return results_; }
  int selected_index() const { return selected_index_; }
  void set_selected_index(int index) { selected_index_ = index; }
  void set_results(std::vector<CommandSuggestion> results) {
    results_ = std::move(results);
  }

  static CommandSuggestionType ParseType(const std::string& kind);
  static void CapResults(std::vector<CommandSuggestion>& results);
  static base::Value BuildTabEntryDict(const std::string& stable_id,
                                       const std::string& browser_session_id,
                                       const std::string& tab_session_id,
                                       const std::string& title,
                                       const std::string& url);

 private:
  static std::string BuildTabsJson(Browser* browser);
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
  void OnResults(ResultsCallback callback,
                 std::vector<CommandSuggestion> results);
  void RequestBookmarkFavicons();
  void OnBookmarkLargeIconLoaded(uint32_t search_generation,
                                  std::string bookmark_url,
                                  const favicon_base::LargeIconResult& result);
  void RequestTabFavicons();
  void HydrateSiteSearchEngines();

  raw_ptr<Browser> browser_;
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

  base::WeakPtrFactory<MahoCommandModel> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
