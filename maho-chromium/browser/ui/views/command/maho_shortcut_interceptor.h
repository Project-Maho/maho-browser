// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_SHORTCUT_INTERCEPTOR_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_SHORTCUT_INTERCEPTOR_H_

#include <string>

#include "components/input/native_web_keyboard_event.h"

class Browser;

namespace ui {
class KeyEvent;
}

namespace maho {

// Intercepts Chromium key events and resolves them against the Rust
// ShortcutManager via maho_core_resolve_shortcut(). If a matching Maho
// shortcut is found, the corresponding action is dispatched through
// ExecuteCommandAction() and the event is consumed.
//
// Intended to be called from BrowserView::PreHandleKeyboardEvent() so
// Maho shortcuts take priority over default Chromium accelerators.
class MahoShortcutInterceptor {
 public:
  // Returns true if the key event matched a registered Maho shortcut
  // and the action was dispatched. The caller should suppress further
  // processing of the event in that case.
  static bool MaybeHandleKeyEvent(
      Browser* browser,
      const input::NativeWebKeyboardEvent& event);

  // Resolves a focused Views key event against Maho's shortcut registry
  // without dispatching the action. Intended for focused widgets that need
  // to mirror BrowserView shortcut behavior locally.
  static std::string ResolveActionForKeyEvent(const ui::KeyEvent& event);

  static void SetRecordingMode(bool enabled);
  static bool IsRecording();

 private:
  static bool g_recording_mode;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_SHORTCUT_INTERCEPTOR_H_
