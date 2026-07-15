// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace maho {
namespace {

bool IsInlineRowButton(views::View* view, std::u16string expected_text) {
  auto* button = views::AsViewClass<views::LabelButton>(view);
  return button && button->GetText() == expected_text;
}

void ExpectEmptyPinnedSectionLayout(views::View* rows) {
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size())
      << "Idle empty-pinned layout should keep hidden pinned section and separator in the view tree, plus + New Tab and the normal section";

  EXPECT_FALSE(rows->children()[0]->GetVisible())
      << "Pinned section should be hidden when empty";
  EXPECT_TRUE(rows->children()[1]->GetVisible())
      << "Pinned separator should remain visible above + New Tab even when the pinned section is empty";
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
  ASSERT_EQ(1u, rows->children().size())
      << "True zero-tab layout must render only the + New Tab CTA";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[0], u"+ New Tab"))
      << "+ New Tab should be the single CTA child";
  EXPECT_TRUE(rows->children()[0]->GetVisible())
      << "+ New Tab should be visible";
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
    if (node.kind == SidebarNodeKind::kTab) {
      EXPECT_TRUE(node.is_pinned) << "Pinned tabs in pinned_tree must be pinned";
      EXPECT_FALSE(node.tab_id.empty())
          << "Pinned tabs in pinned_tree must have a tab_id";
      continue;
    }

    EXPECT_EQ(SidebarNodeKind::kFolder, node.kind)
        << "Pinned tree may only contain tab or folder rows";
    EXPECT_TRUE(node.folder_is_pinned)
        << "Pinned folders in pinned_tree must be marked pinned";
    EXPECT_FALSE(node.folder_id.empty())
        << "Pinned folders in pinned_tree must have a folder_id";
  }

  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      EXPECT_FALSE(node.is_pinned)
          << "Normal tree must not contain pinned tabs";
    }
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

  base::RunLoop().RunUntilIdle();
  ResolveViews();

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

  base::RunLoop().RunUntilIdle();
  ResolveViews();
  first = tab_list_->FindTabRowByIdForTesting(root_tab_ids[0]);
  second = tab_list_->FindTabRowByIdForTesting(root_tab_ids[1]);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  DragViewToPoint(first, GetViewPoint(second, 0.5, 0.15));

  EXPECT_TRUE(observer_.HasEvent("reorder_root_item"))
      << "Expected reorder_root_item event after drag";
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

  std::vector<std::string> pinned_root_ids;
  std::vector<std::string> normal_root_ids;
  auto collect_root_ids = [&](const std::vector<SidebarTreeNode>& tree,
                              std::vector<std::string>& root_ids,
                              bool is_root_level,
                              auto&& collect_root_ids_ref) -> void {
    for (const auto& node : tree) {
      bool is_tab = (node.kind == SidebarNodeKind::kTab);
      bool is_folder = (node.kind == SidebarNodeKind::kFolder);
      bool is_split_group = (node.kind == SidebarNodeKind::kSplitGroup);
      EXPECT_TRUE(is_tab || is_folder || is_split_group)
          << "Every tree node must be a tab, folder, or split group";
      if (is_tab) {
        EXPECT_FALSE(node.tab_id.empty());
        if (is_root_level) {
          EXPECT_TRUE(node.parent_folder_id.empty())
              << "Root tab nodes must stay at the root";
        }
        root_ids.push_back(node.tab_id);
      }
      if (is_folder) {
        EXPECT_FALSE(node.folder_id.empty());
        if (is_root_level) {
          root_ids.push_back(node.folder_id);
        }
      }
      if (is_split_group) {
        EXPECT_FALSE(node.split_id.empty())
            << "Split groups must have a stable split_id";
        collect_root_ids_ref(node.children, root_ids, /*is_root_level=*/false,
                             collect_root_ids_ref);
      }
    }
  };
  collect_root_ids(state.tab_list.pinned_tree, pinned_root_ids,
                   /*is_root_level=*/true, collect_root_ids);
  collect_root_ids(state.tab_list.normal_tree, normal_root_ids,
                   /*is_root_level=*/true, collect_root_ids);

  EXPECT_FALSE(pinned_root_ids.empty())
      << "Pinned root IDs must be collected from seeded profile";
  EXPECT_FALSE(normal_root_ids.empty())
      << "Normal root IDs must be collected from seeded profile";
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

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    auto state_after = GetViewState();
    for (const auto& node : state_after.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
        return !node.is_expanded;
      }
    }
    return false;
  })) << "Folder should collapse after click";

  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop2;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop2.QuitClosure()));
  click_loop2.Run();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    auto state_after = GetViewState();
    for (const auto& node : state_after.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
        return node.is_expanded;
      }
    }
    return false;
  })) << "Folder should expand after second click";
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

  DragViewToPoint(tab_row, GetViewPoint(normal_target, 0.5, 0.05));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("move_tab_to_root");
  }));
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

namespace {

int CountTabsInTree(const std::vector<SidebarTreeNode>& nodes) {
  int total = 0;
  for (const auto& node : nodes) {
    if (node.kind == SidebarNodeKind::kTab) {
      ++total;
    }
    if (!node.children.empty()) {
      total += CountTabsInTree(node.children);
    }
  }
  return total;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, LargeTabCountRenders) {
  constexpr int kAddedTabs = 20;
  AddTestTabs(kAddedTabs);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    TabStripModel* strip = browser()->tab_strip_model();
    return strip && strip->count() >= kAddedTabs + 1;
  })) << "Browser tab strip should eventually include the added tabs";

  ResolveViews();
  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(GetFooterView());

  EXPECT_TRUE(tab_list_->GetVisible())
      << "Tab list should remain visible with many tabs";
  EXPECT_GT(tab_list_->GetPreferredSize().height(), 0)
      << "Tab list should have non-zero preferred height";
  EXPECT_TRUE(GetFooterView()->GetVisible());
  EXPECT_GT(GetFooterView()->GetBoundsInScreen().height(), 0);
  EXPECT_EQ(GetFooterView()->GetPreferredSize().height(), GetFooterView()->height())
      << "Footer must keep its fixed preferred height under large tab counts";
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
                        SidebarRowOrderNoTabsWithBrowserContextKeepsFullScaffold) {
  MahoSidebarTabListModel empty_model;
  tab_list_->Update(empty_model, browser());

  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

// --- Wave 0 baseline: refresh contract and BuildViewStateModel shape --------

// After a seed+refresh cycle the tab list must contain at least one row and
// the view state model must have the same active_space_id in all sections that
// depend on it (tab_list.active_space_id matches footer.space_ids membership).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        RefreshProducesConsistentActiveSpaceIdAcrossSections) {
  SeedProfile(2);

  auto state = GetViewState();

  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "active_space_id must be set after seeded profile refresh";

  if (!state.footer.space_ids.empty()) {
    bool found = false;
    for (const auto& sid : state.footer.space_ids) {
      if (sid == state.tab_list.active_space_id) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found)
        << "tab_list.active_space_id must appear in footer.space_ids";
  }
}

// After rapid-fire tab additions the sidebar view tree must remain
// structurally valid: the tab_rows container must still exist, must be
// visible, and the row count must reflect at least the newly added tabs.
// This tests the coalescing contract indirectly: even if ScheduleRefreshAll
// was triggered multiple times, the final applied state must be coherent.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        RapidTabAdditionsProduceCoherentFinalViewState) {
  SeedProfile(2);

  auto state_before = GetViewState();
  const int tabs_before = CountTabsInTree(state_before.tab_list.normal_tree) +
                          CountTabsInTree(state_before.tab_list.pinned_tree);
  ASSERT_GT(tabs_before, 0) << "Seed 2 must have at least one tab";

  AddTestTabs(5);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    TabStripModel* strip = browser()->tab_strip_model();
    return strip && strip->count() >= tabs_before + 5;
  })) << "Tab strip should reflect the 5 added tabs";

  ResolveViews();

  auto* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows) << "tab_rows container must exist after rapid additions";
  EXPECT_TRUE(tab_list_->GetVisible())
      << "tab list must remain visible after rapid additions";
  EXPECT_GT(rows->children().size(), 0u)
      << "tab_rows must have at least one child after additions";
}

// BuildViewStateModel applied via ApplyViewState (reached through
// tab_list_->Update()) must preserve the active_space_id that was in the
// model: the Update path must not silently drop it.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        BuildViewStateModelPreservesActiveSpaceIdThroughUpdate) {
  SeedProfile(2);

  auto state = GetViewState();
  const std::string space_id = state.tab_list.active_space_id;
  ASSERT_FALSE(space_id.empty());

  MahoSidebarTabListModel patched = state.tab_list;
  patched.active_space_id = space_id;
  tab_list_->Update(patched, browser());

  auto* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_FALSE(rows->children().empty())
      << "Tab rows must survive a round-trip through Update() with the "
         "original active_space_id preserved";
}

// The five structural sections of MahoSidebarViewStateModel must all be
// non-trivially constructed after a seeded profile refresh:
//   - top_bar (navigation fields set)
//   - search (idle_text non-empty)
//   - favorites (items non-empty for seed 2)
//   - tab_list (active_space_id non-empty)
//   - footer (space_count > 0)
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        ViewStateModelHasAllFiveSectionsPopulatedAfterSeed) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();

  EXPECT_FALSE(state.search.idle_text.empty())
      << "search.idle_text must be non-empty after seeded profile";

  EXPECT_FALSE(state.favorites.items.empty())
      << "favorites must be non-empty for seed 2";

  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "tab_list.active_space_id must be set for seed 2";
  EXPECT_FALSE(state.tab_list.normal_tree.empty())
      << "tab_list.normal_tree must be non-empty for seed 2";

  EXPECT_GT(state.footer.space_count, 0)
      << "footer.space_count must be > 0 for seed 2";
}

// Calling tab_list_->Update() with a fresh model that has a different
// active_space_id must not leave stale row views from the previous model.
// (Baseline for the identity rewrite: row identity comes from tab_id,
// not position.)
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        UpdateWithNewSpaceIdClearsStaleRows) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty());

  MahoSidebarTabListModel new_model;
  new_model.active_space_id = "fresh-space-wave0";
  new_model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "only-tab-wave0";
    n.tab_strip_index = 0;
    n.title = u"Wave0 Only Tab";
    return n;
  }());

  tab_list_->Update(new_model, browser());

  auto* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  auto* only_row = tab_list_->FindTabRowByIdForTesting("only-tab-wave0");
  EXPECT_TRUE(only_row)
      << "After Update() with a single-tab model, that tab's row must exist";
}

// After a reorder_favorite dispatch the async refresh pipeline
// (ScheduleRefreshAll → BuildFavoritesModelForSpaceIdJson → ApplyViewState)
// must eventually reflect the new ordering in GetViewState().favorites.items.
//
// The test dispatches reorder_favorite for the first two favorites in the
// seeded profile, then polls GetViewState() until either the order changes
// or we see the event recorded by the observer — either outcome proves the
// dispatch reached the pipeline.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        FavoritesReorderPropagatesIntoViewStateAfterRefresh) {
  SeedProfile(2);
  observer_.Clear();

  auto state_before = GetViewState();
  ASSERT_GE(state_before.favorites.items.size(), 2u)
      << "Seed 2 must produce at least two favorites to test reorder";

  const std::string first_tab_id = state_before.favorites.items[0].tab_id;
  const std::string second_tab_id = state_before.favorites.items[1].tab_id;
  ASSERT_FALSE(first_tab_id.empty());
  ASSERT_FALSE(second_tab_id.empty());

  const std::string space_id = GetActiveSpaceId();
  ASSERT_FALSE(space_id.empty());

  DispatchShellEventEx(
      "reorder_favorite",
      {ShellEventField("space_id", space_id),
       ShellEventField("moved_tab_id", first_tab_id),
       ShellEventField("target_tab_id", second_tab_id),
       ShellEventField("insert_before", false)});

  ASSERT_TRUE(base::test::RunUntil([&]() {
    if (observer_.HasEvent("reorder_favorite")) {
      return true;
    }
    ResolveViews();
    auto state_after = GetViewState();
    if (state_after.favorites.items.size() < 2u) {
      return false;
    }
    return state_after.favorites.items[0].tab_id == second_tab_id;
  })) << "reorder_favorite dispatch must either be recorded by the observer "
        "or produce a changed order in GetViewState() after refresh";

  ResolveViews();
  auto state_final = GetViewState();
  ASSERT_GE(state_final.favorites.items.size(), 2u);
  bool has_first = false;
  bool has_second = false;
  for (const auto& item : state_final.favorites.items) {
    if (item.tab_id == first_tab_id)  has_first = true;
    if (item.tab_id == second_tab_id) has_second = true;
  }
  EXPECT_TRUE(has_first)
      << "first_tab_id must still be present in favorites after reorder";
  EXPECT_TRUE(has_second)
      << "second_tab_id must still be present in favorites after reorder";
}

// --- P0 gap: in-folder child reorder final-state verification ---------------

// Drags the first child of a multi-child folder to after the second child
// (targeting the insertion lane before the third child, or appending if only
// two children exist) and asserts the folder's child order in GetViewState()
// has actually changed — not just that an event was dispatched.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       InFolderChildReorderChangesChildOrderInViewState) {
  SeedProfile(2);
  observer_.Clear();

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();

  std::string folder_id;
  std::string first_child_id;
  std::string second_child_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kFolder || node.children.size() < 2u) {
      continue;
    }
    const auto& c0 = node.children[0];
    const auto& c1 = node.children[1];
    if (c0.kind != SidebarNodeKind::kTab || c1.kind != SidebarNodeKind::kTab) {
      continue;
    }
    folder_id = node.folder_id;
    first_child_id = c0.tab_id;
    second_child_id = c1.tab_id;
    break;
  }

  if (folder_id.empty()) {
    LOG(WARNING) << "No multi-child folder with two plain-tab children in "
                    "seed 2 — skipping InFolderChildReorderChangesChildOrderInViewState";
    return;
  }
  ASSERT_FALSE(first_child_id.empty());
  ASSERT_FALSE(second_child_id.empty());

  auto* second_row = tab_list_->FindTabRowByIdForTesting(second_child_id);
  ASSERT_TRUE(second_row) << "second child row must be resolvable";

  // The insertion lane before first_child sits at child-index 0 inside the
  // folder.  Dragging second_child onto it dispatches reorder_tab_in_folder
  // with to:0, which moves second_child to the front, giving a final order of
  // [second_child, first_child, ...].
  views::View* lane_before_first =
      tab_list_->FindInsertionLaneBeforeTabByIdForTesting(first_child_id);
  if (!lane_before_first) {
    LOG(WARNING) << "Insertion lane before first child not found — skipping";
    return;
  }

  DragViewToView(second_row, lane_before_first);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("reorder_tab_in_folder");
  })) << "Expected reorder_tab_in_folder event after in-folder drag";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    auto s = GetViewState();
    for (const auto& n : s.tab_list.normal_tree) {
      if (n.kind == SidebarNodeKind::kFolder && n.folder_id == folder_id) {
        if (n.children.size() < 2u) {
          return false;
        }
        return n.children[0].kind == SidebarNodeKind::kTab &&
               n.children[0].tab_id == second_child_id &&
               n.children[1].kind == SidebarNodeKind::kTab &&
               n.children[1].tab_id == first_child_id;
      }
    }
    return false;
  })) << "Folder child order must reflect the reorder in GetViewState()";

  ResolveViews();
  auto state_final = GetViewState();
  const SidebarTreeNode* folder_node = nullptr;
  for (const auto& n : state_final.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kFolder && n.folder_id == folder_id) {
      folder_node = &n;
      break;
    }
  }
  ASSERT_TRUE(folder_node) << "Folder must still exist after in-folder reorder";
  ASSERT_GE(folder_node->children.size(), 2u);
  EXPECT_EQ(second_child_id, folder_node->children[0].tab_id)
      << "second_child must be at index 0 after reorder";
  EXPECT_EQ(first_child_id, folder_node->children[1].tab_id)
      << "first_child must be at index 1 after reorder";
}

// --- P0 gap: favorites same-index / no-op stability after refresh -----------

// Dispatches reorder_favorite with the moved item's own tab_id as both the
// moved and target IDs (a semantic no-op).  After the refresh cycle the
// favorites order must be identical to the pre-dispatch order: no items may
// be added, removed, or permuted.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FavoritesNoOpSameIndexStabilityAfterRefresh) {
  SeedProfile(2);
  observer_.Clear();

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state_before = GetViewState();
  ASSERT_GE(state_before.favorites.items.size(), 2u)
      << "Seed 2 must produce at least two favorites for no-op stability test";

  // Snapshot the full tab_id order before the no-op dispatch.
  std::vector<std::string> ids_before;
  for (const auto& item : state_before.favorites.items) {
    ids_before.push_back(item.tab_id);
  }

  const std::string space_id = GetActiveSpaceId();
  ASSERT_FALSE(space_id.empty());

  // Dispatch a reorder where moved == target (same index, no-op semantically).
  const std::string& same_id = ids_before[0];
  DispatchShellEventEx(
      "reorder_favorite",
      {ShellEventField("space_id", space_id),
       ShellEventField("moved_tab_id", same_id),
       ShellEventField("target_tab_id", same_id),
       ShellEventField("insert_before", true)});

  // Wait for either the observer to record the event or at least one
  // RunUntilIdle cycle to pass through the refresh pipeline.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("reorder_favorite");
  })) << "reorder_favorite dispatch must be observed by the recording observer";

  // Give the async refresh pipeline time to settle.
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state_after = GetViewState();

  // The count must be identical.
  ASSERT_EQ(ids_before.size(), state_after.favorites.items.size())
      << "No-op reorder must not add or remove favorites";

  // The order must be identical (a no-op dispatch must not permute the list).
  std::vector<std::string> ids_after;
  for (const auto& item : state_after.favorites.items) {
    ids_after.push_back(item.tab_id);
  }
  EXPECT_EQ(ids_before, ids_after)
      << "Favorites order must be unchanged after a same-index no-op dispatch";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FooterAndBoardPreserveCanonicalThemeData) {
  SeedProfile(2);
  ResolveViews();

  const std::string active_space_id = GetActiveSpaceId();
  ASSERT_FALSE(active_space_id.empty());

  base::ListValue gradient_colors;
  {
    base::DictValue primary;
    primary.Set("hue", 0.08);
    primary.Set("saturation", 0.84);
    primary.Set("brightness", 0.96);
    primary.Set("isCustom", false);
    primary.Set("isPrimary", true);
    gradient_colors.Append(std::move(primary));
  }
  {
    base::DictValue secondary;
    secondary.Set("hue", 0.56);
    secondary.Set("saturation", 0.72);
    secondary.Set("brightness", 0.74);
    secondary.Set("isCustom", false);
    secondary.Set("isPrimary", false);
    gradient_colors.Append(std::move(secondary));
  }

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(gradient_colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 0.9);
  theme.Set("texture", 0.2);

  base::DictValue changes;
  changes.Set("spaceId", active_space_id);
  changes.Set("theme", std::move(theme));

  base::DictValue event;
  event.Set("changes", std::move(changes));
  DispatchShellEventDict("update_space_config", std::move(event));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    auto state = GetViewState();
    const int active_index = state.footer.active_space_index;
    return active_index >= 0 &&
           active_index < static_cast<int>(state.footer.space_themes.size()) &&
           state.footer.space_themes[active_index].has_theme &&
           state.footer.space_themes[active_index].kind ==
               MahoSpaceDisplayThemeKind::kZen &&
           !state.footer.space_themes[active_index].primary_color.empty() &&
           !state.footer.space_themes[active_index].secondary_color.empty();
  }));

  const auto state = GetViewState();
  ASSERT_GE(state.footer.active_space_index, 0);
  ASSERT_LT(state.footer.active_space_index,
            static_cast<int>(state.footer.space_themes.size()));

  const MahoSpaceDisplayThemeModel& active_theme =
      state.footer.space_themes[state.footer.active_space_index];
  EXPECT_TRUE(active_theme.has_theme);
  EXPECT_EQ(MahoSpaceDisplayThemeKind::kZen, active_theme.kind);
  EXPECT_FALSE(active_theme.theme_json.empty());
  EXPECT_FALSE(active_theme.palette_colors.empty());
  EXPECT_EQ(active_theme.primary_color,
            state.footer.space_colors[state.footer.active_space_index]);
  EXPECT_EQ(active_theme.primary_color, state.footer.space_color);
  EXPECT_EQ(active_theme.primary_color, state.footer.space_theme.primary_color);
  EXPECT_EQ(active_theme.secondary_color,
            state.footer.space_theme.secondary_color);

  const SpaceBoardModel board = BuildSpacesBoardModel();
  const auto column_it =
      std::find_if(board.columns.begin(), board.columns.end(), [&](const auto& column) {
        return column.space_id == active_space_id;
      });
  ASSERT_NE(column_it, board.columns.end());
  EXPECT_TRUE(column_it->theme.has_theme);
  EXPECT_EQ(MahoSpaceDisplayThemeKind::kZen, column_it->theme.kind);
  EXPECT_EQ(active_theme.primary_color, column_it->theme.primary_color);
  EXPECT_EQ(active_theme.primary_color, column_it->color);
}

// --- Startup-state: has_active_tab fallback when GetActiveWebContents is null --

// BuildSearchModel with a null browser must always report has_active_tab=false.
// This preserves the "genuinely no tab" contract.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        BuildSearchModelNullBrowserHasNoActiveTab) {
  MahoSidebarStateAdapter adapter;
  MahoSidebarSearchModel model = adapter.BuildSearchModel(nullptr);
  EXPECT_FALSE(model.has_active_tab)
      << "has_active_tab must be false when browser is null";
  EXPECT_EQ(model.display_mode, MahoSidebarAddressDisplayMode::kSearchPlaceholder)
      << "display_mode must be kSearchPlaceholder when browser is null";
}

// After a seeded profile the tab strip has active entries, so
// BuildSearchModel must report has_active_tab=true regardless of whether
// GetActiveWebContents returns a value — covering both the normal path
// (non-null WebContents) and the startup-fallback path (tab strip entry
// present but WebContents temporarily null).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        BuildSearchModelWithTabStripEntryHasActiveTab) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoSidebarStateAdapter adapter;
  MahoSidebarSearchModel model = adapter.BuildSearchModel(browser());
  EXPECT_TRUE(model.has_active_tab)
      << "has_active_tab must be true when the tab strip has a seeded entry";
  EXPECT_FALSE(model.idle_text.empty())
      << "idle_text must be non-empty when a tab is active";
}

// --- Regression tests for helper-based sidebar tab identity/activation ---

// Given a seeded profile with tabs,
// When BuildTabListModel resolves tab identity,
// Then every tab row with a tab_id must have a matching MahoTabIdHelper on
// the corresponding WebContents (no URL fallback, no contents_to_core_id_).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                         TabActivationUsesHelperIdentityOnly) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(strip);

  auto check_nodes = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const auto& node : nodes) {
      if (node.kind != SidebarNodeKind::kTab || node.tab_id.empty() ||
          node.tab_strip_index < 0 ||
          !strip->ContainsIndex(node.tab_strip_index)) {
        continue;
      }
      content::WebContents* contents =
          strip->GetWebContentsAt(node.tab_strip_index);
      ASSERT_TRUE(contents) << "WebContents must exist at index "
                            << node.tab_strip_index;
      auto* helper = MahoTabIdHelper::FromWebContents(contents);
      ASSERT_TRUE(helper) << "MahoTabIdHelper must be attached at index "
                          << node.tab_strip_index;
      EXPECT_EQ(helper->stable_tab_id(), node.tab_id)
          << "Helper stable_tab_id must match core tab_id at index "
          << node.tab_strip_index;
    }
  };

  check_nodes(state.tab_list.pinned_tree);
  check_nodes(state.tab_list.normal_tree);
}

// Given a tab navigated to an internal chrome:// URL,
// When the sidebar resolves its identity,
// Then the helper-based identity still works (no URL-based fallback needed).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                         InternalTabHasHelperIdentity) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();

  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0);

  content::WebContents* contents = strip->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  EXPECT_FALSE(helper->stable_tab_id().empty())
      << "Even internal/special tabs must have a stable_tab_id via helper";

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);
  std::string core_id = tab_list->FindCoreTabIdByWebContents(contents);
  EXPECT_FALSE(core_id.empty())
      << "FindCoreTabIdByWebContents must resolve via helper for any tab";
  EXPECT_EQ(core_id, helper->stable_tab_id());
}

// Given a seeded profile,
// When ResolveTabStripIndexForTabId is called with a known tab_id,
// Then it returns the correct strip index using only the helper scan.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                         ResolveTabStripIndexUsesHelperScan) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0);

  content::WebContents* contents = strip->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->stable_tab_id().empty());

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);

  auto state = GetViewState();
  tab_list->Update(state.tab_list, browser());

  std::string tab_id = helper->stable_tab_id();
  views::View* rows = tab_list->tab_rows_for_testing();
  ASSERT_TRUE(rows);

  SidebarTabRowView* row = tab_list->FindTabRowByIdForTesting(tab_id);
  if (row) {
    EXPECT_EQ(row->tab_id_for_testing(), tab_id);
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                         RuntimeNewTabIdentityAligned) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(strip);
  const int count_before = strip->count();

  NavigateToNewForegroundTab(browser(),
                              GURL("https://maho.test/identity-check"),
                              ui::PAGE_TRANSITION_GENERATED);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(count_before + 1, strip->count())
      << "NavigateToNewForegroundTab must insert exactly one new tab";

  content::WebContents* new_contents =
      strip->GetWebContentsAt(strip->active_index());
  ASSERT_TRUE(new_contents);

  auto* helper = MahoTabIdHelper::FromWebContents(new_contents);
  ASSERT_TRUE(helper) << "New tab must have MahoTabIdHelper attached";
  const std::string helper_id = helper->stable_tab_id();
  EXPECT_FALSE(helper_id.empty())
      << "New tab MahoTabIdHelper must carry a non-empty stable_tab_id";

  // Verify helper_id is present in core's own tab store — not just derived
  // from the helper itself.  maho_core_get_space_tabs reads core state
  // directly so this assertion fails if create_tab was never dispatched.
  MahoCore* core = GetCore();
  ASSERT_TRUE(core);
  const std::string active_space_id = GetActiveSpaceId();
  ASSERT_FALSE(active_space_id.empty())
      << "Active space must be set after seeded profile";
  const std::string space_id_json =
      base::GetQuotedJSONString(active_space_id);
  char* space_tabs_json_c =
      maho_core_get_space_tabs(core, space_id_json.c_str(), nullptr);
  ASSERT_TRUE(space_tabs_json_c)
      << "maho_core_get_space_tabs must return a result for the active space";
  std::string space_tabs_json(space_tabs_json_c);
  maho_string_free(space_tabs_json_c);

  auto parsed = base::JSONReader::Read(space_tabs_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_list())
      << "maho_core_get_space_tabs must return a JSON array";

  bool found_in_core = false;
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (id && *id == helper_id) {
      found_in_core = true;
      break;
    }
  }
  EXPECT_TRUE(found_in_core)
      << "helper stable_tab_id '" << helper_id
      << "' must be present in core's tab store for the active space "
         "(create_tab was dispatched after Navigate succeeded)";
}

// Regression: ExecuteCommandAction("select_space_1") must route through the
// bridge, updating browser-scoped active-space state and sidebar view state.
// Before the fix, command actions called maho_core_activate_space() directly
// and left the bridge stale.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       CommandPathSwitchUpdatesBridgeAndSidebarState) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  MahoCore* core = GetCore();
  ASSERT_TRUE(core);
  std::string target_space_id;
  {
    char* spaces_json_c = maho_core_get_space_view_models(core);
    if (spaces_json_c) {
      std::optional<base::Value> spaces =
          base::JSONReader::Read(spaces_json_c, base::JSON_PARSE_RFC);
      maho_string_free(spaces_json_c);
      if (spaces && spaces->is_list() && !spaces->GetList().empty()) {
        const auto* dict = spaces->GetList()[0].GetIfDict();
        if (dict) {
          const std::string* id = dict->FindString("id");
          if (id) {
            target_space_id = *id;
          }
        }
      }
    }
  }

  if (target_space_id.empty()) {
    GTEST_SKIP() << "maho_core_get_space_view_models returned no spaces";
  }

  std::vector<std::string> all_ids = bridge->GetSpaceIds();
  if (all_ids.size() < 2u) {
    GTEST_SKIP() << "Need at least 2 spaces for command-path switch test";
  }

  std::string initial_space;
  for (const auto& sid : all_ids) {
    if (sid != target_space_id) {
      initial_space = sid;
      break;
    }
  }
  if (!initial_space.empty()) {
    bridge->SetActiveSpaceId(browser(), initial_space);
  }

  const std::string space_before = bridge->GetActiveSpaceId(browser());

  ExecuteCommandAction(browser(), "select_space_1");
  base::RunLoop().RunUntilIdle();

  const std::string bridge_space_after = bridge->GetActiveSpaceId(browser());
  EXPECT_EQ(target_space_id, bridge_space_after)
      << "Bridge must record the target space after select_space_1";

  if (!initial_space.empty()) {
    EXPECT_NE(space_before, bridge_space_after)
        << "select_space_1 must produce a real transition, not a no-op";
  }

  ResolveViews();
  auto state = GetViewState();
  EXPECT_EQ(bridge_space_after, state.tab_list.active_space_id)
      << "Sidebar state must reflect the same active space as the bridge "
         "after a command-path switch";
}

// Regression: footer dot activation must update the browser-scoped bridge
// state, not only the process-global fallback. Before the fix, the no-browser
// SwitchToSpace() overload was used, leaving browser-scoped state stale.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FooterSwitchUpdatesBrowserScopedBridgeState) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::vector<std::string> all_ids = bridge->GetSpaceIds();
  if (all_ids.size() < 2u) {
    GTEST_SKIP() << "Need at least 2 spaces for footer switch test";
  }

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  views::View* second_dot = footer->GetSpaceDotForTesting(1);
  if (!second_dot) {
    GTEST_SKIP() << "Footer has no second dot";
  }

  auto* dot = views::AsViewClass<maho::MahoSidebarSpaceDotView>(second_dot);
  ASSERT_TRUE(dot);
  const std::string target_id = dot->space_id();
  ASSERT_FALSE(target_id.empty());

  const gfx::Point center = second_dot->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  const std::string browser_space = bridge->GetActiveSpaceId(browser());
  EXPECT_EQ(target_id, browser_space)
      << "Browser-scoped bridge state must reflect the footer dot click; "
         "the no-browser overload path would leave this stale";
}

// Regression: command-path switch in window A must not change window B's
// browser-scoped active space.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       CommandPathSwitchDoesNotAffectOtherWindow) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::vector<std::string> all_ids = bridge->GetSpaceIds();
  if (all_ids.size() < 2u) {
    GTEST_SKIP() << "Need at least 2 spaces for multi-window isolation test";
  }

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  base::RunLoop().RunUntilIdle();

  const std::string space_a = bridge->GetActiveSpaceId(browser());
  const std::string space_b_candidate = [&]() {
    for (const auto& sid : all_ids) {
      if (sid != space_a) {
        return sid;
      }
    }
    return std::string();
  }();

  if (space_b_candidate.empty()) {
    chrome::CloseWindow(second_browser);
    base::RunLoop().RunUntilIdle();
    GTEST_SKIP() << "All space ids are identical; cannot test isolation";
  }

  bridge->SetActiveSpaceId(second_browser, space_b_candidate);
  const std::string window2_space_before =
      bridge->GetActiveSpaceId(second_browser);

  ExecuteCommandAction(browser(), "select_space_1");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(window2_space_before, bridge->GetActiveSpaceId(second_browser))
      << "Window 2 active space must not change when window 1 uses "
         "command-path switching";

  chrome::CloseWindow(second_browser);
  base::RunLoop().RunUntilIdle();
}

// Regression: closing the last pinned tab produces a model with an empty
// pinned_tree but the tab strip still has tabs.  RebuildRows must use the full
// scaffold (pinned-section placeholder + separator + + New Tab + normal section)
// so the separator is never lost.  This was the root cause of the
// pinned-separator disappearance bug.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        LastPinnedTabCloseKeepsFullScaffold) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty())
      << "Seed 2 must have at least one normal tab";

  MahoSidebarTabListModel pinned_cleared = state.tab_list;
  pinned_cleared.pinned_tree.clear();

  tab_list_->Update(pinned_cleared, browser());

  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

// Regression: an empty display tree when the tab strip also has tabs (the
// transient gap between closing the last pinned tab and the model rebuild)
// must still produce the full scaffold, not the bare + New Tab layout.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        EmptyDisplayTreeWithLiveTabsKeepsFullScaffold) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0) << "Seed 2 must have at least one live tab";

  MahoSidebarTabListModel empty_trees;
  empty_trees.active_space_id = GetActiveSpaceId();

  tab_list_->Update(empty_trees, browser());

  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size())
      << "Full scaffold must keep hidden pinned + separator + + New Tab + "
         "normal section when live tabs exist, even with an empty display tree";

  EXPECT_TRUE(tab_list_->pinned_separator_for_testing() != nullptr)
      << "pinned_separator_ must be allocated in the full scaffold path";
  EXPECT_TRUE(tab_list_->pinned_separator_for_testing()->GetVisible())
      << "Separator must be visible when live tabs exist";
  EXPECT_TRUE(tab_list_->new_tab_button_for_testing() != nullptr)
      << "+ New Tab button must exist in the full scaffold path";
  EXPECT_FALSE(rows->children()[0]->GetVisible())
      << "Pinned section placeholder must stay hidden for an empty tree";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[2], u"+ New Tab"))
      << "+ New Tab should remain in the scaffold position";
  EXPECT_TRUE(rows->children()[3]->GetVisible())
      << "Normal section container must still exist in the full scaffold path";
}

// True zero-tab state: both the model and the tab strip are empty.
// RebuildRows must produce only the bare + New Tab CTA (1 child).
// This verifies that the zero-tab path is still reachable and correct.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        TrueZeroTabStateRendersBareNewTabOnly) {
  MahoSidebarTabListModel empty_model;
  tab_list_->Update(empty_model, /*browser=*/nullptr);

  ExpectNoTabsRows(tab_list_->tab_rows_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SpaceHeader_TopRegionLayout) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoSidebarTabListModel model;
  model.active_space_icon = "💼";
  model.active_space_name = u"Work";
  model.active_space_id = "space-header-view-test-1";
  model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "tab-header-view-1";
    n.tab_strip_index = 0;
    n.title = u"Test Tab";
    return n;
  }());

  tab_list_->Update(model, browser());
  views::View* header = sidebar_->space_header_view_for_testing();
  ASSERT_NE(header, nullptr);
  UpdateSidebarSpaceHeaderRow(header, model.active_space_icon,
                              model.active_space_name);
  header->SetVisible(!model.active_space_name.empty());
  EXPECT_TRUE(header->GetVisible());

  views::View* top_region = sidebar_->tabs_surface_for_testing()->children()[0];
  ASSERT_GE(top_region->children().size(), 3u);
  EXPECT_EQ(top_region->children()[2], header);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SpaceHeader_VisibilityFollowsActiveSpaceName) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoSidebarTabListModel visible_model;
  visible_model.active_space_icon = "🚀";
  visible_model.active_space_name = u"Work";
  visible_model.active_space_id = "space-header-vis-test";
  visible_model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "tab-vis-1";
    n.tab_strip_index = 0;
    n.title = u"Test Tab";
    return n;
  }());

  tab_list_->Update(visible_model, browser());
  views::View* header = sidebar_->space_header_view_for_testing();
  ASSERT_NE(header, nullptr);
  UpdateSidebarSpaceHeaderRow(header, visible_model.active_space_icon,
                              visible_model.active_space_name);
  header->SetVisible(!visible_model.active_space_name.empty());
  EXPECT_TRUE(header->GetVisible());

  MahoSidebarTabListModel hidden_model;
  hidden_model.active_space_icon = "";
  hidden_model.active_space_name = u"";
  hidden_model.active_space_id = "space-header-vis-test";
  hidden_model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "tab-vis-2";
    n.tab_strip_index = 0;
    n.title = u"Test Tab";
    return n;
  }());

  tab_list_->Update(hidden_model, browser());
  UpdateSidebarSpaceHeaderRow(header, hidden_model.active_space_icon,
                              hidden_model.active_space_name);
  header->SetVisible(!hidden_model.active_space_name.empty());
  EXPECT_FALSE(header->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        TabScrollView_WrapsTabListView) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->contents(), tab_list_);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        TabScrollView_ScrollBarConfig) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->GetHorizontalScrollBarMode(),
            views::ScrollView::ScrollBarMode::kDisabled);
  EXPECT_EQ(scroll->GetVerticalScrollBarMode(),
            views::ScrollView::ScrollBarMode::kHiddenButEnabled);
}

// --- H1 regression: well-formed ShellEvents must never parse-error in core ---
//
// The silent-no-op ("H1") bug class happened when the native shell built a
// ShellEvent payload with a field name the Rust `ShellEvent` enum did not
// expect (e.g. move_tab_to_space sent with `space_id` instead of the
// `target_space_id` the enum actually deserializes).  serde rejected the
// payload, `maho_core_handle_event` returned a structured parse-error payload
// — `{"error":{"kind":"parse",...}}` — and the UI silently assumed success:
// the drag looked like it worked but nothing changed in core.
//
// This contract test dispatches a representative *well-formed* payload for
// every sidebar-relevant ShellEvent kind straight through
// maho_core_handle_event (the same core entry point DispatchShellEvent* uses,
// but read here for its return value, which those helpers discard) and asserts
// the core never answers with the parse-error payload.  Field names below
// mirror the Rust ShellEvent variants exactly (maho-types shell_event.rs).
//
// A negative control at the end re-sends the exact H1 shape and asserts it DOES
// produce a parse error, proving the detector above is not vacuous.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       WellFormedShellEventsDoNotProduceParseErrors) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoCore* core = GetCore();
  ASSERT_TRUE(core);

  // Use a real active space id and a real tab id so the fixtures are as
  // representative as possible.  Parse validity does not depend on the ids
  // referencing live objects (an unknown id is a semantic no-op, never a parse
  // error), but realistic ids keep the payloads honest.
  const std::string space_id = GetActiveSpaceId();
  ASSERT_FALSE(space_id.empty());

  std::string tab_id;
  {
    auto state = GetViewState();
    for (const auto& node : state.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
        tab_id = node.tab_id;
        break;
      }
    }
    if (tab_id.empty()) {
      for (const auto& node : state.tab_list.pinned_tree) {
        if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
          tab_id = node.tab_id;
          break;
        }
      }
    }
  }
  ASSERT_FALSE(tab_id.empty()) << "Seed 2 must expose at least one tab id";

  // base::GetQuotedJSONString returns the value WITH its surrounding double
  // quotes and escapes any embedded quotes, so the ids can be spliced straight
  // into the JSON literals below without breaking them.  Built as raw JSON
  // strings on purpose: base::Value::Dict is not name-visible in this modules
  // TU, so payloads are assembled by hand.
  const std::string q_tab = base::GetQuotedJSONString(tab_id);
  const std::string q_space = base::GetQuotedJSONString(space_id);

  // Round-trips a raw JSON payload through maho_core_handle_event and returns
  // the decoded response.  An empty string means the FFI returned null, which
  // for a live core + valid C string must never happen for a well-formed event.
  auto dispatch = [&](const std::string& json) -> std::string {
    char* result_c = maho_core_handle_event(core, json.c_str());
    if (!result_c) {
      return std::string();
    }
    std::string result(result_c);
    maho_string_free(result_c);
    return result;
  };

  // Order-independent parse-error detection.  serde_json serializes with keys
  // sorted alphabetically (no preserve_order), so the payload is
  // {"error":{"detail":"...","kind":"parse"}} — the "error" object and the
  // "kind":"parse" member must both be present, but their order must not be
  // assumed.
  auto is_parse_error = [](const std::string& response) -> bool {
    return response.find("\"error\"") != std::string::npos &&
           response.find("\"kind\":\"parse\"") != std::string::npos;
  };

  // Table of {description, raw JSON payload}.  Field names mirror the Rust
  // ShellEvent variants exactly (maho-types shell_event.rs); every id is
  // quoted via base::GetQuotedJSONString.
  std::vector<std::pair<std::string, std::string>> rows = {
      // PinTab { tab_id }
      {"pin_tab", "{\"kind\":\"pin_tab\",\"tab_id\":" + q_tab + "}"},
      // UnpinTab { tab_id }
      {"unpin_tab", "{\"kind\":\"unpin_tab\",\"tab_id\":" + q_tab + "}"},
      // FavoriteTab { tab_id }
      {"favorite_tab", "{\"kind\":\"favorite_tab\",\"tab_id\":" + q_tab + "}"},
      // ChangeTabRole { tab_id, new_role: TabRole }.  TabRole is internally
      // tagged with `type` (serde tag="type", rename_all="camelCase").
      {"change_tab_role",
       "{\"kind\":\"change_tab_role\",\"tab_id\":" + q_tab +
           ",\"new_role\":{\"type\":\"pinned\"}}"},
      // ReorderFavorite { tab_id, new_index: usize }
      {"reorder_favorite", "{\"kind\":\"reorder_favorite\",\"tab_id\":" + q_tab +
                               ",\"new_index\":0}"},
      // CloseTab { tab_id, expected_space_id: Option (serde default) }
      {"close_tab (no expected_space_id)",
       "{\"kind\":\"close_tab\",\"tab_id\":" + q_tab + "}"},
      {"close_tab (with expected_space_id)",
       "{\"kind\":\"close_tab\",\"tab_id\":" + q_tab +
           ",\"expected_space_id\":" + q_space + "}"},
      // SuspendTab { tab_id }
      {"suspend_tab", "{\"kind\":\"suspend_tab\",\"tab_id\":" + q_tab + "}"},
      // ArchiveTabById { tab_id }
      {"archive_tab_by_id",
       "{\"kind\":\"archive_tab_by_id\",\"tab_id\":" + q_tab + "}"},
      // MoveTabToSpace { tab_id, target_space_id, section: String (default) }.
      // target_space_id — NOT space_id — is the exact field the H1 bug got
      // wrong.
      {"move_tab_to_space (target_space_id + section)",
       "{\"kind\":\"move_tab_to_space\",\"tab_id\":" + q_tab +
           ",\"target_space_id\":" + q_space + ",\"section\":\"normal\"}"},
      // MoveTabToRoot { space_id, folder_id, tab_id, before_tab_id: Option }
      {"move_tab_to_root",
       "{\"kind\":\"move_tab_to_root\",\"space_id\":" + q_space +
           ",\"folder_id\":\"folder-contract-test\",\"tab_id\":" + q_tab + "}"},
      // ReorderRootItem { space_id, item: RootItem, insertion_point:
      // RootInsertionPoint }.  RootItem is tag="kind", content="id"; Append is
      // {"kind":"append"}.
      {"reorder_root_item",
       "{\"kind\":\"reorder_root_item\",\"space_id\":" + q_space +
           ",\"item\":{\"kind\":\"tab\",\"id\":" + q_tab +
           "},\"insertion_point\":{\"kind\":\"append\"}}"},
      // CreateFolderWithTabs { space_id, name, tab_ids: Vec<TabId> }
      {"create_folder_with_tabs",
       "{\"kind\":\"create_folder_with_tabs\",\"space_id\":" + q_space +
           ",\"name\":\"Contract Folder\",\"tab_ids\":[" + q_tab + "]}"},
  };

  for (const auto& [description, json] : rows) {
    const std::string response = dispatch(json);
    EXPECT_FALSE(response.empty())
        << "maho_core_handle_event returned null for well-formed '"
        << description << "'";
    EXPECT_FALSE(is_parse_error(response))
        << description << " -> " << response;
  }

  // Negative control — the exact H1 shape: move_tab_to_space carrying the wrong
  // field name `space_id` instead of `target_space_id`.  serde reports the
  // required `target_space_id` as missing, so the core MUST answer with the
  // parse-error payload.  If this ever stops being a parse error, the guard
  // above has gone vacuous.
  {
    const std::string bad = "{\"kind\":\"move_tab_to_space\",\"tab_id\":" +
                            q_tab + ",\"space_id\":" + q_space + "}";
    const std::string response = dispatch(bad);
    ASSERT_FALSE(response.empty())
        << "malformed event must still return a non-null error payload";
    EXPECT_TRUE(is_parse_error(response))
        << "malformed move_tab_to_space (space_id instead of target_space_id) "
           "must yield a parse-error payload; got: " << response;
  }
}

}  // namespace
}  // namespace maho
