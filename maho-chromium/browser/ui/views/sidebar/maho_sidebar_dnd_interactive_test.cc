// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "chrome/browser/ui/browser.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace maho {
namespace {

class MahoSidebarDndInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragReordersFavoriteTile) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_GE(state.favorites.items.size(), 2u);

  views::View* first_tile = favorites_->GetTileForTesting(0);
  views::View* second_tile = favorites_->GetTileForTesting(1);
  ASSERT_TRUE(first_tile);
  ASSERT_TRUE(second_tile);

  DragViewToView(first_tile, second_tile);

  EXPECT_TRUE(observer_.HasEvent("reorder_favorite"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragTabToPinnedSection) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty());

  std::string tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(tab_id);
  ASSERT_TRUE(tab_row);

  views::View* pinned_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  DragViewToView(tab_row, pinned_target);

  EXPECT_TRUE(observer_.HasEvent("pin_tab"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragTabIntoFolder) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_id = node.folder_id;
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty()) << "Seed 2 must have at least one folder";

  std::string tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty()) {
      tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(tab_id);
  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(tab_row);
  ASSERT_TRUE(folder_row);

  DragViewToView(tab_row, folder_row);

  EXPECT_TRUE(observer_.HasEvent("move_folder_into_folder") ||
              observer_.HasEvent("move_tab_to_folder"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragReordersRegularTabs) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::vector<std::string> root_tab_ids;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty()) {
      root_tab_ids.push_back(node.tab_id);
      if (root_tab_ids.size() == 2)
        break;
    }
  }
  ASSERT_EQ(2u, root_tab_ids.size());

  auto* first = tab_list_->FindTabRowByIdForTesting(root_tab_ids[0]);
  auto* second = tab_list_->FindTabRowByIdForTesting(root_tab_ids[1]);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  DragViewToView(first, second);

  EXPECT_TRUE(observer_.HasEvent("reorder_tab"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragReordersFolder) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::vector<std::string> folder_ids;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_ids.push_back(node.folder_id);
      if (folder_ids.size() == 2)
        break;
    }
  }
  ASSERT_GE(folder_ids.size(), 2u) << "Seed 2 must have at least 2 folders";

  auto* first = tab_list_->FindFolderRowByIdForTesting(folder_ids[0]);
  auto* second = tab_list_->FindFolderRowByIdForTesting(folder_ids[1]);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  DragViewToView(first, second);

  EXPECT_TRUE(observer_.HasEvent("reorder_folder"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragTabOutOfFolder) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string child_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      for (const auto& child : node.children) {
        if (child.kind == SidebarNodeKind::kTab) {
          child_tab_id = child.tab_id;
          break;
        }
      }
      if (!child_tab_id.empty())
        break;
    }
  }
  ASSERT_FALSE(child_tab_id.empty())
      << "Seed 2 must have a tab inside a folder";

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(child_tab_id);
  ASSERT_TRUE(tab_row);

  views::View* normal_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_TRUE(normal_target);

  DragViewToView(tab_row, normal_target);

  EXPECT_TRUE(observer_.HasEvent("move_tab_to_root"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragUnpinTab) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.pinned_tree.empty());

  std::string pinned_tab_id;
  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      pinned_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(pinned_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(pinned_tab_id);
  ASSERT_TRUE(tab_row);

  views::View* normal_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_TRUE(normal_target);

  DragViewToView(tab_row, normal_target);

  EXPECT_TRUE(observer_.HasEvent("unpin_tab"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragCancelRestoresState) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(tab_id);
  ASSERT_TRUE(tab_row);

  const gfx::Point source_center =
      tab_row->GetBoundsInScreen().CenterPoint();

  ASSERT_TRUE(ui_controls::SendMouseMove(source_center.x(),
                                          source_center.y()));
  base::RunLoop press_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN, press_loop.QuitClosure()));
  press_loop.Run();

  ASSERT_TRUE(ui_controls::SendMouseMove(source_center.x() + 50,
                                          source_center.y() + 50));

  base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, key_loop.QuitClosure()));
  key_loop.Run();

  base::RunLoop release_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::UP, release_loop.QuitClosure()));
  release_loop.Run();

  EXPECT_FALSE(observer_.HasEvent("reorder_tab"))
      << "No reorder should fire after ESC cancel";
  EXPECT_FALSE(observer_.HasEvent("pin_tab"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       InvalidCrossAreaDropRejected) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_GE(state.favorites.items.size(), 1u);

  views::View* fav_tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(fav_tile);

  views::View* pinned_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  DragViewToView(fav_tile, pinned_target);

  EXPECT_FALSE(observer_.HasEvent("pin_tab"))
      << "Dragging a favorite tile to pinned section should be rejected";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DuplicatePinDefended) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.pinned_tree.empty());

  std::string already_pinned_id;
  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      already_pinned_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(already_pinned_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(already_pinned_id);
  ASSERT_TRUE(tab_row);

  views::View* pinned_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  DragViewToView(tab_row, pinned_target);

  EXPECT_FALSE(observer_.HasEvent("pin_tab"))
      << "Re-pinning an already pinned tab should be a no-op";
}

}  // namespace
}  // namespace maho
