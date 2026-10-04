// Copyright 2026 Maho Browser. All rights reserved.

#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "ui/views/widget/widget.h"

namespace {

class MahoBoostCodeMirrorTest : public MahoBoostInteractiveUiTest {};

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: CodeModeResize
//
// Verifies that opening the boost editor creates a widget with the default
// boost-mode width (~184px), and switching to code mode resizes it to ~452px.
// Tolerance of ±20px accommodates window frame / chrome.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostCodeMirrorTest, CodeModeResize) {
  NavigateTo("/title1.html");

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

  EXPECT_EQ(maho::kCodeModeWidth, widget->GetWindowBoundsInScreen().width());
}

// ---------------------------------------------------------------------------
// Test 2: CssPersistenceRoundTrip
//
// Sets CodeMirror 6 content to "body { color: red; }", closes the editor
// (triggering a commit), reopens it, switches back to code mode, and verifies
// the CSS persisted.
//
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostCodeMirrorTest, CssPersistenceRoundTrip) {
  NavigateTo("/title1.html");

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

  constexpr char kCss[] = "body { color: rgb(1, 2, 3); }";
  ASSERT_TRUE(WriteCodeMirrorCss(editor_wc, kCss));
  ASSERT_TRUE(FlushCodeMirror(editor_wc));
  ASSERT_TRUE(WaitForCoreBoostCss("rgb(1, 2, 3)"));
  ASSERT_TRUE(WaitForTargetStyle("rgb(1, 2, 3)"));

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

  const std::optional<std::string> persisted_css = ReadCodeMirrorCss(editor_wc2);
  ASSERT_TRUE(persisted_css);
  EXPECT_EQ(kCss, *persisted_css);
}

// ---------------------------------------------------------------------------
// Test 3: CodeMirrorMounted
//
// Switches to code mode and verifies that the CodeMirror 6 editor properly
// mounts its DOM elements (.cm-editor and .cm-content).
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostCodeMirrorTest, CodeMirrorMounted) {
  NavigateTo("/title1.html");

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  content::WebContents* editor_wc = WaitForEditorLoad();
  ASSERT_TRUE(editor_wc) << "Boost editor did not load in time";

  ASSERT_TRUE(SwitchToCodeMode(editor_wc))
      << "Failed to switch to code mode";

  // Wait for .cm-editor to appear (CM6 async init, 5s default timeout).
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_wc))
      << "CodeMirror .cm-editor did not mount within 5s timeout";

  ASSERT_TRUE(WaitForCondition(
      "CodeMirror test seam to schedule a measure and focus operation",
      [editor_wc]() {
        const auto result = content::EvalJs(
            editor_wc,
            "window.__mahoBoostTest?.measure() && "
            "window.__mahoBoostTest?.focus() && "
            "window.__mahoBoostTest?.mounted()" );
        return result.is_ok() && result.ExtractBool();
      }));
  ASSERT_TRUE(FlushCodeMirror(editor_wc));
  ASSERT_TRUE(WaitForCondition(
      "CodeMirror test seam to report editor focus",
      [editor_wc]() {
        const auto result = content::EvalJs(
            editor_wc,
            "document.getElementById('custom-css-editor')?.contains("
            "document.activeElement) ?? false");
        return result.is_ok() && result.ExtractBool();
      }));
}
