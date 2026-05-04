// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"

namespace maho {
namespace {

class MahoSidebarSpacesInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       FooterActionOpensSpacesSurface) {
  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  views::View* library_button = footer->library_button_for_testing();
  ASSERT_TRUE(library_button);

  const gfx::Point center = library_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();

  base::RunLoop().RunUntilIdle();

  content::WebContents* active_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  bool navigated_to_spaces =
      active_contents->GetVisibleURL().spec().find("maho-space") !=
      std::string::npos;
  bool popover_visible =
      footer->command_overlay_controller_visible_for_testing();

  EXPECT_TRUE(navigated_to_spaces || popover_visible)
      << "Expected spaces surface to open via navigation or popover";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceOpensConfigSurface) {
  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  views::View* plus_button = footer->plus_button_for_testing();
  ASSERT_TRUE(plus_button);

  const gfx::Point center = plus_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();

  base::RunLoop().RunUntilIdle();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  EXPECT_TRUE(sidebar->is_create_mode_for_testing())
      << "Expected create space view to be visible after clicking plus button";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       SpaceDotSwitchesTabList) {
  SeedProfile(2);

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  auto state_before = GetViewState();
  std::string space_id_before = state_before.tab_list.active_space_id;

  views::View* second_dot = footer->GetSpaceDotForTesting(1);
  if (!second_dot) {
    LOG(WARNING) << "Only one space in seed 2 — skipping space switch test";
    return;
  }

  const gfx::Point center = second_dot->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  auto state_after = GetViewState();
  EXPECT_NE(space_id_before, state_after.tab_list.active_space_id)
      << "Active space ID should change after clicking a different space dot";
}

}  // namespace
}  // namespace maho
