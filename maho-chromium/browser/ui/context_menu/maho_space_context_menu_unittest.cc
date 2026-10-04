// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_space_context_menu.h"

#include <memory>
#include <vector>

#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"

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

std::vector<ItemDesc> FlattenModel(const ui::SimpleMenuModel& model) {
  std::vector<ItemDesc> out;
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    out.push_back({model.GetTypeAt(i), model.GetCommandIdAt(i)});
  }
  return out;
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsRename) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_RENAME));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsChangeIcon) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_CHANGE_ICON));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsEditTheme) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_EDIT_THEME));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsNewFolder) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_NEW_FOLDER));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsShare) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_SHARE));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsExportTheme) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_EXPORT_THEME));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsImportTheme) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_IMPORT_THEME));
}

TEST(MahoSpaceContextMenuTest, BuildMenuModelContainsDelete) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_DELETE));
}

TEST(MahoSpaceContextMenuTest, TopLevelOrderMatchesProduction) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  auto items = FlattenModel(*model);
  ASSERT_EQ(items.size(), 11u);

  EXPECT_EQ(items[0].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[0].command_id, IDC_MAHO_SPACE_RENAME);

  EXPECT_EQ(items[1].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[1].command_id, IDC_MAHO_SPACE_CHANGE_ICON);

  EXPECT_EQ(items[2].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[2].command_id, IDC_MAHO_SPACE_EDIT_THEME);

  EXPECT_EQ(items[3].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[4].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[4].command_id, IDC_MAHO_SPACE_NEW_FOLDER);

  EXPECT_EQ(items[5].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[6].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[6].command_id, IDC_MAHO_SPACE_SHARE);

  EXPECT_EQ(items[7].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[7].command_id, IDC_MAHO_SPACE_EXPORT_THEME);

  EXPECT_EQ(items[8].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[8].command_id, IDC_MAHO_SPACE_IMPORT_THEME);

  EXPECT_EQ(items[9].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[10].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[10].command_id, IDC_MAHO_SPACE_DELETE);
}

TEST(MahoSpaceContextMenuTest, TopLevelHasExactlyThreeSeparators) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  int sep_count = 0;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
      ++sep_count;
    }
  }
  EXPECT_EQ(sep_count, 3);
}

TEST(MahoSpaceContextMenuTest, AllCommandsEnabledRegardlessOfBrowser) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_RENAME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_CHANGE_ICON));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_EDIT_THEME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_NEW_FOLDER));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_SHARE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_EXPORT_THEME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_IMPORT_THEME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_DELETE));
}

TEST(MahoSpaceContextMenuTest, NothingIsChecked) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_SPACE_RENAME));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_SPACE_DELETE));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_SPACE_SHARE));
}

TEST(MahoSpaceContextMenuTest, ExecuteNewFolderWithNullBrowserDoesNotCrash) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  menu.ExecuteCommand(IDC_MAHO_SPACE_NEW_FOLDER, 0);
}

TEST(MahoSpaceContextMenuTest, ExecuteShareWithNullBrowserDoesNotCrash) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  menu.ExecuteCommand(IDC_MAHO_SPACE_SHARE, 0);
}

TEST(MahoSpaceContextMenuTest, ExecuteDeleteWithNullBrowserDoesNotCrash) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  menu.ExecuteCommand(IDC_MAHO_SPACE_DELETE, 0);
}

TEST(MahoSpaceContextMenuTest, ExecuteDeleteNullBrowserIsHandledByDialogGuard) {
  MahoSpaceContextMenu menu(nullptr, "space-delete-guard");
  menu.ExecuteCommand(IDC_MAHO_SPACE_DELETE, 0);
}

TEST(MahoSpaceContextMenuTest, ExecuteDeleteEmptySpaceIdWithNullBrowserDoesNotCrash) {
  MahoSpaceContextMenu menu(nullptr, "");
  menu.ExecuteCommand(IDC_MAHO_SPACE_DELETE, 0);
}

TEST(MahoSpaceContextMenuTest, EmptySpaceIdDoesNotCrashOnBuild) {
  MahoSpaceContextMenu menu(nullptr, "");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_RENAME));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_SPACE_DELETE));
}

TEST(MahoSpaceContextMenuTest, CommandIdValues) {
  EXPECT_EQ(IDC_MAHO_SPACE_RENAME, 57030);
  EXPECT_EQ(IDC_MAHO_SPACE_CHANGE_ICON, 57031);
  EXPECT_EQ(IDC_MAHO_SPACE_EDIT_THEME, 57032);
  EXPECT_EQ(IDC_MAHO_SPACE_NEW_FOLDER, 57033);
  EXPECT_EQ(IDC_MAHO_SPACE_SHARE, 57035);
  EXPECT_EQ(IDC_MAHO_SPACE_DELETE, 57036);
  EXPECT_EQ(IDC_MAHO_SPACE_EXPORT_THEME, 57037);
  EXPECT_EQ(IDC_MAHO_SPACE_IMPORT_THEME, 57038);
}

}  // namespace
