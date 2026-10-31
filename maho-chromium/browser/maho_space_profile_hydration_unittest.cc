// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_hydration.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/json/string_escape.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoSpaceProfileHydrationTest, ParsesAndFiltersSerializedSnapshot) {
  std::optional<SpaceProfileHydrationState> state =
      ParseSpaceProfileHydrationState(
          R"([{"id":"work","profileId":"default"},)"
          R"({"id":"personal","profileId":"alpha"},)"
          R"({"id":"invalid","profileId":"../escape"},)"
          R"({"profileId":"default"}])",
          R"("personal")");

  ASSERT_TRUE(state.has_value());
  ASSERT_EQ(state->space_to_profile.size(), 2u);
  EXPECT_EQ(state->space_to_profile.at("work"),
            base::FilePath::FromASCII("Default"));
  EXPECT_EQ(state->space_to_profile.at("personal"),
            base::FilePath::FromASCII("MahoProfile_alpha"));
  EXPECT_FALSE(state->space_to_profile.contains("invalid"));
  EXPECT_EQ(state->active_space_id, "personal");
}

TEST(MahoSpaceProfileHydrationTest, BuildsAuthoritativeProfileCatalog) {
  constexpr char kGeneratedDefaultProfileId[] =
      "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
  constexpr char kWorkProfileId[] =
      "39b56c04-70eb-49d5-ae4b-d858b18c0f71";
  const std::string profiles_json =
      R"([{"id":")" + std::string(kGeneratedDefaultProfileId) +
      R"(","name":"Default","avatarColor":"#007AFF","archiveTimeoutHours":12,"dataStoreId":null},{"id":")" +
      std::string(kWorkProfileId) +
      R"(","name":"Work","avatarColor":"#AF52DE","archiveTimeoutHours":null,"dataStoreId":"work-store"}])";
  const std::string active_id_json =
      R"(")" + std::string(kWorkProfileId) + R"(")";
  const std::string spaces_json =
      R"([{"id":"personal","profileId":")" +
      std::string(kGeneratedDefaultProfileId) +
      R"("},{"id":"work","profileId":")" +
      std::string(kWorkProfileId) + R"("}])";

  ProfileCatalogResult result =
      ParseProfileCatalog(profiles_json, active_id_json, spaces_json, 7);

  ASSERT_EQ(result.error, ProfileCatalogError::kNone);
  ASSERT_EQ(result.records.size(), 2u);
  EXPECT_EQ(result.records[0].maho_id, kGeneratedDefaultProfileId);
  EXPECT_EQ(result.records[0].chromium_basename,
            base::FilePath::FromASCII("Default"));
  EXPECT_TRUE(result.records[0].is_default);
  EXPECT_FALSE(result.records[0].is_active);
  EXPECT_EQ(result.records[0].space_ids,
            std::vector<std::string>({"personal"}));
  EXPECT_EQ(result.records[1].maho_id, kWorkProfileId);
  EXPECT_EQ(result.records[1].chromium_basename,
            base::FilePath::FromASCII("MahoProfile_" +
                                      std::string(kWorkProfileId)));
  EXPECT_EQ(result.records[1].avatar_color, "#AF52DE");
  EXPECT_FALSE(result.records[1].archive_timeout_hours.has_value());
  EXPECT_FALSE(result.records[1].is_default);
  EXPECT_TRUE(result.records[1].is_active);
  EXPECT_EQ(result.records[1].space_ids,
            std::vector<std::string>({"work"}));
  EXPECT_EQ(result.records[1].revision, 7u);
  EXPECT_EQ(result.records[1].lifecycle, ProfileLifecycleState::kReady);
}

TEST(MahoSpaceProfileHydrationTest,
     LegacyMissingProfileIdMapsToGeneratedDefaultProfile) {
  constexpr char kDefaultId[] = "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
  ProfileCatalogResult catalog = ParseProfileCatalog(
      R"([{"id":")" + std::string(kDefaultId) +
          R"(","name":"Default","dataStoreId":null}])",
      R"(")" + std::string(kDefaultId) + R"(")",
      R"([{"id":"legacy-space"}])", 2);

  ASSERT_EQ(catalog.error, ProfileCatalogError::kNone);
  ASSERT_EQ(catalog.records.size(), 1u);
  EXPECT_EQ(catalog.records[0].space_ids,
            std::vector<std::string>({"legacy-space"}));
}

TEST(MahoSpaceProfileHydrationTest, CatalogErrorsAreStructuredAndFailClosed) {
  constexpr char kDefaultId[] = "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
  const std::string valid_profiles =
      R"([{"id":")" + std::string(kDefaultId) +
      R"(","name":"Default","dataStoreId":null}])";

  EXPECT_EQ(ParseProfileCatalog("{", R"("active")", "[]", 1).error,
            ProfileCatalogError::kMalformedProfilesJson);
  EXPECT_EQ(ParseProfileCatalog(valid_profiles, "not-json", "[]", 1).error,
            ProfileCatalogError::kMalformedActiveIdJson);
  EXPECT_EQ(ParseProfileCatalog(valid_profiles, R"("missing")", "[]", 1)
                .error,
            ProfileCatalogError::kUnknownActiveProfile);
  EXPECT_EQ(ParseProfileCatalog(valid_profiles, R"(")" +
                                                    std::string(kDefaultId) +
                                                    R"(")",
                                "{", 1)
                .error,
            ProfileCatalogError::kMalformedSpacesJson);
}

TEST(MahoSpaceProfileHydrationTest,
     RegistryRejectsTraversalUnknownAndStaleBeforeReturningPath) {
  constexpr char kDefaultId[] = "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
  const std::string active_id_json =
      R"(")" + std::string(kDefaultId) + R"(")";
  for (const std::string& invalid_id :
       {"default", "../", "slash/value", "back\\slash", "colon:value"}) {
    const std::string quoted_id = base::GetQuotedJSONString(invalid_id);
    const std::string profiles_json =
        R"([{"id":)" + quoted_id +
        R"(,"name":"Bad","dataStoreId":null}])";
    EXPECT_EQ(ParseProfileCatalog(profiles_json, quoted_id, "[]", 3).error,
              ProfileCatalogError::kInvalidProfileId);
  }

  ProfileCatalogResult catalog = ParseProfileCatalog(
      R"([{"id":")" + std::string(kDefaultId) +
          R"(","name":"Default","dataStoreId":null}])",
      active_id_json, "[]", 3);
  ASSERT_EQ(catalog.error, ProfileCatalogError::kNone);
  EXPECT_EQ(ResolveProfileRegistryPath(catalog, kDefaultId, 2).error,
            ProfileRegistryLookupError::kStaleRevision);
  EXPECT_EQ(ResolveProfileRegistryPath(catalog, "unknown", 3).error,
            ProfileRegistryLookupError::kUnknownProfile);
  EXPECT_EQ(CanonicalizeProfileRegistryId(catalog, "default"), kDefaultId);
  EXPECT_EQ(CanonicalizeProfileRegistryId(catalog, "  DeFaUlT  "),
            kDefaultId);
  ProfileRegistryPathResult resolved =
      ResolveProfileRegistryPath(catalog, "default", 3);
  EXPECT_EQ(resolved.error, ProfileRegistryLookupError::kNone);
  ASSERT_TRUE(resolved.path.has_value());
  EXPECT_EQ(*resolved.path, base::FilePath::FromASCII("Default"));
}

TEST(MahoSpaceProfileHydrationTest,
     CatalogHydrationUsesOnlyReadyRegistryMembers) {
  constexpr char kDefaultId[] = "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
  constexpr char kWorkId[] = "39b56c04-70eb-49d5-ae4b-d858b18c0f71";
  ProfileCatalogResult catalog = ParseProfileCatalog(
      R"([{"id":")" + std::string(kDefaultId) +
          R"(","name":"Default","dataStoreId":null},{"id":")" +
          std::string(kWorkId) +
          R"(","name":"Work","dataStoreId":"work-store"}])",
      R"(")" + std::string(kDefaultId) + R"(")",
      R"([{"id":"personal","profileId":"default"},{"id":"work","profileId":")" +
          std::string(kWorkId) + R"("}])",
      4);
  ASSERT_EQ(catalog.error, ProfileCatalogError::kNone);
  catalog.records[1].lifecycle = ProfileLifecycleState::kRepairRequired;

  std::optional<SpaceProfileHydrationState> state =
      BuildSpaceProfileHydrationStateFromCatalog(catalog, R"("personal")");

  ASSERT_TRUE(state.has_value());
  ASSERT_EQ(state->space_to_profile.size(), 1u);
  EXPECT_EQ(state->space_to_profile.at("personal"),
            base::FilePath::FromASCII("Default"));
  EXPECT_FALSE(state->space_to_profile.contains("work"));
}

TEST(MahoSpaceProfileHydrationTest, RegistryRejectsNonReadyLifecycleStates) {
  constexpr char kDefaultId[] = "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
  ProfileCatalogResult catalog = ParseProfileCatalog(
      R"([{"id":")" + std::string(kDefaultId) +
          R"(","name":"Default","dataStoreId":null}])",
      R"(")" + std::string(kDefaultId) + R"(")", "[]", 9);
  ASSERT_EQ(catalog.error, ProfileCatalogError::kNone);

  catalog.records[0].lifecycle = ProfileLifecycleState::kDeleting;
  EXPECT_EQ(ResolveProfileRegistryPath(catalog, kDefaultId, 9).error,
            ProfileRegistryLookupError::kProfileDeleting);
  catalog.records[0].lifecycle = ProfileLifecycleState::kProvisioning;
  EXPECT_EQ(ResolveProfileRegistryPath(catalog, kDefaultId, 9).error,
            ProfileRegistryLookupError::kProfileProvisioning);
  catalog.records[0].lifecycle = ProfileLifecycleState::kRepairRequired;
  EXPECT_EQ(ResolveProfileRegistryPath(catalog, kDefaultId, 9).error,
            ProfileRegistryLookupError::kProfileRepairRequired);
}

TEST(MahoSpaceProfileHydrationTest, LifecycleStatesRoundTrip) {
  for (ProfileLifecycleState state : {ProfileLifecycleState::kReady,
                                      ProfileLifecycleState::kProvisioning,
                                      ProfileLifecycleState::kDeleting,
                                      ProfileLifecycleState::kRepairRequired}) {
    EXPECT_EQ(ProfileLifecycleStateFromString(
                  ProfileLifecycleStateToString(state)),
              state);
  }
  EXPECT_FALSE(ProfileLifecycleStateFromString("unknown").has_value());
}

TEST(MahoSpaceProfileHydrationTest, ValidatesProfileIdentifiers) {
  EXPECT_EQ(ProfileBasenameForId(""), base::FilePath::FromASCII("Default"));
  EXPECT_EQ(ProfileBasenameForId("   "),
            base::FilePath::FromASCII("Default"));
  EXPECT_EQ(ProfileBasenameForId("Default"),
            base::FilePath::FromASCII("Default"));
  EXPECT_EQ(ProfileBasenameForId("alpha"),
            base::FilePath::FromASCII("MahoProfile_alpha"));

  EXPECT_FALSE(ProfileBasenameForId("alpha/beta").has_value());
  EXPECT_FALSE(ProfileBasenameForId("alpha\\beta").has_value());
  EXPECT_FALSE(ProfileBasenameForId("a:b").has_value());
  EXPECT_FALSE(ProfileBasenameForId(".").has_value());
  EXPECT_FALSE(ProfileBasenameForId("..").has_value());
  EXPECT_FALSE(ProfileBasenameForId("foo..bar").has_value());
  EXPECT_FALSE(ProfileBasenameForId(".alpha").has_value());
  EXPECT_FALSE(ProfileBasenameForId("\xff\xfe").has_value());
  EXPECT_FALSE(ProfileBasenameForId(std::string(65, 'a')).has_value());
}

TEST(MahoSpaceProfileHydrationTest, RejectsMalformedInput) {
  EXPECT_FALSE(ParseSpaceProfileHydrationState(
                   R"([{"id":"work","profileId":"default"})",
                   R"("work")")
                   .has_value());
  EXPECT_FALSE(ParseSpaceProfileHydrationState(
                   R"([{"id":"work","profileId":"default"}])", "42")
                   .has_value());
}

TEST(MahoSpaceProfileHydrationTest, MergesWithoutDeletingExistingSpaces) {
  base::flat_map<std::string, base::FilePath> spaces = {
      {"existing", base::FilePath::FromASCII("Default")},
      {"updated", base::FilePath::FromASCII("MahoProfile_old")},
  };
  std::string active_space_id = "existing";
  SpaceProfileHydrationState state;
  state.space_to_profile = {
      {"new", base::FilePath::FromASCII("Default")},
      {"updated", base::FilePath::FromASCII("MahoProfile_new")},
  };
  state.active_space_id = "new";

  SpaceProfileHydrationChanges changes = MergeSpaceProfileHydrationState(
      std::move(state), &spaces, &active_space_id);

  EXPECT_TRUE(changes.structural);
  EXPECT_TRUE(changes.active_space);
  EXPECT_EQ(spaces.size(), 3u);
  EXPECT_EQ(spaces.at("existing"), base::FilePath::FromASCII("Default"));
  EXPECT_EQ(spaces.at("updated"),
            base::FilePath::FromASCII("MahoProfile_new"));
  EXPECT_EQ(active_space_id, "new");
}

TEST(MahoSpaceProfileHydrationTest,
     AuthoritativeReplacementRemovesStaleAndNonReadyMappings) {
  base::flat_map<std::string, base::FilePath> spaces = {
      {"stale", base::FilePath::FromASCII("MahoProfile_stale")},
      {"ready", base::FilePath::FromASCII("MahoProfile_old")},
  };
  std::string active_space_id = "stale";
  SpaceProfileHydrationState state;
  state.space_to_profile = {
      {"ready", base::FilePath::FromASCII("MahoProfile_ready")},
  };
  state.active_space_id = "ready";

  SpaceProfileHydrationChanges changes = ReplaceSpaceProfileHydrationState(
      std::move(state), &spaces, &active_space_id);

  EXPECT_TRUE(changes.structural);
  EXPECT_TRUE(changes.active_space);
  ASSERT_EQ(spaces.size(), 1u);
  EXPECT_EQ(spaces.at("ready"),
            base::FilePath::FromASCII("MahoProfile_ready"));
  EXPECT_FALSE(spaces.contains("stale"));
  EXPECT_EQ(active_space_id, "ready");
}

TEST(MahoSpaceProfileHydrationTest, RepeatedMergeIsIdempotent) {
  base::flat_map<std::string, base::FilePath> spaces;
  std::string active_space_id;
  auto make_state = [] {
    SpaceProfileHydrationState state;
    state.space_to_profile = {
        {"work", base::FilePath::FromASCII("Default")},
    };
    state.active_space_id = "work";
    return state;
  };

  SpaceProfileHydrationChanges first = MergeSpaceProfileHydrationState(
      make_state(), &spaces, &active_space_id);
  SpaceProfileHydrationChanges second = MergeSpaceProfileHydrationState(
      make_state(), &spaces, &active_space_id);

  EXPECT_TRUE(first.structural);
  EXPECT_TRUE(first.active_space);
  EXPECT_FALSE(second.structural);
  EXPECT_FALSE(second.active_space);
  EXPECT_EQ(spaces.size(), 1u);
  EXPECT_EQ(active_space_id, "work");
}

TEST(MahoSpaceProfileHydrationTest, DeferredHydrationPreservesActiveSpaceAssociation) {
  base::flat_map<std::string, base::FilePath> spaces;
  std::string active_space_id;

  // Simulate unhydrated initial state.
  EXPECT_TRUE(spaces.empty());
  EXPECT_TRUE(active_space_id.empty());

  // First hydration completes once storage/bridge is ready.
  const std::string spaces_json =
      R"([{"id":"space-alpha","profileId":"default"},{"id":"space-beta","profileId":"default"}])";
  const std::string active_id_json = R"("space-alpha")";

  std::optional<SpaceProfileHydrationState> state =
      ParseSpaceProfileHydrationState(spaces_json, active_id_json);
  ASSERT_TRUE(state.has_value());

  SpaceProfileHydrationChanges changes = MergeSpaceProfileHydrationState(
      std::move(*state), &spaces, &active_space_id);

  EXPECT_TRUE(changes.structural);
  EXPECT_TRUE(changes.active_space);
  EXPECT_EQ(active_space_id, "space-alpha");
  EXPECT_EQ(spaces.size(), 2u);
  EXPECT_EQ(spaces.at("space-alpha"), base::FilePath::FromASCII("Default"));
  EXPECT_EQ(spaces.at("space-beta"), base::FilePath::FromASCII("Default"));
}

}  // namespace
}  // namespace maho
