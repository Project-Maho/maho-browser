// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
#define MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/no_destructor.h"
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

  // Per-space forced appearance. kAuto follows the browser/OS setting.
  enum class ColorScheme {
    kAuto,
    kLight,
    kDark,
  };

  struct SpaceHSV {
    SkScalar hue;        // 0-360
    SkScalar saturation; // 0.0-1.0
    SkScalar value;      // 0.0-1.0

    bool operator==(const SpaceHSV& other) const {
      return hue == other.hue && saturation == other.saturation &&
             value == other.value;
    }
    bool operator!=(const SpaceHSV& other) const {
      return !(*this == other);
    }
  };

  struct ThemeData {
    ThemeKind kind;
    SpaceHSV primary_hsv;
    std::optional<SpaceHSV> secondary_hsv;
    // All parsed gradient stops, canonical primary-first order. Solid themes
    // hold a single stop. Empty only for the achromatic fallback.
    std::vector<SpaceHSV> stops;
    float opacity;
    float texture;
    float grain;
    ColorScheme color_scheme = ColorScheme::kAuto;
    bool achromatic = false;

    bool operator==(const ThemeData& other) const {
      return kind == other.kind && primary_hsv == other.primary_hsv &&
             secondary_hsv == other.secondary_hsv && stops == other.stops &&
             opacity == other.opacity &&
             texture == other.texture && grain == other.grain &&
             color_scheme == other.color_scheme && achromatic == other.achromatic;
    }
    bool operator!=(const ThemeData& other) const {
      return !(*this == other);
    }
  };

  // Updates cached state by reading the active space's theme from Rust core FFI.
  // Returns true if the active theme changed compared to the previous state.
  static bool UpdateFromCore();

  // Returns cached theme data for the active space, or std::nullopt if none.
  static std::optional<ThemeData> GetThemeData();

  // Returns the active space's forced dark-mode preference for the SIDEBAR
  // only: true = force dark, false = force light, std::nullopt = follow the OS
  // system appearance (kAuto or no active theme). Honors preview overrides.
  // This no longer drives the whole-window ColorProviderKey; the browser chrome
  // follows prefs::kBrowserColorScheme independently.
  static std::optional<bool> GetForcedDarkMode();

  // Resolves the sidebar's effective dark mode: the space theme scheme when set
  // (kLight/kDark), otherwise `os_dark` (the OS system appearance, passed in by
  // the caller so this class stays free of ui::NativeTheme dependencies).
  static bool ResolveSidebarDarkMode(bool os_dark);

  // Returns true when the OS system appearance is dark. Pref-independent: reads
  // ui::NativeTheme's native-UI instance, not prefs::kBrowserColorScheme.
  static bool IsOsDarkMode();

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
  static base::NoDestructor<std::optional<ThemeData>> active_theme_;
  static base::NoDestructor<std::optional<ThemeData>> preview_override_;
};

#endif  // MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
