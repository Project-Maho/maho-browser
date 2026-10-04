// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"

#include <memory>

#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"

namespace {

bool ModelContainsCommand(const ui::SimpleMenuModel& model, int command_id) {
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (model.GetTypeAt(i) == ui::MenuModel::TYPE_SUBMENU) {
      const ui::MenuModel* sub = model.GetSubmenuModelAt(i);
      if (sub) {
        for (size_t j = 0; j < sub->GetItemCount(); ++j) {
          if (sub->GetCommandIdAt(j) == command_id) {
            return true;
          }
        }
      }
    }
    if (model.GetCommandIdAt(i) == command_id) {
      return true;
    }
  }
  return false;
}

const ui::MenuModel* FindSubMenuForCommand(const ui::SimpleMenuModel& model,
                                           int command_id) {
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (model.GetTypeAt(i) == ui::MenuModel::TYPE_SUBMENU &&
        model.GetCommandIdAt(i) == command_id) {
      return model.GetSubmenuModelAt(i);
    }
  }
  return nullptr;
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsCopyLink) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_COPY_URL));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsShare) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_SHARE));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsChangeIcon) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_CHANGE_ICON));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsRename) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_RENAME));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsMute) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_MUTE));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsOpenInSplitView) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_OPEN_SPLIT));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsDuplicate) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_DUPLICATE));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsBoost) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_BOOST));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsMoveToSubmenu) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_MOVE_TO_SPACE));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsArchiveTab) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_ARCHIVE));
}

TEST(MahoTabContextMenuTest, BuildMenuModelContainsArchiveTabsBelow) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_ARCHIVE_BELOW));
}

TEST(MahoTabContextMenuTest, AllCommandsDisabledWhenBrowserIsNull) {
  MahoTabContextMenu menu(nullptr, 0);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_COPY_URL));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_SHARE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_CHANGE_ICON));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_RENAME));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MUTE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_OPEN_SPLIT));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_DUPLICATE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_BOOST));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_ARCHIVE));
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_ARCHIVE_BELOW));
}

TEST(MahoTabContextMenuTest, RenameTabSilentWithNullDelegate) {
  // ExecuteCommand with no delegate must not crash.
  MahoTabContextMenu menu(nullptr, 0);
  menu.ExecuteCommand(IDC_MAHO_TAB_RENAME, 0);
}

// Rename does not need a valid tab-strip index to fire — the delegate owns
// the row and calls BeginTitleEditing() directly on itself.
TEST(MahoTabContextMenuTest, RenameTabDelegateCalledWhenEnabled) {
  class TestDelegate : public MahoTabContextMenu::Delegate {
   public:
    void RenameTab(int tab_index) override {
      rename_called = true;
      last_tab_index = tab_index;
    }
    bool rename_called = false;
    int last_tab_index = -1;
  };

  TestDelegate delegate;
  // browser is null (no TabStripModel), but rename must still reach the
  // delegate because inline editing does not require a live tab-strip entry.
  MahoTabContextMenu menu(nullptr, 3, &delegate);
  menu.ExecuteCommand(IDC_MAHO_TAB_RENAME, 0);
  EXPECT_TRUE(delegate.rename_called);
  EXPECT_EQ(delegate.last_tab_index, 3);
}

TEST(MahoTabContextMenuTest, CommandIdValues) {
  EXPECT_EQ(IDC_MAHO_TAB_BOOST, 57017);
  EXPECT_EQ(IDC_MAHO_TAB_SHARE, 57020);
  EXPECT_EQ(IDC_MAHO_TAB_CHANGE_ICON, 57021);
  EXPECT_EQ(IDC_MAHO_TAB_RENAME, 57022);
  EXPECT_EQ(IDC_MAHO_TAB_ARCHIVE, 57023);
  EXPECT_EQ(IDC_MAHO_TAB_ARCHIVE_BELOW, 57024);
}

// ---------------------------------------------------------------------------
// Top-level menu order & separator positions
// ---------------------------------------------------------------------------

// Returns a flat list of {type, command_id} pairs for the top-level model.
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

// With a null browser the mute/unmute branch always adds "Mute" (no
// WebContents → no audio muted state), so the expected order is fixed.
TEST(MahoTabContextMenuTest, TopLevelOrderMatchesProduction) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  std::vector<ItemDesc> items = FlattenTopLevel(*model);

  // Expected sequence (index → command):
  //  0  Copy Link
  //  1  Share
  //  2  [separator]
  //  3  Change Icon…
  //  4  Rename…
  //  5  Mute
  //  6  Pin (null core → role != "pinned" → "Pin")
  //  7  Add to Favorites (L3-compliant slot 57010)
  //  8  [separator]
  //  9  Open in Split View
  // 10  Duplicate
  // 11  Boost
  // 12  Move to (submenu)
  // 13  [separator]
  // 14  Sleep Tab
  // 15  Archive Tab
  // 16  Archive Tabs Below
  // 17  [separator]
  // 18  Close Tab
  ASSERT_EQ(items.size(), 19u);

  EXPECT_EQ(items[0].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[0].command_id, IDC_MAHO_TAB_COPY_URL);

  EXPECT_EQ(items[1].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[1].command_id, IDC_MAHO_TAB_SHARE);

  EXPECT_EQ(items[2].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[3].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[3].command_id, IDC_MAHO_TAB_CHANGE_ICON);

  EXPECT_EQ(items[4].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[4].command_id, IDC_MAHO_TAB_RENAME);

  EXPECT_EQ(items[5].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[5].command_id, IDC_MAHO_TAB_MUTE);

  // Null core → QueryTabRole returns "" → not pinned → "Pin".
  EXPECT_EQ(items[6].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[6].command_id, IDC_MAHO_TAB_PIN);

  // "Add to Favorites" reuses the historical favorite slot value 57010 under an
  // L3-compliant name in production; the header enum for that slot is 57010.
  EXPECT_EQ(items[7].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[7].command_id, 57010);

  EXPECT_EQ(items[8].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[9].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[9].command_id, IDC_MAHO_TAB_OPEN_SPLIT);

  EXPECT_EQ(items[10].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[10].command_id, IDC_MAHO_TAB_DUPLICATE);

  EXPECT_EQ(items[11].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[11].command_id, IDC_MAHO_TAB_BOOST);

  EXPECT_EQ(items[12].type, ui::MenuModel::TYPE_SUBMENU);
  EXPECT_EQ(items[12].command_id, IDC_MAHO_TAB_MOVE_TO_SPACE);

  EXPECT_EQ(items[13].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[14].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[14].command_id, IDC_MAHO_TAB_FREEZE);

  EXPECT_EQ(items[15].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[15].command_id, IDC_MAHO_TAB_ARCHIVE);

  EXPECT_EQ(items[16].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[16].command_id, IDC_MAHO_TAB_ARCHIVE_BELOW);

  EXPECT_EQ(items[17].type, ui::MenuModel::TYPE_SEPARATOR);

  EXPECT_EQ(items[18].type, ui::MenuModel::TYPE_COMMAND);
  EXPECT_EQ(items[18].command_id, IDC_MAHO_TAB_CLOSE);
}

TEST(MahoTabContextMenuTest, TopLevelHasExactlyFourSeparators) {
  MahoTabContextMenu menu(nullptr, 0);
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

// ---------------------------------------------------------------------------
// Move-to submenu — fallback shape (no-core path with null browser)
// ---------------------------------------------------------------------------

TEST(MahoTabContextMenuTest, MoveToSubmenuIsPresentAsSubmenuType) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  // Verify the Move-to item is a TYPE_SUBMENU at the top level.
  bool found_submenu = false;
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetCommandIdAt(i) == IDC_MAHO_TAB_MOVE_TO_SPACE) {
      EXPECT_EQ(model->GetTypeAt(i), ui::MenuModel::TYPE_SUBMENU);
      found_submenu = true;
      break;
    }
  }
  EXPECT_TRUE(found_submenu);
}

// With a null browser there is no MahoCore → no spaces → the submenu falls
// back to a single synthetic item with command_id 0 and label "No other spaces".
TEST(MahoTabContextMenuTest, MoveToSubmenuFallbackHasOneNoOtherSpacesItem) {
  MahoTabContextMenu menu(nullptr, 0);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  const ui::MenuModel* sub =
      FindSubMenuForCommand(*model, IDC_MAHO_TAB_MOVE_TO_SPACE);
  ASSERT_NE(sub, nullptr);
  ASSERT_EQ(sub->GetItemCount(), 1u);
  EXPECT_EQ(sub->GetCommandIdAt(0), 0);
  EXPECT_EQ(sub->GetLabelAt(0), u"No other spaces");
}

// ---------------------------------------------------------------------------
// Helper boundary tests: IsMoveToSpaceCommand, MoveToSpaceIndex
// ---------------------------------------------------------------------------

TEST(MahoTabContextMenuTest, IsMoveToSpaceCommandBoundaries) {
  // Just below base → not a move-space command.
  EXPECT_FALSE(IsMoveToSpaceCommand(IDC_MAHO_TAB_MOVE_SPACE_BASE - 1));

  // Exactly at base → is a move-space command.
  EXPECT_TRUE(IsMoveToSpaceCommand(IDC_MAHO_TAB_MOVE_SPACE_BASE));

  // Interior value.
  EXPECT_TRUE(IsMoveToSpaceCommand(IDC_MAHO_TAB_MOVE_SPACE_BASE + 10));

  // Exactly at max → still in range.
  EXPECT_TRUE(IsMoveToSpaceCommand(IDC_MAHO_TAB_MOVE_SPACE_MAX));

  // One above max → out of range.
  EXPECT_FALSE(IsMoveToSpaceCommand(IDC_MAHO_TAB_MOVE_SPACE_MAX + 1));

  // Unrelated small command ID.
  EXPECT_FALSE(IsMoveToSpaceCommand(IDC_MAHO_TAB_COPY_URL));
}

TEST(MahoTabContextMenuTest, MoveToSpaceIndexDecoding) {
  EXPECT_EQ(MoveToSpaceIndex(IDC_MAHO_TAB_MOVE_SPACE_BASE), 0);
  EXPECT_EQ(MoveToSpaceIndex(IDC_MAHO_TAB_MOVE_SPACE_BASE + 1), 1);
  EXPECT_EQ(MoveToSpaceIndex(IDC_MAHO_TAB_MOVE_SPACE_BASE + 5), 5);
}

// ---------------------------------------------------------------------------
// Enabled-state asymmetries
// ---------------------------------------------------------------------------

// IDC_MAHO_TAB_UNMUTE shares the same enabled-state path as Mute; with a null
// browser (no valid tab index) it must also return false.
TEST(MahoTabContextMenuTest, UnmuteDisabledWhenBrowserIsNull) {
  MahoTabContextMenu menu(nullptr, 0);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_UNMUTE));
}

// The Move-to anchor command (IDC_MAHO_TAB_MOVE_TO_SPACE) is not in the
// IsMoveToSpaceCommand range — it is the submenu header. It falls into the
// named-case branch and requires a valid tab index → must be false here.
TEST(MahoTabContextMenuTest, MoveToAnchorCommandDisabledWhenBrowserIsNull) {
  MahoTabContextMenu menu(nullptr, 0);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_TO_SPACE));
}

// A dynamic move-space command (in the IDC_MAHO_TAB_MOVE_SPACE_BASE range)
// always returns true regardless of browser state — the submenu item itself
// drives the action, not the browser.
TEST(MahoTabContextMenuTest, DynamicMoveSpaceCommandAlwaysEnabled) {
  MahoTabContextMenu menu(nullptr, 0);
  // Test a couple of representative IDs in the dynamic range.
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_SPACE_BASE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_SPACE_BASE + 1));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_SPACE_MAX));
}

// Unknown command IDs (outside all defined ranges) must return false.
TEST(MahoTabContextMenuTest, UnknownCommandIdIsDisabled) {
  MahoTabContextMenu menu(nullptr, 0);
  EXPECT_FALSE(menu.IsCommandIdEnabled(0));
  EXPECT_FALSE(menu.IsCommandIdEnabled(99999));
  // The fallback sentinel used by the no-spaces submenu item should also be
  // disabled because it is command_id 0, which is not a recognised command.
  EXPECT_FALSE(menu.IsCommandIdEnabled(0));
}

TEST(MahoTabContextMenuTest, ConstructWithCoreTabIdBuildsSameModel) {
  MahoTabContextMenu menu(nullptr, 0, nullptr, "some-uuid-1234");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_COPY_URL));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_MOVE_TO_SPACE));
}

TEST(MahoTabContextMenuTest, EmptyCoreTabIdDoesNotBreakEnabledState) {
  MahoTabContextMenu menu(nullptr, 0, nullptr, "");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_SPACE_BASE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_SPACE_BASE + 1));
}

// ---------------------------------------------------------------------------
// Share and Archive Tab — null-browser safety
// ---------------------------------------------------------------------------

// Share falls back to copying the URL. With a null browser there is no
// WebContents, so the clipboard write is skipped — must not crash.
TEST(MahoTabContextMenuTest, ShareSilentWithNullBrowser) {
  MahoTabContextMenu menu(nullptr, 0);
  menu.ExecuteCommand(IDC_MAHO_TAB_SHARE, 0);
}

TEST(MahoTabContextMenuTest, BoostSilentWithNullBrowser) {
  MahoTabContextMenu menu(nullptr, 0);
  menu.ExecuteCommand(IDC_MAHO_TAB_BOOST, 0);
}

// Archive Tab activates the tab then posts to the thread pool. With a null
// browser has_valid_tab_index is false, so it returns early — must not crash.
TEST(MahoTabContextMenuTest, ArchiveTabSilentWithNullBrowser) {
  MahoTabContextMenu menu(nullptr, 0);
  menu.ExecuteCommand(IDC_MAHO_TAB_ARCHIVE, 0);
}

TEST(MahoTabContextMenuTest, FreezeSilentWithNullBrowser) {
  MahoTabContextMenu menu(nullptr, 0);
  menu.ExecuteCommand(IDC_MAHO_TAB_FREEZE, 0);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_FREEZE));
}

TEST(MahoTabContextMenuTest, BuildMenuModelMultiSelect) {
  std::vector<std::string> selected_ids = {"tab-1", "tab-2"};
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-1", selected_ids);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  // Multi-select items must be present
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_CLOSE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_PIN));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_UNMUTE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_MOVE_TO_SPACE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_FREEZE));

  // Single-only items must not be present
  EXPECT_FALSE(ModelContainsCommand(*model, IDC_MAHO_TAB_COPY_URL));
  EXPECT_FALSE(ModelContainsCommand(*model, IDC_MAHO_TAB_RENAME));
  EXPECT_FALSE(ModelContainsCommand(*model, IDC_MAHO_TAB_DUPLICATE));
  EXPECT_FALSE(ModelContainsCommand(*model, IDC_MAHO_TAB_BOOST));
}

// ---------------------------------------------------------------------------
// Todo 6: multi-select "Open in Split View" eligibility gating
// ---------------------------------------------------------------------------

// With a null browser no selected id resolves to a live, unsplit strip index,
// so CountEligibleSplitTabs() is 0 (< 2). The multi-select menu must omit the
// split command entirely while every eligibility-independent multi-select item
// (Close / Pin / Unmute / Move to Space) stays present.
TEST(MahoTabContextMenuTest, MultiSelectOpenSplitOmittedWhenNoEligibleTabs) {
  std::vector<std::string> selected_ids = {"tab-1", "tab-2"};
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-1", selected_ids);
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);

  EXPECT_FALSE(ModelContainsCommand(*model, IDC_MAHO_TAB_OPEN_SPLIT));

  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_CLOSE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_PIN));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_UNMUTE));
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_MOVE_TO_SPACE));
}

// The enabled state mirrors the model gating: with zero eligible tabs the
// split command is disabled, but the eligibility-independent multi-select
// commands stay enabled.
TEST(MahoTabContextMenuTest, MultiSelectOpenSplitDisabledWhenNoEligibleTabs) {
  std::vector<std::string> selected_ids = {"tab-1", "tab-2"};
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-1", selected_ids);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_OPEN_SPLIT));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_CLOSE));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_MOVE_TO_SPACE));
}


// ---------------------------------------------------------------------------
// Single-tab role actions: Pin/Unpin, Add to Favorites, Close Tab
// ---------------------------------------------------------------------------

// Command ID of the single-tab "Add to Favorites" item. Production defines an
// L3-compliant alias in the .cc bound to the historical favorite slot value.
constexpr int kSingleTabFavoriteCommandId = 57010;

TEST(MahoTabContextMenuTest, SingleTabMenuContainsPinOrUnpin) {
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-uuid-9");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_PIN) ||
              ModelContainsCommand(*model, IDC_MAHO_TAB_UNPIN));
}

TEST(MahoTabContextMenuTest, SingleTabMenuContainsAddToFavorites) {
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-uuid-9");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, kSingleTabFavoriteCommandId));
}

TEST(MahoTabContextMenuTest, SingleTabMenuContainsCloseTab) {
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-uuid-9");
  std::unique_ptr<ui::SimpleMenuModel> model = menu.BuildMenuModel();
  ASSERT_NE(model, nullptr);
  EXPECT_TRUE(ModelContainsCommand(*model, IDC_MAHO_TAB_CLOSE));
}

// Close Tab routes through the delegate funnel (id-based). With a delegate and
// a non-empty core tab id it is enabled even without a live tab-strip index, so
// it works for suspended tabs.
TEST(MahoTabContextMenuTest, CloseTabRoutesThroughDelegateById) {
  class TestDelegate : public MahoTabContextMenu::Delegate {
   public:
    void CloseTabById(const std::string& tab_id) override {
      close_called = true;
      last_id = tab_id;
    }
    bool close_called = false;
    std::string last_id;
  };

  TestDelegate delegate;
  MahoTabContextMenu menu(nullptr, -1, &delegate, "tab-uuid-9");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_CLOSE));
  menu.ExecuteCommand(IDC_MAHO_TAB_CLOSE, 0);
  EXPECT_TRUE(delegate.close_called);
  EXPECT_EQ(delegate.last_id, "tab-uuid-9");
}

// Close Tab is a no-op (not a crash) when no delegate is wired.
TEST(MahoTabContextMenuTest, CloseTabSilentWithNullDelegate) {
  MahoTabContextMenu menu(nullptr, 0, nullptr, "tab-uuid-9");
  menu.ExecuteCommand(IDC_MAHO_TAB_CLOSE, 0);
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_CLOSE));
}

// ---------------------------------------------------------------------------
// R-11 / task-11-menu: Private context menu contract (frozen stubs)
// These three tests are part of the immutable incognito test inventory.
// Real implementations land in Todo 11; stubs unblock inventory validation.
// ---------------------------------------------------------------------------

// Verify that BuildMenuModel() for an OTR profile never queries MahoCore and
// produces exactly the 5-item allowlist:
// Reload / Duplicate / Copy Link / Open in Split View / Close Tab.
TEST(MahoTabContextMenuTest, PrivateModelReadsNoMahoCoreAndIsExactAllowlist) {
  GTEST_SKIP() << "Frozen stub: implementation due in Todo 11 (R-11-c)";
}

// Verify that executing any command outside the 5-item OTR allowlist (e.g.
// Rename, Pin, Move to Space, Archive) is a silent no-op and does not call
// into MahoCore, the delegate, or the space bridge.
TEST(MahoTabContextMenuTest, ForgedPersistentCommandsAreNoOps) {
  GTEST_SKIP() << "Frozen stub: implementation due in Todo 11 (R-11-c)";
}

// Positive control: a regular (non-OTR) menu contains the full item set
// including MahoCore-backed commands such as Move to Space and Add to
// Favorites, proving the OTR guard does not affect regular profiles.
TEST(MahoTabContextMenuTest, RegularMenuPositiveControl) {
  GTEST_SKIP() << "Frozen stub: implementation due in Todo 11 (R-11-c)";
}

}  // namespace

