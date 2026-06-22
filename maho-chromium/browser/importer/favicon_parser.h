// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_FAVICON_PARSER_H_
#define MAHO_BROWSER_IMPORTER_FAVICON_PARSER_H_

#include <vector>

#include "base/files/file_path.h"
#include "components/favicon_base/favicon_usage_data.h"

namespace maho::importer {

// Parses favicons from a Chromium-family browser's "Favicons" SQLite DB.
// Returns FaviconUsageData suitable for FaviconService::SetImportedFavicons().
favicon_base::FaviconUsageDataList ParseChromiumFavicons(
    const base::FilePath& favicons_db_path);

// Parses favicons from Firefox/Zen favicons.sqlite.
favicon_base::FaviconUsageDataList ParseFirefoxFavicons(
    const base::FilePath& favicons_db_path);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_FAVICON_PARSER_H_
