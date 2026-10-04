// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"

#include <windows.h>
#include <cmath>

#include "ui/display/screen.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/point_conversions.h"

namespace maho {

std::optional<NativePoint> ResolveNativePointWin(
    const VisualFrame& frame,
    const gfx::PointF& screen_dip,
    display::win::ScreenWin* screen_win_override) {
  if (!std::isfinite(screen_dip.x()) || !std::isfinite(screen_dip.y())) {
    return std::nullopt;
  }

  display::win::ScreenWin* screen_win =
      screen_win_override
          ? screen_win_override
          : static_cast<display::win::ScreenWin*>(display::Screen::Get());
  if (!screen_win) {
    return std::nullopt;
  }

  // Windows: ScreenWin instance DIPToScreenPoint (display-aware, no global dip*scale shortcut)
  const gfx::Point pixel_point =
      screen_win->DIPToScreenPoint(gfx::ToRoundedPoint(screen_dip));

  const int screen_left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int screen_top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int screen_width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int screen_height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
  const gfx::Rect virtual_screen(screen_left, screen_top, screen_width,
                                 screen_height);

  return MahoMcpInputSynthesizer::NormalizeWindowsNativePoint(
      screen_dip, pixel_point, virtual_screen);
}

NativeInputAvailability CheckNativeInputAvailabilityWin(
    std::optional<bool> interactive_override,
    std::optional<int64_t> last_error_override,
    std::optional<int> send_input_override) {
  NativeInputAvailability result;

  bool is_interactive = false;
  if (interactive_override.has_value()) {
    is_interactive = *interactive_override;
  } else {
    HWINSTA hwinsta = ::GetProcessWindowStation();
    if (hwinsta) {
      USEROBJECTFLAGS uof = {0};
      if (::GetUserObjectInformationW(hwinsta, UOI_FLAGS, &uof, sizeof(uof),
                                      nullptr)) {
        is_interactive = (uof.dwFlags & WSF_VISIBLE) != 0;
      }
    }
  }

  if (!is_interactive) {
    result.available = false;
    result.reason =
        "non-interactive window station or desktop context; input insertion unavailable";
    result.raw_os_status = last_error_override.value_or(
        static_cast<int64_t>(::GetLastError()));
    return result;
  }

  // Report SendInput insertion failures honestly if tested/observed.
  if (send_input_override.has_value() && *send_input_override == 0) {
    result.available = false;
    result.raw_os_status = last_error_override.value_or(
        static_cast<int64_t>(::GetLastError()));
    // Honest error reporting: NEVER claim a Windows SendInput error proves a UIPI cause.
    result.reason =
        "SendInput failed to insert events; insertion failure may stem from UIPI, invalid state, or desktop restrictions (cause cannot be proven to be UIPI)";
    return result;
  }

  result.available = true;
  result.reason =
      "interactive window station active and input synthesis available";
  result.raw_os_status = last_error_override.value_or(0);
  return result;
}

class NativeInputDispatcherWin : public NativeInputDispatcher {
 public:
  NativeInputDispatcherWin() = default;
  ~NativeInputDispatcherWin() override = default;

  NativeDispatchResult Dispatch(base::span<const NativeEvent> events) override {
    NativeDispatchResult result;
    result.requested = events.size();
    result.inserted = 0;
    result.raw_os_error = 0;
    result.submission_known = true;

    std::vector<INPUT> win_inputs;
    for (const auto& ev : events) {
      std::vector<WinSimulatedInput> sim_inputs =
          BuildWindowsInputsForNativeEvent(ev);
      for (const auto& sim : sim_inputs) {
        INPUT inp = {};
        if (sim.type == WinSimulatedInput::kMouse) {
          inp.type = INPUT_MOUSE;
          inp.mi.dx = sim.mouse_dx;
          inp.mi.dy = sim.mouse_dy;
          inp.mi.dwFlags = sim.mouse_flags;
        } else {
          inp.type = INPUT_KEYBOARD;
          inp.ki.wVk = sim.vk;
          inp.ki.wScan = sim.scan;
          inp.ki.dwFlags = sim.key_flags;
        }
        win_inputs.push_back(inp);
      }
    }

    if (win_inputs.empty()) {
      return result;
    }

    UINT inserted = ::SendInput(static_cast<UINT>(win_inputs.size()),
                                win_inputs.data(), sizeof(INPUT));
    result.inserted = (inserted == win_inputs.size()) ? events.size() : 0;
    if (inserted != win_inputs.size()) {
      result.raw_os_error = static_cast<int64_t>(::GetLastError());
      result.error_message = "SendInput failed to insert all requested events";
    }
    return result;
  }
};

NativeInputDispatcher* GetPlatformNativeDispatcherWin() {
  static NativeInputDispatcherWin s_win_dispatcher;
  return &s_win_dispatcher;
}

}  // namespace maho
