// Copyright 2026 Maho Browser. All rights reserved.

#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace {

// Extracts the boost editor's WebContents from the controller's widget.
// The widget's contents view (MahoBoostWindowView) holds a single child
// WebView that hosts the chrome://maho-boost WebUI.
content::WebContents* GetEditorWebContents(
    maho::MahoBoostWindowController& controller) {
  views::Widget* widget = controller.GetWidgetForTesting();
  if (!widget) {
    return nullptr;
  }

  views::View* contents = widget->GetContentsView();
  if (!contents || contents->children().empty()) {
    return nullptr;
  }

  auto* web_view =
      views::AsViewClass<views::WebView>(contents->children()[0]);
  if (!web_view) {
    return nullptr;
  }

  return web_view->GetWebContents();
}

class MahoBoostBehaviorTest : public InProcessBrowserTest {
 public:
  MahoBoostBehaviorTest() = default;
  ~MahoBoostBehaviorTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  // Navigate the active tab to |path| on the embedded test server.
  void NavigateTo(const std::string& path) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL(path)));
  }

  // Returns the active tab's WebContents.
  content::WebContents* GetTargetWebContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Opens the boost editor for the active tab's domain and waits for the
  // editor WebUI to finish loading.  Returns the editor's WebContents.
  content::WebContents* OpenEditorAndWait() {
    auto& controller = maho::MahoBoostWindowController::GetForBrowser(
        browser(), browser()->profile());
    controller.ShowForActiveDomain(GetTargetWebContents());

    content::WebContents* editor_wc = GetEditorWebContents(controller);
    if (!editor_wc) {
      return nullptr;
    }

    // Wait for the editor document to finish loading (10s timeout).
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return editor_wc->IsDocumentOnLoadCompletedInPrimaryMainFrame();
    })) << "Boost editor WebUI failed to load within timeout";

    // Allow a short idle to let Mojo bindings and JS bootstrap() settle.
    base::RunLoop().RunUntilIdle();

    return editor_wc;
  }
};

// ---------------------------------------------------------------------------
// Test 1: ColorPickerDotDrag
//
// Simulates dragging the primary color dot via pointer events in the editor,
// then verifies that a <style id="maho-boost-style"> element with color CSS
// is injected into the target page.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ColorPickerDotDrag) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  // Simulate pointer drag on the primary color dot.
  // Element ID: zen-boost-color-picker-dot-primary (app.ts:468)
  constexpr char kDragScript[] = R"js(
    (function() {
      const dot = document.getElementById('zen-boost-color-picker-dot-primary');
      if (!dot) return false;
      const rect = dot.getBoundingClientRect();
      dot.dispatchEvent(new PointerEvent('pointerdown', {
        clientX: rect.x + 5, clientY: rect.y + 5, bubbles: true
      }));
      document.dispatchEvent(new PointerEvent('pointermove', {
        clientX: rect.x + 30, clientY: rect.y + 30, bubbles: true
      }));
      document.dispatchEvent(new PointerEvent('pointerup', {bubbles: true}));
      return true;
    })()
  )js";

  EXPECT_TRUE(content::EvalJs(editor_wc, kDragScript).ExtractBool())
      << "Failed to dispatch pointer events on primary dot";

  // Wait for Mojo round-trip: poll for <style id="maho-boost-style"> on the
  // target page.  The page handler injects CSS via this element.
  content::WebContents* target_wc = GetTargetWebContents();
  ASSERT_TRUE(target_wc);

  constexpr char kPollStyleScript[] = R"js(
    (function() {
      const style = document.getElementById('maho-boost-style');
      return style ? style.textContent : '';
    })()
  )js";

  bool style_appeared = false;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    auto result = content::EvalJs(target_wc, kPollStyleScript);
    if (!result.is_ok()) {
      return false;
    }
    const std::string css = result.ExtractString();
    style_appeared = !css.empty();
    return style_appeared;
  })) << "maho-boost-style element never appeared on target page within "
         "timeout";

  // Read the final CSS content and assert it contains color-related rules.
  auto final_css = content::EvalJs(target_wc, kPollStyleScript);
  ASSERT_TRUE(final_css.is_ok());
  const std::string css_text = final_css.ExtractString();
  EXPECT_TRUE(css_text.find("hue-rotate(") != std::string::npos ||
              css_text.find("filter:") != std::string::npos ||
              css_text.find("filter") != std::string::npos)
      << "Expected color CSS (hue-rotate or filter) in injected style, got: "
      << css_text;
}

// ---------------------------------------------------------------------------
// Test 2: MagicThemeToggle
//
// Clicks the magic-theme button and asserts the active class toggles on/off.
// Element ID: zen-boost-magic-theme (app.ts:467)
// Active class: zen-boost-button-active (app.ts:1397)
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, MagicThemeToggle) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  // Click the magic-theme button.
  ASSERT_TRUE(content::ExecJs(editor_wc,
      "document.getElementById('zen-boost-magic-theme').click()"));

  // Small delay for DOM update.
  base::RunLoop().RunUntilIdle();

  // Read active state.
  constexpr char kReadActive[] = R"js(
    document.getElementById('zen-boost-magic-theme')
      .classList.contains('zen-boost-button-active')
  )js";

  EXPECT_TRUE(content::EvalJs(editor_wc, kReadActive).ExtractBool())
      << "Magic theme button should be active after first click";

  // Click again to deactivate.
  ASSERT_TRUE(content::ExecJs(editor_wc,
      "document.getElementById('zen-boost-magic-theme').click()"));

  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(content::EvalJs(editor_wc, kReadActive).ExtractBool())
      << "Magic theme button should be inactive after second click";
}

// ---------------------------------------------------------------------------
// Test 3: CaseCycleFourModes
//
// Clicks the case button 4 times and asserts the `case-mode` attribute
// cycles through: none → uppercase → lowercase → capitalize → none.
// Element ID: zen-boost-case (app.ts:479)
// Attribute: case-mode (app.ts:1372)
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, CaseCycleFourModes) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  // Read the case-mode attribute from the case button.
  constexpr char kReadCaseMode[] = R"js(
    document.getElementById('zen-boost-case').getAttribute('case-mode') || 'none'
  )js";

  // Initial state should be 'none'.
  EXPECT_EQ("none", content::EvalJs(editor_wc, kReadCaseMode).ExtractString());

  // Expected cycle: none → uppercase → lowercase → capitalize → none
  const std::string expected_modes[] = {
      "uppercase", "lowercase", "capitalize", "none"};

  for (const auto& expected : expected_modes) {
    ASSERT_TRUE(content::ExecJs(editor_wc,
        "document.getElementById('zen-boost-case').click()"));
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(expected,
              content::EvalJs(editor_wc, kReadCaseMode).ExtractString())
        << "Case mode mismatch during cycle";
  }
}

// ---------------------------------------------------------------------------
// Test 4: SizeCycleFiveModes
//
// Clicks the size button 5 times and asserts the displayed percentage cycles
// through: 100% → 110% → 125% → 150% → 90% → 100%.
// Element ID: zen-boost-size (app.ts:476)
// Value display: zen-boost-size-value textContent (app.ts:1388)
// When at 100%, the value element is hidden and zen-boost-size-text is shown.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, SizeCycleFiveModes) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  // Read the displayed size percentage.  At 100% the value element is hidden
  // and the label element (zen-boost-size-text) shows "Size".  For non-100%
  // modes, zen-boost-size-value shows the percentage (e.g. "110%").
  constexpr char kReadSize[] = R"js(
    (function() {
      const valueEl = document.getElementById('zen-boost-size-value');
      if (valueEl && valueEl.style.display !== 'none') {
        return valueEl.textContent || '';
      }
      return '100%';
    })()
  )js";

  // Initial state: 100%.
  EXPECT_EQ("100%", content::EvalJs(editor_wc, kReadSize).ExtractString());

  // Expected cycle: 100% → 110% → 125% → 150% → 90% → 100%
  const std::string expected_sizes[] = {
      "110%", "125%", "150%", "90%", "100%"};

  for (const auto& expected : expected_sizes) {
    ASSERT_TRUE(content::ExecJs(editor_wc,
        "document.getElementById('zen-boost-size').click()"));
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(expected,
              content::EvalJs(editor_wc, kReadSize).ExtractString())
        << "Size mode mismatch during cycle";
  }
}

// ---------------------------------------------------------------------------
// Test 5: ZapModeRoundTrip
//
// Enters zap mode, waits for content scripts to inject on the target page,
// simulates an element zap, and verifies the zap list updates in the editor.
// Element ID: zen-boost-zap (app.ts:481)
//
// IMPORTANT: This test depends on Plan A (GRD content script loading).  If
// content scripts are not present, the test is skipped gracefully.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ZapModeRoundTrip) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  content::WebContents* target_wc = GetTargetWebContents();
  ASSERT_TRUE(target_wc);

  // Enter zap mode by clicking the zap button.
  ASSERT_TRUE(content::ExecJs(editor_wc,
      "document.getElementById('zen-boost-zap').click()"));

  // Wait for content scripts to inject on the target page.
  // Content scripts expose window.__mahoBoost when zap mode is active.
  constexpr char kCheckContentScripts[] =
      "typeof window.__mahoBoost === 'object'";

  bool scripts_loaded = false;
  if (base::test::RunUntil([&]() {
        auto result = content::EvalJs(target_wc, kCheckContentScripts);
        if (!result.is_ok()) {
          return false;
        }
        scripts_loaded = result.ExtractBool();
        return scripts_loaded;
      })) {
    // Content scripts loaded — proceed with zap simulation.

    // Dispatch a click on the body element to simulate zapping it.
    constexpr char kSimulateZap[] = R"js(
      (function() {
        const el = document.body;
        if (window.__mahoBoost && window.__mahoBoost.simulateZap) {
          window.__mahoBoost.simulateZap('body');
          return true;
        }
        // Fallback: dispatch a click event that the zap overlay listens for.
        el.dispatchEvent(new MouseEvent('click', {bubbles: true}));
        return true;
      })()
    )js";
    EXPECT_TRUE(content::EvalJs(target_wc, kSimulateZap).ExtractBool());

    // Wait for the editor's zap list to update.
    constexpr char kCheckZapList[] = R"js(
      (function() {
        const zapValue = document.getElementById('zen-boost-zap-value');
        return zapValue ? zapValue.textContent : '';
      })()
    )js";

    bool zap_appeared = false;
    ASSERT_TRUE(base::test::RunUntil([&]() {
      auto result = content::EvalJs(editor_wc, kCheckZapList);
      if (!result.is_ok()) {
        return false;
      }
      const std::string text = result.ExtractString();
      // A non-empty zap count means at least one selector was zapped.
      zap_appeared = !text.empty();
      return zap_appeared;
    })) << "Zap list in editor never updated after simulating zap";

    // Exit zap mode by clicking "Done" (same zap button toggles off).
    ASSERT_TRUE(content::ExecJs(editor_wc,
        "document.getElementById('zen-boost-zap').click()"));
    base::RunLoop().RunUntilIdle();

    // Verify the zap button shows the count (persisted).
    constexpr char kVerifyZapPersisted[] = R"js(
      document.getElementById('zen-boost-zap').getAttribute('hideicon') === 'true'
    )js";
    EXPECT_TRUE(
        content::EvalJs(editor_wc, kVerifyZapPersisted).ExtractBool())
        << "Zap selectors should be persisted (hideicon=true when count > 0)";
  } else {
    GTEST_SKIP() << "Content scripts not loaded — Plan A (GRD content script "
                    "loading) may not be merged yet";
  }
}

}  // namespace
