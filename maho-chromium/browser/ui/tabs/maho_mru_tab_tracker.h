// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_MRU_TAB_TRACKER_H_
#define MAHO_BROWSER_UI_TABS_MAHO_MRU_TAB_TRACKER_H_

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace content {
class WebContents;
}

namespace maho {

// Tracks tab activation history for a single browser window in most-recently-
// used (MRU) order.  Front of the list is the most recently active tab;
// index [1] is the previously active tab; and so on.
//
// Ownership: expected to be owned by BrowserView with lifetime tied to the
// browser window.  The tracker holds a raw pointer to the TabStripModel and
// removes itself as an observer in the destructor.
class MahoMruTabTracker : public TabStripModelObserver {
 public:
  explicit MahoMruTabTracker(TabStripModel* tab_strip_model);
  MahoMruTabTracker(const MahoMruTabTracker&) = delete;
  MahoMruTabTracker& operator=(const MahoMruTabTracker&) = delete;
  ~MahoMruTabTracker() override;

  std::vector<content::WebContents*> GetMruList() const;

  // Filters GetMruList() to entries whose Maho space matches |space_id|.
  // Requires the MahoCore singleton to be initialized; if it is not or if
  // the FFI payload cannot be parsed, this returns the unfiltered
  // GetMruList() as a graceful fallback.
  //
  // The tab->space map is cached to avoid repeated FFI round-trips and JSON
  // parses on rapid Ctrl+Tab presses.  The cache is invalidated on any
  // TabStripModel change (insert/remove/replace/activate) since any of
  // those may reflect a space membership change on the Maho side.
  std::vector<content::WebContents*> GetMruListForSpace(
      const std::string& space_id) const;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  class ContentsWatcher;

  void PushToFront(content::WebContents* contents);
  void Remove(content::WebContents* contents);
  void StartWatching(content::WebContents* contents);
  void StopWatching(content::WebContents* contents);
  void OnContentsDestroyed(content::WebContents* contents);
  void InvalidateSpaceCache() const;
  const base::flat_map<std::string, std::string>& EnsureSpaceCache() const;

  raw_ptr<TabStripModel> tab_strip_model_;
  std::deque<content::WebContents*> mru_;
  std::unordered_map<content::WebContents*, std::unique_ptr<ContentsWatcher>>
      watchers_;

  mutable base::flat_map<std::string, std::string> space_cache_;
  mutable bool space_cache_valid_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_MRU_TAB_TRACKER_H_
