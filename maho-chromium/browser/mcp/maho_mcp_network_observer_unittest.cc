// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_network_observer.h"

#include <string>
#include <vector>

#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

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
  entry.request_headers.Set("Accept", "application/json");
  entry.response_headers.Set("Content-Type", "application/json");
  observer.TestOnly_AddEntry(std::move(entry));

  base::DictValue har = observer.GetHarRaw();
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
  observer.TestOnly_AddEntry(std::move(entry));

  Redacted<base::DictValue> har = observer.GetHar();
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
  observer.TestOnly_AddEntry(std::move(entry));

  Redacted<base::DictValue> har = observer.GetHar();
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
