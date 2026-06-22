// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_FIREFOX_HISTORY_PARSER_H_
#define MAHO_BROWSER_IMPORTER_FIREFOX_HISTORY_PARSER_H_

#include <vector>

#include "base/files/file_path.h"
#include "maho/browser/importer/chromium_history_parser.h"

namespace maho {

// |places_sqlite| should point to the places.sqlite file.
// MUST be called on a thread pool with base::MayBlock().
std::vector<ParsedHistoryRow> ParseFirefoxHistory(
    const base::FilePath& places_sqlite);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_FIREFOX_HISTORY_PARSER_H_
