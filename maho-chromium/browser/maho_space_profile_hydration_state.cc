// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_hydration.h"

#include <utility>

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

std::optional<std::string> ParseActiveSpace(
    std::string_view active_space_id_json) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      std::string(active_space_id_json), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_string()) {
    return std::nullopt;
  }
  return parsed->GetString();
}

}  // namespace

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
  return SpaceProfileHydrationState{std::move(*spaces),
                                    std::move(*active_space)};
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

}  // namespace maho
