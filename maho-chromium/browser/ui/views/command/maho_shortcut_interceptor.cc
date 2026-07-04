// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"

#include <string>

#include "base/logging.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/input/native_web_keyboard_event.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/view_utils.h"

namespace maho {

bool MahoShortcutInterceptor::g_recording_mode = false;

namespace {

constexpr char kShortcutTracePrefix[] = "[maho-shortcut-trace]";

const char* EventTypeToString(blink::WebInputEvent::Type type) {
  switch (type) {
    case blink::WebInputEvent::Type::kRawKeyDown:
      return "RawKeyDown";
    case blink::WebInputEvent::Type::kKeyDown:
      return "KeyDown";
    case blink::WebInputEvent::Type::kKeyUp:
      return "KeyUp";
    default:
      return "Other";
  }
}

std::string VKeyToString(int vk) {
  if (vk >= ui::VKEY_A && vk <= ui::VKEY_Z) {
    char c = static_cast<char>('a' + (vk - ui::VKEY_A));
    return std::string(1, c);
  }
  if (vk >= ui::VKEY_0 && vk <= ui::VKEY_9) {
    char c = static_cast<char>('0' + (vk - ui::VKEY_0));
    return std::string(1, c);
  }
  switch (vk) {
    case ui::VKEY_RETURN:    return "enter";
    case ui::VKEY_ESCAPE:    return "escape";
    case ui::VKEY_TAB:       return "tab";
    case ui::VKEY_SPACE:     return "space";
    case ui::VKEY_BACK:      return "backspace";
    case ui::VKEY_DELETE:    return "delete";
    case ui::VKEY_UP:        return "arrowup";
    case ui::VKEY_DOWN:      return "arrowdown";
    case ui::VKEY_LEFT:      return "arrowleft";
    case ui::VKEY_RIGHT:     return "arrowright";
    case ui::VKEY_HOME:      return "home";
    case ui::VKEY_END:       return "end";
    case ui::VKEY_PRIOR:     return "pageup";
    case ui::VKEY_NEXT:      return "pagedown";
    case ui::VKEY_OEM_COMMA: return ",";
    case ui::VKEY_OEM_PERIOD:return ".";
    case ui::VKEY_OEM_2:     return "/";
    case ui::VKEY_OEM_4:     return "[";
    case ui::VKEY_OEM_6:     return "]";
    case ui::VKEY_OEM_MINUS: return "-";
    case ui::VKEY_OEM_PLUS:  return "=";
    default:
      if (vk >= ui::VKEY_F1 && vk <= ui::VKEY_F12) {
        return "f" + std::to_string(vk - ui::VKEY_F1 + 1);
      }
      return {};
  }
}

std::string BuildKeyComboJson(const input::NativeWebKeyboardEvent& event) {
  std::string key = VKeyToString(event.windows_key_code);
  if (key.empty()) {
    return {};
  }

  int mods = event.GetModifiers();
  std::string modifiers = "[";
  bool first = true;

  auto append = [&](const char* name) {
    if (!first) modifiers += ",";
    modifiers += "\"";
    modifiers += name;
    modifiers += "\"";
    first = false;
  };

  if (mods & blink::WebInputEvent::kAltKey)
    append("alt");
  if (mods & blink::WebInputEvent::kControlKey)
    append("ctrl");
  if (mods & blink::WebInputEvent::kMetaKey)
    append("meta");
  if (mods & blink::WebInputEvent::kShiftKey)
    append("shift");

  modifiers += "]";

  return "{\"key\":\"" + key + "\",\"modifiers\":" + modifiers + "}";
}

std::string BuildKeyComboJson(const ui::KeyEvent& event) {
  std::string key = VKeyToString(event.key_code());
  if (key.empty()) {
    return {};
  }

  std::string modifiers = "[";
  bool first = true;

  auto append = [&](const char* name) {
    if (!first) {
      modifiers += ",";
    }
    modifiers += "\"";
    modifiers += name;
    modifiers += "\"";
    first = false;
  };

  if (event.IsAltDown()) {
    append("alt");
  }
  if (event.IsControlDown()) {
    append("ctrl");
  }
  if (event.IsCommandDown()) {
    append("meta");
  }
  if (event.IsShiftDown()) {
    append("shift");
  }

  modifiers += "]";
  return "{\"key\":\"" + key + "\",\"modifiers\":" + modifiers + "}";
}

std::string ResolveActionForKeyComboJson(const std::string& json) {
  if (json.empty()) {
    return {};
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return {};
  }

  char* action = maho_core_resolve_shortcut(core, json.c_str());
  if (!action) {
    return {};
  }

  std::string action_id(action);
  maho_string_free(action);
  return action_id;
}

void LogShortcutTrace(const input::NativeWebKeyboardEvent& event,
                      const std::string& key_combo_json,
                      const std::string& action_id,
                      bool fallback_applied,
                      bool handled,
                      const char* phase) {
  LOG(WARNING) << kShortcutTracePrefix << " phase=" << phase
               << " event_type=" << EventTypeToString(event.GetType())
               << " key_code=" << event.windows_key_code
               << " modifiers=" << event.GetModifiers()
               << " key_combo_json=" << key_combo_json
               << " action_id=" << (action_id.empty() ? "<empty>" : action_id)
               << " fallback_ai_panel=" << fallback_applied
               << " handled=" << handled;
}

}  // namespace

// static
std::string MahoShortcutInterceptor::ResolveActionForKeyEvent(
    const ui::KeyEvent& event) {
  return ResolveActionForKeyComboJson(BuildKeyComboJson(event));
}

// static
bool MahoShortcutInterceptor::MaybeHandleKeyEvent(
    Browser* browser,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown &&
      event.GetType() != blink::WebInputEvent::Type::kKeyDown) {
    return false;
  }

  if (g_recording_mode) {
    std::string key = VKeyToString(event.windows_key_code);
    if (!key.empty()) {
      auto mojo_key_combo = maho_settings::mojom::MojoKeyCombo::New();
      mojo_key_combo->key = key;
      const int mods = event.GetModifiers();
      if (mods & blink::WebInputEvent::kAltKey)
        mojo_key_combo->modifiers.push_back("alt");
      if (mods & blink::WebInputEvent::kControlKey)
        mojo_key_combo->modifiers.push_back("ctrl");
      if (mods & blink::WebInputEvent::kMetaKey)
        mojo_key_combo->modifiers.push_back("meta");
      if (mods & blink::WebInputEvent::kShiftKey)
        mojo_key_combo->modifiers.push_back("shift");

      if (auto* handler = MahoSettingsPageHandler::GetActiveInstance()) {
        handler->NotifyShortcutRecorded(std::move(mojo_key_combo));
      }
    }
    return false;
  }

  const int modifiers = event.GetModifiers();
  const bool has_cmd_ctrl =
      modifiers & (blink::WebInputEvent::kMetaKey |
                   blink::WebInputEvent::kControlKey);
  const bool trace_shortcut =
      has_cmd_ctrl && !(modifiers & blink::WebInputEvent::kShiftKey) &&
      (event.windows_key_code == ui::VKEY_E || event.windows_key_code == ui::VKEY_T ||
       event.windows_key_code == ui::VKEY_L);
  const std::string key_combo_json =
      trace_shortcut ? BuildKeyComboJson(event) : std::string();

  std::string action_id =
      ResolveActionForKeyComboJson(trace_shortcut ? key_combo_json
                                                  : BuildKeyComboJson(event));
  bool fallback_applied = false;
  if (action_id.empty()) {
    if (trace_shortcut) {
      LogShortcutTrace(event, key_combo_json, action_id, fallback_applied,
                       false, "unhandled_after_resolve");
    }
    return false;
  }

  if (trace_shortcut) {
    LogShortcutTrace(event, key_combo_json, action_id, fallback_applied, true,
                     "dispatch_execute_command_action");
  }
  ExecuteCommandAction(browser, action_id);
  return true;
}

// static
void MahoShortcutInterceptor::SetRecordingMode(bool enabled) {
  g_recording_mode = enabled;
}

// static
bool MahoShortcutInterceptor::IsRecording() {
  return g_recording_mode;
}

}  // namespace maho
