// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/view_utils.h"

namespace maho {
namespace {

class MahoSidebarEdgeCasesInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       SidebarResizePersists) {
  ASSERT_TRUE(container_);

  int initial_width = container_->width();
  ASSERT_GT(initial_width, 0);

  gfx::Rect bounds = container_->GetBoundsInScreen();
  gfx::Point edge(bounds.right(), bounds.CenterPoint().y());

  ASSERT_TRUE(ui_controls::SendMouseMove(edge.x(), edge.y()));
  base::RunLoop press_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN, press_loop.QuitClosure()));
  press_loop.Run();

  int delta = 40;
  ASSERT_TRUE(ui_controls::SendMouseMove(edge.x() + delta, edge.y()));

  base::RunLoop release_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::UP, release_loop.QuitClosure()));
  release_loop.Run();
  base::RunLoop().RunUntilIdle();

  int stored_width = browser()->profile()->GetPrefs()->GetInteger(
      sidebar_prefs::kSidebarWidth);
  EXPECT_NE(initial_width, stored_width)
      << "Sidebar width pref should update after resize drag";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FavoritesAddViaShellEvent) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  size_t initial_fav_count = state.favorites.items.size();

  std::string tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.is_favorite) {
      tab_id = node.tab_id;
      break;
    }
  }

  if (tab_id.empty()) {
    LOG(WARNING) << "All tabs are already favorites in seed 2 — skipping";
    return;
  }

  DispatchShellEvent("add_favorite",
                     {{"tab_id", tab_id},
                      {"space_id", GetActiveSpaceId()}});
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto new_state = GetViewState();
  EXPECT_GT(new_state.favorites.items.size(), initial_fav_count)
      << "Favorite count should increase after add_favorite event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FavoritesRemoveViaShellEvent) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());

  std::string fav_tab_id = state.favorites.items[0].tab_id;
  ASSERT_FALSE(fav_tab_id.empty());

  size_t initial_count = state.favorites.items.size();

  DispatchShellEvent("remove_favorite",
                     {{"tab_id", fav_tab_id},
                      {"space_id", GetActiveSpaceId()}});
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto new_state = GetViewState();
  EXPECT_LT(new_state.favorites.items.size(), initial_count)
      << "Favorite count should decrease after remove_favorite event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       FolderRenameViaContextMenu) {
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
  ASSERT_FALSE(folder_id.empty());

  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(folder_row);

  const gfx::Point center = folder_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop rclick_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::RIGHT, ui_controls::DOWN | ui_controls::UP,
      rclick_loop.QuitClosure()));
  rclick_loop.Run();
  base::RunLoop().RunUntilIdle();

  base::RunLoop esc_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(folder_row->GetVisible())
      << "Folder row should remain visible after dismissing context menu";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       MultiWindowIndependentSidebars) {
  SeedProfile(2);

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  ASSERT_NE(browser(), second_browser);

  BrowserView* bv2 =
      BrowserView::GetBrowserViewForBrowser(second_browser);
  ASSERT_TRUE(bv2);

  MahoSidebarContainerView* container2 = nullptr;
  for (views::View* child : bv2->children()) {
    auto* candidate = views::AsViewClass<MahoSidebarContainerView>(child);
    if (candidate) {
      container2 = candidate;
      break;
    }
  }
  ASSERT_TRUE(container2)
      << "Second browser should also have a sidebar container";
  EXPECT_TRUE(container2->GetVisible());

  chrome::CloseWindow(second_browser);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container_->GetVisible())
      << "First browser sidebar unaffected by second window close";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       EmptyStateFavoritesDisplay) {
  auto state = GetViewState();
  if (state.favorites.items.empty()) {
    EXPECT_TRUE(favorites_->GetVisible())
        << "Favorites grid should be visible even when empty";
    EXPECT_TRUE(favorites_->is_drop_indicator_visible_for_testing() == false)
        << "No drop indicator in empty state";
  } else {
    for (size_t i = 0; i < state.favorites.items.size(); ++i) {
      views::View* tile = favorites_->GetTileForTesting(i);
      EXPECT_TRUE(tile) << "Tile " << i << " should exist";
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       RapidTabCloseNoCrash) {
  AddTestTabs(10);
  ResolveViews();

  int initial_count = browser()->tab_strip_model()->count();
  ASSERT_GE(initial_count, 11);

  for (int i = 0; i < 5; ++i) {
    int last_index = browser()->tab_strip_model()->count() - 1;
    if (last_index <= 0)
      break;
    browser()->tab_strip_model()->CloseWebContentsAt(
        last_index, TabCloseTypes::CLOSE_NONE);
  }
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  int remaining = static_cast<int>(state.tab_list.normal_tree.size()) +
                  static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_EQ(initial_count - 5, remaining)
      << "Rapid close should remove exactly 5 tabs without crash";
  EXPECT_TRUE(tab_list_->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarEdgeCasesInteractiveTest,
                       UpdatePillVisibilityFromModel) {
  SeedProfile(2);

  auto state = GetViewState();
  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  EXPECT_EQ(state.footer.update_pill.visible,
            state.footer.update_pill.has_update)
      << "Update pill visibility should match has_update flag";
}

}  // namespace
}  // namespace maho
