// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_controller.h"

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

views::View* FindViewByAccessibleName(views::View* root,
                                      const std::u16string& name) {
  if (!root) {
    return nullptr;
  }
  ui::AXNodeData data;
  root->GetViewAccessibility().GetAccessibleNodeData(&data);
  const std::u16string ax_name =
      data.GetString16Attribute(ax::mojom::StringAttribute::kName);
  if (ax_name.find(name) != std::u16string::npos) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindViewByAccessibleName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

class MahoSpacesOverlayInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoSpacesOverlayInteractiveUiTest() = default;
  MahoSpacesOverlayInteractiveUiTest(
      const MahoSpacesOverlayInteractiveUiTest&) = delete;
  MahoSpacesOverlayInteractiveUiTest& operator=(
      const MahoSpacesOverlayInteractiveUiTest&) = delete;
  ~MahoSpacesOverlayInteractiveUiTest() override = default;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  }

 protected:
  views::Widget* GetBrowserWidget() {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
    EXPECT_TRUE(bv);
    return bv ? bv->GetWidget() : nullptr;
  }

  void SendKeyPress(ui::KeyboardCode key_code) {
    base::RunLoop loop;
    ASSERT_TRUE(ui_controls::SendKeyPressNotifyWhenDone(
        browser()->GetWindow()->GetNativeWindow(), key_code,
        false, false, false, false,
        loop.QuitClosure()));
    loop.Run();
    base::RunLoop().RunUntilIdle();
  }

  void ClickScreenPoint(const gfx::Point& pt) {
    ASSERT_TRUE(ui_controls::SendMouseMove(pt.x(), pt.y()));
    base::RunLoop loop;
    ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
        ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
        loop.QuitClosure()));
    loop.Run();
    base::RunLoop().RunUntilIdle();
  }

  views::View* WaitForBackButton(MahoSpacesOverlayController* controller) {
    views::View* result = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&]() {
      views::Widget* parent = GetBrowserWidget();
      if (!parent) {
        return false;
      }
      result = FindViewByAccessibleName(parent->GetRootView(),
                                       u"Back");
      return result != nullptr;
    }));
    return result;
  }
};

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       ShowMakesOverlayVisible) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  EXPECT_FALSE(controller->IsVisible());

  controller->Show(parent_widget);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));
  EXPECT_TRUE(controller->IsVisible());

  views::Widget* overlay_widget = controller->GetOverlayWidgetForTesting();
  ASSERT_TRUE(overlay_widget);
  views::View* root_view = overlay_widget->GetRootView();
  ASSERT_TRUE(root_view);
  ASSERT_TRUE(root_view->layer());
  EXPECT_TRUE(root_view->layer()->fills_bounds_opaquely());

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       DismissBeforeShowIsNoOp) {
  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  EXPECT_FALSE(controller->IsVisible());

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  EXPECT_FALSE(controller->IsVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       DismissExplicitCancelHidesOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !controller->IsVisible(); }));
  EXPECT_FALSE(controller->IsVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       ToggleOpensAndCloses) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  EXPECT_FALSE(controller->IsVisible());

  controller->Toggle(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));
  EXPECT_TRUE(controller->IsVisible());

  controller->Toggle(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !controller->IsVisible(); }));
  EXPECT_FALSE(controller->IsVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       EscapeKeyDismissesOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  SendKeyPress(ui::VKEY_ESCAPE);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !controller->IsVisible(); }));
  EXPECT_FALSE(controller->IsVisible())
      << "Escape key must dismiss the spaces overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       BackButtonClickDismissesOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(bv);
  auto* sidebar_container = static_cast<MahoSidebarContainerView*>(
      bv->maho_sidebar_container());
  ASSERT_TRUE(sidebar_container);

  sidebar_container->ToggleSpacesOverlay();

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return sidebar_container->IsSpacesOverlayVisible(); }));

  MahoSpacesOverlayController* controller =
      sidebar_container->overlay_host()->spaces_overlay_controller();
  ASSERT_TRUE(controller);

  views::View* back_button = WaitForBackButton(controller);
  ASSERT_TRUE(back_button);

  ClickScreenPoint(back_button->GetBoundsInScreen().CenterPoint());

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !sidebar_container->IsSpacesOverlayVisible(); }));
  EXPECT_FALSE(sidebar_container->IsSpacesOverlayVisible())
      << "Clicking the back button must dismiss the spaces overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       OverlayBoundsFillBrowserWindow) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(bv);
  auto* sidebar_container = static_cast<MahoSidebarContainerView*>(
      bv->maho_sidebar_container());
  ASSERT_TRUE(sidebar_container);

  sidebar_container->ToggleSpacesOverlay();

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return sidebar_container->IsSpacesOverlayVisible(); }));

  MahoSpacesOverlayController* controller =
      sidebar_container->overlay_host()->spaces_overlay_controller();
  ASSERT_TRUE(controller);

  views::Widget* overlay_widget = controller->GetOverlayWidgetForTesting();
  ASSERT_TRUE(overlay_widget);

  int inset_left = sidebar_container->GetSidebarSlotInsets().left();

  const gfx::Rect parent_client = parent_widget->GetClientAreaBoundsInScreen();
  const gfx::Rect overlay_bounds = overlay_widget->GetWindowBoundsInScreen();

  EXPECT_EQ(parent_client.x() + inset_left, overlay_bounds.x());
  EXPECT_EQ(parent_client.width() - inset_left, overlay_bounds.width());
  EXPECT_EQ(parent_client.y(), overlay_bounds.y());
  EXPECT_EQ(parent_client.height(), overlay_bounds.height());

  sidebar_container->DismissSpacesOverlay();
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       SecondShowReplacesExistingOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));

  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));
  EXPECT_TRUE(controller->IsVisible());

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       OverlaySurfaceViewIsVisibleAfterShow) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  views::Widget* overlay_widget = controller->GetOverlayWidgetForTesting();
  ASSERT_TRUE(overlay_widget);

  views::View* board_view = FindViewByAccessibleName(overlay_widget->GetRootView(), u"Spaces board");
  EXPECT_TRUE(board_view)
      << "Spaces board view must be reachable from the overlay widget root view";
  if (board_view) {
    EXPECT_TRUE(board_view->GetVisible());
    EXPECT_FALSE(board_view->GetBoundsInScreen().IsEmpty());
  }

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       DestroyControllerWhileVisibleDoesNotCrash) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(static_cast<Browser*>(browser()));
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  controller.reset();
  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace maho
