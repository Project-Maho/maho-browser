// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#include <cctype>

#ifndef MAHO_STANDALONE_TEST
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/task/sequenced_task_runner.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/event_constants.h"
#endif

namespace maho {

#ifndef MAHO_STANDALONE_TEST
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

content::RenderWidgetHost* GetTargetRenderWidgetHost(content::WebContents* wc) {
  if (!wc) return nullptr;
  if (content::RenderFrameHost* focused = wc->GetFocusedFrame()) {
    if (content::RenderWidgetHost* rwh = focused->GetRenderWidgetHost()) {
      return rwh;
    }
  }
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  return view ? view->GetRenderWidgetHost() : nullptr;
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
bool MahoMcpInputSynthesizer::HoverAt(content::WebContents* wc,
                                      int x_css,
                                      int y_css,
                                      bool sensitive) {
  if (!wc) return false;
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;
  content::RenderFrameHost* primary = wc->GetPrimaryMainFrame();
  if (!primary) return false;

  const content::GlobalRenderFrameHostId primary_frame_id =
      primary->GetGlobalId();
  gfx::Point widget_point(x_css, y_css);
  gfx::Point screen_point = widget_point + view->GetViewBounds().origin().OffsetFromOrigin();

  blink::WebMouseEvent mouse_event(
      blink::WebInputEvent::Type::kMouseMove,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_event.SetPositionInWidget(widget_point.x(), widget_point.y());
  mouse_event.SetPositionInScreen(screen_point.x(), screen_point.y());
  
  rwh->ForwardMouseEvent(mouse_event);
  MahoActionMarkerService::ShowForRevalidatedTarget(
      wc, {.viewport_point = gfx::PointF(x_css, y_css),
           .kind = MahoActionMarkerService::Kind::kHover,
           .sensitive = sensitive,
           .primary_frame_id = primary_frame_id});
  return true;
}

// static
bool MahoMcpInputSynthesizer::ClickAt(
    content::WebContents* wc,
    int x_css,
    int y_css,
    const std::vector<std::string>& modifiers,
    bool sensitive,
    MahoActionMarkerService::Kind marker_kind) {
  if (!wc) return false;
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) return false;
  content::RenderWidgetHost* rwh = view->GetRenderWidgetHost();
  if (!rwh) return false;
  content::RenderFrameHost* primary = wc->GetPrimaryMainFrame();
  if (!primary) return false;

  const content::GlobalRenderFrameHostId primary_frame_id =
      primary->GetGlobalId();
  const int base_modifiers = ModifierListToWebModifiers(modifiers);
  gfx::Point widget_point(x_css, y_css);
  gfx::Point screen_point =
      widget_point + view->GetViewBounds().origin().OffsetFromOrigin();

  auto build = [&](blink::WebInputEvent::Type type, int modifiers_mask,
                   blink::WebMouseEvent::Button button, int click_count) {
    blink::WebMouseEvent event(type, modifiers_mask, base::TimeTicks::Now());
    event.button = button;
    event.click_count = click_count;
    event.SetPositionInWidget(widget_point.x(), widget_point.y());
    event.SetPositionInScreen(screen_point.x(), screen_point.y());
    return event;
  };

  // Move the pointer over the target first so hover/pointer state is correct,
  // then a trusted press + release. Blink expands this into the full
  // pointerdown -> mousedown -> pointerup -> mouseup -> click sequence.
  rwh->ForwardMouseEvent(build(blink::WebInputEvent::Type::kMouseMove,
                               base_modifiers,
                               blink::WebMouseEvent::Button::kNoButton, 0));
  rwh->ForwardMouseEvent(build(
      blink::WebInputEvent::Type::kMouseDown,
      base_modifiers | blink::WebInputEvent::kLeftButtonDown,
      blink::WebMouseEvent::Button::kLeft, 1));
  rwh->ForwardMouseEvent(build(blink::WebInputEvent::Type::kMouseUp,
                               base_modifiers,
                               blink::WebMouseEvent::Button::kLeft, 1));
  MahoActionMarkerService::ShowForRevalidatedTarget(
      wc, {.viewport_point = gfx::PointF(x_css, y_css),
           .kind = marker_kind,
           .sensitive = sensitive,
           .primary_frame_id = primary_frame_id});
  return true;
}

// static
std::vector<TypingKeyUnit> MahoMcpInputSynthesizer::Utf8ToTypingUnits(
    const std::string& text) {
  std::u16string u16 = base::UTF8ToUTF16(text);
  std::vector<TypingKeyUnit> units;
  units.reserve(u16.size());
  for (size_t i = 0; i < u16.size(); ++i) {
    char16_t c1 = u16[i];
    TypingKeyUnit unit;
    if (c1 >= 0xD800 && c1 <= 0xDBFF && i + 1 < u16.size()) {
      char16_t c2 = u16[i + 1];
      if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
        unit.code_point = 0x10000 +
                          ((static_cast<char32_t>(c1) - 0xD800) << 10) +
                          (static_cast<char32_t>(c2) - 0xDC00);
        unit.utf16.push_back(c1);
        unit.utf16.push_back(c2);
        unit.utf8 = base::UTF16ToUTF8(unit.utf16);
        units.push_back(std::move(unit));
        ++i;
        continue;
      }
    }
    unit.code_point = static_cast<char32_t>(c1);
    unit.utf16.push_back(c1);
    unit.utf8 = base::UTF16ToUTF8(unit.utf16);
    units.push_back(std::move(unit));
  }
  return units;
}

// static
bool MahoMcpInputSynthesizer::TypeText(content::WebContents* wc,
                                       const std::string& text) {
  content::RenderWidgetHost* rwh = GetTargetRenderWidgetHost(wc);
  if (!rwh) return false;

  std::vector<TypingKeyUnit> units = Utf8ToTypingUnits(text);
  for (const auto& unit : units) {
    if (unit.code_point < 0x80) {
      char c = static_cast<char>(unit.code_point);
      ui::KeyboardCode key_code = ui::VKEY_UNKNOWN;
      const char upper = static_cast<char>(std::toupper(
          static_cast<unsigned char>(c)));
      if (upper >= 'A' && upper <= 'Z') {
        key_code = static_cast<ui::KeyboardCode>(ui::VKEY_A + (upper - 'A'));
      } else if (c >= '0' && c <= '9') {
        key_code = static_cast<ui::KeyboardCode>(ui::VKEY_0 + (c - '0'));
      } else if (c == ' ') {
        key_code = ui::VKEY_SPACE;
      }
      const ui::DomCode dom_code = ui::UsLayoutKeyboardCodeToDomCode(key_code);
      const int dom_key =
          static_cast<int>(ui::DomKey::FromCharacter(unit.code_point));

      blink::WebKeyboardEvent key_down(blink::WebInputEvent::Type::kRawKeyDown,
                                       blink::WebInputEvent::kNoModifiers,
                                       base::TimeTicks::Now());
      key_down.windows_key_code = key_code;
      key_down.dom_code = static_cast<int>(dom_code);
      key_down.dom_key = dom_key;
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(key_down, wc->GetNativeView()));

      blink::WebKeyboardEvent key_char(blink::WebInputEvent::Type::kChar,
                                       blink::WebInputEvent::kNoModifiers,
                                       base::TimeTicks::Now());
      key_char.windows_key_code = key_code;
      key_char.dom_code = static_cast<int>(dom_code);
      key_char.dom_key = dom_key;
      key_char.text[0] = static_cast<char16_t>(c);
      key_char.unmodified_text[0] = static_cast<char16_t>(c);
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(key_char, wc->GetNativeView()));

      blink::WebKeyboardEvent key_up(blink::WebInputEvent::Type::kKeyUp,
                                     blink::WebInputEvent::kNoModifiers,
                                     base::TimeTicks::Now());
      key_up.windows_key_code = key_code;
      key_up.dom_code = static_cast<int>(dom_code);
      key_up.dom_key = dom_key;
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(key_up, wc->GetNativeView()));
    } else {
      const int dom_key =
          static_cast<int>(ui::DomKey::FromCharacter(unit.code_point));

      blink::WebKeyboardEvent key_down(blink::WebInputEvent::Type::kRawKeyDown,
                                       blink::WebInputEvent::kNoModifiers,
                                       base::TimeTicks::Now());
      key_down.windows_key_code = 0;
      key_down.dom_code = 0;
      key_down.dom_key = dom_key;
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(key_down, wc->GetNativeView()));

      blink::WebKeyboardEvent key_char(blink::WebInputEvent::Type::kChar,
                                       blink::WebInputEvent::kNoModifiers,
                                       base::TimeTicks::Now());
      key_char.windows_key_code = 0;
      key_char.dom_code = 0;
      key_char.dom_key = dom_key;
      for (size_t k = 0;
           k < unit.utf16.size() &&
           k < blink::WebKeyboardEvent::kTextLengthCap;
           ++k) {
        key_char.text[k] = unit.utf16[k];
        key_char.unmodified_text[k] = unit.utf16[k];
      }
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(key_char, wc->GetNativeView()));

      blink::WebKeyboardEvent key_up(blink::WebInputEvent::Type::kKeyUp,
                                     blink::WebInputEvent::kNoModifiers,
                                     base::TimeTicks::Now());
      key_up.windows_key_code = 0;
      key_up.dom_code = 0;
      key_up.dom_key = dom_key;
      rwh->ForwardKeyboardEvent(
          input::NativeWebKeyboardEvent(key_up, wc->GetNativeView()));
    }
  }
  return true;
}

namespace {

class AsyncTypingSession {
 public:
  AsyncTypingSession(base::WeakPtr<content::WebContents> wc,
                     std::string text,
                     TypingPacingPolicy policy,
                     base::OnceCallback<void(bool)> callback)
      : web_contents_(std::move(wc)),
        units_(MahoMcpInputSynthesizer::Utf8ToTypingUnits(text)),
        policy_(std::move(policy)),
        prng_(policy_.CreatePrng()),
        callback_(std::move(callback)) {}

  void Start() {
    Step();
  }

 private:
  void Step() {
    if (!web_contents_) {
      Finish(false);
      return;
    }

    if (index_ >= units_.size()) {
      Finish(true);
      return;
    }

    const auto& current_unit = units_[index_];
    if (!MahoMcpInputSynthesizer::TypeText(web_contents_.get(),
                                           current_unit.utf8)) {
      Finish(false);
      return;
    }

    index_++;
    if (index_ >= units_.size()) {
      Finish(true);
      return;
    }

    char32_t prev_cp = current_unit.code_point;
    char32_t next_cp = units_[index_].code_point;
    int32_t delay_ms = policy_.delay_for(prev_cp, next_cp, prng_);

    if (delay_ms <= 0) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&AsyncTypingSession::Step, weak_factory_.GetWeakPtr()));
    } else {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&AsyncTypingSession::Step, weak_factory_.GetWeakPtr()),
          base::Milliseconds(delay_ms));
    }
  }

  void Finish(bool success) {
    if (callback_) {
      std::move(callback_).Run(success);
    }
    delete this;
  }

  base::WeakPtr<content::WebContents> web_contents_;
  std::vector<TypingKeyUnit> units_;
  TypingPacingPolicy policy_;
  SplitMix64Prng prng_;
  size_t index_{0};
  base::OnceCallback<void(bool)> callback_;
  base::WeakPtrFactory<AsyncTypingSession> weak_factory_{this};
};

}  // namespace

void MahoMcpInputSynthesizer::TypeTextAsync(
    content::WebContents* wc,
    const std::string& text,
    TypingOptions options,
    base::OnceCallback<void(bool)> callback) {
  if (!options.pacing_policy.validate()) {
    if (base::SequencedTaskRunner::HasCurrentDefault()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done) {
                std::move(done).Run(false);
              },
              std::move(callback)));
    } else {
      std::move(callback).Run(false);
    }
    return;
  }

  if (text.empty()) {
    if (base::SequencedTaskRunner::HasCurrentDefault()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done) {
                std::move(done).Run(true);
              },
              std::move(callback)));
    } else {
      std::move(callback).Run(true);
    }
    return;
  }

  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    const bool dispatched = TypeText(wc, text);
    std::move(callback).Run(dispatched);
    return;
  }

  auto* session = new AsyncTypingSession(
      wc ? wc->GetWeakPtr() : nullptr,
      text,
      options.pacing_policy,
      std::move(callback));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](AsyncTypingSession* pending_session) {
            pending_session->Start();
          },
          base::Unretained(session)));
}
#endif

#ifdef MAHO_STANDALONE_TEST
// static
std::vector<TypingKeyUnit> MahoMcpInputSynthesizer::Utf8ToTypingUnits(
    const std::string& text) {
  std::vector<TypingKeyUnit> units;
  size_t i = 0;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    char32_t cp = 0;
    size_t extra = 0;
    if (c < 0x80) {
      cp = c;
      extra = 0;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = text.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    if (i + extra >= text.size()) {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = text.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    bool valid = true;
    for (size_t j = 1; j <= extra; ++j) {
      unsigned char next = static_cast<unsigned char>(text[i + j]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (next & 0x3F);
    }

    if (!valid) {
      TypingKeyUnit unit;
      unit.code_point = c;
      unit.utf16.push_back(static_cast<char16_t>(c));
      unit.utf8 = text.substr(i, 1);
      units.push_back(std::move(unit));
      ++i;
      continue;
    }

    TypingKeyUnit unit;
    unit.code_point = cp;
    unit.utf8 = text.substr(i, 1 + extra);
    if (cp <= 0xFFFF) {
      unit.utf16.push_back(static_cast<char16_t>(cp));
    } else if (cp <= 0x10FFFF) {
      unit.utf16.push_back(
          static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
      unit.utf16.push_back(
          static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
    }
    units.push_back(std::move(unit));
    i += 1 + extra;
  }
  return units;
}
#endif

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

#ifndef MAHO_STANDALONE_TEST
// static
bool MahoMcpInputSynthesizer::KeyPress(content::WebContents* wc,
                                       const std::string& key,
                                       const std::vector<std::string>& modifiers) {
  content::RenderWidgetHost* rwh = GetTargetRenderWidgetHost(wc);
  if (!rwh) return false;

  ui::KeyboardCode key_code = KeyStringToKeyCode(key);
  int web_modifiers = ModifierListToWebModifiers(modifiers);

  ui::DomCode dom_code = ui::UsLayoutKeyboardCodeToDomCode(key_code);
  
  int event_flags = 0;
  if (web_modifiers & blink::WebInputEvent::kShiftKey) event_flags |= ui::EF_SHIFT_DOWN;
  if (web_modifiers & blink::WebInputEvent::kControlKey) event_flags |= ui::EF_CONTROL_DOWN;
  if (web_modifiers & blink::WebInputEvent::kAltKey) event_flags |= ui::EF_ALT_DOWN;
  if (web_modifiers & blink::WebInputEvent::kMetaKey) event_flags |= ui::EF_COMMAND_DOWN;

  ui::DomKey dom_key;
  ui::KeyboardCode unused_key_code;
  if (!ui::DomCodeToUsLayoutDomKey(dom_code, event_flags, &dom_key, &unused_key_code)) {
    if (key.size() == 1) {
      dom_key = ui::DomKey::FromCharacter(key[0]);
    } else {
      dom_key = ui::DomKey::UNIDENTIFIED;
    }
  }

  // RawKeyDown
  blink::WebKeyboardEvent key_down(
      blink::WebInputEvent::Type::kRawKeyDown, web_modifiers,
      base::TimeTicks::Now());
  key_down.windows_key_code = key_code;
  key_down.dom_code = static_cast<int>(dom_code);
  key_down.dom_key = static_cast<int>(dom_key);
  rwh->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(key_down, wc->GetNativeView()));

  // Char
  if (key.size() == 1 || key == "Enter") {
    blink::WebKeyboardEvent key_char(
        blink::WebInputEvent::Type::kChar, web_modifiers,
        base::TimeTicks::Now());
    key_char.windows_key_code = key_code;
    key_char.dom_code = static_cast<int>(dom_code);
    key_char.dom_key = static_cast<int>(dom_key);
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
  key_up.dom_code = static_cast<int>(dom_code);
  key_up.dom_key = static_cast<int>(dom_key);
  rwh->ForwardKeyboardEvent(input::NativeWebKeyboardEvent(key_up, wc->GetNativeView()));

  return true;
}
#endif

}  // namespace maho
