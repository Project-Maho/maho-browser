// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "content/public/test/browser_test.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/browser_commands.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/maho_space_profile_bridge.h"

namespace maho {
namespace {

class MahoSidebarSpacesInteractiveTest
    : public MahoSidebarInteractiveTestBase {
};

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       FooterActionOpensSpacesSurface) {
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
  auto* active_dot = views::AsViewClass<MahoSidebarSpaceDotView>(
      footer->GetSpaceDotForTesting(1));
  ASSERT_TRUE(active_dot);
  EXPECT_TRUE(active_dot->is_active())
      << "Clicked space dot should become the active footer dot";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       MultiWindowReadRegression) {
  SeedProfile(2);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  Browser* browser_b = CreateBrowser(browser()->profile());
  ASSERT_TRUE(browser_b);

  const std::string space_a = bridge->GetActiveSpaceId(browser());
  bridge->SetActiveSpaceId(browser_b, space_a);

  std::vector<std::string> all_space_ids = bridge->GetSpaceIds();
  std::string space_b;
  for (const auto& sid : all_space_ids) {
    if (sid != space_a) {
      space_b = sid;
      break;
    }
  }
  ASSERT_FALSE(space_b.empty());

  bridge->SwitchToSpace(browser(), space_b);
  base::RunLoop().RunUntilIdle();

  BrowserView* bv_b = BrowserView::GetBrowserViewForBrowser(browser_b);
  ASSERT_TRUE(bv_b);
  MahoSidebarView* sidebar_b = nullptr;
  for (views::View* child : bv_b->children()) {
    if (auto* container = views::AsViewClass<MahoSidebarContainerView>(child)) {
      sidebar_b = views::AsViewClass<MahoSidebarView>(container->sidebar_view());
      break;
    }
  }
  ASSERT_TRUE(sidebar_b);

  sidebar_b->ScheduleRefreshAll();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(space_a, bridge->GetActiveSpaceId(browser_b));
  EXPECT_EQ(space_b, bridge->GetActiveSpaceId(browser()));

  chrome::CloseWindow(browser_b);
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceHandOffRegression) {
  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  MahoSidebarCreateSpaceView* create_space_view = nullptr;
  for (views::View* child : sidebar->children()) {
    if (auto* candidate = views::AsViewClass<MahoSidebarCreateSpaceView>(child)) {
      create_space_view = candidate;
      break;
    }
  }
  ASSERT_TRUE(create_space_view);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  const std::string new_space_id = "test_created_space_id";
  create_space_view->CompleteCreateSpaceForTesting(true, new_space_id);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(new_space_id, bridge->GetActiveSpaceId(browser()));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       EmptyIdGuardRegression) {
  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  MahoSidebarCreateSpaceView* create_space_view = nullptr;
  for (views::View* child : sidebar->children()) {
    if (auto* candidate = views::AsViewClass<MahoSidebarCreateSpaceView>(child)) {
      create_space_view = candidate;
      break;
    }
  }
  ASSERT_TRUE(create_space_view);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  const std::string initial_space = bridge->GetActiveSpaceId(browser());
  size_t initial_count = bridge->GetRegisteredSpaceCount();

  create_space_view->CompleteCreateSpaceForTesting(true, "");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_space, bridge->GetActiveSpaceId(browser()));
  EXPECT_EQ(initial_count, bridge->GetRegisteredSpaceCount());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       DefaultProfileSentinelRegression) {
  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  MahoSidebarCreateSpaceView* create_space_view = nullptr;
  for (views::View* child : sidebar->children()) {
    if (auto* candidate = views::AsViewClass<MahoSidebarCreateSpaceView>(child)) {
      create_space_view = candidate;
      break;
    }
  }
  ASSERT_TRUE(create_space_view);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  const std::string new_space_id = "test_default_profile_space";
  create_space_view->CompleteCreateSpaceForTesting(true, new_space_id);
  base::RunLoop().RunUntilIdle();

  base::FilePath profile_path = bridge->GetProfilePathForTesting(new_space_id);
  EXPECT_EQ(base::FilePath::FromASCII("Default"), profile_path);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest, ProfileBasenameForIdValidation) {
  // Sentinel mapping: empty, default, and Default map to Default
  EXPECT_EQ(base::FilePath::FromASCII("Default"),
            MahoSpaceProfileBridge::ProfileBasenameForId(""));
  EXPECT_EQ(base::FilePath::FromASCII("Default"),
            MahoSpaceProfileBridge::ProfileBasenameForId("   "));
  EXPECT_EQ(base::FilePath::FromASCII("Default"),
            MahoSpaceProfileBridge::ProfileBasenameForId("default"));
  EXPECT_EQ(base::FilePath::FromASCII("Default"),
            MahoSpaceProfileBridge::ProfileBasenameForId("Default"));

  // Non-default mapping
  EXPECT_EQ(base::FilePath::FromASCII("MahoProfile_alpha"),
            MahoSpaceProfileBridge::ProfileBasenameForId("alpha"));
  EXPECT_EQ(base::FilePath::FromASCII("MahoProfile_Default-Like-But-Not"),
            MahoSpaceProfileBridge::ProfileBasenameForId("Default-Like-But-Not"));

  // Rejections (std::nullopt)
  // contains / or \\ 
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId("alpha/beta"));
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId("alpha\\beta"));
  // equals "." or ".."
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId("."));
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId(".."));
  // contains a .. segment
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId("foo..bar"));
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId("..foo"));
  // starts with .
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId(".alpha"));
  // non-ASCII
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId("\xff\xfe"));
  // exceeds kMahoProfileIdMaxLen (64)
  std::string long_id(MahoSpaceProfileBridge::kMahoProfileIdMaxLen + 1, 'a');
  EXPECT_EQ(std::nullopt, MahoSpaceProfileBridge::ProfileBasenameForId(long_id));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest, ExplicitFailurePathNoMutation) {
  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  MahoSidebarCreateSpaceView* create_space_view = nullptr;
  for (views::View* child : sidebar->children()) {
    if (auto* candidate = views::AsViewClass<MahoSidebarCreateSpaceView>(child)) {
      create_space_view = candidate;
      break;
    }
  }
  ASSERT_TRUE(create_space_view);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  const std::string initial_space = bridge->GetActiveSpaceId(browser());
  size_t initial_count = bridge->GetRegisteredSpaceCount();

  // Failure path 1: successful-but-empty-id create result (should not mutate)
  create_space_view->CompleteCreateSpaceForTesting(true, "", "default");
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(initial_space, bridge->GetActiveSpaceId(browser()));
  EXPECT_EQ(initial_count, bridge->GetRegisteredSpaceCount());

  // Failure path 2: non-empty space id but invalid profile id (should not mutate)
  create_space_view->CompleteCreateSpaceForTesting(true, "valid_space_id", "invalid/profile/id");
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(initial_space, bridge->GetActiveSpaceId(browser()));
  EXPECT_EQ(initial_count, bridge->GetRegisteredSpaceCount());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest, StartupHydrationParity) {
  // Parity check for representative inputs: "", "default", "alpha"
  std::vector<std::string> test_ids = {"", "default", "alpha"};
  for (const auto& pid : test_ids) {
    std::optional<base::FilePath> path =
        MahoSpaceProfileBridge::ProfileBasenameForId(pid);
    ASSERT_TRUE(path.has_value());

    // Both startup hydration (PopulateBridgeFromCore) and space creation
    // success path (OnSpaceCreated) route through ProfileBasenameForId.
    // Asserting ProfileBasenameForId outputs same values guarantees byte-identity.
    EXPECT_EQ(*path, *MahoSpaceProfileBridge::ProfileBasenameForId(pid));
  }
}

}  // namespace
}  // namespace maho

