// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/chromium_history_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

bool CreateTestHistoryDb(const base::FilePath& db_path,
                         const std::vector<std::pair<std::string, int64_t>>& test_urls) {
  sql::Database db("History");
  if (!db.Open(db_path)) {
    return false;
  }
  if (!db.Execute("CREATE TABLE urls ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "url TEXT,"
                  "title TEXT,"
                  "visit_count INTEGER,"
                  "last_visit_time INTEGER)")) {
    return false;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "INSERT INTO urls (url, title, visit_count, last_visit_time) "
      "VALUES (?, ?, ?, ?)"));

  for (const auto& item : test_urls) {
    stmt.Reset(true);
    stmt.BindString(0, item.first);
    stmt.BindString16(1, u"Test Title");
    stmt.BindInt(2, 5);
    stmt.BindInt64(3, item.second);
    if (!stmt.Run()) {
      return false;
    }
  }
  return true;
}

TEST(ChromiumHistoryParserTest, ParseEmptyOrMissing) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Test missing file
  base::FilePath fake_bookmarks = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Bookmarks"));
  auto results = ParseChromiumHistory(fake_bookmarks);
  EXPECT_TRUE(results.empty());
}

TEST(ChromiumHistoryParserTest, ParseAndDeduplicate) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath fake_bookmarks = temp_dir.GetPath().Append(FILE_PATH_LITERAL("Bookmarks"));
  base::FilePath history_path = temp_dir.GetPath().Append(FILE_PATH_LITERAL("History"));

  // Create test items with:
  // - Valid HTTP URL visited at time 200
  // - Duplicate HTTP URL visited at time 100 (should be deduped, keeping time 200)
  // - chrome:// URL (should be skipped)
  // - Invalid URL (should be skipped)
  // - Another HTTP URL visited at time 150
  std::vector<std::pair<std::string, int64_t>> test_data = {
      {"https://github.com", 200},
      {"https://google.com", 150},
      {"https://github.com", 100},
      {"chrome://settings", 300},
      {"invalid_url", 400},
  };

  ASSERT_TRUE(CreateTestHistoryDb(history_path, test_data));

  auto results = ParseChromiumHistory(fake_bookmarks);
  ASSERT_EQ(results.size(), 2u);

  // Results should be sorted by last_visit_time DESC (order by query)
  EXPECT_EQ(results[0].url.spec(), "https://github.com/");
  EXPECT_EQ(results[0].last_visit, base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(200)));

  EXPECT_EQ(results[1].url.spec(), "https://google.com/");
  EXPECT_EQ(results[1].last_visit, base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(150)));
}

}  // namespace
}  // namespace maho
