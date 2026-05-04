// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_

#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

class Browser;
class TabStripModel;

namespace maho {

enum class CommandSuggestionType {
  kTab,
  kBookmark,
  kHistory,
  kAction,
  kNavigation,
  kCalculator,
  kUnitConversion,
  kArchivedTab,
  kClosedTab,
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
  std::string action_id;
  int32_t tab_index = -1;
  // Each pair is (start, end) character offset for match highlighting.
  std::vector<std::pair<int, int>> match_ranges;
};

// Owns search state and calls Rust FFI on a thread pool.
class MahoCommandModel {
 public:
  using ResultsCallback =
      base::RepeatingCallback<void(std::vector<CommandSuggestion>)>;

  explicit MahoCommandModel(Browser* browser);
  MahoCommandModel(const MahoCommandModel&) = delete;
  MahoCommandModel& operator=(const MahoCommandModel&) = delete;
  ~MahoCommandModel();

  // Trigger a search. Results arrive asynchronously via the callback.
  void Search(const std::string& query, ResultsCallback callback);

  // Cancel any pending search.
  void Cancel();

  const std::string& query() const { return query_; }
  const std::vector<CommandSuggestion>& results() const { return results_; }
  int selected_index() const { return selected_index_; }
  void set_selected_index(int index) { selected_index_ = index; }

 private:
  static std::string BuildTabsJson(TabStripModel* model);
  static std::vector<CommandSuggestion> SearchOnPool(
      const std::string& query,
      const std::string& tabs_json);
  static CommandSuggestionType ParseType(const std::string& kind);

  void FireSearch();
  void OnResults(ResultsCallback callback,
                 std::vector<CommandSuggestion> results);

  raw_ptr<Browser> browser_;
  std::string query_;
  std::vector<CommandSuggestion> results_;
  int selected_index_ = 0;
  bool actions_only_ = false;
  ResultsCallback pending_callback_;
  base::OneShotTimer debounce_timer_;

  base::WeakPtrFactory<MahoCommandModel> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_MODEL_H_
