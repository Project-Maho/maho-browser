// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/firefox_history_parser.h"
#include "maho/browser/importer/mozlz4_decompressor.h"

#include "base/logging.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

// Helper to create a literal-only MozLz4 block
std::vector<uint8_t> CompressToMozLz4(const std::vector<uint8_t>& raw_bytes) {
  std::vector<uint8_t> compressed;
  // 8-byte magic: "mozLz40\0"
  const uint8_t magic[] = {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'};
  compressed.insert(compressed.end(), magic, magic + 8);

  // 4-byte LE uncompressed size
  uint32_t size = raw_bytes.size();
  compressed.push_back(size & 0xff);
  compressed.push_back((size >> 8) & 0xff);
  compressed.push_back((size >> 16) & 0xff);
  compressed.push_back((size >> 24) & 0xff);

  // LZ4 literal-only block
  size_t L = raw_bytes.size();
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
  compressed.insert(compressed.end(), raw_bytes.begin(), raw_bytes.end());
  return compressed;
}

bool CreateTestPlacesDb(const base::FilePath& db_path,
                        const std::vector<std::pair<std::string, int64_t>>& test_data) {
  sql::DatabaseOptions options;
  options.set_wal_mode(false);
  sql::Database db(options, "FirefoxImporter");
  if (!db.Open(db_path)) {
    return false;
  }
  if (!db.Execute("CREATE TABLE moz_places ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "url TEXT,"
                  "title TEXT,"
                  "visit_count INTEGER)")) {
    return false;
  }
  if (!db.Execute("CREATE TABLE moz_historyvisits ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "place_id INTEGER,"
                  "visit_date INTEGER)")) {
    return false;
  }

  sql::Statement place_stmt(db.GetUniqueStatement(
      "INSERT INTO moz_places (url, title, visit_count) VALUES (?, 'Test Title', 1)"));
  sql::Statement visit_stmt(db.GetUniqueStatement(
      "INSERT INTO moz_historyvisits (place_id, visit_date) VALUES (?, ?)"));

  for (const auto& item : test_data) {
    place_stmt.Reset(true);
    place_stmt.BindString(0, item.first);
    if (!place_stmt.Run()) {
      return false;
    }
    int64_t place_id = db.GetLastInsertRowId();

    visit_stmt.Reset(true);
    visit_stmt.BindInt64(0, place_id);
    visit_stmt.BindInt64(1, item.second);
    if (!visit_stmt.Run()) {
      return false;
    }
  }
  return true;
}

TEST(FirefoxHistoryParserTest, ParseNormalDatabase) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("places.sqlite"));

  std::vector<std::pair<std::string, int64_t>> test_data = {
      {"https://github.com", 1600000000000000ULL}, // PRTime is microseconds since 1970
      {"https://google.com", 1500000000000000ULL},
      {"about:config", 1700000000000000ULL}, // should be skipped
  };

  ASSERT_TRUE(CreateTestPlacesDb(db_path, test_data));

  auto results = ParseFirefoxHistory(db_path);
  ASSERT_EQ(results.size(), 2u);

  EXPECT_EQ(results[0].url.spec(), "https://github.com/");
  EXPECT_EQ(results[0].last_visit, base::Time::UnixEpoch() + base::Microseconds(1600000000000000ULL));

  EXPECT_EQ(results[1].url.spec(), "https://google.com/");
  EXPECT_EQ(results[1].last_visit, base::Time::UnixEpoch() + base::Microseconds(1500000000000000ULL));
}

TEST(FirefoxHistoryParserTest, ParseCompressedDatabase) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath raw_db_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("temp_places.sqlite"));
  std::vector<std::pair<std::string, int64_t>> test_data = {
      {"https://github.com", 1600000000000000ULL},
  };
  ASSERT_TRUE(CreateTestPlacesDb(raw_db_path, test_data));

  // Read raw DB bytes
  std::string raw_bytes;
  ASSERT_TRUE(base::ReadFileToString(raw_db_path, &raw_bytes));

  // Compress
  std::vector<uint8_t> compressed = CompressToMozLz4(
      std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end()));

  base::FilePath compressed_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("places.sqlite.jsonlz4"));
  ASSERT_TRUE(base::WriteFile(compressed_path, compressed));

  // Parse compressed file
  auto decomp = maho::importer::DecompressMozLz4(compressed_path);
  ASSERT_TRUE(decomp.has_value());
  ASSERT_EQ(*decomp, raw_bytes);
  auto results = ParseFirefoxHistory(compressed_path);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].url.spec(), "https://github.com/");
}

}  // namespace
}  // namespace maho
