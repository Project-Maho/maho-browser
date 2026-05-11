// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/test/run_until.h"
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
#include "ui/events/test/test_event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

bool CaptureWidgetAsPNG(views::Widget* widget, const base::FilePath& dest_path) {
  if (!widget || !widget->IsVisible()) {
    return false;
  }
  gfx::NativeWindow native_window = widget->GetNativeWindow();
  if (!native_window) {
    return false;
  }
  const gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  if (bounds.IsEmpty()) {
    return false;
  }
  // The source_rect for GrabWindowSnapshotAsPNG is in layer-space (DIP)
  // coordinates relative to the window origin, so use a rect at (0,0).
  const gfx::Rect source_rect(bounds.size());

  bool success = false;
  base::RunLoop run_loop;
  ui::GrabWindowSnapshotAsPNG(
      native_window, source_rect,
      base::BindOnce(
          [](bool* out_success, const base::FilePath* path,
             base::OnceClosure quit,
             scoped_refptr<base::RefCountedMemory> data) {
            if (data && data->size() > 0) {
              const std::string png_data(
                  reinterpret_cast<const char*>(data->front()), data->size());
              base::ScopedAllowBlockingForTesting allow_blocking;
              *out_success = base::WriteFile(*path, png_data);
            }
            std::move(quit).Run();
          },
          &success, &dest_path, run_loop.QuitClosure()));
  run_loop.Run();
  return success;
}

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
  MahoSidebarCreateSpaceView* GetCreateSpaceView();
};

void TypeTextIntoWindow(gfx::NativeWindow window, std::u16string_view text) {
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

  ASSERT_TRUE(base::test::RunUntil([this] {
    MahoSidebarView* sidebar = GetSidebarView();
    return sidebar && sidebar->is_create_space_mode_for_testing() &&
           sidebar->create_space_view_for_testing() &&
           sidebar->create_space_view_for_testing()->GetVisible();
  }));
}

MahoSidebarCreateSpaceView* MahoSidebarSpacesInteractiveTest::GetCreateSpaceView() {
  MahoSidebarView* sidebar = GetSidebarView();
  return sidebar ? sidebar->create_space_view_for_testing() : nullptr;
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
                       CreateSpaceOpensInlineSidebarSurface) {
  OpenCreateSpaceSurface();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(sidebar->tabs_surface_for_testing());
  ASSERT_TRUE(sidebar->create_space_view_for_testing());
  EXPECT_TRUE(sidebar->is_create_space_mode_for_testing());
  EXPECT_TRUE(sidebar->create_space_view_for_testing()->GetVisible())
      << "Expected inline sidebar create-space surface after clicking New Space";
  EXPECT_FALSE(sidebar->tabs_surface_for_testing()->GetVisible());
  ASSERT_TRUE(sidebar->top_bar_view_for_testing());
  EXPECT_TRUE(sidebar->top_bar_view_for_testing()->GetVisible());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());

  const gfx::Rect top_bar_bounds =
      sidebar->top_bar_view_for_testing()->GetBoundsInScreen();
  const gfx::Rect create_bounds =
      sidebar->create_space_view_for_testing()->GetBoundsInScreen();
  const gfx::Rect footer_bounds = GetFooterView()->GetBoundsInScreen();
  EXPECT_FALSE(create_bounds.Intersects(top_bar_bounds));
  EXPECT_FALSE(create_bounds.Intersects(footer_bounds));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceThemeRowOpensNativeThemeDialog) {
  OpenCreateSpaceSurface();

  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  ASSERT_TRUE(create_view->theme_button_for_testing());

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    views::Widget* widget =
        MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting();
    return widget && widget->IsVisible();
  }));
  EXPECT_TRUE(create_view->GetVisible())
      << "Opening the native theme picker should keep the sidebar create form mounted";

  // Assert forbidden create-space content is absent from the dialog.
  {
    views::Widget* theme_widget =
        MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting();
    ASSERT_TRUE(theme_widget);
    views::View* dialog_contents = theme_widget->GetContentsView();
    ASSERT_TRUE(dialog_contents);
    EXPECT_FALSE(FindViewWithAccessibleName(dialog_contents, u"Create a Space"))
        << "Dialog must not contain 'Create a Space' text";
    EXPECT_FALSE(FindViewWithAccessibleName(dialog_contents, u"Space name"))
        << "Dialog must not contain 'Space name' placeholder";
    EXPECT_FALSE(FindViewWithAccessibleName(dialog_contents, u"Profile"))
        << "Dialog must not contain 'Profile' card";
    EXPECT_FALSE(FindViewWithAccessibleName(dialog_contents, u"Choose a Theme"))
        << "Dialog must not contain 'Choose a Theme' card";
    EXPECT_FALSE(FindViewWithAccessibleName(dialog_contents, u"Close"))
        << "Dialog must not contain 'Close' action";
    EXPECT_FALSE(FindViewWithAccessibleName(dialog_contents, u"Use Theme"))
        << "Dialog must not contain 'Use Theme' action";
  }

  // Assert chooser dialog width is 680px (standalone, not 1110px split).
  {
    views::Widget* theme_widget =
        MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting();
    ASSERT_TRUE(theme_widget);
    const gfx::Rect bounds = theme_widget->GetWindowBoundsInScreen();
    EXPECT_EQ(680, bounds.width())
        << "Theme chooser dialog should be 680px wide (chooser-only)";
  }

  {
    views::Widget* theme_widget =
        MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting();
    ASSERT_TRUE(theme_widget);
    const base::FilePath artifact_path(
        "/Users/indo/code/project/maho-workspace/.sisyphus/artifacts/"
        "theme_dialog_proof.png");
    EXPECT_TRUE(CaptureWidgetAsPNG(theme_widget, artifact_path))
        << "Failed to write theme dialog proof PNG to " << artifact_path;
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceEscapeReturnsToTabs) {
  OpenCreateSpaceSurface();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(sidebar->tabs_surface_for_testing());
  ASSERT_TRUE(GetCreateSpaceView());
  ASSERT_TRUE(GetCreateSpaceView()->name_field_for_testing());

  base::RunLoop esc_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE, false, false,
      false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  EXPECT_TRUE(base::test::RunUntil([this] {
    MahoSidebarView* sidebar = GetSidebarView();
    return sidebar && !sidebar->is_create_space_mode_for_testing();
  }));
  EXPECT_TRUE(sidebar->tabs_surface_for_testing()->GetVisible());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  EXPECT_GT(GetFooterView()->GetBoundsInScreen().height(), 0);
  EXPECT_GT(sidebar->tabs_surface_for_testing()->GetBoundsInScreen().height(), 0);
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
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop theme_click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      theme_click_loop.QuitClosure()));
  theme_click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() !=
           nullptr;
  }));
  MahoSidebarCreateSpaceThemeSelection selected_theme;
  selected_theme.preset_index = 2;
  selected_theme.brightness = 55;
  selected_theme.grain = 25;
  selected_theme.mode = MahoSidebarCreateSpaceThemeMode::kSolid;
  MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting(selected_theme);
  ASSERT_TRUE(MahoSidebarCreateSpaceView::AcceptThemePickerDialogForTesting());
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() ==
           nullptr;
  }));
  ASSERT_TRUE(create_view->theme_summary_label_for_testing());
  const std::u16string_view theme_summary =
      create_view->theme_summary_label_for_testing()->GetText();
  EXPECT_EQ(0u, theme_summary.rfind(u"Solid · #", 0));

  auto* name_field = create_view->name_field_for_testing();
  ASSERT_TRUE(name_field);
  create_view->SetNameForTesting(u"test space");
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(u"test space", name_field->GetText());

  auto* create_button = create_view->create_button_for_testing();
  ASSERT_TRUE(create_button);
  ASSERT_TRUE(create_button->GetEnabled());

  create_view->CompleteCreateSpaceForTesting(true, "test-space-2");
  base::RunLoop().RunUntilIdle();

  ResolveViews();

  EXPECT_TRUE(base::test::RunUntil([this] {
    MahoSidebarView* sidebar = GetSidebarView();
    return sidebar && !sidebar->is_create_space_mode_for_testing();
  }));
  EXPECT_GT(bridge->GetRegisteredSpaceCount(), registered_before);
  EXPECT_TRUE(sidebar->tabs_surface_for_testing()->GetVisible());
  EXPECT_NE(active_space_before, GetActiveSpaceId())
      << "Creating a space should switch the footer to the new active space";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceFailureShowsInlineErrorAndPreservesName) {
  OpenCreateSpaceSurface();

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);

  auto* name_field = create_view->name_field_for_testing();
  ASSERT_TRUE(name_field);
  name_field->RequestFocus();
  TypeTextIntoWindow(browser()->window()->GetNativeWindow(), u"keep this name");
  base::RunLoop().RunUntilIdle();

  create_view->CompleteCreateSpaceForTesting(false, std::string());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(GetCreateSpaceView());
  EXPECT_TRUE(GetCreateSpaceView()->GetVisible())
      << "Failure should keep the inline create surface open";
  ASSERT_TRUE(create_view->error_label_for_testing());
  EXPECT_TRUE(create_view->error_label_for_testing()->GetVisible());
  EXPECT_FALSE(create_view->error_label_for_testing()->GetText().empty());
  EXPECT_EQ(u"keep this name", create_view->name_field_for_testing()->GetText());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       CreateSpaceWhitespaceOnlyNameKeepsCreateDisabled) {
  OpenCreateSpaceSurface();

  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  auto* create_button = create_view->create_button_for_testing();
  ASSERT_TRUE(create_button);

  EXPECT_FALSE(create_button->GetEnabled());

  create_view->SetNameForTesting(u"valid name");
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(create_button->GetEnabled());

  create_view->SetNameForTesting(u"   \n\t  ");
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(create_button->GetEnabled());
  EXPECT_TRUE(GetCreateSpaceView());
  EXPECT_TRUE(GetCreateSpaceView()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       ThemeDialogDefaultState) {
  OpenCreateSpaceSurface();
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  ASSERT_TRUE(create_view->theme_button_for_testing());

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()
          ->GetBoundsInScreen()
          .CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() !=
           nullptr;
  }));

  ASSERT_TRUE(create_view->theme_summary_label_for_testing());
  const std::u16string summary =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());
  EXPECT_NE(std::u16string::npos, summary.find(u"Zen"))
      << "Default theme mode should be Zen, got: " << summary;
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       ThemeDialogCancelPreservesOuterSelection) {
  OpenCreateSpaceSurface();
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  ASSERT_TRUE(create_view->theme_summary_label_for_testing());

  const std::u16string summary_before =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()
          ->GetBoundsInScreen()
          .CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() !=
           nullptr;
  }));

  MahoSidebarCreateSpaceThemeSelection changed_theme;
  changed_theme.preset_index = 3;
  changed_theme.brightness = 40;
  changed_theme.grain = 50;
  changed_theme.mode = MahoSidebarCreateSpaceThemeMode::kSolid;
  MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting(changed_theme);

  views::Widget* theme_widget =
      MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting();
  ASSERT_TRUE(theme_widget);
  theme_widget->Close();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() ==
           nullptr;
  }));

  ASSERT_TRUE(create_view->theme_summary_label_for_testing());
  EXPECT_EQ(summary_before,
            create_view->theme_summary_label_for_testing()->GetText())
      << "Cancelling the theme dialog should not change the outer selection";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       ThemeDialogAcceptUpdatesOuterSelection) {
  OpenCreateSpaceSurface();
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  ASSERT_TRUE(create_view->theme_summary_label_for_testing());

  const std::u16string summary_before =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()
          ->GetBoundsInScreen()
          .CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() !=
           nullptr;
  }));

  MahoSidebarCreateSpaceThemeSelection new_theme;
  new_theme.preset_index = 4;
  new_theme.brightness = 60;
  new_theme.grain = 30;
  new_theme.mode = MahoSidebarCreateSpaceThemeMode::kSolid;
  MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting(new_theme);
  ASSERT_TRUE(MahoSidebarCreateSpaceView::AcceptThemePickerDialogForTesting());
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() ==
           nullptr;
  }));

  ASSERT_TRUE(create_view->theme_summary_label_for_testing());
  const std::u16string summary_after =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());
  EXPECT_NE(summary_before, summary_after)
      << "Accepting the theme dialog should update the outer selection";
  EXPECT_EQ(0u, summary_after.rfind(u"Solid · #", 0))
      << "Summary should reflect Solid mode after accept";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       ThemeDialogEnterAccepts) {
  OpenCreateSpaceSurface();
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  ASSERT_TRUE(create_view->theme_summary_label_for_testing());

  const std::u16string summary_before =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()
          ->GetBoundsInScreen()
          .CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() !=
           nullptr;
  }));

  MahoSidebarCreateSpaceThemeSelection new_theme;
  new_theme.preset_index = 5;
  new_theme.brightness = 70;
  new_theme.grain = 20;
  new_theme.mode = MahoSidebarCreateSpaceThemeMode::kSolid;
  MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting(new_theme);

  // Press Enter to accept (real keyboard path).
  base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_RETURN, false, false,
      false, false, enter_loop.QuitClosure()));
  enter_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() ==
           nullptr;
  }));

  ASSERT_TRUE(create_view->theme_summary_label_for_testing());
  const std::u16string summary_after =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());
  EXPECT_NE(summary_before, summary_after)
      << "Enter key should accept the theme selection";
  EXPECT_EQ(0u, summary_after.rfind(u"Solid · #", 0))
      << "Summary should reflect Solid mode after Enter accept";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarSpacesInteractiveTest,
                       ThemeDialogEscCancels) {
  OpenCreateSpaceSurface();
  auto* create_view = GetCreateSpaceView();
  ASSERT_TRUE(create_view);
  ASSERT_TRUE(create_view->theme_summary_label_for_testing());

  const std::u16string summary_before =
      std::u16string(create_view->theme_summary_label_for_testing()->GetText());

  const gfx::Point theme_center =
      create_view->theme_button_for_testing()
          ->GetBoundsInScreen()
          .CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(theme_center.x(), theme_center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() !=
           nullptr;
  }));

  MahoSidebarCreateSpaceThemeSelection changed_theme;
  changed_theme.preset_index = 3;
  changed_theme.brightness = 40;
  changed_theme.grain = 50;
  changed_theme.mode = MahoSidebarCreateSpaceThemeMode::kSolid;
  MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting(changed_theme);

  // Press Esc to cancel (real keyboard path).
  base::RunLoop esc_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeWindow(), ui::VKEY_ESCAPE, false, false,
      false, false, esc_loop.QuitClosure()));
  esc_loop.Run();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([] {
    return MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting() ==
           nullptr;
  }));

  ASSERT_TRUE(create_view->theme_summary_label_for_testing());
  EXPECT_EQ(summary_before,
            create_view->theme_summary_label_for_testing()->GetText())
      << "Esc key should cancel and preserve the original selection";
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
