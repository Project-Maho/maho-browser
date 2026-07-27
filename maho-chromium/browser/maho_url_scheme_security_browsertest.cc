// Copyright 2026 Maho Browser. All rights reserved.

#ifndef HAS_OUT_OF_PROC_TEST_RUNNER
#define HAS_OUT_OF_PROC_TEST_RUNNER
#endif

#include "maho/browser/maho_url_scheme.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/run_loop.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/origin.h"

class Profile;

namespace maho {
namespace {

constexpr char kDisableLoginGateSwitch[] = "maho-disable-login-gate";

content::WebContents* ActiveWebContents(Browser* browser) {
  return browser->tab_strip_model()->GetActiveWebContents();
}

class ScopedExternalProtocolFailureDelegate
    : public ExternalProtocolHandler::Delegate {
 public:
  ScopedExternalProtocolFailureDelegate() {
    ExternalProtocolHandler::SetDelegateForTesting(this);
  }

  ScopedExternalProtocolFailureDelegate(
      const ScopedExternalProtocolFailureDelegate&) = delete;
  ScopedExternalProtocolFailureDelegate& operator=(
      const ScopedExternalProtocolFailureDelegate&) = delete;

  ~ScopedExternalProtocolFailureDelegate() override {
    ExternalProtocolHandler::SetDelegateForTesting(nullptr);
  }

  int dispatch_count() const { return dispatch_count_; }

  scoped_refptr<shell_integration::DefaultSchemeClientWorker> CreateShellWorker(
      const GURL& url) override {
    ++dispatch_count_;
    ADD_FAILURE() << "Unexpected external protocol shell worker for " << url;
    return nullptr;
  }

  ExternalProtocolHandler::BlockState GetBlockState(
      const std::string& scheme,
      Profile* profile) override {
    ++dispatch_count_;
    ADD_FAILURE() << "Unexpected external protocol block-state check for "
                  << scheme;
    return ExternalProtocolHandler::BLOCK;
  }

  void BlockRequest() override { ++dispatch_count_; }

  void RunExternalProtocolDialog(
      const GURL& url,
      content::WebContents* web_contents,
      ui::PageTransition page_transition,
      bool has_user_gesture,
      const std::optional<url::Origin>& initiating_origin,
      const std::u16string& program_name) override {
    ++dispatch_count_;
    ADD_FAILURE() << "Unexpected external protocol dialog for " << url;
  }

  void LaunchUrlWithoutSecurityCheck(
      const GURL& url,
      content::WebContents* web_contents) override {
    ++dispatch_count_;
    ADD_FAILURE() << "Unexpected external protocol launch for " << url;
  }

  void FinishedProcessingCheck() override {}

 private:
  int dispatch_count_ = 0;
};

bool RendererCanRequest(content::WebContents* contents, const GURL& url) {
  content::RenderFrameHost* main_frame = contents->GetPrimaryMainFrame();
  return content::ChildProcessSecurityPolicy::GetInstance()->CanRequestURL(
      main_frame->GetProcess()->GetID().GetUnsafeValue(), url);
}

void ExpectNoMahoCommit(content::WebContents* contents,
                        const GURL& expected_main_frame_url) {
  EXPECT_EQ(expected_main_frame_url, contents->GetLastCommittedURL());
  EXPECT_EQ(expected_main_frame_url,
            contents->GetPrimaryMainFrame()->GetLastCommittedURL());
  EXPECT_NE("maho",
            contents->GetPrimaryMainFrame()->GetLastCommittedOrigin().scheme());
  EXPECT_NE("maho",
            contents->GetPrimaryMainFrame()->GetSiteInstance()->GetSiteURL()
                .scheme());
}

}  // namespace

class MahoUrlSchemeSecurityBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(kDisableLoginGateSwitch);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_https_test_server().SetSSLConfig(
        net::EmbeddedTestServer::CERT_TEST_NAMES);
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_https_test_server().Start());
  }

  content::WebContents* NavigateToNormalHttpsPage() {
    const GURL url =
        embedded_https_test_server().GetURL("a.test", "/title1.html");
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    content::WebContents* contents = ActiveWebContents(browser());
    EXPECT_EQ(url, contents->GetLastCommittedURL());
    return contents;
  }

  void AttemptRendererScriptAndExpectBlocked(std::string_view script) {
    content::WebContents* contents = ActiveWebContents(browser());
    const GURL before_url = contents->GetLastCommittedURL();
    const int before_tab_count = browser()->tab_strip_model()->count();

    EXPECT_FALSE(RendererCanRequest(contents, GURL(kMahoSettingsPublicURL)));
    EXPECT_TRUE(content::ExecJs(contents, script,
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE));
    base::RunLoop().RunUntilIdle();

    ExpectNoMahoCommit(contents, before_url);
    EXPECT_EQ(before_tab_count, browser()->tab_strip_model()->count());
  }
};

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       BrowserInitiatedAliasCommitsTrustedSettingsWebUI) {
  ScopedExternalProtocolFailureDelegate external_protocol_guard;
  const GURL alias_url(kMahoSettingsPublicURL);
  GURL actual_url;
  ASSERT_TRUE(MapMahoUrlAliasToActualUrl(alias_url, &actual_url));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), alias_url));

  content::WebContents* contents = ActiveWebContents(browser());
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(actual_url, entry->GetURL());
  EXPECT_EQ(alias_url, entry->GetVirtualURL());
  EXPECT_EQ(actual_url, contents->GetPrimaryMainFrame()->GetLastCommittedURL());
  EXPECT_EQ("chrome",
            contents->GetPrimaryMainFrame()->GetLastCommittedOrigin().scheme());
  EXPECT_TRUE(content::ChildProcessSecurityPolicy::GetInstance()
                  ->HasWebUIBindings(
                      contents->GetPrimaryMainFrame()
                          ->GetProcess()
                          ->GetID()
                          .GetUnsafeValue()));
  EXPECT_EQ(0, external_protocol_guard.dispatch_count());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       RendererLocationHrefCannotCommitPrivilegedAlias) {
  ScopedExternalProtocolFailureDelegate external_protocol_guard;
  NavigateToNormalHttpsPage();

  AttemptRendererScriptAndExpectBlocked(
      "location.href = 'maho://settings/'; true;");

  EXPECT_EQ(0, external_protocol_guard.dispatch_count());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       RendererIframeCannotCommitPrivilegedAlias) {
  ScopedExternalProtocolFailureDelegate external_protocol_guard;
  NavigateToNormalHttpsPage();

  AttemptRendererScriptAndExpectBlocked(
      "const iframe = document.createElement('iframe');"
      "iframe.src = 'maho://settings/';"
      "document.body.appendChild(iframe); true;");

  EXPECT_EQ(0, external_protocol_guard.dispatch_count());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       RendererBlankTargetAndWindowOpenCannotCommitAlias) {
  ScopedExternalProtocolFailureDelegate external_protocol_guard;
  NavigateToNormalHttpsPage();

  AttemptRendererScriptAndExpectBlocked(
      "const anchor = document.createElement('a');"
      "anchor.href = 'maho://settings/';"
      "anchor.target = '_blank';"
      "document.body.appendChild(anchor);"
      "anchor.click();"
      "window.open('maho://settings/', '_blank'); true;");

  EXPECT_EQ(0, external_protocol_guard.dispatch_count());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       RendererInvalidAliasesDoNotEscalate) {
  ScopedExternalProtocolFailureDelegate external_protocol_guard;
  NavigateToNormalHttpsPage();

  EXPECT_FALSE(IsValidMahoUrlAlias(GURL("maho://test/")));
  AttemptRendererScriptAndExpectBlocked("location.href = 'maho://test/'; true;");

  EXPECT_FALSE(IsValidMahoUrlAlias(GURL("maho://settings.evil/")));
  AttemptRendererScriptAndExpectBlocked(
      "location.href = 'maho://settings.evil/'; true;");

  EXPECT_FALSE(IsValidMahoUrlAlias(GURL("maho://settings:443/")));
  AttemptRendererScriptAndExpectBlocked(
      "location.href = 'maho://settings:443/'; true;");

  EXPECT_FALSE(IsValidMahoUrlAlias(GURL("maho://user:pass@settings/")));
  AttemptRendererScriptAndExpectBlocked(
      "location.href = 'maho://user:pass@settings/'; true;");

  EXPECT_EQ(0, external_protocol_guard.dispatch_count());
}

}  // namespace maho
