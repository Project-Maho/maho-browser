// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

class MahoMruTabTracker::ContentsWatcher : public content::WebContentsObserver {
 public:
  ContentsWatcher(content::WebContents* contents, MahoMruTabTracker* owner)
      : content::WebContentsObserver(contents), owner_(owner) {}

  void WebContentsDestroyed() override {
    content::WebContents* wc = web_contents();
    if (owner_ && wc) {
      owner_->OnContentsDestroyed(wc);
    }
  }

 private:
  raw_ptr<MahoMruTabTracker> owner_;
};

MahoMruTabTracker::MahoMruTabTracker(TabStripModel* tab_strip_model)
    : tab_strip_model_(tab_strip_model) {
  if (!tab_strip_model_) {
    return;
  }
  tab_strip_model_->AddObserver(this);

  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    if (contents) {
      mru_.push_back(contents);
      StartWatching(contents);
    }
  }
  content::WebContents* active = tab_strip_model_->GetActiveWebContents();
  if (active) {
    PushToFront(active);
  }
}

MahoMruTabTracker::~MahoMruTabTracker() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  watchers_.clear();
}

std::vector<content::WebContents*> MahoMruTabTracker::GetMruList() const {
  return std::vector<content::WebContents*>(mru_.begin(), mru_.end());
}

std::vector<content::WebContents*> MahoMruTabTracker::GetMruListForSpace(
    const std::string& space_id) const {
  std::vector<content::WebContents*> full = GetMruList();
  if (space_id.empty()) {
    return full;
  }

  const auto& tab_to_space = EnsureSpaceCache();
  if (tab_to_space.empty()) {
    return full;
  }

  std::vector<content::WebContents*> filtered;
  filtered.reserve(full.size());
  for (content::WebContents* contents : full) {
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper) {
      continue;
    }
    const std::string& tab_id = helper->stable_tab_id();
    if (tab_id.empty()) {
      continue;
    }
    auto it = tab_to_space.find(tab_id);
    if (it != tab_to_space.end() && it->second == space_id) {
      filtered.push_back(contents);
    }
  }
  return filtered;
}

void MahoMruTabTracker::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  InvalidateSpaceCache();
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& delta : change.GetInsert()->contents) {
        content::WebContents* contents = delta.contents;
        if (!contents) {
          continue;
        }
        Remove(contents);
        if (mru_.empty()) {
          mru_.push_back(contents);
        } else {
          mru_.insert(mru_.begin() + 1, contents);
        }
        StartWatching(contents);
      }
      break;
    case TabStripModelChange::kRemoved:
      for (const auto& delta : change.GetRemove()->contents) {
        Remove(delta.contents);
        StopWatching(delta.contents);
      }
      break;
    case TabStripModelChange::kReplaced: {
      auto* replace = change.GetReplace();
      auto it = std::find(mru_.begin(), mru_.end(), replace->old_contents);
      if (it != mru_.end()) {
        *it = replace->new_contents;
      }
      StopWatching(replace->old_contents);
      StartWatching(replace->new_contents);
      break;
    }
    case TabStripModelChange::kMoved:
    case TabStripModelChange::kSelectionOnly:
      break;
  }

  if (selection.active_tab_changed() && selection.new_contents) {
    PushToFront(selection.new_contents);
  }
}

void MahoMruTabTracker::PushToFront(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  Remove(contents);
  mru_.push_front(contents);
}

void MahoMruTabTracker::Remove(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  auto it = std::find(mru_.begin(), mru_.end(), contents);
  if (it != mru_.end()) {
    mru_.erase(it);
  }
}

void MahoMruTabTracker::StartWatching(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  if (watchers_.find(contents) != watchers_.end()) {
    return;
  }
  watchers_[contents] = std::make_unique<ContentsWatcher>(contents, this);
}

void MahoMruTabTracker::StopWatching(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  watchers_.erase(contents);
}

void MahoMruTabTracker::OnContentsDestroyed(content::WebContents* contents) {
  // WebContents can be destroyed out-of-band (renderer crash, extension close,
  // programmatic tab.remove).  Prune from the MRU deque to eliminate any
  // dangling raw pointer and invalidate the space cache.
  Remove(contents);
  watchers_.erase(contents);
  InvalidateSpaceCache();
}

void MahoMruTabTracker::InvalidateSpaceCache() const {
  space_cache_.clear();
  space_cache_valid_ = false;
}

const base::flat_map<std::string, std::string>&
MahoMruTabTracker::EnsureSpaceCache() const {
  if (space_cache_valid_) {
    return space_cache_;
  }

  auto* core = maho::GetCore();
  if (!core) {
    space_cache_valid_ = true;
    return space_cache_;
  }

  char* json_str = maho_core_get_tab_view_models(core);
  if (!json_str) {
    space_cache_valid_ = true;
    return space_cache_;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    space_cache_valid_ = true;
    return space_cache_;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* sid = dict->FindString("spaceId");
    if (id && sid) {
      space_cache_[*id] = *sid;
    }
  }
  space_cache_valid_ = true;
  return space_cache_;
}

}  // namespace maho
