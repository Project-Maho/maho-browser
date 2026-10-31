// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_bridge.h"

#include "base/containers/flat_set.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_hydration.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {
namespace {

constexpr char kProfileRegistryPref[] = "maho.profile_registry";
constexpr char kProfileRegistryRevisionKey[] = "revision";
constexpr char kProfileRegistryRecordsKey[] = "records";
constexpr char kProfileRegistryIdKey[] = "maho_id";
constexpr char kProfileRegistryNameKey[] = "name";
constexpr char kProfileRegistryAvatarColorKey[] = "avatar_color";
constexpr char kProfileRegistryArchiveTimeoutKey[] = "archive_timeout_hours";
constexpr char kProfileRegistryBasenameKey[] = "chromium_basename";
constexpr char kProfileRegistryDefaultKey[] = "is_default";
constexpr char kProfileRegistryActiveKey[] = "is_active";
constexpr char kProfileRegistryLifecycleKey[] = "lifecycle";
constexpr char kProfileRegistrySpaceIdsKey[] = "space_ids";

}  // namespace

void MahoSpaceProfileBridge::Observer::OnSpaceProfileBridgeChanged(
    bool is_structural) {
  OnSpaceProfileBridgeChanged();
}

MahoSpaceProfileBridge* MahoSpaceProfileBridge::GetInstance() {
  static base::NoDestructor<MahoSpaceProfileBridge> instance;
  return instance.get();
}

// static
void MahoSpaceProfileBridge::RegisterLocalStatePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kProfileRegistryPref);
}

MahoSpaceProfileBridge::MahoSpaceProfileBridge() {
  LoadProfileRegistryFromLocalState();
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
  ReconcileExistingBrowsersActiveSpace();
  if (auto* tab_registry = MahoTabRegistry::Get()) {
    tab_registry->ReannounceUnannouncedTabs();
  }
}

void MahoSpaceProfileBridge::SetActiveSpaceId(Browser* browser,
                                              const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (browser) {
    const auto space_it = space_to_profile_.find(space_id);
    if (space_it == space_to_profile_.end() || !browser->GetProfile() ||
        browser->GetProfile()->IsOffTheRecord() ||
        browser->GetProfile()->GetPath().BaseName() != space_it->second) {
      browser_active_space_.erase(browser);
    } else {
      browser_active_space_[browser] = space_id;
    }
  } else {
    active_space_id_ = space_id;
    ReconcileExistingBrowsersActiveSpace();
  }
  if (auto* tab_registry = MahoTabRegistry::Get()) {
    tab_registry->ReannounceUnannouncedTabs();
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
  Browser* browser_ptr = static_cast<Browser*>(browser);
  return browser_ptr ? GetActiveSpaceId(browser_ptr) : base::EmptyString();
}

bool MahoSpaceProfileBridge::IsSpaceRegistered(
    std::string_view space_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return space_to_profile_.find(std::string(space_id)) != space_to_profile_.end();
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

bool MahoSpaceProfileBridge::ReconcileProfileRegistry(
    std::string_view profiles_json,
    std::string_view active_profile_id_json,
    std::string_view spaces_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t next_revision = profile_catalog_.revision + 1;
  ProfileCatalogResult next = ParseProfileCatalog(
      profiles_json, active_profile_id_json, spaces_json, next_revision);
  return ReconcileProfileRegistry(std::move(next));
}

bool MahoSpaceProfileBridge::ReconcileProfileRegistry(
    ProfileCatalogResult next) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (next.error != ProfileCatalogError::kNone) {
    return false;
  }
  next.revision = profile_catalog_.revision + 1;
  for (ProfileRegistryRecord& record : next.records) {
    record.revision = next.revision;
    for (const ProfileRegistryRecord& existing : profile_catalog_.records) {
      if (existing.maho_id != record.maho_id) {
        continue;
      }
      record.lifecycle = existing.lifecycle;
      if (record.lifecycle == ProfileLifecycleState::kProvisioning ||
          record.lifecycle == ProfileLifecycleState::kDeleting) {
        record.lifecycle = ProfileLifecycleState::kRepairRequired;
      }
      break;
    }
  }
  profile_catalog_ = std::move(next);
  PersistProfileRegistryToLocalState();
  return true;
}

bool MahoSpaceProfileBridge::ReconcileProfileRegistryFromCore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  char* profiles = maho_core_list_profiles(core);
  char* active_id = maho_core_get_active_profile_id(core);
  char* spaces = maho_core_get_space_view_models(core);
  if (!profiles || !active_id || !spaces) {
    maho_string_free(profiles);
    maho_string_free(active_id);
    maho_string_free(spaces);
    return false;
  }
  const bool success = ReconcileProfileRegistry(profiles, active_id, spaces);
  maho_string_free(profiles);
  maho_string_free(active_id);
  maho_string_free(spaces);
  return success;
}

void MahoSpaceProfileBridge::SetLocalStateForTesting(PrefService* local_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  local_state_for_testing_ = local_state;
  LoadProfileRegistryFromLocalState();
}

const ProfileCatalogResult& MahoSpaceProfileBridge::GetProfileCatalog() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return profile_catalog_;
}

uint64_t MahoSpaceProfileBridge::GetProfileRegistryRevision() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return profile_catalog_.revision;
}

std::optional<std::string> MahoSpaceProfileBridge::CanonicalizeProfileId(
    std::string_view profile_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return CanonicalizeProfileRegistryId(profile_catalog_, profile_id);
}

ProfileRegistryPathResult MahoSpaceProfileBridge::ResolveProfilePath(
    std::string_view profile_id,
    uint64_t expected_registry_revision) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ResolveProfileRegistryPath(profile_catalog_, profile_id,
                                    expected_registry_revision);
}

bool MahoSpaceProfileBridge::UpdateProfileSettings(
    std::string_view profile_id,
    const std::string* name,
    const std::string* avatar_color,
    const std::optional<int32_t>* archive_timeout_hours) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<std::string> canonical_id = CanonicalizeProfileId(profile_id);
  if (!canonical_id) {
    return false;
  }
  for (ProfileRegistryRecord& record : profile_catalog_.records) {
    if (record.maho_id != *canonical_id) {
      continue;
    }
    if (name) {
      record.name = *name;
    }
    if (avatar_color) {
      record.avatar_color = *avatar_color;
    }
    if (archive_timeout_hours) {
      record.archive_timeout_hours = *archive_timeout_hours;
    }
    record.revision = ++profile_catalog_.revision;
    PersistProfileRegistryToLocalState();
    return true;
  }
  return false;
}

bool MahoSpaceProfileBridge::SetProfileLifecycleState(
    std::string_view profile_id,
    ProfileLifecycleState state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<std::string> canonical_id = CanonicalizeProfileId(profile_id);
  if (!canonical_id) {
    return false;
  }
  for (ProfileRegistryRecord& record : profile_catalog_.records) {
    if (record.maho_id == *canonical_id) {
      if (record.lifecycle == state) {
        return true;
      }
      record.lifecycle = state;
      record.revision = ++profile_catalog_.revision;
      PersistProfileRegistryToLocalState();
      return true;
    }
  }
  return false;
}

PrefService* MahoSpaceProfileBridge::GetLocalState() const {
  if (local_state_for_testing_) {
    return local_state_for_testing_;
  }
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

void MahoSpaceProfileBridge::LoadProfileRegistryFromLocalState() {
  PrefService* local_state = GetLocalState();
  if (!local_state || !local_state->FindPreference(kProfileRegistryPref)) {
    return;
  }
  const base::DictValue& persisted =
      local_state->GetDict(kProfileRegistryPref);
  std::optional<int> revision =
      persisted.FindInt(kProfileRegistryRevisionKey);
  const base::ListValue* records = persisted.FindList(kProfileRegistryRecordsKey);
  if (!revision || *revision < 0 || !records) {
    return;
  }

  ProfileCatalogResult loaded;
  loaded.revision = static_cast<uint64_t>(*revision);
  base::flat_set<std::string> loaded_ids;
  size_t default_count = 0;
  size_t active_count = 0;
  for (const base::Value& value : *records) {
    const base::DictValue* dict = value.GetIfDict();
    const std::string* id = dict ? dict->FindString(kProfileRegistryIdKey) : nullptr;
    const std::string* name =
        dict ? dict->FindString(kProfileRegistryNameKey) : nullptr;
    const std::string* avatar_color =
        dict ? dict->FindString(kProfileRegistryAvatarColorKey) : nullptr;
    const base::Value* archive_timeout =
        dict ? dict->Find(kProfileRegistryArchiveTimeoutKey) : nullptr;
    const std::string* basename =
        dict ? dict->FindString(kProfileRegistryBasenameKey) : nullptr;
    const std::string* lifecycle =
        dict ? dict->FindString(kProfileRegistryLifecycleKey) : nullptr;
    std::optional<bool> is_default =
        dict ? dict->FindBool(kProfileRegistryDefaultKey) : std::nullopt;
    std::optional<bool> is_active =
        dict ? dict->FindBool(kProfileRegistryActiveKey) : std::nullopt;
    std::optional<ProfileLifecycleState> parsed_lifecycle =
        lifecycle ? ProfileLifecycleStateFromString(*lifecycle) : std::nullopt;
    if (!id || id->empty() || !name || !basename || !is_default || !is_active ||
        !parsed_lifecycle || !loaded_ids.insert(*id).second) {
      return;
    }
    ProfileRegistryRecord record;
    record.maho_id = *id;
    record.name = *name;
    record.avatar_color = avatar_color ? *avatar_color : "#007AFF";
    if (archive_timeout && !archive_timeout->is_none()) {
      std::optional<int> timeout = archive_timeout->GetIfInt();
      if (!timeout || *timeout < 0) {
        return;
      }
      record.archive_timeout_hours = *timeout;
    }
    record.chromium_basename = base::FilePath::FromUTF8Unsafe(*basename);
    std::optional<base::FilePath> expected_basename =
        *is_default ? std::optional<base::FilePath>(
                          base::FilePath::FromASCII("Default"))
                    : ProfileBasenameForId(*id);
    if (!expected_basename || record.chromium_basename != *expected_basename) {
      return;
    }
    record.is_default = *is_default;
    record.is_active = *is_active;
    default_count += record.is_default ? 1u : 0u;
    active_count += record.is_active ? 1u : 0u;
    record.revision = loaded.revision;
    record.lifecycle = *parsed_lifecycle;
    if (const base::ListValue* space_ids =
            dict->FindList(kProfileRegistrySpaceIdsKey)) {
      for (const base::Value& space_id : *space_ids) {
        if (space_id.is_string()) {
          record.space_ids.push_back(space_id.GetString());
        }
      }
    }
    loaded.records.push_back(std::move(record));
  }
  if (loaded.records.empty() || default_count != 1u || active_count != 1u) {
    return;
  }
  profile_catalog_ = std::move(loaded);
}

void MahoSpaceProfileBridge::PersistProfileRegistryToLocalState() {
  PrefService* local_state = GetLocalState();
  if (!local_state || !local_state->FindPreference(kProfileRegistryPref)) {
    return;
  }
  base::DictValue persisted;
  persisted.Set(kProfileRegistryRevisionKey,
                static_cast<int>(profile_catalog_.revision));
  base::ListValue records;
  for (const ProfileRegistryRecord& record : profile_catalog_.records) {
    base::DictValue dict;
    dict.Set(kProfileRegistryIdKey, record.maho_id);
    dict.Set(kProfileRegistryNameKey, record.name);
    dict.Set(kProfileRegistryAvatarColorKey, record.avatar_color);
    if (record.archive_timeout_hours) {
      dict.Set(kProfileRegistryArchiveTimeoutKey,
               *record.archive_timeout_hours);
    }
    dict.Set(kProfileRegistryBasenameKey,
             record.chromium_basename.AsUTF8Unsafe());
    dict.Set(kProfileRegistryDefaultKey, record.is_default);
    dict.Set(kProfileRegistryActiveKey, record.is_active);
    dict.Set(kProfileRegistryLifecycleKey,
             ProfileLifecycleStateToString(record.lifecycle));
    base::ListValue space_ids;
    for (const std::string& space_id : record.space_ids) {
      space_ids.Append(space_id);
    }
    dict.Set(kProfileRegistrySpaceIdsKey, std::move(space_ids));
    records.Append(std::move(dict));
  }
  persisted.Set(kProfileRegistryRecordsKey, std::move(records));
  local_state->SetDict(kProfileRegistryPref, std::move(persisted));
}

// static
std::optional<base::FilePath> MahoSpaceProfileBridge::ProfileBasenameForId(
    std::string_view profile_id) {
  return maho::ProfileBasenameForId(profile_id);
}

}  // namespace maho
