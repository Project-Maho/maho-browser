// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_bridge.h"

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "maho/browser/maho_space_profile_hydration.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"

namespace maho {

void MahoSpaceProfileBridge::Observer::OnSpaceProfileBridgeChanged(
    bool is_structural) {
  OnSpaceProfileBridgeChanged();
}

MahoSpaceProfileBridge* MahoSpaceProfileBridge::GetInstance() {
  static base::NoDestructor<MahoSpaceProfileBridge> instance;
  return instance.get();
}

MahoSpaceProfileBridge::MahoSpaceProfileBridge() {
  browser_collection_observation_.Observe(
      GlobalBrowserCollection::GetInstance());
}

MahoSpaceProfileBridge::~MahoSpaceProfileBridge() = default;

void MahoSpaceProfileBridge::RegisterSpace(const std::string& space_id,
                                           const base::FilePath& profile_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  space_to_profile_[space_id] = profile_path;
  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
}

void MahoSpaceProfileBridge::UnregisterSpace(const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  space_to_profile_.erase(space_id);
  last_active_tab_per_space_.erase(space_id);
  if (active_space_id_ == space_id) {
    active_space_id_.clear();
  }
  for (auto it = browser_active_space_.begin();
       it != browser_active_space_.end();) {
    if (it->second == space_id) {
      it = browser_active_space_.erase(it);
    } else {
      ++it;
    }
  }
  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
}

void MahoSpaceProfileBridge::SetActiveSpaceId(const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_space_id_ = space_id;
}

void MahoSpaceProfileBridge::SetActiveSpaceId(Browser* browser,
                                              const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (browser) {
    browser_active_space_[browser] = space_id;
  } else {
    active_space_id_ = space_id;
  }
}

void MahoSpaceProfileBridge::NotifyChanged(bool is_structural) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged(is_structural);
  }
}

void MahoSpaceProfileBridge::SetLastActiveTab(const std::string& space_id,
                                              const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  last_active_tab_per_space_[space_id] = tab_id;
}

std::string MahoSpaceProfileBridge::GetLastActiveTab(
    const std::string& space_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = last_active_tab_per_space_.find(space_id);
  if (it == last_active_tab_per_space_.end()) {
    return std::string();
  }
  return it->second;
}

void MahoSpaceProfileBridge::AddObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void MahoSpaceProfileBridge::RemoveObserver(Observer* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

const std::string& MahoSpaceProfileBridge::GetActiveSpaceId() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return active_space_id_;
}

const std::string& MahoSpaceProfileBridge::GetActiveSpaceId(
    Browser* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return active_space_id_;
  }
  auto it = browser_active_space_.find(browser);
  return it == browser_active_space_.end() ? base::EmptyString() : it->second;
}

const std::string& MahoSpaceProfileBridge::GetActiveSpaceId(
    BrowserWindowInterface* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return active_space_id_;
  }
  Browser* browser_ptr = browser->GetBrowserForMigrationOnly();
  return browser_ptr ? GetActiveSpaceId(browser_ptr) : base::EmptyString();
}

size_t MahoSpaceProfileBridge::GetRegisteredSpaceCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return space_to_profile_.size();
}

int MahoSpaceProfileBridge::GetActiveSpaceIndex() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_space_id_.empty()) {
    return -1;
  }

  int index = 0;
  for (const auto& entry : space_to_profile_) {
    const auto& space_id = entry.first;
    if (space_id == active_space_id_) {
      return index;
    }
    ++index;
  }

  return -1;
}

int MahoSpaceProfileBridge::GetActiveSpaceIndex(Browser* browser) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string& space_id = GetActiveSpaceId(browser);
  if (space_id.empty()) {
    return -1;
  }

  int index = 0;
  for (const auto& entry : space_to_profile_) {
    if (entry.first == space_id) {
      return index;
    }
    ++index;
  }

  return -1;
}

void MahoSpaceProfileBridge::ClearBrowserActiveSpace(Browser* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (browser) {
    MahoSpaceThemeState::ClearPreviewOverride(browser);
    browser_active_space_.erase(browser);
  }
}

std::vector<std::string> MahoSpaceProfileBridge::GetSpaceIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> ids;
  ids.reserve(space_to_profile_.size());
  for (const auto& entry : space_to_profile_) {
    ids.push_back(entry.first);
  }
  return ids;
}

std::string MahoSpaceProfileBridge::GetSpaceIdForProfile(
    Profile* profile) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!profile) {
    return "default";
  }
  const base::FilePath profile_basename = profile->GetPath().BaseName();
  for (const auto& entry : space_to_profile_) {
    if (entry.second == profile_basename) {
      return entry.first;
    }
  }
  return "default";
}

base::FilePath MahoSpaceProfileBridge::GetProfilePathForSpace(
    const std::string& space_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = space_to_profile_.find(space_id);
  if (it != space_to_profile_.end()) {
    return it->second;
  }
  return base::FilePath();
}

// static
std::optional<base::FilePath> MahoSpaceProfileBridge::ProfileBasenameForId(
    std::string_view profile_id) {
  return maho::ProfileBasenameForId(profile_id);
}

}  // namespace maho
