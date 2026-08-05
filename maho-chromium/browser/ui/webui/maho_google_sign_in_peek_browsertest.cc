// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_google_sign_in.h"

#include <memory>

#include "base/command_line.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/peek/maho_peek_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace maho::auth::testing {
namespace {

constexpr char kFakeOAuthPath[] = "/maho-google-sign-in-fake-oauth";

std::unique_ptr<net::test_server::HttpResponse> HandleFakeOAuthRequest(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kFakeOAuthPath) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(R"html(
    <!doctype html>
    <title>Local fake Google OAuth relay</title>
    <button id="finish" onclick="window.close()">Finish</button>
  )html");
  return response;
}

class MahoGoogleSignInPeekBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleFakeOAuthRequest));
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
    if (browser()->tab_strip_model()->empty()) {
      chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                       /*foreground=*/true);
    }
    ASSERT_TRUE(browser()->tab_strip_model()->GetActiveWebContents());
  }

 protected:
  GURL FakeOAuthUrl() const {
    return embedded_test_server()->GetURL(kFakeOAuthPath);
  }

  MahoPeekController* PeekControllerFor(Browser* host_browser) {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(host_browser);
    return browser_view ? browser_view->GetOrCreateMahoPeekController()
                        : nullptr;
  }

  content::WebContents* NavigateAndExpectHostedInPeek(
      Profile* profile,
      Browser* host_browser) {
    content::TestNavigationObserver navigation(FakeOAuthUrl());
    navigation.StartWatchingNewWebContents();
    content::WebContents* contents = NavigateToGoogleSignInForTesting(
        profile, host_browser, FakeOAuthUrl());
    EXPECT_TRUE(contents);
    if (!contents) {
      return nullptr;
    }
    navigation.Wait();
    EXPECT_TRUE(navigation.last_navigation_succeeded());

    MahoPeekController* controller = PeekControllerFor(host_browser);
    EXPECT_TRUE(controller);
    if (!controller) {
      return contents;
    }
    EXPECT_EQ(MahoPeekController::State::Open, controller->state_for_testing());
    EXPECT_TRUE(controller->peek_view_for_testing());
    if (!controller->peek_view_for_testing()) {
      return contents;
    }
    EXPECT_EQ(contents,
              controller->peek_view_for_testing()->hosted_contents());
    EXPECT_EQ(FakeOAuthUrl(), contents->GetLastCommittedURL());
    return contents;
  }
};

// Deferred execution: shared *browsertests targets currently do not link.
IN_PROC_BROWSER_TEST_F(MahoGoogleSignInPeekBrowserTest,
                       SettingsOriginatedUsesExplicitTabBrowserHost) {
  content::WebContents* settings_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(settings_contents);
  Browser* settings_host = chrome::FindBrowserWithTab(settings_contents);
  ASSERT_EQ(browser(), settings_host);
  ASSERT_TRUE(maho::IsPeekEligible(settings_host));

  ASSERT_TRUE(NavigateAndExpectHostedInPeek(browser()->profile(),
                                            settings_host));
}

// Deferred execution: shared *browsertests targets currently do not link.
IN_PROC_BROWSER_TEST_F(MahoGoogleSignInPeekBrowserTest,
                       WelcomeOriginatedUsesLastActiveExactProfileBrowser) {
  Profile* flow_profile = browser()->profile();
  BrowserWindowInterface* last_active =
      chrome::FindLastActiveWithProfile(flow_profile);
  ASSERT_TRUE(last_active);
  Browser* welcome_host = last_active->GetBrowserForMigrationOnly();
  ASSERT_TRUE(maho::IsPeekEligible(welcome_host));
  ASSERT_EQ(flow_profile, welcome_host->profile());
  ASSERT_EQ(static_cast<content::BrowserContext*>(flow_profile),
            welcome_host->profile());

  ASSERT_TRUE(NavigateAndExpectHostedInPeek(flow_profile, welcome_host));
}

// Deferred execution: shared *browsertests targets currently do not link.
IN_PROC_BROWSER_TEST_F(MahoGoogleSignInPeekBrowserTest,
                       BusyOrDisabledOrMissingHostFallsBackToPopup) {
  MahoPeekController* controller = PeekControllerFor(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->ShowUrl(nullptr,
                                  embedded_test_server()->GetURL("/busy")));

  ui_test_utils::BrowserCreatedObserver busy_popup_observer;
  content::WebContents* busy_fallback = NavigateToGoogleSignInForTesting(
      browser()->profile(), browser(), FakeOAuthUrl());
  Browser* busy_popup = busy_popup_observer.Wait();
  ASSERT_TRUE(busy_popup);
  EXPECT_TRUE(busy_popup->is_type_popup());
  EXPECT_EQ(busy_fallback,
            busy_popup->tab_strip_model()->GetActiveWebContents());
  CloseBrowserSynchronously(busy_popup);
  controller->Hide();

  browser()->profile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kPeekEnabled, false);
  ui_test_utils::BrowserCreatedObserver disabled_popup_observer;
  content::WebContents* disabled_fallback = NavigateToGoogleSignInForTesting(
      browser()->profile(), browser(), FakeOAuthUrl());
  Browser* disabled_popup = disabled_popup_observer.Wait();
  ASSERT_TRUE(disabled_popup);
  EXPECT_TRUE(disabled_popup->is_type_popup());
  EXPECT_EQ(disabled_fallback,
            disabled_popup->tab_strip_model()->GetActiveWebContents());
  CloseBrowserSynchronously(disabled_popup);

  ui_test_utils::BrowserCreatedObserver missing_host_popup_observer;
  content::WebContents* missing_host_fallback =
      NavigateToGoogleSignInForTesting(browser()->profile(), nullptr,
                                       FakeOAuthUrl());
  Browser* missing_host_popup = missing_host_popup_observer.Wait();
  ASSERT_TRUE(missing_host_popup);
  EXPECT_TRUE(missing_host_popup->is_type_popup());
  EXPECT_EQ(missing_host_fallback,
            missing_host_popup->tab_strip_model()->GetActiveWebContents());
  CloseBrowserSynchronously(missing_host_popup);
}

}  // namespace
}  // namespace maho::auth::testing
