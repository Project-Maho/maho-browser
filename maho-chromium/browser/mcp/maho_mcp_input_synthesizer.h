// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_

#include <string>
#include <vector>
#include <optional>

#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace content {
class WebContents;
}

namespace maho {

class MahoMcpInputSynthesizer {
 public:
  static bool Scroll(content::WebContents* wc,
                     const std::string& direction,
                     int pixels,
                     std::optional<ui::AXNodeID> ax_id);

  static bool Hover(content::WebContents* wc, ui::AXNodeID ax_id);
  static bool HoverAt(content::WebContents* wc, int x_css, int y_css);

  static bool KeyPress(content::WebContents* wc,
                       const std::string& key,
                       const std::vector<std::string>& modifiers);

  static ui::KeyboardCode KeyStringToKeyCode(const std::string& key);

  static int ModifierListToWebModifiers(
      const std::vector<std::string>& modifiers);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
