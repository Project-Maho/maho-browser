// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"

#include <memory>
#include <vector>

#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool ModelContainsCommand(const ui::SimpleMenuModel& model, int command_id) {
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (model.GetCommandIdAt(i) == command_id) {
      return true;
    }
  }
  return false;
}

struct ItemDesc {
  ui::MenuModel::ItemType type;
  int command_id;
};

std::vector<ItemDesc> FlattenTopLevel(const ui::SimpleMenuModel& model) {
  std::vector<ItemDesc> out;
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    out.push_back({model.GetTypeAt(i), model.GetCommandIdAt(i)});
  }
  return out;
}

// ---------------------------------------------------------------------------
// Command ID value assertions — live favorites range 57050-57052, 57057
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, CommandIdValues) {
  EXPECT_EQ(IDC_MAHO_FAV_OPEN_NEW_TAB, 57050);
  EXPECT_EQ(IDC_MAHO_FAV_OPEN_SPLIT,   57051);
  EXPECT_EQ(IDC_MAHO_FAV_COPY_LINK,    57052);
  EXPECT_EQ(IDC_MAHO_FAV_REMOVE,       57057);
  EXPECT_EQ(IDC_MAHO_FAV_BOOST,        57058);
  EXPECT_EQ(IDC_MAHO_FAV_RELOAD,       57059);
  EXPECT_EQ(IDC_MAHO_FAV_MUTE,         57060);
  EXPECT_EQ(IDC_MAHO_FAV_UNMUTE,       57061);
}

// ---------------------------------------------------------------------------
// BuildMenuModel — item presence
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsOpenNewTab) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_OPEN_NEW_TAB));
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsOpenSplit) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_OPEN_SPLIT));
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsCopyLink) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_COPY_LINK));
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsRemove) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_REMOVE));
}

// ---------------------------------------------------------------------------
// BuildMenuModel — top-level order and separator structure
// ---------------------------------------------------------------------------

// Expected top-level structure with an empty tab_id (no Move-to-Space submenu):
//  0  Open in New Tab       (COMMAND)
//  1  Open in Split View    (COMMAND)
//  2  Boost                 (COMMAND)
//  3  [separator]
//  4  Copy Link             (COMMAND)
//  5  Reload                (COMMAND)
//  6  Duplicate Tab         (COMMAND)
//  7  Mute Tab              (COMMAND)
//  8  [separator]
//  9  Remove from Favorites (COMMAND)
TEST(MahoFavoritesContextMenuTest, TopLevelOrderMatchesProduction) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  auto items = FlattenTopLevel(*model);
  ASSERT_EQ(items.size(), 10u);

  EXPECT_EQ(items[0].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[0].command_id, IDC_MAHO_FAV_OPEN_NEW_TAB);

  EXPECT_EQ(items[1].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[1].command_id, IDC_MAHO_FAV_OPEN_SPLIT);

  EXPECT_EQ(items[2].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[2].command_id, IDC_MAHO_FAV_BOOST);

  EXPECT_EQ(items[3].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[4].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[4].command_id, IDC_MAHO_FAV_COPY_LINK);

  EXPECT_EQ(items[5].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[5].command_id, IDC_MAHO_FAV_RELOAD);

  EXPECT_EQ(items[6].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[6].command_id, IDC_MAHO_FAV_DUPLICATE);

  EXPECT_EQ(items[7].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[7].command_id, IDC_MAHO_FAV_MUTE);

  EXPECT_EQ(items[8].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[9].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[9].command_id, IDC_MAHO_FAV_REMOVE);
}

TEST(MahoFavoritesContextMenuTest, TopLevelHasExactlyTwoSeparators) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  int sep_count = 0;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
      ++sep_count;
    }
  }
  EXPECT_EQ(sep_count, 2);
}

// ---------------------------------------------------------------------------
// IsCommandIdEnabled — URL-valid path
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, UrlCommandsEnabledWhenUrlIsValid) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_SPLIT));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_COPY_LINK));
}

TEST(MahoFavoritesContextMenuTest, UrlCommandsDisabledWhenUrlIsInvalid) {
  MahoFavoritesContextMenu menu(nullptr, GURL(), u"No URL", std::string());
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_SPLIT));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_COPY_LINK));
}

TEST(MahoFavoritesContextMenuTest, UnknownCommandIdIsDisabled) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_FALSE(menu.IsCommandIdEnabled(0));
  EXPECT_FALSE(menu.IsCommandIdEnabled(99999));
}

// ---------------------------------------------------------------------------
// IsCommandIdChecked — nothing is checked
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, NothingIsChecked) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FAV_COPY_LINK));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FAV_REMOVE));
}

// ---------------------------------------------------------------------------
// ExecuteCommand — null-safety for Remove
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, ExecuteRemoveWithNullBrowserDoesNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  menu.ExecuteCommand(IDC_MAHO_FAV_REMOVE, 0);
}

TEST(MahoFavoritesContextMenuTest, ExecuteUnknownCommandDoesNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  menu.ExecuteCommand(0, 0);
  menu.ExecuteCommand(99999, 0);
}

// ---------------------------------------------------------------------------
// BuildMenuModel — empty/invalid URL still returns a model
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, BuildMenuModelWithEmptyUrlReturnsModel) {
  MahoFavoritesContextMenu menu(nullptr, GURL(), u"No URL", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_COPY_LINK));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_REMOVE));
}

// ---------------------------------------------------------------------------
// IsCommandIdEnabled — Remove depends on tab_id
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, RemoveDisabledWhenTabIdEmpty) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"),
                                u"Example", std::string());
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_REMOVE));
}

TEST(MahoFavoritesContextMenuTest, RemoveEnabledWhenTabIdPresent) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"),
                                u"Example", "tab-abc-123");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_REMOVE));
}

TEST(MahoFavoritesContextMenuTest, RemoveEnabledWithInvalidUrlWhenTabIdPresent) {
  MahoFavoritesContextMenu menu(nullptr, GURL(), u"Example", "tab-abc-123");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_REMOVE));
}

TEST(MahoFavoritesContextMenuTest, ExecuteRemoveWithEmptyTabIdDoesNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"),
                                u"Example", std::string());
  menu.ExecuteCommand(IDC_MAHO_FAV_REMOVE, 0);
}

// ---------------------------------------------------------------------------
// Shared tab actions — present in the menu, gated on a live tab
// ---------------------------------------------------------------------------

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsSharedTabActions) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_BOOST));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_RELOAD));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_DUPLICATE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_MUTE));
}

// Without a live browser/tab strip the favorite cannot resolve to a tab, so the
// WebContents-backed actions are disabled even though they are shown.
TEST(MahoFavoritesContextMenuTest, SharedTabActionsDisabledWithoutLiveTab) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1");
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_BOOST));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_RELOAD));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_DUPLICATE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_MUTE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_UNMUTE));
}

TEST(MahoFavoritesContextMenuTest, ExecuteSharedTabActionsWithoutLiveTabDoNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1");
  menu.ExecuteCommand(IDC_MAHO_FAV_BOOST, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_RELOAD, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_DUPLICATE, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_MUTE, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_UNMUTE, 0);
}

}  // namespace
