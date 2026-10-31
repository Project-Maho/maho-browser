// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
#define MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/no_destructor.h"
#include "third_party/skia/include/core/SkColor.h"

class Browser;

// Static state holder for parsed Space themes and browser-owned previews.
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
    ThemeData();
    ThemeData(const ThemeData&);
    ThemeData& operator=(const ThemeData&);
    ThemeData(ThemeData&&) noexcept;
    ThemeData& operator=(ThemeData&&) noexcept;
    ~ThemeData();

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

  using ChangedSpaceIds = std::vector<std::string>;

  // Replaces the committed cache from the all-Spaces Rust Core snapshot and
  // returns the Space IDs whose resolved committed theme changed.
  static ChangedSpaceIds UpdateFromCore();
  static ChangedSpaceIds UpdateFromSnapshot(const std::string& spaces_json);

  static ChangedSpaceIds UpdateFromSnapshotForTesting(
      const std::string& spaces_json);

  // Resolves browser-local theme state with preview(owner) taking precedence
  // over the committed theme for the browser's canonical active Space.
  // Incognito/guest and null browser boundaries always return no theme.
  static std::optional<ThemeData> GetThemeDataForBrowser(Browser* browser);

  // Returns cached theme data for the active space, or std::nullopt if none.
  static std::optional<ThemeData> GetThemeData();

  // Returns true when the OS system appearance is dark. Pref-independent: reads
  // ui::NativeTheme's native-UI instance, not prefs::kBrowserColorScheme.
  static bool IsOsDarkMode();

  // Returns a chrome tint color for space theme blending, or SK_ColorTRANSPARENT
  // if no space has an active color. Preserved as a compatibility helper for
  // primary-color tinting. Applies appearance-aware adjustments, with subtle
  // theme-kind-specific moderation for non-solid themes so downstream chrome
  // mixing can preserve stronger surface differentiation.
  static SkColor GetChromeTint(bool is_dark, uint8_t alpha = 15);

  // Sets a preview-only override for its owning regular browser. Does NOT write
  // to core.
  // `theme_json` must be a valid SpaceThemeData JSON string; returns false if
  // parsing fails (the existing preview is left unchanged on failure).
  static bool SetPreviewOverride(Browser* browser,
                                 const std::string& theme_json);

  static void ClearPreviewOverride(Browser* browser);

  static bool HasPreviewOverride(Browser* browser);
  static size_t GetPreviewOverrideCountForTesting();

  // Clears cached state.
  static void Clear();

 private:
  static base::NoDestructor<base::flat_map<std::string, ThemeData>>
      committed_themes_;
  static base::NoDestructor<base::flat_map<Browser*, ThemeData>>
      preview_overrides_;
};

#endif  // MAHO_BROWSER_UI_THEME_MAHO_SPACE_THEME_STATE_H_
