// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_SHORTCUT_INTERCEPTOR_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_SHORTCUT_INTERCEPTOR_H_

#include <string>
#include <vector>

#include "components/input/native_web_keyboard_event.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_codes.h"

class Browser;

namespace ui {
class KeyEvent;
}

namespace maho {

// --- Registry key codec -----------------------------------------------------
// C++ side of the shortcut registry's key alphabet. Must round-trip every
// key string the Rust registry can emit (KeyCombo::key in
// maho/crates/maho-types/src/keyboard.rs + defaults in
// maho/crates/maho-core/src/shortcut_manager.rs). Synchronization is
// enforced by MahoShortcutContractTest in maho_shortcut_contract_unittest.cc;
// see maho-chromium/docs/shortcut-registry-contract.md.
std::string VKeyToString(int vk);

// Inverse of VKeyToString(); the two must stay in sync.
ui::KeyboardCode StringToVKey(const std::string& key);

// Resolves Chromium key events against the Rust ShortcutManager (the single
// source of truth for shortcut resolution, via maho_core_resolve_shortcut())
// and dispatches the matching action through ExecuteCommandAction().
//
// DISPATCH ARCHITECTURE (why there are three BrowserView entry points, not one)
// ---------------------------------------------------------------------------
// Maho shortcuts reach this class through three BrowserView hooks. All three
// resolve through the same Rust registry; they differ only in WHEN Chromium
// hands us the event:
//
//   1. BrowserView::PreHandleKeyboardEvent() -> MaybeHandleKeyEvent()
//      Pre-renderer WebContentsDelegate hook. Gives Maho shortcuts "reserved"
//      priority (handled BEFORE the web page, which therefore cannot
//      preventDefault() them) and hosts shortcut RECORDING capture. Structural
//      constraint: it only fires when a WebContents has keyboard focus, so it
//      does NOT run when there is no active tab.
//   2. BrowserView::OnKeyEvent() -> ResolveActionForKeyEvent()
//      Views-level fallback for focused widgets. Largely redundant with (3).
//   3. BrowserView::AcceleratorPressed() -> ResolveActionForAccelerator()
//      FocusManager accelerator target. Fires regardless of focus, so it is the
//      ONLY path that works with no active tab. Requires every shortcut to be
//      registered as a FocusManager accelerator in BrowserView::AddedToWidget()
//      via GetRegisteredAccelerators().
//
// Do NOT collapse these into a single path without care: dropping (1) removes
// pre-renderer priority for Maho combos that collide with NON-reserved Chromium
// commands (e.g. Cmd+L, Ctrl+F) — web pages could then intercept them — and
// leaves shortcut recording without a home (AcceleratorPressed only fires for
// already-registered combos and always executes). A true single path would
// require a window-level pre-target ui::EventHandler and a deliberate "web can
// never block Maho shortcuts" policy.
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

  // Resolve a ui::Accelerator to a Maho shortcut action id (empty if none).
  // Used by BrowserView::AcceleratorPressed to dispatch Maho shortcuts
  // registered via FocusManager, which fires regardless of focus target.
  static std::string ResolveActionForAccelerator(
      const ui::Accelerator& accelerator);

  // Returns the full set of currently-enabled Maho shortcuts (from the Rust
  // ShortcutManager) as ui::Accelerators, so they can be registered with the
  // FocusManager. FocusManager accelerators fire regardless of focus target,
  // which is the only shortcut path that survives when there is no active tab
  // (i.e. no focused WebContents to drive PreHandleKeyboardEvent). Combos that
  // cannot be mapped to a ui::KeyboardCode are skipped. Returns an empty vector
  // if the Rust core is unavailable.
  static std::vector<ui::Accelerator> GetRegisteredAccelerators();

  static void SetRecordingMode(bool enabled);
  static bool IsRecording();

 private:
  static bool g_recording_mode;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_SHORTCUT_INTERCEPTOR_H_
