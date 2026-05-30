// Copyright 2026 Maho Browser. All rights reserved.

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

// Retrieve the WebContents hosting chrome://maho-boost from the boost widget.
// The widget's contents view is a MahoBoostWindowView whose first child is the
// views::WebView that loaded the Boost WebUI.
content::WebContents* GetBoostEditorWebContents(views::Widget* widget) {
  if (!widget) {
    return nullptr;
  }
  views::View* contents = widget->GetContentsView();
  if (!contents || contents->children().empty()) {
    return nullptr;
  }
  auto* web_view = views::AsViewClass<views::WebView>(contents->children()[0]);
  if (!web_view) {
    return nullptr;
  }
  return web_view->web_contents();
}

class MahoBoostCodeMirrorTest : public InProcessBrowserTest {
 public:
  MahoBoostCodeMirrorTest() = default;
  ~MahoBoostCodeMirrorTest() override = default;

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  maho::MahoBoostWindowController& GetController() {
    return maho::MahoBoostWindowController::GetForBrowser(
        browser(), browser()->profile());
  }

  content::WebContents* GetActiveTab() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  void ShowBoostEditor() {
    GetController().ShowForActiveDomain(GetActiveTab());
  }

  bool WaitUntilShowing() {
    return base::test::RunUntil([&]() { return GetController().IsShowing(); });
  }

  bool WaitUntilHidden() {
    return base::test::RunUntil(
        [&]() { return !GetController().IsShowing(); });
  }

  // Wait for the boost editor's WebContents to finish document load.
  // Returns the WebContents on success, nullptr on timeout.
  content::WebContents* WaitForEditorLoad() {
    views::Widget* widget = GetController().GetWidgetForTesting();
    if (!widget) {
      return nullptr;
    }

    content::WebContents* editor_wc = nullptr;
    bool loaded = base::test::RunUntil([&]() {
      editor_wc = GetBoostEditorWebContents(widget);
      return editor_wc &&
             editor_wc->IsDocumentOnLoadCompletedInPrimaryMainFrame();
    });

    return loaded ? editor_wc : nullptr;
  }

  // Switch the editor to code mode by clicking #zen-boost-code and wait for
  // the mode resize to take effect.
  //
  // The code mode button is `#zen-boost-code` (see app.ts line 655:
  //   this.dom.codeButton.addEventListener('click', () => {
  //     void this.onCodeButtonPressed();
  //   });
  // which calls updateViewMode(WindowMode.kCode) and
  // pageHandler.requestModeResize(WindowMode.kCode)).
  bool SwitchToCodeMode(content::WebContents* editor_wc) {
    auto result = content::EvalJs(editor_wc, R"(
      (function() {
        const btn = document.getElementById('zen-boost-code');
        if (!btn) return false;
        btn.click();
        return true;
      })()
    )");
    if (!result.is_ok() || !result.ExtractBool()) {
      return false;
    }

    // Wait for the widget to resize to code mode width (~452px).
    views::Widget* widget = GetController().GetWidgetForTesting();
    if (!widget) {
      return false;
    }
    return base::test::RunUntil([&]() {
      int width = widget->GetWindowBoundsInScreen().width();
      return width >= 432 && width <= 472;
    });
  }

  // Wait for CodeMirror 6 `.cm-editor` element to appear in the DOM.
  // CM6 initializes asynchronously; this polls with the default RunUntil
  // timeout (~5s).
  bool WaitForCodeMirrorMount(content::WebContents* editor_wc) {
    return base::test::RunUntil([&]() {
      auto result = content::EvalJs(editor_wc,
          "document.querySelector('.cm-editor') !== null");
      return result.is_ok() && result.ExtractBool();
    });
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: CodeModeResize
//
// Verifies that opening the boost editor creates a widget with the default
// boost-mode width (~184px), and switching to code mode resizes it to ~452px.
// Tolerance of ±20px accommodates window frame / chrome.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostCodeMirrorTest, CodeModeResize) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  // Wait for the editor to load.
  content::WebContents* editor_wc = WaitForEditorLoad();
  ASSERT_TRUE(editor_wc) << "Boost editor did not load in time";

  // Verify initial widget width is approximately kBoostModeWidth (184).
  views::Widget* widget = GetController().GetWidgetForTesting();
  ASSERT_TRUE(widget);
  {
    int initial_width = widget->GetWindowBoundsInScreen().width();
    EXPECT_GE(initial_width, 164)
        << "Initial width " << initial_width
        << " is too narrow (expected ~184 ±20)";
    EXPECT_LE(initial_width, 204)
        << "Initial width " << initial_width
        << " is too wide (expected ~184 ±20)";
  }

  // Switch to code mode by clicking #zen-boost-code.
  ASSERT_TRUE(SwitchToCodeMode(editor_wc))
      << "Failed to switch to code mode or widget did not resize";

  // Assert final code-mode width is approximately kCodeModeWidth (452).
  {
    int code_width = widget->GetWindowBoundsInScreen().width();
    EXPECT_GE(code_width, 432)
        << "Code mode width " << code_width
        << " is too narrow (expected ~452 ±20)";
    EXPECT_LE(code_width, 472)
        << "Code mode width " << code_width
        << " is too wide (expected ~452 ±20)";
  }
}

// ---------------------------------------------------------------------------
// Test 2: CssPersistenceRoundTrip
//
// Sets CodeMirror 6 content to "body { color: red; }", closes the editor
// (triggering a commit), reopens it, switches back to code mode, and verifies
// the CSS persisted.
//
// FRAGILITY NOTE: This test accesses the CM6 internal API via the private
// `codeMirrorView` field stored on the app instance.  The test uses a
// workaround: since the app stores the CM6 EditorView in a closure-scoped
// variable, we access CM6 content via the DOM's `.cm-content` element
// (which is the contenteditable container) as a robust fallback.
//
// The primary approach attempts `.cm-editor`'s internal `cmView.view` property
// (a CM6 internal), which may break if CM6 internalizes further.  If this
// path fails, the test falls back to reading `.cm-content` textContent which
// is always available but less precise (may include extra whitespace/newlines).
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostCodeMirrorTest, CssPersistenceRoundTrip) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  // Open editor and switch to code mode.
  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  content::WebContents* editor_wc = WaitForEditorLoad();
  ASSERT_TRUE(editor_wc) << "Boost editor did not load in time";

  ASSERT_TRUE(SwitchToCodeMode(editor_wc))
      << "Failed to switch to code mode";

  // Wait for CodeMirror to mount.
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_wc))
      << "CodeMirror .cm-editor did not mount within timeout";

  // Set CM6 content via JavaScript.
  // Primary path: access CM6 view via `.cm-editor` element's `cmView.view`
  // internal property.  This is fragile — cmView is a CM6 internal that may
  // change in future versions.  If not accessible, fall back to dispatching
  // input events (documented below as a comment, not implemented as primary
  // due to unreliability of synthetic contenteditable input).
  {
    auto set_result = content::EvalJs(editor_wc, R"(
      (async function() {
        const editorEl = document.querySelector('.cm-editor');
        if (!editorEl) return 'no-editor';

        // Primary: CM6 internal cmView accessor.
        // cmView is an internal property set by CM6 on the DOM element.
        // It provides access to the EditorView instance.
        if (editorEl.cmView && editorEl.cmView.view) {
          const view = editorEl.cmView.view;
          view.dispatch({
            changes: {
              from: 0,
              to: view.state.doc.length,
              insert: 'body { color: red; }'
            }
          });
          return 'ok-cmView';
        }

        // Fallback: focus the .cm-content and use execCommand.
        // This is less reliable but does not depend on CM6 internals.
        // NOTE: If both paths fail, this test will fail and needs
        // investigation — possibly content::SimulateKeyPress typing.
        const content = editorEl.querySelector('.cm-content');
        if (content) {
          content.focus();
          content.textContent = '';
          document.execCommand('insertText', false, 'body { color: red; }');
          return 'ok-fallback';
        }

        return 'no-content';
      })()
    )");

    std::string set_status = set_result.ExtractString();
    ASSERT_TRUE(set_status == "ok-cmView" || set_status == "ok-fallback")
        << "Failed to set CM6 content, status: " << set_status;
    SCOPED_TRACE("CM6 content set via: " + set_status);
  }

  // Small delay to allow the debounced update to fire (150ms debounce in
  // app.ts GENERAL_UPDATE_DEBOUNCE_MS) and commit the custom CSS via Mojo.
  base::RunLoop run_loop;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(300));
  run_loop.Run();

  // Close editor — triggers commit via widget destruction / unload lifecycle.
  views::Widget* widget = GetController().GetWidgetForTesting();
  ASSERT_TRUE(widget);
  widget->Close();
  ASSERT_TRUE(WaitUntilHidden());

  // Reopen editor for the same domain.
  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  content::WebContents* editor_wc2 = WaitForEditorLoad();
  ASSERT_TRUE(editor_wc2) << "Boost editor did not reload in time";

  ASSERT_TRUE(SwitchToCodeMode(editor_wc2))
      << "Failed to switch to code mode on reopen";
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_wc2))
      << "CodeMirror did not mount on reopen";

  // Read CM6 content back.
  // FRAGILITY NOTE: `.cm-editor.cmView.view.state.doc.toString()` is a CM6
  // internal API.  If CM6 changes its internal structure, this accessor will
  // break.  The fallback reads `.cm-content` textContent instead.
  {
    auto read_result = content::EvalJs(editor_wc2, R"(
      (function() {
        const editorEl = document.querySelector('.cm-editor');
        if (!editorEl) return '';

        // Primary: CM6 internal state access.
        if (editorEl.cmView && editorEl.cmView.view) {
          return editorEl.cmView.view.state.doc.toString();
        }

        // Fallback: read .cm-content textContent (may have extra whitespace).
        const content = editorEl.querySelector('.cm-content');
        return content ? content.textContent.trim() : '';
      })()
    )");

    ASSERT_TRUE(read_result.is_ok())
        << "Failed to read CM6 content: " << read_result.ExtractError();

    std::string persisted_css = read_result.ExtractString();
    EXPECT_EQ(persisted_css, "body { color: red; }")
        << "Custom CSS did not persist across editor close/reopen. Got: '"
        << persisted_css << "'";
  }
}

// ---------------------------------------------------------------------------
// Test 3: CodeMirrorMounted
//
// Switches to code mode and verifies that the CodeMirror 6 editor properly
// mounts its DOM elements (.cm-editor and .cm-content).
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostCodeMirrorTest, CodeMirrorMounted) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  content::WebContents* editor_wc = WaitForEditorLoad();
  ASSERT_TRUE(editor_wc) << "Boost editor did not load in time";

  ASSERT_TRUE(SwitchToCodeMode(editor_wc))
      << "Failed to switch to code mode";

  // Wait for .cm-editor to appear (CM6 async init, 5s default timeout).
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_wc))
      << "CodeMirror .cm-editor did not mount within 5s timeout";

  // Assert .cm-editor is present.
  {
    auto result = content::EvalJs(editor_wc,
        "document.querySelector('.cm-editor') !== null");
    ASSERT_TRUE(result.is_ok()) << result.ExtractError();
    EXPECT_TRUE(result.ExtractBool())
        << ".cm-editor element not found in boost editor DOM";
  }

  // Assert .cm-content (the editable area) is present.
  {
    auto result = content::EvalJs(editor_wc,
        "document.querySelector('.cm-content') !== null");
    ASSERT_TRUE(result.is_ok()) << result.ExtractError();
    EXPECT_TRUE(result.ExtractBool())
        << ".cm-content element not found — CodeMirror may not have "
           "initialized its editable container";
  }
}
