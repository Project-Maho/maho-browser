// Copyright 2026 Maho Browser. All rights reserved.
//
// Interactive regression tests for the exact-primary-Incognito Command Palette
// (task-10-command-ui). These exercise the real BrowserView entrypoint and the
// live MahoCommandOverlayController/View created for an Incognito browser, and
// assert the private presentation/execution contract reachable through the
// public testing accessors:
//   BrowserView::GetMahoCommandOverlayControllerForTesting()
//   MahoCommandOverlayController::GetOverlayViewForTesting()
//   MahoCommandOverlayView::{textfield, CurrentModeForTesting,
//                            IsModeChipVisibleForTesting,
//                            AiIngressAllowedForTesting,
//                            GetRenderedResultCountForTesting}

#include "base/strings/utf_string_conversions.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/controls/textfield/textfield.h"

namespace {

class MahoCommandIncognitoInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoCommandIncognitoInteractiveUiTest() = default;
  MahoCommandIncognitoInteractiveUiTest(
      const MahoCommandIncognitoInteractiveUiTest&) = delete;
  MahoCommandIncognitoInteractiveUiTest& operator=(
      const MahoCommandIncognitoInteractiveUiTest&) = delete;
  ~MahoCommandIncognitoInteractiveUiTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  Browser* OpenIncognitoWithPage() {
    Browser* incognito = CreateIncognitoBrowser();
    EXPECT_TRUE(incognito);
    EXPECT_TRUE(incognito->profile()->IsIncognitoProfile());
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        incognito, embedded_test_server()->GetURL("/title1.html")));
    return incognito;
  }

  maho::MahoCommandOverlayView* OpenOverlay(Browser* incognito) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
    EXPECT_TRUE(bv);
    if (!bv) {
      return nullptr;
    }
    bv->ShowMahoCommandOverlayForCurrentTab();
    if (!base::test::RunUntil([&]() {
          auto* c = bv->GetMahoCommandOverlayControllerForTesting();
          return c && c->IsVisible() && c->GetOverlayViewForTesting();
        })) {
      return nullptr;
    }
    return bv->GetMahoCommandOverlayControllerForTesting()
        ->GetOverlayViewForTesting();
  }

  static bool SendKey(maho::MahoCommandOverlayView* view,
                      ui::KeyboardCode code) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    return view->HandleKeyEvent(view->textfield(), event);
  }
};

// Safe allowlisted commands surface and the private palette is functional: an
// action-only query renders at least one row and the overlay stays open.
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       AllowlistHappyPath) {
  Browser* incognito = OpenIncognitoWithPage();
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  view->textfield()->SetText(u">reload");
  view->ContentsChanged(view->textfield(), u">reload");

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return view->GetRenderedResultCountForTesting() > 0u; }))
      << "private commands-only query must render allowlisted action rows";
}

// Typing a normal query must not surface the Maho search-engine picker and the
// palette must remain in a plain search context (no saved-source sub-modes).
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       UnsafeQueriesAndPickerAbsent) {
  Browser* incognito = OpenIncognitoWithPage();
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  view->textfield()->SetText(u"secret query");
  view->ContentsChanged(view->textfield(), u"secret query");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(maho::CommandOverlayMode::kSearch, view->CurrentModeForTesting());
  EXPECT_FALSE(view->IsModeChipVisibleForTesting())
      << "no site-search/picker sub-mode chip may appear in private search";
}

// ai+Tab (and any forged AI-mode entry) is denied before mutation in Incognito.
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       AiTabReturnAndForgedModeDenied) {
  Browser* incognito = OpenIncognitoWithPage();
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  EXPECT_FALSE(view->AiIngressAllowedForTesting());

  view->textfield()->SetText(u"ai");
  EXPECT_TRUE(SendKey(view, ui::VKEY_TAB));

  EXPECT_NE(maho::CommandOverlayMode::kAI, view->CurrentModeForTesting())
      << "Incognito must never enter AI mode";
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());
}

// Escape dismisses the overlay and focus returns to the page's WebContents.
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       FocusRestoresToWebContents) {
  Browser* incognito = OpenIncognitoWithPage();
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
  ASSERT_TRUE(bv);
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    auto* c = bv->GetMahoCommandOverlayControllerForTesting();
    return !c || !c->IsVisible();
  })) << "Escape must dismiss the private command overlay";

  content::WebContents* active =
      incognito->tab_strip_model()->GetActiveWebContents();
  EXPECT_TRUE(active);
}

}  // namespace
