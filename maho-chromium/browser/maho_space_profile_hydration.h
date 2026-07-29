// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_SPACE_PROFILE_HYDRATION_H_
#define MAHO_BROWSER_MAHO_SPACE_PROFILE_HYDRATION_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"

namespace maho {

struct SpaceProfileHydrationState {
  base::flat_map<std::string, base::FilePath> space_to_profile;
  std::string active_space_id;
};

struct SpaceProfileHydrationChanges {
  bool structural = false;
  bool active_space = false;
};

std::optional<SpaceProfileHydrationState> ParseSpaceProfileHydrationState(
    std::string_view spaces_json,
    std::string_view active_space_id_json);

SpaceProfileHydrationChanges MergeSpaceProfileHydrationState(
    SpaceProfileHydrationState state,
    base::flat_map<std::string, base::FilePath>* space_to_profile,
    std::string* active_space_id);

std::optional<base::FilePath> ProfileBasenameForId(
    std::string_view profile_id);

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_SPACE_PROFILE_HYDRATION_H_
