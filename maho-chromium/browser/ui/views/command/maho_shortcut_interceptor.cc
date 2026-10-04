// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/input/native_web_keyboard_event.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_constants.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/view_utils.h"

namespace maho {

bool MahoShortcutInterceptor::g_recording_mode = false;

// --- Registry key codec (declared in maho_shortcut_interceptor.h) ----------

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
    // "+" is the registry spelling of the OEM_PLUS key (unshifted "=").
    case ui::VKEY_OEM_PLUS:  return "+";
    case ui::VKEY_OEM_5:     return "\\";
    default:
      if (vk >= ui::VKEY_F1 && vk <= ui::VKEY_F12) {
        return "f" + std::to_string(vk - ui::VKEY_F1 + 1);
      }
      return {};
  }
}

// Inverse of VKeyToString(); the two must stay in sync.
ui::KeyboardCode StringToVKey(const std::string& key) {
  if (key.size() == 1) {
    char c = key[0];
    if (c >= 'a' && c <= 'z') {
      return static_cast<ui::KeyboardCode>(ui::VKEY_A + (c - 'a'));
    }
    if (c >= '0' && c <= '9') {
      return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (c - '0'));
    }
    switch (c) {
      case ',': return ui::VKEY_OEM_COMMA;
      case '.': return ui::VKEY_OEM_PERIOD;
      case '/': return ui::VKEY_OEM_2;
      case '[': return ui::VKEY_OEM_4;
      case ']': return ui::VKEY_OEM_6;
      case '-': return ui::VKEY_OEM_MINUS;
      case '=':
      case '+': return ui::VKEY_OEM_PLUS;
      case '\\': return ui::VKEY_OEM_5;
      default:  return ui::VKEY_UNKNOWN;
    }
  }

  if (key == "enter") return ui::VKEY_RETURN;
  if (key == "escape") return ui::VKEY_ESCAPE;
  if (key == "tab") return ui::VKEY_TAB;
  if (key == "space") return ui::VKEY_SPACE;
  if (key == "backspace") return ui::VKEY_BACK;
  if (key == "delete") return ui::VKEY_DELETE;
  if (key == "arrowup") return ui::VKEY_UP;
  if (key == "arrowdown") return ui::VKEY_DOWN;
  if (key == "arrowleft") return ui::VKEY_LEFT;
  if (key == "arrowright") return ui::VKEY_RIGHT;
  if (key == "home") return ui::VKEY_HOME;
  if (key == "end") return ui::VKEY_END;
  if (key == "pageup") return ui::VKEY_PRIOR;
  if (key == "pagedown") return ui::VKEY_NEXT;

  // Function keys: "f1".."f12".
  if (key.size() >= 2 && (key[0] == 'f' || key[0] == 'F')) {
    int n = 0;
    if (base::StringToInt(key.substr(1), &n) && n >= 1 && n <= 12) {
      return static_cast<ui::KeyboardCode>(ui::VKEY_F1 + (n - 1));
    }
  }

  return ui::VKEY_UNKNOWN;
}

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

int ModifiersFromList(const base::ListValue& modifiers) {
  int flags = ui::EF_NONE;
  for (const base::Value& mod : modifiers) {
    if (!mod.is_string()) {
      continue;
    }
    const std::string& name = mod.GetString();
    if (name == "ctrl") {
      flags |= ui::EF_CONTROL_DOWN;
    } else if (name == "shift") {
      flags |= ui::EF_SHIFT_DOWN;
    } else if (name == "alt") {
      flags |= ui::EF_ALT_DOWN;
    } else if (name == "meta") {
      flags |= ui::EF_COMMAND_DOWN;
    }
  }
  return flags;
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

std::string BuildKeyComboJson(const ui::Accelerator& accelerator) {
  std::string key = VKeyToString(accelerator.key_code());
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

  if (accelerator.IsAltDown()) {
    append("alt");
  }
  if (accelerator.IsCtrlDown()) {
    append("ctrl");
  }
  if (accelerator.IsCmdDown()) {
    append("meta");
  }
  if (accelerator.IsShiftDown()) {
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
std::string MahoShortcutInterceptor::ResolveActionForAccelerator(
    const ui::Accelerator& accelerator) {
  return ResolveActionForKeyComboJson(BuildKeyComboJson(accelerator));
}

// static
std::vector<ui::Accelerator>
MahoShortcutInterceptor::GetRegisteredAccelerators() {
  std::vector<ui::Accelerator> accelerators;

  MahoCore* core = maho::GetCore();
  if (!core) {
    return accelerators;
  }

  char* shortcuts = maho_core_get_shortcuts(core);
  if (!shortcuts) {
    return accelerators;
  }
  std::string json(shortcuts);
  maho_string_free(shortcuts);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return accelerators;
  }

  for (const base::Value& entry : parsed->GetList()) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    if (!dict->FindBool("enabled").value_or(false)) {
      continue;
    }
    const base::DictValue* combo = dict->FindDict("keyCombo");
    if (!combo) {
      continue;
    }
    const std::string* key = combo->FindString("key");
    if (!key) {
      continue;
    }
    ui::KeyboardCode key_code = StringToVKey(*key);
    if (key_code == ui::VKEY_UNKNOWN) {
      continue;
    }
    const base::ListValue* modifiers = combo->FindList("modifiers");
    int flags = modifiers ? ModifiersFromList(*modifiers) : ui::EF_NONE;
    accelerators.emplace_back(key_code, flags);
  }

  return accelerators;
}

// static
bool MahoShortcutInterceptor::MaybeHandleKeyEvent(
    Browser* browser,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown &&
      event.GetType() != blink::WebInputEvent::Type::kKeyDown) {
    return false;
  }

  // Dismiss a visible command overlay on Escape from WebContents focus.
  if (event.windows_key_code == ui::VKEY_ESCAPE &&
      !(event.GetModifiers() & (blink::WebInputEvent::kAltKey |
                                blink::WebInputEvent::kControlKey |
                                blink::WebInputEvent::kMetaKey |
                                blink::WebInputEvent::kShiftKey))) {
    if (browser) {
      if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser)) {
        if (auto* controller =
                browser_view->GetMahoCommandOverlayControllerForTesting()) {
          if (controller->IsVisible()) {
            controller->Hide();
            return true;
          }
        }
      }
    }
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
