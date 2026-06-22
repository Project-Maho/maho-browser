// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/arc_sidebar_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::importer {
namespace {

bool WriteSidebarFile(const base::FilePath& support_dir, const std::string& json) {
  base::FilePath file_path = support_dir.Append(FILE_PATH_LITERAL("StorableSidebar.json"));
  return base::WriteFile(file_path, json);
}

TEST(ArcSidebarParserTest, ParseEmptyOrMissingFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Missing file case
  ArcParseResult result = ParseArcSidebar(temp_dir.GetPath());
  EXPECT_TRUE(result.workspaces.empty());

  // Empty file case
  ASSERT_TRUE(WriteSidebarFile(temp_dir.GetPath(), ""));
  result = ParseArcSidebar(temp_dir.GetPath());
  EXPECT_TRUE(result.workspaces.empty());
}

TEST(ArcSidebarParserTest, ParseInvalidJson) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  ASSERT_TRUE(WriteSidebarFile(temp_dir.GetPath(), "{ malformed json: true }"));
  ArcParseResult result = ParseArcSidebar(temp_dir.GetPath());
  EXPECT_TRUE(result.workspaces.empty());
}

TEST(ArcSidebarParserTest, ParseValidFirebaseSyncPath) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // StorableSidebar.json structure using firebaseSyncState.syncData
  // We use alternating lists where key index is even, and dict value is odd.
  std::string json =
      "{\n"
      "  \"firebaseSyncState\": {\n"
      "    \"syncData\": {\n"
      "      \"spaceModels\": [\n"
      "        \"space-uuid-1\", {\n"
      "          \"title\": \"Work Space\",\n"
      "          \"customInfo\": {\n"
      "            \"iconType\": {\n"
      "              \"emoji_v2\": \"💼\"\n"
      "            }\n"
      "          },\n"
      "          \"containerIDs\": [\n"
      "            \"pinned\", \"item-uuid-pinned-list\",\n"
      "            \"unpinned\", \"item-uuid-unpinned-list\"\n"
      "          ]\n"
      "        }\n"
      "      ],\n"
      "      \"items\": [\n"
      "        \"item-uuid-pinned-list\", {\n"
      "          \"id\": \"item-uuid-pinned-list\",\n"
      "          \"childrenIds\": [\n"
      "            \"item\", \"tab-uuid-1\",\n"
      "            \"item\", \"folder-uuid-1\"\n"
      "          ]\n"
      "        },\n"
      "        \"tab-uuid-1\", {\n"
      "          \"id\": \"tab-uuid-1\",\n"
      "          \"data\": {\n"
      "            \"tab\": {\n"
      "              \"savedURL\": \"https://github.com\",\n"
      "              \"savedTitle\": \"GitHub\"\n"
      "            }\n"
      "          }\n"
      "        },\n"
      "        \"folder-uuid-1\", {\n"
      "          \"id\": \"folder-uuid-1\",\n"
      "          \"title\": \"Dev Folder\",\n"
      "          \"data\": {\n"
      "            \"list\": {}\n"
      "          }\n"
      "        },\n"
      "        \"item-uuid-unpinned-list\", {\n"
      "          \"id\": \"item-uuid-unpinned-list\",\n"
      "          \"childrenIds\": {\n"
      "            \"_0\": [\n"
      "              \"item\", \"tab-uuid-2\",\n"
      "              \"item\", \"tab-uuid-invalid\"\n"
      "            ]\n"
      "          }\n"
      "        },\n"
      "        \"tab-uuid-2\", {\n"
      "          \"id\": \"tab-uuid-2\",\n"
      "          \"data\": {\n"
      "            \"tab\": {\n"
      "              \"savedURL\": \"https://google.com\",\n"
      "              \"savedTitle\": \"Google\"\n"
      "            }\n"
      "          }\n"
      "        },\n"
      "        \"tab-uuid-invalid\", {\n"
      "          \"id\": \"tab-uuid-invalid\",\n"
      "          \"data\": {\n"
      "            \"tab\": {\n"
      "              \"savedURL\": \"chrome://settings\",\n"
      "              \"savedTitle\": \"Settings\"\n"
      "            }\n"
      "          }\n"
      "        }\n"
      "      ]\n"
      "    }\n"
      "  }\n"
      "}";

  ASSERT_TRUE(WriteSidebarFile(temp_dir.GetPath(), json));

  ArcParseResult result = ParseArcSidebar(temp_dir.GetPath());
  ASSERT_EQ(result.workspaces.size(), 1u);

  const auto& ws = result.workspaces[0];
  EXPECT_EQ(ws.name, "Work Space");
  EXPECT_EQ(ws.icon, "💼");

  // Pinned items
  ASSERT_EQ(ws.pinned_tabs.size(), 1u);
  EXPECT_EQ(ws.pinned_tabs[0].url, "https://github.com");
  EXPECT_EQ(ws.pinned_tabs[0].title, "GitHub");

  // Folders
  ASSERT_EQ(ws.folders.size(), 1u);
  EXPECT_EQ(ws.folders[0].id, "folder-uuid-1");
  EXPECT_EQ(ws.folders[0].name, "Dev Folder");

  // Regular / Unpinned items (excluding invalid chrome:// URL)
  ASSERT_EQ(ws.regular_tabs.size(), 1u);
  EXPECT_EQ(ws.regular_tabs[0].url, "https://google.com");
  EXPECT_EQ(ws.regular_tabs[0].title, "Google");
}

TEST(ArcSidebarParserTest, ParseValidFallbackContainersPath) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // StorableSidebar.json structure using sidebar.containers[1]
  std::string json =
      "{\n"
      "  \"sidebar\": {\n"
      "    \"containers\": [\n"
      "      {},\n"
      "      {\n"
      "        \"spaces\": [\n"
      "          \"space-uuid-1\", {\n"
      "            \"title\": \"Fallback Space\",\n"
      "            \"customInfo\": {\n"
      "              \"iconType\": {\n"
      "                \"icon\": \"archive\"\n"
      "              }\n"
      "            },\n"
      "            \"containerIDs\": [\n"
      "              \"pinned\", \"item-uuid-pinned-list\"\n"
      "            ]\n"
      "          }\n"
      "        ],\n"
      "        \"items\": [\n"
      "          \"item-uuid-pinned-list\", {\n"
      "            \"id\": \"item-uuid-pinned-list\",\n"
      "            \"childrenIds\": {\n"
      "              \"items\": [\n"
      "                \"item\", \"tab-uuid-1\"\n"
      "              ]\n"
      "            }\n"
      "          },\n"
      "          \"tab-uuid-1\", {\n"
      "            \"id\": \"tab-uuid-1\",\n"
      "            \"data\": {\n"
      "              \"tab\": {\n"
      "                \"savedURL\": \"https://github.com\",\n"
      "                \"savedTitle\": \"GitHub\"\n"
      "              }\n"
      "            }\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    ]\n"
      "  }\n"
      "}";

  ASSERT_TRUE(WriteSidebarFile(temp_dir.GetPath(), json));

  ArcParseResult result = ParseArcSidebar(temp_dir.GetPath());
  ASSERT_EQ(result.workspaces.size(), 1u);

  const auto& ws = result.workspaces[0];
  EXPECT_EQ(ws.name, "Fallback Space");
  EXPECT_EQ(ws.icon, "archive");

  ASSERT_EQ(ws.pinned_tabs.size(), 1u);
  EXPECT_EQ(ws.pinned_tabs[0].url, "https://github.com");
  EXPECT_EQ(ws.pinned_tabs[0].title, "GitHub");
}

}  // namespace
}  // namespace maho::importer
