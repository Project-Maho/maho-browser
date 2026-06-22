// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_SAFARI_BOOKMARK_PARSER_H_
#define MAHO_BROWSER_IMPORTER_SAFARI_BOOKMARK_PARSER_H_

#include <vector>

#include "base/files/file_path.h"
#include "maho/browser/importer/chromium_bookmark_parser.h"

namespace maho::importer {

enum class SafariParseStatus {
  kSuccess = 0,
  kFileNotFound = 1,
  kPermissionDenied = 2,
  kPlistParseFailed = 3,
  kUnsupportedPlatform = 4,
};

struct SafariParseResult {
  SafariParseResult();
  ~SafariParseResult();
  SafariParseResult(SafariParseResult&&);
  SafariParseResult& operator=(SafariParseResult&&);

  std::vector<maho::ParsedBookmark> bookmarks;
  bool fda_required = false;
  SafariParseStatus status = SafariParseStatus::kSuccess;
};

// Parses Safari's Bookmarks.plist using CoreFoundation.
// If Full Disk Access is not granted, returns fda_required=true.
SafariParseResult ParseSafariBookmarks(const base::FilePath& plist_path);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_SAFARI_BOOKMARK_PARSER_H_
