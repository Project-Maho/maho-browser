// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_address_bar_context_menu.h"

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

TEST(MahoAddressBarContextMenuTest, BuildMenuModelContainsEditAddress) {
  MahoAddressBarContextMenu menu(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_ADDR_EDIT));
}

TEST(MahoAddressBarContextMenuTest, BuildMenuModelContainsCopyUrl) {
  MahoAddressBarContextMenu menu(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_ADDR_COPY_URL));
}

TEST(MahoAddressBarContextMenuTest, BuildMenuModelContainsCopyMarkdown) {
  MahoAddressBarContextMenu menu(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_ADDR_COPY_MARKDOWN));
}

TEST(MahoAddressBarContextMenuTest, BuildMenuModelHasExactlyThreeItems) {
  MahoAddressBarContextMenu menu(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  int non_separator_count = 0;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetTypeAt(i) != ui::MenuModel::TYPE_SEPARATOR) {
      ++non_separator_count;
    }
  }
  EXPECT_EQ(non_separator_count, 3);
}

TEST(MahoAddressBarContextMenuTest, BuildMenuModelHasOneSeparator) {
  MahoAddressBarContextMenu menu(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  int sep_count = 0;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
      ++sep_count;
    }
  }
  EXPECT_EQ(sep_count, 1);
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

TEST(MahoAddressBarContextMenuTest, TopLevelOrderMatchesProduction) {
  MahoAddressBarContextMenu menu(nullptr);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  auto items = FlattenModel(*model);
  ASSERT_EQ(items.size(), 4u);

  EXPECT_EQ(items[0].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[0].command_id, IDC_MAHO_ADDR_EDIT);

  EXPECT_EQ(items[1].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[1].command_id, IDC_MAHO_ADDR_COPY_URL);

  EXPECT_EQ(items[2].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[3].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[3].command_id, IDC_MAHO_ADDR_COPY_MARKDOWN);
}

TEST(MahoAddressBarContextMenuTest, EditAddressDisabledWithNoCallback) {
  MahoAddressBarContextMenu menu(nullptr);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_ADDR_EDIT));
}

TEST(MahoAddressBarContextMenuTest, EditAddressEnabledWhenCallbackProvided) {
  bool called = false;
  MahoAddressBarContextMenu menu(nullptr,
                                  base::BindRepeating([](bool* c) { *c = true; },
                                                      &called));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_ADDR_EDIT));
}

// IsCommandIdEnabled for copy actions calls browser_->tab_strip_model()
// unconditionally, so null-browser tests for those commands cannot run safely
// in unit tests without a real Browser object. Copy-command enabled-state is
// exercised by integration/interactive tests that provide a real Browser.

TEST(MahoAddressBarContextMenuTest, UnknownCommandIdDisabled) {
  MahoAddressBarContextMenu menu(nullptr);
  EXPECT_FALSE(menu.IsCommandIdEnabled(0));
  EXPECT_FALSE(menu.IsCommandIdEnabled(99999));
}

TEST(MahoAddressBarContextMenuTest, NothingIsChecked) {
  MahoAddressBarContextMenu menu(nullptr);
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_ADDR_EDIT));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_ADDR_COPY_URL));
  EXPECT_FALSE(menu.IsCommandIdChecked(IDC_MAHO_ADDR_COPY_MARKDOWN));
}

TEST(MahoAddressBarContextMenuTest, ExecuteEditAddressInvokesCallback) {
  bool called = false;
  MahoAddressBarContextMenu menu(nullptr,
                                  base::BindRepeating([](bool* c) { *c = true; },
                                                      &called));
  menu.ExecuteCommand(IDC_MAHO_ADDR_EDIT, 0);
  EXPECT_TRUE(called);
}

TEST(MahoAddressBarContextMenuTest, ExecuteEditAddressWithNullCallbackDoesNotCrash) {
  MahoAddressBarContextMenu menu(nullptr);
  menu.ExecuteCommand(IDC_MAHO_ADDR_EDIT, 0);
}

TEST(MahoAddressBarContextMenuTest, CommandIdValues) {
  EXPECT_EQ(IDC_MAHO_ADDR_EDIT, 57025);
  EXPECT_EQ(IDC_MAHO_ADDR_COPY_URL, 57026);
  EXPECT_EQ(IDC_MAHO_ADDR_COPY_MARKDOWN, 57027);
}

}  // namespace
