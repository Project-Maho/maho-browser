// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
#define MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "third_party/skia/include/core/SkColor.h"

// Static state holder for the active space's theme color data.
// Updated when the active space changes; read by AddMahoColorMixers.
// All methods must be called on the UI thread.
class MahoSpaceThemeState {
 public:
  enum class ThemeKind {
    kSolid,
    kGradient,
    kZen,
  };

  struct SpaceHSV {
    SkScalar hue;        // 0-360
    SkScalar saturation; // 0.0-1.0
    SkScalar value;      // 0.0-1.0
  };

  struct ThemeData {
    ThemeKind kind;
    SpaceHSV primary_hsv;
    std::optional<SpaceHSV> secondary_hsv;
    float opacity;
    float texture;
    float grain;
  };

  // Updates cached state by reading the active space's theme from Rust core FFI.
  static void UpdateFromCore();

  // Returns cached theme data for the active space, or std::nullopt if none.
  static std::optional<ThemeData> GetThemeData();

  // Returns a chrome tint color for space theme blending, or SK_ColorTRANSPARENT
  // if no space has an active color. Preserved as a compatibility helper for
  // primary-color tinting. Applies appearance-aware adjustments, with subtle
  // theme-kind-specific moderation for non-solid themes so downstream chrome
  // mixing can preserve stronger surface differentiation.
  static SkColor GetChromeTint(bool is_dark, uint8_t alpha = 15);

  // Sets a preview-only override. `GetThemeData()` returns this instead of the
  // persisted active theme while a preview is active. Does NOT write to core.
  // `theme_json` must be a valid SpaceThemeData JSON string; returns false if
  // parsing fails (the existing preview is left unchanged on failure).
  static bool SetPreviewOverride(const std::string& theme_json);

  // Clears the preview override. `GetThemeData()` reverts to `active_theme_`.
  static void ClearPreviewOverride();

  // Returns true if a preview override is currently active.
  static bool HasPreviewOverride();

  // Clears cached state.
  static void Clear();

 private:
  static std::optional<ThemeData> active_theme_;
  static std::optional<ThemeData> preview_override_;
};

#endif  // MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
