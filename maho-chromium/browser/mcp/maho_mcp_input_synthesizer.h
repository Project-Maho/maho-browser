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
#include "base/functional/callback.h"
#include "maho/browser/ui/views/maho_action_marker_service.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/events/keycodes/keyboard_codes.h"
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

// Purpose discipline: this is input UX and accessibility pacing ONLY.
// Anti-bot evasion is explicitly out of scope and all delays are strictly bounded.
// No site-detection or adaptive behavior is implemented.
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

  // Decodes a UTF-8 string into discrete typing units (code points with
  // UTF-16 code units and UTF-8 representations).
  static std::vector<TypingKeyUnit> Utf8ToTypingUnits(const std::string& text);

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

  static bool KeyPress(content::WebContents* wc,
                       const std::string& key,
                       const std::vector<std::string>& modifiers);

  static ui::KeyboardCode KeyStringToKeyCode(const std::string& key);

  static int ModifierListToWebModifiers(
      const std::vector<std::string>& modifiers);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_INPUT_SYNTHESIZER_H_
