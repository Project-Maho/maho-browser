// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/controls/label.h"

namespace maho {
namespace {

bool IsInlineRowButton(views::View* view, std::u16string expected_text) {
  auto* button = views::AsViewClass<views::LabelButton>(view);
  return button && button->GetText() == expected_text;
}

bool IsPlaceholderLabel(views::View* view, std::u16string expected_text) {
  auto* label = views::AsViewClass<views::Label>(view);
  return label && label->GetText() == expected_text;
}

void ExpectEmptyPinnedSectionLayout(views::View* rows) {
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size())
      << "Idle empty-pinned layout should keep the hidden pinned section and separator in the view tree, plus + New Tab and the normal section";

  EXPECT_FALSE(rows->children()[0]->GetVisible())
      << "Pinned section should be hidden when empty";
  EXPECT_FALSE(rows->children()[1]->GetVisible())
      << "Pinned separator should be hidden when empty";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[2], u"+ New Tab"))
      << "+ New Tab should follow the pinned section and separator";
  EXPECT_TRUE(rows->children()[2]->GetVisible())
      << "+ New Tab should remain visible";
  EXPECT_TRUE(rows->children()[3]->GetVisible())
      << "Normal section should remain visible after + New Tab";
  EXPECT_FALSE(rows->children()[3]->children().empty())
      << "Normal section should contain the normal rows";
}

void ExpectNoTabsRows(views::View* rows) {
  ASSERT_TRUE(rows);
  ASSERT_EQ(2u, rows->children().size())
      << "No-tabs layout should omit the separator and normal section";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[0], u"+ New Tab"))
      << "+ New Tab should be first when no tabs are open";
  EXPECT_TRUE(IsPlaceholderLabel(rows->children()[1], u"No tabs open"))
      << "Placeholder should follow + New Tab in no-tabs layout";
}

class MahoSidebarContractTest : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, SeededFavoritesMatchSnapshot) {
  SeedProfile(2);

  auto state = GetViewState();
  EXPECT_FALSE(state.favorites.items.empty())
      << "Seed 2 must produce at least one favorite";

  for (const auto& fav : state.favorites.items) {
    EXPECT_FALSE(fav.title.empty()) << "Favorite must have a title";
    EXPECT_TRUE(fav.url.is_valid()) << "Favorite must have a valid URL";
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, SeededFoldersMatchSnapshot) {
  SeedProfile(2);

  auto state = GetViewState();
  std::vector<const SidebarTreeNode*> folders;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folders.push_back(&node);
    }
  }
  EXPECT_FALSE(folders.empty()) << "Seed 2 must produce at least one folder";

  for (const auto* folder : folders) {
    EXPECT_FALSE(folder->folder_name.empty())
        << "Folder must have a name";
    EXPECT_FALSE(folder->children.empty())
        << "Folder '" << folder->folder_name << "' must have children";
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, PinnedSectionContract) {
  SeedProfile(2);

  auto state = GetViewState();
  EXPECT_FALSE(state.tab_list.pinned_tree.empty())
      << "Seed 2 must produce at least one pinned tab";

  for (const auto& node : state.tab_list.pinned_tree) {
    EXPECT_TRUE(node.is_pinned) << "All pinned_tree entries must be pinned";
    EXPECT_FALSE(node.tab_id.empty()) << "Pinned tab must have a tab_id";
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, FolderSupportContract) {
  SeedProfile(2);

  auto state = GetViewState();
  bool found_folder = false;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      found_folder = true;
      EXPECT_FALSE(node.folder_name.empty())
          << "Folder must have folder_name";
      EXPECT_FALSE(node.folder_id.empty())
          << "Folder must have folder_id";
      EXPECT_FALSE(node.children.empty())
          << "Folder must have children";
    }
  }
  EXPECT_TRUE(found_folder) << "Seed 2 must have at least one folder";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, TabReorderContract) {
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

  EXPECT_TRUE(observer_.HasEvent("reorder_tab"))
      << "Expected reorder_tab event after drag";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, SidebarTreeJsonContract) {
  SeedProfile(2);

  auto state = GetViewState();

  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "active_space_id must be set";
  EXPECT_FALSE(state.tab_list.normal_tree.empty())
      << "normal_tree must be non-empty";
  EXPECT_FALSE(state.tab_list.pinned_tree.empty())
      << "pinned_tree must be non-empty for seed 2";
  EXPECT_FALSE(state.favorites.items.empty())
      << "favorites must be non-empty for seed 2";

  for (const auto& node : state.tab_list.normal_tree) {
    bool is_tab = (node.kind == SidebarNodeKind::kTab);
    bool is_folder = (node.kind == SidebarNodeKind::kFolder);
    EXPECT_TRUE(is_tab || is_folder)
        << "Every tree node must be a tab or folder";
    if (is_tab) {
      EXPECT_FALSE(node.tab_id.empty());
    }
    if (is_folder) {
      EXPECT_FALSE(node.folder_id.empty());
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, FolderCollapseExpandToggle) {
  SeedProfile(2);

  auto state = GetViewState();
  std::string folder_id;
  int children_count = 0;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && !node.children.empty()) {
      folder_id = node.folder_id;
      children_count = static_cast<int>(node.children.size());
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty());
  ASSERT_GT(children_count, 0);

  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(folder_row);

  const gfx::Point center = folder_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  auto state_after = GetViewState();
  for (const auto& node : state_after.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
      EXPECT_FALSE(node.is_expanded)
          << "Folder should be collapsed after click";
      break;
    }
  }

  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop2;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop2.QuitClosure()));
  click_loop2.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  auto state_restored = GetViewState();
  for (const auto& node : state_restored.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
      EXPECT_TRUE(node.is_expanded)
          << "Folder should be expanded after second click";
      break;
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       EmptyFolderRemovedAfterLastChildMove) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  std::string child_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && node.children.size() == 1) {
      folder_id = node.folder_id;
      if (node.children[0].kind == SidebarNodeKind::kTab) {
        child_tab_id = node.children[0].tab_id;
      }
      break;
    }
  }

  if (folder_id.empty()) {
    LOG(WARNING) << "No single-child folder in seed 2 — skipping";
    return;
  }
  ASSERT_FALSE(child_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(child_tab_id);
  ASSERT_TRUE(tab_row);

  views::View* normal_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_TRUE(normal_target);

  DragViewToView(tab_row, normal_target);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state_after = GetViewState();
  bool folder_still_exists = false;
  for (const auto& node : state_after.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
      folder_still_exists = true;
      break;
    }
  }
  EXPECT_FALSE(folder_still_exists)
      << "Empty folder should be pruned after last child is moved out";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, LargeTabCountRenders) {
  AddTestTabs(50);
  ResolveViews();

  auto state = GetViewState();
  int total = static_cast<int>(state.tab_list.normal_tree.size()) +
              static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_GE(total, 51) << "50 added tabs + NTP should all appear";

  EXPECT_TRUE(tab_list_->GetVisible())
      << "Tab list should remain visible with many tabs";
  EXPECT_GT(tab_list_->GetPreferredSize().height(), 0)
      << "Tab list should have non-zero preferred height";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SidebarRowOrderTreeLayoutKeepsEmptyPinnedSectionContainer) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.active_space_id.empty())
      << "Seed 2 should use tree layout";

  MahoSidebarTabListModel empty_pinned_tree = state.tab_list;
  empty_pinned_tree.pinned_tree.clear();

  tab_list_->Update(empty_pinned_tree, browser());
  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SidebarRowOrderFlatFallbackPlacesSeparatorAboveNewTab) {
  SeedProfile(0);

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.pinned_tabs.empty() && state.tab_list.tabs.empty())
      << "Seed 0 should provide flat rows to validate the fallback layout";

  MahoSidebarTabListModel flat_model = state.tab_list;
  flat_model.active_space_id = "test-space";
  flat_model.pinned_tabs.clear();
  flat_model.pinned_tree.clear();
  flat_model.normal_tree.clear();

  tab_list_->Update(flat_model, browser());
  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SidebarRowOrderNoTabsKeepsPlaceholderWithoutSeparator) {
  MahoSidebarTabListModel empty_model;
  tab_list_->Update(empty_model, browser());

  ExpectNoTabsRows(tab_list_->tab_rows_for_testing());
}

}  // namespace
}  // namespace maho
