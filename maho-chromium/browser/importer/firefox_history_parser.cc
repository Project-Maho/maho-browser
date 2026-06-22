// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/importer/firefox_history_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/files/scoped_temp_dir.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

#include "maho/browser/importer/mozlz4_decompressor.h"

namespace maho {

namespace {

constexpr int kMaxHistoryRows = 100000;

base::Time FirefoxMicrosecondsToTime(int64_t microseconds) {
  if (microseconds <= 0) {
    return base::Time::Now();
  }
  return base::Time::UnixEpoch() + base::Microseconds(microseconds);
}

bool CopyDatabaseFiles(const base::FilePath& source,
                       const base::FilePath& dest_dir,
                       const std::string& filename) {
  base::FilePath dest = dest_dir.Append(filename);
  if (!base::CopyFile(source, dest)) {
    return false;
  }

  base::FilePath wal = source.AddExtension("wal");
  if (base::PathExists(wal)) {
    base::CopyFile(wal, dest_dir.Append(filename + ".wal"));
  }

  base::FilePath shm = source.AddExtension("shm");
  if (base::PathExists(shm)) {
    base::CopyFile(shm, dest_dir.Append(filename + ".shm"));
  }

  return true;
}

}  // namespace

std::vector<ParsedHistoryRow> ParseFirefoxHistory(
    const base::FilePath& places_sqlite) {
  std::vector<ParsedHistoryRow> results;

  if (!base::PathExists(places_sqlite)) {
    return results;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return results;
  }

  base::FilePath db_copy = temp_dir.GetPath().Append("places.sqlite");

  bool is_compressed = false;
  std::string file_header;
  base::ReadFileToStringWithMaxSize(places_sqlite, &file_header, 8);
  if (file_header.size() == 8 &&
      memcmp(file_header.data(), "mozLz40\0", 8) == 0) {
    is_compressed = true;
  }

  if (is_compressed) {
    auto decompressed = maho::importer::DecompressMozLz4(places_sqlite);
    if (!decompressed.has_value()) {
      return results;
    }
    if (!base::WriteFile(db_copy, *decompressed)) {
      return results;
    }
  } else {
    if (!CopyDatabaseFiles(places_sqlite, temp_dir.GetPath(),
                            "places.sqlite")) {
      return results;
    }
  }

  sql::Database db("MahoImporterFirefoxHistory");
  if (!db.Open(db_copy)) {
    return results;
  }

  static constexpr char kQuery[] =
      "SELECT p.url, p.title, p.visit_count, MAX(h.visit_date) "
      "FROM moz_places p "
      "JOIN moz_historyvisits h ON p.id = h.place_id "
      "WHERE p.url NOT LIKE 'place:%' "
      "  AND p.url NOT LIKE 'about:%' "
      "GROUP BY p.id "
      "ORDER BY MAX(h.visit_date) DESC "
      "LIMIT ?";

  sql::Statement statement(db.GetUniqueStatement(kQuery));
  statement.BindInt(0, kMaxHistoryRows);

  while (statement.Step()) {
    std::string url_str = statement.ColumnString(0);
    GURL url(url_str);
    if (!url.is_valid()) {
      continue;
    }

    ParsedHistoryRow row;
    row.url = std::move(url);
    row.title = statement.ColumnString16(1);
    row.visit_count = statement.ColumnInt(2);
    row.last_visit = FirefoxMicrosecondsToTime(statement.ColumnInt64(3));
    results.push_back(std::move(row));
  }

  return results;
}

}  // namespace maho
