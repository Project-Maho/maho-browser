// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_CHROMIUM_HISTORY_PARSER_H_
#define MAHO_BROWSER_IMPORTER_CHROMIUM_HISTORY_PARSER_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace maho {

struct ParsedHistoryRow {
  GURL url;
  std::u16string title;
  int visit_count = 0;
  base::Time last_visit;
};

// Parses a Chromium-format History SQLite file.
// |profile_path| should point to the profile directory (containing "History").
// MUST be called on a thread pool with base::MayBlock().
std::vector<ParsedHistoryRow> ParseChromiumHistory(
    const base::FilePath& profile_path);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_CHROMIUM_HISTORY_PARSER_H_
