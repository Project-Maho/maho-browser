// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_json_rpc.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoMcpJsonRpcTest : public testing::Test {
 protected:
  MahoMcpJsonRpc framer_;
};

TEST_F(MahoMcpJsonRpcTest, ParseErrorOnBadJson) {
  std::string input = "not valid json at all\n";
  framer_.AppendData(input);

  auto response = framer_.TakeNextResponse();
  ASSERT_TRUE(response.has_value());

  std::optional<base::Value> parsed =
      base::JSONReader::Read(*response, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());

  const auto& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("jsonrpc"), "2.0");

  const auto* error = dict.FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32700);
  EXPECT_EQ(*error->FindString("message"), "Parse error");
}

TEST_F(MahoMcpJsonRpcTest, InvalidRequestOnMissingJsonRpc) {
  // Valid JSON but missing "jsonrpc" field.
  std::string input = R"({"method":"test","id":1})" "\n";
  framer_.AppendData(input);

  auto response = framer_.TakeNextResponse();
  ASSERT_TRUE(response.has_value());

  std::optional<base::Value> parsed =
      base::JSONReader::Read(*response, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* error = parsed->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32600);
  EXPECT_EQ(*error->FindString("message"), "Invalid Request");
}

TEST_F(MahoMcpJsonRpcTest, MethodNotFoundOnUnknown) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"nonexistent","id":42})" "\n";
  framer_.AppendData(input);

  auto messages = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(messages.has_value());
  EXPECT_EQ(messages->method, "nonexistent");
  EXPECT_EQ(messages->id.value(), base::Value(42));

  // Simulate the session replying with method-not-found.
  std::string error_response =
      framer_.BuildErrorResponse(messages->id, -32601, "Method not found");
  std::optional<base::Value> parsed =
      base::JSONReader::Read(error_response, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* error = parsed->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32601);
  EXPECT_EQ(*error->FindString("message"), "Method not found");
}

TEST_F(MahoMcpJsonRpcTest, InvalidParamsOnMissing) {
  // Valid JSON-RPC but missing required params for initialize.
  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1})" "\n";
  framer_.AppendData(input);

  auto messages = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(messages.has_value());
  EXPECT_EQ(messages->method, "initialize");
  EXPECT_FALSE(messages->params.has_value());

  // Session responds with invalid params.
  std::string error_response =
      framer_.BuildErrorResponse(messages->id, -32602, "Invalid params");
  std::optional<base::Value> parsed =
      base::JSONReader::Read(error_response, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* error = parsed->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32602);
  EXPECT_EQ(*error->FindString("message"), "Invalid params");
}

TEST_F(MahoMcpJsonRpcTest, ValidRequestResponseRoundtrip) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"tools/list","id":"abc","params":{}})" "\n";
  framer_.AppendData(input);

  auto message = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message->method, "tools/list");
  EXPECT_EQ(message->id.value(), base::Value("abc"));
  EXPECT_TRUE(message->params.has_value());
  EXPECT_TRUE(message->params->is_dict());

  // Build a success response.
  base::Value::Dict result;
  result.Set("tools", base::Value::List());
  std::string response =
      framer_.BuildSuccessResponse(message->id, base::Value(std::move(result)));

  std::optional<base::Value> parsed =
      base::JSONReader::Read(response, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());

  const auto& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("jsonrpc"), "2.0");
  EXPECT_EQ(*dict.FindString("id"), "abc");

  const auto* result_val = dict.FindDict("result");
  ASSERT_TRUE(result_val);
  EXPECT_TRUE(result_val->FindList("tools"));
}

TEST_F(MahoMcpJsonRpcTest, NotificationProducesNoResponse) {
  // A notification has no "id" field — no response should be generated.
  std::string input =
      R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{}})"
      "\n";
  framer_.AppendData(input);

  // Should not produce an auto-response (parse-error/invalid-request).
  auto response = framer_.TakeNextResponse();
  EXPECT_FALSE(response.has_value());

  // Should produce a parsed message with no id.
  auto message = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message->method, "notifications/cancelled");
  EXPECT_FALSE(message->id.has_value());
}

TEST_F(MahoMcpJsonRpcTest, MultipleMessagesInOneBuffer) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"a","id":1})" "\n"
      R"({"jsonrpc":"2.0","method":"b","id":2})" "\n";
  framer_.AppendData(input);

  auto msg1 = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(msg1->method, "a");

  auto msg2 = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(msg2.has_value());
  EXPECT_EQ(msg2->method, "b");

  auto msg3 = framer_.TakeNextParsedMessage();
  EXPECT_FALSE(msg3.has_value());
}

TEST_F(MahoMcpJsonRpcTest, PartialBufferAccumulation) {
  // First chunk: incomplete line.
  framer_.AppendData(R"({"jsonrpc":"2.0","met)");

  auto msg = framer_.TakeNextParsedMessage();
  EXPECT_FALSE(msg.has_value());

  // Second chunk completes the line.
  framer_.AppendData(R"(hod":"x","id":99})" "\n");

  msg = framer_.TakeNextParsedMessage();
  ASSERT_TRUE(msg.has_value());
  EXPECT_EQ(msg->method, "x");
  EXPECT_EQ(msg->id.value(), base::Value(99));
}

}  // namespace
}  // namespace maho
