// Copyright 2026 Maho Browser. All rights reserved.

#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

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
  // The first child of the contents view is the WebView hosting the WebUI.
  auto* web_view = views::AsViewClass<views::WebView>(contents->children()[0]);
  if (!web_view) {
    return nullptr;
  }
  return web_view->web_contents();
}

class MahoBoostDomEquivalenceTest : public InProcessBrowserTest {
 public:
  MahoBoostDomEquivalenceTest() = default;
  ~MahoBoostDomEquivalenceTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoBoostDomEquivalenceTest, ZenMandatedIdsPresent) {
  // 1–2. Navigate to a test page.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  // 3–4. Get the boost controller and show the editor.
  auto& controller = maho::MahoBoostWindowController::GetForBrowser(
      browser(), browser()->profile());
  controller.ShowForActiveDomain(
      browser()->tab_strip_model()->GetActiveWebContents());

  // 5. Wait for the boost editor's WebContents to finish loading (10s timeout).
  views::Widget* widget = controller.GetWidgetForTesting();
  ASSERT_TRUE(widget) << "Boost widget was not created";

  content::WebContents* editor_wc = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    editor_wc = GetBoostEditorWebContents(widget);
    return editor_wc &&
           editor_wc->IsDocumentOnLoadCompletedInPrimaryMainFrame();
  })) << "Boost editor WebContents did not finish loading within timeout";

  // 6. Sanity-check we have the right WebContents.
  ASSERT_TRUE(editor_wc);
  EXPECT_TRUE(editor_wc->GetLastCommittedURL().SchemeIs("chrome"));
  SCOPED_TRACE("Editor URL: " + editor_wc->GetLastCommittedURL().spec());

  // 7. Execute JS to extract all IDs and classes from the DOM.
  const std::string js = R"(
    (function() {
      const ids = [...document.querySelectorAll('[id]')].map(e => e.id);
      const classes = [...document.querySelectorAll('[class]')]
        .flatMap(e => [...e.classList]);
      return JSON.stringify({
        ids: [...new Set(ids)],
        classes: [...new Set(classes)]
      });
    })()
  )";

  content::EvalJsResult eval_result = content::EvalJs(editor_wc, js);
  ASSERT_TRUE(eval_result.is_ok())
      << "JS evaluation failed: " << eval_result.ExtractError();

  // 8. Parse the JSON result.
  std::string json_str = eval_result.ExtractString();
  SCOPED_TRACE("Raw DOM dump: " + json_str);

  std::optional<base::Value> parsed = base::JSONReader::Read(json_str, 0);
  ASSERT_TRUE(parsed.has_value()) << "Failed to parse JSON: " << json_str;
  ASSERT_TRUE(parsed->is_dict()) << "JSON root is not a dict: " << json_str;

  const base::DictValue& root = parsed->GetDict();
  const base::ListValue* ids_list = root.FindList("ids");
  const base::ListValue* classes_list = root.FindList("classes");
  ASSERT_TRUE(ids_list) << "Missing 'ids' array in DOM dump";
  ASSERT_TRUE(classes_list) << "Missing 'classes' array in DOM dump";

  std::set<std::string> ids;
  for (const auto& v : *ids_list) {
    if (v.is_string()) {
      ids.insert(v.GetString());
    }
  }
  std::set<std::string> classes;
  for (const auto& v : *classes_list) {
    if (v.is_string()) {
      classes.insert(v.GetString());
    }
  }

  // 9. Assert Zen-mandated DOM IDs are present.
  //    On failure, the SCOPED_TRACE above dumps the full DOM JSON for
  //    diagnosing mismatches.
  const std::vector<std::string> required_ids = {
      "boost-editor",
      "color-section",
      "typography-section",
      "zap-section",
      "dot-primary",
      "dot-secondary",
      "color-wheel",
      "font-grid",
      "case-cycle-btn",
      "size-cycle-btn",
      "code-editor-container",
      "custom-css-editor",
      "advanced-color-toggle",
      "advanced-color-popup",
      "magic-theme-toggle",
      "smart-invert-toggle",
  };

  for (const auto& id : required_ids) {
    EXPECT_TRUE(ids.contains(id))
        << "Zen-mandated DOM id '" << id
        << "' not found in editor DOM. Present IDs: " << json_str;
  }

  // 10. Assert structural classes.
  EXPECT_TRUE(classes.contains("boost-mode") || classes.contains("code-mode"))
      << "Neither 'boost-mode' nor 'code-mode' class found. "
      << "Present classes (see SCOPED_TRACE above for full dump)";

  const std::vector<std::string> required_classes = {
      "color-wheel-container",
      "font-button",
      "zap-controls",
  };

  for (const auto& cls : required_classes) {
    EXPECT_TRUE(classes.contains(cls))
        << "Zen-mandated class '" << cls
        << "' not found in editor DOM. Present classes: " << json_str;
  }
}
