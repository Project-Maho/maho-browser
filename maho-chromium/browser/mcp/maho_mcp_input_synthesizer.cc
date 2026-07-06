// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#include <cctype>

#include "base/time/time.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace maho {

namespace {

ui::AXNode* FindNodeById(ui::AXNode* root, ui::AXNodeID ax_id) {
  if (!root) return nullptr;
  if (root->id() == ax_id) return root;
  for (const auto& child : root->children()) {
    ui::AXNode* result = FindNodeById(child.get(), ax_id);
    if (result) return result;
  }
  return nullptr;
}

gfx::RectF GetAbsoluteBounds(ui::AXNode* node, ui::AXNode* root) {
  gfx::RectF bounds = node->data().relative_bounds.bounds;
  ui::AXNode* current = node;
  while (current && current != root) {
    ui::AXNodeID container_id = current->data().relative_bounds.offset_container_id;
    ui::AXNode* container = nullptr;
    if (container_id > 0) {
      container = FindNodeById(root, container_id);
    }
    if (!container) {
      container = current->parent();
    }
    if (container) {
      bounds.Offset(container->data().relative_bounds.bounds.OffsetFromOrigin());
    }
    current = container;
  }
  return bounds;
}

gfx::Rect GetNodeBounds(content::WebContents* wc, ui::AXNodeID ax_id) {
  ui::AXNode* root = wc->GetAccessibilityRootNode();
  if (!root) return gfx::Rect();
  ui::AXNode* node = FindNodeById(root, ax_id);
  if (!node) return gfx::Rect();
  
  gfx::RectF abs_rect = GetAbsoluteBounds(node, root);
  gfx::Rect view_rect = wc->GetRenderWidgetHostView()
                            ? wc->GetRenderWidgetHostView()->GetViewBounds()
                            : gfx::Rect();
  return gfx::Rect(view_rect.x() + abs_rect.x(),
                   view_rect.y() + abs_rect.y(),
                   abs_rect.width(),
                   abs_rect.height());
}

}  // namespace

// static
bool MahoMcpInputSynthesizer::Scroll(content::WebContents* wc,
                                     const std::string& direction,
                                     int pixels,
                                     std::optional<ui::AXNodeID> ax_id) {
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;

  gfx::Point widget_point(100, 100);
  gfx::Point screen_point(100, 100);

  if (ax_id.has_value()) {
    gfx::Rect bounds = GetNodeBounds(wc, ax_id.value());
    if (!bounds.IsEmpty()) {
      screen_point = bounds.CenterPoint();
      widget_point = screen_point - view->GetViewBounds().origin().OffsetFromOrigin();
    }
  }

  blink::WebMouseWheelEvent wheel_event(
      blink::WebInputEvent::Type::kMouseWheel,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  wheel_event.SetPositionInWidget(widget_point.x(), widget_point.y());
  wheel_event.SetPositionInScreen(screen_point.x(), screen_point.y());

  int delta_x = 0;
  int delta_y = 0;
  if (direction == "down") delta_y = -pixels;
  else if (direction == "up") delta_y = pixels;
  else if (direction == "left") delta_x = pixels;
  else if (direction == "right") delta_x = -pixels;

  wheel_event.delta_x = delta_x;
  wheel_event.delta_y = delta_y;
  wheel_event.phase = blink::WebMouseWheelEvent::kPhaseBegan;

  rwh->ForwardWheelEvent(wheel_event);

  blink::WebMouseWheelEvent end_event = wheel_event;
  end_event.delta_x = 0;
  end_event.delta_y = 0;
  end_event.phase = blink::WebMouseWheelEvent::kPhaseEnded;
  rwh->ForwardWheelEvent(end_event);

  return true;
}

// static
bool MahoMcpInputSynthesizer::Hover(content::WebContents* wc, ui::AXNodeID ax_id) {
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;

  gfx::Rect bounds = GetNodeBounds(wc, ax_id);
  if (bounds.IsEmpty()) return false;
  gfx::Point screen_point = bounds.CenterPoint();
  gfx::Point widget_point = screen_point - view->GetViewBounds().origin().OffsetFromOrigin();

  blink::WebMouseEvent mouse_event(
      blink::WebInputEvent::Type::kMouseMove,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_event.SetPositionInWidget(widget_point.x(), widget_point.y());
  mouse_event.SetPositionInScreen(screen_point.x(), screen_point.y());
  
  rwh->ForwardMouseEvent(mouse_event);
  return true;
}

// static
ui::KeyboardCode MahoMcpInputSynthesizer::KeyStringToKeyCode(
    const std::string& key) {
  if (key == "Enter") return ui::VKEY_RETURN;
  if (key == "Tab") return ui::VKEY_TAB;
  if (key == "Escape") return ui::VKEY_ESCAPE;
  if (key == "Backspace") return ui::VKEY_BACK;
  if (key == "Delete") return ui::VKEY_DELETE;
  if (key == "ArrowUp") return ui::VKEY_UP;
  if (key == "ArrowDown") return ui::VKEY_DOWN;
  if (key == "ArrowLeft") return ui::VKEY_LEFT;
  if (key == "ArrowRight") return ui::VKEY_RIGHT;
  if (key == "Home") return ui::VKEY_HOME;
  if (key == "End") return ui::VKEY_END;
  if (key == "PageUp") return ui::VKEY_PRIOR;
  if (key == "PageDown") return ui::VKEY_NEXT;
  if (key == " " || key == "Space") return ui::VKEY_SPACE;
  if (key == "F1") return ui::VKEY_F1;
  if (key == "F2") return ui::VKEY_F2;
  if (key == "F3") return ui::VKEY_F3;
  if (key == "F4") return ui::VKEY_F4;
  if (key == "F5") return ui::VKEY_F5;
  if (key == "F6") return ui::VKEY_F6;
  if (key == "F7") return ui::VKEY_F7;
  if (key == "F8") return ui::VKEY_F8;
  if (key == "F9") return ui::VKEY_F9;
  if (key == "F10") return ui::VKEY_F10;
  if (key == "F11") return ui::VKEY_F11;
  if (key == "F12") return ui::VKEY_F12;
  if (key.size() == 1) {
    char c = std::toupper(key[0]);
    if (c >= 'A' && c <= 'Z') {
      return static_cast<ui::KeyboardCode>(ui::VKEY_A + (c - 'A'));
    } else if (c >= '0' && c <= '9') {
      return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (c - '0'));
    }
  }
  return ui::VKEY_UNKNOWN;
}

// static
int MahoMcpInputSynthesizer::ModifierListToWebModifiers(
    const std::vector<std::string>& modifiers) {
  int web_modifiers = blink::WebInputEvent::kNoModifiers;
  for (const auto& mod : modifiers) {
    if (mod == "shift") web_modifiers |= blink::WebInputEvent::kShiftKey;
    else if (mod == "ctrl") web_modifiers |= blink::WebInputEvent::kControlKey;
    else if (mod == "alt") web_modifiers |= blink::WebInputEvent::kAltKey;
    else if (mod == "meta") web_modifiers |= blink::WebInputEvent::kMetaKey;
  }
  return web_modifiers;
}

// static
bool MahoMcpInputSynthesizer::KeyPress(content::WebContents* wc,
                                       const std::string& key,
                                       const std::vector<std::string>& modifiers) {
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;

  ui::KeyboardCode key_code = KeyStringToKeyCode(key);
  int web_modifiers = ModifierListToWebModifiers(modifiers);

  // RawKeyDown
  blink::WebKeyboardEvent key_down(
      blink::WebInputEvent::Type::kRawKeyDown, web_modifiers,
      base::TimeTicks::Now());
  key_down.windows_key_code = key_code;
  rwh->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(key_down, wc->GetNativeView()));

  // Char
  if (key.size() == 1 || key == "Enter") {
    blink::WebKeyboardEvent key_char(
        blink::WebInputEvent::Type::kChar, web_modifiers,
        base::TimeTicks::Now());
    key_char.windows_key_code = key_code;
    if (key == "Enter") {
      key_char.text[0] = '\r';
      key_char.unmodified_text[0] = '\r';
    } else {
      key_char.text[0] = key[0];
      key_char.unmodified_text[0] = key[0];
    }
    rwh->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(key_char, wc->GetNativeView()));
  }

  // KeyUp
  blink::WebKeyboardEvent key_up(
      blink::WebInputEvent::Type::kKeyUp, web_modifiers,
      base::TimeTicks::Now());
  key_up.windows_key_code = key_code;
  rwh->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(key_up, wc->GetNativeView()));

  return true;
}

}  // namespace maho
