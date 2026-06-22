// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_CHROMIUM_BOOKMARK_PARSER_H_
#define MAHO_BROWSER_IMPORTER_CHROMIUM_BOOKMARK_PARSER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace maho {

struct ParsedBookmark {
  ParsedBookmark();
  ~ParsedBookmark();
  ParsedBookmark(const ParsedBookmark&);
  ParsedBookmark& operator=(const ParsedBookmark&);
  ParsedBookmark(ParsedBookmark&&);
  ParsedBookmark& operator=(ParsedBookmark&&);

  std::u16string title;
  GURL url;                    // empty for folders
  bool is_folder = false;
  std::vector<ParsedBookmark> children;
  base::Time date_added;
};

// Parses a Chromium-format Bookmarks JSON file.
// MUST be called on a thread pool with base::MayBlock().
// Returns empty vector on parse error or missing file.
std::vector<ParsedBookmark> ParseChromiumBookmarks(
    const base::FilePath& bookmarks_file);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_CHROMIUM_BOOKMARK_PARSER_H_
