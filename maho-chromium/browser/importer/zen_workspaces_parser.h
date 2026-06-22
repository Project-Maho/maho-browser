// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_ZEN_WORKSPACES_PARSER_H_
#define MAHO_BROWSER_IMPORTER_ZEN_WORKSPACES_PARSER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace maho::importer {

struct ParsedTab {
  ParsedTab();
  ~ParsedTab();
  ParsedTab(ParsedTab&&);
  ParsedTab& operator=(ParsedTab&&);

  std::string url;
  std::string title;
  bool is_pinned = false;
  bool is_essential = false;
};

struct ParsedFolder {
  ParsedFolder();
  ~ParsedFolder();
  ParsedFolder(ParsedFolder&&);
  ParsedFolder& operator=(ParsedFolder&&);

  std::string id;
  std::string name;
  std::string parent_id;
  std::string workspace_id;
  std::vector<ParsedTab> tabs;
};

enum class ZenParseStatus {
  kSuccess = 0,
  kDecompressFailed = 1,
  kJsonParseFailed = 2,
  kMissingRequiredFields = 3,
};

struct ParsedWorkspace {
  ParsedWorkspace();
  ~ParsedWorkspace();
  ParsedWorkspace(ParsedWorkspace&&);
  ParsedWorkspace& operator=(ParsedWorkspace&&);

  std::string uuid;
  std::string name;
  std::string icon;
  std::string theme_color_hex;
  int container_id = 0;
  std::vector<ParsedTab> pinned_tabs;
  std::vector<ParsedTab> favorites;
  std::vector<ParsedTab> regular_tabs;
  std::vector<ParsedFolder> folders;
};

struct ZenParseResult {
  ZenParseResult();
  ~ZenParseResult();
  ZenParseResult(ZenParseResult&&);
  ZenParseResult& operator=(ZenParseResult&&);

  ZenParseStatus status = ZenParseStatus::kSuccess;
  std::vector<ParsedWorkspace> workspaces;
  std::string active_workspace_uuid;
};

ZenParseResult ParseZenWorkspaces(const base::FilePath& profile_dir);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_ZEN_WORKSPACES_PARSER_H_
