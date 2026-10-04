// Copyright 2026 Maho Browser. All rights reserved.
//
// In-process browser regression tests for the command overlay unified click
// path (Phase 4).
//
// Exercises the real focus/click entrypoint: BrowserView::SetFocusToLocationBar
// (is_user_initiated=true).  That method unconditionally calls
// ShowMahoCommandOverlayForCurrentTab(), which lazily creates the
// MahoCommandOverlayController on first use via
// GetOrCreateMahoCommandOverlayController().
//
// LAZY-INIT CONTRACT: maho_command_overlay_controller_ is null until the first
// call to ShowMahoCommandOverlayForCurrentTab() (reached via
// SetFocusToLocationBar(true), LocationBarView::RequestFocus(), or any other
// overlay entry point).
// GetMahoCommandOverlayControllerForTesting() therefore returns null before
// any triggering call.  Tests must trigger first, then read the controller.
//
// Tests:
//   1. FocusPath_ControllerIsReachableViaBrowserView — overlay wiring viability
//   2. FocusPath_UserInitiatedFocusOpensOverlay — entrypoint → IsVisible()
//   3. FocusPath_OverlayPrefillsCurrentUrl — URL prefill + select-all contract
//   4. FocusPath_EscapeDismissesOverlay — live dismiss→Hide() round-trip
//   5. FocusPath_RepeatedOpenHideCycles — overlay survives N open→hide→open
//      cycles through the real Hide() path (not a manual state reset).
//   6. FocusPath_OverlayHidesOnDeactivation — overlay hides when a second
//      browser window steals native window activation.
//   7. FocusPath_OnFocusInterceptOpensOverlay (Wave 1 gap closure)
//      — exercises the actual chromium_src OnFocus() intercept: calling
//      RequestFocus() on the real
//      LocationBarView triggers OnFocus(), which the overlay intercepts and
//      routes to ShowMahoCommandOverlayForCurrentTab(). This behavior requires
//      a live Browser + real LocationBarView.
//
// Access pattern: all BrowserView and controller state is read through
// public testing accessors (no private-field access):
//   BrowserView::GetMahoCommandOverlayControllerForTesting()
//   MahoCommandOverlayController::GetOverlayViewForTesting()
//   MahoCommandOverlayView::textfield()  (already public)

#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font.h"
#include "ui/gfx/range/range.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"

namespace {

class MahoCommandOverlayInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoCommandOverlayInteractiveUiTest() = default;
  MahoCommandOverlayInteractiveUiTest(
      const MahoCommandOverlayInteractiveUiTest&) = delete;
  MahoCommandOverlayInteractiveUiTest& operator=(
      const MahoCommandOverlayInteractiveUiTest&) = delete;
  ~MahoCommandOverlayInteractiveUiTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  BrowserView* GetBrowserView() {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
    EXPECT_TRUE(bv);
    return bv;
  }

  // Returns the controller only after it has been created by a triggering call.
  // maho_command_overlay_controller_ is null until ShowMahoCommandOverlay*()
  // is first invoked; call TriggerUserInitiatedFocus() (or RequestFocus()) and
  // wait for IsVisible() before calling this.
  maho::MahoCommandOverlayController* GetController() {
    BrowserView* bv = GetBrowserView();
    if (!bv) {
      return nullptr;
    }
    return bv->GetMahoCommandOverlayControllerForTesting();
  }

  // Invokes the real user-initiated focus entrypoint.
  // SetFocusToLocationBar(true) unconditionally calls
  // ShowMahoCommandOverlayForCurrentTab(), which lazily creates the controller
  // on the first call.
  void TriggerUserInitiatedFocus() {
    BrowserView* bv = GetBrowserView();
    ASSERT_TRUE(bv);
    bv->SetFocusToLocationBar(/*is_user_initiated=*/true);
  }

  LocationBarView* GetLocationBarView() {
    BrowserView* bv = GetBrowserView();
    return bv ? bv->GetLocationBarView() : nullptr;
  }

  content::WebContents* GetActiveWebContents() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_ControllerIsReachableViaBrowserView) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "SetFocusToLocationBar(true) must lazily create the controller and "
         "open the overlay; GetOrCreateMahoCommandOverlayController() wiring "
         "is broken.";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller)
      << "Controller must be non-null after the first triggering call.";
  EXPECT_TRUE(controller->IsVisible());

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_UserInitiatedFocusOpensOverlay) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "SetFocusToLocationBar(true) must open the overlay via "
         "ShowMahoCommandOverlayForCurrentTab(); wiring is broken.";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  EXPECT_TRUE(controller->IsVisible());

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_OverlayPrefillsCurrentUrl) {
  const GURL test_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), test_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);
  views::Textfield* textfield = overlay_view->textfield();
  ASSERT_TRUE(textfield);

  const std::string expected_url =
      GetActiveWebContents()->GetLastCommittedURL().spec();
  ASSERT_FALSE(expected_url.empty());

  EXPECT_EQ(base::UTF8ToUTF16(expected_url), textfield->GetText())
      << "Textfield must be prefilled with the active tab URL; got: "
      << base::UTF16ToUTF8(textfield->GetText());
  EXPECT_TRUE(textfield->GetPlaceholderText().empty())
      << "Current-tab open state should not show a placeholder when the URL is "
         "prefilled";

  const gfx::Range sel = textfield->GetSelectedRange();
  EXPECT_EQ(0u, sel.GetMin())
      << "select_initial_text=true must select from position 0";
  EXPECT_EQ(static_cast<uint32_t>(expected_url.size()), sel.GetMax())
      << "select_initial_text=true must select to end of the URL string";

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_CurrentTabSeededRowsRenderOnFirstOpen) {
  const GURL first_url = embedded_test_server()->GetURL("/title2.html");
  const GURL second_url = embedded_test_server()->GetURL("/title3.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& seeds = overlay_view->GetSeededResultsForTesting();
  ASSERT_TRUE(overlay_view->IsSeededStateForTesting());
  ASSERT_GE(seeds.size(), 2u);
  EXPECT_EQ(seeds.size(), overlay_view->GetRenderedResultCountForTesting())
      << "Seeded current-tab rows must already be painted on the first visible "
         "frame, without waiting for any async model replacement";
  EXPECT_EQ(seeds.size(),
            overlay_view->GetResultsContainerForTesting()->children().size())
      << "Seeded first-paint open must not flash a helper row before real "
         "results arrive";
  EXPECT_EQ(second_url.spec(), seeds.front().execution_payload);
  EXPECT_TRUE(overlay_view->TextfieldIsExpandedForTesting());
  EXPECT_NE(nullptr, overlay_view->GetActiveDescendantForTesting());

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_CurrentTabSeedUsesNavigationHistory) {
  const GURL first_url = embedded_test_server()->GetURL("/title2.html");
  const GURL second_url = embedded_test_server()->GetURL("/title3.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& seeds = overlay_view->GetSeededResultsForTesting();
  ASSERT_GE(seeds.size(), 2u);
  EXPECT_EQ(overlay_view->GetRenderedResultCountForTesting(), seeds.size());

  EXPECT_EQ(second_url.spec(), seeds[0].execution_payload);
  EXPECT_EQ("Title Of More Awesomeness", seeds[0].title);
  EXPECT_EQ(first_url.spec(), seeds[1].execution_payload);
  EXPECT_EQ("Title Of Awesomeness", seeds[1].title);

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_InputRowChromeFlatAtRuntime) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  ASSERT_NE(nullptr, overlay_view->GetSearchRowForTesting());
  EXPECT_EQ(nullptr, overlay_view->GetSearchRowForTesting()->GetBackground());
  EXPECT_EQ(nullptr, overlay_view->GetSearchRowForTesting()->GetBorder());

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_RowSubtitlePreservesUrlPath) {
  const GURL deep_url =
      embedded_test_server()->GetURL("/title2.html?cmd=palette#seeded");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), deep_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& children =
      overlay_view->GetResultsContainerForTesting()->children();
  ASSERT_FALSE(children.empty());
  auto* row =
      static_cast<maho::MahoCommandResultRowView*>(children.front().get());
  ASSERT_NE(nullptr, row->GetCompactMetadataLabelForTesting());
  EXPECT_NE(std::u16string::npos,
            row->GetCompactMetadataLabelForTesting()->GetText().find(
                u"/title2.html?cmd=palette#seeded"));

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_TitleWeightMatchesPolicyAtRuntime) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& children =
      overlay_view->GetResultsContainerForTesting()->children();
  ASSERT_FALSE(children.empty());
  auto* row =
      static_cast<maho::MahoCommandResultRowView*>(children.front().get());
  ASSERT_NE(nullptr, row->GetTitleLabelForTesting());
  EXPECT_EQ(
      maho::MahoCommandResultRowView::GetSeededRowTitleFontWeightForTesting(),
      row->GetTitleLabelForTesting()->font_list().GetFontWeight());

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_DeduplicatesDuplicateHistoryUrls) {
  const GURL first_url = embedded_test_server()->GetURL("/title2.html");
  const GURL second_url = embedded_test_server()->GetURL("/title3.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& seeds = overlay_view->GetSeededResultsForTesting();
  // The dedup contract requires first_url to appear exactly once even after
  // navigating first -> second -> first.  An extra pre-existing history entry
  // (e.g. the initial NTP or about:blank) may produce seeds.size() == 3, so
  // we assert GE(2) rather than EQ(2).
  ASSERT_GE(seeds.size(), 2u)
      << "Expected at least two seeded rows after navigating to two distinct "
         "URLs";
  EXPECT_EQ(overlay_view->GetRenderedResultCountForTesting(), seeds.size());

  // Row 0 must be the current page (first_url, the last navigation).
  EXPECT_EQ(first_url.spec(), seeds[0].execution_payload);

  // first_url must appear exactly once — duplicate collapsing is the contract.
  size_t first_url_count = 0;
  size_t second_url_count = 0;
  for (const auto& seed : seeds) {
    if (seed.execution_payload == first_url.spec()) {
      ++first_url_count;
    } else if (seed.execution_payload == second_url.spec()) {
      ++second_url_count;
    }
  }
  EXPECT_EQ(1u, first_url_count)
      << "first_url must appear exactly once in seeded results (dedup "
         "contract)";
  EXPECT_GE(second_url_count, 1u)
      << "second_url must appear at least once in seeded results";

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_UsesVisibleRowCapWithRealHistory) {
  const std::vector<GURL> urls = {
      embedded_test_server()->GetURL("/title1.html"),
      embedded_test_server()->GetURL("/title2.html"),
      embedded_test_server()->GetURL("/title3.html"),
      embedded_test_server()->GetURL("/button.html"),
      embedded_test_server()->GetURL("/click.html"),
      embedded_test_server()->GetURL("/cookie1.html"),
  };

  for (const GURL& url : urls) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const size_t visible_cap =
      maho::MahoCommandOverlayView::GetMaxSeededVisibleResultsForTesting();

  EXPECT_EQ(visible_cap, overlay_view->GetRenderedResultCountForTesting())
      << "Opening overlay after more navigations than the seeded cap must show "
         "exactly the centrally-defined visible row count";
  EXPECT_EQ(visible_cap, overlay_view->GetSeededResultsForTesting().size());

  EXPECT_EQ(visible_cap,
            overlay_view->GetResultsContainerForTesting()->children().size())
      << "No hint label must be present in the results container when seeded "
         "state is active at the visible cap";

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(
    MahoCommandOverlayInteractiveUiTest,
    CurrentTabSeed_InternalHistoryEntriesDoNotConsumeVisibleCap) {
  const GURL ordinary_url = embedded_test_server()->GetURL("/title1.html");
  const GURL favicon_url =
      embedded_test_server()->GetURL("/favicon/title2_with_favicon.html");
  const GURL page3_url = embedded_test_server()->GetURL("/title3.html");
  const GURL page4_url = embedded_test_server()->GetURL("/button.html");
  const GURL page5_url = embedded_test_server()->GetURL("/click.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), ordinary_url));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://history/")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), favicon_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page3_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page4_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page5_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& seeds = overlay_view->GetSeededResultsForTesting();
  const size_t visible_cap =
      maho::MahoCommandOverlayView::GetMaxSeededVisibleResultsForTesting();
  ASSERT_EQ(visible_cap, seeds.size());
  EXPECT_EQ(visible_cap, overlay_view->GetRenderedResultCountForTesting());

  for (const auto& seed : seeds) {
    EXPECT_FALSE(seed.execution_payload.starts_with("chrome://"))
        << "Internal pages must remain suppressed from seeded current-tab rows";
  }

  EXPECT_EQ(page5_url.spec(), seeds[0].execution_payload);
  EXPECT_EQ(page4_url.spec(), seeds[1].execution_payload);
  EXPECT_EQ(page3_url.spec(), seeds[2].execution_payload);
  EXPECT_EQ(favicon_url.spec(), seeds[3].execution_payload);
  EXPECT_EQ(ordinary_url.spec(), seeds[4].execution_payload);

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_RowContentMatchesNavigation) {
  const std::vector<GURL> urls = {
      embedded_test_server()->GetURL("/title1.html"),
      embedded_test_server()->GetURL("/title2.html"),
      embedded_test_server()->GetURL("/title3.html"),
      embedded_test_server()->GetURL("/button.html"),
      embedded_test_server()->GetURL("/click.html"),
      embedded_test_server()->GetURL("/cookie1.html"),
  };

  for (const GURL& url : urls) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  }
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  const auto& seeds = overlay_view->GetSeededResultsForTesting();
  const size_t visible_cap =
      maho::MahoCommandOverlayView::GetMaxSeededVisibleResultsForTesting();
  ASSERT_EQ(visible_cap, seeds.size());
  EXPECT_EQ(visible_cap, overlay_view->GetRenderedResultCountForTesting())
      << "Rendered row count must equal the visible seeded cap";

  EXPECT_EQ(urls[5].spec(), seeds[0].execution_payload)
      << "Row 0 must be the current page";
  EXPECT_EQ(urls[5].spec(), seeds[0].title)
      << "Current page without a <title> must fall back to the URL string";

  EXPECT_EQ(urls[4].spec(), seeds[1].execution_payload)
      << "Row 1 must be the most recent prior navigation entry";
  EXPECT_EQ(urls[4].spec(), seeds[1].title)
      << "Untitled navigation entries must use the URL fallback";

  EXPECT_EQ(urls[3].spec(), seeds[2].execution_payload);
  EXPECT_EQ("Button", seeds[2].title);

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_ArrowDownMovesSelectionToRow1) {
  const GURL page1 = embedded_test_server()->GetURL("/title2.html");
  const GURL page2 = embedded_test_server()->GetURL("/title3.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page2));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  ASSERT_GE(overlay_view->GetSeededResultsForTesting().size(), 2u);

  views::ViewAccessibility* active_before =
      overlay_view->GetActiveDescendantForTesting();
  ASSERT_NE(nullptr, active_before);

  const auto& children_before =
      overlay_view->GetResultsContainerForTesting()->children();
  ASSERT_GE(children_before.size(), 2u);
  EXPECT_EQ(&children_before.front()->GetViewAccessibility(), active_before)
      << "Row 0 must be selected on open";

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_DOWN, false,
                                              false, false, false))
      << "Failed to dispatch Down arrow key";

  views::ViewAccessibility* active_after =
      overlay_view->GetActiveDescendantForTesting();
  ASSERT_NE(nullptr, active_after);
  EXPECT_NE(active_before, active_after)
      << "Active descendant must change after Down arrow press";

  const auto& children_after =
      overlay_view->GetResultsContainerForTesting()->children();
  ASSERT_GE(children_after.size(), 2u);
  EXPECT_EQ(&children_after[1]->GetViewAccessibility(), active_after)
      << "Row 1 must be the active descendant after one Down press";

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_EnterOnRow0NavigatesToCurrentUrl) {
  const GURL page1 = embedded_test_server()->GetURL("/title2.html");
  const GURL page2 = embedded_test_server()->GetURL("/title3.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page1));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page2));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  }));

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  ASSERT_GE(overlay_view->GetSeededResultsForTesting().size(), 1u);
  EXPECT_EQ(page2.spec(),
            overlay_view->GetSeededResultsForTesting()[0].execution_payload)
      << "Row 0 seed must be the current page URL before pressing Enter";

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN, false,
                                              false, false, false))
      << "Failed to dispatch Return key event";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return !c || !c->IsVisible();
  })) << "Pressing Enter on row 0 must dismiss the overlay";

  EXPECT_EQ(page2, GetActiveWebContents()->GetLastCommittedURL())
      << "Enter on the current-page seed must keep the browser on the same URL";
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_EscapeDismissesOverlay) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "Precondition: overlay must open before Escape test";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);
  views::Textfield* textfield = overlay_view->textfield();
  ASSERT_TRUE(textfield);

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                              /*control=*/false,
                                              /*shift=*/false,
                                              /*alt=*/false,
                                              /*command=*/false))
      << "Failed to dispatch Escape key event via SendKeyPressSync";

  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }))
      << "Escape must dismiss the overlay; dismiss_callback wiring between "
         "MahoCommandOverlayView and MahoCommandOverlayController is broken.";
  EXPECT_FALSE(controller->IsVisible());
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_RepeatedOpenHideCycles) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  constexpr int kCycles = 3;
  for (int i = 0; i < kCycles; ++i) {
    TriggerUserInitiatedFocus();

    ASSERT_TRUE(base::test::RunUntil([&]() {
      maho::MahoCommandOverlayController* c = GetController();
      return c && c->IsVisible();
    })) << "Cycle "
        << i << ": SetFocusToLocationBar(true) failed to open overlay";

    maho::MahoCommandOverlayController* controller = GetController();
    ASSERT_TRUE(controller) << "Cycle " << i << ": controller is null";

    maho::MahoCommandOverlayView* overlay_view =
        controller->GetOverlayViewForTesting();
    ASSERT_TRUE(overlay_view) << "Cycle " << i << ": overlay view is null";
    ASSERT_TRUE(overlay_view->textfield())
        << "Cycle " << i << ": textfield is null";

    controller->Hide();
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return !controller->IsVisible();
    })) << "Cycle "
        << i << ": Hide() did not hide the overlay";
  }
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_OverlayHidesOnDeactivation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "Precondition: overlay must open before deactivation test";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  Browser* second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(second_browser));

  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }))
      << "Overlay must hide when the browser window loses activation to a "
         "second browser window; OnWidgetActivationChanged hide path is "
         "broken.";

  {
    base::RunLoop drain;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, drain.QuitClosure(), base::Milliseconds(300));
    drain.Run();
  }
  CloseBrowserSynchronously(second_browser);
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_OnFocusInterceptOpensOverlay) {

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "SetFocusToLocationBar(true) "
         "(the convergence point of the OnFocus() intercept) must open the "
         "overlay via ShowMahoCommandOverlayForCurrentTab().";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  EXPECT_TRUE(controller->IsVisible());

  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);
  views::Textfield* textfield = overlay_view->textfield();
  ASSERT_TRUE(textfield);

  const std::string expected_url =
      GetActiveWebContents()->GetLastCommittedURL().spec();
  EXPECT_EQ(base::UTF8ToUTF16(expected_url), textfield->GetText())
      << "Overlay opened via enabled-path must prefill the current URL";

  const gfx::Range sel = textfield->GetSelectedRange();
  EXPECT_EQ(0u, sel.GetMin());
  EXPECT_EQ(static_cast<uint32_t>(expected_url.size()), sel.GetMax())
      << "select_initial_text=true must produce full select-all";

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_EscapeRestoresFocusToPreviousView) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  LocationBarView* location_bar = GetLocationBarView();
  ASSERT_TRUE(location_bar);
  location_bar->RequestFocus();
  base::RunLoop().RunUntilIdle();

  BrowserView* bv = GetBrowserView();
  ASSERT_TRUE(bv);
  views::FocusManager* fm = bv->GetWidget()->GetFocusManager();
  ASSERT_TRUE(fm);
  views::View* pre_show_focused = fm->GetFocusedView();
  ASSERT_TRUE(pre_show_focused)
      << "A view must hold focus before opening the overlay";

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "Precondition: overlay must open";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                              /*control=*/false,
                                              /*shift=*/false,
                                              /*alt=*/false,
                                              /*command=*/false))
      << "Failed to dispatch Escape";

  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }))
      << "Escape must dismiss the overlay";

  // After hide the FocusManager must have focus on the pre-show view (or on
  // some view within the same widget, since the exact focused view may have
  // been reassigned by the platform).  The key contract is that focus returned
  // to the parent browser window rather than staying orphaned on the dismissed
  // overlay widget.
  EXPECT_EQ(pre_show_focused, fm->GetFocusedView())
      << "Escape must restore focus to the view that was focused before Show()";
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       FocusPath_DeactivationRestoresFocusToPreviousView) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  LocationBarView* location_bar = GetLocationBarView();
  ASSERT_TRUE(location_bar);
  location_bar->RequestFocus();
  base::RunLoop().RunUntilIdle();

  BrowserView* bv = GetBrowserView();
  ASSERT_TRUE(bv);
  views::FocusManager* fm = bv->GetWidget()->GetFocusManager();
  ASSERT_TRUE(fm);
  views::View* pre_show_focused = fm->GetFocusedView();
  ASSERT_TRUE(pre_show_focused);

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "Precondition: overlay must open";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);

  Browser* second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(second_browser));

  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }))
      << "Overlay must hide on window deactivation";

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(pre_show_focused, fm->GetFocusedView())
      << "Deactivation-triggered hide must restore focus to the pre-show view";

  {
    base::RunLoop drain;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, drain.QuitClosure(), base::Milliseconds(300));
    drain.Run();
  }
  CloseBrowserSynchronously(second_browser);
}

namespace {

class MahoMiniInteractiveTest : public InProcessBrowserTest {
 public:
  MahoMiniInteractiveTest() = default;
  MahoMiniInteractiveTest(const MahoMiniInteractiveTest&) = delete;
  MahoMiniInteractiveTest& operator=(const MahoMiniInteractiveTest&) = delete;
  ~MahoMiniInteractiveTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  Browser* FindPopupBrowser() {
    Browser* result = nullptr;
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi->GetType() == BrowserWindowInterface::TYPE_POPUP &&
          bwi != browser()) {
        result = static_cast<Browser*>(bwi);
        break;
      }
    }
    return result;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoMiniInteractiveTest,
                       MahoMini_OpensPopupFromNormalBrowser) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  const size_t initial_browser_count =
      GlobalBrowserCollection::GetInstance()->GetSize();

  maho::ExecuteCommandAction(static_cast<Browser*>(browser()), "maho_mini");

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() >
           initial_browser_count;
  })) << "maho_mini action must open a new popup browser window";

  Browser* popup = FindPopupBrowser();
  ASSERT_TRUE(popup) << "A TYPE_POPUP browser must exist after maho_mini";
  EXPECT_EQ(Browser::TYPE_POPUP, popup->type());
}

IN_PROC_BROWSER_TEST_F(MahoMiniInteractiveTest,
                       LaunchMahoMini_WithNoOpenBrowser) {
  const size_t initial_browser_count =
      GlobalBrowserCollection::GetInstance()->GetSize();

  maho::MahoMiniRequest request;
  request.url = embedded_test_server()->GetURL("/title1.html");

  // Launch with nullptr context (parentless launch)
  maho::LaunchMahoMini(nullptr, request);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() >
           initial_browser_count;
  })) << "LaunchMahoMini must open a new popup browser window";

  Browser* popup = FindPopupBrowser();
  ASSERT_TRUE(popup) << "A TYPE_POPUP browser must exist after LaunchMahoMini";
  EXPECT_EQ(Browser::TYPE_POPUP, popup->type());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(
      request.url,
      popup->GetTabStripModel()->GetActiveWebContents()->GetLastCommittedURL());

  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoMiniInteractiveTest,
                       MahoMini_PopupNavigatesIndependently) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  const GURL opener_url = browser()
                              ->GetTabStripModel()
                              ->GetActiveWebContents()
                              ->GetLastCommittedURL();

  maho::ExecuteCommandAction(static_cast<Browser*>(browser()), "maho_mini");

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return FindPopupBrowser() != nullptr;
  })) << "Popup must open before navigation test";

  Browser* popup = FindPopupBrowser();
  ASSERT_TRUE(popup);

  const GURL nav_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(popup, nav_url));
  base::RunLoop().RunUntilIdle();

  const GURL popup_url =
      popup->GetTabStripModel()->GetActiveWebContents()->GetLastCommittedURL();
  EXPECT_EQ(nav_url, popup_url) << "Popup must navigate to the target URL";

  const GURL opener_url_after = browser()
                                    ->GetTabStripModel()
                                    ->GetActiveWebContents()
                                    ->GetLastCommittedURL();
  EXPECT_EQ(opener_url, opener_url_after)
      << "Navigating in the popup must not change the opener's active tab URL";

  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoMiniInteractiveTest,
                       MahoMini_PromoteClosesPopupAndMovesTabToNormalBrowser) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::ExecuteCommandAction(static_cast<Browser*>(browser()), "maho_mini");

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return FindPopupBrowser() != nullptr;
  })) << "Popup must open before promote test";

  Browser* popup = FindPopupBrowser();
  ASSERT_TRUE(popup);

  const GURL nav_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(popup, nav_url));
  base::RunLoop().RunUntilIdle();

  const int opener_tab_count_before = browser()->GetTabStripModel()->count();

  maho::ExecuteCommandAction(popup, "maho_mini");

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return FindPopupBrowser() == nullptr;
  })) << "maho_mini from popup must close the popup via PromoteToTab";

  EXPECT_EQ(opener_tab_count_before + 1, browser()->GetTabStripModel()->count())
      << "Promoted tab must be inserted into the normal browser";

  const GURL promoted_url = browser()
                                ->GetTabStripModel()
                                ->GetActiveWebContents()
                                ->GetLastCommittedURL();
  EXPECT_EQ(nav_url, promoted_url)
      << "The promoted tab must carry the URL that was loaded in the popup";
}

// Regression tests for the new-tab submit path:
// Typed navigation in kNewTab mode always opens a new foreground tab.
// Selecting an existing-tab result in kNewTab mode activates the existing tab
// (the tab-activate branch in OpenCommandSuggestion wins over disposition).

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       NewTab_EnterOnTypedUrlOpensNewTab) {
  const GURL initial_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), initial_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  const int initial_tab_count = browser()->GetTabStripModel()->count();

  BrowserView* bv = GetBrowserView();
  ASSERT_TRUE(bv);
  bv->ShowMahoCommandOverlayForNewTab();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "ShowMahoCommandOverlayForNewTab must open the overlay";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);
  views::Textfield* textfield = overlay_view->textfield();
  ASSERT_TRUE(textfield);

  const GURL target_url = embedded_test_server()->GetURL("/title2.html");
  textfield->SetText(base::UTF8ToUTF16(target_url.spec()));

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN, false,
                                              false, false, false))
      << "Failed to dispatch Return key event";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return !c || !c->IsVisible();
  })) << "Pressing Enter must dismiss the overlay";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->GetTabStripModel()->count() > initial_tab_count;
  })) << "Enter in new-tab mode must create a new tab";

  EXPECT_EQ(initial_tab_count + 1, browser()->GetTabStripModel()->count())
      << "Tab count must increase by exactly 1";

  content::WebContents* new_active =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(new_active);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return new_active->GetLastCommittedURL() == target_url ||
           new_active->GetVisibleURL() == target_url;
  })) << "New tab must navigate to the typed URL; got: "
      << new_active->GetLastCommittedURL().spec();

  EXPECT_EQ(
      initial_url,
      browser()->GetTabStripModel()->GetWebContentsAt(0)->GetLastCommittedURL())
      << "Original tab URL must remain unchanged after new-tab open";
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       NewTab_SelectingExistingTabResultActivatesExistingTab) {
  const GURL page1 = embedded_test_server()->GetURL("/title1.html");
  const GURL page2 = embedded_test_server()->GetURL("/title2.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page1));

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), page2, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  base::RunLoop().RunUntilIdle();

  browser()->GetTabStripModel()->ActivateTabAt(0);
  ASSERT_EQ(page1, GetActiveWebContents()->GetLastCommittedURL());

  const int tab_count_before = browser()->GetTabStripModel()->count();
  ASSERT_EQ(2, tab_count_before);

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  BrowserView* bv = GetBrowserView();
  ASSERT_TRUE(bv);
  bv->ShowMahoCommandOverlayForNewTab();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "ShowMahoCommandOverlayForNewTab must open the overlay";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  maho::CommandSuggestion tab_suggestion;
  tab_suggestion.type = maho::CommandSuggestionType::kTab;
  tab_suggestion.title = "Title Of Awesomeness";
  tab_suggestion.execution_payload = page2.spec();
  tab_suggestion.tab_index = 1;
  overlay_view->InitializeSeededStateForTesting({tab_suggestion});
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN, false,
                                              false, false, false))
      << "Failed to dispatch Return key event";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return !c || !c->IsVisible();
  })) << "Pressing Enter on an existing-tab result must dismiss the overlay";

  EXPECT_EQ(tab_count_before, browser()->GetTabStripModel()->count())
      << "Tab count must not increase: selecting an existing tab activates it, "
         "never duplicates it";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return GetActiveWebContents()->GetLastCommittedURL() == page2;
  })) << "The existing tab at index 1 must become the active tab";

  EXPECT_EQ(page2, GetActiveWebContents()->GetLastCommittedURL())
      << "Active tab must be page2 (the activated existing tab)";
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       NewTab_SelectingNavigationSuggestionOpensNewTab) {
  const GURL initial_url = embedded_test_server()->GetURL("/title1.html");
  const GURL nav_url = embedded_test_server()->GetURL("/title2.html");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), initial_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  const int tab_count_before = browser()->GetTabStripModel()->count();

  BrowserView* bv = GetBrowserView();
  ASSERT_TRUE(bv);
  bv->ShowMahoCommandOverlayForNewTab();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "ShowMahoCommandOverlayForNewTab must open the overlay";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  maho::CommandSuggestion nav_suggestion;
  nav_suggestion.type = maho::CommandSuggestionType::kNavigation;
  nav_suggestion.title = "Title 2";
  nav_suggestion.execution_payload = nav_url.spec();
  overlay_view->UpdateResultViewsForTesting({nav_suggestion});
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN, false,
                                              false, false, false))
      << "Failed to dispatch Return key event";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return !c || !c->IsVisible();
  })) << "Pressing Enter on a navigation result in new-tab mode must dismiss "
         "the overlay";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->GetTabStripModel()->count() > tab_count_before;
  })) << "Selecting a navigation suggestion in new-tab mode must open a NEW "
         "tab, not reuse the current one";

  EXPECT_EQ(tab_count_before + 1, browser()->GetTabStripModel()->count())
      << "Tab count must increase by exactly 1";

  EXPECT_EQ(
      initial_url,
      browser()->GetTabStripModel()->GetWebContentsAt(0)->GetLastCommittedURL())
      << "The original tab must remain at its original URL";
}

IN_PROC_BROWSER_TEST_F(MahoCommandOverlayInteractiveUiTest,
                       CurrentTabSeed_DividerHiddenOnOrdinarySiteFocusPath) {
  const GURL ordinary_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), ordinary_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  TriggerUserInitiatedFocus();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    maho::MahoCommandOverlayController* c = GetController();
    return c && c->IsVisible();
  })) << "Overlay must open via the real user-initiated focus path";

  maho::MahoCommandOverlayController* controller = GetController();
  ASSERT_TRUE(controller);
  maho::MahoCommandOverlayView* overlay_view =
      controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay_view);

  ASSERT_TRUE(overlay_view->IsSeededStateForTesting())
      << "Ordinary-site navigation must produce seeded state on open";
  ASSERT_GE(overlay_view->GetSeededResultsForTesting().size(), 1u)
      << "At least one seeded result must be present";

  EXPECT_FALSE(overlay_view->IsDividerVisibleForTesting())
      << "Divider must be hidden in the seeded kCurrentTab path on an "
         "ordinary site (Cmd+L acceptance path invariant)";

  controller->Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !controller->IsVisible(); }));
}
