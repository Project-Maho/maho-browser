// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifndef MAHO_STANDALONE_TEST
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "maho/browser/ui/views/maho_action_marker_service.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/size_f.h"
#else
namespace content {
class WebContents;
}
namespace ui {
using AXNodeID = int32_t;
enum KeyboardCode {
  VKEY_UNKNOWN = 0,
  VKEY_RETURN = 0x0D,
  VKEY_TAB = 0x09,
  VKEY_ESCAPE = 0x1B,
  VKEY_BACK = 0x08,
  VKEY_DELETE = 0x2E,
  VKEY_UP = 0x26,
  VKEY_DOWN = 0x28,
  VKEY_LEFT = 0x25,
  VKEY_RIGHT = 0x27,
  VKEY_HOME = 0x24,
  VKEY_END = 0x23,
  VKEY_PRIOR = 0x21,
  VKEY_NEXT = 0x22,
  VKEY_SPACE = 0x20,
  VKEY_0 = 0x30,
  VKEY_9 = 0x39,
  VKEY_A = 0x41,
  VKEY_Z = 0x5A,
  VKEY_F1 = 0x70,
  VKEY_F2 = 0x71,
  VKEY_F3 = 0x72,
  VKEY_F4 = 0x73,
  VKEY_F5 = 0x74,
  VKEY_F6 = 0x75,
  VKEY_F7 = 0x76,
  VKEY_F8 = 0x77,
  VKEY_F9 = 0x78,
  VKEY_F10 = 0x79,
  VKEY_F11 = 0x7A,
  VKEY_F12 = 0x7B,
};
}  // namespace ui
namespace base {
template <typename Signature>
class OnceCallback;
}
namespace maho {
class MahoActionMarkerService {
 public:
  enum class Kind {
    kClick,
    kHover,
  };
};
}  // namespace maho
namespace blink {
namespace WebInputEvent {
enum Modifiers {
  kNoModifiers = 0,
  kShiftKey = 1 << 0,
  kControlKey = 1 << 1,
  kAltKey = 1 << 2,
  kMetaKey = 1 << 3,
};
}  // namespace WebInputEvent
}  // namespace blink
#endif

namespace maho {

// 64-bit SplitMix PRNG for deterministic, reproducible pseudo-random sequences.
class SplitMix64Prng {
 public:
  constexpr explicit SplitMix64Prng(uint64_t seed = 0x853c49e6748fea9bULL)
      : state_(seed) {}

  uint64_t NextU64() {
    uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }

  // Returns integer in [min, max] inclusive.
  int32_t NextRange(int32_t min, int32_t max) {
    if (min >= max) {
      return min;
    }
    uint64_t span = static_cast<uint64_t>(max - min + 1);
    return min + static_cast<int32_t>(NextU64() % span);
  }

  uint64_t state() const { return state_; }

 private:
  uint64_t state_;
};

struct DelayRangeMs {
  int32_t min_ms{0};
  int32_t max_ms{0};

  constexpr bool operator==(const DelayRangeMs& other) const = default;
  constexpr bool is_valid(int32_t cap_ms) const {
    return min_ms >= 0 && min_ms <= max_ms && max_ms <= cap_ms;
  }
};

struct TypingKeyUnit {
  char32_t code_point{0};
  std::u16string utf16;
  std::string utf8;

  bool operator==(const TypingKeyUnit& other) const {
    return code_point == other.code_point &&
           utf16 == other.utf16 &&
           utf8 == other.utf8;
  }
};

// Bounded pacing for browser automation and accessibility input. These timing
// choices do not establish human behavior or physical hardware input. Challenge
// detection and action authorization belong to the caller, not this policy.
struct TypingPacingPolicy {
  // Max delay cap (5 seconds)
  static constexpr int32_t kMaxDelayCapMs = 5000;

  DelayRangeMs base_range_ms{0, 60};
  DelayRangeMs punctuation_range_ms{120, 240};
  DelayRangeMs space_range_ms{40, 80};
  std::optional<uint64_t> rng_seed{std::nullopt};

  // Presets
  static TypingPacingPolicy Instant() {
    return TypingPacingPolicy{
        .base_range_ms = {0, 0},
        .punctuation_range_ms = {0, 0},
        .space_range_ms = {0, 0},
        .rng_seed = std::nullopt,
    };
  }

  static TypingPacingPolicy Paced(
      std::optional<uint64_t> seed = std::nullopt) {
    return TypingPacingPolicy{
        .base_range_ms = {0, 60},
        .punctuation_range_ms = {120, 240},
        .space_range_ms = {40, 80},
        .rng_seed = seed,
    };
  }

  static TypingPacingPolicy Accessibility(
      std::optional<uint64_t> seed = std::nullopt) {
    return TypingPacingPolicy{
        .base_range_ms = {50, 100},
        .punctuation_range_ms = {150, 300},
        .space_range_ms = {80, 150},
        .rng_seed = seed,
    };
  }

  bool validate() const {
    return base_range_ms.is_valid(kMaxDelayCapMs) &&
           punctuation_range_ms.is_valid(kMaxDelayCapMs) &&
           space_range_ms.is_valid(kMaxDelayCapMs);
  }

  SplitMix64Prng CreatePrng() const {
    return SplitMix64Prng(rng_seed.value_or(0x853c49e6748fea9bULL));
  }

  static constexpr bool IsPunctuationChar(char32_t c) {
    return c == U'.' || c == U',' || c == U'!' || c == U'?' || c == U';' ||
           c == U':' || c == U'-' || c == U'\n' || c == U'\r' ||
           c == 0x3002 || c == 0xFF0C || c == 0xFF01 || c == 0xFF1F ||
           c == 0xFF1B || c == 0xFF1A || c == 0x2026 || c == 0x00B7;
  }

  // Calculates bounded pacing delay between characters.
  // Purpose discipline: this is input UX and accessibility pacing ONLY.
  // Anti-bot evasion is explicitly out of scope and all delays are strictly bounded.
  // No site-detection or adaptive behavior is implemented.
  int32_t delay_for(std::optional<char32_t> prev_char,
                    char32_t next_char,
                    SplitMix64Prng& rng) const {
    DelayRangeMs target_range = base_range_ms;
    if (prev_char.has_value() && IsPunctuationChar(*prev_char)) {
      target_range = punctuation_range_ms;
    } else if (next_char == U' ' ||
               (prev_char.has_value() && *prev_char == U' ')) {
      target_range = space_range_ms;
    }

    if (target_range.min_ms >= target_range.max_ms) {
      return target_range.min_ms;
    }
    int32_t sampled = rng.NextRange(target_range.min_ms, target_range.max_ms);
    if (sampled < target_range.min_ms) {
      sampled = target_range.min_ms;
    }
    if (sampled > target_range.max_ms) {
      sampled = target_range.max_ms;
    }
    return sampled;
  }

  int32_t delay_for(char32_t prev_char,
                    char32_t next_char,
                    SplitMix64Prng& rng) const {
    return delay_for(
        prev_char == 0 ? std::nullopt : std::optional<char32_t>(prev_char),
        next_char, rng);
  }

  int32_t delay_for(char prev_char, char next_char, SplitMix64Prng& rng) const {
    return delay_for(
        prev_char == '\0'
            ? std::nullopt
            : std::optional<char32_t>(
                  static_cast<unsigned char>(prev_char)),
        static_cast<char32_t>(static_cast<unsigned char>(next_char)), rng);
  }

  int32_t delay_for(std::optional<char> prev_char,
                    char next_char,
                    SplitMix64Prng& rng) const {
    return delay_for(
        prev_char.has_value()
            ? std::optional<char32_t>(
                  static_cast<unsigned char>(*prev_char))
            : std::nullopt,
        static_cast<char32_t>(static_cast<unsigned char>(next_char)), rng);
  }

  int32_t delay_for(char16_t prev_char,
                    char16_t next_char,
                    SplitMix64Prng& rng) const {
    return delay_for(
        prev_char == u'\0'
            ? std::nullopt
            : std::optional<char32_t>(static_cast<char32_t>(prev_char)),
        static_cast<char32_t>(next_char), rng);
  }

  int32_t delay_for(std::optional<char16_t> prev_char,
                    char16_t next_char,
                    SplitMix64Prng& rng) const {
    return delay_for(
        prev_char.has_value()
            ? std::optional<char32_t>(static_cast<char32_t>(*prev_char))
            : std::nullopt,
        static_cast<char32_t>(next_char), rng);
  }
};

struct TypingOptions {
  TypingPacingPolicy pacing_policy;
};

#ifndef MAHO_STANDALONE_TEST
// Strictly bounded motion profile governing how a synthesized trusted click
// travels to its target. All ranges are hard-capped so total motion time stays
// small and deterministic under a fixed seed. This exists for input realism /
// UX consistency (coherent hover + press semantics), never as adaptive or
// site-aware behavior.
struct ClickMotionProfile {
  ClickMotionProfile();
  ClickMotionProfile(const ClickMotionProfile&);
  ClickMotionProfile(ClickMotionProfile&&) noexcept;
  ClickMotionProfile& operator=(const ClickMotionProfile&);
  ClickMotionProfile& operator=(ClickMotionProfile&&) noexcept;
  ~ClickMotionProfile();

  // Max total pointer-travel duration cap (1 second).
  static constexpr int32_t kMaxMotionDurationCapMs = 1000;

  DelayRangeMs total_duration_ms{80, 250};
  int32_t min_waypoints{8};
  int32_t max_waypoints{20};
  // Per-waypoint positional jitter bound in px; the final point is exact.
  int32_t approach_jitter_px{2};
  DelayRangeMs pre_click_dwell_ms{40, 120};
  DelayRangeMs press_release_ms{50, 90};
  std::optional<uint64_t> rng_seed{std::nullopt};

  // ── Bounded human-motion additions (plan item 8) ──
  // Terminal containment rect in CSS px: terminal jitter/overshoot must stay
  // inside this rect; the final waypoint is its exact center.
  gfx::RectF target_rect;
  // Gaussian perpendicular jitter sigma for approach waypoints (CSS px).
  double approach_jitter_sigma_px{0.8};
  // Fraction of motions that carry a bounded 2-4px overshoot + correction.
  double overshoot_probability{0.2};
  int32_t overshoot_min_px{2};
  int32_t overshoot_max_px{4};
  // Truncated log-normal pre-click dwell window (ms).
  DelayRangeMs dwell_ms{80, 220};

  bool validate() const {
    return min_waypoints >= 1 && min_waypoints <= max_waypoints &&
           max_waypoints <= 64 &&
           total_duration_ms.is_valid(kMaxMotionDurationCapMs) &&
           approach_jitter_px >= 0 && approach_jitter_px <= 10 &&
           pre_click_dwell_ms.is_valid(TypingPacingPolicy::kMaxDelayCapMs) &&
           press_release_ms.is_valid(TypingPacingPolicy::kMaxDelayCapMs) &&
           approach_jitter_sigma_px >= 0.0 &&
           overshoot_probability >= 0.0 && overshoot_probability <= 1.0 &&
           overshoot_min_px >= 1 && overshoot_min_px <= overshoot_max_px &&
           dwell_ms.is_valid(TypingPacingPolicy::kMaxDelayCapMs);
  }
};

struct VisualFrameTarget {
  bool valid{false};
  int tab_id{0};
  int browser_id{0};
  int64_t generation{0};

  constexpr bool operator==(const VisualFrameTarget& other) const = default;

  VisualFrameTarget() = default;
  VisualFrameTarget(bool v, int tab, int browser, int64_t gen)
      : valid(v), tab_id(tab), browser_id(browser), generation(gen) {}

  template <typename T>
  VisualFrameTarget(const T& t)
      : valid(t.valid),
        tab_id(t.tab_id),
        browser_id(t.browser_id),
        generation(t.generation) {}

  template <typename T>
  VisualFrameTarget& operator=(const T& t) {
    valid = t.valid;
    tab_id = t.tab_id;
    browser_id = t.browser_id;
    generation = t.generation;
    return *this;
  }
};

// Visual frame capture state used for visual verification and screen coordinate
// translation. Token is server-owned and invalidated on navigation, scroll/zoom,
// resize, activation changes, lease loss, and disconnect.
struct VisualFrame {
  VisualFrame();
  VisualFrame(const VisualFrame&);
  VisualFrame& operator=(const VisualFrame&);
  VisualFrame(VisualFrame&&) noexcept;
  VisualFrame& operator=(VisualFrame&&) noexcept;
  ~VisualFrame();

  std::string token;
  VisualFrameTarget target;
  uint64_t document_epoch{0};
  uint64_t lease_epoch{0};
  gfx::SizeF viewport_css_size;
  gfx::Size bitmap_size;
  gfx::PointF visual_viewport_offset;
  float page_zoom_factor{1.0f};
  float page_scale_factor{1.0f};
  float device_scale_factor{1.0f};
  uint64_t view_transform_generation{0};
  gfx::Rect view_bounds_in_screen;
  std::vector<uint8_t> redacted_png;

  // Revalidation state for rejecting moved windows or stale screenshots.
  std::optional<gfx::Rect> live_view_bounds;
  std::optional<uint64_t> live_transform_generation;
  std::optional<uint64_t> live_document_epoch;
  base::TimeTicks captured_at{base::TimeTicks::Now()};
  bool is_stale{false};
};

inline constexpr uint32_t kMouseEventFMove = 0x0001;
inline constexpr uint32_t kMouseEventFVirtualDesk = 0x4000;
inline constexpr uint32_t kMouseEventFAbsolute = 0x8000;

struct NativePoint {
  // Screen DIP point (macOS unscaled global CG point coordinates).
  gfx::PointF screen_dip;

  // Explicit macOS global CG point coordinates.
  gfx::PointF mac_cg_point;

  // Windows physical pixel point and normalized virtual-desktop coordinates.
  gfx::Point win_pixel_point;
  int win_normalized_x{0};
  int win_normalized_y{0};
  uint32_t win_flags{0};

  bool operator==(const NativePoint& other) const = default;
};

// Availability status and OS preflight diagnostic for native input synthesis.
struct NativeInputAvailability {
  bool available{false};
  std::string reason;
  int64_t raw_os_status{0};

  bool operator==(const NativeInputAvailability& other) const = default;
};
using NativeInputPreflightResult = NativeInputAvailability;

#if BUILDFLAG(IS_MAC)
std::optional<NativePoint> ResolveNativePointMac(
    const VisualFrame& frame,
    const gfx::PointF& screen_dip);
NativeInputAvailability CheckNativeInputAvailabilityMac(
    std::optional<bool> trusted_override = std::nullopt);
#endif

#if BUILDFLAG(IS_WIN)
}  // namespace maho

// Forward-declare at global scope: a nested-namespace-definition inside maho
// would declare maho::display::win::ScreenWin, shadowing the real class and
// breaking the Windows TU.
namespace display::win {
class ScreenWin;
}

namespace maho {

std::optional<NativePoint> ResolveNativePointWin(
    const VisualFrame& frame,
    const gfx::PointF& screen_dip,
    display::win::ScreenWin* screen_win_override = nullptr);
NativeInputAvailability CheckNativeInputAvailabilityWin(
    std::optional<bool> interactive_override = std::nullopt,
    std::optional<int64_t> last_error_override = std::nullopt,
    std::optional<int> send_input_override = std::nullopt);
#endif

// Native input event representation for the native dispatcher seam.
struct NativeEvent {
  enum class Type {
    kMouseMove,
    kMouseDown,
    kMouseUp,
    kKeyDown,
    kKeyUp,
    kKeyChar,
  };

  enum class MouseButton {
    kNone,
    kLeft,
    kRight,
    kMiddle,
  };

  NativeEvent();
  ~NativeEvent();
  NativeEvent(const NativeEvent&);
  NativeEvent& operator=(const NativeEvent&);
  NativeEvent(NativeEvent&&) noexcept;
  NativeEvent& operator=(NativeEvent&&) noexcept;

  Type type{Type::kMouseMove};
  NativePoint point;
  MouseButton button{MouseButton::kNone};

  // Keyboard fields
  ui::KeyboardCode key_code{ui::VKEY_UNKNOWN};
  uint32_t scan_code{0};
  uint32_t modifiers{0};  // e.g. blink::WebInputEvent::Modifiers
  std::u16string text;    // Unicode UTF-16 code units
  char32_t code_point{0};

  static NativeEvent MakeMouseMove(const NativePoint& pt);
  static NativeEvent MakeMouseDown(const NativePoint& pt,
                                  MouseButton btn = MouseButton::kLeft);
  static NativeEvent MakeMouseUp(const NativePoint& pt,
                                MouseButton btn = MouseButton::kLeft);
  static NativeEvent MakeKeyDown(ui::KeyboardCode code,
                                uint32_t scan = 0,
                                uint32_t mods = 0);
  static NativeEvent MakeKeyUp(ui::KeyboardCode code,
                              uint32_t scan = 0,
                              uint32_t mods = 0);
  static NativeEvent MakeKeyChar(char32_t cp,
                                const std::u16string& u16_text,
                                uint32_t mods = 0);

  bool operator==(const NativeEvent& other) const = default;
};

// Dispatch result contract.
struct NativeDispatchResult {
  size_t requested{0};
  size_t inserted{0};
  int64_t raw_os_error{0};
  bool submission_known{false};
  std::string error_message;

  bool ok() const {
    return requested > 0 && inserted == requested && raw_os_error == 0;
  }

  bool operator==(const NativeDispatchResult& other) const = default;
};

// Common dispatcher interface.
class NativeInputDispatcher {
 public:
  virtual ~NativeInputDispatcher() = default;
  virtual NativeDispatchResult Dispatch(
      base::span<const NativeEvent> events) = 0;
};

// In-memory test dispatcher for unit tests (deterministic non-OS fake).
class TestNativeInputDispatcher : public NativeInputDispatcher {
 public:
  TestNativeInputDispatcher();
  ~TestNativeInputDispatcher() override;

  NativeDispatchResult Dispatch(
      base::span<const NativeEvent> events) override;

  const std::vector<NativeEvent>& dispatched_events() const {
    return dispatched_events_;
  }
  size_t event_count() const { return dispatched_events_.size(); }
  void Clear() { dispatched_events_.clear(); }

  void set_fail_all(bool fail) { fail_all_ = fail; }
  void set_simulated_error(int64_t err) { simulated_error_code_ = err; }
  void set_max_insert_count(std::optional<size_t> count) {
    max_insert_count_ = count;
  }

 private:
  std::vector<NativeEvent> dispatched_events_;
  bool fail_all_{false};
  int64_t simulated_error_code_{0};
  std::optional<size_t> max_insert_count_;
};

// Discriminated native action request.
struct NativeActionRequest {
  enum class ActionType {
    kClick,
    kType,
    kKey,
  };

  NativeActionRequest();
  ~NativeActionRequest();
  NativeActionRequest(const NativeActionRequest&);
  NativeActionRequest& operator=(const NativeActionRequest&);
  NativeActionRequest(NativeActionRequest&&) noexcept;
  NativeActionRequest& operator=(NativeActionRequest&&) noexcept;

  ActionType type{ActionType::kClick};
  std::string frame_token;
  int64_t tab_id{0};
  uint64_t lease_epoch{0};
  uint64_t document_epoch{0};

  // Click parameters
  gfx::PointF click_point_css;
  gfx::RectF target_rect_css;
  ClickMotionProfile motion_profile;

  // Type parameters
  std::string text;
  TypingOptions typing_options;

  // Key parameters
  std::string key;
  std::vector<std::string> modifiers;

  // Guard flag: set true if physical/unknown user modifiers are active
  bool user_modifiers_active{false};

  bool Validate() const;
};

// Result of a native action dispatch.
struct NativeActionResult {
  NativeActionResult();
  ~NativeActionResult();
  NativeActionResult(const NativeActionResult&);
  NativeActionResult& operator=(const NativeActionResult&);
  NativeActionResult(NativeActionResult&&) noexcept;
  NativeActionResult& operator=(NativeActionResult&&) noexcept;

  bool success{false};
  bool cancelled{false};
  bool cleanup_required{false};
  int error_code{0};
  std::string error_message;
  size_t events_dispatched{0};
  NativeDispatchResult dispatch_result;

  bool operator==(const NativeActionResult& other) const = default;
};

// Security context and authorization checks for native input.
struct NativeSecurityContext {
  NativeSecurityContext();
  ~NativeSecurityContext();
  NativeSecurityContext(const NativeSecurityContext&);
  NativeSecurityContext& operator=(const NativeSecurityContext&);
  NativeSecurityContext(NativeSecurityContext&&) noexcept;
  NativeSecurityContext& operator=(NativeSecurityContext&&) noexcept;

  int64_t tab_id{0};
  uint64_t expected_lease_epoch{0};
  uint64_t expected_document_epoch{0};

  bool is_foreground_window{true};
  bool is_target_tab_active{true};
  bool is_occluded{false};
  bool has_focus{true};
  bool user_held_modifiers{false};

  base::RepeatingCallback<uint64_t(int64_t tab_id)> lease_epoch_lookup;
  base::RepeatingCallback<bool()> foreground_check;
  base::RepeatingCallback<bool()> focus_check;
};

// Cross-platform Windows simulated input packet for cross-platform unit testing.
struct WinSimulatedInput {
  enum Type { kMouse, kKeyboard };
  Type type{kMouse};
  int mouse_dx{0};
  int mouse_dy{0};
  uint32_t mouse_flags{0};
  uint16_t vk{0};
  uint16_t scan{0};
  uint32_t key_flags{0};
  uint16_t unicode_char{0};

  bool operator==(const WinSimulatedInput& other) const = default;
};

std::vector<WinSimulatedInput> BuildWindowsInputsForNativeEvent(
    const NativeEvent& event);
uint16_t WindowsVkForKeyboardCode(ui::KeyboardCode key_code);
uint16_t WindowsScanCodeForKeyboardCode(ui::KeyboardCode key_code);
bool WindowsIsExtendedKey(ui::KeyboardCode key_code);

#if BUILDFLAG(IS_MAC)
uint16_t MacKeyCodeForKeyboardCode(ui::KeyboardCode key_code);
NativeInputDispatcher* GetPlatformNativeDispatcherMac();
#endif

#if BUILDFLAG(IS_WIN)
NativeInputDispatcher* GetPlatformNativeDispatcherWin();
#endif

class NativeInputOperation {
 public:
  enum class State {
    kCreated,
    kMoving,
    kPrePressDwell,
    kPressed,
    kPostPressDwell,
    kCompleted,
    kCancelled,
    kFailed,
  };

  NativeInputOperation(
      content::WebContents* wc,
      const VisualFrame& frame,
      const NativeActionRequest& request,
      NativeInputDispatcher* dispatcher,
      NativeSecurityContext context,
      base::OnceCallback<void(NativeActionResult)> callback);
  ~NativeInputOperation();

  void Start();
  void Cancel();

  base::WeakPtr<NativeInputOperation> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  State state() const { return state_; }
  bool cleanup_required() const { return cleanup_required_; }
  bool mouse_pressed() const { return mouse_pressed_; }
  const std::vector<ui::KeyboardCode>& held_keys() const { return held_keys_; }
  size_t events_dispatched() const { return events_dispatched_; }

 private:
  bool IsContextPreserved() const;
  void DoPress();
  void DoRelease();
  void ExecuteTypeAction();
  void ExecuteKeyAction();
  void HandleInterruptionDuringPress();
  void FinishSuccess();
  void FinishCancelled(bool cleanup_required,
                       int error_code = 0,
                       const std::string& message = "");
  void FinishWithError(int error_code, const std::string& message);

  base::WeakPtr<content::WebContents> web_contents_;
  VisualFrame frame_;
  NativeActionRequest request_;
  raw_ptr<NativeInputDispatcher> dispatcher_{nullptr};
  NativeSecurityContext context_;
  base::OnceCallback<void(NativeActionResult)> callback_;

  State state_{State::kCreated};
  NativePoint target_point_;
  bool cancelled_{false};
  bool cleanup_required_{false};
  bool mouse_pressed_{false};
  std::vector<ui::KeyboardCode> held_keys_;
  size_t events_dispatched_{0};
  base::WeakPtrFactory<NativeInputOperation> weak_factory_{this};
};
#endif  // !MAHO_STANDALONE_TEST

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

  // Normalizes CRLF ("\r\n") and isolated CR ("\r") into standard newline ("\n").
  static std::string NormalizeCrlf(const std::string& text);

  // Decodes a UTF-8 string into discrete typing units (code points with
  // UTF-16 code units and UTF-8 representations).
  static std::vector<TypingKeyUnit> Utf8ToTypingUnits(const std::string& text);

  // Builds the sequence of native events representing a single typing unit.
  // Emits Enter key (KeyDown + KeyUp VKEY_RETURN) for newline semantics.
  static std::vector<NativeEvent> BuildNativeEventsForUnit(
      const TypingKeyUnit& unit);

  // Type text by forwarding a trusted key sequence (RawKeyDown + Char + KeyUp)
  // per character. Framework-controlled inputs (e.g. React) require trusted
  // keystrokes to fire onChange; assigning element.value synthetically is
  // reverted on the next render. The caller is responsible for focusing and
  // clearing the target field first.
  static bool TypeText(content::WebContents* wc, const std::string& text);

  // Resolves on the next UI task after Chromium has accepted the entire
  // trusted key sequence. This is an event-loop boundary, not a timer; the
  // caller must still verify the target AX state before reporting success.
  static void TypeTextAsync(content::WebContents* wc,
                            const std::string& text,
                            TypingOptions options,
                            base::OnceCallback<void(bool)> callback);

#ifndef MAHO_STANDALONE_TEST
  // Sine ease-in-out progress curve mapped to [0, 1]. Returns 0 for t <= 0 and
  // 1 for t >= 1.
  static double EaseInOutFraction(double t);

  // Builds a deterministic pointer trajectory of exactly |waypoint_count|
  // points from |start| toward |end| following the ease-in-out profile. The
  // final point equals |end| exactly; intermediate points carry bounded
  // jitter of |jitter_px| px sampled from |rng|.
  static std::vector<gfx::PointF> ComputeTrajectory(
      const gfx::PointF& start,
      const gfx::PointF& end,
      int waypoint_count,
      int jitter_px,
      SplitMix64Prng& rng);

  // Bounded human-motion plan (plan item 8): approach waypoints with
  // perpendicular Gaussian jitter (sigma =
  // profile.approach_jitter_sigma_px, clamped at 3 sigma), an optional
  // bounded overshoot + correction kept inside |target_rect| (disabled for
  // tiny targets), and a truncated log-normal pre-click dwell in
  // profile.dwell_ms. Deterministic under |rng|.
  struct BoundedMotionPlan {
    BoundedMotionPlan();
    ~BoundedMotionPlan();
    BoundedMotionPlan(const BoundedMotionPlan&);
    BoundedMotionPlan& operator=(const BoundedMotionPlan&);
    BoundedMotionPlan(BoundedMotionPlan&&) noexcept;
    BoundedMotionPlan& operator=(BoundedMotionPlan&&) noexcept;

    std::vector<gfx::PointF> waypoints;
    int32_t dwell_ms{0};
    bool overshoot_applied{false};
  };
  static BoundedMotionPlan ComputeBoundedMotion(
      const gfx::PointF& start,
      const gfx::RectF& target_rect,
      const ClickMotionProfile& profile,
      SplitMix64Prng& rng);

  // Box-Muller standard normal draw from |rng| (deterministic).
  static double NextGaussian(SplitMix64Prng& rng);

  // Truncated log-normal dwell sample clamped to profile.dwell_ms.
  static int32_t NextDwellMs(const ClickMotionProfile& profile,
                             SplitMix64Prng& rng);

  // Dispatches a trusted left-button click whose pointer events travel an
  // interpolated ease-in-out trajectory over a bounded, randomized duration,
  // followed by a pre-click dwell and a press-to-release gap, all drawn from
  // |profile|. Invokes |callback| with true once the full sequence has been
  // forwarded, or false if dispatch failed (e.g. no WebContents/task runner).
  // Requires a UI-sequenced task runner; otherwise falls back to synchronous
  // ClickAt().
  static void ClickAtAsync(
      content::WebContents* wc,
      int x_css,
      int y_css,
      const std::vector<std::string>& modifiers,
      bool sensitive,
      MahoActionMarkerService::Kind marker_kind,
      const ClickMotionProfile& profile,
      base::OnceCallback<void(bool)> callback);

  // Resolves a CSS viewport coordinate from a captured VisualFrame to platform-native
  // screen coordinates (NativePoint).
  // Verifies captured bitmap-to-CSS mapping, generates page zoom and visual viewport
  // transform, and adds the view origin from GetViewBounds.
  // macOS: screen DIPs become unscaled global CG points (no Retina scale, no Y inversion).
  // Windows: ScreenWin instance DIPToScreenPoint, then physical virtual-desktop normalization.
  // Non-mac/non-win: returns std::nullopt.
  static std::optional<NativePoint> ResolveNativePoint(
      const VisualFrame& frame,
      const gfx::PointF& css);

  // Overload verifying against a live WebContents view bounds.
  static std::optional<NativePoint> ResolveNativePoint(
      content::WebContents* wc,
      const VisualFrame& frame,
      const gfx::PointF& css);

  // Transforms CSS viewport coordinates to screen DIP coordinates by verifying
  // the frame's bitmap-to-CSS mapping, applying visual viewport offset, page zoom,
  // and visual viewport scale, then offsetting by view_bounds_in_screen.origin().
  static std::optional<gfx::PointF> CssToScreenDip(
      const VisualFrame& frame,
      const gfx::PointF& css);

  // Normalizes a Windows physical pixel point to virtual desktop coordinates:
  // (pixel - origin) * 65535 / (extent - 1) with VIRTUALDESK and ABSOLUTE flags.
  // Cross-platform helper for unit tests and Windows platform implementation.
  static std::optional<NativePoint> NormalizeWindowsNativePoint(
      const gfx::PointF& screen_dip,
      const gfx::Point& pixel_point,
      const gfx::Rect& virtual_screen);

  // Test helper for Windows resolution with explicit physical pixel and virtual screen.
  static std::optional<NativePoint> ResolveNativePointWinForTesting(
      const VisualFrame& frame,
      const gfx::PointF& css,
      const gfx::Point& pixel_point,
      const gfx::Rect& virtual_screen);

  // Probes system accessibility / native input synthesis availability.
  // macOS: queries AXIsProcessTrustedWithOptions(nullptr) without prompting.
  // Windows: verifies interactive window station and reports SendInput status.
  // Linux: returns available = false (unsupported).
  static NativeInputAvailability CheckNativeInputAvailability();

  // Test helper to override native input availability during tests.
  static void SetNativeInputAvailabilityOverrideForTesting(
      std::optional<NativeInputAvailability> override_val);

  // Explicit native async entry point with security context.
  static base::WeakPtr<NativeInputOperation> DispatchNativeActionAsync(
      content::WebContents* wc,
      const VisualFrame& frame,
      const NativeActionRequest& request,
      NativeInputDispatcher* dispatcher,
      NativeSecurityContext context,
      base::OnceCallback<void(NativeActionResult)> callback);

  // Overload using default security context derived from request.
  static base::WeakPtr<NativeInputOperation> DispatchNativeActionAsync(
      content::WebContents* wc,
      const VisualFrame& frame,
      const NativeActionRequest& request,
      NativeInputDispatcher* dispatcher,
      base::OnceCallback<void(NativeActionResult)> callback);

  // Converts a Unicode code point to 1 or 2 UTF-16 code units (surrogate pair).
  static std::vector<uint16_t> CodePointToUtf16(char32_t code_point);
#endif  // !MAHO_STANDALONE_TEST

  static bool KeyPress(content::WebContents* wc,
                       const std::string& key,
                       const std::vector<std::string>& modifiers);

  // Direct forwarding skips Cocoa's menu command translation. Returns the
  // missing macOS editing command to attach to the trusted raw keydown.
  static std::string MacEditCommandForKeyPress(ui::KeyboardCode key_code,
                                             int web_modifiers);

  static ui::KeyboardCode KeyStringToKeyCode(const std::string& key);

  static int ModifierListToWebModifiers(
      const std::vector<std::string>& modifiers);
};

#ifndef MAHO_STANDALONE_TEST
inline std::optional<NativePoint> ResolveNativePoint(
    const VisualFrame& frame,
    const gfx::PointF& css) {
  return MahoMcpInputSynthesizer::ResolveNativePoint(frame, css);
}

inline NativeInputAvailability CheckNativeInputAvailability() {
  return MahoMcpInputSynthesizer::CheckNativeInputAvailability();
}

inline NativeInputAvailability PreflightNativeInput() {
  return MahoMcpInputSynthesizer::CheckNativeInputAvailability();
}
#endif

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
