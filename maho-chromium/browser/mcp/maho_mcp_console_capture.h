// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_CONSOLE_CAPTURE_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_CONSOLE_CAPTURE_H_

#include <string>
#include <vector>

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom-forward.h"

namespace maho {

class MahoMcpConsoleCapture : public content::WebContentsObserver,
                              public content::WebContentsUserData<MahoMcpConsoleCapture> {
 public:
  struct ConsoleMessage {
    ConsoleMessage();
    ~ConsoleMessage();
    ConsoleMessage(const ConsoleMessage&);
    ConsoleMessage(ConsoleMessage&&) noexcept;
    ConsoleMessage& operator=(const ConsoleMessage&);
    ConsoleMessage& operator=(ConsoleMessage&&) noexcept;

    std::string level;
    std::string message;
    std::string source_url;
    int line = 0;
    int64_t timestamp_ms = 0;
  };

  ~MahoMcpConsoleCapture() override;

  const std::vector<ConsoleMessage>& messages() const { return messages_; }
  void Clear() { messages_.clear(); }

  static constexpr size_t kMaxMessageSize = 4096;
  static constexpr size_t kMaxBufferSize = 500;

  static std::string ConsoleLevelToString(
      blink::mojom::ConsoleMessageLevel level);

  // content::WebContentsObserver:
  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override;

 private:
  explicit MahoMcpConsoleCapture(content::WebContents* web_contents);
  friend class content::WebContentsUserData<MahoMcpConsoleCapture>;

  std::vector<ConsoleMessage> messages_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_CONSOLE_CAPTURE_H_
