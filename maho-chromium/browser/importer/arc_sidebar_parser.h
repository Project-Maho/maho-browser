// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_ARC_SIDEBAR_PARSER_H_
#define MAHO_BROWSER_IMPORTER_ARC_SIDEBAR_PARSER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "maho/browser/importer/zen_workspaces_parser.h"

namespace maho::importer {

struct ArcParseResult {
  ArcParseResult();
  ~ArcParseResult();
  ArcParseResult(ArcParseResult&&);
  ArcParseResult& operator=(ArcParseResult&&);

  std::vector<ParsedWorkspace> workspaces;
};

ArcParseResult ParseArcSidebar(const base::FilePath& arc_support_dir);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_ARC_SIDEBAR_PARSER_H_
