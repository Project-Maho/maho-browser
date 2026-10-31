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
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/browser_commands.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"

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

  // The spaces action popover is presented asynchronously after the click
  // lands; wait for it instead of racing a single synchronous check.
  EXPECT_TRUE(::base::test::RunUntil([&]() {
    return footer->command_overlay_controller_visible_for_testing();
  })) << "Expected spaces action popover to be visible after clicking the plus button";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       SpaceDotSwitchesTabList) {
  SeedProfile(2);

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  auto state_before = GetViewState();
  std::string space_id_before = state_before.tab_list.active_space_id;

  // The footer picks up the seeded second space on its next async refresh.
  views::View* second_dot = nullptr;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    second_dot = footer->GetSpaceDotForTesting(1);
    return second_dot != nullptr;
  })) << "Expected second space dot to exist for SeedProfile(2)";

  // The dot was just added; lay it out before aiming the click at it.
  ASSERT_TRUE(second_dot->GetWidget());
  second_dot->GetWidget()->LayoutRootViewIfNecessary();
  const gfx::Point center = second_dot->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();

  // The space switch lands on the sidebar's async refresh.
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    ResolveViews();
    return GetViewState().tab_list.active_space_id != space_id_before;
  }));
  auto state_after = GetViewState();
  EXPECT_NE(space_id_before, state_after.tab_list.active_space_id)
      << "Active space ID should change after clicking a different space dot";
  // The footer re-marks its active dot when the refreshed model lands.
  EXPECT_TRUE(BlockAndPollUntil([&]() {
    auto* active_dot = views::AsViewClass<MahoSidebarSpaceDotView>(
        footer->GetSpaceDotForTesting(1));
    return active_dot && active_dot->is_active();
  })) << "Clicked space dot should become the active footer dot";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       MultiWindowReadRegression) {
  SeedProfile(2);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  Browser* browser_b = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
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

  bridge->SwitchToSpace(static_cast<Browser*>(browser()), space_b);
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

  base::FilePath profile_path = bridge->GetProfilePathForSpace(new_space_id);
  EXPECT_EQ(base::FilePath::FromASCII("Default"), profile_path);
}

// Locks the cache-reset gating from MahoSpaceProfileBridge::SwitchToSpace:
// UpdateFromCore() must return no changed Space IDs when the snapshot is
// unchanged and identify the active Space when its theme changes.
IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       UpdateFromCoreGatesCacheResetOnThemeChange) {
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string active_space = GetActiveSpaceId();
  ASSERT_FALSE(active_space.empty());

  // Apply a known theme, then sync cached state to it. The first sync's return
  // value depends on prior process state, so it is intentionally ignored.
  const std::string theme_a =
      R"({"type":"solid","color":{"hue":240.0,"saturation":0.6,)"
      R"("brightness":0.8,"grain":0.3}})";
  ASSERT_TRUE(maho_theme::ApplyThemeJsonToSpace(core, active_space, theme_a));
  MahoSpaceThemeState::UpdateFromCore();

  // No mutation between calls -> must report no change (skips cache reset).
  EXPECT_TRUE(MahoSpaceThemeState::UpdateFromCore().empty())
      << "UpdateFromCore must identify no changed Spaces for an unchanged snapshot";

  // A genuinely different theme -> must report a change (triggers cache reset).
  const std::string theme_b =
      R"({"type":"solid","color":{"hue":30.0,"saturation":0.9,)"
      R"("brightness":0.5,"grain":0.1}})";
  ASSERT_TRUE(maho_theme::ApplyThemeJsonToSpace(core, active_space, theme_b));
  EXPECT_FALSE(MahoSpaceThemeState::UpdateFromCore().empty())
      << "UpdateFromCore must identify the Space whose theme changed";
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
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  // 1. Simulate PopulateBridgeFromCore behavior (direct registration after helper resolution)
  const std::string space_id_hydration = "space_hydrated";
  std::optional<base::FilePath> path_hydration =
      MahoSpaceProfileBridge::ProfileBasenameForId("alpha");
  ASSERT_TRUE(path_hydration.has_value());
  bridge->RegisterSpace(space_id_hydration, *path_hydration);

  // 2. Simulate OnSpaceCreated behavior by using the testing seam on the view
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

  const std::string space_id_created = "space_created";
  create_space_view->CompleteCreateSpaceForTesting(true, space_id_created, "alpha");
  base::RunLoop().RunUntilIdle();

  // Verify that the resulting profile paths stored in the bridge are identical
  base::FilePath path_from_hydration = bridge->GetProfilePathForSpace(space_id_hydration);
  base::FilePath path_from_creation = bridge->GetProfilePathForSpace(space_id_created);

  EXPECT_FALSE(path_from_hydration.empty());
  EXPECT_EQ(path_from_hydration, path_from_creation);
}

}  // namespace
}  // namespace maho
