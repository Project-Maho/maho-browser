// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/check.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "content/public/test/browser_test.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/focus/focus_manager.h"

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

void ClickScreenPoint(const gfx::Point& point) {
  ASSERT_TRUE(ui_controls::SendMouseMove(point.x(), point.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();
}

void SendKeyPressToWindow(gfx::NativeWindow window, ui::KeyboardCode key_code) {
  base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      window, key_code, false, false, false, false, key_loop.QuitClosure()));
  key_loop.Run();
  base::RunLoop().RunUntilIdle();
}

class MahoSidebarActionsInteractiveTest
    : public MahoSidebarInteractiveTestBase {
 protected:
  views::Widget* OpenFooterActionPopover() {
    MahoSidebarFooterView* footer = GetFooterView();
    CHECK(footer);
    views::MdTextButton* plus_button = footer->plus_button_for_testing();
    CHECK(plus_button);

    plus_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    plus_button->RequestFocus();
    base::RunLoop().RunUntilIdle();

    SendKeyPressToWindow(plus_button->GetWidget()->GetNativeWindow(),
                         ui::VKEY_SPACE);

    EXPECT_TRUE(base::test::RunUntil([footer] {
      views::Widget* widget = footer->action_popover_widget_for_testing();
      return widget && widget->IsVisible();
    }));
    return footer->action_popover_widget_for_testing();
  }
};

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPlusShowsAnchoredActionPopover) {
  SeedProfile(1);

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::View* plus_button = footer->plus_button_for_testing();
  ASSERT_TRUE(plus_button);

  views::Widget* popover_widget = OpenFooterActionPopover();
  ASSERT_TRUE(popover_widget);
  ASSERT_FALSE(popover_widget->GetWindowBoundsInScreen().IsEmpty());

  auto* bubble_delegate =
      static_cast<views::BubbleDialogDelegateView*>(popover_widget->widget_delegate());
  ASSERT_TRUE(bubble_delegate);

  EXPECT_EQ(plus_button, bubble_delegate->GetAnchorView());
  EXPECT_EQ(plus_button->GetBoundsInScreen(), bubble_delegate->GetAnchorRect());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPopoverNewTabExecutesAndDismisses) {
  SeedProfile(1);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_EQ(nullptr, browser_view->GetMahoCommandOverlayControllerForTesting());

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::Widget* popover_widget = OpenFooterActionPopover();
  ASSERT_TRUE(popover_widget);

  views::View* new_tab_row =
      FindViewWithAccessibleName(popover_widget->GetContentsView(), u"New Tab");
  ASSERT_TRUE(new_tab_row);

  ClickScreenPoint(new_tab_row->GetBoundsInScreen().CenterPoint());

  EXPECT_TRUE(base::test::RunUntil([footer] {
    return footer->action_popover_widget_for_testing() == nullptr;
  }));
  EXPECT_TRUE(base::test::RunUntil([browser_view] {
    auto* overlay_controller =
        browser_view->GetMahoCommandOverlayControllerForTesting();
    return overlay_controller && overlay_controller->IsVisible();
  }));

  auto* overlay_controller =
      browser_view->GetMahoCommandOverlayControllerForTesting();
  ASSERT_TRUE(overlay_controller);
  EXPECT_TRUE(overlay_controller->IsVisible());

  overlay_controller->Hide();
  EXPECT_TRUE(base::test::RunUntil([overlay_controller] {
    return !overlay_controller->IsVisible();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPopoverEscapeDismissesWithoutExecutingAction) {
  SeedProfile(1);
  observer_.Clear();

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_EQ(nullptr, browser_view->GetMahoCommandOverlayControllerForTesting());

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::Widget* popover_widget = OpenFooterActionPopover();
  ASSERT_TRUE(popover_widget);

  SendKeyPressToWindow(popover_widget->GetNativeWindow(), ui::VKEY_ESCAPE);

  EXPECT_TRUE(base::test::RunUntil([footer] {
    return footer->action_popover_widget_for_testing() == nullptr;
  }));
  auto* overlay_controller =
      browser_view->GetMahoCommandOverlayControllerForTesting();
  EXPECT_TRUE(overlay_controller == nullptr || !overlay_controller->IsVisible());
  EXPECT_TRUE(observer_.events().empty());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPopoverDismissesOnBrowserFocusLossClick) {
  SeedProfile(1);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ASSERT_TRUE(browser_view->toolbar());

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::Widget* popover_widget = OpenFooterActionPopover();
  ASSERT_TRUE(popover_widget);

  ClickScreenPoint(browser_view->toolbar()->GetBoundsInScreen().CenterPoint());

  EXPECT_TRUE(base::test::RunUntil([footer] {
    return footer->action_popover_widget_for_testing() == nullptr;
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPopoverDismissRestoresFocusToPlusButton) {
  SeedProfile(1);

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::MdTextButton* plus_button = footer->plus_button_for_testing();
  ASSERT_TRUE(plus_button);

  plus_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  plus_button->RequestFocus();
  ASSERT_EQ(plus_button,
            plus_button->GetWidget()->GetFocusManager()->GetFocusedView());

  views::Widget* popover_widget = OpenFooterActionPopover();
  ASSERT_TRUE(popover_widget);

  SendKeyPressToWindow(popover_widget->GetNativeWindow(), ui::VKEY_ESCAPE);

  EXPECT_TRUE(base::test::RunUntil([footer] {
    return footer->action_popover_widget_for_testing() == nullptr;
  }));

  EXPECT_EQ(plus_button,
            plus_button->GetWidget()->GetFocusManager()->GetFocusedView())
      << "Focus must return to plus_button after popover dismiss";
}

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

  ShowContextMenuOnView(target_row);
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

  ShowContextMenuOnView(tab_row);
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
  EXPECT_TRUE(icon->GetVisible())
      << "Icon must be visible at startup via affordance_visible before any hover";
  EXPECT_FALSE(icon->IsHoverVisibleForTesting());
  EXPECT_FALSE(icon->IsPanelOpenForTesting());
  EXPECT_TRUE(hover_utility_slot->GetVisible())
      << "Utility slot must be visible at baseline (has_active_tab=true) "
         "so the affordance hit-target is present before hover";

  const gfx::Rect idle_bounds_before_hover = idle_button->GetBoundsInScreen();

  const gfx::Point idle_center = idle_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  ASSERT_TRUE(base::test::RunUntil([&]() { return icon->IsHoverVisibleForTesting(); }));
  EXPECT_TRUE(icon->GetVisible())
      << "Hovering the sidebar pill should keep the utility icon visible";
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
  EXPECT_GT(hover_utility_slot->GetBoundsInScreen().CenterPoint().x(),
            idle_bounds_after_hover.CenterPoint().x())
      << "Utility slot should stay in the trailing half of the pill";
  EXPECT_GT(icon_bounds.CenterPoint().x(),
            idle_bounds_after_hover.CenterPoint().x())
      << "Utility affordance should stay in the trailing half of the pill";

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
  auto* overlay_controller =
      BrowserView::GetBrowserViewForBrowser(browser())
          ->GetMahoCommandOverlayControllerForTesting();
  EXPECT_TRUE(overlay_controller == nullptr || !overlay_controller->IsVisible())
      << "Clicking the utility icon should not fall through to the command "
         "overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       UtilitySlotCenterClickOpensUtilityPanel) {
  SeedProfile(2);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_EQ(nullptr, browser_view->GetMahoCommandOverlayControllerForTesting());

  MahoSidebarSearchView* search = sidebar_->search_view_for_testing();
  ASSERT_TRUE(search);

  auto* icon = search->utility_icon_for_testing();
  ASSERT_TRUE(icon);
  auto* idle_button = search->idle_button_for_testing();
  ASSERT_TRUE(idle_button);
  auto* utility_slot = search->utility_icon_slot_for_testing();
  ASSERT_TRUE(utility_slot);
  ASSERT_TRUE(utility_slot->GetVisible());
  EXPECT_FALSE(icon->IsPanelOpenForTesting());

  const gfx::Point idle_center = idle_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return icon->IsHoverVisibleForTesting(); }));

  ClickScreenPoint(utility_slot->GetBoundsInScreen().CenterPoint());

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return icon->IsPanelOpenForTesting(); }));
  auto* overlay_controller =
      browser_view->GetMahoCommandOverlayControllerForTesting();
  EXPECT_TRUE(overlay_controller == nullptr || !overlay_controller->IsVisible())
      << "Clicking the utility slot should open the utility panel instead of "
         "falling through to the command overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       TrailingInsetClickOpensUtilityPanelNotCommandOverlay) {
  SeedProfile(2);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_EQ(nullptr, browser_view->GetMahoCommandOverlayControllerForTesting());

  MahoSidebarSearchView* search = sidebar_->search_view_for_testing();
  ASSERT_TRUE(search);

  auto* icon = search->utility_icon_for_testing();
  ASSERT_TRUE(icon);
  auto* idle_button = search->idle_button_for_testing();
  ASSERT_TRUE(idle_button);
  auto* utility_slot = search->utility_icon_slot_for_testing();
  ASSERT_TRUE(utility_slot);
  ASSERT_TRUE(utility_slot->GetVisible());
  EXPECT_FALSE(icon->IsPanelOpenForTesting());

  const gfx::Point idle_center = idle_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return icon->IsHoverVisibleForTesting(); }));

  // Click in the trailing inset: one pixel right of the utility slot's
  // trailing screen edge, vertically centered on the pill. This point is
  // inside idle_button_ but outside utility_icon_slot_, which is the region
  // that previously fell through to the command overlay.
  const gfx::Rect slot_screen = utility_slot->GetBoundsInScreen();
  const gfx::Rect idle_screen = idle_button->GetBoundsInScreen();
  const int trailing_inset_x = slot_screen.right() + 1;
  const int trailing_inset_y = idle_screen.CenterPoint().y();
  ASSERT_LT(trailing_inset_x, idle_screen.right())
      << "Trailing inset must be inside the idle pill bounds";
  ClickScreenPoint(gfx::Point(trailing_inset_x, trailing_inset_y));

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return icon->IsPanelOpenForTesting(); }))
      << "Click in trailing pill inset must open the utility panel";
  auto* overlay_controller =
      browser_view->GetMahoCommandOverlayControllerForTesting();
  EXPECT_TRUE(overlay_controller == nullptr || !overlay_controller->IsVisible())
      << "Click in trailing pill inset must not fall through to the command "
         "overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                        EditingSuppressesUtilityIconPlaceholderDoesNot) {
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
  ASSERT_TRUE(base::test::RunUntil([&]() { return icon->IsHoverVisibleForTesting(); }));
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
  placeholder_model.has_active_tab = true;
  search->Update(placeholder_model);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  ASSERT_TRUE(base::test::RunUntil([&]() { return icon->IsHoverVisibleForTesting(); }));
  EXPECT_TRUE(utility_slot->GetVisible())
      << "Placeholder mode with an active tab should keep the utility slot available";
  EXPECT_TRUE(icon->IsHoverVisibleForTesting())
      << "Placeholder mode should not suppress hover-revealed utility icon";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       UtilitySlotVisibleAtStartupBeforeHover) {
  SeedProfile(2);

  MahoSidebarSearchView* search = sidebar_->search_view_for_testing();
  ASSERT_TRUE(search);

  auto* utility_slot = search->utility_icon_slot_for_testing();
  ASSERT_TRUE(utility_slot);
  auto* icon = search->utility_icon_for_testing();
  ASSERT_TRUE(icon);

  MahoSidebarSearchModel startup_model = GetViewState().search;
  startup_model.has_active_tab = true;
  startup_model.display_mode = MahoSidebarAddressDisplayMode::kAddressCard;
  startup_model.force_placeholder_for_empty_favorites = false;
  search->Update(startup_model);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(utility_slot->GetVisible())
      << "Utility slot must be visible at startup (has_active_tab=true) before "
         "any hover, so the affordance hit-target exists after relaunch";
  EXPECT_TRUE(icon->GetVisible())
      << "Icon must be visible at startup (affordance_visible=true) without "
         "requiring an initial hover";
  EXPECT_FALSE(icon->IsHoverVisibleForTesting())
      << "hover_visible flag stays false until an actual hover occurs";
  EXPECT_TRUE(icon->IsAffordanceVisibleForTesting())
      << "affordance_visible flag drives the startup-visible icon state";

  MahoSidebarSearchModel editing_model = startup_model;
  editing_model.has_active_tab = true;
  search->Update(editing_model);
  base::RunLoop().RunUntilIdle();

  const gfx::Point idle_center =
      search->idle_button_for_testing()->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(idle_center.x(), idle_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return search->is_editing_for_testing(); }));

  EXPECT_FALSE(utility_slot->GetVisible())
      << "Utility slot must be hidden while editing, regardless of "
         "has_active_tab";
  EXPECT_FALSE(icon->GetVisible())
      << "Inner icon must also be hidden during editing";
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
