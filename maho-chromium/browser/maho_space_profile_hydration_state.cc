// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_hydration.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/json/json_reader.h"

namespace maho {
namespace {

constexpr size_t kProfileIdMaxLen = 64;

bool IsAsciiWhitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\v' || value == '\f';
}

bool EqualsCaseInsensitive(std::string_view left, std::string_view right) {
  if (left.length() != right.length()) {
    return false;
  }
  for (size_t index = 0; index < left.length(); ++index) {
    char left_value = left[index];
    char right_value = right[index];
    if (left_value >= 'A' && left_value <= 'Z') {
      left_value += 32;
    }
    if (right_value >= 'A' && right_value <= 'Z') {
      right_value += 32;
    }
    if (left_value != right_value) {
      return false;
    }
  }
  return true;
}

std::optional<base::flat_map<std::string, base::FilePath>> ParseSpaces(
    std::string_view spaces_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(std::string(spaces_json), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  base::flat_map<std::string, base::FilePath> spaces;
  for (const base::Value& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* space_id = dict->FindString("id");
    if (!space_id || space_id->empty()) {
      continue;
    }
    const std::string* profile_id = dict->FindString("profileId");
    std::optional<base::FilePath> profile_path = ProfileBasenameForId(
        profile_id ? *profile_id : std::string_view());
    if (profile_path) {
      spaces[*space_id] = std::move(*profile_path);
    }
  }
  return spaces;
}

std::optional<std::string> ParseJsonString(std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(std::string(json), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_string()) {
    return std::nullopt;
  }
  return parsed->GetString();
}

std::optional<std::string> ParseActiveSpace(
    std::string_view active_space_id_json) {
  return ParseJsonString(active_space_id_json);
}

const std::string* FindCompatibleProfileId(const base::DictValue& dict) {
  const std::string* profile_id = dict.FindString("profileId");
  return profile_id ? profile_id : dict.FindString("profile_id");
}

const base::ListValue* FindCompatibleSpaceIds(const base::DictValue& dict) {
  const base::ListValue* space_ids = dict.FindList("spaceIds");
  return space_ids ? space_ids : dict.FindList("space_ids");
}

}  // namespace

SpaceProfileHydrationState::SpaceProfileHydrationState() = default;
SpaceProfileHydrationState::SpaceProfileHydrationState(
    const SpaceProfileHydrationState&) = default;
SpaceProfileHydrationState& SpaceProfileHydrationState::operator=(
    const SpaceProfileHydrationState&) = default;
SpaceProfileHydrationState::SpaceProfileHydrationState(
    SpaceProfileHydrationState&&) noexcept = default;
SpaceProfileHydrationState& SpaceProfileHydrationState::operator=(
    SpaceProfileHydrationState&&) noexcept = default;
SpaceProfileHydrationState::~SpaceProfileHydrationState() = default;

ProfileRegistryRecord::ProfileRegistryRecord() = default;
ProfileRegistryRecord::ProfileRegistryRecord(
    const ProfileRegistryRecord&) = default;
ProfileRegistryRecord& ProfileRegistryRecord::operator=(
    const ProfileRegistryRecord&) = default;
ProfileRegistryRecord::ProfileRegistryRecord(
    ProfileRegistryRecord&&) noexcept = default;
ProfileRegistryRecord& ProfileRegistryRecord::operator=(
    ProfileRegistryRecord&&) noexcept = default;
ProfileRegistryRecord::~ProfileRegistryRecord() = default;

ProfileCatalogResult::ProfileCatalogResult() = default;
ProfileCatalogResult::ProfileCatalogResult(const ProfileCatalogResult&) =
    default;
ProfileCatalogResult& ProfileCatalogResult::operator=(
    const ProfileCatalogResult&) = default;
ProfileCatalogResult::ProfileCatalogResult(ProfileCatalogResult&&) noexcept =
    default;
ProfileCatalogResult& ProfileCatalogResult::operator=(
    ProfileCatalogResult&&) noexcept = default;
ProfileCatalogResult::~ProfileCatalogResult() = default;

ProfileRegistryPathResult::ProfileRegistryPathResult() = default;
ProfileRegistryPathResult::ProfileRegistryPathResult(
    const ProfileRegistryPathResult&) = default;
ProfileRegistryPathResult& ProfileRegistryPathResult::operator=(
    const ProfileRegistryPathResult&) = default;
ProfileRegistryPathResult::ProfileRegistryPathResult(
    ProfileRegistryPathResult&&) noexcept = default;
ProfileRegistryPathResult& ProfileRegistryPathResult::operator=(
    ProfileRegistryPathResult&&) noexcept = default;
ProfileRegistryPathResult::~ProfileRegistryPathResult() = default;

static ProfileRegistryPathResult MakePathResult(
    ProfileRegistryLookupError error) {
  ProfileRegistryPathResult result;
  result.error = error;
  return result;
}

static ProfileRegistryPathResult MakePathResult(base::FilePath path) {
  ProfileRegistryPathResult result;
  result.path = std::move(path);
  return result;
}

std::optional<base::FilePath> ProfileBasenameForId(
    std::string_view profile_id) {
  while (!profile_id.empty() && IsAsciiWhitespace(profile_id.front())) {
    profile_id.remove_prefix(1);
  }
  while (!profile_id.empty() && IsAsciiWhitespace(profile_id.back())) {
    profile_id.remove_suffix(1);
  }

  if (profile_id.empty() || EqualsCaseInsensitive(profile_id, "default")) {
    return base::FilePath::FromASCII("Default");
  }
  if (profile_id.length() > kProfileIdMaxLen || profile_id.front() == '.' ||
      profile_id.find("..") != std::string_view::npos) {
    return std::nullopt;
  }
  for (char value : profile_id) {
    if (value < 32 || value > 126 || value == '/' || value == '\\' ||
        value == ':') {
      return std::nullopt;
    }
  }
  return base::FilePath::FromASCII("MahoProfile_" + std::string(profile_id));
}

std::string_view ProfileLifecycleStateToString(ProfileLifecycleState state) {
  switch (state) {
    case ProfileLifecycleState::kReady:
      return "ready";
    case ProfileLifecycleState::kProvisioning:
      return "provisioning";
    case ProfileLifecycleState::kDeleting:
      return "deleting";
    case ProfileLifecycleState::kRepairRequired:
      return "repair_required";
  }
}

std::optional<ProfileLifecycleState> ProfileLifecycleStateFromString(
    std::string_view state) {
  if (state == "ready") {
    return ProfileLifecycleState::kReady;
  }
  if (state == "provisioning") {
    return ProfileLifecycleState::kProvisioning;
  }
  if (state == "deleting") {
    return ProfileLifecycleState::kDeleting;
  }
  if (state == "repair_required") {
    return ProfileLifecycleState::kRepairRequired;
  }
  return std::nullopt;
}

ProfileCatalogResult ParseProfileCatalog(std::string_view profiles_json,
                                         std::string_view active_id_json,
                                         std::string_view spaces_json,
                                         uint64_t revision) {
  ProfileCatalogResult result;
  result.revision = revision;

  std::optional<base::Value> profiles =
      base::JSONReader::Read(std::string(profiles_json), base::JSON_PARSE_RFC);
  if (!profiles || !profiles->is_list()) {
    result.error = ProfileCatalogError::kMalformedProfilesJson;
    return result;
  }
  std::optional<std::string> active_id = ParseJsonString(active_id_json);
  if (!active_id) {
    result.error = ProfileCatalogError::kMalformedActiveIdJson;
    return result;
  }
  std::optional<base::Value> spaces =
      base::JSONReader::Read(std::string(spaces_json), base::JSON_PARSE_RFC);
  if (!spaces || !spaces->is_list()) {
    result.error = ProfileCatalogError::kMalformedSpacesJson;
    return result;
  }

  base::flat_map<std::string, std::vector<std::string>> spaces_by_profile;
  for (const base::Value& item : spaces->GetList()) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* space_id = dict->FindString("id");
    const std::string* profile_id = FindCompatibleProfileId(*dict);
    if (space_id && !space_id->empty()) {
      spaces_by_profile[profile_id && !profile_id->empty() ? *profile_id
                                                           : "default"]
          .push_back(*space_id);
    }
  }

  base::flat_set<std::string> known_ids;
  bool found_active = false;
  size_t default_count = 0;
  for (const base::Value& item : profiles->GetList()) {
    const base::DictValue* dict = item.GetIfDict();
    const std::string* id = dict ? dict->FindString("id") : nullptr;
    const std::string* name = dict ? dict->FindString("name") : nullptr;
    const std::string* avatar_color =
        dict ? dict->FindString("avatarColor") : nullptr;
    if (!avatar_color && dict) {
      avatar_color = dict->FindString("avatar_color");
    }
    if (!dict || !id || id->empty() || !name || !known_ids.insert(*id).second) {
      result.records.clear();
      result.error = ProfileCatalogError::kInvalidProfileRecord;
      return result;
    }

    std::optional<base::FilePath> derived_basename = ProfileBasenameForId(*id);
    if (!derived_basename ||
        *derived_basename == base::FilePath::FromASCII("Default")) {
      result.records.clear();
      result.error = ProfileCatalogError::kInvalidProfileId;
      return result;
    }

    const base::Value* data_store_id = dict->Find("dataStoreId");
    if (!data_store_id) {
      data_store_id = dict->Find("data_store_id");
    }
    if (data_store_id && !data_store_id->is_none() &&
        !data_store_id->is_string()) {
      result.records.clear();
      result.error = ProfileCatalogError::kInvalidDefaultMetadata;
      return result;
    }
    const bool is_default = !data_store_id || data_store_id->is_none();
    default_count += is_default ? 1u : 0u;

    base::FilePath basename =
        is_default ? base::FilePath::FromASCII("Default")
                   : std::move(*derived_basename);

    ProfileRegistryRecord record;
    record.maho_id = *id;
    record.name = *name;
    record.avatar_color = avatar_color ? *avatar_color : "#007AFF";
    const base::Value* archive_timeout = dict->Find("archiveTimeoutHours");
    if (!archive_timeout) {
      archive_timeout = dict->Find("archive_timeout_hours");
    }
    if (archive_timeout && !archive_timeout->is_none()) {
      std::optional<double> timeout = archive_timeout->GetIfDouble();
      if (!timeout || *timeout < 0 || *timeout > std::numeric_limits<int32_t>::max() ||
          *timeout != static_cast<int32_t>(*timeout)) {
        result.records.clear();
        result.error = ProfileCatalogError::kInvalidProfileRecord;
        return result;
      }
      record.archive_timeout_hours = static_cast<int32_t>(*timeout);
    }
    record.chromium_basename = std::move(basename);
    record.is_default = is_default;
    record.is_active = *id == *active_id;
    record.revision = revision;
    if (const base::ListValue* profile_space_ids =
            FindCompatibleSpaceIds(*dict)) {
      for (const base::Value& space_id : *profile_space_ids) {
        if (space_id.is_string()) {
          record.space_ids.push_back(space_id.GetString());
        }
      }
    }
    // Merge in spaces the space catalog attributes to this profile, plus (for
    // the default profile) legacy spaces whose profileId is empty and were
    // grouped under the "default" bucket. These sources are additive, not
    // either/or: the default profile owns both its id-tagged spaces and the
    // seed space created with an empty ProfileId. Treating them as either/or
    // (the prior behavior) orphaned that seed space, so SwitchToSpace rejected
    // it as an unknown space_id and it could never be activated.
    base::flat_set<std::string> seen_space_ids(record.space_ids.begin(),
                                               record.space_ids.end());
    auto merge_space_bucket = [&](const std::string& bucket_key) {
      auto bucket_it = spaces_by_profile.find(bucket_key);
      if (bucket_it == spaces_by_profile.end()) {
        return;
      }
      for (const std::string& space_id : bucket_it->second) {
        if (seen_space_ids.insert(space_id).second) {
          record.space_ids.push_back(space_id);
        }
      }
    };
    merge_space_bucket(*id);
    if (is_default) {
      merge_space_bucket("default");
    }
    found_active = found_active || record.is_active;
    result.records.push_back(std::move(record));
  }

  for (const auto& [profile_id, space_ids] : spaces_by_profile) {
    if (!known_ids.contains(profile_id) &&
        !EqualsCaseInsensitive(profile_id, "default")) {
      result.records.clear();
      result.error = ProfileCatalogError::kInvalidProfileRecord;
      return result;
    }
  }
  if (default_count != 1u) {
    result.records.clear();
    result.error = ProfileCatalogError::kInvalidDefaultMetadata;
    return result;
  }
  if (!found_active) {
    result.records.clear();
    result.error = ProfileCatalogError::kUnknownActiveProfile;
    return result;
  }
  return result;
}

std::optional<std::string> CanonicalizeProfileRegistryId(
    const ProfileCatalogResult& catalog,
    std::string_view profile_id) {
  if (catalog.error != ProfileCatalogError::kNone) {
    return std::nullopt;
  }
  while (!profile_id.empty() && IsAsciiWhitespace(profile_id.front())) {
    profile_id.remove_prefix(1);
  }
  while (!profile_id.empty() && IsAsciiWhitespace(profile_id.back())) {
    profile_id.remove_suffix(1);
  }
  if (profile_id.empty() || EqualsCaseInsensitive(profile_id, "default")) {
    for (const ProfileRegistryRecord& record : catalog.records) {
      if (record.is_default) {
        return record.maho_id;
      }
    }
    return std::nullopt;
  }
  for (const ProfileRegistryRecord& record : catalog.records) {
    if (record.maho_id == profile_id) {
      return record.maho_id;
    }
  }
  return std::nullopt;
}

ProfileRegistryPathResult ResolveProfileRegistryPath(
    const ProfileCatalogResult& catalog,
    std::string_view profile_id,
  uint64_t expected_revision) {
  if (catalog.error != ProfileCatalogError::kNone) {
    return MakePathResult(ProfileRegistryLookupError::kCatalogUnavailable);
  }
  if (catalog.revision != expected_revision) {
    return MakePathResult(ProfileRegistryLookupError::kStaleRevision);
  }
  std::optional<base::FilePath> validated_id = ProfileBasenameForId(profile_id);
  if (!validated_id ||
      (*validated_id == base::FilePath::FromASCII("Default") &&
       !CanonicalizeProfileRegistryId(catalog, profile_id))) {
    return MakePathResult(ProfileRegistryLookupError::kInvalidProfileId);
  }
  std::optional<std::string> canonical_id =
      CanonicalizeProfileRegistryId(catalog, profile_id);
  if (!canonical_id) {
    return MakePathResult(ProfileRegistryLookupError::kUnknownProfile);
  }
  for (const ProfileRegistryRecord& record : catalog.records) {
    if (record.maho_id != *canonical_id) {
      continue;
    }
    switch (record.lifecycle) {
      case ProfileLifecycleState::kReady:
        return MakePathResult(record.chromium_basename);
      case ProfileLifecycleState::kProvisioning:
        return MakePathResult(
            ProfileRegistryLookupError::kProfileProvisioning);
      case ProfileLifecycleState::kDeleting:
        return MakePathResult(ProfileRegistryLookupError::kProfileDeleting);
      case ProfileLifecycleState::kRepairRequired:
        return MakePathResult(
            ProfileRegistryLookupError::kProfileRepairRequired);
    }
  }
  return MakePathResult(ProfileRegistryLookupError::kUnknownProfile);
}

std::optional<SpaceProfileHydrationState>
BuildSpaceProfileHydrationStateFromCatalog(
    const ProfileCatalogResult& catalog,
    std::string_view active_space_id_json) {
  if (catalog.error != ProfileCatalogError::kNone) {
    return std::nullopt;
  }
  std::optional<std::string> active_space =
      ParseActiveSpace(active_space_id_json);
  if (!active_space) {
    return std::nullopt;
  }
  SpaceProfileHydrationState state;
  state.active_space_id = std::move(*active_space);
  for (const ProfileRegistryRecord& record : catalog.records) {
    if (record.lifecycle != ProfileLifecycleState::kReady) {
      continue;
    }
    for (const std::string& space_id : record.space_ids) {
      if (!space_id.empty()) {
        state.space_to_profile[space_id] = record.chromium_basename;
      }
    }
  }
  return state;
}

std::optional<SpaceProfileHydrationState> ParseSpaceProfileHydrationState(
    std::string_view spaces_json,
    std::string_view active_space_id_json) {
  std::optional<base::flat_map<std::string, base::FilePath>> spaces =
      ParseSpaces(spaces_json);
  std::optional<std::string> active_space =
      ParseActiveSpace(active_space_id_json);
  if (!spaces || !active_space) {
    return std::nullopt;
  }
  SpaceProfileHydrationState state;
  state.space_to_profile = std::move(*spaces);
  state.active_space_id = std::move(*active_space);
  return state;
}

SpaceProfileHydrationChanges MergeSpaceProfileHydrationState(
    SpaceProfileHydrationState state,
    base::flat_map<std::string, base::FilePath>* spaces,
    std::string* active_space_id) {
  SpaceProfileHydrationChanges changes;
  for (auto& [space_id, profile_path] : state.space_to_profile) {
    auto existing = spaces->find(space_id);
    if (existing == spaces->end() || existing->second != profile_path) {
      (*spaces)[space_id] = std::move(profile_path);
      changes.structural = true;
    }
  }
  changes.active_space = *active_space_id != state.active_space_id;
  if (changes.active_space) {
    *active_space_id = std::move(state.active_space_id);
  }
  return changes;
}

SpaceProfileHydrationChanges ReplaceSpaceProfileHydrationState(
    SpaceProfileHydrationState state,
    base::flat_map<std::string, base::FilePath>* spaces,
    std::string* active_space_id) {
  SpaceProfileHydrationChanges changes;
  changes.structural = *spaces != state.space_to_profile;
  if (changes.structural) {
    *spaces = std::move(state.space_to_profile);
  }
  changes.active_space = *active_space_id != state.active_space_id;
  if (changes.active_space) {
    *active_space_id = std::move(state.active_space_id);
  }
  return changes;
}

}  // namespace maho
