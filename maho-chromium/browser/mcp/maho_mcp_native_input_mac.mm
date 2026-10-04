// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#include <cmath>

namespace maho {

std::optional<NativePoint> ResolveNativePointMac(
    const VisualFrame& frame,
    const gfx::PointF& screen_dip) {
  if (!std::isfinite(screen_dip.x()) || !std::isfinite(screen_dip.y())) {
    return std::nullopt;
  }

  // macOS: screen DIPs become UNSCALED global CG points.
  // NO Retina multiplication, NO Y re-inversion.
  NativePoint pt;
  pt.screen_dip = screen_dip;
  pt.mac_cg_point = screen_dip;
  return pt;
}

NativeInputAvailability CheckNativeInputAvailabilityMac(
    std::optional<bool> trusted_override) {
  NativeInputAvailability result;
  const bool trusted = trusted_override.has_value()
                           ? *trusted_override
                           : AXIsProcessTrustedWithOptions(nullptr);

  if (trusted) {
    result.available = true;
    result.reason =
        "accessibility process is trusted (trust does not prove event delivery)";
    result.raw_os_status = 1;
  } else {
    result.available = false;
    result.reason =
        "accessibility permission not granted (process untrusted); prompt suppressed";
    result.raw_os_status = 0;
  }
  return result;
}

uint16_t MacKeyCodeForKeyboardCode(ui::KeyboardCode key_code) {
  switch (key_code) {
    case ui::VKEY_A: return 0x00;
    case ui::VKEY_S: return 0x01;
    case ui::VKEY_D: return 0x02;
    case ui::VKEY_F: return 0x03;
    case ui::VKEY_H: return 0x04;
    case ui::VKEY_G: return 0x05;
    case ui::VKEY_Z: return 0x06;
    case ui::VKEY_X: return 0x07;
    case ui::VKEY_C: return 0x08;
    case ui::VKEY_V: return 0x09;
    case ui::VKEY_B: return 0x0B;
    case ui::VKEY_Q: return 0x0C;
    case ui::VKEY_W: return 0x0D;
    case ui::VKEY_E: return 0x0E;
    case ui::VKEY_R: return 0x0F;
    case ui::VKEY_Y: return 0x10;
    case ui::VKEY_T: return 0x11;
    case ui::VKEY_1: return 0x12;
    case ui::VKEY_2: return 0x13;
    case ui::VKEY_3: return 0x14;
    case ui::VKEY_4: return 0x15;
    case ui::VKEY_6: return 0x16;
    case ui::VKEY_5: return 0x17;
    case ui::VKEY_9: return 0x19;
    case ui::VKEY_7: return 0x1A;
    case ui::VKEY_8: return 0x1C;
    case ui::VKEY_0: return 0x1D;
    case ui::VKEY_O: return 0x1F;
    case ui::VKEY_U: return 0x20;
    case ui::VKEY_I: return 0x22;
    case ui::VKEY_P: return 0x23;
    case ui::VKEY_RETURN: return 0x24;
    case ui::VKEY_L: return 0x25;
    case ui::VKEY_J: return 0x26;
    case ui::VKEY_K: return 0x28;
    case ui::VKEY_TAB: return 0x30;
    case ui::VKEY_SPACE: return 0x31;
    case ui::VKEY_BACK: return 0x33;
    case ui::VKEY_ESCAPE: return 0x35;
    case ui::VKEY_COMMAND: return 0x37;
    case ui::VKEY_SHIFT: return 0x38;
    case ui::VKEY_MENU: return 0x3A;
    case ui::VKEY_CONTROL: return 0x3B;
    case ui::VKEY_F1: return 0x7A;
    case ui::VKEY_F2: return 0x78;
    case ui::VKEY_F3: return 0x63;
    case ui::VKEY_F4: return 0x76;
    case ui::VKEY_F5: return 0x60;
    case ui::VKEY_F6: return 0x61;
    case ui::VKEY_F7: return 0x62;
    case ui::VKEY_F8: return 0x64;
    case ui::VKEY_F9: return 0x65;
    case ui::VKEY_F10: return 0x6D;
    case ui::VKEY_F11: return 0x67;
    case ui::VKEY_F12: return 0x6F;
    case ui::VKEY_HOME: return 0x73;
    case ui::VKEY_PRIOR: return 0x74;
    case ui::VKEY_DELETE: return 0x75;
    case ui::VKEY_END: return 0x77;
    case ui::VKEY_NEXT: return 0x79;
    case ui::VKEY_LEFT: return 0x7B;
    case ui::VKEY_RIGHT: return 0x7C;
    case ui::VKEY_DOWN: return 0x7D;
    case ui::VKEY_UP: return 0x7E;
    default: return 0xFFFF;
  }
}

CGEventFlags CGEventFlagsForModifiersMac(uint32_t modifiers) {
  CGEventFlags flags = 0;
  if (modifiers & blink::WebInputEvent::kShiftKey) flags |= kCGEventFlagMaskShift;
  if (modifiers & blink::WebInputEvent::kControlKey) flags |= kCGEventFlagMaskControl;
  if (modifiers & blink::WebInputEvent::kAltKey) flags |= kCGEventFlagMaskAlternate;
  if (modifiers & blink::WebInputEvent::kMetaKey) flags |= kCGEventFlagMaskCommand;
  return flags;
}

class NativeInputDispatcherMac : public NativeInputDispatcher {
 public:
  NativeInputDispatcherMac() = default;
  ~NativeInputDispatcherMac() override = default;

  NativeDispatchResult Dispatch(base::span<const NativeEvent> events) override {
    NativeDispatchResult result;
    result.requested = events.size();
    result.inserted = 0;
    result.raw_os_error = 0;
    // macOS void CGEventPost cannot acknowledge receipt
    result.submission_known = false;

    for (const auto& ev : events) {
      CGEventRef cg_event = nullptr;
      switch (ev.type) {
        case NativeEvent::Type::kMouseMove: {
          CGPoint pt = CGPointMake(ev.point.mac_cg_point.x(),
                                   ev.point.mac_cg_point.y());
          cg_event = CGEventCreateMouseEvent(
              nullptr, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
          break;
        }
        case NativeEvent::Type::kMouseDown: {
          CGPoint pt = CGPointMake(ev.point.mac_cg_point.x(),
                                   ev.point.mac_cg_point.y());
          cg_event = CGEventCreateMouseEvent(
              nullptr, kCGEventLeftMouseDown, pt, kCGMouseButtonLeft);
          break;
        }
        case NativeEvent::Type::kMouseUp: {
          CGPoint pt = CGPointMake(ev.point.mac_cg_point.x(),
                                   ev.point.mac_cg_point.y());
          cg_event = CGEventCreateMouseEvent(
              nullptr, kCGEventLeftMouseUp, pt, kCGMouseButtonLeft);
          break;
        }
        case NativeEvent::Type::kKeyDown: {
          uint16_t mac_code = MacKeyCodeForKeyboardCode(ev.key_code);
          cg_event = CGEventCreateKeyboardEvent(nullptr, mac_code, true);
          if (cg_event && ev.modifiers != 0) {
            CGEventSetFlags(cg_event, CGEventFlagsForModifiersMac(ev.modifiers));
          }
          break;
        }
        case NativeEvent::Type::kKeyUp: {
          uint16_t mac_code = MacKeyCodeForKeyboardCode(ev.key_code);
          cg_event = CGEventCreateKeyboardEvent(nullptr, mac_code, false);
          if (cg_event && ev.modifiers != 0) {
            CGEventSetFlags(cg_event, CGEventFlagsForModifiersMac(ev.modifiers));
          }
          break;
        }
        case NativeEvent::Type::kKeyChar: {
          cg_event = CGEventCreateKeyboardEvent(nullptr, 0, true);
          if (cg_event) {
            if (!ev.text.empty()) {
              CGEventKeyboardSetUnicodeString(
                  cg_event, ev.text.length(),
                  reinterpret_cast<const UniChar*>(ev.text.data()));
            }
            CGEventPost(kCGHIDEventTap, cg_event);
            CFRelease(cg_event);
          }
          cg_event = CGEventCreateKeyboardEvent(nullptr, 0, false);
          if (cg_event) {
            if (!ev.text.empty()) {
              CGEventKeyboardSetUnicodeString(
                  cg_event, ev.text.length(),
                  reinterpret_cast<const UniChar*>(ev.text.data()));
            }
            CGEventPost(kCGHIDEventTap, cg_event);
            CFRelease(cg_event);
            cg_event = nullptr;
          }
          result.inserted++;
          continue;
        }
      }
      if (cg_event) {
        CGEventPost(kCGHIDEventTap, cg_event);
        CFRelease(cg_event);
        result.inserted++;
      }
    }
    return result;
  }
};

NativeInputDispatcher* GetPlatformNativeDispatcherMac() {
  static NativeInputDispatcherMac s_mac_dispatcher;
  return &s_mac_dispatcher;
}

}  // namespace maho
