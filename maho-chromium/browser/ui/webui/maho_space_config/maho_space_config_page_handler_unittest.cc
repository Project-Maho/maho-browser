// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kGeneratedDefaultProfileId[] =
    "2fd9d16c-cf31-4a71-91dd-783f87420dd2";
constexpr char kWorkProfileId[] = "profile-456";

void InstallProfileRegistry() {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge->ReconcileProfileRegistry(
      R"([{"id":"2fd9d16c-cf31-4a71-91dd-783f87420dd2","name":"Default","dataStoreId":null},{"id":"profile-456","name":"Work","dataStoreId":"work-store"}])",
      R"("2fd9d16c-cf31-4a71-91dd-783f87420dd2")",
      R"([{"id":"personal","profileId":"default"},{"id":"space-123","profileId":"profile-456"}])"));
}

maho_space_config::mojom::SpaceInfoPtr ParseSpaceInfo(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }
  return MahoSpaceConfigPageHandler::ParseSpaceInfoFromDictForTesting(
      parsed->GetDict());
}

maho_space_config::mojom::ProfileInfoPtr ParseProfileInfo(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }
  return MahoSpaceConfigPageHandler::ParseProfileInfoFromDictForTesting(
      parsed->GetDict());
}

std::optional<std::string> ProfileIdFromPayload(
    const std::optional<std::string>& profile_id) {
  InstallProfileRegistry();
  std::optional<std::string> event_json =
      MahoSpaceConfigPageHandler::BuildProfileUpdateEventJsonForTesting(
          "space-123", profile_id);
  if (!event_json) {
    return std::nullopt;
  }
  std::optional<base::Value> event =
      base::JSONReader::Read(*event_json, base::JSON_PARSE_RFC);
  const base::DictValue* changes =
      event && event->is_dict() ? event->GetDict().FindDict("changes") : nullptr;
  const std::string* parsed_profile_id =
      changes ? changes->FindString("profileId") : nullptr;
  return parsed_profile_id ? std::optional<std::string>(*parsed_profile_id)
                           : std::nullopt;
}

TEST(MahoSpaceConfigPageHandlerTest, ParsesCamelCaseViewModel) {
  const char kRustSpaceViewModelJson[] = R"({
    "id": "space-123",
    "name": "Work",
    "icon": "briefcase",
    "color": {
      "hue": 220.0,
      "saturation": 0.8,
      "brightness": 0.9,
      "grain": 0.1
    },
    "theme": {
      "type": "gradient",
      "gradientColors": [
        {
          "hue": 220.0,
          "saturation": 0.8,
          "brightness": 0.9,
          "isPrimary": true,
          "isCustom": false
        }
      ],
      "harmony": "analogous",
      "opacity": 1.0,
      "texture": 0.0
    },
    "profileId": "profile-456",
    "tabCount": 3,
    "isActive": true,
    "orderIndex": 0
  })";

  auto info = ParseSpaceInfo(kRustSpaceViewModelJson);
  ASSERT_TRUE(info);

  EXPECT_EQ(info->id, "space-123");
  EXPECT_EQ(info->name, "Work");
  EXPECT_EQ(info->icon, "briefcase");

  EXPECT_EQ(info->color, "#2E6BE6");
  ASSERT_TRUE(info->theme_json.has_value());
  std::optional<base::Value> theme =
      base::JSONReader::Read(*info->theme_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(theme.has_value());
  ASSERT_TRUE(theme->is_dict());
  const std::string* theme_type = theme->GetDict().FindString("type");
  ASSERT_TRUE(theme_type);
  EXPECT_EQ(*theme_type, "gradient");
  EXPECT_TRUE(theme->GetDict().FindList("gradientColors"));
  ASSERT_TRUE(info->profile_id.has_value());
  EXPECT_EQ(*info->profile_id, "profile-456");
}

TEST(MahoSpaceConfigPageHandlerTest, NormalizesFractionalHueColor) {
  auto info = ParseSpaceInfo(R"({
    "id": "space-fractional",
    "color": {
      "hue": 0.5,
      "saturation": 1.0,
      "brightness": 1.0
    }
  })");

  ASSERT_TRUE(info);
  EXPECT_EQ(info->color, "#00FFFF");
}

TEST(MahoSpaceConfigPageHandlerTest, PreservesDegreeHueColorSemantics) {
  auto info = ParseSpaceInfo(R"({
    "id": "space-degrees",
    "color": {
      "hue": 120.0,
      "saturation": 1.0,
      "brightness": 1.0
    }
  })");

  ASSERT_TRUE(info);
  EXPECT_EQ(info->color, "#00FF00");
}

TEST(MahoSpaceConfigPageHandlerTest, SnakeCaseFixtureIsRejected) {
  const char kSnakeCaseJson[] = R"({
    "id": "space-789",
    "name": "Personal",
    "icon": "heart",
    "color": "#FF0000",
    "theme_json": "{\"type\":\"solid\"}",
    "profile_id": "profile-789"
  })";

  auto info = ParseSpaceInfo(kSnakeCaseJson);
  ASSERT_TRUE(info);

  EXPECT_FALSE(info->theme_json.has_value());
  EXPECT_FALSE(info->profile_id.has_value());
}

TEST(MahoSpaceConfigPageHandlerTest,
     ParsesDefaultProfileFromNullOrMissingDataStoreId) {
  for (const std::string& json : {
           R"({"id":"default-uuid","name":"Default","dataStoreId":null})",
           R"({"id":"legacy-default","name":"Legacy Default"})"}) {
    SCOPED_TRACE(json);
    auto profile = ParseProfileInfo(json);
    ASSERT_TRUE(profile);
    EXPECT_TRUE(profile->is_default);
  }
}

TEST(MahoSpaceConfigPageHandlerTest, ParsesNonDefaultProfileFromDataStoreId) {
  auto profile = ParseProfileInfo(
      R"({"id":"work-uuid","name":"Work","dataStoreId":"store-1"})");
  ASSERT_TRUE(profile);
  EXPECT_FALSE(profile->is_default);
}

TEST(MahoSpaceConfigPageHandlerTest, BuildsCamelCaseProfileUpdatePayload) {
  InstallProfileRegistry();
  std::optional<std::string> event_json =
      MahoSpaceConfigPageHandler::BuildProfileUpdateEventJsonForTesting(
          "space-123", "profile-456");
  ASSERT_TRUE(event_json.has_value());

  std::optional<base::Value> event =
      base::JSONReader::Read(*event_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(event.has_value());
  ASSERT_TRUE(event->is_dict());
  const std::string* kind = event->GetDict().FindString("kind");
  ASSERT_TRUE(kind);
  EXPECT_EQ(*kind, "update_space_config");
  const base::DictValue* changes = event->GetDict().FindDict("changes");
  ASSERT_TRUE(changes);
  const std::string* space_id = changes->FindString("spaceId");
  const std::string* profile_id = changes->FindString("profileId");
  ASSERT_TRUE(space_id);
  ASSERT_TRUE(profile_id);
  EXPECT_EQ(*space_id, "space-123");
  EXPECT_EQ(*profile_id, "profile-456");
  EXPECT_FALSE(changes->Find("profile_id"));
}

TEST(MahoSpaceConfigPageHandlerTest, NormalizesNullProfileToDefaultPayload) {
  InstallProfileRegistry();
  std::optional<std::string> event_json =
      MahoSpaceConfigPageHandler::BuildProfileUpdateEventJsonForTesting(
          "space-123", std::nullopt);
  ASSERT_TRUE(event_json.has_value());

  std::optional<base::Value> event =
      base::JSONReader::Read(*event_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(event.has_value());
  const base::DictValue* changes = event->GetDict().FindDict("changes");
  ASSERT_TRUE(changes);
  const std::string* profile_id = changes->FindString("profileId");
  ASSERT_TRUE(profile_id);
  EXPECT_EQ(*profile_id, kGeneratedDefaultProfileId);
}

TEST(MahoSpaceConfigPageHandlerTest, AcceptsDefaultProfileIdForms) {
  const std::vector<std::optional<std::string>> values = {
      std::nullopt, "", "   ", "default", "DeFaUlT"};
  for (const auto& value : values) {
    SCOPED_TRACE(value.value_or("<null>"));
    EXPECT_EQ(ProfileIdFromPayload(value), kGeneratedDefaultProfileId);
  }
}

TEST(MahoSpaceConfigPageHandlerTest, RejectsUnknownValidLookingProfileId) {
  const std::string profile_id(
      maho::MahoSpaceProfileBridge::kMahoProfileIdMaxLen, 'a');
  EXPECT_FALSE(ProfileIdFromPayload(profile_id).has_value());
}

TEST(MahoSpaceConfigPageHandlerTest, RejectsDeletingAndRepairProfiles) {
  InstallProfileRegistry();
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge->SetProfileLifecycleState(
      kWorkProfileId, maho::ProfileLifecycleState::kDeleting));
  EXPECT_FALSE(ProfileIdFromPayload(kWorkProfileId).has_value());

  InstallProfileRegistry();
  ASSERT_TRUE(bridge->SetProfileLifecycleState(
      kWorkProfileId, maho::ProfileLifecycleState::kRepairRequired));
  EXPECT_FALSE(ProfileIdFromPayload(kWorkProfileId).has_value());

  InstallProfileRegistry();
}

TEST(MahoSpaceConfigPageHandlerTest, RejectsInvalidProfileIdMatrix) {
  const std::vector<std::string> invalid_ids = {
      "alpha/beta", "alpha\\beta", "alpha:beta", "..", "foo..bar",
      ".alpha",     "\xC3\xA9",    std::string(
                                      maho::MahoSpaceProfileBridge::
                                              kMahoProfileIdMaxLen +
                                          1,
                                      'a')};
  for (const std::string& profile_id : invalid_ids) {
    SCOPED_TRACE(profile_id);
    EXPECT_FALSE(ProfileIdFromPayload(profile_id).has_value());
  }
}

TEST(MahoSpaceConfigPageHandlerTest, AcceptsMatchingSpaceConfigUpdate) {
  EXPECT_TRUE(
      MahoSpaceConfigPageHandler::CoreAcceptedSpaceConfigUpdateForTesting(
          R"([{"kind":"space_config_updated","changes":{"spaceId":"space-123","profileId":"profile-456"}}])",
          "space-123", "profile-456"));
}

TEST(MahoSpaceConfigPageHandlerTest, RejectsMismatchedSpaceConfigUpdates) {
  struct Case {
    const char* name;
    const char* response;
  };
  const Case cases[] = {
      {"unrelated space",
       R"([{"kind":"space_config_updated","changes":{"spaceId":"space-other","profileId":"profile-456"}}])"},
      {"missing profile",
       R"([{"kind":"space_config_updated","changes":{"spaceId":"space-123"}}])"},
      {"mismatched profile",
       R"([{"kind":"space_config_updated","changes":{"spaceId":"space-123","profileId":"profile-other"}}])"},
  };
  for (const Case& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    EXPECT_FALSE(
        MahoSpaceConfigPageHandler::CoreAcceptedSpaceConfigUpdateForTesting(
            test_case.response, "space-123", "profile-456"));
  }
}

}  // namespace
