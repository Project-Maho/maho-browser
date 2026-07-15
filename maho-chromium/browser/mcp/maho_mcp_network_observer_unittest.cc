// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_network_observer.h"

#include <string>
#include <vector>

#include "base/byte_count.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/test_renderer_host.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_version.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "url/gurl.h"

namespace maho {
namespace {

base::DictValue BuildHarForEntry(MahoMcpNetworkObserver::Entry entry) {
  std::vector<MahoMcpNetworkObserver::Entry> entries;
  entries.push_back(std::move(entry));
  return MahoMcpNetworkObserver::BuildHarFromEntries(entries);
}

blink::mojom::ResourceLoadInfoPtr CreateResourceLoadInfo(
    const GURL& url,
    network::mojom::RequestDestination request_destination) {
  auto info = blink::mojom::ResourceLoadInfo::New();
  info->final_url = url;
  info->original_url = url;
  info->method = "GET";
  info->request_destination = request_destination;
  info->mime_type = "text/plain";
  info->load_timing_info.request_start = base::TimeTicks::Now();
  info->load_timing_info.receive_headers_end =
      info->load_timing_info.request_start + base::Milliseconds(25);
  info->raw_body_bytes = base::ByteCount(321);
  info->total_received_bytes = base::ByteCount(654);
  info->http_status_code = 404;
  return info;
}

TEST(MahoMcpNetworkObserverTest, HarBuild_EmptyCapture) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture();
  observer.StopCapture();

  base::DictValue har = observer.GetHarRaw();
  const std::string* version = har.FindString("version");
  ASSERT_TRUE(version);
  EXPECT_EQ(*version, "1.2");
  const base::ListValue* entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  EXPECT_EQ(entries->size(), 0u);
}

TEST(MahoMcpNetworkObserverTest, HarBuild_SingleRequest) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture();

  MahoMcpNetworkObserver::Entry entry;
  entry.url = "https://example.com/api/data";
  entry.method = "GET";
  entry.status = 200;
  entry.status_text = "OK";
  entry.mime_type = "application/json";
  entry.duration = base::Milliseconds(150);
  entry.request_size = 128;
  entry.response_size = 4096;
  entry.response_transfer_size = 8192;
  entry.request_headers.Set("Accept", "application/json");
  entry.response_headers.Set("Content-Type", "application/json");

  base::DictValue har = BuildHarForEntry(std::move(entry));
  const base::ListValue* entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* e = (*entries)[0].GetIfDict();
  ASSERT_TRUE(e);
  EXPECT_EQ(*e->FindDouble("time"), 150.0);

  const base::DictValue* request = e->FindDict("request");
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("method"), "GET");
  EXPECT_EQ(*request->FindString("url"), "https://example.com/api/data");

  const base::DictValue* response = e->FindDict("response");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->FindInt("status").value(), 200);
  EXPECT_EQ(*response->FindString("statusText"), "OK");
}

TEST(MahoMcpNetworkObserverTest, HarBuild_HeadersRedacted) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture();

  MahoMcpNetworkObserver::Entry entry;
  entry.url = "https://example.com/api";
  entry.method = "POST";
  entry.status = 200;
  entry.status_text = "OK";
  entry.mime_type = "text/html";
  entry.request_headers.Set("Authorization", "Bearer secret-token-123");
  entry.request_headers.Set("Content-Type", "application/json");
  entry.response_headers.Set("Set-Cookie", "session=abc123; Path=/");
  entry.response_headers.Set("X-Request-Id", "req-456");

  Redacted<base::DictValue> har =
      MahoMcpFirewall::WrapHar(BuildHarForEntry(std::move(entry)));
  const base::DictValue& dict = har.get();
  const base::ListValue* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* e = (*entries)[0].GetIfDict();
  const base::DictValue* request = e->FindDict("request");
  ASSERT_TRUE(request);

  const base::DictValue* req_headers = request->FindDict("headers");
  ASSERT_TRUE(req_headers);
  const std::string* auth = req_headers->FindString("Authorization");
  ASSERT_TRUE(auth);
  EXPECT_EQ(*auth, "[REDACTED]");

  const std::string* ct = req_headers->FindString("Content-Type");
  ASSERT_TRUE(ct);
  EXPECT_EQ(*ct, "application/json");

  const base::DictValue* response = e->FindDict("response");
  ASSERT_TRUE(response);
  const base::DictValue* resp_headers = response->FindDict("headers");
  ASSERT_TRUE(resp_headers);
  const std::string* cookie = resp_headers->FindString("Set-Cookie");
  ASSERT_TRUE(cookie);
  EXPECT_EQ(*cookie, "[REDACTED]");

  const std::string* xrid = resp_headers->FindString("X-Request-Id");
  ASSERT_TRUE(xrid);
  EXPECT_EQ(*xrid, "req-456");
}

TEST(MahoMcpNetworkObserverTest, HarBuild_UrlRedacted) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture();

  MahoMcpNetworkObserver::Entry entry;
  entry.url = "https://example.com/callback?code=abc123&state=xyz&page=1";
  entry.method = "GET";
  entry.status = 302;
  entry.status_text = "Found";
  entry.mime_type = "text/html";

  Redacted<base::DictValue> har =
      MahoMcpFirewall::WrapHar(BuildHarForEntry(std::move(entry)));
  const base::DictValue& dict = har.get();
  const base::ListValue* entries = dict.FindList("entries");
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* e = (*entries)[0].GetIfDict();
  const base::DictValue* request = e->FindDict("request");
  const std::string* url = request->FindString("url");
  ASSERT_TRUE(url);

  EXPECT_NE(url->find("code=[REDACTED]"), std::string::npos);
  EXPECT_NE(url->find("state=[REDACTED]"), std::string::npos);
  EXPECT_NE(url->find("page=1"), std::string::npos);
  EXPECT_EQ(url->find("abc123"), std::string::npos);
  EXPECT_EQ(url->find("xyz"), std::string::npos);
}

class MahoMcpNetworkObserverWebContentsTest
    : public content::RenderViewHostTestHarness {};

TEST_F(MahoMcpNetworkObserverWebContentsTest,
       ResourceLoadCompleteRecordsSubresourceHarFields) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());

  auto info = CreateResourceLoadInfo(
      GURL("https://example.com/app.js"),
      network::mojom::RequestDestination::kScript);
  info->method = "POST";
  info->mime_type = "application/javascript";
  static_cast<content::WebContentsObserver*>(&observer)->ResourceLoadComplete(
      web_contents()->GetPrimaryMainFrame(), content::GlobalRequestID(),
      *info);

  base::DictValue har = observer.GetHarRaw();
  const base::ListValue* entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* entry = (*entries)[0].GetIfDict();
  ASSERT_TRUE(entry);
  const base::DictValue* request = entry->FindDict("request");
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("method"), "POST");
  EXPECT_EQ(request->FindInt("bodySize").value(), -1);
  EXPECT_FALSE(request->FindBool("_headersAvailable").value());

  const base::DictValue* response = entry->FindDict("response");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->FindInt("status").value(), 404);
  EXPECT_EQ(*response->FindString("statusText"), "Not Found");
  EXPECT_EQ(response->FindInt("bodySize").value(), 321);
  EXPECT_EQ(response->FindInt("_transferSize").value(), 654);
  EXPECT_FALSE(response->FindBool("_headersAvailable").value());

  const base::DictValue* content = response->FindDict("content");
  ASSERT_TRUE(content);
  EXPECT_EQ(content->FindInt("size").value(), 321);
  EXPECT_EQ(*content->FindString("mimeType"), "application/javascript");
}

TEST_F(MahoMcpNetworkObserverWebContentsTest,
       MainFrameDocumentResourceDoesNotDuplicateNavigationEntry) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());

  const GURL url("https://example.com/main-frame.html");
  content::MockNavigationHandle handle(url,
                                       web_contents()->GetPrimaryMainFrame());
  handle.set_has_committed(true);
  EXPECT_CALL(handle, GetRequestMethod())
      .WillOnce(testing::Return(std::string("GET")));
  EXPECT_CALL(handle, NavigationStart())
      .WillOnce(testing::Return(base::TimeTicks::Now()));
  static_cast<content::WebContentsObserver*>(&observer)->DidFinishNavigation(
      &handle);

  auto info =
      CreateResourceLoadInfo(url, network::mojom::RequestDestination::kDocument);
  static_cast<content::WebContentsObserver*>(&observer)->ResourceLoadComplete(
      web_contents()->GetPrimaryMainFrame(), content::GlobalRequestID(),
      *info);

  base::DictValue har = observer.GetHarRaw();
  const base::ListValue* entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);
}

TEST_F(MahoMcpNetworkObserverWebContentsTest,
       LiveNavigationHeadersAndUrlAreRedacted) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());

  const GURL url(
      "https://example.com/callback?access_token=secret-token&page=1");
  content::MockNavigationHandle handle(url,
                                       web_contents()->GetPrimaryMainFrame());
  handle.set_has_committed(true);

  net::HttpRequestHeaders request_headers;
  request_headers.SetHeader("Authorization", "Bearer secret-token");
  request_headers.SetHeader("Accept", "text/html");
  handle.set_request_headers(request_headers);

  scoped_refptr<net::HttpResponseHeaders> response_headers =
      net::HttpResponseHeaders::Builder(net::HttpVersion(1, 1), "201 Created")
          .AddHeader("Set-Cookie", "session=secret-cookie; Path=/")
          .AddHeader("X-Request-Id", "req-123")
          .Build();
  handle.set_response_headers(std::move(response_headers));

  EXPECT_CALL(handle, GetRequestMethod())
      .WillOnce(testing::Return(std::string("POST")));
  EXPECT_CALL(handle, NavigationStart())
      .WillOnce(testing::Return(base::TimeTicks::Now()));
  static_cast<content::WebContentsObserver*>(&observer)->DidFinishNavigation(
      &handle);

  Redacted<base::DictValue> har = observer.GetHar();
  const base::ListValue* entries = har.get().FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* entry = (*entries)[0].GetIfDict();
  ASSERT_TRUE(entry);
  const base::DictValue* request = entry->FindDict("request");
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("method"), "POST");
  const std::string* captured_url = request->FindString("url");
  ASSERT_TRUE(captured_url);
  EXPECT_NE(captured_url->find("access_token=[REDACTED]"), std::string::npos);
  EXPECT_NE(captured_url->find("page=1"), std::string::npos);
  EXPECT_EQ(captured_url->find("secret-token"), std::string::npos);
  const base::DictValue* request_headers_dict = request->FindDict("headers");
  ASSERT_TRUE(request_headers_dict);
  EXPECT_EQ(*request_headers_dict->FindString("Authorization"), "[REDACTED]");
  EXPECT_EQ(*request_headers_dict->FindString("Accept"), "text/html");

  const base::DictValue* response = entry->FindDict("response");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->FindInt("status").value(), 201);
  EXPECT_EQ(*response->FindString("statusText"), "Created");
  const base::DictValue* response_headers_dict = response->FindDict("headers");
  ASSERT_TRUE(response_headers_dict);
  EXPECT_EQ(*response_headers_dict->FindString("Set-Cookie"), "[REDACTED]");
  EXPECT_EQ(*response_headers_dict->FindString("X-Request-Id"), "req-123");
}

TEST_F(MahoMcpNetworkObserverWebContentsTest,
       DataUrlNavigationRedactsInlinePayload) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());

  const GURL url("data:text/html,<script>secret_payload()</script>");
  content::MockNavigationHandle handle(url,
                                       web_contents()->GetPrimaryMainFrame());
  handle.set_has_committed(true);
  EXPECT_CALL(handle, GetRequestMethod())
      .WillOnce(testing::Return(std::string("GET")));
  EXPECT_CALL(handle, NavigationStart())
      .WillOnce(testing::Return(base::TimeTicks::Now()));
  static_cast<content::WebContentsObserver*>(&observer)->DidFinishNavigation(
      &handle);

  base::DictValue har = observer.GetHarRaw();
  const base::ListValue* entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* entry = (*entries)[0].GetIfDict();
  ASSERT_TRUE(entry);
  const base::DictValue* request = entry->FindDict("request");
  ASSERT_TRUE(request);
  const std::string* captured_url = request->FindString("url");
  ASSERT_TRUE(captured_url);
  EXPECT_NE(captured_url->find("data:text/html,"), std::string::npos);
  EXPECT_NE(captured_url->find("[REDACTED_DATA_URL_PAYLOAD]"),
            std::string::npos);
  EXPECT_EQ(captured_url->find("secret_payload"), std::string::npos);
}

TEST_F(MahoMcpNetworkObserverWebContentsTest,
       CapturesLiveCommittedNavigationDuringCapture) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());

  const GURL url(
      "https://example.com/mcp-live-har-marker?access_token=secret-token");
  content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents(),
                                                             url);

  Redacted<base::DictValue> har = observer.GetHar();
  const base::ListValue* entries = har.get().FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);

  const base::DictValue* entry = (*entries)[0].GetIfDict();
  ASSERT_TRUE(entry);
  const base::DictValue* request = entry->FindDict("request");
  ASSERT_TRUE(request);
  const std::string* captured_url = request->FindString("url");
  ASSERT_TRUE(captured_url);
  EXPECT_NE(captured_url->find("mcp-live-har-marker"), std::string::npos);
  EXPECT_EQ(captured_url->find("secret-token"), std::string::npos);
  EXPECT_NE(captured_url->find("access_token=[REDACTED]"), std::string::npos);
}

// Compile-time gate: the following would fail without .get()/.take():
//   base::Value::Dict leaked = observer.GetHar();  // no implicit conversion
TEST(MahoMcpNetworkObserverTest, Redacted_CompileTimeGate) {
  MahoMcpNetworkObserver observer;
  observer.StartCapture();
  observer.StopCapture();

  auto har = observer.GetHar();
  const base::DictValue& safe = har.get();
  EXPECT_TRUE(safe.FindList("entries"));

  auto har2 = observer.GetHar();
  base::DictValue taken = std::move(har2).take();
  EXPECT_TRUE(taken.FindList("entries"));
}

}  // namespace
}  // namespace maho
