// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoMcpSessionTest : public testing::Test {
 protected:
  void SetUp() override {
    // Create session with matching UID (simulates same-user connection).
    session_ = std::make_unique<MahoMcpSession>(getuid());
  }

  std::unique_ptr<MahoMcpSession> session_;
};

TEST_F(MahoMcpSessionTest, RejectsToolCallBeforeInitialize) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"test"}})"
      "\n";

  auto responses = session_->ProcessData(input);
  ASSERT_FALSE(responses.empty());

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* error = parsed->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32600);
  EXPECT_TRUE(error->FindString("message")->find("initialize expected") !=
              std::string::npos);

  EXPECT_EQ(session_->state(), MahoMcpSession::State::kClosed);
}

TEST_F(MahoMcpSessionTest, InitializeSucceedsForSameUid) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";

  auto responses = session_->ProcessData(input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());

  const auto& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("jsonrpc"), "2.0");
  EXPECT_FALSE(dict.FindDict("error"));

  const auto* result = dict.FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(session_->state(), MahoMcpSession::State::kActive);
}

TEST_F(MahoMcpSessionTest, InitializeReturnsCapabilities) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":"init-1",)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";

  auto responses = session_->ProcessData(input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);

  // Verify serverInfo.
  const auto* server_info = result->FindDict("serverInfo");
  ASSERT_TRUE(server_info);
  EXPECT_EQ(*server_info->FindString("name"), "maho-browser");
  EXPECT_EQ(*server_info->FindString("version"), "0.4.1");

  // Verify protocolVersion.
  EXPECT_EQ(*result->FindString("protocolVersion"), "2025-03-26");

  // Verify capabilities contains tools.
  const auto* capabilities = result->FindDict("capabilities");
  ASSERT_TRUE(capabilities);
  EXPECT_TRUE(capabilities->FindDict("tools"));
}

TEST_F(MahoMcpSessionTest, ToolsListAfterInitialize) {
  // First: initialize.
  std::string init_input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";
  session_->ProcessData(init_input);
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  // Then: tools/list.
  std::string tools_input =
      R"({"jsonrpc":"2.0","method":"tools/list","id":2})" "\n";
  auto responses = session_->ProcessData(tools_input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);
  // tools/list returns a "tools" array (empty for now — Wave 2A adds tools).
  EXPECT_TRUE(result->FindList("tools"));
}

TEST_F(MahoMcpSessionTest, RejectsPeerUidMismatch) {
  auto foreign_session = std::make_unique<MahoMcpSession>(getuid() + 1);

  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";

  auto responses = foreign_session->ProcessData(input);
  EXPECT_EQ(foreign_session->state(), MahoMcpSession::State::kClosed);
}

// D1 allowed_domains: helper to drive a session through initialize
// followed by an arbitrary payload, returning the LAST response parsed.
class MahoMcpSessionAllowedDomainsTest : public MahoMcpSessionTest {
 protected:
  void InitializeSession() {
    std::string init =
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n";
    (void)session_->ProcessData(init);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  base::Value CallTool(const std::string& payload) {
    auto responses = session_->ProcessData(payload);
    EXPECT_FALSE(responses.empty());
    if (responses.empty()) return base::Value();
    std::optional<base::Value> parsed =
        base::JSONReader::Read(responses.back(), base::JSON_PARSE_RFC);
    if (!parsed.has_value()) return base::Value();
    return std::move(*parsed);
  }
};

TEST_F(MahoMcpSessionAllowedDomainsTest, NavigateBlockedWhenEmptyAllowlist) {
  InitializeSession();
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://example.com/"}}})" "\n");
  const auto* error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  const auto* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(*data->FindString("status"), "domain_approval_required");
}

TEST_F(MahoMcpSessionAllowedDomainsTest, SetAllowedDomainsThenNavigateOk) {
  InitializeSession();
  base::Value set_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["example.com"]}}})" "\n");
  const auto* set_result = set_resp.GetDict().FindDict("result");
  ASSERT_TRUE(set_result);
  EXPECT_EQ(set_result->FindInt("count").value(), 1);

  base::Value nav_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://example.com/foo"}}})" "\n");
  const auto* nav_result = nav_resp.GetDict().FindDict("result");
  ASSERT_TRUE(nav_result);
  EXPECT_EQ(nav_result->FindBool("navigated").value(), true);
}

TEST_F(MahoMcpSessionAllowedDomainsTest, WildcardMatchesSubdomainAndBare) {
  InitializeSession();
  (void)CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["*.github.com"]}}})" "\n");

  base::Value r1 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://api.github.com/repos"}}})" "\n");
  ASSERT_TRUE(r1.GetDict().FindDict("result"));

  base::Value r2 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://github.com/"}}})" "\n");
  ASSERT_TRUE(r2.GetDict().FindDict("result"));
}

TEST_F(MahoMcpSessionAllowedDomainsTest, DisallowedHostBlockedAfterSet) {
  InitializeSession();
  (void)CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["example.com"]}}})" "\n");

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://evil.example.org/"}}})" "\n");
  const auto* error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  const auto* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(*data->FindString("status"), "domain_not_allowed");
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithUrlEnforced) {
  InitializeSession();
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_tab_new",)"
      R"("arguments":{"url":"https://example.com/"}}})" "\n");
  const auto* error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithoutUrlUnaffected) {
  InitializeSession();
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_tab_new",)"
      R"("arguments":{}}})" "\n");
  ASSERT_TRUE(resp.GetDict().FindDict("result"));
}

class MahoMcpSessionNewFeaturesTest : public MahoMcpSessionAllowedDomainsTest {
 protected:
  void SetUp() override {
    MahoMcpSessionAllowedDomainsTest::SetUp();
    InitializeSession();
  }
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_RingBuffer_HoldsUpToLimit) {
  for (int i = 0; i < 501; ++i) {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "message " + std::to_string(i);
    msg.source_url = "https://example.com";
    msg.line = i;
    msg.timestamp_ms = 1000 + i;
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0,"limit":500}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 500u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "message 1");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_LevelFilter_ReturnsMatchingOnly) {
  {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "log message";
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }
  {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "error";
    msg.message = "error message";
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0,"level_filter":["error"]}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "error message");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_SinceTimestamp_ReturnsAfterCutoff) {
  for (int i = 0; i < 3; ++i) {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "message " + std::to_string(i);
    msg.timestamp_ms = 1000 + i;
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0,"since_timestamp_ms":1001}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "message 2");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_Firewall_ScrubsAuthorizationInMessage) {
  MahoMcpSession::ConsoleMessage msg;
  msg.level = "log";
  msg.message = "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMiIsInVzZXIiOiJhZG1pbiJ9.signature";
  session_->AddConsoleMessageForTesting(0, std::move(msg));

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "Bearer [REDACTED_JWT]");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_Firewall_ScrubsAccessTokenInUrl) {
  MahoMcpSession::ConsoleMessage msg;
  msg.level = "log";
  msg.message = "Loaded resource";
  msg.source_url = "https://example.com/api?access_token=super-secret-token";
  session_->AddConsoleMessageForTesting(0, std::move(msg));

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("source_url"), "https://example.com/api?access_token=[REDACTED]");
}

TEST_F(MahoMcpSessionNewFeaturesTest, WaitForNavigation_ReturnsImmediately) {
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_wait_for_navigation",)"
      R"("arguments":{"tab_id":0}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result->FindString("url"), "about:blank");
}

TEST_F(MahoMcpSessionNewFeaturesTest, WaitForNavigation_TimesOutForUnknownTab) {
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_wait_for_navigation",)"
      R"("arguments":{"tab_id":999}}})" "\n");
      
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("timeout").value());
}

TEST_F(MahoMcpSessionNewFeaturesTest, TabSwitch_UpdatesActiveTab) {
  (void)CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["example.com"]}}})" "\n");

  base::Value new_tab_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_tab_new",)"
      R"("arguments":{"url":"https://example.com/"}}})" "\n");
  const auto* new_tab_result = new_tab_resp.GetDict().FindDict("result");
  ASSERT_TRUE(new_tab_result);
  const auto* tab = new_tab_result->FindDict("tab");
  ASSERT_TRUE(tab);
  int new_tab_id = tab->FindInt("id").value();

  base::Value switch_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"browser_tab_switch",)"
      R"("arguments":{"tab_id":0}}})" "\n");
  const auto* switch_result = switch_resp.GetDict().FindDict("result");
  ASSERT_TRUE(switch_result);
  EXPECT_TRUE(switch_result->FindBool("activated").value());
  EXPECT_EQ(switch_result->FindInt("previous_tab_id").value(), new_tab_id);

  base::Value get_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":5,)"
      R"("params":{"name":"browser_tab_get",)"
      R"("arguments":{"tab_id":0}}})" "\n");
  const auto* get_result = get_resp.GetDict().FindDict("result");
  ASSERT_TRUE(get_result);
  EXPECT_TRUE(get_result->FindBool("is_active").value());
}

TEST_F(MahoMcpSessionNewFeaturesTest, SetViewportSize_ValidAndOutOfBounds) {
  base::Value resp1 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_viewport_size",)"
      R"("arguments":{"width_px":375,"height_px":667}}})" "\n");
  const auto* r1 = resp1.GetDict().FindDict("result");
  ASSERT_TRUE(r1);
  EXPECT_EQ(r1->FindInt("width").value(), 375);
  EXPECT_EQ(r1->FindInt("height").value(), 667);

  base::Value resp2 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_set_viewport_size",)"
      R"("arguments":{"width_px":99,"height_px":667}}})" "\n");
  const auto* error = resp2.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32602);
}

TEST_F(MahoMcpSessionNewFeaturesTest, InteractiveTools_Scroll_Hover_KeyPress) {
  base::Value scroll_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_scroll",)"
      R"("arguments":{"direction":"down","pixels":200}}})" "\n");
  const auto* scroll_result = scroll_resp.GetDict().FindDict("result");
  ASSERT_TRUE(scroll_result);
  EXPECT_TRUE(scroll_result->FindBool("scrolled").value());
  EXPECT_EQ(scroll_result->FindInt("new_scroll_y").value(), 200);

  base::Value key_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_key_press",)"
      R"("arguments":{"key":"Enter","modifiers":["shift"]}}})" "\n");
  const auto* key_result = key_resp.GetDict().FindDict("result");
  ASSERT_TRUE(key_result);
  EXPECT_TRUE(key_result->FindBool("key_pressed").value());
  EXPECT_EQ(*key_result->FindString("key"), "Enter");

  base::Value invalid_key_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"browser_key_press",)"
      R"("arguments":{"key":"InvalidKeyName"}}})" "\n");
  const auto* key_error = invalid_key_resp.GetDict().FindDict("error");
  ASSERT_TRUE(key_error);
  EXPECT_EQ(key_error->FindInt("code").value(), -32602);
}

}  // namespace
}  // namespace maho
