// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_SPACE_PROFILE_HYDRATION_H_
#define MAHO_BROWSER_MAHO_SPACE_PROFILE_HYDRATION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"

namespace maho {

struct SpaceProfileHydrationState {
  SpaceProfileHydrationState();
  SpaceProfileHydrationState(const SpaceProfileHydrationState&);
  SpaceProfileHydrationState& operator=(const SpaceProfileHydrationState&);
  SpaceProfileHydrationState(SpaceProfileHydrationState&&) noexcept;
  SpaceProfileHydrationState& operator=(
      SpaceProfileHydrationState&&) noexcept;
  ~SpaceProfileHydrationState();

  base::flat_map<std::string, base::FilePath> space_to_profile;
  std::string active_space_id;
};

struct SpaceProfileHydrationChanges {
  bool structural = false;
  bool active_space = false;
};

enum class ProfileLifecycleState {
  kReady,
  kProvisioning,
  kDeleting,
  kRepairRequired,
};

struct ProfileRegistryRecord {
  ProfileRegistryRecord();
  ProfileRegistryRecord(const ProfileRegistryRecord&);
  ProfileRegistryRecord& operator=(const ProfileRegistryRecord&);
  ProfileRegistryRecord(ProfileRegistryRecord&&) noexcept;
  ProfileRegistryRecord& operator=(ProfileRegistryRecord&&) noexcept;
  ~ProfileRegistryRecord();

  std::string maho_id;
  std::string name;
  std::string avatar_color;
  std::optional<int32_t> archive_timeout_hours;
  base::FilePath chromium_basename;
  bool is_default = false;
  bool is_active = false;
  uint64_t revision = 0;
  ProfileLifecycleState lifecycle = ProfileLifecycleState::kReady;
  std::vector<std::string> space_ids;
};

enum class ProfileCatalogError {
  kNone,
  kCoreUnavailable,
  kMalformedProfilesJson,
  kMalformedActiveIdJson,
  kMalformedSpacesJson,
  kInvalidProfileRecord,
  kInvalidProfileId,
  kInvalidDefaultMetadata,
  kUnknownActiveProfile,
};

struct ProfileCatalogResult {
  ProfileCatalogResult();
  ProfileCatalogResult(const ProfileCatalogResult&);
  ProfileCatalogResult& operator=(const ProfileCatalogResult&);
  ProfileCatalogResult(ProfileCatalogResult&&) noexcept;
  ProfileCatalogResult& operator=(ProfileCatalogResult&&) noexcept;
  ~ProfileCatalogResult();

  ProfileCatalogError error = ProfileCatalogError::kNone;
  uint64_t revision = 0;
  std::vector<ProfileRegistryRecord> records;
};

enum class ProfileRegistryLookupError {
  kNone,
  kCatalogUnavailable,
  kInvalidProfileId,
  kUnknownProfile,
  kStaleRevision,
  kProfileDeleting,
  kProfileProvisioning,
  kProfileRepairRequired,
};

struct ProfileRegistryPathResult {
  ProfileRegistryPathResult();
  ProfileRegistryPathResult(const ProfileRegistryPathResult&);
  ProfileRegistryPathResult& operator=(const ProfileRegistryPathResult&);
  ProfileRegistryPathResult(ProfileRegistryPathResult&&) noexcept;
  ProfileRegistryPathResult& operator=(ProfileRegistryPathResult&&) noexcept;
  ~ProfileRegistryPathResult();

  ProfileRegistryLookupError error = ProfileRegistryLookupError::kNone;
  std::optional<base::FilePath> path;
};

ProfileCatalogResult ParseProfileCatalog(std::string_view profiles_json,
                                         std::string_view active_id_json,
                                         std::string_view spaces_json,
                                         uint64_t revision);

std::string_view ProfileLifecycleStateToString(ProfileLifecycleState state);
std::optional<ProfileLifecycleState> ProfileLifecycleStateFromString(
    std::string_view state);

std::optional<std::string> CanonicalizeProfileRegistryId(
    const ProfileCatalogResult& catalog,
    std::string_view profile_id);

ProfileRegistryPathResult ResolveProfileRegistryPath(
    const ProfileCatalogResult& catalog,
    std::string_view profile_id,
    uint64_t expected_revision);

std::optional<SpaceProfileHydrationState>
BuildSpaceProfileHydrationStateFromCatalog(
    const ProfileCatalogResult& catalog,
    std::string_view active_space_id_json);

std::optional<SpaceProfileHydrationState> ParseSpaceProfileHydrationState(
    std::string_view spaces_json,
    std::string_view active_space_id_json);

SpaceProfileHydrationChanges MergeSpaceProfileHydrationState(
    SpaceProfileHydrationState state,
    base::flat_map<std::string, base::FilePath>* space_to_profile,
    std::string* active_space_id);

SpaceProfileHydrationChanges ReplaceSpaceProfileHydrationState(
    SpaceProfileHydrationState state,
    base::flat_map<std::string, base::FilePath>* space_to_profile,
    std::string* active_space_id);

std::optional<base::FilePath> ProfileBasenameForId(
    std::string_view profile_id);

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_SPACE_PROFILE_HYDRATION_H_
