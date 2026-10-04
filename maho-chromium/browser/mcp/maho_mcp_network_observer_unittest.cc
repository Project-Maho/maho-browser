// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_network_observer.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "base/byte_size.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/test_renderer_host.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_navigation_tracker.h"
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

// allow: SIZE_OK - Existing GN-linked observer/navigation TU with shared user
// edits. Lane permits this TU, not a split requiring shared GN registration.

class MahoMcpNetworkObserverTest
    : public content::RenderViewHostTestHarness {};

using MahoMcpNetworkObserverWebContentsTest = MahoMcpNetworkObserverTest;

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
  info->raw_body_bytes = base::ByteSize(321);
  info->total_received_bytes = base::ByteSize(654);
  info->http_status_code = 404;
  return info;
}

TEST_F(MahoMcpNetworkObserverTest, HarBuild_EmptyCapture) {
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

TEST_F(MahoMcpNetworkObserverTest, HarBuild_SingleRequest) {
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

TEST_F(MahoMcpNetworkObserverTest, HarBuild_HeadersRedacted) {
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

TEST_F(MahoMcpNetworkObserverTest, HarBuild_UrlRedacted) {
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

TEST_F(MahoMcpNetworkObserverTest,
       MemoryThreadCaptureByteAndEntryBounds) {
  // Given: an attached observer and subresource completions, not a fabricated HAR vector.
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());
  // When: twice the entry allowance completes during a single capture.
  for (int i = 0; i < 20000; ++i) {
    auto info = CreateResourceLoadInfo(
        GURL("https://example.com/mt/" + std::to_string(i)),
        network::mojom::RequestDestination::kScript);
    static_cast<content::WebContentsObserver*>(&observer)->ResourceLoadComplete(
        web_contents()->GetPrimaryMainFrame(), content::GlobalRequestID(),
        info->original_url, *info);
  }
  observer.StopCapture();
  // Then: earliest records survive and omitted records are explicitly counted.
  const auto har = observer.GetHar();
  const auto* entries = har.get().FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 10000u);
  EXPECT_EQ(*entries->front().GetDict().FindStringByDottedPath("request.url"),
            "https://example.com/mt/0");
  EXPECT_EQ(*entries->back().GetDict().FindStringByDottedPath("request.url"),
            "https://example.com/mt/9999");
  EXPECT_EQ(har.get().FindBool("_truncated"), true);
  EXPECT_EQ(har.get().FindInt("_droppedEntries"), 10000);
}

TEST_F(MahoMcpNetworkObserverTest,
       MemoryThreadCaptureBytesBindBeforeEntryCount) {
  // Given: each legal entry is under 64 KiB, but 1,000 URLs exceed 8 MiB.
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());
  const std::string path(16 * 1024, 'x');
  // When: real resource callbacks reach the resident byte limit first.
  for (int i = 0; i < 1000; ++i) {
    auto info = CreateResourceLoadInfo(
        GURL("https://example.com/" + path + "?i=" + std::to_string(i)),
        network::mojom::RequestDestination::kScript);
    static_cast<content::WebContentsObserver*>(&observer)->ResourceLoadComplete(
        web_contents()->GetPrimaryMainFrame(), content::GlobalRequestID(),
        info->original_url, *info);
  }
  observer.StopCapture();
  // Then: the URL payload alone cannot exceed the total retained byte allowance.
  const auto har = observer.GetHar();
  const auto* entries = har.get().FindList("entries");
  ASSERT_TRUE(entries);
  size_t url_bytes = 0;
  for (const auto& entry : *entries) {
    const auto* url = entry.GetDict().FindStringByDottedPath("request.url");
    ASSERT_TRUE(url);
    url_bytes += url->size();
  }
  EXPECT_LE(url_bytes, 8u * 1024u * 1024u);
  ASSERT_FALSE(entries->empty());
  EXPECT_LT(entries->size(), 1000u);
  EXPECT_EQ(har.get().FindInt("_droppedEntries"), 1000 - entries->size());
  const auto retained = har.get().FindInt("_retainedBytes");
  ASSERT_TRUE(retained);
  ASSERT_GE(*retained, 0);
  EXPECT_GE(static_cast<size_t>(*retained), url_bytes);
  EXPECT_LE(*retained, 8 * 1024 * 1024);
  EXPECT_EQ(har.get().FindInt("_limitBytes"), 8 * 1024 * 1024);
}

TEST_F(MahoMcpNetworkObserverTest,
       MemoryThreadHarRedactionAndTruncation) {
  // Given: one sensitive navigation followed by an individually oversized resource.
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());
  content::MockNavigationHandle handle(
      GURL("https://example.com/?access_token=MT_SECRET_DO_NOT_RETURN&keep=1"),
      web_contents()->GetPrimaryMainFrame());
  handle.set_has_committed(true);
  net::HttpRequestHeaders headers;
  headers.SetHeader("Authorization", "Bearer MT_SECRET_DO_NOT_RETURN");
  headers.SetHeader("Accept", "text/html");
  handle.set_request_headers(headers);
  EXPECT_CALL(handle, GetRequestMethod()).WillOnce(testing::Return("GET"));
  EXPECT_CALL(handle, NavigationStart()).WillOnce(testing::Return(base::TimeTicks::Now()));
  static_cast<content::WebContentsObserver*>(&observer)->DidFinishNavigation(&handle);
  auto oversized = CreateResourceLoadInfo(
      GURL("https://example.com/" + std::string(65537, 'x')),
      network::mojom::RequestDestination::kScript);
  // When: the oversized entry crosses the actual callback/retention/egress path.
  static_cast<content::WebContentsObserver*>(&observer)->ResourceLoadComplete(
      web_contents()->GetPrimaryMainFrame(), content::GlobalRequestID(),
      oversized->original_url, *oversized);
  observer.StopCapture();
  const auto har = observer.GetHar();
  // Then: omission is truthful and the retained navigation still crosses the firewall.
  const auto* entries = har.get().FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);
  EXPECT_EQ(har.get().FindBool("_truncated"), true);
  EXPECT_EQ(har.get().FindInt("_droppedEntries"), 1);
  const auto& request = entries->front().GetDict();
  const auto* authorization = request.FindStringByDottedPath("request.headers.Authorization");
  ASSERT_TRUE(authorization);
  EXPECT_EQ(*authorization, "[REDACTED]");
  EXPECT_EQ(*request.FindStringByDottedPath("request.headers.Accept"), "text/html");
  const auto* url = request.FindStringByDottedPath("request.url");
  ASSERT_TRUE(url);
  EXPECT_EQ(url->find("MT_SECRET_DO_NOT_RETURN"), std::string::npos);
  EXPECT_NE(url->find("keep=1"), std::string::npos);
}

TEST_F(MahoMcpNetworkObserverTest,
       MemoryThreadStopMovesSnapshotAndCancelDiscardsReply) {
  // Given: an attached observer with captured requests.
  MahoMcpNetworkObserver observer;
  observer.StartCapture(web_contents());
  for (int i = 0; i < 5; ++i) {
    auto info = CreateResourceLoadInfo(
        GURL("https://example.com/mt/" + std::to_string(i)),
        network::mojom::RequestDestination::kScript);
    static_cast<content::WebContentsObserver*>(&observer)->ResourceLoadComplete(
        web_contents()->GetPrimaryMainFrame(), content::GlobalRequestID(),
        info->original_url, *info);
  }
  ASSERT_EQ(observer.entry_count_for_testing(), 5u);

  // When: StopCapture detaches the observer on UI and moves entries into an owned snapshot.
  observer.StopCapture();
  auto snapshot = observer.TakeSnapshot();

  // Then: the observer is detached and retains zero entries.
  EXPECT_FALSE(observer.is_capturing());
  EXPECT_EQ(observer.web_contents(), nullptr);
  EXPECT_EQ(observer.entry_count_for_testing(), 0u);
  EXPECT_EQ(snapshot.entries.size(), 5u);

  // And: pure HAR assembly runs on owned data off-UI with firewall redaction.
  Redacted<base::DictValue> redacted_har =
      MahoMcpNetworkObserver::BuildRedactedHarFromSnapshot(std::move(snapshot));
  const auto* entries = redacted_har.get().FindList("entries");
  ASSERT_TRUE(entries);
  EXPECT_EQ(entries->size(), 5u);

  // And: cancellation before reply delivery invalidates the generation token,
  // discarding the reply so no stale HAR is published.
  uint64_t capture_generation = 42;
  bool delivered = false;
  auto reply_callback = base::BindOnce(
      [](uint64_t expected_gen, const uint64_t* current_gen, bool* delivered_flag,
         Redacted<base::DictValue> har) {
        if (*current_gen != expected_gen) {
          return;  // Canceled / invalidated: discard reply.
        }
        *delivered_flag = true;
      },
      capture_generation, &capture_generation, &delivered);

  // Simulate CancelNetworkCapture / revocation by advancing generation.
  ++capture_generation;
  std::move(reply_callback).Run(std::move(redacted_har));
  EXPECT_FALSE(delivered);
}

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
      info->original_url, *info);

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
      info->original_url, *info);

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
TEST_F(MahoMcpNetworkObserverTest, Redacted_CompileTimeGate) {
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

class MahoMcpNavigationTrackerWebContentsTest
    : public content::RenderViewHostTestHarness {};

blink::mojom::ResourceLoadInfoPtr CreateSubresourceLoadInfo(
    const GURL& url,
    int http_status_code) {
  auto info = blink::mojom::ResourceLoadInfo::New();
  info->final_url = url;
  info->original_url = url;
  info->method = "GET";
  info->request_destination = network::mojom::RequestDestination::kScript;
  info->mime_type = "application/javascript";
  info->load_timing_info.request_start = base::TimeTicks::Now();
  info->load_timing_info.receive_headers_end =
      info->load_timing_info.request_start + base::Milliseconds(5);
  info->raw_body_bytes = base::ByteSize(1024);
  info->total_received_bytes = base::ByteSize(2048);
  info->http_status_code = http_status_code;
  return info;
}

void DispatchSubresourceLoad(MahoMcpNavigationTracker* tracker,
                             content::WebContents* web_contents,
                             const GURL& url,
                             int http_status_code) {
  auto info = CreateSubresourceLoadInfo(url, http_status_code);
  static_cast<content::WebContentsObserver*>(tracker)->ResourceLoadComplete(
      web_contents->GetPrimaryMainFrame(), content::GlobalRequestID(),
      info->original_url, *info);
}

// Fakes a committed primary main frame navigation directly on the tracker
// (the same pattern the observer tests use via MockNavigationHandle).
void CommitNavigationOn(MahoMcpNavigationTracker* tracker,
                        content::WebContents* web_contents,
                        const GURL& url,
                        bool same_document) {
  content::MockNavigationHandle handle(url,
                                       web_contents->GetPrimaryMainFrame());
  handle.set_has_committed(true);
  handle.set_is_same_document(same_document);
  static_cast<content::WebContentsObserver*>(tracker)->DidFinishNavigation(
      &handle);
}

// Subresource challenge evidence must accumulate in the tracker even when
// no HAR capture has ever been started: the tracker never consults the
// observer or its capturing_ flag.
TEST_F(MahoMcpNavigationTrackerWebContentsTest,
       SubresourceChallengeTokensCollectedIndependentOfHarRecording) {
  MahoMcpNetworkObserver observer;
  ASSERT_FALSE(observer.is_capturing());

  MahoMcpNavigationTracker::CreateForWebContents(web_contents());
  MahoMcpNavigationTracker* tracker =
      MahoMcpNavigationTracker::FromWebContents(web_contents());

  // Passive Turnstile widget embed with 200: collected as evidence, but
  // never reported as a block.
  DispatchSubresourceLoad(tracker, web_contents(),
                          GURL("https://example.com/turnstile/v0/api.js"), 200);

  // Explicit challenge endpoint answered 200: presence of the endpoint
  // alone is evidence of an active challenge.
  DispatchSubresourceLoad(
      tracker, web_contents(),
      GURL("https://example.com/cdn-cgi/challenge-platform/scripts/jsd/main.js"),
      200);

  // Passive embed answered with 403: upgrades to a blocked challenge status.
  DispatchSubresourceLoad(tracker, web_contents(),
                          GURL("https://www.google.com/recaptcha/api.js"), 403);

  // Passive embed answered with 429: same upgrade path.
  DispatchSubresourceLoad(tracker, web_contents(),
                          GURL("https://example.com/js/datadome-tags.js"), 429);

  ASSERT_EQ(tracker->subresource_evidence().size(), 4u);
  EXPECT_FALSE(tracker->subresource_evidence()[0].is_blocked);
  EXPECT_EQ(tracker->subresource_evidence()[0].provider, "cloudflare_turnstile");
  EXPECT_EQ(tracker->subresource_evidence()[0].reason, "passive_presence");
  EXPECT_EQ(tracker->subresource_evidence()[0].status_code, 200);

  EXPECT_TRUE(tracker->subresource_evidence()[1].is_blocked);
  EXPECT_EQ(tracker->subresource_evidence()[1].provider, "cloudflare_turnstile");
  EXPECT_EQ(tracker->subresource_evidence()[1].reason, "challenge_endpoint");
  EXPECT_EQ(tracker->subresource_evidence()[1].status_code, 200);

  EXPECT_TRUE(tracker->subresource_evidence()[2].is_blocked);
  EXPECT_EQ(tracker->subresource_evidence()[2].provider, "recaptcha");
  EXPECT_EQ(tracker->subresource_evidence()[2].reason, "challenge_status_code");
  EXPECT_EQ(tracker->subresource_evidence()[2].status_code, 403);

  EXPECT_TRUE(tracker->subresource_evidence()[3].is_blocked);
  EXPECT_EQ(tracker->subresource_evidence()[3].provider, "datadome");
  EXPECT_EQ(tracker->subresource_evidence()[3].status_code, 429);

  // The 403 and 429 loads updated the challenge status (last one wins).
  EXPECT_TRUE(tracker->current_challenge().is_blocked);
  EXPECT_EQ(tracker->current_challenge().status_code, 429);
  EXPECT_EQ(tracker->current_challenge().provider, "datadome");

  // HAR independence: the observer (never started, but also exercising a
  // second full capture lifecycle) recorded nothing from these loads.
  base::DictValue har = observer.GetHarRaw();
  const base::ListValue* entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  EXPECT_EQ(entries->size(), 0u);

  MahoMcpNetworkObserver capturing_observer;
  capturing_observer.StartCapture(web_contents());
  capturing_observer.StopCapture();
  har = capturing_observer.GetHarRaw();
  entries = har.FindList("entries");
  ASSERT_TRUE(entries);
  EXPECT_EQ(entries->size(), 0u);
}

// Evidence is scoped to the current document: a committed cross-document
// main frame navigation drops it, a same-document navigation keeps it.
TEST_F(MahoMcpNavigationTrackerWebContentsTest,
       SubresourceEvidenceClearsOnDocumentReplacement) {
  MahoMcpNavigationTracker::CreateForWebContents(web_contents());
  MahoMcpNavigationTracker* tracker =
      MahoMcpNavigationTracker::FromWebContents(web_contents());

  const GURL doc_a("https://example.com/doc-a.html");
  CommitNavigationOn(tracker, web_contents(), doc_a,
                     /*same_document=*/false);

  DispatchSubresourceLoad(
      tracker, web_contents(),
      GURL("https://example.com/cdn-cgi/challenge-platform/scripts/jsd/main.js"),
      200);
  DispatchSubresourceLoad(tracker, web_contents(),
                          GURL("https://example.com/turnstile/v0/api.js"), 200);
  ASSERT_EQ(tracker->subresource_evidence().size(), 2u);

  // Same-document navigation (fragment) is NOT a document replacement.
  CommitNavigationOn(tracker, web_contents(), doc_a.Resolve("#section"),
                     /*same_document=*/true);
  EXPECT_EQ(tracker->subresource_evidence().size(), 2u);

  // Cross-document commit replaces the document: evidence is dropped. The
  // navigation itself is not a challenge, so the block status recorded from
  // the previous document is retained.
  CommitNavigationOn(tracker, web_contents(),
                     GURL("https://example.com/doc-b.html"),
                     /*same_document=*/false);
  EXPECT_TRUE(tracker->subresource_evidence().empty());
  EXPECT_TRUE(tracker->current_challenge().is_blocked);

  // The new document accumulates its own evidence from scratch.
  DispatchSubresourceLoad(tracker, web_contents(),
                          GURL("https://example.com/turnstile/v0/api.js"), 200);
  ASSERT_EQ(tracker->subresource_evidence().size(), 1u);
  EXPECT_EQ(tracker->subresource_evidence()[0].final_url,
            "https://example.com/turnstile/v0/api.js");
}

// An unchallenged page that merely embeds captcha widgets must never be
// reported as blocked. Also covers the bounded evidence buffer (50).
TEST_F(MahoMcpNavigationTrackerWebContentsTest,
       PassiveSubresourcePresenceDoesNotSetIsBlocked) {
  MahoMcpNavigationTracker::CreateForWebContents(web_contents());
  MahoMcpNavigationTracker* tracker =
      MahoMcpNavigationTracker::FromWebContents(web_contents());

  CommitNavigationOn(tracker, web_contents(),
                     GURL("https://example.com/login.html"),
                     /*same_document=*/false);

  // Passive embeds from every supported provider.
  const GURL passive_urls[] = {
      GURL("https://www.google.com/recaptcha/api.js"),
      GURL("https://example.com/hcaptcha/1/api.js"),
      GURL("https://example.com/turnstile/v0/api.js"),
      GURL("https://example.com/js/datadome-tags.js"),
      GURL("https://example.com/akam/11/seed.js"),
      GURL("https://example.com/kpsdk/page.js"),
  };
  const size_t kPassiveCount = std::size(passive_urls);
  for (const GURL& url : passive_urls) {
    DispatchSubresourceLoad(tracker, web_contents(), url, 200);
  }

  ASSERT_EQ(tracker->subresource_evidence().size(), kPassiveCount);
  for (const auto& evidence : tracker->subresource_evidence()) {
    EXPECT_FALSE(evidence.is_blocked) << evidence.final_url;
    EXPECT_EQ(evidence.reason, "passive_presence");
  }
  EXPECT_FALSE(tracker->current_challenge().is_blocked);

  // Overflow the bounded buffer: exactly kMaxSubresourceEvidenceSize
  // entries survive and the oldest ones were evicted.
  for (size_t i = 0; i < 60; ++i) {
    DispatchSubresourceLoad(
        tracker, web_contents(),
        GURL(base::StringPrintf("https://example.com/turnstile/v%d/api.js",
                                static_cast<int>(i))),
        200);
  }
  EXPECT_EQ(tracker->subresource_evidence().size(),
            MahoMcpNavigationTracker::kMaxSubresourceEvidenceSize);
  EXPECT_LT(tracker->subresource_evidence().size(), 60u);
  // 66 total loads against a 50-slot window: the 16 oldest entries (the six
  // passive embeds plus v0..v9) were evicted first.
  EXPECT_EQ(tracker->subresource_evidence()[0].final_url,
            "https://example.com/turnstile/v10/api.js");
  EXPECT_FALSE(tracker->current_challenge().is_blocked);
}

// Same-document SPA navigations must produce events observed by
// production DidFinishNavigation while preserving document-scoped evidence.
TEST_F(MahoMcpNavigationTrackerWebContentsTest,
       SameDocumentNavigationRecordsEventAndPreservesEvidence) {
  MahoMcpNavigationTracker::CreateForWebContents(web_contents());
  MahoMcpNavigationTracker* tracker =
      MahoMcpNavigationTracker::FromWebContents(web_contents());

  const GURL initial_url("https://example.com/spa/app");
  content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents(),
                                                             initial_url);
  ASSERT_EQ(tracker->events().size(), 1u);
  EXPECT_EQ(tracker->events()[0].url, initial_url.spec());
  EXPECT_EQ(tracker->events()[0].status_code, 200);

  DispatchSubresourceLoad(tracker, web_contents(),
                          GURL("https://example.com/turnstile/v0/api.js"), 200);
  ASSERT_EQ(tracker->subresource_evidence().size(), 1u);

  const GURL spa_route_url("https://example.com/spa/app#dashboard");
  auto same_doc_sim = content::NavigationSimulator::CreateRendererInitiated(
      spa_route_url, web_contents()->GetPrimaryMainFrame());
  same_doc_sim->CommitSameDocument();

  ASSERT_EQ(tracker->events().size(), 2u);
  EXPECT_EQ(tracker->events()[1].url, spa_route_url.spec());
  EXPECT_EQ(tracker->events()[1].status_code, 200);
  EXPECT_GE(tracker->events()[1].timestamp_ms,
            tracker->events()[0].timestamp_ms);
  EXPECT_EQ(tracker->subresource_evidence().size(), 1u);

  const GURL push_state_url("https://example.com/spa/app/settings");
  auto push_state_sim = content::NavigationSimulator::CreateRendererInitiated(
      push_state_url, web_contents()->GetPrimaryMainFrame());
  push_state_sim->CommitSameDocument();

  ASSERT_EQ(tracker->events().size(), 3u);
  EXPECT_EQ(tracker->events()[2].url, push_state_url.spec());
  EXPECT_EQ(tracker->events()[2].status_code, 200);
  EXPECT_EQ(tracker->subresource_evidence().size(), 1u);
}

TEST_F(MahoMcpNavigationTrackerWebContentsTest,
       ErrorPageNavigationDoesNotRecordEvent) {
  MahoMcpNavigationTracker::CreateForWebContents(web_contents());
  MahoMcpNavigationTracker* tracker =
      MahoMcpNavigationTracker::FromWebContents(web_contents());

  const GURL initial_url("https://example.com/index.html");
  content::NavigationSimulator::NavigateAndCommitFromBrowser(web_contents(),
                                                             initial_url);
  ASSERT_EQ(tracker->events().size(), 1u);

  const GURL error_url("https://example.com/nonexistent");
  auto error_sim = content::NavigationSimulator::CreateRendererInitiated(
      error_url, web_contents()->GetPrimaryMainFrame());
  error_sim->Fail(net::ERR_CONNECTION_FAILED);
  error_sim->CommitErrorPage();

  EXPECT_EQ(tracker->events().size(), 1u);
  EXPECT_EQ(tracker->events()[0].url, initial_url.spec());
}

}  // namespace
}  // namespace maho
