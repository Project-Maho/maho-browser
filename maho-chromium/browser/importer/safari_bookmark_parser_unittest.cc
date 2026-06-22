// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/safari_bookmark_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_MAC)
#include <sys/stat.h>
#endif

namespace maho::importer {
namespace {

#if BUILDFLAG(IS_MAC)

std::string MakeNestedPlist(int levels) {
  std::string plist = 
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\">\n"
      "<dict>\n"
      "  <key>Children</key>\n"
      "  <array>\n";

  for (int i = 0; i < levels; ++i) {
    plist += 
        "    <dict>\n"
        "      <key>WebBookmarkType</key>\n"
        "      <string>WebBookmarkTypeList</string>\n"
        "      <key>Title</key>\n"
        "      <string>Level_" + std::to_string(i) + "</string>\n"
        "      <key>Children</key>\n"
        "      <array>\n";
  }

  // Leaf at the deepest level
  plist += 
      "        <dict>\n"
      "          <key>WebBookmarkType</key>\n"
      "          <string>WebBookmarkTypeLeaf</string>\n"
      "          <key>URLString</key>\n"
      "          <string>https://github.com</string>\n"
      "          <key>URIDictionary</key>\n"
      "          <dict>\n"
      "            <key>title</key>\n"
      "            <string>Deep Leaf</string>\n"
      "          </dict>\n"
      "        </dict>\n";

  for (int i = 0; i < levels; ++i) {
    plist += 
        "      </array>\n"
        "    </dict>\n";
  }

  plist += 
      "  </array>\n"
      "</dict>\n"
      "</plist>\n";
  return plist;
}

TEST(SafariBookmarkParserTest, ParseValidPlist) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  std::string plist_content = MakeNestedPlist(1);
  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Bookmarks.plist"));
  ASSERT_TRUE(base::WriteFile(file_path, plist_content));

  SafariParseResult result = ParseSafariBookmarks(file_path);
  EXPECT_EQ(result.status, SafariParseStatus::kSuccess);
  EXPECT_FALSE(result.fda_required);
  ASSERT_EQ(result.bookmarks.size(), 1u);

  const auto& folder = result.bookmarks[0];
  EXPECT_TRUE(folder.is_folder);
  EXPECT_EQ(folder.title, u"Level_0");
  ASSERT_EQ(folder.children.size(), 1u);
  EXPECT_FALSE(folder.children[0].is_folder);
  EXPECT_EQ(folder.children[0].url.spec(), "https://github.com/");
  EXPECT_EQ(folder.children[0].title, u"Deep Leaf");
}

TEST(SafariBookmarkParserTest, ParseCorruptPlist) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Bookmarks.plist"));
  ASSERT_TRUE(base::WriteFile(file_path, "not a valid XML plist format"));

  SafariParseResult result = ParseSafariBookmarks(file_path);
  EXPECT_EQ(result.status, SafariParseStatus::kPlistParseFailed);
  EXPECT_TRUE(result.bookmarks.empty());
}

TEST(SafariBookmarkParserTest, ParseFileNotFound) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("NonExistent.plist"));
  SafariParseResult result = ParseSafariBookmarks(file_path);
  EXPECT_EQ(result.status, SafariParseStatus::kFileNotFound);
  EXPECT_FALSE(result.fda_required);
}

TEST(SafariBookmarkParserTest, ParsePermissionDenied) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("NoRead.plist"));
  ASSERT_TRUE(base::WriteFile(file_path, "dummy content"));

  // Revoke all permissions to trigger EACCES
  ASSERT_TRUE(base::SetPosixFilePermissions(file_path, 0000));

  SafariParseResult result = ParseSafariBookmarks(file_path);
  EXPECT_EQ(result.status, SafariParseStatus::kPermissionDenied);
  EXPECT_TRUE(result.fda_required);

  // Restore permissions to allow cleanup
  base::SetPosixFilePermissions(file_path, 0600);
}

TEST(SafariBookmarkParserTest, CapRecursionDepth) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Create a 35-level deep plist. Recursion limit is 32.
  std::string plist_content = MakeNestedPlist(35);
  base::FilePath file_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Bookmarks.plist"));
  ASSERT_TRUE(base::WriteFile(file_path, plist_content));

  SafariParseResult result = ParseSafariBookmarks(file_path);
  EXPECT_EQ(result.status, SafariParseStatus::kSuccess);

  // Walk the parsed tree to verify depth cap
  int parsed_depth = 0;
  const std::vector<maho::ParsedBookmark>* current_level = &result.bookmarks;
  while (!current_level->empty() && (*current_level)[0].is_folder) {
    parsed_depth++;
    current_level = &(*current_level)[0].children;
  }

  // Level 0 to Level 31 is 32 levels of folders.
  // The 33rd folder at depth 32 is skipped, so parsed_depth should be exactly 32.
  EXPECT_EQ(parsed_depth, 32);
}

#else

TEST(SafariBookmarkParserTest, UnsupportedPlatform) {
  base::FilePath file_path(FILE_PATH_LITERAL("dummy.plist"));
  SafariParseResult result = ParseSafariBookmarks(file_path);
  EXPECT_EQ(result.status, SafariParseStatus::kUnsupportedPlatform);
}

#endif

}  // namespace
}  // namespace maho::importer
