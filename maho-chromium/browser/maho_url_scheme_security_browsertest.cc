// Copyright 2026 Maho Browser. All rights reserved.

#ifndef HAS_OUT_OF_PROC_TEST_RUNNER
#define HAS_OUT_OF_PROC_TEST_RUNNER
#endif

#include <utility>

#include "maho/browser/maho_url_scheme.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_url_loader_factory.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/components/constants/webui_url_constants.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/test/test_url_loader_client.h"
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
  return browser->GetTabStripModel()->GetActiveWebContents();
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
            contents->GetPrimaryMainFrame()
                ->GetSiteInstance()
                ->GetSecurityPrincipal()
                .GetDeprecatedSiteURL()
                .scheme());
}

struct WebUIEndpointResponse {
  int net_error = net::ERR_FAILED;
  std::string body;
  scoped_refptr<net::HttpResponseHeaders> headers;
  std::string mime_type;
};

WebUIEndpointResponse FetchWebUIEndpoint(content::WebContents* contents,
                                         const GURL& url) {
  mojo::Remote<network::mojom::URLLoaderFactory> loader_factory(
      content::CreateWebUIURLLoaderFactory(
          contents->GetPrimaryMainFrame(), std::string(url.scheme()),
          {std::string(url.host())}));
  network::ResourceRequest request;
  request.url = url;
  mojo::PendingRemote<network::mojom::URLLoader> loader;
  network::TestURLLoaderClient client;
  loader_factory->CreateLoaderAndStart(
      loader.InitWithNewPipeAndPassReceiver(), /*request_id=*/0,
      /*options=*/0, request, client.CreateRemote(),
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
  client.RunUntilComplete();

  WebUIEndpointResponse response;
  response.net_error = client.completion_status().error_code;
  if (client.response_head()) {
    response.headers = client.response_head()->headers;
    response.mime_type = client.response_head()->mime_type;
  }
  if (!client.response_body().is_valid()) {
    return response;
  }
  size_t body_size = 0;
  if (client.response_body().ReadData(MOJO_READ_DATA_FLAG_QUERY,
                                      base::span<uint8_t>(), body_size) !=
      MOJO_RESULT_OK) {
    return response;
  }
  std::vector<uint8_t> body(body_size);
  if (body_size > 0 &&
      client.response_body().ReadData(MOJO_READ_DATA_FLAG_ALL_OR_NONE, body,
                                      body_size) != MOJO_RESULT_OK) {
    return response;
  }
  response.body.assign(body.begin(), body.end());
  return response;
}

std::optional<std::string> HeaderValue(const WebUIEndpointResponse& response,
                                       std::string_view name) {
  return response.headers
             ? response.headers->GetNormalizedHeader(std::string(name))
             : std::nullopt;
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
    content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
    EXPECT_EQ(url, contents->GetLastCommittedURL());
    return contents;
  }

  void AttemptRendererScriptAndExpectBlocked(std::string_view script) {
    content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
    const GURL before_url = contents->GetLastCommittedURL();
    const int before_tab_count = browser()->GetTabStripModel()->count();

    EXPECT_FALSE(RendererCanRequest(contents, GURL(kMahoSettingsPublicURL)));
    EXPECT_TRUE(content::ExecJs(contents, script,
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE));
    base::RunLoop().RunUntilIdle();

    ExpectNoMahoCommit(contents, before_url);
    EXPECT_EQ(before_tab_count, browser()->GetTabStripModel()->count());
  }

  std::string RegisterArtifact(std::string relative_path,
                               std::string display_name,
                               std::string bytes) {
    auto* registry = ai::MahoArtifactRegistry::GetForProfile(browser()->GetProfile());
    CHECK(registry);
    const base::FilePath& root = registry->artifact_root();
    CHECK(!root.empty());
    base::FilePath path =
        root.Append(base::FilePath::FromUTF8Unsafe(relative_path));
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      CHECK(base::CreateDirectory(path.DirName()));
      CHECK(base::WriteFile(path, bytes));
    }
    auto artifact_id = registry->RegisterArtifact(
        "endpoint-security-session", std::move(display_name), "text/html",
        bytes.size(), std::move(relative_path), 1);
    CHECK(artifact_id.has_value());
    return *artifact_id;
  }

  void InstallArtifactDataSources(content::WebContents* contents) {
    EXPECT_FALSE(ui_test_utils::NavigateToURL(
        browser(), GURL(kMahoArtifactPreviewUntrustedURL)));
    EXPECT_FALSE(ui_test_utils::NavigateToURL(
        browser(), GURL(kMahoArtifactExportUntrustedURL)));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://newtab")));
    ASSERT_EQ(contents, ActiveWebContents(static_cast<Browser*>(browser())));
  }
};

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       ArtifactPreviewEndpointServesSanitizedHtmlAndStrictCsp) {
  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
  InstallArtifactDataSources(contents);
  const std::string artifact_id = RegisterArtifact(
      "preview/valid.html", "preview.html",
      R"HTML(<main>safe<script>alert(1)</script><img src="https://evil.test/x" onerror="steal()"></main>)HTML");
  auto* registry = ai::MahoArtifactRegistry::GetForProfile(browser()->GetProfile());
  auto capability = registry->IssueCapability(artifact_id, "preview", contents);
  ASSERT_TRUE(capability.has_value());

  const WebUIEndpointResponse response = FetchWebUIEndpoint(
      contents, GURL(base::StrCat(
                    {kMahoArtifactPreviewUntrustedURL, "?cap=", *capability})));
  ASSERT_EQ(net::OK, response.net_error);
  EXPECT_EQ("text/html", response.mime_type);
  EXPECT_NE(std::string::npos, response.body.find("<main>safe"));
  EXPECT_EQ(std::string::npos, response.body.find("script"));
  EXPECT_EQ(std::string::npos, response.body.find("evil.test"));
  EXPECT_EQ(std::string::npos, response.body.find("onerror"));
  const auto csp = HeaderValue(response, "Content-Security-Policy");
  ASSERT_TRUE(csp.has_value());
  EXPECT_NE(std::string::npos, csp->find("default-src 'none'"));
  EXPECT_NE(std::string::npos, csp->find("script-src 'none'"));
  EXPECT_NE(std::string::npos,
            csp->find("frame-ancestors chrome://maho-ai"));
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       ArtifactEndpointsRejectInvalidExpiredAndWrongWebContents) {
  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
  InstallArtifactDataSources(contents);
  const std::string artifact_id =
      RegisterArtifact("denials/data.txt", "data.txt", "endpoint bytes");
  auto* registry = ai::MahoArtifactRegistry::GetForProfile(browser()->GetProfile());

  EXPECT_NE(net::OK,
            FetchWebUIEndpoint(
                contents,
                GURL(base::StrCat({kMahoArtifactPreviewUntrustedURL,
                                   "?cap=not-a-capability"})))
                .net_error);

  auto expired = registry->IssueCapability(artifact_id, "preview", contents);
  ASSERT_TRUE(expired.has_value());
  ASSERT_TRUE(registry->ExpireCapabilityForTesting(*expired));
  EXPECT_NE(net::OK,
            FetchWebUIEndpoint(
                contents,
                GURL(base::StrCat({kMahoArtifactPreviewUntrustedURL, "?cap=",
                                   *expired})))
                .net_error);

  auto wrong_contents =
      registry->IssueCapability(artifact_id, "export", contents);
  ASSERT_TRUE(wrong_contents.has_value());
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  content::WebContents* other_contents = ActiveWebContents(static_cast<Browser*>(browser()));
  ASSERT_NE(contents, other_contents);
  EXPECT_NE(net::OK,
            FetchWebUIEndpoint(
                other_contents,
                GURL(base::StrCat({kMahoArtifactExportUntrustedURL, "?cap=",
                                   *wrong_contents})))
                .net_error);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       ArtifactEndpointRejectsCrossProfileCapability) {
  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
  InstallArtifactDataSources(contents);
  const std::string artifact_id =
      RegisterArtifact("profile/data.txt", "profile.txt", "profile bytes");
  auto* registry = ai::MahoArtifactRegistry::GetForProfile(browser()->GetProfile());
  auto capability = registry->IssueCapability(artifact_id, "preview", contents);
  ASSERT_TRUE(capability.has_value());

  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  content::WebContents* incognito_contents = ActiveWebContents(incognito);
  EXPECT_FALSE(ui_test_utils::NavigateToURL(
      incognito, GURL(kMahoArtifactPreviewUntrustedURL)));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(incognito, GURL("chrome://newtab")));
  EXPECT_NE(net::OK,
            FetchWebUIEndpoint(
                incognito_contents,
                GURL(base::StrCat({kMahoArtifactPreviewUntrustedURL, "?cap=",
                                   *capability})))
                .net_error);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       ArtifactExportEndpointReturnsExactBytesAndSafeHeaders) {
  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
  InstallArtifactDataSources(contents);
  const std::string bytes("exact\0artifact\xff" "bytes", 20);
  const std::string artifact_id =
      RegisterArtifact("export/report.bin", "Quarterly report.bin", bytes);
  auto* registry = ai::MahoArtifactRegistry::GetForProfile(browser()->GetProfile());
  auto capability = registry->IssueCapability(artifact_id, "export", contents);
  ASSERT_TRUE(capability.has_value());

  const WebUIEndpointResponse response = FetchWebUIEndpoint(
      contents, GURL(base::StrCat(
                    {kMahoArtifactExportUntrustedURL, "?cap=", *capability})));
  ASSERT_EQ(net::OK, response.net_error);
  EXPECT_EQ(bytes, response.body);
  EXPECT_EQ("application/octet-stream; name=\"Quarterly report.bin\"",
            response.mime_type);
  EXPECT_EQ("application/octet-stream; name=\"Quarterly report.bin\"",
            HeaderValue(response, "Content-Type"));
  EXPECT_EQ("no-cache", HeaderValue(response, "Cache-Control"));
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       TrustedAiCspAllowsOnlyExactArtifactPreviewFrameHost) {
  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kMahoAIURL)));
  base::RunLoop().RunUntilIdle();
  contents = ActiveWebContents(static_cast<Browser*>(browser()));
  const WebUIEndpointResponse response =
      FetchWebUIEndpoint(contents, GURL(kMahoAIURL));
  ASSERT_EQ(net::OK, response.net_error);
  const auto csp = HeaderValue(response, "Content-Security-Policy");
  ASSERT_TRUE(csp.has_value());
  EXPECT_NE(std::string::npos,
            csp->find("frame-src https://maho.lemonsqueezy.com "
                      "https://lemonsqueezy.com "
                      "chrome-untrusted://maho-ai-artifact-preview;"));
  EXPECT_EQ(std::string::npos,
            csp->find("chrome-untrusted://maho-ai-artifact-export"));
  EXPECT_EQ(std::string::npos, csp->find("chrome-untrusted:;"));
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeSecurityBrowserTest,
                       BrowserInitiatedAliasCommitsTrustedSettingsWebUI) {
  ScopedExternalProtocolFailureDelegate external_protocol_guard;
  const GURL alias_url(kMahoSettingsPublicURL);
  GURL actual_url;
  ASSERT_TRUE(MapMahoUrlAliasToActualUrl(alias_url, &actual_url));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), alias_url));

  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
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
