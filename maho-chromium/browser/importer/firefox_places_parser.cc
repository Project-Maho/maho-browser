// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/firefox_places_parser.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kMaxRecursionDepth = 50;

// Firefox moz_bookmarks type constants.
constexpr int kBookmarkType = 1;
constexpr int kFolderType = 2;

// Well-known Firefox root folder IDs.
constexpr int64_t kRootId = 1;
constexpr int64_t kMenuId = 2;
constexpr int64_t kToolbarId = 3;
constexpr int64_t kOtherId = 5;
constexpr int64_t kMobileId = 6;

struct BookmarkRow {
  int64_t id;
  int type;
  int64_t parent;
  std::u16string title;
  int64_t date_added;
  GURL url;
};

base::Time FirefoxMicrosecondsToTime(int64_t microseconds) {
  if (microseconds <= 0) {
    return base::Time::Now();
  }
  return base::Time::UnixEpoch() + base::Microseconds(microseconds);
}

void BuildTree(
    int64_t parent_id,
    const std::map<int64_t, std::vector<BookmarkRow>>& children_by_parent,
    std::vector<ParsedBookmark>& out,
    int depth) {
  if (depth > kMaxRecursionDepth) {
    return;
  }

  auto it = children_by_parent.find(parent_id);
  if (it == children_by_parent.end()) {
    return;
  }

  for (const auto& row : it->second) {
    if (row.type == kBookmarkType) {
      if (!row.url.is_valid() || row.url.SchemeIs("javascript") ||
          row.url.SchemeIs("place")) {
        continue;
      }

      ParsedBookmark bookmark;
      bookmark.title = row.title.empty()
                           ? base::UTF8ToUTF16(row.url.host())
                           : row.title;
      bookmark.url = row.url;
      bookmark.is_folder = false;
      bookmark.date_added = FirefoxMicrosecondsToTime(row.date_added);
      out.push_back(std::move(bookmark));
    } else if (row.type == kFolderType) {
      ParsedBookmark folder;
      folder.title = row.title;
      folder.is_folder = true;
      folder.date_added = FirefoxMicrosecondsToTime(row.date_added);

      BuildTree(row.id, children_by_parent, folder.children, depth + 1);
      out.push_back(std::move(folder));
    }
  }
}

bool CopyDatabaseFiles(const base::FilePath& places_sqlite,
                       const base::FilePath& dest_dir) {
  base::FilePath dest_db = dest_dir.Append("places.sqlite");
  if (!base::CopyFile(places_sqlite, dest_db)) {
    return false;
  }

  base::FilePath wal = places_sqlite.AddExtension("wal");
  if (base::PathExists(wal)) {
    base::CopyFile(wal, dest_dir.Append("places.sqlite.wal"));
  }

  base::FilePath shm = places_sqlite.AddExtension("shm");
  if (base::PathExists(shm)) {
    base::CopyFile(shm, dest_dir.Append("places.sqlite.shm"));
  }

  return true;
}

}  // namespace

std::vector<ParsedBookmark> ParseFirefoxBookmarks(
    const base::FilePath& places_sqlite) {
  std::vector<ParsedBookmark> results;

  if (!base::PathExists(places_sqlite)) {
    return results;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return results;
  }

  if (!CopyDatabaseFiles(places_sqlite, temp_dir.GetPath())) {
    return results;
  }

  base::FilePath db_copy = temp_dir.GetPath().Append("places.sqlite");

  sql::Database db("FirefoxImporter");
  if (!db.Open(db_copy)) {
    return results;
  }

  static constexpr char kQuery[] =
      "SELECT b.id, b.type, b.parent, b.title, b.dateAdded, p.url "
      "FROM moz_bookmarks b "
      "LEFT JOIN moz_places p ON b.fk = p.id "
      "WHERE b.type IN (1, 2) "
      "ORDER BY b.parent, b.position";

  sql::Statement statement(db.GetUniqueStatement(kQuery));

  std::map<int64_t, std::vector<BookmarkRow>> children_by_parent;

  while (statement.Step()) {
    BookmarkRow row;
    row.id = statement.ColumnInt64(0);
    row.type = statement.ColumnInt(1);
    row.parent = statement.ColumnInt64(2);
    row.title = statement.ColumnString16(3);
    row.date_added = statement.ColumnInt64(4);
    std::string url_str = statement.ColumnString(5);
    if (!url_str.empty()) {
      row.url = GURL(url_str);
    }

    children_by_parent[row.parent].push_back(std::move(row));
  }

  if (!statement.Succeeded()) {
    return results;
  }

  constexpr int64_t kRootFolders[] = {kMenuId, kToolbarId, kOtherId, kMobileId};
  for (int64_t root_id : kRootFolders) {
    auto it = children_by_parent.find(kRootId);
    if (it == children_by_parent.end()) {
      continue;
    }

    for (const auto& row : it->second) {
      if (row.id == root_id && row.type == kFolderType) {
        ParsedBookmark folder;
        folder.title = row.title;
        folder.is_folder = true;
        folder.date_added = FirefoxMicrosecondsToTime(row.date_added);
        BuildTree(row.id, children_by_parent, folder.children, 1);
        results.push_back(std::move(folder));
        break;
      }
    }
  }

  return results;
}

}  // namespace maho
