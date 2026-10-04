// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_THEME_HELPER_H_
#define MAHO_BROWSER_UI_THEME_MAHO_THEME_HELPER_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "third_party/skia/include/core/SkColor.h"

class Browser;

namespace ui {
class ColorProvider;
class NativeTheme;
}  // namespace ui

struct MahoSidebarForcedColors {
  SkColor surface = SK_ColorBLACK;
  SkColor on_surface = SK_ColorWHITE;
  SkColor outline = SK_ColorWHITE;
  SkColor focus = SK_ColorWHITE;
};

struct MahoSidebarThemeEnvironment {
  MahoSidebarThemeEnvironment();
  MahoSidebarThemeEnvironment(const MahoSidebarThemeEnvironment&);
  MahoSidebarThemeEnvironment& operator=(
      const MahoSidebarThemeEnvironment&);
  MahoSidebarThemeEnvironment(MahoSidebarThemeEnvironment&&) noexcept;
  MahoSidebarThemeEnvironment& operator=(
      MahoSidebarThemeEnvironment&&) noexcept;
  ~MahoSidebarThemeEnvironment();

  std::optional<MahoSpaceThemeState::ThemeData> theme;
  bool os_dark = false;
  std::vector<SkColor> contrast_backdrops;
  std::optional<MahoSidebarForcedColors> forced_colors;
};

struct MahoSidebarPalette {
  MahoSidebarPalette();
  MahoSidebarPalette(const MahoSidebarPalette&);
  MahoSidebarPalette& operator=(const MahoSidebarPalette&);
  MahoSidebarPalette(MahoSidebarPalette&&) noexcept;
  MahoSidebarPalette& operator=(MahoSidebarPalette&&) noexcept;
  ~MahoSidebarPalette();

  bool dark_family = false;
  bool used_surface_fallback = false;
  bool forced_colors = false;
  std::vector<SkColor> surface_stops;
  std::vector<SkColor> opaque_contrast_stops;
  // Opacity-aware opaque stops: each translucent surface stop composited over
  // the base surface. Used to paint the content pane so lowering the Space
  // opacity mutes the content in step with the sidebar's perceived tone.
  std::vector<SkColor> content_surface_stops;
  SkColor row_hover = SK_ColorTRANSPARENT;
  SkColor row_active = SK_ColorTRANSPARENT;
  SkColor row_selected = SK_ColorTRANSPARENT;
  std::vector<SkColor> normal_contrast_surfaces;
  std::vector<SkColor> hover_contrast_surfaces;
  std::vector<SkColor> active_contrast_surfaces;
  std::vector<SkColor> selected_contrast_surfaces;
  SkColor primary_text = SK_ColorTRANSPARENT;
  SkColor secondary_text = SK_ColorTRANSPARENT;
  SkColor tertiary_text = SK_ColorTRANSPARENT;
  SkColor disabled_text = SK_ColorTRANSPARENT;
  SkColor neutral_glyph = SK_ColorTRANSPARENT;
  SkColor outline = SK_ColorTRANSPARENT;
  SkColor focus_ring = SK_ColorTRANSPARENT;
  float grain = 0.0f;
  // Paint mode this snapshot was resolved for. Stamped by
  // MahoSidebarPaletteHost::Update from the sidebar's own
  // ShouldUseOpaqueSidebarBackground(). Every surface that paints the snapshot
  // (docked rail, Downloads/Archive library overlay, Spaces overlay) reads this
  // field instead of deciding opacity for itself, so two surfaces of the same
  // theme can never disagree about which stop vector to paint.
  bool opaque = false;
};

// Value equality over every field, including `opaque`. Used by
// MahoSidebarPaletteHost::Update to recognize a native-theme notification that
// resolved to the snapshot already on screen.
bool operator==(const MahoSidebarPalette& lhs, const MahoSidebarPalette& rhs);
bool operator!=(const MahoSidebarPalette& lhs, const MahoSidebarPalette& rhs);

MahoSidebarPalette ResolveMahoSidebarPalette(
    const MahoSidebarThemeEnvironment& environment);

MahoSidebarThemeEnvironment BuildMahoSidebarThemeEnvironmentForBrowser(
    Browser* browser,
    bool os_dark,
    std::vector<SkColor> contrast_backdrops = {});

MahoSidebarThemeEnvironment BuildMahoSidebarThemeEnvironmentForBrowser(
    Browser* browser,
    const ui::ColorProvider* color_provider,
    const ui::NativeTheme* native_theme);

enum class MahoSidebarPaletteUpdateReason {
  kConstruction,
  kStructuralBridge,
  kPreview,
  kNativeTheme,
  kOpaqueMode,
  kCosmetic,
};

class MahoSidebarPaletteHost {
 public:
  using ApplyCallback =
      base::RepeatingCallback<void(const MahoSidebarPalette&)>;

  MahoSidebarPaletteHost();
  MahoSidebarPaletteHost(const MahoSidebarPaletteHost&) = delete;
  MahoSidebarPaletteHost& operator=(const MahoSidebarPaletteHost&) = delete;
  ~MahoSidebarPaletteHost();

  void SetApplyCallback(ApplyCallback callback);
  // Resolves `environment` into the host's snapshot and publishes it to the
  // apply callback. Returns false without touching the snapshot when the update
  // is a no-op: kCosmetic never computes, and a kNativeTheme notification that
  // resolves to the palette already held (including its `opaque` paint mode)
  // does not recompute or fan out.
  bool Update(MahoSidebarPaletteUpdateReason reason,
              const MahoSidebarThemeEnvironment& environment,
              bool opaque);

  const MahoSidebarPalette& palette() const { return palette_; }
  bool opaque() const { return opaque_; }
  bool has_palette() const { return apply_count_ > 0; }

  const MahoSidebarPalette& palette_for_testing() const { return palette_; }
  bool opaque_for_testing() const { return opaque_; }
  int compute_count_for_testing() const { return compute_count_; }
  int apply_count_for_testing() const { return apply_count_; }

 private:
  MahoSidebarPalette palette_;
  bool opaque_ = false;
  int compute_count_ = 0;
  int apply_count_ = 0;
  ApplyCallback apply_callback_;
};

// Utility functions for Maho theme color manipulation.
// Color registration is handled by AddMahoColorMixers (maho_color_mixer.h).
// Used for space theme tinting.

namespace maho_theme {

// Linearly blends two SkColors by the given fraction (0.0 = base, 1.0 = overlay).
// Intended for Phase 3 space theme tinting.
SkColor BlendColors(SkColor base, SkColor overlay, float fraction);

}  // namespace maho_theme

#endif  // MAHO_BROWSER_UI_THEME_MAHO_THEME_HELPER_H_
