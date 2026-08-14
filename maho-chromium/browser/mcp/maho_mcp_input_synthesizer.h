// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_

#include <string>
#include <vector>
#include <optional>

#include "maho/browser/ui/views/maho_action_marker_service.h"
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
  static bool HoverAt(content::WebContents* wc,
                      int x_css,
                      int y_css,
                      bool sensitive = true);

  // Dispatch a trusted left-button click at CSS viewport coordinates by
  // forwarding real mouse move/down/up events. Blink expands the trusted
  // press+release into the full pointerdown -> mousedown -> pointerup ->
  // mouseup -> click sequence, which custom (React / pointer-event) widgets
  // require and synthetic JS MouseEvents (isTrusted=false) do not trigger.
  static bool ClickAt(
      content::WebContents* wc,
      int x_css,
      int y_css,
      const std::vector<std::string>& modifiers,
      // Unknown sensitivity fails closed to a position-only marker. Callers
      // with revalidated non-sensitive metadata may pass false.
      bool sensitive = true,
      MahoActionMarkerService::Kind marker_kind =
          MahoActionMarkerService::Kind::kClick);

  // Type text by forwarding a trusted key sequence (RawKeyDown + Char + KeyUp)
  // per character. Framework-controlled inputs (e.g. React) require trusted
  // keystrokes to fire onChange; assigning element.value synthetically is
  // reverted on the next render. The caller is responsible for focusing and
  // clearing the target field first.
  static bool TypeText(content::WebContents* wc, const std::string& text);

  static bool KeyPress(content::WebContents* wc,
                       const std::string& key,
                       const std::vector<std::string>& modifiers);

  static ui::KeyboardCode KeyStringToKeyCode(const std::string& key);

  static int ModifierListToWebModifiers(
      const std::vector<std::string>& modifiers);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
