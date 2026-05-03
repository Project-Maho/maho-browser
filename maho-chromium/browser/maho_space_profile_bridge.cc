// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_bridge.h"

#include "base/logging.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_window.h"

namespace maho {

MahoSpaceProfileBridge* MahoSpaceProfileBridge::GetInstance() {
  static base::NoDestructor<MahoSpaceProfileBridge> instance;
  return instance.get();
}

MahoSpaceProfileBridge::MahoSpaceProfileBridge() = default;
MahoSpaceProfileBridge::~MahoSpaceProfileBridge() = default;

void MahoSpaceProfileBridge::RegisterSpace(
    const std::string& space_id,
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
  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
}

bool MahoSpaceProfileBridge::SwitchToSpace(const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = space_to_profile_.find(space_id);
  if (it == space_to_profile_.end()) {
    LOG(WARNING) << "Maho: Unknown space_id: " << space_id;
    return false;
  }

  auto old_it = space_to_profile_.find(active_space_id_);
  bool profile_changed = (old_it == space_to_profile_.end() ||
                          old_it->second != it->second);

  active_space_id_ = space_id;

  if (profile_changed) {
    profiles::SwitchToProfile(it->second, /*always_create=*/false);
  }

  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
  return true;
}

void MahoSpaceProfileBridge::SetActiveSpaceId(const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_space_id_ = space_id;
}

void MahoSpaceProfileBridge::NotifyChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
}

void MahoSpaceProfileBridge::SetLastActiveTab(const std::string& space_id,
                                               int tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  last_active_tab_per_space_[space_id] = tab_id;
}

int MahoSpaceProfileBridge::GetLastActiveTab(
    const std::string& space_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = last_active_tab_per_space_.find(space_id);
  if (it == last_active_tab_per_space_.end()) {
    return -1;
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

std::vector<std::string> MahoSpaceProfileBridge::GetSpaceIds() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> ids;
  ids.reserve(space_to_profile_.size());
  for (const auto& entry : space_to_profile_) {
    ids.push_back(entry.first);
  }
  return ids;
}

}  // namespace maho
