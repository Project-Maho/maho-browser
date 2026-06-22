// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/chromium_bookmark_parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"

namespace maho {

ParsedBookmark::ParsedBookmark() = default;
ParsedBookmark::~ParsedBookmark() = default;
ParsedBookmark::ParsedBookmark(const ParsedBookmark&) = default;
ParsedBookmark& ParsedBookmark::operator=(const ParsedBookmark&) = default;
ParsedBookmark::ParsedBookmark(ParsedBookmark&&) = default;
ParsedBookmark& ParsedBookmark::operator=(ParsedBookmark&&) = default;

namespace {

constexpr size_t kMaxFileSizeBytes = 100 * 1024 * 1024;
constexpr int kMaxRecursionDepth = 50;

base::Time ParseWindowsEpochMicroseconds(const std::string* date_str) {
  if (!date_str || date_str->empty()) {
    return base::Time::Now();
  }
  int64_t microseconds = 0;
  if (!base::StringToInt64(*date_str, &microseconds) || microseconds <= 0) {
    return base::Time::Now();
  }
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

bool IsImportableUrl(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }
  if (url.SchemeIs("javascript") || url.SchemeIs("chrome")) {
    return false;
  }
  return true;
}

void ParseNodeRecursive(const base::DictValue& node,
                        std::vector<ParsedBookmark>& out,
                        int depth) {
  if (depth > kMaxRecursionDepth) {
    return;
  }

  const std::string* type = node.FindString("type");
  if (!type) {
    return;
  }

  const std::string* name = node.FindString("name");
  std::u16string title =
      name ? base::UTF8ToUTF16(*name) : std::u16string();

  if (*type == "url") {
    const std::string* url_str = node.FindString("url");
    if (!url_str) {
      return;
    }
    GURL url(*url_str);
    if (!IsImportableUrl(url)) {
      return;
    }

    ParsedBookmark bookmark;
    bookmark.title = std::move(title);
    bookmark.url = std::move(url);
    bookmark.is_folder = false;
    bookmark.date_added =
        ParseWindowsEpochMicroseconds(node.FindString("date_added"));
    out.push_back(std::move(bookmark));
  } else if (*type == "folder") {
    ParsedBookmark folder;
    folder.title = std::move(title);
    folder.is_folder = true;
    folder.date_added =
        ParseWindowsEpochMicroseconds(node.FindString("date_added"));

    const base::ListValue* children = node.FindList("children");
    if (children) {
      for (const auto& child : *children) {
        const base::DictValue* child_dict = child.GetIfDict();
        if (child_dict) {
          ParseNodeRecursive(*child_dict, folder.children, depth + 1);
        }
      }
    }

    out.push_back(std::move(folder));
  }
}

void ParseRootFolder(const base::DictValue& roots,
                     const std::string& root_name,
                     std::vector<ParsedBookmark>& out) {
  const base::DictValue* root = roots.FindDict(root_name);
  if (!root) {
    return;
  }

  const base::ListValue* children = root->FindList("children");
  if (!children) {
    return;
  }

  for (const auto& child : *children) {
    const base::DictValue* child_dict = child.GetIfDict();
    if (child_dict) {
      ParseNodeRecursive(*child_dict, out, 1);
    }
  }
}

}  // namespace

std::vector<ParsedBookmark> ParseChromiumBookmarks(
    const base::FilePath& bookmarks_file) {
  std::vector<ParsedBookmark> results;

  std::string contents;
  if (!base::ReadFileToString(bookmarks_file, &contents)) {
    return results;
  }

  if (contents.size() > kMaxFileSizeBytes) {
    return results;
  }

  auto parsed = base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!parsed) {
    return results;
  }

  const base::DictValue* roots = parsed->FindDict("roots");
  if (!roots) {
    return results;
  }

  ParseRootFolder(*roots, "bookmark_bar", results);
  ParseRootFolder(*roots, "other", results);
  ParseRootFolder(*roots, "synced", results);

  return results;
}

}  // namespace maho
