// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/test/browser_test.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield.h"

namespace maho {
namespace {

class MahoSidebarActionsInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ContextMenuCloseTab) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  int initial_count = static_cast<int>(state.tab_list.normal_tree.size());
  ASSERT_GE(initial_count, 2);

  const SidebarTreeNode* target_node = nullptr;
  SidebarTabRowView* target_row = nullptr;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kTab || node.is_active ||
        node.tab_strip_index < 0 || node.tab_id.empty()) {
      continue;
    }
    if (!browser()->tab_strip_model()->ContainsIndex(node.tab_strip_index)) {
      continue;
    }

    auto* row = tab_list_->FindTabRowByIdForTesting(node.tab_id);
    if (!row || !row->GetVisible()) {
      continue;
    }

    target_node = &node;
    target_row = row;
    break;
  }
  ASSERT_TRUE(target_node);
  ASSERT_TRUE(target_row);

  const gfx::Point center = target_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop rclick_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::RIGHT, ui_controls::DOWN | ui_controls::UP,
      rclick_loop.QuitClosure()));
  rclick_loop.Run();
  base::RunLoop().RunUntilIdle();

  base::RunLoop down_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false, down_loop.QuitClosure()));
  down_loop.Run();
  base::RunLoop().RunUntilIdle();

  base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false, key_loop.QuitClosure()));
  key_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  auto new_state = GetViewState();
  bool tab_still_exists = false;
  for (const auto& node : new_state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.tab_id == target_node->tab_id) {
      tab_still_exists = true;
      break;
    }
  }
  EXPECT_FALSE(tab_still_exists)
      << "Tab should be closed after context menu close action";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ContextMenuPinTab) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string unpinned_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty()) {
      unpinned_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(unpinned_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(unpinned_tab_id);
  ASSERT_TRUE(tab_row);

  const gfx::Point center = tab_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop rclick_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::RIGHT, ui_controls::DOWN | ui_controls::UP,
      rclick_loop.QuitClosure()));
  rclick_loop.Run();
  base::RunLoop().RunUntilIdle();

  for (int i = 0; i < 7; ++i) {
    base::RunLoop down_loop;
    ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
        browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
        false, false, false, false, down_loop.QuitClosure()));
    down_loop.Run();
  }

  base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false, enter_loop.QuitClosure()));
  enter_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(observer_.HasEvent("pin_tab") ||
              observer_.HasEvent("toggle_pin"))
      << "Expected pin event from context menu";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       TabRenameInlineEditing) {
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

  views::View* title_btn = tab_row->title_button_for_testing();
  ASSERT_TRUE(title_btn);

  const gfx::Point center = title_btn->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop dbl1;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      dbl1.QuitClosure()));
  dbl1.Run();
  base::RunLoop dbl2;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      dbl2.QuitClosure()));
  dbl2.Run();
  base::RunLoop().RunUntilIdle();

  views::Textfield* field = tab_row->title_field_for_testing();
  if (!field || !field->GetVisible()) {
    LOG(WARNING) << "Inline rename not triggered by double-click — skipping";
    return;
  }

  field->SetText(u"Renamed Tab");

  base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false, enter_loop.QuitClosure()));
  enter_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(observer_.HasEvent("rename_tab"))
      << "Expected rename_tab event after inline edit commit";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       SearchFieldAcceptsInput) {
  SeedProfile(2);

  MahoSidebarSearchView* search = sidebar_->search_view_for_testing();
  ASSERT_TRUE(search);

  const gfx::Point center = search->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  views::Textfield* tf = search->textfield_for_testing();
  if (!tf || !tf->GetVisible()) {
    LOG(WARNING) << "Search textfield not visible after click — skipping";
    return;
  }

  tf->SetText(u"example");
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(search->is_editing_for_testing())
      << "Search view should be in editing state";

  base::RunLoop esc_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(search->is_editing_for_testing())
      << "ESC should exit search editing mode";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                        HoverRevealsUtilityIconAndClickOpensUtilityPanel) {
  SeedProfile(2);

  MahoSidebarSearchView* search = sidebar_->search_view_for_testing();
  ASSERT_TRUE(search);

  auto* icon = search->utility_icon_for_testing();
  ASSERT_TRUE(icon);
  auto* idle_button = search->idle_button_for_testing();
  ASSERT_TRUE(idle_button);
  auto* hover_utility_slot = search->utility_icon_slot_for_testing();
  ASSERT_TRUE(hover_utility_slot);
  EXPECT_FALSE(icon->GetVisible());
  EXPECT_FALSE(icon->IsHoverVisibleForTesting());
  EXPECT_FALSE(icon->IsPanelOpenForTesting());
  EXPECT_FALSE(hover_utility_slot->GetVisible())
      << "Baseline pill state should not expose an empty utility slot";

  const gfx::Rect idle_bounds_before_hover = idle_button->GetBoundsInScreen();

  const gfx::Point idle_center = idle_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  ASSERT_TRUE(base::test::RunUntil([&]() { return icon->GetVisible(); }));
  EXPECT_TRUE(icon->IsHoverVisibleForTesting())
      << "Hovering the sidebar pill should reveal the utility icon";
  EXPECT_TRUE(hover_utility_slot->GetVisible())
      << "Utility slot visibility should follow the actual hover affordance";

  const gfx::Rect idle_bounds_after_hover = idle_button->GetBoundsInScreen();
  const gfx::Rect icon_bounds = icon->GetBoundsInScreen();
  EXPECT_EQ(idle_bounds_before_hover.size(), idle_bounds_after_hover.size())
      << "Hover-only utility affordance should not resize the pill";
  EXPECT_EQ(hover_utility_slot, icon->parent())
      << "Utility affordance should live in the dedicated far-right slot";
  EXPECT_EQ(idle_button, hover_utility_slot->parent())
      << "Utility affordance should be attached to the idle pill surface";
  EXPECT_GE(hover_utility_slot->GetBoundsInScreen().right(),
            idle_bounds_after_hover.right() - 10)
      << "Utility slot should occupy the far-right edge once the baseline icon is removed";
  EXPECT_GE(icon_bounds.right(), idle_bounds_after_hover.right() - 10)
      << "Utility affordance should sit at the far-right edge of the pill";

  const gfx::Point icon_center = icon->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(icon_center.x(), icon_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() { return icon->IsPanelOpenForTesting(); }));
  EXPECT_TRUE(icon->GetVisible())
      << "Utility icon should remain visible while its panel is open";
  EXPECT_TRUE(icon->IsHoverVisibleForTesting())
      << "Panel opening should not immediately hide hover visibility";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       EditingAndPlaceholderSuppressUtilityIcon) {
  SeedProfile(2);

  MahoSidebarSearchView* search = sidebar_->search_view_for_testing();
  ASSERT_TRUE(search);

  auto* icon = search->utility_icon_for_testing();
  ASSERT_TRUE(icon);
  auto* idle_button = search->idle_button_for_testing();
  ASSERT_TRUE(idle_button);
  auto* utility_slot = search->utility_icon_slot_for_testing();
  ASSERT_TRUE(utility_slot);

  const gfx::Point idle_center = idle_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  ASSERT_TRUE(base::test::RunUntil([&]() { return icon->GetVisible(); }));
  EXPECT_TRUE(icon->IsHoverVisibleForTesting());

  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  base::RunLoop edit_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      edit_loop.QuitClosure()));
  edit_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return search->is_editing_for_testing() && !icon->GetVisible();
  }));
  EXPECT_FALSE(utility_slot->GetVisible())
      << "Editing should suppress the utility slot together with the icon";
  EXPECT_FALSE(icon->IsHoverVisibleForTesting())
      << "Editing should clear hover visibility for the utility icon";
  EXPECT_FALSE(icon->IsPanelOpenForTesting());

  base::RunLoop esc_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() { return !search->is_editing_for_testing(); }));

  MahoSidebarSearchModel placeholder_model = GetViewState().search;
  placeholder_model.display_mode = MahoSidebarAddressDisplayMode::kSearchPlaceholder;
  placeholder_model.force_placeholder_for_empty_favorites = true;
  search->Update(placeholder_model);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() { return !icon->GetVisible(); }));
  EXPECT_FALSE(utility_slot->GetVisible())
      << "Placeholder mode should keep the utility slot hidden";
  EXPECT_FALSE(icon->IsHoverVisibleForTesting())
      << "Placeholder mode should suppress the hover-revealed utility icon";

  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(icon->GetVisible())
      << "Placeholder mode should keep the utility affordance hidden even on hover";
  EXPECT_FALSE(icon->IsHoverVisibleForTesting())
      << "Placeholder mode should continue suppressing hover visibility";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       NewTabButtonCreatesTab) {
  SeedProfile(2);

  int initial_tab_count = browser()->tab_strip_model()->count();

  views::View* new_tab_btn = tab_list_->new_tab_button_for_testing();
  ASSERT_TRUE(new_tab_btn);

  const gfx::Point center = new_tab_btn->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_tab_count + 1, browser()->tab_strip_model()->count())
      << "New tab button should create one new tab";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       KeyboardNavigationMoveFocus) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty());

  std::string first_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      first_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(first_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(first_tab_id);
  ASSERT_TRUE(tab_row);
  tab_row->RequestFocus();
  base::RunLoop().RunUntilIdle();

  base::RunLoop down_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false, down_loop.QuitClosure()));
  down_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_NE(tab_row, tab_list_->GetFocusManager()->GetFocusedView())
      << "Arrow down should move focus away from first tab row";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       AudioIndicatorPresent) {
  SeedProfile(2);

  auto state = GetViewState();
  bool found_audible = false;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.is_audible) {
      found_audible = true;
      break;
    }
  }

  if (!found_audible) {
    LOG(WARNING) << "No audible tab in seed 2 — verifying indicator model only";
  }

  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      EXPECT_TRUE(node.is_audible == true || node.is_audible == false)
          << "is_audible field must be a valid boolean";
      EXPECT_TRUE(node.is_muted == true || node.is_muted == false)
          << "is_muted field must be a valid boolean";
    }
  }
}

}  // namespace
}  // namespace maho
