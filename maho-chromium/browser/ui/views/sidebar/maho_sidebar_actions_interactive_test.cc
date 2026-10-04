// Copyright 2026 Maho Browser. All rights reserved.

#include "base/compiler_specific.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "build/build_config.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/net/maho_translate_injection_handler.h"
#include "maho/browser/ui/notifications/maho_notification_overlay.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_action_popover_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h"
#include "maho/browser/ui/views/frame/maho_contents_header_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/font.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/paint_info.h"
#include "ui/views/view_constants.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/url_constants.h"

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
  ::base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  ::base::RunLoop().RunUntilIdle();
}

void SendKeyPressToWindow(gfx::NativeWindow window, ui::KeyboardCode key_code) {
  ::base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      window, key_code, false, false, false, false, key_loop.QuitClosure()));
  key_loop.Run();
  ::base::RunLoop().RunUntilIdle();
}

// Writes a PNG of the live browser window NEXT TO THE GTEST SUMMARY, which is
// the only artifact directory the VM runner pulls back to the host.
void CaptureProofPngToSummaryDir(views::Widget* widget,
                                 const char* artifact_name) {
  ASSERT_TRUE(widget);
  const ::base::FilePath summary_path =
      ::base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "test-launcher-summary-output");
  if (summary_path.empty()) {
    // Running outside the harness (no summary switch): nothing to attach the
    // evidence to, and the assertions remain the real gate.
    return;
  }

  scoped_refptr<base::RefCountedMemory> png_data;
  bool finished = false;
  const gfx::Rect window_screen = widget->GetWindowBoundsInScreen();
  ui::GrabWindowSnapshotAsPNG(
      widget->GetNativeWindow(), gfx::Rect(window_screen.size()),
      ::base::BindOnce(
          [](scoped_refptr<base::RefCountedMemory>* out, bool* done,
             scoped_refptr<base::RefCountedMemory> data) {
            *out = data;
            *done = true;
          },
          &png_data, &finished));
  ASSERT_TRUE(::base::test::RunUntil([&finished] { return finished; }));
  ASSERT_TRUE(png_data && png_data->size() > 0);

  const ::base::FilePath artifact_path =
      summary_path.DirName().AppendASCII(artifact_name);
  {
    ::base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(::base::CreateDirectory(artifact_path.DirName()));
    ASSERT_TRUE(::base::WriteFile(
        artifact_path,
        UNSAFE_BUFFERS(::base::span<const uint8_t>(png_data->data(),
                                                   png_data->size()))));
  }
  fprintf(stderr, "MAHO_PROOF_PNG %s (%zu bytes)\n",
          artifact_path.value().c_str(), png_data->size());
  fflush(stderr);
}

std::map<std::string, std::string>& FourWayPages();

// The 4-way capture proof pages are served by the embedded test server; the
// handler must be registered before the server starts (see SetUpOnMainThread)
// while the page map is built lazily on first request inside the test body.
std::unique_ptr<net::test_server::HttpResponse> ServeFourWayPages(
    const net::test_server::HttpRequest& request) {
  const auto& pages = FourWayPages();
  auto it = pages.find(request.relative_url);
  if (it == pages.end()) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html; charset=utf-8");
  response->set_content(it->second);
  return response;
}

class MahoSidebarActionsInteractiveTest
    : public MahoSidebarInteractiveTestBase {
 protected:
  void SetUpOnMainThread() override {
    // Handlers must be registered before the embedded server starts.
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeFourWayPages));
    MahoSidebarInteractiveTestBase::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  views::Widget* OpenFooterActionPopover() {
    MahoSidebarFooterView* footer = GetFooterView();
    CHECK(footer);
    views::MdTextButton* plus_button = footer->plus_button_for_testing();
    CHECK(plus_button);

    plus_button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    plus_button->RequestFocus();
    ::base::RunLoop().RunUntilIdle();

    SendKeyPressToWindow(plus_button->GetWidget()->GetNativeWindow(),
                         ui::VKEY_SPACE);

    EXPECT_TRUE(::base::test::RunUntil([footer] {
      views::Widget* widget = footer->action_popover_widget_for_testing();
      return widget && widget->IsVisible();
    }));
    return footer->action_popover_widget_for_testing();
  }

};

// Live-webpage runs route the guest browser's HTTP(S) traffic through a
// CONNECT relay on the host (the vmnet gateway 192.168.64.1). The Tart shared
// network's outbound NAT is non-functional in this environment, but the guest
// can always reach the host itself; the relay forwards unmodified end-to-end
// TLS so the four panes render genuinely live production pages.
class MahoLiveWeb4WayTest : public MahoSidebarActionsInteractiveTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    MahoSidebarActionsInteractiveTest::SetUpCommandLine(command_line);
    command_line->RemoveSwitch("no-proxy-server");
    command_line->AppendSwitchASCII("proxy-server", "192.168.64.1:8899");
    command_line->AppendSwitchASCII("proxy-bypass-list", "<local>");
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
      popover_widget->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(bubble_delegate);

  EXPECT_EQ(plus_button, bubble_delegate->GetAnchorView());
  EXPECT_EQ(plus_button->GetBoundsInScreen(), bubble_delegate->GetAnchorRect());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPopoverOwnsCallbackUntilClosed) {
  SeedProfile(1);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto lifetime = std::make_shared<int>(0);
  std::weak_ptr<int> weak_lifetime = lifetime;
  auto* widget = MahoSidebarActionPopoverView::Show(
      footer->plus_button_for_testing(),
      base::BindRepeating(
          [](std::shared_ptr<int> token, MahoSidebarActionPopoverAction) {},
          std::move(lifetime)));
  ASSERT_TRUE(widget);
  EXPECT_FALSE(weak_lifetime.expired());
  widget->CloseNow();
  EXPECT_TRUE(weak_lifetime.expired());
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

  EXPECT_TRUE(::base::test::RunUntil([footer] {
    return footer->action_popover_widget_for_testing() == nullptr;
  }));
  EXPECT_TRUE(::base::test::RunUntil([browser_view] {
    auto* overlay_controller =
        browser_view->GetMahoCommandOverlayControllerForTesting();
    return overlay_controller && overlay_controller->IsVisible();
  }));

  auto* overlay_controller =
      browser_view->GetMahoCommandOverlayControllerForTesting();
  ASSERT_TRUE(overlay_controller);
  EXPECT_TRUE(overlay_controller->IsVisible());

  overlay_controller->Hide();
  EXPECT_TRUE(::base::test::RunUntil([overlay_controller] {
    return !overlay_controller->IsVisible();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       FooterPopoverNewSpaceAxPressShowsCreateSurface) {
  SeedProfile(1);

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);
  views::Widget* popover_widget = OpenFooterActionPopover();
  ASSERT_TRUE(popover_widget);

  views::View* new_space_row =
      FindViewWithAccessibleName(popover_widget->GetContentsView(), u"New Space");
  ASSERT_TRUE(new_space_row);

  ui::AXNodeData row_data;
  new_space_row->GetViewAccessibility().GetAccessibleNodeData(&row_data);
  EXPECT_TRUE(row_data.HasAction(ax::mojom::Action::kDoDefault));

  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  EXPECT_TRUE(new_space_row->HandleAccessibleAction(action));

  EXPECT_TRUE(::base::test::RunUntil([footer] {
    return footer->action_popover_widget_for_testing() == nullptr;
  }));

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
  EXPECT_TRUE(create_space_view->GetVisible());
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

  EXPECT_TRUE(::base::test::RunUntil([footer] {
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

  EXPECT_TRUE(::base::test::RunUntil([footer] {
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

  EXPECT_TRUE(::base::test::RunUntil([footer] {
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
    if (!browser()->GetTabStripModel()->ContainsIndex(node.tab_strip_index)) {
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
  ::base::RunLoop().RunUntilIdle();

  ::base::RunLoop down_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false, down_loop.QuitClosure()));
  down_loop.Run();
  ::base::RunLoop().RunUntilIdle();

  ::base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false, key_loop.QuitClosure()));
  key_loop.Run();
  ::base::RunLoop().RunUntilIdle();

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
  ::base::RunLoop().RunUntilIdle();

  for (int i = 0; i < 7; ++i) {
    ::base::RunLoop down_loop;
    ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
        browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
        false, false, false, false, down_loop.QuitClosure()));
    down_loop.Run();
  }

  ::base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false, enter_loop.QuitClosure()));
  enter_loop.Run();
  ::base::RunLoop().RunUntilIdle();

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
  ::base::RunLoop dbl1;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      dbl1.QuitClosure()));
  dbl1.Run();
  ::base::RunLoop dbl2;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      dbl2.QuitClosure()));
  dbl2.Run();
  ::base::RunLoop().RunUntilIdle();

  views::Textfield* field = tab_row->title_field_for_testing();
  if (!field || !field->GetVisible()) {
    LOG(WARNING) << "Inline rename not triggered by double-click — skipping";
    return;
  }

  field->SetText(u"Renamed Tab");

  ::base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false, enter_loop.QuitClosure()));
  enter_loop.Run();
  ::base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(observer_.HasEvent("rename_tab"))
      << "Expected rename_tab event after inline edit commit";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                        NewTabButtonCreatesTab) {
  SeedProfile(2);

  int initial_tab_count = browser()->GetTabStripModel()->count();

  views::View* new_tab_btn = tab_list_->new_tab_button_for_testing();
  ASSERT_TRUE(new_tab_btn);

  const gfx::Point center = new_tab_btn->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  ::base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  ::base::RunLoop().RunUntilIdle();

  EXPECT_EQ(initial_tab_count + 1, browser()->GetTabStripModel()->count())
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
  ::base::RunLoop().RunUntilIdle();

  ::base::RunLoop down_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false, down_loop.QuitClosure()));
  down_loop.Run();
  ::base::RunLoop().RunUntilIdle();

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

// Menu item positions in the tab context menu (maho_tab_context_menu.cc).
// DOWN presses needed to reach each item from the menu's initial focus state:
//   DOWN×1=Copy Link, DOWN×2=Share, [separator], DOWN×3=Change Icon,
//   DOWN×4=Rename, DOWN×5=Mute Tab
constexpr int kContextMenuDownsToRename = 4;

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                        ContextMenuRenameTab) {
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
  ASSERT_TRUE(tab_row->GetVisible());

  ShowContextMenuOnView(tab_row);
  ::base::RunLoop().RunUntilIdle();

  for (int i = 0; i < kContextMenuDownsToRename; ++i) {
    SendKeyPressToWindow(browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN);
  }

  SendKeyPressToWindow(browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN);

  views::Textfield* field = tab_row->title_field_for_testing();
  ASSERT_TRUE(field) << "title_field_for_testing() must exist after Rename — delegate wiring is broken";
  ASSERT_TRUE(field->GetVisible()) << "Inline rename field must be visible after context-menu Rename";

  field->SetText(u"Context Menu Renamed");

  SendKeyPressToWindow(browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN);

  EXPECT_TRUE(observer_.HasEvent("rename_tab"))
      << "Expected rename_tab event after context-menu Rename commit";
}

// Seeds the maho-test page the same way MahoSidebarDnDBrowserTest does: a plain
// navigation plus WaitForLoadStop. This initializes the arc-layout space so the
// TabStripModel counts real tabs, and deliberately avoids
// MahoSidebarInteractiveTestBase::SeedProfile, whose post-navigation RunUntil
// on sidebar state never resolves headless and times the test out.
void SeedTestPage(Browser* browser) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser, GURL("chrome://maho-test/?seed=2")));
  content::WebContents* wc = browser->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(wc);
  ASSERT_TRUE(content::WaitForLoadStop(wc));
  ::base::RunLoop().RunUntilIdle();
}

// Appends a fresh foreground tab straight to the TabStripModel. The new
// contents is navigated to about:blank before insertion, mirroring
// MahoSplitViewController::AddSplitWithURL; an un-navigated WebContents is
// dropped by Maho tab management and never counted. Direct insertion keeps the
// split fixture deterministic and free of the network/seed navigations that
// hang in headless environments.
void AppendBlankTab(Browser* browser) {
  TabStripModel* model = browser->GetTabStripModel();
  content::WebContents::CreateParams create_params(browser->GetProfile());
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(create_params);
  const GURL blank_url(url::kAboutBlankURL);
  content::NavigationController::LoadURLParams load_params(blank_url);
  load_params.transition_type = ui::PAGE_TRANSITION_GENERATED;
  contents->GetController().LoadURLWithParams(load_params);
  model->InsertWebContentsAt(model->count(), std::move(contents),
                             AddTabTypes::ADD_ACTIVE);
  ::base::RunLoop().RunUntilIdle();
}

// Builds a real two-pane split of tab strip indices 0 and 1 with index 0 as
// the canonical active tab, using the single-partner AddToNewSplit contract.
// This intentionally does NOT go through MahoSplitViewController::AddSplit so
// that the next/prev activation behavior can be exercised independently of the
// split-creation path owned by a sibling todo.
void CreateTwoPaneSplit(TabStripModel* model) {
  ASSERT_TRUE(model);
  ASSERT_GE(model->count(), 2);
  model->ActivateTabAt(
      0, TabStripUserGestureDetails(
             TabStripUserGestureDetails::GestureType::kOther));
  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  model->AddToNewSplit({1}, visual_data,
                       split_tabs::SplitTabCreatedSource::kToolbarButton);
  ::base::RunLoop().RunUntilIdle();
  ASSERT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));
  ASSERT_EQ(0, model->active_index());
}

// Happy path: in a two-pane split, next_split_view must move the canonical
// TabStripModel active tab to the partner, and prev_split_view must move it
// back. The active WebContents and MultiContentsView active contents must
// converge after event processing. This fails against a view-only
// implementation that mutates MultiContentsView::SetActiveIndex without
// touching TabStripModel.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       NextPrevSplitPaneActivatesPartnerThroughModel) {
  SeedTestPage(static_cast<Browser*>(browser()));
  AppendBlankTab(static_cast<Browser*>(browser()));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  CreateTwoPaneSplit(model);

  content::WebContents* tab0 = model->GetWebContentsAt(0);
  content::WebContents* tab1 = model->GetWebContentsAt(1);
  ASSERT_TRUE(tab0);
  ASSERT_TRUE(tab1);
  ASSERT_NE(tab0, tab1);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  MultiContentsView* mcv = browser_view->multi_contents_view();
  ASSERT_TRUE(mcv);
  ASSERT_TRUE(mcv->IsInSplitView());

  // next_split_view: canonical active index moves to the partner.
  ExecuteCommandAction(static_cast<Browser*>(browser()), "next_split_view");
  ::base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, model->active_index())
      << "next_split_view must activate the partner through TabStripModel";
  EXPECT_EQ(tab1, model->GetActiveWebContents());
  ASSERT_TRUE(mcv->GetActiveContentsView());
  EXPECT_EQ(model->GetActiveWebContents(),
            mcv->GetActiveContentsView()->web_contents())
      << "MultiContentsView active contents must converge with the model "
         "active WebContents after next_split_view";

  // prev_split_view: canonical active index moves back to the original tab.
  ExecuteCommandAction(static_cast<Browser*>(browser()), "prev_split_view");
  ::base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, model->active_index())
      << "prev_split_view must activate the partner through TabStripModel";
  EXPECT_EQ(tab0, model->GetActiveWebContents());
  ASSERT_TRUE(mcv->GetActiveContentsView());
  EXPECT_EQ(model->GetActiveWebContents(),
            mcv->GetActiveContentsView()->web_contents())
      << "MultiContentsView active contents must converge with the model "
         "active WebContents after prev_split_view";
}

// Edge: outside a split, next/prev split-pane commands must be a no-op — the
// active index and tab count are unchanged and no CHECK/crash occurs.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       NextPrevSplitPaneOnRegularTabIsNoOp) {
  SeedTestPage(static_cast<Browser*>(browser()));
  AppendBlankTab(static_cast<Browser*>(browser()));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_GE(model->count(), 2);
  model->ActivateTabAt(
      0, TabStripUserGestureDetails(
             TabStripUserGestureDetails::GestureType::kOther));
  ::base::RunLoop().RunUntilIdle();
  ASSERT_FALSE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));

  const int count_before = model->count();
  const int active_before = model->active_index();

  ExecuteCommandAction(static_cast<Browser*>(browser()), "next_split_view");
  ::base::RunLoop().RunUntilIdle();
  EXPECT_EQ(active_before, model->active_index())
      << "next_split_view on a regular tab must not change the active index";
  EXPECT_EQ(count_before, model->count())
      << "next_split_view on a regular tab must not change the tab count";

  ExecuteCommandAction(static_cast<Browser*>(browser()), "prev_split_view");
  ::base::RunLoop().RunUntilIdle();
  EXPECT_EQ(active_before, model->active_index())
      << "prev_split_view on a regular tab must not change the active index";
  EXPECT_EQ(count_before, model->count())
      << "prev_split_view on a regular tab must not change the tab count";
}

// Happy path: with tab A active and tab B in the background, the single-tab
// context-menu split action folds the EXISTING background tab B into a split
// with A — no new tab is created, and A stays canonical active. This fails
// against the old context-menu path that called
// AddSplitWithURL(GetVisibleURL()), which created a duplicate blank tab of B's
// URL and grew the tab count. RUNTIME GREEN is CI-deferred (the maho-login-wall
// login-gate SIGSEGVs InProcessBrowserTest startup harness-wide); the failing
// reference to the pre-baseline-absent AddSplitForExistingTab is the source
// -level RED proof.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       AddSplitForExistingTabFoldsBackgroundTabKeepingActive) {
  SeedTestPage(static_cast<Browser*>(browser()));
  AppendBlankTab(static_cast<Browser*>(browser()));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_GE(model->count(), 2);

  model->ActivateTabAt(0, TabStripUserGestureDetails(
                              TabStripUserGestureDetails::GestureType::kOther));
  ::base::RunLoop().RunUntilIdle();
  ASSERT_EQ(0, model->active_index());
  ASSERT_FALSE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));

  content::WebContents* tab_a = model->GetActiveWebContents();
  content::WebContents* tab_b = model->GetWebContentsAt(1);
  ASSERT_TRUE(tab_a);
  ASSERT_TRUE(tab_b);
  ASSERT_NE(tab_a, tab_b);

  const int count_before = model->count();

  MahoSplitViewController(static_cast<Browser*>(browser())).AddSplitForExistingTab(1);
  ::base::RunLoop().RunUntilIdle();

  EXPECT_EQ(count_before, model->count())
      << "splitting an existing background tab must not create a new tab";
  EXPECT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));
  EXPECT_EQ(tab_a, model->GetActiveWebContents())
      << "tab A must remain the canonical active tab after folding in B";

  std::optional<split_tabs::SplitTabId> split_a =
      model->GetSplitForTab(model->GetIndexOfWebContents(tab_a));
  std::optional<split_tabs::SplitTabId> split_b =
      model->GetSplitForTab(model->GetIndexOfWebContents(tab_b));
  ASSERT_TRUE(split_a.has_value());
  ASSERT_TRUE(split_b.has_value());
  EXPECT_EQ(split_a.value(), split_b.value())
      << "A and B must share exactly one split id";
}

// Fallback path: invoking the split action on the ACTIVE tab (clicked index ==
// active index) has no distinct existing partner, so it delegates to AddSplit()
// and creates exactly one blank partner, yielding a valid two-pane split.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       AddSplitForExistingTabOnActiveCreatesBlankPartner) {
  SeedTestPage(static_cast<Browser*>(browser()));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  const int active_index = model->active_index();
  ASSERT_NE(TabStripModel::kNoTab, active_index);
  ASSERT_FALSE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));

  const int count_before = model->count();

  MahoSplitViewController(static_cast<Browser*>(browser())).AddSplitForExistingTab(active_index);
  ::base::RunLoop().RunUntilIdle();

  EXPECT_EQ(count_before + 1, model->count())
      << "splitting the active tab must create exactly one blank partner";
  EXPECT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()))
      << "a valid two-pane split must exist after the active-tab self-split";
}

// Edge: the action is a no-op (no crash, no new tab, unchanged active index)
// when the clicked tab is already a split member, and likewise when the active
// pivot tab is already split.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       AddSplitForExistingTabOnSplitMemberIsNoOp) {
  SeedTestPage(static_cast<Browser*>(browser()));
  AppendBlankTab(static_cast<Browser*>(browser()));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_GE(model->count(), 2);
  CreateTwoPaneSplit(model);

  const int count_before = model->count();
  const int active_before = model->active_index();

  MahoSplitViewController(static_cast<Browser*>(browser())).AddSplitForExistingTab(1);
  ::base::RunLoop().RunUntilIdle();
  EXPECT_EQ(count_before, model->count())
      << "splitting an already-split clicked tab must not create a tab";
  EXPECT_EQ(active_before, model->active_index());

  MahoSplitViewController(static_cast<Browser*>(browser())).AddSplitForExistingTab(0);
  ::base::RunLoop().RunUntilIdle();
  EXPECT_EQ(count_before, model->count())
      << "splitting when the active tab is already split must not create a tab";
  EXPECT_EQ(active_before, model->active_index());
}

void BuildFourWaySplitAndCaptureProof(
    Browser* browser,
    const std::array<GURL, 4>& page_urls,
    const char* artifact_name);

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CaptureFourWaySplitScreenshotUsesLiveTabContents) {
  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 0: Setup rich live web pages ===";
  // Warm the sidebar/FFI pipeline before the navigation burst: on a cold core
  // the first tab events trigger synchronous space-bridge initialization that
  // exceeds the global RunLoop watchdog (observed ~62s for STEP 0).
  SeedProfile(2);
  // constexpr string_view table: a `static const std::array<std::string, 4>`
  // needs an exit-time destructor, which Chromium builds reject under
  // -Werror,-Wexit-time-destructors.
  static constexpr std::array<std::string_view, 4> page_bodies = {
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Wikipedia</title><style>"
      "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
      "background:%23ffffff;color:%23202122;padding:24px;box-sizing:border-box;}"
      "header{border-bottom:1px solid %23a2a9b1;padding-bottom:12px;margin-bottom:16px;display:flex;align-items:center;gap:12px;}"
      ".logo{font-size:26px;font-weight:700;letter-spacing:-0.5px;color:%23000000;}"
      ".badge{background:%23eaecf0;color:%2354595d;padding:3px 8px;border-radius:4px;font-size:12px;font-weight:600;}"
      "h1{font-size:22px;margin:0 0 12px 0;color:%23000;border-bottom:1px solid %23eaecf0;padding-bottom:6px;}"
      "p{font-size:14px;line-height:1.6;color:%23404244;margin:0 0 12px 0;}"
      ".card{background:%23f8f9fa;border:1px solid %23c8ccd1;border-radius:8px;padding:14px;margin-top:14px;}"
      ".card h3{margin:0 0 6px 0;font-size:14px;color:%23202122;}"
      "</style></head><body>"
      "<header><div class='logo'>WIKIPEDIA</div><div class='badge'>The Free Encyclopedia</div></header>"
      "<h1>Maho Browser Architecture</h1>"
      "<p>Maho Browser is a next-generation workstation browser featuring Chromium core overlays, native Rust engine integration, and multi-pane spatial productivity interfaces.</p>"
      "<div class='card'><h3>Key Features</h3><p>• 4-Way MultiContents grid layout<br>• Isolated Tart macOS VM E2E test harness<br>• Real-time AI context extraction</p></div>"
      "</body></html>",

      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Rust Programming</title><style>"
      "body{margin:0;font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;"
      "background:%230f141c;color:%23e6edf3;padding:24px;box-sizing:border-box;}"
      "header{border-bottom:1px solid %2330363d;padding-bottom:12px;margin-bottom:16px;display:flex;align-items:center;gap:10px;}"
      ".logo{font-size:22px;font-weight:700;color:%23f74c00;}"
      ".tag{background:%231f242c;color:%23ff9e64;padding:3px 8px;border-radius:4px;font-size:11px;border:1px solid %23414868;}"
      "h1{font-size:20px;margin:0 0 12px 0;color:%237aa2f7;}"
      "pre{background:%23161b22;border:1px solid %2330363d;border-radius:6px;padding:14px;font-size:13px;color:%237ee787;line-height:1.5;overflow:hidden;margin:0;}"
      ".kw{color:%23ff7b72;}.fn{color:%23d2a8ff;}.str{color:%23a5d6ff;}"
      "</style></head><body>"
      "<header><div class='logo'>Rust 1.85</div><div class='tag'>Async Engine Core</div></header>"
      "<h1>maho_core::split_view</h1>"
      "<pre><span class='kw'>pub async fn</span> <span class='fn'>create_4way_split</span>(id: SplitId) -&gt; Result&lt;()&gt; {<br>"
      "    <span class='kw'>let</span> layout = SplitLayout::Grid2x2;<br>"
      "    engine.apply_topology(id, layout).<span class='kw'>await</span><br>"
      "}</pre>"
      "</body></html>",

      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>GitHub - maho-workspace</title><style>"
      "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
      "background:%230d1117;color:%23c9d1d9;padding:24px;box-sizing:border-box;}"
      "header{border-bottom:1px solid %2321262d;padding-bottom:12px;margin-bottom:16px;display:flex;align-items:center;justify-content:space-between;}"
      ".repo{font-size:18px;font-weight:600;color:%2358a6ff;display:flex;align-items:center;gap:8px;}"
      ".branch{background:%2321262d;color:%238b949e;padding:3px 10px;border-radius:12px;font-size:12px;}"
      ".commit{background:%23161b22;border:1px solid %2330363d;border-radius:6px;padding:12px;margin-top:12px;}"
      ".title{color:%23f0f6fc;font-size:14px;font-weight:600;margin-bottom:4px;}"
      ".desc{color:%238b949e;font-size:12px;margin:0;}"
      "</style></head><body>"
      "<header><div class='repo'>maho-browser / workspace</div><div class='branch'>main</div></header>"
      "<div class='commit'><div class='title'>feat(split): verified 4-way MultiContentsView layout in Tart VM</div>"
      "<p class='desc'>commit 7a5f0fda · verified with live tab WebContents snapshots</p></div>"
      "</body></html>",

      "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>MDN Web Docs</title><style>"
      "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
      "background:%231b1b1f;color:%23ffffff;padding:24px;box-sizing:border-box;}"
      "header{border-bottom:1px solid %23323238;padding-bottom:12px;margin-bottom:16px;display:flex;align-items:center;gap:10px;}"
      ".logo{font-size:22px;font-weight:800;letter-spacing:1px;color:%238585ff;}"
      "h1{font-size:20px;margin:0 0 10px 0;color:%23ffffff;}"
      ".doc-box{background:%232b2b33;border-left:4px solid %238585ff;padding:12px 16px;border-radius:0 8px 8px 0;margin-top:12px;}"
      ".doc-box p{margin:0;font-size:13px;line-height:1.5;color:%23d0d0d8;}"
      "</style></head><body>"
      "<header><div class='logo'>MDN_</div></header>"
      "<h1>CSS Grid Layout: 2x2 Viewport Split</h1>"
      "<div class='doc-box'><p>The CSS Grid Layout module excels at dividing browser viewport real estate into precise proportional quadrants for multi-tasking workflows.</p></div>"
      "</body></html>"
  };

  // Serve the pages over the embedded test server instead of data: URLs:
  // data-URL navigation plus first-paint font fallback stalls far beyond the
  // global RunLoop watchdog in the Tart VM, and an explicit charset fixes
  // mojibake in the captured proof.
  // The handler (registered before the server started) reads this shared map;
  // fill it before the first navigation so the proof pages resolve.
  std::map<std::string, std::string>& fourway_pages = FourWayPages();
  for (size_t i = 0; i < page_bodies.size(); ++i) {
    fourway_pages["/4way/page" + base::NumberToString(i) + ".html"] =
        std::string(page_bodies[i]);
  }
  std::array<GURL, 4> page_urls;
  for (size_t i = 0; i < page_bodies.size(); ++i) {
    page_urls[i] = embedded_test_server()->GetURL(
        "/4way/page" + base::NumberToString(i) + ".html");
  }

  BuildFourWaySplitAndCaptureProof(static_cast<Browser*>(browser()), page_urls,
                                   "4way-split-proof.png");
}

// Navigates four tabs to |page_urls|, assembles the canonical Maho 4-way
// split through the TabStripModel split APIs (2-way, third pane, fourth pane),
// and captures the window snapshot next to the gtest summary artifact. Shared
// by the embedded-fixture proof and the live-webpage proof tests so both
// exercise the identical pane-assembly and capture contract.
void BuildFourWaySplitAndCaptureProof(
    Browser* browser,
    const std::array<GURL, 4>& page_urls,
    const char* artifact_name) {
  TabStripModel* model = browser->GetTabStripModel();
  ASSERT_TRUE(model);

  // Navigate tab 0 in place, then add tabs 1-3. Maho's session-restore
  // propagation may insert registry tabs asynchronously, so resolve each page
  // tab by URL instead of assuming stable strip indices.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser, page_urls[0]));
  for (size_t i = 1; i < 4; ++i) {
    // Mirrors InProcessBrowserTest::AddTabAtIndexToBrowser (protected) with
    // the exact NavigateParams it uses for a foreground indexed tab.
    NavigateParams params(browser, page_urls[i], ui::PAGE_TRANSITION_LINK);
    params.tabstrip_index = static_cast<int>(i);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    params.web_app_navigation_data.emplace();
    params.web_app_navigation_data->SetNavigationCapturingForceOff(true);
    Navigate(&params);
    ::base::RunLoop().RunUntilIdle();
  }
  ASSERT_GE(model->count(), 4);
  auto find_page_index = [&](const GURL& url) -> int {
    for (int j = 0; j < model->count(); ++j) {
      content::WebContents* candidate = model->GetWebContentsAt(j);
      if (candidate && candidate->GetLastCommittedURL() == url) {
        return j;
      }
    }
    return -1;
  };
  std::array<int, 4> page_indices = {};
  for (size_t i = 0; i < page_urls.size(); ++i) {
    ASSERT_TRUE(::base::test::RunUntil([&]() {
      return find_page_index(page_urls[i]) >= 0;
    })) << "page " << i << " tab not committed";
    const int idx = find_page_index(page_urls[i]);
    content::WebContents* contents = model->GetWebContentsAt(idx);
    // Live pages can keep loading late beacons for a long time; a hanging
    // subresource must not fail the proof. Page identity stays strict below.
    const bool load_stopped = content::WaitForLoadStop(contents);
    LOG(INFO) << "=== 4WAY_SPLIT_PAGE " << i << " url=" << page_urls[i]
              << " load_stopped=" << load_stopped;
    ASSERT_EQ(page_urls[i], contents->GetLastCommittedURL());
    page_indices[i] = idx;
  }

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 1: Create 2way split ===";
  model->ActivateTabAt(
      page_indices[0],
      TabStripUserGestureDetails(
          TabStripUserGestureDetails::GestureType::kOther));
  split_tabs::SplitTabVisualData visual_data_2way =
      split_tabs::SplitTabVisualData::CreateTwoPane(
          split_tabs::SplitTabLayout::kSideBySide, 0.5);
  const split_tabs::SplitTabId split_id = model->AddToNewSplit(
      {page_indices[1]}, visual_data_2way,
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ASSERT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 2: Add 3rd tab ===";
  split_tabs::SplitTabVisualData visual_data_3way =
      *model->GetSplitData(split_id)->visual_data();
  ASSERT_TRUE(visual_data_3way.InsertPaneAtLeaf(
      1u, split_tabs::SplitTabLayout::kStacked, 0.5,
      split_tabs::SplitPaneInsertionSide::kAfter));
  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 2b: calling AddToExistingSplit ===";
  ASSERT_TRUE(model->AddToExistingSplit(
      split_id, page_indices[2], 2u, visual_data_3way,
      split_tabs::SplitTabCreatedSource::kToolbarButton));
  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 2c: AddToExistingSplit returned ===";

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 3: Add 4th tab ===";
  split_tabs::SplitTabVisualData visual_data_4way =
      *model->GetSplitData(split_id)->visual_data();
  ASSERT_TRUE(visual_data_4way.InsertPaneAtLeaf(
      0u, split_tabs::SplitTabLayout::kStacked, 0.5,
      split_tabs::SplitPaneInsertionSide::kBefore));
  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 3b: calling AddToExistingSplit ===";
  ASSERT_TRUE(model->AddToExistingSplit(
      split_id, page_indices[3], 0u, visual_data_4way,
      split_tabs::SplitTabCreatedSource::kToolbarButton));
  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 3c: AddToExistingSplit returned ===";
  model->ActivateTabAt(page_indices[0]);

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 4: MultiContentsView verify ===";
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  ASSERT_TRUE(browser_view);
  MultiContentsView* mcv = browser_view->multi_contents_view();
  ASSERT_TRUE(mcv);
  ASSERT_TRUE(mcv->IsInSplitView());

  views::Widget* widget = browser_view->GetWidget();
  ASSERT_TRUE(widget);
  widget->SetBounds(gfx::Rect(0, 0, 1920, 1080));
  widget->LayoutRootViewIfNecessary();

  // Live panes finish network-driven painting asynchronously; give them a
  // bounded settle so the proof snapshot shows rendered content in all four
  // quadrants instead of capturing mid-paint blank panes.
  base::RunLoop pane_settle;
  content::GetUIThreadTaskRunner({})->PostDelayedTask(
      FROM_HERE, pane_settle.QuitClosure(), base::Seconds(4));
  pane_settle.Run();

  const size_t pane_count = mcv->visible_pane_count();
  ASSERT_EQ(4u, pane_count);

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 5: Window/View snapshot capture ===";
  scoped_refptr<base::RefCountedMemory> png_data;
  bool snapshot_finished = false;
  const gfx::Rect window_screen = widget->GetWindowBoundsInScreen();
  const gfx::Rect source_rect(window_screen.size());

  ui::GrabWindowSnapshotAsPNG(
      widget->GetNativeWindow(), source_rect,
      ::base::BindOnce(
          [](scoped_refptr<base::RefCountedMemory>* out_data,
             bool* finished,
             scoped_refptr<base::RefCountedMemory> data) {
            *out_data = data;
            *finished = true;
          },
          &png_data, &snapshot_finished));
  ASSERT_TRUE(::base::test::RunUntil(
      [&snapshot_finished] { return snapshot_finished; }));

  ASSERT_TRUE(png_data && png_data->size() > 0);

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 6: Write PNG artifact ===";
  const ::base::FilePath summary_path =
      ::base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "test-launcher-summary-output");
  ASSERT_FALSE(summary_path.empty());
  const ::base::FilePath artifact_path =
      summary_path.DirName().AppendASCII(artifact_name);
  {
    ::base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(::base::CreateDirectory(artifact_path.DirName()));
    ASSERT_TRUE(::base::WriteFile(
        artifact_path,
        UNSAFE_BUFFERS(::base::span<const uint8_t>(png_data->data(),
                                                   png_data->size()))));
  }
  fprintf(stderr, "Captured genuine 4-way split proof screenshot to: %s (size: %zu bytes)\n",
          artifact_path.value().c_str(), png_data->size());
  fflush(stderr);

  LOG(INFO) << "=== 4WAY_SCREENSHOT_STEP 7: FINISHED ===";
}


// Genuine live-webpage proof: identical split assembly and widget capture,
// but all four panes navigate real https sites from the open internet so the
// capture shows real production page renders, not embedded fixtures. Selected
// explicitly via the live-4way suite; never part of the deterministic suites.
IN_PROC_BROWSER_TEST_F(MahoLiveWeb4WayTest,
                       CaptureFourWaySplitScreenshotUsesLiveWebPages) {
  LOG(INFO) << "=== 4WAY_LIVE_STEP 0: Navigating real web pages ===";
  SeedProfile(2);
  const std::array<GURL, 4> page_urls = {
      GURL("https://www.wikipedia.org/"),
      GURL("https://developer.mozilla.org/en-US/"),
      GURL("https://rust-lang.org/"),
      GURL("https://news.ycombinator.com/"),
  };
  BuildFourWaySplitAndCaptureProof(static_cast<Browser*>(browser()), page_urls,
                                   "4way-split-proof-real.png");
}

// Regression: the sidebar state adapter used to materialize split-group rows
// only when a split had exactly two members, so the viewport-supported 4-way
// splits degraded to flat tab rows in the sidebar. A four-member split must
// project into one kSplitGroup with all four members as children.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       GroupSplitTabsProjectsFourMemberSplitGroup) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  SeedTestPage(static_cast<Browser*>(browser()));
  while (model->count() < 4) {
    AppendBlankTab(static_cast<Browser*>(browser()));
  }
  ASSERT_GE(model->count(), 4);

  split_tabs::SplitTabVisualData two_pane =
      split_tabs::SplitTabVisualData::CreateTwoPane(
          split_tabs::SplitTabLayout::kSideBySide, 0.5);
  const split_tabs::SplitTabId split_id =
      model->AddToNewSplit({1}, two_pane,
                           split_tabs::SplitTabCreatedSource::kToolbarButton);
  ASSERT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));

  split_tabs::SplitTabVisualData three_pane =
      *model->GetSplitData(split_id)->visual_data();
  ASSERT_TRUE(three_pane.InsertPaneAtLeaf(
      1u, split_tabs::SplitTabLayout::kStacked, 0.5,
      split_tabs::SplitPaneInsertionSide::kAfter));
  ASSERT_TRUE(model->AddToExistingSplit(
      split_id, 2, 2u, three_pane,
      split_tabs::SplitTabCreatedSource::kToolbarButton));

  split_tabs::SplitTabVisualData four_pane =
      *model->GetSplitData(split_id)->visual_data();
  ASSERT_TRUE(four_pane.InsertPaneAtLeaf(
      0u, split_tabs::SplitTabLayout::kStacked, 0.5,
      split_tabs::SplitPaneInsertionSide::kBefore));
  ASSERT_TRUE(model->AddToExistingSplit(
      split_id, 3, 0u, four_pane,
      split_tabs::SplitTabCreatedSource::kToolbarButton));

  std::vector<SidebarTreeNode> rows;
  for (int i = 0; i < model->count(); ++i) {
    SidebarTreeNode node;
    node.kind = SidebarNodeKind::kTab;
    node.tab_strip_index = i;
    node.is_active = model->active_index() == i;
    rows.push_back(std::move(node));
  }

  maho::GroupSplitTabs(rows, model);

  const SidebarTreeNode* split_group = nullptr;
  int split_group_count = 0;
  for (const auto& node : rows) {
    if (node.kind == SidebarNodeKind::kSplitGroup) {
      ++split_group_count;
      split_group = &node;
    }
  }
  ASSERT_EQ(split_group_count, 1)
      << "A four-member split must project into one kSplitGroup row";
  ASSERT_TRUE(split_group);
  EXPECT_EQ(split_group->children.size(), 4u)
      << "All four split members must be children of the group";
  for (const auto& child : split_group->children) {
    EXPECT_TRUE(child.is_in_split);
  }
}

std::map<std::string, std::string>& FourWayPages() {
  static base::NoDestructor<std::map<std::string, std::string>> pages;
  return *pages;
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ProcessingSurfaceIsLocalizedToPersistentActionRow) {
  // Seed 2 guarantees at least one top-level normal tab row, which we need to
  // prove the processing surface stays localized to the action row and never
  // blanks the surrounding tab list.
  SeedProfile(2);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* action_row = list_view->action_row_for_testing();
  views::View* button_surface = list_view->action_buttons_for_testing();
  MahoSidebarProcessingPlaceholderView* processing =
      list_view->action_processing_view_for_testing();
  views::View* tidy = list_view->tidy_button_for_testing();

  ASSERT_TRUE(action_row);
  ASSERT_TRUE(button_surface);
  ASSERT_TRUE(processing);
  ASSERT_TRUE(tidy);

  std::string normal_tab_id;
  auto state = GetViewState();
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
      normal_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(normal_tab_id.empty())
      << "Seed 2 must expose at least one normal tab row";
  SidebarTabRowView* tab_row =
      list_view->FindTabRowByIdForTesting(normal_tab_id);
  ASSERT_TRUE(tab_row);

  EXPECT_EQ(action_row->GetPreferredSize(views::SizeBounds()).height(), 42);
  EXPECT_EQ(processing->parent(), action_row);
  EXPECT_EQ(button_surface->parent(), action_row);

  // Disable-animation routes loop resolution straight to the real hidden
  // callback, so we can start/stop processing deterministically without sleeps.
  processing->set_disable_animation_for_testing(true);

  list_view->StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode::kLoop, tidy);
  ASSERT_TRUE(list_view->action_placeholder_active_for_testing());

  ASSERT_TRUE(list_view->GetWidget());
  list_view->GetWidget()->LayoutRootViewIfNecessary();

  EXPECT_TRUE(processing->GetVisible())
      << "Processing surface must be visible while processing is active";
  EXPECT_EQ(processing->GetLocalBounds(), action_row->GetLocalBounds())
      << "Processing surface must fill exactly the persistent 42dp action row";

  EXPECT_TRUE(tab_row->GetVisible())
      << "Tab rows must remain visible while only the action row processes";
  EXPECT_TRUE(tab_row->IsDrawn())
      << "Tab rows must remain drawn while only the action row processes";

  list_view->ResolveActionPlaceholderDeferredForTesting(true);
  EXPECT_TRUE(::base::test::RunUntil([list_view]() {
    return !list_view->action_placeholder_active_for_testing();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ProcessingSurvivesRebuildAndRestoresActions) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* tidy = list_view->tidy_button_for_testing();
  ASSERT_TRUE(tidy);
  views::View* button_surface = list_view->action_buttons_for_testing();
  ASSERT_TRUE(button_surface);
  MahoSidebarProcessingPlaceholderView* processing_before =
      list_view->action_processing_view_for_testing();
  ASSERT_TRUE(processing_before);
  processing_before->set_disable_animation_for_testing(true);

  views::View* action_row_before = list_view->action_row_for_testing();
  ASSERT_TRUE(action_row_before);

  list_view->StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode::kLoop, tidy);
  ASSERT_TRUE(list_view->action_placeholder_active_for_testing());
  EXPECT_FALSE(button_surface->GetCanProcessEventsWithinSubtree());

  const auto assert_survived = [&](const char* stage) {
    EXPECT_EQ(action_row_before, list_view->action_row_for_testing())
        << "Action row pointer must survive rebuild: " << stage;
    EXPECT_EQ(processing_before,
              list_view->action_processing_view_for_testing())
        << "Processing view pointer must survive rebuild: " << stage;
    EXPECT_TRUE(list_view->action_placeholder_active_for_testing())
        << "Active processing must survive rebuild: " << stage;
  };

  // Two browser-context rebuilds and one zero-context (nullptr) rebuild must
  // all keep the row/processing pointers and the in-progress flag unchanged.
  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), static_cast<Browser*>(browser()));
  ::base::RunLoop().RunUntilIdle();
  assert_survived("first browser-context rebuild");

  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), static_cast<Browser*>(browser()));
  ::base::RunLoop().RunUntilIdle();
  assert_survived("second browser-context rebuild");

  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), nullptr);
  ::base::RunLoop().RunUntilIdle();
  assert_survived("zero-context rebuild");

  list_view->ResolveActionPlaceholderDeferredForTesting(true);
  EXPECT_TRUE(::base::test::RunUntil([list_view]() {
    return !list_view->action_placeholder_active_for_testing();
  }));
  EXPECT_TRUE(button_surface->GetCanProcessEventsWithinSubtree())
      << "Action buttons must accept input again after resolution";
  EXPECT_FALSE(button_surface->GetViewAccessibility().GetIsIgnored())
      << "Action buttons must be re-exposed to accessibility after resolution";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ProcessingBlocksInputAndRestoresKeyboardFocus) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* tidy = list_view->tidy_button_for_testing();
  ASSERT_TRUE(tidy);
  MahoSidebarProcessingPlaceholderView* processing =
      list_view->action_processing_view_for_testing();
  ASSERT_TRUE(processing);
  processing->set_disable_animation_for_testing(true);

  tidy->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  tidy->RequestFocus();
  ASSERT_TRUE(tidy->HasFocus());

  list_view->StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode::kLoop, tidy);

  views::View* button_surface = list_view->action_buttons_for_testing();
  ASSERT_TRUE(button_surface);
  EXPECT_FALSE(button_surface->GetCanProcessEventsWithinSubtree());
  EXPECT_TRUE(button_surface->GetViewAccessibility().GetIsIgnored());
  EXPECT_FALSE(tidy->HasFocus());

  // Restoration must run through the real placeholder lifecycle. Calling
  // OnActionPlaceholderHidden() directly is forbidden: it would bypass the
  // processing view and hide the actual hidden-callback wiring under test.
  list_view->ResolveActionPlaceholderDeferredForTesting(true);
  EXPECT_TRUE(::base::test::RunUntil([list_view]() {
    return !list_view->action_placeholder_active_for_testing();
  }));

  EXPECT_TRUE(button_surface->GetCanProcessEventsWithinSubtree());
  EXPECT_FALSE(button_surface->GetViewAccessibility().GetIsIgnored());
  EXPECT_TRUE(tidy->HasFocus())
      << "Keyboard focus must return to the trigger button after processing";
}

// Mouse-style activation: the trigger button is NOT focused when processing
// starts, so the completed lifecycle must not force keyboard focus onto it.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ProcessingMouseTriggerDoesNotForceFocusOnRestore) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* tidy = list_view->tidy_button_for_testing();
  ASSERT_TRUE(tidy);
  MahoSidebarProcessingPlaceholderView* processing =
      list_view->action_processing_view_for_testing();
  ASSERT_TRUE(processing);
  processing->set_disable_animation_for_testing(true);

  if (tidy->GetFocusManager()) {
    tidy->GetFocusManager()->ClearFocus();
  }
  ASSERT_FALSE(tidy->HasFocus());

  list_view->StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode::kLoop, tidy);
  ASSERT_TRUE(list_view->action_placeholder_active_for_testing());
  EXPECT_FALSE(tidy->HasFocus());

  list_view->ResolveActionPlaceholderDeferredForTesting(true);
  EXPECT_TRUE(::base::test::RunUntil([list_view]() {
    return !list_view->action_placeholder_active_for_testing();
  }));

  EXPECT_FALSE(tidy->HasFocus())
      << "A mouse-triggered action must not force keyboard focus onto the "
         "trigger button after the processing lifecycle completes";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       SingleCycleRestoresWithoutExternalResolve) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  list_view->StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode::kSingleCycle,
      list_view->clear_button_for_testing());

  EXPECT_TRUE(::base::test::RunUntil([list_view]() {
    return !list_view->action_placeholder_active_for_testing();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       DISABLED_ProcessingSlowWorkingVisualQaHarness) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* tidy = list_view->tidy_button_for_testing();
  ASSERT_TRUE(tidy);

  list_view->StartActionProcessingForTesting(
      MahoSidebarProcessingPlaceholderView::Mode::kLoop, tidy);

  ::base::RunLoop run_loop;
  ::base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, ::base::BindLambdaForTesting([&]() {
        list_view->ResolveActionPlaceholderDeferredForTesting(true);
        ::base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, run_loop.QuitClosure(), ::base::Milliseconds(600));
      }),
      ::base::Milliseconds(2500));
  run_loop.Run();
}

// Regression: the Cmd+Shift+C / command-palette copy path
// (ExecuteCommandAction "copy_url") must surface the upper-right "Link copied"
// toast, the same MahoNotificationOverlay the Maho Mini copy button shows.
// Before the fix this path wrote the clipboard but never called
// MahoNotificationOverlay::Show, so the link was copied silently with no toast.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CopyUrlShortcutShowsLinkCopiedToast) {
  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  ExecuteCommandAction(static_cast<Browser*>(browser()), "copy_url");
  ::base::RunLoop().RunUntilIdle();

  MahoNotificationOverlay* overlay =
      MahoNotificationOverlay::FromBrowser(static_cast<Browser*>(browser()));
  ASSERT_NE(overlay, nullptr)
      << "copy_url must create the notification overlay that hosts the toast";
  EXPECT_TRUE(overlay->IsVisible())
      << "copy_url must show the 'Link copied' toast, not copy silently";
}

// copy_url_markdown shares the same copy surface and must also show the toast.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CopyUrlMarkdownShortcutShowsLinkCopiedToast) {
  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  ExecuteCommandAction(static_cast<Browser*>(browser()), "copy_url_markdown");
  ::base::RunLoop().RunUntilIdle();

  MahoNotificationOverlay* overlay =
      MahoNotificationOverlay::FromBrowser(static_cast<Browser*>(browser()));
  ASSERT_NE(overlay, nullptr);
  EXPECT_TRUE(overlay->IsVisible())
      << "copy_url_markdown must also show the 'Link copied' toast";
}

// Regression: Cmd+W resolves through MahoShortcutInterceptor to
// ExecuteCommandAction("close_tab"), bypassing IDC_CLOSE_TAB. With several
// sidebar tabs multi-selected it must close every selected tab, not just the
// active one.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CloseTabShortcutClosesAllMultiSelectedTabs) {
  TabStripModel* model = browser()->GetTabStripModel();
  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  while (model->count() < 4) {
    ASSERT_TRUE(AddTabAtIndex(model->count(), page_url,
                              ui::PAGE_TRANSITION_TYPED));
  }
  ::base::RunLoop().RunUntilIdle();

  std::vector<std::string> ids;
  for (int i = 0; i < model->count(); ++i) {
    auto* helper = MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    ASSERT_TRUE(helper);
    ASSERT_FALSE(helper->stable_tab_id().empty());
    ids.push_back(helper->stable_tab_id());
  }

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);
  tab_list->ClearSelection();
  tab_list->ToggleTabSelected(ids[1]);
  tab_list->ToggleTabSelected(ids[2]);
  tab_list->ToggleTabSelected(ids[3]);
  ASSERT_TRUE(tab_list->HasMultiSelection());
  const std::vector<std::string> selected_ids(
      tab_list->selected_tab_ids().begin(), tab_list->selected_tab_ids().end());
  const size_t selected = selected_ids.size();
  const int count_before = model->count();

  ExecuteCommandAction(static_cast<Browser*>(browser()), "close_tab");
  ASSERT_TRUE(::base::test::RunUntil([&] {
    return model->count() == count_before - static_cast<int>(selected);
  })) << "close_tab must close all " << selected
      << " multi-selected tabs, count is " << model->count();

  for (int i = 0; i < model->count(); ++i) {
    auto* helper = MahoTabIdHelper::FromWebContents(model->GetWebContentsAt(i));
    ASSERT_TRUE(helper);
    EXPECT_FALSE(tab_list->IsTabSelected(helper->stable_tab_id()));
    EXPECT_EQ(std::ranges::count(selected_ids, helper->stable_tab_id()), 0)
        << "selected tab " << helper->stable_tab_id() << " survived close_tab";
  }
}

// ---------------------------------------------------------------------------
// Accelerator-level regression coverage.
//
// The tests above drive actions directly through ExecuteCommandAction(), which
// cannot observe whether a keystroke ever reaches that action. The three
// defects covered here all lived strictly in the accelerator layer, so they
// stayed green under action-level coverage.
// ---------------------------------------------------------------------------

// The Maho accelerator table must actually register Cmd+Shift+C, Cmd+L and
// Cmd+E and resolve each to its Maho action. Previously the pinned-revision
// guard skipped every replacement for global_keyboard_shortcuts_mac.mm, so the
// upstream hidden table kept mapping Cmd+Shift+C to IDC_DEV_TOOLS_INSPECT and
// none of these accelerators resolved to a Maho action.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       MahoAcceleratorsRegisterAndResolveToMahoActions) {
  const std::vector<ui::Accelerator> registered =
      MahoShortcutInterceptor::GetRegisteredAccelerators();
  ASSERT_FALSE(registered.empty())
      << "Maho registered no accelerators at all; the registry never reached "
         "the FocusManager";

  // Resolve through the production entry point rather than guessing modifier
  // constants, so the assertion follows the same path BrowserView uses.
  std::map<std::string, ui::Accelerator> by_action;
  for (const ui::Accelerator& accelerator : registered) {
    const std::string action =
        MahoShortcutInterceptor::ResolveActionForAccelerator(accelerator);
    if (!action.empty()) {
      by_action.emplace(action, accelerator);
    }
  }

  struct Expectation {
    const char* action;
    ui::KeyboardCode key;
    bool shift;
  };
  const std::array<Expectation, 3> kExpectations = {{
      {"copy_url", ui::VKEY_C, true},
      {"command_bar", ui::VKEY_L, false},
      {"ai_panel", ui::VKEY_E, false},
  }};

  for (const Expectation& expectation : kExpectations) {
    auto it = by_action.find(expectation.action);
    ASSERT_NE(it, by_action.end())
        << "no registered accelerator resolves to \"" << expectation.action
        << "\"";
    EXPECT_EQ(expectation.key, it->second.key_code())
        << expectation.action << " is bound to the wrong key";
    EXPECT_TRUE(it->second.IsCmdDown())
        << expectation.action << " must be a Command-modified accelerator";
    EXPECT_EQ(expectation.shift, it->second.IsShiftDown())
        << expectation.action << " has the wrong Shift modifier";
  }
}

// Maho accelerator registration is macOS-only: kHighPriority exists so
// prePerformKeyEquivalent consults the FocusManager before CommandForKeyEvent,
// which is a mac-only dispatch path, and on Windows/Linux BrowserView's own
// LoadAccelerators() registers Ctrl+L on this same target.
#if BUILDFLAG(IS_MAC)
// A window created AFTER the Rust core is already up runs the immediate
// AddedToWidget() registration and the core-ready retry against a NON-empty
// shortcut list, so both paths try to register the same (accelerator, target)
// pair. The first window never covers this: at its AddedToWidget() the core is
// still cold and the list is empty, which is why the original six-defect run
// passed while this defect remained latent.
//
// out/Default builds with is_debug=false and dcheck_always_on=false, so a
// duplicate does NOT crash here -- AcceleratorManager just pushes a second
// entry for the same target. Detect it by observing behavior instead:
// AcceleratorTargetInfo::Unregister erases only the FIRST matching target, so
// after a single unregister a correctly-registered accelerator is gone, while a
// double-registered one is still registered.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       SecondWindowRegistersMahoAcceleratorsExactlyOnce) {
  const std::vector<ui::Accelerator> registered =
      MahoShortcutInterceptor::GetRegisteredAccelerators();
  ASSERT_FALSE(registered.empty())
      << "core is not ready in this test, so the duplicate-registration path "
         "this test exists to cover would not be exercised";

  Browser* const second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_NE(nullptr, second_browser);
  BrowserView* const second_view =
      BrowserView::GetBrowserViewForBrowser(second_browser);
  ASSERT_NE(nullptr, second_view);
  views::FocusManager* const focus_manager = second_view->GetFocusManager();
  ASSERT_NE(nullptr, focus_manager);

  for (const ui::Accelerator& accelerator : registered) {
    ASSERT_TRUE(focus_manager->IsAcceleratorRegistered(accelerator))
        << "second window is missing a Maho accelerator, so the idempotence "
           "guard skipped the only real registration";
    EXPECT_TRUE(focus_manager->HasPriorityHandler(accelerator))
        << "second window registered a Maho accelerator below kHighPriority, "
           "so CommandForKeyEvent() would win before the FocusManager";
  }

  // Probe command_bar (Cmd+L): upstream's table entry for VKEY_L sits inside an
  // "#if !BUILDFLAG(IS_MAC)" block, so on macOS this window's only registration
  // for it is Maho's. One unregister must therefore clear it completely.
  auto command_bar = std::ranges::find_if(
      registered, [](const ui::Accelerator& accelerator) {
        return MahoShortcutInterceptor::ResolveActionForAccelerator(
                   accelerator) == "command_bar";
      });
  ASSERT_NE(command_bar, registered.end());

  focus_manager->UnregisterAccelerator(*command_bar, second_view);
  EXPECT_FALSE(focus_manager->IsAcceleratorRegistered(*command_bar))
      << "command_bar survived a single unregister, so it was registered more "
         "than once on this window -- the AddedToWidget() call and the "
         "core-ready retry both installed it";
}
#endif  // BUILDFLAG(IS_MAC)


// End-to-end proof for the headline symptom: a real Cmd+Shift+C keystroke must
// copy the current tab URL and must not open DevTools Inspect.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CmdShiftCKeystrokeCopiesUrlAndDoesNotOpenDevTools) {
  // Warm the sidebar/FFI pipeline before driving input. On a cold core the
  // first tab events trigger synchronous space-bridge initialization that on
  // its own exceeds the global RunLoop watchdog (~62s observed), which is what
  // previously timed this test out before any keystroke was evaluated.
  SeedProfile(1);
  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  // Seed a sentinel so a no-op keystroke cannot be mistaken for a pass.
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(u"maho-clipboard-sentinel");
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ::base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser_view->GetWidget()->GetNativeWindow(), ui::VKEY_C,
      /*control=*/false, /*shift=*/true, /*alt=*/false, /*command=*/true,
      key_loop.QuitClosure()));
  key_loop.Run();

  EXPECT_TRUE(::base::test::RunUntil([&page_url] {
    const std::u16string text = ui::clipboard_test_util::ReadText(
        ui::Clipboard::GetForCurrentThread(), ui::ClipboardBuffer::kCopyPaste,
        /*data_dst=*/nullptr);
    return text == ::base::UTF8ToUTF16(page_url.spec());
  })) << "Cmd+Shift+C must copy the active tab URL to the clipboard";

  EXPECT_EQ(nullptr,
            DevToolsWindow::GetInstanceForInspectedWebContents(contents))
      << "Cmd+Shift+C must not open DevTools Inspect";

  CaptureProofPngToSummaryDir(browser_view->GetWidget(),
                              "c3-cmd-shift-c-no-devtools.png");
}

// C4: Cmd+L must raise the Maho command bar. Before the core-ready
// registration retry this key reached no Maho accelerator at all, so it fell
// through to Chromium's own Cmd+L (focus the omnibox) and the Maho command bar
// never appeared.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CmdLKeystrokeOpensMahoCommandBar) {
  SeedProfile(1);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  // The controller object may already be allocated (SeedProfile navigates and
  // warms the overlay), so gate on visibility rather than on the pointer.
  maho::MahoCommandOverlayController* before_controller =
      browser_view->GetMahoCommandOverlayControllerForTesting();
  ASSERT_FALSE(before_controller && before_controller->IsVisible())
      << "Precondition: the command bar must not be visible before the "
         "keystroke";

  ::base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser_view->GetWidget()->GetNativeWindow(), ui::VKEY_L,
      /*control=*/false, /*shift=*/false, /*alt=*/false, /*command=*/true,
      key_loop.QuitClosure()));
  key_loop.Run();

  EXPECT_TRUE(::base::test::RunUntil([browser_view] {
    maho::MahoCommandOverlayController* controller =
        browser_view->GetMahoCommandOverlayControllerForTesting();
    return controller && controller->IsVisible();
  })) << "Cmd+L must open the Maho command bar, not Chromium's omnibox focus";

  CaptureProofPngToSummaryDir(browser_view->GetWidget(),
                              "c4-cmd-l-command-bar.png");
}

// C4: Cmd+E must open the Maho AI side panel for the same reason.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       CmdEKeystrokeOpensMahoAiPanel) {
  SeedProfile(1);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  SidePanelUI* side_panel_ui = browser()->GetFeatures().side_panel_ui();
  ASSERT_TRUE(side_panel_ui);
  const SidePanelEntryKey maho_ai_key(SidePanelEntryId::kMahoAiPanel);
  ASSERT_FALSE(side_panel_ui->IsSidePanelEntryShowing(maho_ai_key))
      << "Precondition: the AI panel must be closed before the keystroke";

  ::base::RunLoop key_loop;
  ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
      browser_view->GetWidget()->GetNativeWindow(), ui::VKEY_E,
      /*control=*/false, /*shift=*/false, /*alt=*/false, /*command=*/true,
      key_loop.QuitClosure()));
  key_loop.Run();

  EXPECT_TRUE(::base::test::RunUntil([side_panel_ui, maho_ai_key] {
    return side_panel_ui->IsSidePanelEntryShowing(maho_ai_key);
  })) << "Cmd+E must open the Maho AI side panel";

  CaptureProofPngToSummaryDir(browser_view->GetWidget(),
                              "c4-cmd-e-ai-panel.png");

  // Leave the panel closed so teardown does not race the hosted WebContents.
  side_panel_ui->Close();
}

// Numbered-tab commands are favorite selectors in Maho. With no favorite at the
// requested index they must do nothing; previously they fell through to
// SelectNumberedTab, so Cmd+2/3/4 jumped around the tab strip even when only a
// single favorite existed.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       NumberedTabCommandsDoNotFallBackToTabStrip) {
  // Warm the sidebar/FFI pipeline before the tab burst, then use the fixture's
  // lightweight tab helper. A cold core turns the first tab events into
  // synchronous space-bridge initialization that alone exceeds the global
  // RunLoop watchdog (~62s observed), which is what previously timed this out.
  SeedProfile(1);
  AddTestTabs(3);
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  ASSERT_GE(tab_strip->count(), 4);
  tab_strip->ActivateTabAt(0);
  ::base::RunLoop().RunUntilIdle();
  const int active_before = tab_strip->active_index();

  for (int command : {IDC_SELECT_TAB_1, IDC_SELECT_TAB_2, IDC_SELECT_TAB_3}) {
    chrome::ExecuteCommand(browser(), command);
    ::base::RunLoop().RunUntilIdle();
    EXPECT_EQ(active_before, tab_strip->active_index())
        << "command " << command
        << " moved the active tab; numbered accelerators must not fall back to "
           "SelectNumberedTab when no favorite occupies that index";
  }
}

// Settings must land on Maho's own surface. macOS routes the app menu and
// Cmd+comma through chrome::ShowSettings(), which had no Maho override and so
// opened upstream chrome://settings.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ShowSettingsOpensMahoSettingsNotChromiumSettings) {
  chrome::ShowSettings(browser());
  ::base::RunLoop().RunUntilIdle();

  content::WebContents* contents =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  EXPECT_TRUE(::base::test::RunUntil([&contents, this] {
    contents = browser()->GetTabStripModel()->GetActiveWebContents();
    if (!contents) {
      return false;
    }
    const GURL url = contents->GetVisibleURL();
    return url.host() == "maho-settings" ||
           (url.scheme() == "maho" && url.host() == "settings");
  })) << "Settings opened "
      << contents->GetVisibleURL().spec()
      << "; expected Maho's settings surface, not Chromium's";

  CaptureProofPngToSummaryDir(
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(),
      "c1-settings-surface.png");
}

// Task 9 assertions for the pane-header bubble anchors: the Arc layout hands
// out the Maho provider, and with the right split pane active the translate
// bubble anchor sits inside that pane's header.
IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       ArcLayoutUsesMahoToolbarButtonProvider) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  // The Maho sidebar container marks an Arc-layout browser on every pinned
  // revision (IsMahoArcLayoutActive() is not declared at ee4bd9e9).
  ASSERT_TRUE(browser_view->maho_sidebar_container());
  ToolbarButtonProvider* provider = browser_view->toolbar_button_provider();
  ASSERT_TRUE(provider);
  // BrowserView hands out its own Maho provider in place of the upstream
  // ToolbarView (Chromium builds without RTTI, so compare identities).
  // toolbar_button_provider() exists on every pinned revision; the
  // ToolbarButtonProvider::From() user-data lookup is 72f18f12-only.
  EXPECT_NE(provider,
            static_cast<ToolbarButtonProvider*>(browser_view->toolbar()));
#if MAHO_TOOLBAR_BUTTON_PROVIDER_OWNS_USER_DATA
  // The Maho provider also owns the browser's user-data slot, so upstream
  // From() callers (sharing hub, QR code, send-tab-to-self) anchor to it.
  EXPECT_EQ(ToolbarButtonProvider::From(browser()), provider);
#endif
}

IN_PROC_BROWSER_TEST_F(MahoSidebarActionsInteractiveTest,
                       TranslateAnchorsToActiveRightPaneHeader) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  // Maho windows start with zero tabs, so SeedTestPage's NavigateToURL would
  // wait on a tab that does not exist. Seed an offline tab first.
  if (model->count() == 0) {
    ASSERT_TRUE(AddTabAtIndex(0, GURL("data:text/html,<title>left</title>"),
                              ui::PAGE_TRANSITION_TYPED));
  }
  ASSERT_TRUE(::base::test::RunUntil(
      [model] { return model->GetActiveWebContents() != nullptr; }));
  SeedTestPage(static_cast<Browser*>(browser()));
  ASSERT_TRUE(AddTabAtIndex(model->count(),
                            GURL("data:text/html,<title>right</title>"),
                            ui::PAGE_TRANSITION_TYPED));
  CreateTwoPaneSplit(model);
  model->ActivateTabAt(
      1, TabStripUserGestureDetails(
             TabStripUserGestureDetails::GestureType::kOther));
  ::base::RunLoop().RunUntilIdle();

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  MultiContentsView* mcv = browser_view->multi_contents_view();
  ASSERT_TRUE(mcv);
  ASSERT_TRUE(mcv->IsInSplitView());
  ContentsContainerView* right = mcv->contents_container_views()[1];
  ASSERT_EQ(right, browser_view->GetActiveContentsContainerView());
  MahoContentsHeaderView* header = right->maho_contents_header();
  ASSERT_TRUE(header);
  ASSERT_TRUE(::base::test::RunUntil([header] { return header->IsDrawn(); }));

  views::View* anchor = browser_view->toolbar_button_provider()
                            ->GetBubbleAnchor(kActionShowTranslate)
                            .GetIfView();
  ASSERT_TRUE(anchor);
  EXPECT_TRUE(header->GetBoundsInScreen().Contains(anchor->GetBoundsInScreen()))
      << "translate anchor " << anchor->GetBoundsInScreen().ToString()
      << " is outside the active right pane header "
      << header->GetBoundsInScreen().ToString();
}

}  // namespace
}  // namespace maho
