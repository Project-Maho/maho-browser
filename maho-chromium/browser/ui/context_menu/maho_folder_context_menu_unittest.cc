// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_folder_context_menu.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
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

std::vector<ItemDesc> FlattenTopLevel(const ui::SimpleMenuModel& model) {
  std::vector<ItemDesc> out;
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    out.push_back({model.GetTypeAt(i), model.GetCommandIdAt(i)});
  }
  return out;
}

TEST(MahoFolderContextMenuTest, CommandIdValues) {
  EXPECT_EQ(IDC_MAHO_FOLDER_RENAME,   57070);
  EXPECT_EQ(IDC_MAHO_FOLDER_DELETE,   57074);
}

TEST(MahoFolderContextMenuTest, BuildMenuModelIsNonNull) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
}

TEST(MahoFolderContextMenuTest, BuildMenuModelContainsRename) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_RENAME));
}

TEST(MahoFolderContextMenuTest, BuildMenuModelContainsDelete) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_DELETE));
}

TEST(MahoFolderContextMenuTest, TopLevelOrderMatchesProduction) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  auto items = FlattenTopLevel(*model);
  ASSERT_EQ(items.size(), 5u);

  EXPECT_EQ(items[0].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[0].command_id, IDC_MAHO_FOLDER_RENAME);

  EXPECT_EQ(items[1].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[2].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[2].command_id, IDC_MAHO_FOLDER_CONVERT_TO_SPACE);

  EXPECT_EQ(items[3].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[4].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[4].command_id, IDC_MAHO_FOLDER_DELETE);
}

TEST(MahoFolderContextMenuTest, TopLevelHasExactlyTwoSeparators) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
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

TEST(MahoFolderContextMenuTest, TopLevelHasThreeCommandItems) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  int cmd_count = 0;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetTypeAt(i) == ui::MenuModel::TYPE_COMMAND) {
      ++cmd_count;
    }
  }
  EXPECT_EQ(cmd_count, 3);
}

TEST(MahoFolderContextMenuTest, BuildMenuModelWithEmptyFolderIdReturnsModel) {
  MahoFolderContextMenu menu(nullptr, "");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_RENAME));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_CONVERT_TO_SPACE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_DELETE));
}

TEST(MahoFolderContextMenuTest, AllCommandsEnabledWithNullBrowser) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FOLDER_RENAME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FOLDER_CONVERT_TO_SPACE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_FOLDER_DELETE));
}

TEST(MahoFolderContextMenuTest, UnknownCommandIdIsEnabled) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  EXPECT_TRUE(menu.IsCommandIdEnabled(0));
  EXPECT_TRUE(menu.IsCommandIdEnabled(99999));
}

TEST(MahoFolderContextMenuTest, NothingIsChecked) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FOLDER_RENAME));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FOLDER_CONVERT_TO_SPACE));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_FOLDER_DELETE));
}

TEST(MahoFolderContextMenuTest, ExecuteRenameWithNullBrowserDoesNotCrash) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  menu.ExecuteCommand(IDC_MAHO_FOLDER_RENAME, 0);
}

TEST(MahoFolderContextMenuTest, ExecuteConvertToSpaceWithNullBrowserDoesNotCrash) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  menu.ExecuteCommand(IDC_MAHO_FOLDER_CONVERT_TO_SPACE, 0);
}

TEST(MahoFolderContextMenuTest, ExecuteDeleteWithNullBrowserDoesNotCrash) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  menu.ExecuteCommand(IDC_MAHO_FOLDER_DELETE, 0);
}

TEST(MahoFolderContextMenuTest, ExecuteUnknownCommandDoesNotCrash) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  menu.ExecuteCommand(0, 0);
  menu.ExecuteCommand(99999, 0);
}

TEST(MahoFolderContextMenuTest, ExecuteCommandWithNonZeroEventFlagsDoesNotCrash) {
  MahoFolderContextMenu menu(nullptr, "folder-abc");
  menu.ExecuteCommand(IDC_MAHO_FOLDER_RENAME, /*event_flags=*/1);
  menu.ExecuteCommand(IDC_MAHO_FOLDER_CONVERT_TO_SPACE, /*event_flags=*/2);
  menu.ExecuteCommand(IDC_MAHO_FOLDER_DELETE, /*event_flags=*/0xFF);
}

TEST(MahoFolderContextMenuTest, MoveToRootHiddenForRootFolder) {
  MahoFolderContextMenu menu(nullptr, "space-1", "folder-root",
                              /*parent_folder_id=*/"",
                              MahoFolderContextMenu::BeginRenameCallback());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_FALSE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_MOVE_TO_ROOT));
}

TEST(MahoFolderContextMenuTest, MoveToRootPresentForNestedFolder) {
  MahoFolderContextMenu menu(nullptr, "space-1", "folder-child",
                              /*parent_folder_id=*/"folder-parent",
                              MahoFolderContextMenu::BeginRenameCallback());
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_FOLDER_MOVE_TO_ROOT));
}

TEST(MahoFolderContextMenuTest, ExecuteMoveToRootDoesNotCrashWithNullBrowser) {
  MahoFolderContextMenu menu(nullptr, "space-1", "folder-child",
                              /*parent_folder_id=*/"folder-parent",
                              MahoFolderContextMenu::BeginRenameCallback());
  menu.ExecuteCommand(IDC_MAHO_FOLDER_MOVE_TO_ROOT, 0);
}

TEST(MahoFolderContextMenuTest, ExecuteRenameFiresCallback) {
  bool callback_fired = false;
  MahoFolderContextMenu menu(
      nullptr, "space-1", "folder-abc",
      /*parent_folder_id=*/"",
      base::BindOnce([](bool* flag) { *flag = true; }, &callback_fired));
  menu.ExecuteCommand(IDC_MAHO_FOLDER_RENAME, 0);
  EXPECT_TRUE(callback_fired)
      << "ExecuteCommand(RENAME) must fire the begin_rename_callback";
}

TEST(MahoFolderContextMenuTest, ExecuteRenameCallbackOnlyFiresOnce) {
  int fire_count = 0;
  MahoFolderContextMenu menu(
      nullptr, "space-1", "folder-abc",
      /*parent_folder_id=*/"",
      base::BindOnce([](int* count) { ++(*count); }, &fire_count));
  menu.ExecuteCommand(IDC_MAHO_FOLDER_RENAME, 0);
  menu.ExecuteCommand(IDC_MAHO_FOLDER_RENAME, 0);
  EXPECT_EQ(fire_count, 1)
      << "BeginRenameCallback is a OnceClosure; second call must be no-op";
}

}  // namespace

