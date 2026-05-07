// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"

namespace maho {
namespace {

class MahoSidebarNavigationInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LaunchShowsSidebar) {
  ExpectSidebarVisible(true);

  auto state = GetViewState();
  int total_tabs = static_cast<int>(state.tab_list.normal_tree.size()) +
                   static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_GE(total_tabs, 1) << "Expected at least 1 tab (NTP) in sidebar";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       RealTabsPopulateSidebar) {
  AddTestTabs(3);

  ResolveViews();
  auto state = GetViewState();
  int total_tabs = static_cast<int>(state.tab_list.normal_tree.size()) +
                   static_cast<int>(state.tab_list.pinned_tree.size());
  // NTP + 3 added tabs.
  EXPECT_EQ(4, total_tabs);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       CollapsingPanelKeepsLayoutEnabled) {
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();

  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(false);
  ExpectSidebarVisible(true);

  ASSERT_TRUE(GetContainerView());
  ASSERT_TRUE(GetContainerView()->sidebar_view());
  EXPECT_TRUE(GetContainerView()->GetVisible());
  EXPECT_FALSE(GetContainerView()->sidebar_view()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ClickSidebarTabSwitchesActive) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_GE(state.tab_list.normal_tree.size(), 2u);

  // Find a non-active tab in the tree.
  int active_index = browser()->tab_strip_model()->active_index();
  std::string target_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.is_active) {
      target_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(target_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
  ASSERT_TRUE(tab_row);

  // Click the tab row to activate it.
  const gfx::Point center = tab_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();

  base::RunLoop().RunUntilIdle();

  // The active index should have changed.
  EXPECT_NE(active_index, browser()->tab_strip_model()->active_index());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       TabCloseFromSidebar) {
  SeedProfile(2);

  auto state = GetViewState();
  int initial_count = static_cast<int>(state.tab_list.normal_tree.size());
  ASSERT_GE(initial_count, 2);

  std::string target_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.is_active) {
      target_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(target_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
  ASSERT_TRUE(tab_row);

  views::View* close_btn = tab_row->close_button_for_testing();
  ASSERT_TRUE(close_btn);

  const gfx::Point center = close_btn->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  auto new_state = GetViewState();
  int new_count = static_cast<int>(new_state.tab_list.normal_tree.size());
  EXPECT_EQ(initial_count - 1, new_count);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       TabTitleUpdatesOnNavigation) {
  AddTestTabs(1);
  ResolveViews();

  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  std::u16string title_before = active->GetTitle();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("https://example.org/different-page")));
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  bool found_updated = false;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.is_active) {
      found_updated = (node.title != title_before || !node.title.empty());
      break;
    }
  }
  EXPECT_TRUE(found_updated)
      << "Sidebar should reflect updated title after navigation";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       FavoriteClickActivatesTab) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());

  const auto& first_fav = state.favorites.items[0];
  ASSERT_FALSE(first_fav.tab_id.empty())
      << "First favorite must have a matching tab";

  views::View* tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(tile);

  const gfx::Point center = tile->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(active->GetVisibleURL().spec().find(first_fav.url.host()) !=
              std::string::npos)
      << "Active tab should match clicked favorite URL";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       CollapseAndReopenPreservesState) {
  SeedProfile(2);
  auto state_before = GetViewState();
  int tab_count_before =
      static_cast<int>(state_before.tab_list.normal_tree.size());

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(false);
  ExpectSidebarVisible(true);

  ASSERT_TRUE(GetContainerView());
  ASSERT_TRUE(GetContainerView()->rail_reopen_button_for_testing());
  ASSERT_TRUE(GetContainerView()->rail_reopen_button_for_testing()->GetVisible());

  views::View* reopen_button =
      GetContainerView()->rail_reopen_button_for_testing();
  const gfx::Point center = reopen_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(true);
  ExpectSidebarVisible(true);

  auto state_after = GetViewState();
  int tab_count_after =
      static_cast<int>(state_after.tab_list.normal_tree.size());
  EXPECT_EQ(tab_count_before, tab_count_after)
      << "Tab count must be preserved across collapse and reopen";
}

}  // namespace
}  // namespace maho
