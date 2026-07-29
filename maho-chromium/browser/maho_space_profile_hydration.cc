// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_hydration.h"

#include <algorithm>
#include <utility>
#include "base/logging.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

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

  if (browser) {
    auto browser_it = browser_active_space_.find(browser);
    if (browser_it != browser_active_space_.end() &&
        browser_it->second == space_id) {
      return true;
    }
  } else if (active_space_id_ == space_id) {
    return true;
  }

  const std::string& previous_space =
      browser ? GetActiveSpaceId(browser) : active_space_id_;
  auto old_it = space_to_profile_.find(previous_space);
  bool profile_changed =
      old_it != space_to_profile_.end() && old_it->second != it->second;

  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_activate_space(core, space_id.c_str());
  }

  if (profile_changed) {
    active_space_id_ = space_id;
    if (g_browser_process && g_browser_process->profile_manager()) {
      Profile* target_profile =
          g_browser_process->profile_manager()->GetProfileByPath(
              g_browser_process->profile_manager()->user_data_dir().Append(
                  it->second));
      if (target_profile) {
        Browser* target_browser = chrome::FindBrowserWithProfile(target_profile);
        if (target_browser) {
          browser_active_space_[target_browser] = space_id;
        }
      }
    }
    profiles::SwitchToProfile(it->second, /*always_create=*/false);
  } else if (browser) {
    browser_active_space_[browser] = space_id;
  } else {
    active_space_id_ = space_id;
  }

  MahoSpaceThemeState::ChangedSpaceIds changed_spaces =
      MahoSpaceThemeState::UpdateFromCore();
  if (!profile_changed) {
    MahoSidebarView::RefreshSpaceThemeForBrowser(browser);
  }
  for (Browser* affected_browser : GetBrowsersForSpaces(changed_spaces)) {
    MahoSidebarView::RefreshSpaceThemeForBrowser(affected_browser);
  }

  for (Observer& observer : observers_) {
    observer.OnSpaceProfileBridgeChanged();
  }
  return true;
}

bool MahoSpaceProfileBridge::HydrateFromCore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }

  char* spaces_json_cstr = maho_core_get_space_view_models(core);
  char* active_space_id_json_cstr = maho_core_get_active_space_id(core);
  if (!spaces_json_cstr || !active_space_id_json_cstr) {
    maho_string_free(spaces_json_cstr);
    maho_string_free(active_space_id_json_cstr);
    return false;
  }

  std::string spaces_json(spaces_json_cstr);
  std::string active_space_id_json(active_space_id_json_cstr);
  maho_string_free(spaces_json_cstr);
  maho_string_free(active_space_id_json_cstr);
  return HydrateFromSerializedStateForTesting(spaces_json,
                                              active_space_id_json);
}

bool MahoSpaceProfileBridge::HydrateFromSerializedStateForTesting(
    std::string_view spaces_json,
    std::string_view active_space_id_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<SpaceProfileHydrationState> state =
      ParseSpaceProfileHydrationState(spaces_json, active_space_id_json);
  if (!state) {
    return false;
  }

  SpaceProfileHydrationChanges changes = MergeSpaceProfileHydrationState(
      std::move(*state), &space_to_profile_, &active_space_id_);
  if (changes.structural || changes.active_space) {
    NotifyChanged(changes.structural);
  }
  return true;
}

std::vector<Browser*> MahoSpaceProfileBridge::GetBrowsersForSpaces(
    const std::vector<std::string>& space_ids) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<Browser*> browsers;
  for (const auto& entry : browser_active_space_) {
    Browser* browser = entry.first;
    if (!browser || !browser->profile() || browser->profile()->IsOffTheRecord()) {
      continue;
    }
    const std::string& active_space_id = GetActiveSpaceId(browser);
    if (std::find(space_ids.begin(), space_ids.end(), active_space_id) !=
        space_ids.end()) {
      browsers.push_back(browser);
    }
  }
  return browsers;
}

void MahoSpaceProfileBridge::ClearBrowserActiveSpacesForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  browser_active_space_.clear();
}

void MahoSpaceProfileBridge::EnsureBrowserCollectionObservationForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_collection_observation_.IsObserving()) {
    browser_collection_observation_.Observe(
        GlobalBrowserCollection::GetInstance());
  }
}

void MahoSpaceProfileBridge::ResetBrowserCollectionObservationForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  browser_collection_observation_.Reset();
}

void MahoSpaceProfileBridge::OnBrowserCreated(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return;
  }
  Browser* browser_ptr = browser->GetBrowserForMigrationOnly();
  if (!browser_ptr || active_space_id_.empty()) {
    return;
  }
  auto it = space_to_profile_.find(active_space_id_);
  if (it != space_to_profile_.end()) {
    const base::FilePath browser_profile_basename =
        browser_ptr->profile()->GetPath().BaseName();
    if (browser_profile_basename != it->second) {
      return;
    }
  }
  browser_active_space_.emplace(browser_ptr, active_space_id_);
}

void MahoSpaceProfileBridge::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  Browser* browser_ptr = browser->GetBrowserForMigrationOnly();
  ClearBrowserActiveSpace(browser_ptr);
}

}  // namespace maho
