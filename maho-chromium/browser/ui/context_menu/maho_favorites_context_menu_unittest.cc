// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_favorites_context_menu.h"

#include <memory>
#include <vector>

#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

namespace {

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

class TestFavoritesMenuDelegate : public MahoFavoritesContextMenu::Delegate {
 public:
  void OnFavoriteShareRequested() override { share_called_ = true; }
  void OnFavoriteRenameRequested() override { rename_called_ = true; }
  void OnFavoriteIconChangeRequested() override { icon_called_ = true; }
  void OnFavoritePinnedUrlEditRequested() override { pinned_edit_called_ = true; }

  bool share_called_ = false;
  bool rename_called_ = false;
  bool icon_called_ = false;
  bool pinned_edit_called_ = false;
};

class TestableFavoritesMenu : public MahoFavoritesContextMenu {
 public:
  using MahoFavoritesContextMenu::MahoFavoritesContextMenu;

  void set_live_tab(bool live_tab) { live_tab_ = live_tab; }
  int toast_count() const { return toast_count_; }

 protected:
  bool HasLiveTab() const override { return live_tab_; }
  void NotifyLinkCopied() override { ++toast_count_; }

 private:
  bool live_tab_ = false;
  int toast_count_ = 0;
};

TEST(MahoFavoritesContextMenuTest, CommandIdValues) {
  EXPECT_EQ(IDC_MAHO_FAV_OPEN_NEW_TAB, 57050);
  EXPECT_EQ(IDC_MAHO_FAV_OPEN_SPLIT,   57051);
  EXPECT_EQ(IDC_MAHO_FAV_COPY_LINK,    57052);
  EXPECT_EQ(IDC_MAHO_FAV_RENAME,       57053);
  EXPECT_EQ(IDC_MAHO_FAV_CHANGE_ICON,  57054);
  EXPECT_EQ(IDC_MAHO_FAV_MOVE_TO_SPACE, 57055);
  EXPECT_EQ(IDC_MAHO_FAV_DUPLICATE,    57056);
  EXPECT_EQ(IDC_MAHO_FAV_REMOVE,       57057);
  EXPECT_EQ(IDC_MAHO_FAV_BOOST,        57058);
  EXPECT_EQ(IDC_MAHO_FAV_RELOAD,       57059);
  EXPECT_EQ(IDC_MAHO_FAV_MUTE,         57060);
  EXPECT_EQ(IDC_MAHO_FAV_UNMUTE,       57061);
  EXPECT_EQ(IDC_MAHO_FAV_SHARE,        57062);
  EXPECT_EQ(IDC_MAHO_FAV_EDIT_PINNED_REPLACE, 57063);
  EXPECT_EQ(IDC_MAHO_FAV_EDIT_PINNED_EDIT,    57064);
  EXPECT_EQ(IDC_MAHO_FAV_EDIT_PINNED,         57065);
  EXPECT_EQ(IDC_MAHO_FAV_MOVE_FOLDER_BASE,    57400);
  EXPECT_EQ(IDC_MAHO_FAV_MOVE_FOLDER_MAX,     57499);
}

TEST(MahoFavoritesContextMenuTest, MoveToFolderHelpers) {
  EXPECT_TRUE(IsMoveToFolderCommand(IDC_MAHO_FAV_MOVE_FOLDER_BASE));
  EXPECT_TRUE(IsMoveToFolderCommand(IDC_MAHO_FAV_MOVE_FOLDER_BASE + 42));
  EXPECT_TRUE(IsMoveToFolderCommand(IDC_MAHO_FAV_MOVE_FOLDER_MAX));
  EXPECT_FALSE(IsMoveToFolderCommand(IDC_MAHO_FAV_MOVE_FOLDER_BASE - 1));
  EXPECT_FALSE(IsMoveToFolderCommand(IDC_MAHO_FAV_MOVE_FOLDER_MAX + 1));
  EXPECT_EQ(MoveToFolderIndex(IDC_MAHO_FAV_MOVE_FOLDER_BASE + 7), 7);
}

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

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsShare) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_SHARE));
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsChangeIcon) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_CHANGE_ICON));
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsRename) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_RENAME));
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsEditPinnedPageSubmenuAndChildren) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_EDIT_PINNED));

  ui::MenuModel* sub = nullptr;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetCommandIdAt(i) == IDC_MAHO_FAV_EDIT_PINNED) {
      sub = model->GetSubmenuModelAt(i);
      break;
    }
  }
  ASSERT_NE(sub, nullptr);
  ASSERT_EQ(sub->GetItemCount(), 2u);
  EXPECT_EQ(sub->GetCommandIdAt(0), IDC_MAHO_FAV_EDIT_PINNED_REPLACE);
  EXPECT_EQ(sub->GetCommandIdAt(1), IDC_MAHO_FAV_EDIT_PINNED_EDIT);
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsRemove) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_REMOVE));
}

TEST(MahoFavoritesContextMenuTest, TopLevelOrderMatchesProductionWithoutTabId) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  auto items = FlattenTopLevel(*model);
  ASSERT_EQ(items.size(), 16u);

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
  EXPECT_EQ(items[5].command_id, IDC_MAHO_FAV_SHARE);

  EXPECT_EQ(items[6].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[7].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[7].command_id, IDC_MAHO_FAV_CHANGE_ICON);

  EXPECT_EQ(items[8].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[8].command_id, IDC_MAHO_FAV_RENAME);

  EXPECT_EQ(items[9].type, ui::MenuModel::TYPE_SUBMENU);
  EXPECT_EQ(items[9].command_id, IDC_MAHO_FAV_EDIT_PINNED);

  EXPECT_EQ(items[10].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[11].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[11].command_id, IDC_MAHO_FAV_RELOAD);

  EXPECT_EQ(items[12].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[12].command_id, IDC_MAHO_FAV_DUPLICATE);

  EXPECT_EQ(items[13].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[13].command_id, IDC_MAHO_FAV_MUTE);

  EXPECT_EQ(items[14].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[15].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[15].command_id, IDC_MAHO_FAV_REMOVE);
}

TEST(MahoFavoritesContextMenuTest, TopLevelOrderMatchesProductionWithTabId) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "t1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  auto items = FlattenTopLevel(*model);
  ASSERT_EQ(items.size(), 18u);

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
  EXPECT_EQ(items[5].command_id, IDC_MAHO_FAV_SHARE);

  EXPECT_EQ(items[6].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[7].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[7].command_id, IDC_MAHO_FAV_CHANGE_ICON);

  EXPECT_EQ(items[8].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[8].command_id, IDC_MAHO_FAV_RENAME);

  EXPECT_EQ(items[9].type, ui::MenuModel::TYPE_SUBMENU);
  EXPECT_EQ(items[9].command_id, IDC_MAHO_FAV_EDIT_PINNED);

  EXPECT_EQ(items[10].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[11].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[11].command_id, IDC_MAHO_FAV_RELOAD);

  EXPECT_EQ(items[12].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[12].command_id, IDC_MAHO_FAV_DUPLICATE);

  EXPECT_EQ(items[13].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[13].command_id, IDC_MAHO_FAV_MUTE);

  EXPECT_EQ(items[14].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[15].type, ui::MenuModel::TYPE_SUBMENU);
  EXPECT_EQ(items[15].command_id, IDC_MAHO_FAV_MOVE_TO_SPACE);

  EXPECT_EQ(items[16].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[17].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[17].command_id, IDC_MAHO_FAV_REMOVE);
}

TEST(MahoFavoritesContextMenuTest, TopLevelSeparatorCounts) {
  {
    MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
    std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
    ASSERT_NE(model, nullptr);

    int sep_count = 0;
    for (size_t i = 0; i < model->GetItemCount(); ++i) {
      if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
        ++sep_count;
      }
    }
    EXPECT_EQ(sep_count, 4);
  }
  {
    MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "t1");
    std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
    ASSERT_NE(model, nullptr);

    int sep_count = 0;
    for (size_t i = 0; i < model->GetItemCount(); ++i) {
      if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
        ++sep_count;
      }
    }
    EXPECT_EQ(sep_count, 5);
  }
}

TEST(MahoFavoritesContextMenuTest, UrlCommandsEnabledWhenUrlIsValid) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_SPLIT));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_COPY_LINK));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_SHARE));
}

TEST(MahoFavoritesContextMenuTest, UrlCommandsDisabledWhenUrlIsInvalid) {
  MahoFavoritesContextMenu menu(nullptr, GURL(), u"No URL", "tab-1");
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_OPEN_SPLIT));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_COPY_LINK));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_SHARE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED_REPLACE));
}

TEST(MahoFavoritesContextMenuTest, TabIdDependentCommandsDisabledWhenTabIdEmpty) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_RENAME));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_CHANGE_ICON));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED_EDIT));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_REMOVE));
}

TEST(MahoFavoritesContextMenuTest, TabIdSetWithoutLiveTabEnablesMetadataActionsAndDisablesLiveTabActions) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_RENAME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_CHANGE_ICON));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED_EDIT));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_REMOVE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_SHARE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_COPY_LINK));

  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_RELOAD));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_MUTE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_UNMUTE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED_REPLACE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_BOOST));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_DUPLICATE));
}

TEST(MahoFavoritesContextMenuTest, UnknownCommandIdIsDisabled) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_FALSE(menu.IsCommandIdEnabled(0));
  EXPECT_FALSE(menu.IsCommandIdEnabled(99999));
}

TEST(MahoFavoritesContextMenuTest, NothingIsChecked) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FAV_COPY_LINK));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FAV_REMOVE));
}

TEST(MahoFavoritesContextMenuTest, ExecuteRemoveWithNullBrowserDoesNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  menu.ExecuteCommand(IDC_MAHO_FAV_REMOVE, 0);
}

TEST(MahoFavoritesContextMenuTest, ExecuteUnknownCommandDoesNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", std::string());
  menu.ExecuteCommand(0, 0);
  menu.ExecuteCommand(99999, 0);
}

TEST(MahoFavoritesContextMenuTest, BuildMenuModelWithEmptyUrlReturnsModel) {
  MahoFavoritesContextMenu menu(nullptr, GURL(), u"No URL", std::string());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_OPEN_NEW_TAB));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_COPY_LINK));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_REMOVE));
}

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

TEST(MahoFavoritesContextMenuTest, BuildMenuModelContainsSharedTabActions) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_BOOST));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_RELOAD));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_DUPLICATE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FAV_MUTE));
}

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

TEST(MahoFavoritesContextMenuTest, ExecuteWithNullDelegateAndBrowserDoesNotCrash) {
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1", nullptr);
  menu.ExecuteCommand(IDC_MAHO_FAV_SHARE, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_RENAME, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_CHANGE_ICON, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_EDIT_PINNED_EDIT, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_EDIT_PINNED_REPLACE, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_MOVE_FOLDER_BASE, 0);
  menu.ExecuteCommand(IDC_MAHO_FAV_MOVE_FOLDER_BASE + 5, 0);
}

TEST(MahoFavoritesContextMenuTest, ExecuteCommandsInvokesDelegate) {
  TestFavoritesMenuDelegate delegate;
  MahoFavoritesContextMenu menu(nullptr, GURL("https://example.com"), u"Example", "tab-1", &delegate);
  menu.ExecuteCommand(IDC_MAHO_FAV_SHARE, 0);
  EXPECT_TRUE(delegate.share_called_);
  menu.ExecuteCommand(IDC_MAHO_FAV_RENAME, 0);
  EXPECT_TRUE(delegate.rename_called_);
  menu.ExecuteCommand(IDC_MAHO_FAV_CHANGE_ICON, 0);
  EXPECT_TRUE(delegate.icon_called_);
  menu.ExecuteCommand(IDC_MAHO_FAV_EDIT_PINNED_EDIT, 0);
  EXPECT_TRUE(delegate.pinned_edit_called_);
}

TEST(MahoFavoritesContextMenuTest, LiveTabEnablesAllTabActions) {
  TestableFavoritesMenu menu(nullptr, GURL("https://example.com"), u"Example",
                             "tab-1");
  menu.set_live_tab(true);
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_BOOST));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_RELOAD));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_DUPLICATE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_MUTE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_UNMUTE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FAV_EDIT_PINNED_REPLACE));
}

TEST(MahoFavoritesContextMenuTest, ExecuteCopyLinkInvokesSharedToastSeam) {
  TestableFavoritesMenu menu(nullptr, GURL("https://example.com"), u"Example",
                             "tab-1");
  menu.ExecuteCommand(IDC_MAHO_FAV_COPY_LINK, 0);
  EXPECT_EQ(menu.toast_count(), 1);
}

}  // namespace
