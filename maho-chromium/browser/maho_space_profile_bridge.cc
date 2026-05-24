// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_bridge.h"

#include "base/logging.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"

namespace maho {

MahoSpaceProfileBridge* MahoSpaceProfileBridge::GetInstance() {
  static base::NoDestructor<MahoSpaceProfileBridge> instance;
  return instance.get();
}

MahoSpaceProfileBridge::MahoSpaceProfileBridge() {
  browser_collection_observation_.Observe(GlobalBrowserCollection::GetInstance());
}

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

bool MahoSpaceProfileBridge::SwitchToSpace(const std::string& space_id) {
  return SwitchToSpace(nullptr, space_id);
}

bool MahoSpaceProfileBridge::SwitchToSpace(Browser* browser,
                                           const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = space_to_profile_.find(space_id);
  if (it == space_to_profile_.end()) {
    LOG(WARNING) << "Maho: Unknown space_id: " << space_id;
    return false;
  }

  // Same-target fast path. For the browser-scoped case, only skip when an
  // explicit entry already exists; a missing entry must still be seeded even
  // if the global active_space_id_ fallback already equals space_id.
  if (browser) {
    auto browser_it = browser_active_space_.find(browser);
    if (browser_it != browser_active_space_.end() &&
        browser_it->second == space_id) {
      return true;
    }
  } else {
    if (active_space_id_ == space_id) {
      return true;
    }
  }

  const std::string& previous_space =
      browser ? GetActiveSpaceId(browser) : active_space_id_;
  auto old_it = space_to_profile_.find(previous_space);
  bool profile_changed = (old_it != space_to_profile_.end() &&
                          old_it->second != it->second);

  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_activate_space(core, space_id.c_str());
  }

  // Always update both browser-scoped and the process-global fallback so
  // surfaces that have no browser context see a consistent last-activated value.
  if (browser) {
    browser_active_space_[browser] = space_id;
  }
  active_space_id_ = space_id;

  if (profile_changed) {
    profiles::SwitchToProfile(it->second, /*always_create=*/false);
  }

  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();

  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
  return true;
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

void MahoSpaceProfileBridge::NotifyChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
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
  if (browser) {
    auto it = browser_active_space_.find(browser);
    if (it != browser_active_space_.end()) {
      return it->second;
    }
  }
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
    browser_active_space_.erase(browser);
  }
}

void MahoSpaceProfileBridge::OnBrowserCreated(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return;
  }
  Browser* browser_ptr = browser->GetBrowserForMigrationOnly();
  if (browser_ptr && !active_space_id_.empty()) {
    browser_active_space_.emplace(browser_ptr, active_space_id_);
  }
}

void MahoSpaceProfileBridge::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  ClearBrowserActiveSpace(browser->GetBrowserForMigrationOnly());
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

base::FilePath MahoSpaceProfileBridge::GetProfilePathForTesting(
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
  auto is_ascii_whitespace = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
  };
  std::string_view trimmed = profile_id;
  while (!trimmed.empty() && is_ascii_whitespace(trimmed.front())) {
    trimmed.remove_prefix(1);
  }
  while (!trimmed.empty() && is_ascii_whitespace(trimmed.back())) {
    trimmed.remove_suffix(1);
  }

  auto equals_case_insensitive = [](std::string_view a, std::string_view b) {
    if (a.length() != b.length()) {
      return false;
    }
    for (size_t i = 0; i < a.length(); ++i) {
      char ca = a[i];
      char cb = b[i];
      if (ca >= 'A' && ca <= 'Z') ca += 32;
      if (cb >= 'A' && cb <= 'Z') cb += 32;
      if (ca != cb) {
        return false;
      }
    }
    return true;
  };

  if (trimmed.empty() || equals_case_insensitive(trimmed, "default")) {
    return base::FilePath::FromASCII("Default");
  }

  // Validation rules for non-default ids:
  if (trimmed.length() > kMahoProfileIdMaxLen) {
    return std::nullopt;
  }

  if (trimmed.front() == '.') {
    return std::nullopt;
  }

  if (trimmed == "." || trimmed == "..") {
    return std::nullopt;
  }

  if (trimmed.find("..") != std::string_view::npos) {
    return std::nullopt;
  }

  for (char c : trimmed) {
    if (c < 32 || c > 126) {
      return std::nullopt;
    }
    if (c == '/' || c == '\\') {
      return std::nullopt;
    }
  }

  return base::FilePath::FromASCII("MahoProfile_" + std::string(trimmed));
}

}  // namespace maho
