// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_console_capture.h"

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"

namespace maho {

MahoMcpConsoleCapture::ConsoleMessage::ConsoleMessage() = default;
MahoMcpConsoleCapture::ConsoleMessage::~ConsoleMessage() = default;
MahoMcpConsoleCapture::ConsoleMessage::ConsoleMessage(const ConsoleMessage&) = default;
MahoMcpConsoleCapture::ConsoleMessage::ConsoleMessage(ConsoleMessage&&) noexcept = default;
MahoMcpConsoleCapture::ConsoleMessage& MahoMcpConsoleCapture::ConsoleMessage::operator=(const ConsoleMessage&) = default;
MahoMcpConsoleCapture::ConsoleMessage& MahoMcpConsoleCapture::ConsoleMessage::operator=(ConsoleMessage&&) noexcept = default;

MahoMcpConsoleCapture::MahoMcpConsoleCapture(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoMcpConsoleCapture>(*web_contents) {}

MahoMcpConsoleCapture::~MahoMcpConsoleCapture() = default;

// static
std::string MahoMcpConsoleCapture::ConsoleLevelToString(
    blink::mojom::ConsoleMessageLevel level) {
  switch (level) {
    case blink::mojom::ConsoleMessageLevel::kVerbose:
      return "debug";
    case blink::mojom::ConsoleMessageLevel::kInfo:
      return "info";
    case blink::mojom::ConsoleMessageLevel::kWarning:
      return "warn";
    case blink::mojom::ConsoleMessageLevel::kError:
      return "error";
    default:
      return "log";
  }
}

void MahoMcpConsoleCapture::OnDidAddMessageToConsole(
    content::RenderFrameHost* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id,
    const std::optional<std::u16string>& untrusted_stack_trace) {
  ConsoleMessage msg;
  msg.level = ConsoleLevelToString(log_level);
  msg.message = base::UTF16ToUTF8(message);
  if (msg.message.size() > kMaxMessageSize) {
    msg.message.resize(kMaxMessageSize);
  }
  msg.source_url = base::UTF16ToUTF8(source_id);
  msg.line = line_no;
  msg.timestamp_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();

  if (messages_.size() >= kMaxBufferSize) {
    messages_.erase(messages_.begin());
  }
  messages_.push_back(std::move(msg));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoMcpConsoleCapture);

}  // namespace maho
