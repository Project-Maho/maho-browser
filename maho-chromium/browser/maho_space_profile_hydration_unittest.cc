// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_space_profile_hydration.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
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
  SpaceProfileHydrationState state{
      .space_to_profile =
          {{"new", base::FilePath::FromASCII("Default")},
           {"updated", base::FilePath::FromASCII("MahoProfile_new")}},
      .active_space_id = "new",
  };

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

TEST(MahoSpaceProfileHydrationTest, RepeatedMergeIsIdempotent) {
  base::flat_map<std::string, base::FilePath> spaces;
  std::string active_space_id;
  auto make_state = [] {
    return SpaceProfileHydrationState{
        .space_to_profile =
            {{"work", base::FilePath::FromASCII("Default")}},
        .active_space_id = "work",
    };
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

}  // namespace
}  // namespace maho
