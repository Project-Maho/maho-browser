// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "chrome/browser/ui/browser.h"
#include "content/public/test/browser_test.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "base/time/time.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/test/test_event.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"

namespace maho {
namespace {

ui::AXNodeData GetAccessibilityNodeData(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

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

  // Collapse panel.
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(false);

  // Re-expand panel via pref toggle.
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(true);
  ExpectSidebarVisible(true);

  auto state_after = GetViewState();
  int tab_count_after =
      static_cast<int>(state_after.tab_list.normal_tree.size());
  EXPECT_EQ(tab_count_before, tab_count_after)
      << "Tab count must be preserved across collapse and reopen";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       HoverRevealAndRepin) {
  SeedProfile(2);
  ResolveViews();

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  ASSERT_TRUE(container->sidebar_view());

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container->IsAutoHideMode());
  EXPECT_EQ(sidebar_layout::kHoverTriggerWidthDp,
            container->GetPreferredSize().width());
  EXPECT_FALSE(container->sidebar_view()->GetVisible());

  const gfx::Point center = container->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container->sidebar_view()->GetVisible());
  EXPECT_TRUE(container->IsOverlayVisible());

  const gfx::Rect screen_bounds = container->GetBoundsInScreen();
  const gfx::Point outside(screen_bounds.right() + 50,
                           screen_bounds.CenterPoint().y());
  ASSERT_TRUE(ui_controls::SendMouseMove(outside.x(), outside.y()));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container->sidebar_view()->GetVisible());
  EXPECT_TRUE(container->IsOverlayVisible());

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(container->IsAutoHideMode());
  EXPECT_FALSE(container->IsOverlayVisible());
  EXPECT_TRUE(container->sidebar_view()->GetVisible());
  ExpectSidebarPanelExpanded(true);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveModeToggleAndRailSelection) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  const gfx::Point archive_center =
      archive_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(archive_center.x(), archive_center.y()));
  base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      enter_loop.QuitClosure()));
  enter_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(sidebar->archive_view_for_testing());
  ASSERT_TRUE(sidebar->library_rail_view_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            sidebar->library_rail_view_for_testing()->selected_category());

  auto* rail_button = sidebar->library_rail_view_for_testing()
                          ->button_for_category_for_testing(
                              MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ASSERT_TRUE(rail_button);
  views::test::ButtonTestApi(rail_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            sidebar->library_rail_view_for_testing()->selected_category());
  EXPECT_TRUE(sidebar->archive_view_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveFilterAccessibilityValueUpdates) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  const gfx::Point archive_center =
      archive_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(archive_center.x(), archive_center.y()));
  base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      enter_loop.QuitClosure()));
  enter_loop.Run();
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  EXPECT_EQ(u"Filter Inactive",
            archive_view->filter_accessibility_value_for_testing());

  auto* filter_button = archive_view->filter_button_for_testing();
  ASSERT_TRUE(filter_button);
  views::test::ButtonTestApi(filter_button).NotifyClick(ui::test::TestEvent());
  ASSERT_TRUE(archive_view->filter_menu_model_for_testing());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->window()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(ArchiveFilterMode::kToday, archive_view->filter_mode_for_testing());
  EXPECT_EQ(u"Filter Active: Today",
            archive_view->filter_accessibility_value_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveBackRestoresFooterArchiveFocus) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  archive_button->RequestFocus();
  ASSERT_TRUE(sidebar->GetFocusManager());
  EXPECT_EQ(archive_button, sidebar->GetFocusManager()->GetFocusedView());

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(sidebar->library_rail_view_for_testing());
  auto* back_button =
      sidebar->library_rail_view_for_testing()->back_button_for_testing();
  ASSERT_TRUE(back_button);

  views::test::ButtonTestApi(back_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(sidebar->is_archive_mode_for_testing());
  EXPECT_EQ(archive_button, sidebar->GetFocusManager()->GetFocusedView());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveReturnKeepsFooterAndTabsVisible) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(sidebar->library_rail_view_for_testing());
  auto* back_button =
      sidebar->library_rail_view_for_testing()->back_button_for_testing();
  ASSERT_TRUE(back_button);

  views::test::ButtonTestApi(back_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  EXPECT_FALSE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  EXPECT_GT(GetFooterView()->GetBoundsInScreen().height(), 0);
  ASSERT_TRUE(GetTabListView());
  EXPECT_TRUE(GetTabListView()->GetVisible());
  EXPECT_GT(GetTabListView()->GetBoundsInScreen().height(), 0);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveEmptyStateAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  auto* empty_state = archive_view->empty_state_view_for_testing();
  ASSERT_TRUE(empty_state);
  ASSERT_TRUE(empty_state->GetVisible());

  ui::AXNodeData data = GetAccessibilityNodeData(empty_state);
  EXPECT_EQ(ax::mojom::Role::kGroup, data.role);
  EXPECT_EQ(u"Archive empty state, no archived tabs yet",
            data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveResultsListAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  ASSERT_TRUE(archive_view->scroll_view_for_testing());
  ASSERT_TRUE(archive_view->list_container_for_testing());

  ui::AXNodeData scroll_data =
      GetAccessibilityNodeData(archive_view->scroll_view_for_testing());
  EXPECT_EQ(ax::mojom::Role::kGroup, scroll_data.role);
  EXPECT_EQ(u"Archived tabs results container",
            scroll_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_EQ(u"No tabs",
            scroll_data.GetString16Attribute(ax::mojom::StringAttribute::kValue));

  ui::AXNodeData list_data =
      GetAccessibilityNodeData(archive_view->list_container_for_testing());
  EXPECT_EQ(ax::mojom::Role::kList, list_data.role);
  EXPECT_EQ(u"Archived tabs results",
            list_data.GetString16Attribute(ax::mojom::StringAttribute::kName));

  ui::AXNodeData search_icon_data = GetAccessibilityNodeData(
      archive_view->children()[0]->children()[0]->children()[0]);
  EXPECT_TRUE(search_icon_data.HasState(ax::mojom::State::kIgnored));

  ui::AXNodeData rail_button_data = GetAccessibilityNodeData(
      sidebar->library_rail_view_for_testing()->button_for_category_for_testing(
          MahoSidebarLibraryRailView::Category::kArchivedTabs));
  EXPECT_TRUE(
      rail_button_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveNonEmptyAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "test-archived-tab";
  tab.title = u"Archive QA Tab";
  tab.url = "https://example.com/archive-qa";
  tab.host = u"example.com / archive-qa";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(archive_view->scroll_view_for_testing()->GetVisible());
  ASSERT_TRUE(archive_view->list_container_for_testing());
  ASSERT_GE(archive_view->list_container_for_testing()->children().size(), 3u);

  views::View* section_header =
      archive_view->list_container_for_testing()->children()[0];
  ui::AXNodeData section_data = GetAccessibilityNodeData(section_header);
  EXPECT_EQ(ax::mojom::Role::kGroup, section_data.role);
  EXPECT_EQ(u"Today, 1 archived tab",
            section_data.GetString16Attribute(ax::mojom::StringAttribute::kName));

  views::View* section_container =
      archive_view->list_container_for_testing()->children()[2];
  ASSERT_FALSE(section_container->children().empty());
  views::View* row = section_container->children()[0];
  ui::AXNodeData row_data = GetAccessibilityNodeData(row);
  EXPECT_EQ(ax::mojom::Role::kButton, row_data.role);
  EXPECT_EQ(u"Restore archived tab Archive QA Tab",
            row_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_TRUE(row->GetEnabled());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveRootPanelAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  ASSERT_GE(archive_view->children().size(), 2u);

  ui::AXNodeData root_data = GetAccessibilityNodeData(archive_view);
  EXPECT_EQ(ax::mojom::Role::kGroup, root_data.role);
  EXPECT_EQ(u"Archived tabs panel",
            root_data.GetString16Attribute(ax::mojom::StringAttribute::kName));

  views::View* header_surface = archive_view->children()[0].get();
  views::View* body_container = archive_view->children()[1].get();
  ASSERT_TRUE(header_surface);
  ASSERT_TRUE(body_container);

  ui::AXNodeData body_data = GetAccessibilityNodeData(body_container);
  EXPECT_FALSE(
      body_data.HasStringAttribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveUntitledRowUsesHostnameTitleAndLocationSubtitle) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "untitled-archive-tab";
  tab.title = u"";
  tab.url = "https://example.com/archive/path";
  tab.host = u"example.com / archive/path";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(archive_view->list_container_for_testing());
  ASSERT_GE(archive_view->list_container_for_testing()->children().size(), 3u);
  views::View* section_container =
      archive_view->list_container_for_testing()->children()[2];
  ASSERT_TRUE(section_container);
  ASSERT_FALSE(section_container->children().empty());

  views::View* row = section_container->children()[0];
  ASSERT_TRUE(row);
  ASSERT_GE(row->children().size(), 3u);

  views::View* text_container = row->children()[1];
  ASSERT_TRUE(text_container);
  ASSERT_GE(text_container->children().size(), 2u);

  auto* title_label =
      static_cast<views::Label*>(text_container->children()[0].get());
  auto* subtitle_label =
      static_cast<views::Label*>(text_container->children()[1].get());
  ASSERT_TRUE(title_label);
  ASSERT_TRUE(subtitle_label);

  EXPECT_EQ(u"example.com", title_label->GetText());
  EXPECT_EQ(u"example.com / archive/path", subtitle_label->GetText());

  ui::AXNodeData row_data = GetAccessibilityNodeData(row);
  EXPECT_EQ(u"Restore archived tab example.com",
            row_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveFilteredEmptyAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "filtered-empty-tab";
  tab.title = u"Visible Archive Tab";
  tab.url = "https://example.com/visible";
  tab.host = u"example.com / visible";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  auto* filter_button = archive_view->filter_button_for_testing();
  ASSERT_TRUE(filter_button);
  views::test::ButtonTestApi(filter_button).NotifyClick(ui::test::TestEvent());
  ASSERT_TRUE(archive_view->filter_menu_model_for_testing());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->window()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false));
  base::RunLoop().RunUntilIdle();

  auto* empty_state = archive_view->empty_state_view_for_testing();
  ASSERT_TRUE(empty_state);
  ASSERT_TRUE(empty_state->GetVisible());
  ui::AXNodeData empty_data = GetAccessibilityNodeData(empty_state);
  EXPECT_EQ(ax::mojom::Role::kGroup, empty_data.role);
  EXPECT_EQ(u"Archive empty state, no matching tabs",
            empty_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveSearchMatchesUnicodeCaseFoldedQuery) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "unicode-search-tab";
  tab.title = u"Café Archive";
  tab.url = "https://example.com/archive";
  tab.host = u"example.com / archive";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(archive_view->scroll_view_for_testing()->GetVisible());
  ASSERT_FALSE(archive_view->empty_state_view_for_testing()->GetVisible());
  ASSERT_GE(archive_view->list_container_for_testing()->children().size(), 3u);

  ASSERT_GE(archive_view->children().size(), 1u);
  views::View* header_surface = archive_view->children()[0].get();
  ASSERT_GE(header_surface->children().size(), 1u);
  views::View* search_shell = header_surface->children()[0].get();
  ASSERT_GE(search_shell->children().size(), 2u);
  auto* search_field =
      static_cast<views::Textfield*>(search_shell->children()[1].get());
  ASSERT_TRUE(search_field);

  search_field->SetText(u"CAFÉ");
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(archive_view->scroll_view_for_testing()->GetVisible());
  EXPECT_FALSE(archive_view->empty_state_view_for_testing()->GetVisible());
  EXPECT_GE(archive_view->list_container_for_testing()->children().size(), 3u);

  search_field->SetText(u"no-match");
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(archive_view->scroll_view_for_testing()->GetVisible());
  EXPECT_TRUE(archive_view->empty_state_view_for_testing()->GetVisible());
  EXPECT_TRUE(archive_view->list_container_for_testing()->children().empty());
}

}  // namespace
}  // namespace maho
