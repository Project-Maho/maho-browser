// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_MAHO_BOOKMARK_IMPORTER_H_
#define MAHO_BROWSER_IMPORTER_MAHO_BOOKMARK_IMPORTER_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/importer/chromium_bookmark_parser.h"
#include "maho/browser/importer/maho_browser_detector.h"

#include "base/sequence_checker.h"

class Profile;

namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}  // namespace bookmarks

namespace maho {

// Callback signature: (status_message, is_complete)
using ImportProgressCallback =
    base::RepeatingCallback<void(const std::string&, bool)>;

class MahoBookmarkImporter {
 public:
  MahoBookmarkImporter();
  ~MahoBookmarkImporter();

  MahoBookmarkImporter(const MahoBookmarkImporter&) = delete;
  MahoBookmarkImporter& operator=(const MahoBookmarkImporter&) = delete;

  // Async. Calls |callback| repeatedly with progress, ending with
  // complete=true. |callback| fires on UI thread.
  // Caller must outlive the import (use weak ptr if needed).
  void ImportBookmarks(const DetectedBrowser& browser,
                       Profile* profile,
                       bool skip_if_workspace_active,
                       ImportProgressCallback callback);

  void WriteBookmarksToModel(bookmarks::BookmarkModel* model,
                             const bookmarks::BookmarkNode* parent,
                             const std::vector<ParsedBookmark>& bookmarks);

  bool is_running() const { return is_running_; }

 private:
  void OnParseComplete(Profile* profile,
                       ImportProgressCallback callback,
                       std::vector<ParsedBookmark> bookmarks);

  bool is_running_ = false;
  base::SequenceChecker sequence_checker_;
  base::WeakPtrFactory<MahoBookmarkImporter> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_MAHO_BOOKMARK_IMPORTER_H_
