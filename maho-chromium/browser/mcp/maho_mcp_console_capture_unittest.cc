// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_console_capture.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"

namespace maho {

TEST(MahoMcpConsoleCaptureTest, ConsoleLevelToString_Verbose) {
  EXPECT_EQ(MahoMcpConsoleCapture::ConsoleLevelToString(
                blink::mojom::ConsoleMessageLevel::kVerbose),
            "debug");
}

TEST(MahoMcpConsoleCaptureTest, ConsoleLevelToString_Info) {
  EXPECT_EQ(MahoMcpConsoleCapture::ConsoleLevelToString(
                blink::mojom::ConsoleMessageLevel::kInfo),
            "info");
}

TEST(MahoMcpConsoleCaptureTest, ConsoleLevelToString_Warning) {
  EXPECT_EQ(MahoMcpConsoleCapture::ConsoleLevelToString(
                blink::mojom::ConsoleMessageLevel::kWarning),
            "warn");
}

TEST(MahoMcpConsoleCaptureTest, ConsoleLevelToString_Error) {
  EXPECT_EQ(MahoMcpConsoleCapture::ConsoleLevelToString(
                blink::mojom::ConsoleMessageLevel::kError),
            "error");
}

TEST(MahoMcpConsoleCaptureTest, MaxMessageSizeConstant) {
  EXPECT_EQ(MahoMcpConsoleCapture::kMaxMessageSize, 4096u);
}

TEST(MahoMcpConsoleCaptureTest, MaxBufferSizeConstant) {
  EXPECT_EQ(MahoMcpConsoleCapture::kMaxBufferSize, 500u);
}

TEST(MahoMcpConsoleCaptureTest, ConsoleMessageStruct_DefaultInit) {
  MahoMcpConsoleCapture::ConsoleMessage msg;
  EXPECT_EQ(msg.line, 0);
  EXPECT_EQ(msg.timestamp_ms, 0);
  EXPECT_TRUE(msg.level.empty());
  EXPECT_TRUE(msg.message.empty());
  EXPECT_TRUE(msg.source_url.empty());
}

TEST(MahoMcpConsoleCaptureTest, ConsoleMessageStruct_CopyAndMove) {
  MahoMcpConsoleCapture::ConsoleMessage msg;
  msg.level = "warn";
  msg.message = "hi";
  msg.line = 42;
  msg.timestamp_ms = 123;

  MahoMcpConsoleCapture::ConsoleMessage copy = msg;
  EXPECT_EQ(copy.level, "warn");
  EXPECT_EQ(copy.line, 42);

  MahoMcpConsoleCapture::ConsoleMessage moved = std::move(msg);
  EXPECT_EQ(moved.level, "warn");
  EXPECT_EQ(moved.message, "hi");
  EXPECT_EQ(moved.timestamp_ms, 123);
}

}  // namespace maho
