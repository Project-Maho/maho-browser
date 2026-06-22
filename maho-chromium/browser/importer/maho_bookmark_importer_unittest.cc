// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/maho_bookmark_importer.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoBookmarkImporterTest, DeduplicateBookmarks) {
  base::test::TaskEnvironment task_environment;
  std::unique_ptr<bookmarks::BookmarkModel> model =
      bookmarks::TestBookmarkClient::CreateModel();

  MahoBookmarkImporter importer;
  const bookmarks::BookmarkNode* parent = model->bookmark_bar_node();

  // Create list of parsed bookmarks:
  // - A folder "Dev" containing "GitHub"
  // - A duplicate folder "Dev" containing "GitHub" (different title/url structure)
  // - A standalone bookmark "Google"
  // - A duplicate bookmark "Google"
  std::vector<ParsedBookmark> bookmarks;

  ParsedBookmark f1;
  f1.title = u"Dev";
  f1.is_folder = true;
  ParsedBookmark b1;
  b1.title = u"GitHub";
  b1.url = GURL("https://github.com");
  f1.children.push_back(std::move(b1));
  bookmarks.push_back(std::move(f1));

  ParsedBookmark f2;
  f2.title = u"Dev";
  f2.is_folder = true;
  ParsedBookmark b2;
  b2.title = u"GitHub";
  b2.url = GURL("https://github.com");
  ParsedBookmark b3;
  b3.title = u"StackOverflow";
  b3.url = GURL("https://stackoverflow.com");
  f2.children.push_back(std::move(b2));
  f2.children.push_back(std::move(b3));
  bookmarks.push_back(std::move(f2));

  ParsedBookmark b4;
  b4.title = u"Google";
  b4.url = GURL("https://google.com");
  bookmarks.push_back(std::move(b4));

  ParsedBookmark b5;
  b5.title = u"Google Duplicate";
  b5.url = GURL("https://google.com");
  bookmarks.push_back(std::move(b5));

  importer.WriteBookmarksToModel(model.get(), parent, bookmarks);

  // Verification:
  // - Should only have 1 folder named "Dev" on bookmark bar.
  // - Inside "Dev", should have "GitHub" and "StackOverflow" (total 2 bookmarks).
  // - Should have 1 bookmark named "Google" (since "Google Duplicate" shares same URL).
  // - Total children of bookmark bar: 2 (1 folder "Dev", 1 bookmark "Google").
  ASSERT_EQ(parent->children().size(), 2u);

  const bookmarks::BookmarkNode* dev_folder = nullptr;
  const bookmarks::BookmarkNode* google_node = nullptr;

  for (const auto& child : parent->children()) {
    if (child->is_folder()) {
      EXPECT_EQ(child->GetTitle(), u"Dev");
      dev_folder = child.get();
    } else {
      EXPECT_EQ(child->url().spec(), "https://google.com/");
      google_node = child.get();
    }
  }

  ASSERT_TRUE(dev_folder);
  ASSERT_TRUE(google_node);

  ASSERT_EQ(dev_folder->children().size(), 2u);
  EXPECT_EQ(dev_folder->children()[0]->GetTitle(), u"GitHub");
  EXPECT_EQ(dev_folder->children()[0]->url().spec(), "https://github.com/");
  EXPECT_EQ(dev_folder->children()[1]->GetTitle(), u"StackOverflow");
  EXPECT_EQ(dev_folder->children()[1]->url().spec(), "https://stackoverflow.com/");
}

TEST(MahoBookmarkImporterTest, WorkspaceSkipActive) {
  base::test::TaskEnvironment task_environment;
  MahoBookmarkImporter importer;

  DetectedBrowser browser;
  browser.type = BrowserType::kZen;
  browser.profile_path = base::FilePath(FILE_PATH_LITERAL("fake_path"));

  base::RunLoop run_loop;
  bool callback_called = false;
  std::string last_msg;

  ImportProgressCallback callback = base::BindRepeating(
      [](base::RunLoop* loop, bool* called, std::string* out_msg,
         const std::string& msg, bool complete) {
        *called = true;
        *out_msg = msg;
        if (complete) {
          loop->Quit();
        }
      },
      &run_loop, &callback_called, &last_msg);

  importer.ImportBookmarks(browser, nullptr, /*skip_if_workspace_active=*/true, callback);
  run_loop.Run();

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(last_msg, "Bookmarks skipped (imported with workspaces).");
  EXPECT_FALSE(importer.is_running());
}

}  // namespace
}  // namespace maho
