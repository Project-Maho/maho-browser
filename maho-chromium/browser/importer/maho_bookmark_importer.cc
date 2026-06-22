// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_bookmark_importer.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "maho/browser/importer/firefox_places_parser.h"
#include "maho/browser/importer/safari_bookmark_parser.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

namespace {

void ParseBookmarkNodeFromJson(const base::Value& val,
                               std::vector<ParsedBookmark>& out) {
  const auto* d = val.GetIfDict();
  if (!d) return;
  ParsedBookmark b;
  if (auto* s = d->FindString("title")) b.title = base::UTF8ToUTF16(*s);
  if (auto* s = d->FindString("url")) b.url = GURL(*s);
  if (auto f = d->FindBool("is_folder")) b.is_folder = *f;
  if (const auto* children = d->FindList("children")) {
    for (const auto& child : *children) {
      ParseBookmarkNodeFromJson(child, b.children);
    }
  }
  out.push_back(std::move(b));
}

std::vector<ParsedBookmark> ParseBookmarksViaRust(
    const base::FilePath& profile_path,
    char* (*ffi_fn)(const char*)) {
  std::vector<ParsedBookmark> result;
  std::string dir_utf8 = profile_path.DirName().AsUTF8Unsafe();
  char* json = ffi_fn(dir_utf8.c_str());
  if (!json) return result;
  std::string json_str(json);
  maho_import_string_free(json);

  auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return result;
  for (const auto& v : parsed->GetList()) {
    ParseBookmarkNodeFromJson(v, result);
  }
  return result;
}

std::vector<ParsedBookmark> ParseChromiumBookmarksViaRust(
    const base::FilePath& profile_path) {
  return ParseBookmarksViaRust(profile_path, &maho_import_parse_chromium_bookmarks);
}

std::vector<ParsedBookmark> ParseFirefoxBookmarksViaRust(
    const base::FilePath& profile_path) {
  return ParseBookmarksViaRust(profile_path, &maho_import_parse_firefox_bookmarks);
}

importer::SafariParseResult ParseSafariBookmarksViaRust(
    const base::FilePath& profile_path) {
  importer::SafariParseResult result;
  std::string dir_utf8 = profile_path.DirName().AsUTF8Unsafe();
  char* json = maho_import_parse_safari_bookmarks(dir_utf8.c_str());
  if (!json) {
    result.fda_required = true;
    return result;
  }
  std::string json_str(json);
  maho_import_string_free(json);

  auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return result;
  for (const auto& v : parsed->GetList()) {
    ParseBookmarkNodeFromJson(v, result.bookmarks);
  }
  return result;
}

}  // namespace

namespace {

size_t CountBookmarks(const std::vector<ParsedBookmark>& bookmarks) {
  size_t count = 0;
  for (const auto& b : bookmarks) {
    if (b.is_folder) {
      count += CountBookmarks(b.children);
    } else {
      ++count;
    }
  }
  return count;
}

bool IsDuplicateURL(const bookmarks::BookmarkNode* parent, const GURL& url) {
  for (const auto& child : parent->children()) {
    if (!child->is_folder() && child->url() == url) {
      return true;
    }
  }
  return false;
}

const bookmarks::BookmarkNode* FindExistingFolder(const bookmarks::BookmarkNode* parent, const std::u16string& title) {
  for (const auto& child : parent->children()) {
    if (child->is_folder() && child->GetTitle() == title) {
      return child.get();
    }
  }
  return nullptr;
}

}  // namespace

MahoBookmarkImporter::MahoBookmarkImporter() = default;
MahoBookmarkImporter::~MahoBookmarkImporter() = default;

void MahoBookmarkImporter::ImportBookmarks(const DetectedBrowser& browser,
                                            Profile* profile,
                                            bool skip_if_workspace_active,
                                            ImportProgressCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_running_ = true;

  if (skip_if_workspace_active &&
      (browser.type == BrowserType::kZen || browser.type == BrowserType::kArc)) {
    callback.Run("Bookmarks skipped (imported with workspaces).", true);
    is_running_ = false;
    return;
  }

  if (browser.type == BrowserType::kSafari) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ParseSafariBookmarksViaRust, browser.profile_path),
        base::BindOnce(
            [](base::WeakPtr<MahoBookmarkImporter> self, Profile* profile,
               ImportProgressCallback cb,
               importer::SafariParseResult parse_result) {
               if (!self) {
                 return;
               }
               DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
               if (parse_result.fda_required) {
                 cb.Run("Grant Full Disk Access in System Settings to import Safari.", true);
                 self->is_running_ = false;
                 return;
               }
               self->OnParseComplete(profile, cb,
                                     std::move(parse_result.bookmarks));
            },
            weak_factory_.GetWeakPtr(), profile, callback));
    return;
  }

  base::OnceCallback<std::vector<ParsedBookmark>()> parse_task;

  switch (browser.type) {
    case BrowserType::kChrome:
    case BrowserType::kArc:
    case BrowserType::kBrave:
    case BrowserType::kEdge:
    case BrowserType::kVivaldi:
    case BrowserType::kOpera:
      parse_task =
          base::BindOnce(&ParseChromiumBookmarksViaRust, browser.profile_path);
      break;
    case BrowserType::kFirefox:
    case BrowserType::kZen:
      parse_task =
          base::BindOnce(&ParseFirefoxBookmarksViaRust, browser.profile_path);
      break;
    case BrowserType::kSafari:
      NOTREACHED();
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      std::move(parse_task),
      base::BindOnce(&MahoBookmarkImporter::OnParseComplete,
                     weak_factory_.GetWeakPtr(), profile,
                     callback));

  callback.Run("Import started\xe2\x80\xa6", false);
}

void MahoBookmarkImporter::OnParseComplete(
    Profile* profile,
    ImportProgressCallback callback,
    std::vector<ParsedBookmark> bookmarks) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (bookmarks.empty()) {
    callback.Run("No bookmarks found.", true);
    is_running_ = false;
    return;
  }

  bookmarks::BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile);
  if (!model) {
    callback.Run("Bookmark storage unavailable.", true);
    is_running_ = false;
    return;
  }

  if (!model->loaded()) {
    callback.Run("Bookmark storage not ready.", true);
    is_running_ = false;
    return;
  }

  callback.Run("Importing bookmarks\xe2\x80\xa6", false);

  model->BeginExtensiveChanges();

  const bookmarks::BookmarkNode* bar = model->bookmark_bar_node();

  // Merge directly into bookmark bar — no "Imported from X" wrapper folder.
  WriteBookmarksToModel(model, bar, bookmarks);
  model->EndExtensiveChanges();

  size_t count = CountBookmarks(bookmarks);
  callback.Run("Imported " + std::to_string(count) + " bookmarks.", true);
  is_running_ = false;
}

void MahoBookmarkImporter::WriteBookmarksToModel(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* parent,
    const std::vector<ParsedBookmark>& bookmarks) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const auto& b : bookmarks) {
    if (b.is_folder) {
      const bookmarks::BookmarkNode* folder = FindExistingFolder(parent, b.title);
      if (!folder) {
        folder = model->AddFolder(parent, parent->children().size(), b.title);
      }
      WriteBookmarksToModel(model, folder, b.children);
    } else {
      if (!IsDuplicateURL(parent, b.url)) {
        model->AddURL(parent, parent->children().size(), b.title, b.url);
      }
    }
  }
}

}  // namespace maho
