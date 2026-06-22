// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_FIREFOX_PLACES_PARSER_H_
#define MAHO_BROWSER_IMPORTER_FIREFOX_PLACES_PARSER_H_

#include <vector>

#include "base/files/file_path.h"
#include "maho/browser/importer/chromium_bookmark_parser.h"  // for ParsedBookmark

namespace maho {

// Parses a Firefox/Zen places.sqlite file.
// MUST be called on a thread pool with base::MayBlock().
// Returns empty vector on parse error or missing file.
std::vector<ParsedBookmark> ParseFirefoxBookmarks(
    const base::FilePath& places_sqlite);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_FIREFOX_PLACES_PARSER_H_
