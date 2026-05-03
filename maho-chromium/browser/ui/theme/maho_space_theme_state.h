// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
#define MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_

#include <cstdint>
#include <optional>

#include "third_party/skia/include/core/SkColor.h"

// Static state holder for the active space's theme color.
// Updated when the active space changes; read by AddMahoColorMixers.
// All methods must be called on the UI thread.
class MahoSpaceThemeState {
 public:
  struct SpaceHSV {
    SkScalar hue;        // 0-360
    SkScalar saturation; // 0.0-1.0
    SkScalar value;      // 0.0-1.0
  };

  // Updates cached state by reading the active space's color from Rust core FFI.
  static void UpdateFromCore();

  // Returns a chrome tint color for space theme blending, or SK_ColorTRANSPARENT
  // if no space has an active color.
  // Applies appearance-aware HSV adjustments matching Swift SpaceThemeManager:
  //   Dark mode: original saturation/brightness
  //   Light mode: saturation *= 0.85 (capped 0.9), brightness *= 1.1 (capped 1.0)
  static SkColor GetChromeTint(bool is_dark, uint8_t alpha = 15);

  // Clears cached state.
  static void Clear();

 private:
  static std::optional<SpaceHSV> active_space_hsv_;
};

#endif  // MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
