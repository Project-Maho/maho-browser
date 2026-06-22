// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/chromium_history_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

#include <unordered_set>

namespace maho {

namespace {

constexpr int kMaxHistoryRows = 100000;

base::Time WebKitMicrosecondsToTime(int64_t webkit_us) {
  if (webkit_us <= 0) {
    return base::Time::Now();
  }
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(webkit_us));
}

}  // namespace

std::vector<ParsedHistoryRow> ParseChromiumHistory(
    const base::FilePath& profile_path) {
  std::vector<ParsedHistoryRow> results;

  base::FilePath history_file = profile_path.DirName().Append("History");
  if (!base::PathExists(history_file)) {
    return results;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return results;
  }

  base::FilePath dest = temp_dir.GetPath().Append("History");
  if (!base::CopyFile(history_file, dest)) {
    return results;
  }

  sql::Database db("MahoImporterChromiumHistory");
  if (!db.Open(dest)) {
    return results;
  }

  static constexpr char kQuery[] =
      "SELECT url, title, visit_count, last_visit_time "
      "FROM urls "
      "WHERE url NOT LIKE 'chrome://%' "
      "  AND url NOT LIKE 'chrome-extension://%' "
      "  AND url NOT LIKE 'maho://%' "
      "ORDER BY last_visit_time DESC "
      "LIMIT ?";

  sql::Statement statement(db.GetUniqueStatement(kQuery));
  statement.BindInt(0, kMaxHistoryRows);

  std::unordered_set<std::string> seen_urls;

  while (statement.Step()) {
    std::string url_str = statement.ColumnString(0);
    if (seen_urls.count(url_str)) {
      continue;
    }
    GURL url(url_str);
    if (!url.is_valid()) {
      continue;
    }

    seen_urls.insert(url_str);

    ParsedHistoryRow row;
    row.url = std::move(url);
    row.title = statement.ColumnString16(1);
    row.visit_count = statement.ColumnInt(2);
    row.last_visit = WebKitMicrosecondsToTime(statement.ColumnInt64(3));
    results.push_back(std::move(row));
  }

  return results;
}

}  // namespace maho
