// Copyright 2026 Maho Browser. All rights reserved.

#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/browser/web_contents_observer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/accessibility/platform/ax_platform_node_base.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

class FocusObserver : public content::WebContentsObserver {
 public:
  explicit FocusObserver(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents) {}

  void OnWebContentsFocused(content::RenderWidgetHost*) override {
    focused_ = true;
  }

  bool focused() const { return focused_; }

 private:
  bool focused_ = false;
};

class MahoBoostLifecycleTest : public MahoBoostInteractiveUiTest {};

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: CreateTempDiscardNoPersistence
//
// Opens the boost editor (creates a temp boost), hides without committing,
// then reopens for the same domain and verifies no prior changes persisted.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       CreateTempDiscardNoPersistence) {
  NavigateTo("/title1.html");

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  const std::optional<std::string> initial_css =
      ReadCodeMirrorCss(editor_web_contents);
  ASSERT_TRUE(initial_css);
  EXPECT_TRUE(initial_css->empty());

  base::WeakPtr<views::Widget> widget =
      GetController().GetWidgetForTesting()->GetWeakPtr();
  HideBoostEditor();
  EXPECT_FALSE(GetController().IsShowing());
  ASSERT_TRUE(WaitUntilDestroyed());
  EXPECT_FALSE(widget);
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  content::WebContents* reopened_editor = WaitForEditorLoad();
  ASSERT_TRUE(reopened_editor);
  ASSERT_TRUE(SwitchToCodeMode(reopened_editor));
  ASSERT_TRUE(WaitForCodeMirrorMount(reopened_editor));
  const std::optional<std::string> reopened_css =
      ReadCodeMirrorCss(reopened_editor);
  ASSERT_TRUE(reopened_css);
  EXPECT_TRUE(reopened_css->empty());
}

// ---------------------------------------------------------------------------
// Test 2: CreateTempCommitPersistence
//
// Opens the boost editor, makes a change (magic-theme toggle), closes the
// editor (triggering a commit via widget destruction), then reopens and
// verifies the change persisted.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       CreateTempCommitPersistence) {
  NavigateTo("/title1.html");

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  constexpr char kCommittedCss[] = "body { border: 3px solid rgb(7, 8, 9); }";
  ASSERT_TRUE(WriteCodeMirrorCss(editor_web_contents, kCommittedCss));
  ASSERT_TRUE(FlushCodeMirror(editor_web_contents));
  ASSERT_TRUE(WaitForCoreBoostCss("rgb(7, 8, 9)"));

  // Widget::Close() mirrors clicking the window X button and synchronously
  // routes through the controller's Hide() close path.
  base::WeakPtr<views::Widget> widget =
      GetController().GetWidgetForTesting()->GetWeakPtr();
  ASSERT_TRUE(widget);
  widget->Close();

  EXPECT_FALSE(GetController().IsShowing());
  ASSERT_TRUE(WaitUntilDestroyed());
  EXPECT_FALSE(widget);
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  content::WebContents* reopened_editor = WaitForEditorLoad();
  ASSERT_TRUE(reopened_editor);
  ASSERT_TRUE(SwitchToCodeMode(reopened_editor));
  ASSERT_TRUE(WaitForCodeMirrorMount(reopened_editor));
  const std::optional<std::string> reopened_css =
      ReadCodeMirrorCss(reopened_editor);
  ASSERT_TRUE(reopened_css);
  EXPECT_EQ(kCommittedCss, *reopened_css);
}

// ---------------------------------------------------------------------------
// Test 3: TabSwitchClosesEditor
//
// Opens the boost editor for Tab 0, adds a second tab and activates it,
// then asserts the editor auto-closed.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, TabSwitchClosesEditor) {
  NavigateTo("/title1.html");

  // Creating a tab produces browser-owned AX nodes. Add and settle it before
  // scoping the counter to the Boost widget so this test measures only editor
  // cleanup, rather than expecting the browser-wide tree to be unchanged.
  const int new_tab_index = browser()->GetTabStripModel()->count();
  chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                   /*foreground=*/false);
  ASSERT_EQ(new_tab_index + 1, browser()->GetTabStripModel()->count());
  base::RunLoop().RunUntilIdle();
  ui::AXPlatformNodeBase::ResetInstanceCountForTesting();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());
  ASSERT_TRUE(WaitForCondition(
      "Boost editor to create scoped AX nodes", []() {
        return ui::AXPlatformNodeBase::GetInstanceCount() > 0;
      }));

  browser()->GetTabStripModel()->ActivateTabAt(new_tab_index);

  ASSERT_TRUE(WaitUntilHidden())
      << "Boost editor must close when the active tab changes";
  EXPECT_FALSE(GetController().IsShowing());
  EXPECT_EQ(nullptr, GetController().GetWidgetForTesting());
  EXPECT_TRUE(WaitForCondition(
      "scoped Boost editor AX nodes to be destroyed", []() {
        return ui::AXPlatformNodeBase::GetInstanceCount() == 0;
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       TabSwitchDiscardsUntouchedTemporaryBoostBeforeClose) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  const auto selected_id = content::EvalJs(
      editor_web_contents,
      "document.querySelector('.boost-shell')?.dataset.selectedBoostId ?? ''");
  ASSERT_TRUE(selected_id.is_ok());
  const std::string temporary_boost_id = selected_id.ExtractString();
  ASSERT_FALSE(temporary_boost_id.empty());

  const int new_tab_index = browser()->GetTabStripModel()->count();
  chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                   /*foreground=*/false);
  browser()->GetTabStripModel()->ActivateTabAt(new_tab_index);

  ASSERT_TRUE(WaitForCondition("host cleanup and widget destruction",
                               [this]() { return !GetController().HasWidget(); }));
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  char* stale_boost = maho_core_boost_get(core, temporary_boost_id.c_str());
  EXPECT_EQ(nullptr, stale_boost);
  if (stale_boost) {
    maho_string_free(stale_boost);
  }
}

IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       OpeningForBackgroundTabActivatesAndFocusesThatTab) {
  NavigateTo("/title1.html");
  content::WebContents* original_tab = GetTargetWebContents();
  ASSERT_TRUE(original_tab);

  const int background_index = browser()->GetTabStripModel()->count();
  chrome::AddTabAt(
      browser(), embedded_test_server()->GetURL("b.com", "/title2.html"),
      /*index=*/-1, /*foreground=*/false);
  content::WebContents* background_tab =
      browser()->GetTabStripModel()->GetWebContentsAt(background_index);
  ASSERT_TRUE(background_tab);
  ASSERT_TRUE(content::NavigateToURL(
      background_tab,
      embedded_test_server()->GetURL("b.com", "/title2.html")));
  ASSERT_EQ(original_tab,
            browser()->GetTabStripModel()->GetActiveWebContents());

  FocusObserver focus_observer(background_tab);

  GetController().ShowForActiveDomain(background_tab);

  EXPECT_EQ(background_tab,
            browser()->GetTabStripModel()->GetActiveWebContents());
  EXPECT_EQ(background_tab,
            GetController().GetTargetWebContentsForTesting());
  EXPECT_TRUE(WaitForCondition("background Boost target receives focus",
                               [&focus_observer]() {
                                 return focus_observer.focused();
                               }));
  ASSERT_TRUE(WaitUntilShowing());
}

// ---------------------------------------------------------------------------
// Test 4: NavigationClosesEditor
//
// Opens the boost editor for a.com, navigates the active tab to a different
// domain (b.com), then asserts the editor auto-closed.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       HostCloseWatchdogDiscardsUntouchedTemporaryBoost) {
  NavigateTo("/title1.html");
  content::WebContents* editor = OpenEditorAndWait();
  ASSERT_TRUE(editor);
  const auto selected = content::EvalJs(
      editor,
      "document.querySelector('.boost-shell')?.dataset.selectedBoostId ?? ''");
  ASSERT_TRUE(selected.is_ok());
  const std::string boost_id = selected.ExtractString();
  ASSERT_FALSE(boost_id.empty());

  GetController().SetEditorKilledCallback(base::DoNothing());
  GetController().Hide();
  ASSERT_TRUE(GetController().IsHostClosePendingForTesting());
  GetController().FireHostCloseWatchdogForTesting();
  ASSERT_TRUE(WaitUntilDestroyed());

  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  char* boost = maho_core_boost_get(core, boost_id.c_str());
  EXPECT_EQ(nullptr, boost);
  if (boost) {
    maho_string_free(boost);
  }
}

IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       TwoWindowsKeepControllerTargetsIsolated) {
  NavigateTo("/title1.html");
  content::WebContents* first_target = GetTargetWebContents();
  ASSERT_TRUE(first_target);

  Browser* second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(second_browser);
  chrome::AddTabAt(second_browser,
                   embedded_test_server()->GetURL("b.com", "/title2.html"),
                   /*index=*/-1, /*foreground=*/true);
  content::WebContents* second_target =
      second_browser->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(second_target);
  ASSERT_TRUE(content::NavigateToURL(
      second_target,
      embedded_test_server()->GetURL("b.com", "/title2.html")));

  auto& first = GetController();
  auto& second = maho::MahoBoostWindowController::GetForBrowser(
      second_browser, second_browser->GetProfile());
  first.ShowForActiveDomain(first_target);
  second.ShowForActiveDomain(second_target);

  ASSERT_TRUE(WaitForCondition("both Boost editors mount", [&]() {
    content::WebContents* first_editor = first.GetEditorWebContentsForTesting();
    content::WebContents* second_editor = second.GetEditorWebContentsForTesting();
    return first_editor && second_editor &&
           content::EvalJs(first_editor,
                           "Boolean(document.querySelector('.boost-shell'))")
               .ExtractBool() &&
           content::EvalJs(second_editor,
                           "Boolean(document.querySelector('.boost-shell'))")
               .ExtractBool();
  }));
  EXPECT_EQ(first_target, first.GetTargetWebContentsForTesting());
  EXPECT_EQ(second_target, second.GetTargetWebContentsForTesting());
}

IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       HostCloseWatchdogCommitsDirtyTemporaryBoost) {
  NavigateTo("/title1.html");
  content::WebContents* editor = OpenEditorAndWait();
  ASSERT_TRUE(editor);
  const auto selected = content::EvalJs(
      editor,
      "document.querySelector('.boost-shell')?.dataset.selectedBoostId ?? ''");
  ASSERT_TRUE(selected.is_ok());
  const std::string boost_id = selected.ExtractString();
  ASSERT_FALSE(boost_id.empty());
  ASSERT_TRUE(SwitchToCodeMode(editor));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor));
  ASSERT_TRUE(WriteCodeMirrorCss(editor, "body { outline: 2px solid red; }"));
  ASSERT_TRUE(FlushCodeMirror(editor));
  ASSERT_TRUE(WaitForCoreBoostCss("outline: 2px solid red"));

  GetController().SetEditorKilledCallback(base::DoNothing());
  GetController().Hide();
  GetController().FireHostCloseWatchdogForTesting();
  ASSERT_TRUE(WaitUntilDestroyed());

  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  char* boost = maho_core_boost_get(core, boost_id.c_str());
  ASSERT_NE(nullptr, boost);
  const std::string boost_json(boost);
  maho_string_free(boost);
  EXPECT_NE(std::string::npos,
            boost_json.find("outline: 2px solid red"));
}

IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, NavigationClosesEditor) {
  NavigateTo("/title1.html");

  const size_t ax_nodes_before = ui::AXPlatformNodeBase::GetInstanceCount();
  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  ASSERT_TRUE(WaitUntilHidden())
      << "Boost editor must close when navigating to a different domain";
  ASSERT_TRUE(WaitUntilDestroyed());
  EXPECT_FALSE(GetController().IsShowing());
  EXPECT_EQ(nullptr, GetController().GetWidgetForTesting());
  EXPECT_EQ(ax_nodes_before, ui::AXPlatformNodeBase::GetInstanceCount());
}

// ---------------------------------------------------------------------------
// Test 5: SameDomainReshowReuses
//
// Opens the boost editor, hides it, re-shows for the same domain, and
// verifies the editor is showing again.  Optionally verifies no extra
// boost editor browser was created.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, SameDomainReshowReuses) {
  NavigateTo("/title1.html");

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  HideBoostEditor();
  ASSERT_TRUE(WaitUntilHidden());
  EXPECT_FALSE(GetController().IsShowing());

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());
  EXPECT_TRUE(GetController().IsShowing());
}
