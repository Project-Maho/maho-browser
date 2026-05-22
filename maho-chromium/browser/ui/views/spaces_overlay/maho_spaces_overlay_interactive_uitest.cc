// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_controller.h"

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.h"
#include "ui/base/test/ui_controls.h"
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
        browser()->window()->GetNativeWindow(), key_code,
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

  views::View* WaitForCloseButton(MahoSpacesOverlayController* controller) {
    views::View* result = nullptr;
    EXPECT_TRUE(base::test::RunUntil([&]() {
      views::Widget* overlay = controller->GetOverlayWidgetForTesting();
      if (!overlay) {
        return false;
      }
      result = FindViewByAccessibleName(overlay->GetRootView(),
                                       u"Close spaces overlay");
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
      std::make_unique<MahoSpacesOverlayController>(browser());
  EXPECT_FALSE(controller->IsVisible());

  controller->Show(parent_widget);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));
  EXPECT_TRUE(controller->IsVisible());

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       DismissBeforeShowIsNoOp) {
  auto controller =
      std::make_unique<MahoSpacesOverlayController>(browser());
  EXPECT_FALSE(controller->IsVisible());

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  EXPECT_FALSE(controller->IsVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       DismissExplicitCancelHidesOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(browser());
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
      std::make_unique<MahoSpacesOverlayController>(browser());
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
      std::make_unique<MahoSpacesOverlayController>(browser());
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
                       CloseButtonClickDismissesOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(browser());
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  views::View* close_button = WaitForCloseButton(controller.get());
  ASSERT_TRUE(close_button);

  ClickScreenPoint(close_button->GetBoundsInScreen().CenterPoint());

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !controller->IsVisible(); }));
  EXPECT_FALSE(controller->IsVisible())
      << "Clicking the close button must dismiss the spaces overlay";
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       OverlayBoundsFillBrowserWindow) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(browser());
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  views::View* close_button = WaitForCloseButton(controller.get());
  ASSERT_TRUE(close_button) << "Close button not found in overlay";

  views::Widget* overlay_widget = close_button->GetWidget();
  ASSERT_TRUE(overlay_widget);

  const gfx::Rect parent_client = parent_widget->GetClientAreaBoundsInScreen();
  const gfx::Rect overlay_bounds = overlay_widget->GetWindowBoundsInScreen();
  EXPECT_EQ(parent_client, overlay_bounds)
      << "Overlay bounds must fill the browser window client area";

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       SecondShowReplacesExistingOverlay) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(browser());

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
      std::make_unique<MahoSpacesOverlayController>(browser());
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  views::View* close_button = WaitForCloseButton(controller.get());
  EXPECT_TRUE(close_button)
      << "Close button must be reachable from the overlay widget root view";
  if (close_button) {
    EXPECT_TRUE(close_button->GetVisible());
    EXPECT_FALSE(close_button->GetBoundsInScreen().IsEmpty());
  }

  controller->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
}

IN_PROC_BROWSER_TEST_F(MahoSpacesOverlayInteractiveUiTest,
                       DestroyControllerWhileVisibleDoesNotCrash) {
  views::Widget* parent_widget = GetBrowserWidget();
  ASSERT_TRUE(parent_widget);

  auto controller =
      std::make_unique<MahoSpacesOverlayController>(browser());
  controller->Show(parent_widget);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return controller->IsVisible(); }));

  controller.reset();
  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace maho
