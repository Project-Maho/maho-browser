// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/favicon_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::importer {
namespace {

// Minimal valid PNG (16x16)
const uint8_t kValidPng[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
  0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
  0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
  0x08, 0x06, 0x00, 0x00, 0x00
};

// Oversized PNG (257x16)
const uint8_t kOversizedPng[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
  0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
  0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x10,
  0x08, 0x06, 0x00, 0x00, 0x00
};

// Invalid format / bad MIME
const uint8_t kInvalidMime[] = {
  0x11, 0x22, 0x33, 0x44, 0x55, 0x66
};

bool CreateTestChromiumFaviconDb(const base::FilePath& db_path,
                                 const std::vector<uint8_t>& image_data) {
  sql::Database db("History");
  if (!db.Open(db_path)) {
    return false;
  }
  if (!db.Execute("CREATE TABLE icon_mapping (page_url TEXT, icon_id INTEGER)")) {
    return false;
  }
  if (!db.Execute("CREATE TABLE favicons (id INTEGER PRIMARY KEY, url TEXT)")) {
    return false;
  }
  if (!db.Execute("CREATE TABLE favicon_bitmaps (icon_id INTEGER, image_data BLOB)")) {
    return false;
  }

  // Insert mapping
  sql::Statement map_stmt(db.GetUniqueStatement(
      "INSERT INTO icon_mapping (page_url, icon_id) VALUES ('https://github.com', 1)"));
  if (!map_stmt.Run()) {
    return false;
  }

  sql::Statement fav_stmt(db.GetUniqueStatement(
      "INSERT INTO favicons (id, url) VALUES (1, 'https://github.com/favicon.ico')"));
  if (!fav_stmt.Run()) {
    return false;
  }

  sql::Statement bmp_stmt(db.GetUniqueStatement(
      "INSERT INTO favicon_bitmaps (icon_id, image_data) VALUES (1, ?)"));
  bmp_stmt.BindBlob(0, image_data);
  return bmp_stmt.Run();
}

TEST(FaviconParserTest, ParseChromiumValidFavicon) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Favicons"));
  ASSERT_TRUE(CreateTestChromiumFaviconDb(db_path, std::vector<uint8_t>(kValidPng, kValidPng + sizeof(kValidPng))));

  auto result = ParseChromiumFavicons(db_path);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].favicon_url.spec(), "https://github.com/favicon.ico");
  EXPECT_EQ(result[0].png_data.size(), sizeof(kValidPng));
}

TEST(FaviconParserTest, ParseChromiumOversizedFaviconRejected) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Favicons"));
  ASSERT_TRUE(CreateTestChromiumFaviconDb(db_path, std::vector<uint8_t>(kOversizedPng, kOversizedPng + sizeof(kOversizedPng))));

  auto result = ParseChromiumFavicons(db_path);
  EXPECT_TRUE(result.empty());
}

TEST(FaviconParserTest, ParseChromiumInvalidMimeRejected) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Favicons"));
  ASSERT_TRUE(CreateTestChromiumFaviconDb(db_path, std::vector<uint8_t>(kInvalidMime, kInvalidMime + sizeof(kInvalidMime))));

  auto result = ParseChromiumFavicons(db_path);
  EXPECT_TRUE(result.empty());
}

}  // namespace
}  // namespace maho::importer
