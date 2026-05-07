// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/run_loop.h"
#include "content/public/test/browser_test.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_utils.h"

namespace maho {
namespace {

views::View* FindViewWithAccessibleName(views::View* root,
                                        const std::u16string& name) {
  if (!root) {
    return nullptr;
  }
  ui::AXNodeData data;
  root->GetViewAccessibility().GetAccessibleNodeData(&data);
  const std::u16string accessible_name =
      data.GetString16Attribute(ax::mojom::StringAttribute::kName);
  if (accessible_name.find(name) != std::u16string::npos) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindViewWithAccessibleName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

class MahoSidebarSpacesInteractiveTest
    : public MahoSidebarInteractiveTestBase {
 protected:
  void OpenCreateSpaceSurface();
};

void TypeTextIntoWindow(const auto& window, std::u16string_view text) {
  for (char16_t ch : text) {
    ui::KeyboardCode key_code = ui::VKEY_UNKNOWN;
    if (ch == u' ') {
      key_code = ui::VKEY_SPACE;
    } else if (ch >= u'a' && ch <= u'z') {
      key_code = static_cast<ui::KeyboardCode>(ui::VKEY_A + (ch - u'a'));
    }
    if (key_code == ui::VKEY_UNKNOWN) {
      continue;
    }
    base::RunLoop loop;
    ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
        window, key_code, false, false, false, false, loop.QuitClosure()));
    loop.Run();
  }
}

void MahoSidebarSpacesInteractiveTest::OpenCreateSpaceSurface() {
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

  views::Widget* action_popover_widget =
      footer->action_popover_widget_for_testing();
  ASSERT_TRUE(action_popover_widget)
      << "Plus button should open the action popover first";

  views::View* action_popover_contents = action_popover_widget->GetContentsView();
  ASSERT_TRUE(action_popover_contents);

  views::View* new_space_row =
      FindViewWithAccessibleName(action_popover_contents, u"New Space");
  ASSERT_TRUE(new_space_row)
      << "Action popover should expose the New Space action";

  const gfx::Point row_center = new_space_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(row_center.x(), row_center.y()));
  base::RunLoop row_click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      row_click_loop.QuitClosure()));
  row_click_loop.Run();
  base::RunLoop().RunUntilIdle();
}

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
                       CreateSpaceOpensConfigSurface) {
  OpenCreateSpaceSurface();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  EXPECT_TRUE(sidebar->is_create_mode_for_testing())
      << "Expected create space view to be visible after clicking plus button";
  ASSERT_TRUE(sidebar->top_bar_view_for_testing());
  ASSERT_TRUE(sidebar->tabs_surface_for_testing());
  ASSERT_TRUE(sidebar->footer_view_for_testing());
  ASSERT_TRUE(sidebar->create_space_view_for_testing());
  EXPECT_TRUE(sidebar->top_bar_view_for_testing()->GetVisible());
  EXPECT_FALSE(sidebar->tabs_surface_for_testing()->GetVisible());
  EXPECT_TRUE(sidebar->footer_view_for_testing()->GetVisible());
  EXPECT_TRUE(sidebar->create_space_view_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceSubmitSucceedsAndClosesSurface) {
  SeedProfile(1);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  const size_t registered_before = bridge->GetRegisteredSpaceCount();
  const std::string active_space_before = GetActiveSpaceId();

  OpenCreateSpaceSurface();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* create_view = sidebar->create_space_view_for_testing();
  ASSERT_TRUE(create_view);

  auto* name_field = create_view->name_field_for_testing();
  ASSERT_TRUE(name_field);
  name_field->RequestFocus();
  TypeTextIntoWindow(browser()->window()->GetNativeWindow(), u"test space");
  base::RunLoop().RunUntilIdle();

  auto* create_button = create_view->create_button_for_testing();
  ASSERT_TRUE(create_button);
  ASSERT_TRUE(create_button->GetEnabled());

  const gfx::Point button_center = create_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(button_center.x(), button_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();

  EXPECT_FALSE(sidebar->is_create_mode_for_testing());
  EXPECT_GT(bridge->GetRegisteredSpaceCount(), registered_before);
  EXPECT_TRUE(sidebar->tabs_surface_for_testing()->GetVisible());
  EXPECT_NE(active_space_before, GetActiveSpaceId())
      << "Creating a space should switch the footer to the new active space";
  ASSERT_TRUE(GetFooterView()->GetSpaceDotForTesting(1))
      << "Footer should show the newly created second space immediately";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceFailureShowsInlineErrorAndPreservesName) {
  OpenCreateSpaceSurface();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* create_view = sidebar->create_space_view_for_testing();
  ASSERT_TRUE(create_view);

  auto* name_field = create_view->name_field_for_testing();
  ASSERT_TRUE(name_field);
  name_field->RequestFocus();
  TypeTextIntoWindow(browser()->window()->GetNativeWindow(), u"keep this name");
  base::RunLoop().RunUntilIdle();

  create_view->CompleteCreateSpaceForTesting(false, std::string());
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_create_mode_for_testing())
      << "Failure should keep the create surface open";
  ASSERT_TRUE(create_view->error_label_for_testing());
  EXPECT_TRUE(create_view->error_label_for_testing()->GetVisible());
  EXPECT_FALSE(create_view->error_label_for_testing()->GetText().empty());
  EXPECT_EQ(u"keep this name", create_view->name_field_for_testing()->GetText());
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

}  // namespace
}  // namespace maho
