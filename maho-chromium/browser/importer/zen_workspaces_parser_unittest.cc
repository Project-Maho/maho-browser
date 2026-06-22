// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/zen_workspaces_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::importer {
namespace {

// Helper to create a literal-only MozLz4 block
std::vector<uint8_t> CompressToMozLz4(const std::string& json) {
  std::vector<uint8_t> compressed;
  // 8-byte magic: "mozLz40\0"
  const uint8_t magic[] = {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'};
  compressed.insert(compressed.end(), magic, magic + 8);

  // 4-byte LE uncompressed size
  uint32_t size = json.size();
  compressed.push_back(size & 0xff);
  compressed.push_back((size >> 8) & 0xff);
  compressed.push_back((size >> 16) & 0xff);
  compressed.push_back((size >> 24) & 0xff);

  // LZ4 literal-only block
  size_t L = json.size();
  if (L < 15) {
    compressed.push_back(static_cast<uint8_t>(L << 4));
  } else {
    compressed.push_back(0xf0);
    size_t remaining = L - 15;
    while (remaining >= 255) {
      compressed.push_back(255);
      remaining -= 255;
    }
    compressed.push_back(static_cast<uint8_t>(remaining));
  }
  compressed.insert(compressed.end(), json.begin(), json.end());
  return compressed;
}

bool WriteZenSessionFile(const base::FilePath& profile_dir, const std::string& json) {
  std::vector<uint8_t> data = CompressToMozLz4(json);
  base::FilePath file_path = profile_dir.Append(FILE_PATH_LITERAL("zen-sessions.jsonlz4"));
  return base::WriteFile(file_path, data);
}

bool WritePrefsJs(const base::FilePath& profile_dir, const std::string& active_ws_uuid) {
  base::FilePath file_path = profile_dir.Append(FILE_PATH_LITERAL("prefs.js"));
  std::string content = "user_pref(\"zen.workspaces.active\", \"" + active_ws_uuid + "\");\n";
  return base::WriteFile(file_path, content);
}

TEST(ZenWorkspacesParserTest, ParseValidWorkspaces) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  std::string json =
      "{\n"
      "  \"spaces\": [\n"
      "    {\n"
      "      \"uuid\": \"space-1\",\n"
      "      \"name\": \"Work\",\n"
      "      \"icon\": \"briefcase\",\n"
      "      \"userContextId\": 1,\n"
      "      \"theme\": {\n"
      "        \"gradientColors\": [{\"color\": \"#ff0000\"}]\n"
      "      }\n"
      "    }\n"
      "  ],\n"
      "  \"folders\": [\n"
      "    {\n"
      "      \"id\": \"folder-1\",\n"
      "      \"name\": \"Dev\",\n"
      "      \"workspaceId\": \"space-1\"\n"
      "    }\n"
      "  ],\n"
      "  \"tabs\": [\n"
      "    {\n"
      "      \"zenWorkspace\": \"space-1\",\n"
      "      \"pinned\": true,\n"
      "      \"entries\": [{\"url\": \"https://github.com\", \"title\": \"GitHub\"}]\n"
      "    },\n"
      "    {\n"
      "      \"zenWorkspace\": \"space-1\",\n"
      "      \"folderId\": \"folder-1\",\n"
      "      \"entries\": [{\"url\": \"https://stackoverflow.com\", \"title\": \"SO\"}]\n"
      "    },\n"
      "    {\n"
      "      \"zenWorkspace\": \"space-1\",\n"
      "      \"zenEssential\": true,\n"
      "      \"entries\": [{\"url\": \"https://gmail.com\", \"title\": \"Gmail\"}]\n"
      "    }\n"
      "  ]\n"
      "}";

  ASSERT_TRUE(WriteZenSessionFile(temp_dir.GetPath(), json));
  ASSERT_TRUE(WritePrefsJs(temp_dir.GetPath(), "space-1"));

  ZenParseResult result = ParseZenWorkspaces(temp_dir.GetPath());

  EXPECT_EQ(result.status, ZenParseStatus::kSuccess);
  EXPECT_EQ(result.active_workspace_uuid, "space-1");
  ASSERT_EQ(result.workspaces.size(), 1u);

  const auto& ws = result.workspaces[0];
  EXPECT_EQ(ws.uuid, "space-1");
  EXPECT_EQ(ws.name, "Work");
  EXPECT_EQ(ws.icon, "briefcase");
  EXPECT_EQ(ws.container_id, 1);
  EXPECT_EQ(ws.theme_color_hex, "#ff0000");

  // Pinned tabs
  ASSERT_EQ(ws.pinned_tabs.size(), 1u);
  EXPECT_EQ(ws.pinned_tabs[0].url, "https://github.com");
  EXPECT_EQ(ws.pinned_tabs[0].title, "GitHub");

  // Essential / favorites
  ASSERT_EQ(ws.favorites.size(), 1u);
  EXPECT_EQ(ws.favorites[0].url, "https://gmail.com");

  // Folders
  ASSERT_EQ(ws.folders.size(), 1u);
  EXPECT_EQ(ws.folders[0].id, "folder-1");
  EXPECT_EQ(ws.folders[0].name, "Dev");

  // Tabs inside folders
  ASSERT_EQ(ws.folders[0].tabs.size(), 1u);
  EXPECT_EQ(ws.folders[0].tabs[0].url, "https://stackoverflow.com");
  EXPECT_EQ(ws.folders[0].tabs[0].title, "SO");
}

TEST(ZenWorkspacesParserTest, ParseInvalidWorkspaceUserContextId) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // space-1 is valid. space-2 is missing userContextId. space-3 has non-positive userContextId.
  std::string json =
      "{\n"
      "  \"spaces\": [\n"
      "    {\n"
      "      \"uuid\": \"space-1\",\n"
      "      \"name\": \"Work\",\n"
      "      \"userContextId\": 1\n"
      "    },\n"
      "    {\n"
      "      \"uuid\": \"space-2\",\n"
      "      \"name\": \"Invalid missing\",\n"
      "      \"icon\": \"invalid\"\n"
      "    },\n"
      "    {\n"
      "      \"uuid\": \"space-3\",\n"
      "      \"name\": \"Invalid negative\",\n"
      "      \"userContextId\": -5\n"
      "    }\n"
      "  ],\n"
      "  \"tabs\": []\n"
      "}";

  ASSERT_TRUE(WriteZenSessionFile(temp_dir.GetPath(), json));

  ZenParseResult result = ParseZenWorkspaces(temp_dir.GetPath());
  EXPECT_EQ(result.status, ZenParseStatus::kSuccess);
  ASSERT_EQ(result.workspaces.size(), 1u);
  EXPECT_EQ(result.workspaces[0].uuid, "space-1");
}

TEST(ZenWorkspacesParserTest, ParseFolderHierarchyNestingLimit) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Create folder chain:
  // folder-1 (root, level 1)
  // folder-2 (parent: folder-1, level 2)
  // folder-3 (parent: folder-2, level 3)
  // folder-4 (parent: folder-3, level 4)
  // folder-5 (parent: folder-4, level 5)
  // folder-6 (parent: folder-5, level 6) -> should be skipped!
  std::string json =
      "{\n"
      "  \"spaces\": [\n"
      "    {\n"
      "      \"uuid\": \"space-1\",\n"
      "      \"name\": \"Work\",\n"
      "      \"userContextId\": 1\n"
      "    }\n"
      "  ],\n"
      "  \"folders\": [\n"
      "    {\"id\": \"folder-1\", \"name\": \"L1\", \"workspaceId\": \"space-1\"},\n"
      "    {\"id\": \"folder-2\", \"name\": \"L2\", \"workspaceId\": \"space-1\", \"parentId\": \"folder-1\"},\n"
      "    {\"id\": \"folder-3\", \"name\": \"L3\", \"workspaceId\": \"space-1\", \"parentId\": \"folder-2\"},\n"
      "    {\"id\": \"folder-4\", \"name\": \"L4\", \"workspaceId\": \"space-1\", \"parentId\": \"folder-3\"},\n"
      "    {\"id\": \"folder-5\", \"name\": \"L5\", \"workspaceId\": \"space-1\", \"parentId\": \"folder-4\"},\n"
      "    {\"id\": \"folder-6\", \"name\": \"L6\", \"workspaceId\": \"space-1\", \"parentId\": \"folder-5\"}\n"
      "  ],\n"
      "  \"tabs\": []\n"
      "}";

  ASSERT_TRUE(WriteZenSessionFile(temp_dir.GetPath(), json));

  ZenParseResult result = ParseZenWorkspaces(temp_dir.GetPath());
  EXPECT_EQ(result.status, ZenParseStatus::kSuccess);
  ASSERT_EQ(result.workspaces.size(), 1u);

  const auto& ws = result.workspaces[0];
  EXPECT_EQ(ws.folders.size(), 5u);

  // Assert L6 is not in folders
  for (const auto& folder : ws.folders) {
    EXPECT_NE(folder.id, "folder-6");
  }
}

TEST(ZenWorkspacesParserTest, ParseMalformedJson) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("zen-sessions.jsonlz4"));
  ASSERT_TRUE(base::WriteFile(file_path, "not compressed at all"));

  ZenParseResult result = ParseZenWorkspaces(temp_dir.GetPath());
  EXPECT_EQ(result.status, ZenParseStatus::kDecompressFailed);
  EXPECT_TRUE(result.workspaces.empty());

  // Valid decompression, but bad json
  ASSERT_TRUE(WriteZenSessionFile(temp_dir.GetPath(), "{invalid json"));
  result = ParseZenWorkspaces(temp_dir.GetPath());
  EXPECT_EQ(result.status, ZenParseStatus::kJsonParseFailed);
  EXPECT_TRUE(result.workspaces.empty());
}

}  // namespace
}  // namespace maho::importer
