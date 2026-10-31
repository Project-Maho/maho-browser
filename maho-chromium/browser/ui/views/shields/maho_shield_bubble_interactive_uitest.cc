// Copyright 2026 Maho Browser. All rights reserved.

#include <set>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/site_control/maho_page_info_ui.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_coordinator.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace {

bool HasLabelWithText(views::View* root, std::u16string_view expected) {
  if (!root) {
    return false;
  }
  if (auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText() == expected) {
    return true;
  }
  for (views::View* child : root->children()) {
    if (HasLabelWithText(child, expected)) {
      return true;
    }
  }
  return false;
}

class FocusRecorder : public views::FocusChangeListener {
 public:
  explicit FocusRecorder(views::FocusManager* fm) : fm_(fm) {
    fm_->AddFocusChangeListener(this);
  }
  ~FocusRecorder() override { fm_->RemoveFocusChangeListener(this); }

  void OnWillChangeFocus(views::View* focused_before,
                         views::View* focused_now) override {}
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override {
    ever_focused_.insert(focused_now);
  }

  bool EverFocused(views::View* view) const {
    return ever_focused_.count(view) > 0;
  }

 private:
  raw_ptr<views::FocusManager> fm_;
  std::set<raw_ptr<views::View>> ever_focused_;
};

class MahoShieldBubbleInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoShieldBubbleInteractiveUiTest() = default;
  MahoShieldBubbleInteractiveUiTest(const MahoShieldBubbleInteractiveUiTest&) =
      delete;
  MahoShieldBubbleInteractiveUiTest& operator=(
      const MahoShieldBubbleInteractiveUiTest&) = delete;
  ~MahoShieldBubbleInteractiveUiTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();

    browser()->GetProfile()->GetPrefs()->SetBoolean(
        maho::sidebar_prefs::kSidebarLayoutEnabled, false);
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    ASSERT_TRUE(browser_view);
    browser_view->InvalidateLayout();
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    if (!browser()->GetTabStripModel()->GetActiveWebContents()) {
      chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
    }
  }

  void TearDownOnMainThread() override {
    if (browser()) {
      maho::MahoShieldBubbleCoordinator::GetForBrowser(static_cast<Browser*>(browser())).Hide();
      browser()->GetTabStripModel()->CloseAllTabs();
      base::RunLoop().RunUntilIdle();
    }
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  views::View* GetAnchorView() {
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    EXPECT_TRUE(browser_view);
    if (!browser_view) {
      return nullptr;
    }
    return browser_view->toolbar();
  }

  content::WebContents* GetActiveWebContents() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }

  std::string GetActiveOrigin() {
    return url::Origin::Create(GetActiveWebContents()->GetLastCommittedURL())
        .Serialize();
  }

  maho::MahoShieldBubbleCoordinator& GetCoordinator() {
    return maho::MahoShieldBubbleCoordinator::GetForBrowser(static_cast<Browser*>(browser()));
  }
};

class ScopedContentBlockingMode {
 public:
  ScopedContentBlockingMode()
      : saved_mode_(
            maho::MahoShieldBubbleView::GetContentBlockingModeForTesting()) {}

  ScopedContentBlockingMode(const ScopedContentBlockingMode&) = delete;
  ScopedContentBlockingMode& operator=(const ScopedContentBlockingMode&) =
      delete;

  ~ScopedContentBlockingMode() {
    maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(
        saved_mode_ == 3 ? 0 : saved_mode_);
  }

 private:
  int saved_mode_;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       PageInfoRouteShowsAndHidesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  maho::MahoShieldBubbleCoordinator& coordinator = GetCoordinator();
  EXPECT_FALSE(coordinator.IsShowing());

  MahoPageInfoUI::ShowShieldBubble(GetAnchorView(), static_cast<Browser*>(browser()),
                                   GetActiveOrigin());

  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));
  EXPECT_TRUE(coordinator.IsShowing());

  coordinator.Hide();

  ASSERT_TRUE(base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       PageInfoBrowserRouteShowsAndHidesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  maho::MahoShieldBubbleCoordinator& coordinator = GetCoordinator();
  ASSERT_FALSE(coordinator.IsShowing());

  MahoPageInfoUI::ShowShieldBubble(static_cast<Browser*>(browser()));

  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));
  EXPECT_TRUE(coordinator.IsShowing());

  coordinator.Hide();

  ASSERT_TRUE(base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       PageInfoBrowserRouteEscapeDismissesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  maho::MahoShieldBubbleCoordinator& coordinator = GetCoordinator();
  ASSERT_FALSE(coordinator.IsShowing());

  MahoPageInfoUI::ShowShieldBubble(static_cast<Browser*>(browser()));

  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));
  views::Widget* bubble_widget = coordinator.GetBubbleWidgetForTesting();
  ASSERT_TRUE(bubble_widget);
  ASSERT_TRUE(bubble_widget->IsVisible());

  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      bubble_widget->GetNativeWindow(), ui::VKEY_ESCAPE,
      /*control=*/false, /*shift=*/false, /*alt=*/false,
      /*command=*/false));

  ASSERT_TRUE(base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       ActiveTabChangeDismissesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoShieldBubbleCoordinator& coordinator = GetCoordinator();
  MahoPageInfoUI::ShowShieldBubble(GetAnchorView(), static_cast<Browser*>(browser()),
                                   GetActiveOrigin());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  ASSERT_TRUE(base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       HideBubbleRestoresFocusToAnchorView) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  base::RunLoop().RunUntilIdle();

  views::View* anchor = GetAnchorView();
  ASSERT_TRUE(anchor);
  anchor->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  anchor->RequestFocus();
  base::RunLoop().RunUntilIdle();

  maho::MahoShieldBubbleCoordinator& coordinator = GetCoordinator();
  MahoPageInfoUI::ShowShieldBubble(anchor, static_cast<Browser*>(browser()), GetActiveOrigin());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  views::FocusManager* fm = anchor->GetWidget()->GetFocusManager();
  ASSERT_TRUE(fm);
  FocusRecorder recorder(fm);

  coordinator.Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return recorder.EverFocused(anchor);
  })) << "Focus must be restored to anchor view after bubble dismiss";
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       NonNativeModesExposeNoToggleAndRejectLateMutation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ScopedContentBlockingMode scoped_mode;
  ASSERT_TRUE(maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(0));

  for (int mode : {1, 2}) {
    maho::MahoShieldBubbleView native_view(static_cast<Browser*>(browser()), GetActiveOrigin(), 0,
                                           false, GetActiveWebContents());
    ASSERT_TRUE(
        maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(mode));
    const std::string exceptions_before =
        maho::MahoShieldBubbleView::GetSiteExceptionsForTesting();

    native_view.ToggleForTesting();

    EXPECT_EQ(maho::MahoShieldBubbleView::GetSiteExceptionsForTesting(),
              exceptions_before);
    maho::MahoShieldBubbleView non_native_view(static_cast<Browser*>(browser()), GetActiveOrigin(), 0,
                                               false, GetActiveWebContents());
    EXPECT_EQ(non_native_view.GetToggleForTesting(), nullptr);
    ASSERT_TRUE(
        maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(0));
  }
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       NativeModeShowsBraveLikeShieldHierarchy) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ScopedContentBlockingMode scoped_mode;
  ASSERT_TRUE(maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(0));

  maho::MahoShieldBubbleView native_view(static_cast<Browser*>(browser()), GetActiveOrigin(), 0,
                                         false, GetActiveWebContents());

  EXPECT_FALSE(HasLabelWithText(&native_view, u"Maho Shield"));
  ASSERT_TRUE(native_view.GetSiteLabelForTesting());
  EXPECT_EQ(native_view.GetSiteLabelForTesting()->GetText(),
            base::UTF8ToUTF16(GURL(GetActiveOrigin()).host()));
  EXPECT_EQ(native_view.GetSiteLabelForTesting()->GetTextStyle(),
            views::style::STYLE_BODY_3_MEDIUM);
  ASSERT_TRUE(native_view.GetBlockedCountLabelForTesting());
  EXPECT_EQ(native_view.GetBlockedCountLabelForTesting()->GetText(), u"0");
  EXPECT_EQ(native_view.GetBlockedCountLabelForTesting()->GetTextStyle(),
            views::style::STYLE_HEADLINE_1);
  ASSERT_TRUE(native_view.GetBlockedCountDescriptionLabelForTesting());
  EXPECT_EQ(
      native_view.GetBlockedCountDescriptionLabelForTesting()->GetText(),
      u"trackers & ads blocked");
  ASSERT_TRUE(native_view.GetStatusLabelForTesting());
  EXPECT_EQ(native_view.GetStatusLabelForTesting()->GetText(),
            u"Shields are up on this site");
  ASSERT_TRUE(native_view.GetToggleForTesting());
  ASSERT_TRUE(native_view.GetToggleLabelForTesting());
  EXPECT_EQ(native_view.GetToggleLabelForTesting()->GetText(),
            u"Block trackers & ads");
  ASSERT_TRUE(native_view.GetToggleSublabelForTesting());
  EXPECT_EQ(native_view.GetToggleSublabelForTesting()->GetText(),
            u"On for " + base::UTF8ToUTF16(GURL(GetActiveOrigin()).host()));
  EXPECT_EQ(native_view.GetNativeToggleRowCountForTesting(), 1u);
  EXPECT_EQ(native_view.GetUnsupportedRowCountForTesting(), 0u);
  ASSERT_TRUE(native_view.GetSettingsButtonForTesting());
  EXPECT_EQ(native_view.GetSettingsButtonForTesting()->GetText(),
            u"Shield settings");
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       NativeModeShowsPausedStatusAndSingularBlockedLabel) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ScopedContentBlockingMode scoped_mode;
  ASSERT_TRUE(maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(0));

  maho::MahoShieldBubbleView native_view(static_cast<Browser*>(browser()), GetActiveOrigin(), 1, true,
                                         GetActiveWebContents());

  ASSERT_TRUE(native_view.GetToggleForTesting());
  EXPECT_FALSE(native_view.GetToggleForTesting()->GetIsOn());
  EXPECT_EQ(native_view.GetNativeToggleRowCountForTesting(), 1u);
  ASSERT_TRUE(native_view.GetBlockedCountLabelForTesting());
  EXPECT_EQ(native_view.GetBlockedCountLabelForTesting()->GetText(), u"1");
  ASSERT_TRUE(native_view.GetBlockedCountDescriptionLabelForTesting());
  EXPECT_EQ(
      native_view.GetBlockedCountDescriptionLabelForTesting()->GetText(),
      u"tracker or ad blocked");
  ASSERT_TRUE(native_view.GetStatusLabelForTesting());
  EXPECT_EQ(native_view.GetStatusLabelForTesting()->GetText(),
             u"Shields are off for this site");
  ASSERT_TRUE(native_view.GetToggleSublabelForTesting());
  EXPECT_EQ(native_view.GetToggleSublabelForTesting()->GetText(),
            u"Off for " + base::UTF8ToUTF16(GURL(GetActiveOrigin()).host()));
  EXPECT_EQ(native_view.GetUnsupportedRowCountForTesting(), 0u);
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       NonNativeModesShowStatusButNoMutableToggle) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ScopedContentBlockingMode scoped_mode;

  struct ModeExpectation {
    int mode;
    std::u16string status_text;
  };

  for (const auto& expectation : {
           ModeExpectation{1, u"Managed by extension"},
           ModeExpectation{2, u"Shields are off"},
       }) {
    ASSERT_TRUE(maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(
        expectation.mode));

    maho::MahoShieldBubbleView view(static_cast<Browser*>(browser()), GetActiveOrigin(), 7, false,
                                    GetActiveWebContents());

    EXPECT_EQ(view.GetToggleForTesting(), nullptr);
    EXPECT_EQ(view.GetToggleLabelForTesting(), nullptr);
    EXPECT_EQ(view.GetToggleSublabelForTesting(), nullptr);
    EXPECT_EQ(view.GetNativeToggleRowCountForTesting(), 0u);
    ASSERT_TRUE(view.GetBlockedCountLabelForTesting());
    EXPECT_EQ(view.GetBlockedCountLabelForTesting()->GetText(), u"7");
    ASSERT_TRUE(view.GetBlockedCountDescriptionLabelForTesting());
    EXPECT_EQ(view.GetBlockedCountDescriptionLabelForTesting()->GetText(),
              u"trackers & ads blocked");
    ASSERT_TRUE(view.GetStatusLabelForTesting());
    EXPECT_EQ(view.GetStatusLabelForTesting()->GetText(),
              expectation.status_text);
    EXPECT_EQ(view.GetUnsupportedRowCountForTesting(), 0u);
  }
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       UnavailableModeShowsNoMutableToggleOrGatedRows) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  maho::MahoShieldBubbleView unavailable_view(nullptr, GetActiveOrigin(), 3,
                                              false, nullptr);

  EXPECT_EQ(unavailable_view.GetToggleForTesting(), nullptr);
  EXPECT_EQ(unavailable_view.GetToggleLabelForTesting(), nullptr);
  EXPECT_EQ(unavailable_view.GetToggleSublabelForTesting(), nullptr);
  EXPECT_EQ(unavailable_view.GetNativeToggleRowCountForTesting(), 0u);
  ASSERT_TRUE(unavailable_view.GetBlockedCountLabelForTesting());
  EXPECT_EQ(unavailable_view.GetBlockedCountLabelForTesting()->GetText(), u"3");
  ASSERT_TRUE(unavailable_view.GetBlockedCountDescriptionLabelForTesting());
  EXPECT_EQ(
      unavailable_view.GetBlockedCountDescriptionLabelForTesting()->GetText(),
      u"trackers & ads blocked");
  ASSERT_TRUE(unavailable_view.GetStatusLabelForTesting());
  EXPECT_EQ(unavailable_view.GetStatusLabelForTesting()->GetText(),
            u"Shields unavailable");
  EXPECT_EQ(unavailable_view.GetUnsupportedRowCountForTesting(), 0u);
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       ShieldSettingsOpensContentBlockerManagement) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ScopedContentBlockingMode scoped_mode;
  ASSERT_TRUE(maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(0));

  maho::MahoShieldBubbleView native_view(static_cast<Browser*>(browser()), GetActiveOrigin(), 0,
                                         false, GetActiveWebContents());
  ASSERT_TRUE(native_view.GetSettingsButtonForTesting());
  EXPECT_EQ(native_view.GetSettingsButtonForTesting()->GetText(),
            u"Shield settings");

  native_view.OpenSettingsForTesting();

  EXPECT_EQ(browser()
                ->GetTabStripModel()
                ->GetActiveWebContents()
                ->GetVisibleURL()
                .spec(),
            "chrome://maho-settings/?pane=content-blocker");
}

IN_PROC_BROWSER_TEST_F(MahoShieldBubbleInteractiveUiTest,
                       ExtensionSettingsOpensExtensionManagement) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ScopedContentBlockingMode scoped_mode;
  ASSERT_TRUE(maho::MahoShieldBubbleView::SetContentBlockingModeForTesting(1));

  maho::MahoShieldBubbleView extension_view(static_cast<Browser*>(browser()), GetActiveOrigin(), 0,
                                            false, GetActiveWebContents());
  ASSERT_TRUE(extension_view.GetSettingsButtonForTesting());
  EXPECT_EQ(extension_view.GetSettingsButtonForTesting()->GetText(),
            u"Shield settings");

  extension_view.OpenSettingsForTesting();

  EXPECT_EQ(browser()
                ->GetTabStripModel()
                ->GetActiveWebContents()
                ->GetVisibleURL()
                .spec(),
            "chrome://maho-settings/?pane=extensions");
}
