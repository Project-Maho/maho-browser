// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_theme_helper.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "build/build_config.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme.h"

namespace {

constexpr SkColor kSidebarBackgroundDark =
    SkColorSetARGB(255, 39, 35, 54);
constexpr SkColor kSidebarBackgroundLight =
    SkColorSetARGB(255, 228, 229, 238);
constexpr SkAlpha kSidebarMinAlpha = 24;
constexpr size_t kGradientSampleCount = 17;

bool ResolveDarkFamily(const MahoSidebarThemeEnvironment& environment) {
  if (!environment.theme) {
    return environment.os_dark;
  }
  switch (environment.theme->color_scheme) {
    case MahoSpaceThemeState::ColorScheme::kLight:
      return false;
    case MahoSpaceThemeState::ColorScheme::kDark:
      return true;
    case MahoSpaceThemeState::ColorScheme::kAuto:
      if (environment.theme->achromatic || environment.theme->stops.empty() ||
          environment.theme->primary_hsv.saturation <= 0.01f) {
        return environment.os_dark;
      }
      const SkScalar hsv[3] = {
          environment.theme->primary_hsv.hue,
          environment.theme->primary_hsv.saturation,
          environment.theme->primary_hsv.value,
      };
      return color_utils::IsDark(SkHSVToColor(hsv));
  }
  return environment.os_dark;
}

float SidebarBlendForKind(MahoSpaceThemeState::ThemeKind kind) {
  switch (kind) {
    case MahoSpaceThemeState::ThemeKind::kSolid:
      return 0.08f;
    case MahoSpaceThemeState::ThemeKind::kGradient:
      return 0.11f;
    case MahoSpaceThemeState::ThemeKind::kZen:
      return 0.09f;
  }
  return 0.08f;
}

SkColor MakeTintedStop(const MahoSpaceThemeState::SpaceHSV& hsv,
                       MahoSpaceThemeState::ThemeKind kind,
                       bool dark_family,
                       SkColor base_surface,
                       SkAlpha surface_alpha) {
  SkScalar saturation = hsv.saturation;
  SkScalar value = dark_family ? std::clamp(hsv.value + 0.02f, 0.0f, 1.0f)
                               : std::clamp(hsv.value * 1.08f, 0.0f, 1.0f);
  if (!dark_family) {
    saturation = std::clamp(saturation * 0.85f, 0.0f, 0.95f);
  }
  const SkScalar adjusted_hsv[3] = {
      hsv.hue,
      std::clamp(saturation, 0.0f, 1.0f),
      value,
  };
  const SkAlpha tint_alpha =
      kind == MahoSpaceThemeState::ThemeKind::kSolid ? 22 : 18;
  const SkColor tint = SkColorSetA(SkHSVToColor(tint_alpha, adjusted_hsv), 255);
  return SkColorSetA(
      maho_theme::BlendColors(base_surface, tint, SidebarBlendForKind(kind)),
      surface_alpha);
}

SkColor SampleGradient(const std::vector<SkColor>& stops, float position) {
  if (stops.size() == 1) {
    return stops.front();
  }
  const float scaled = position * static_cast<float>(stops.size() - 1);
  const size_t left = std::min(static_cast<size_t>(scaled), stops.size() - 2);
  return maho_theme::BlendColors(stops[left], stops[left + 1],
                                 scaled - static_cast<float>(left));
}

void BuildContrastSurfaces(const std::vector<SkColor>& surface_stops,
                           const std::vector<SkColor>& backdrops,
                           MahoSidebarPalette* palette) {
  palette->normal_contrast_surfaces.clear();
  palette->hover_contrast_surfaces.clear();
  palette->active_contrast_surfaces.clear();
  palette->selected_contrast_surfaces.clear();
  for (SkColor backdrop : backdrops) {
    const SkColor opaque_backdrop = SkColorSetA(backdrop, SK_AlphaOPAQUE);
    for (size_t i = 0; i < kGradientSampleCount; ++i) {
      const float position =
          static_cast<float>(i) / static_cast<float>(kGradientSampleCount - 1);
      const SkColor sample = SampleGradient(surface_stops, position);
      const SkColor normal =
          color_utils::GetResultingPaintColor(sample, opaque_backdrop);
      palette->normal_contrast_surfaces.push_back(normal);
      palette->hover_contrast_surfaces.push_back(
          color_utils::GetResultingPaintColor(palette->row_hover, normal));
      palette->active_contrast_surfaces.push_back(
          color_utils::GetResultingPaintColor(palette->row_active, normal));
      palette->selected_contrast_surfaces.push_back(
          color_utils::GetResultingPaintColor(palette->row_selected, normal));
    }
  }
}

// Surfaces the sidebar actually paints in opaque mode (Linux always, other
// platforms while a library/Spaces overlay is open): the opaque stops with
// every row state composited on top. Text roles must stay readable there too,
// not only on the translucent stops composited over the backdrop.
std::vector<SkColor> OpaqueModeContrastSurfaces(
    const MahoSidebarPalette& palette) {
  std::vector<SkColor> surfaces;
  if (palette.opaque_contrast_stops.empty()) {
    return surfaces;
  }
  surfaces.reserve(kGradientSampleCount * 4);
  for (size_t i = 0; i < kGradientSampleCount; ++i) {
    const float position =
        static_cast<float>(i) / static_cast<float>(kGradientSampleCount - 1);
    const SkColor normal =
        SampleGradient(palette.opaque_contrast_stops, position);
    surfaces.push_back(normal);
    surfaces.push_back(
        color_utils::GetResultingPaintColor(palette.row_hover, normal));
    surfaces.push_back(
        color_utils::GetResultingPaintColor(palette.row_active, normal));
    surfaces.push_back(
        color_utils::GetResultingPaintColor(palette.row_selected, normal));
  }
  return surfaces;
}

std::vector<SkColor> AllContrastSurfaces(const MahoSidebarPalette& palette) {
  std::vector<SkColor> surfaces = OpaqueModeContrastSurfaces(palette);
  surfaces.reserve(surfaces.size() +
                   palette.normal_contrast_surfaces.size() * 4);
  surfaces.insert(surfaces.end(), palette.normal_contrast_surfaces.begin(),
                  palette.normal_contrast_surfaces.end());
  surfaces.insert(surfaces.end(), palette.hover_contrast_surfaces.begin(),
                  palette.hover_contrast_surfaces.end());
  surfaces.insert(surfaces.end(), palette.active_contrast_surfaces.begin(),
                  palette.active_contrast_surfaces.end());
  surfaces.insert(surfaces.end(), palette.selected_contrast_surfaces.begin(),
                  palette.selected_contrast_surfaces.end());
  return surfaces;
}

SkColor ResolveRoleColor(SkColor candidate,
                         SkColor endpoint,
                         float minimum_contrast,
                         const std::vector<SkColor>& surfaces) {
  SkColor resolved = candidate;
  for (SkColor surface : surfaces) {
    resolved = color_utils::BlendForMinContrast(
                   resolved, surface, endpoint, minimum_contrast)
                   .color;
  }
  return resolved;
}

bool MeetsContrast(SkColor foreground,
                   float minimum_contrast,
                   const std::vector<SkColor>& surfaces) {
  return std::all_of(surfaces.begin(), surfaces.end(), [&](SkColor surface) {
    return color_utils::GetContrastRatio(foreground, surface) >=
           minimum_contrast;
  });
}

void ResolveRoleColors(MahoSidebarPalette* palette) {
  const std::vector<SkColor> surfaces = AllContrastSurfaces(*palette);
  const SkColor endpoint = palette->dark_family
                               ? SK_ColorWHITE
                               : color_utils::GetColorWithMaxContrast(
                                     SK_ColorWHITE);
  const SkColor primary_candidate =
      palette->dark_family ? SkColorSetRGB(243, 243, 247)
                           : SkColorSetRGB(45, 51, 74);
  const SkColor secondary_candidate =
      palette->dark_family ? SkColorSetRGB(143, 143, 159)
                           : SkColorSetRGB(128, 136, 166);
  const SkColor tertiary_candidate = secondary_candidate;

  palette->primary_text = ResolveRoleColor(
      primary_candidate, endpoint, color_utils::kMinimumReadableContrastRatio,
      surfaces);
  palette->secondary_text = ResolveRoleColor(
      secondary_candidate, endpoint,
      color_utils::kMinimumReadableContrastRatio, surfaces);
  // Subtle roles held to the visible minimum (3.0:1) read as washed out on a
  // light-family surface: mid-tone blue-gray candidates land near the floor
  // and placeholder icons/section headers become hard to distinguish. Hold
  // light-family subtle roles to the readable minimum instead.
  const float subtle_minimum =
      palette->dark_family
          ? color_utils::kMinimumVisibleContrastRatio
          : color_utils::kMinimumReadableContrastRatio;
  palette->tertiary_text = ResolveRoleColor(tertiary_candidate, endpoint,
                                            subtle_minimum, surfaces);
  palette->disabled_text = palette->tertiary_text;
  palette->neutral_glyph = ResolveRoleColor(secondary_candidate, endpoint,
                                            subtle_minimum, surfaces);
}

bool PaletteMeetsContrast(const MahoSidebarPalette& palette) {
  const std::vector<SkColor> surfaces = AllContrastSurfaces(palette);
  return MeetsContrast(palette.primary_text,
                       color_utils::kMinimumReadableContrastRatio, surfaces) &&
         MeetsContrast(palette.secondary_text,
                       color_utils::kMinimumReadableContrastRatio, surfaces) &&
         MeetsContrast(palette.neutral_glyph,
                       palette.dark_family
                           ? color_utils::kMinimumVisibleContrastRatio
                           : color_utils::kMinimumReadableContrastRatio,
                       surfaces);
}

}  // namespace

MahoSidebarThemeEnvironment::MahoSidebarThemeEnvironment() = default;
MahoSidebarThemeEnvironment::MahoSidebarThemeEnvironment(
    const MahoSidebarThemeEnvironment&) = default;
MahoSidebarThemeEnvironment& MahoSidebarThemeEnvironment::operator=(
    const MahoSidebarThemeEnvironment&) = default;
MahoSidebarThemeEnvironment::MahoSidebarThemeEnvironment(
    MahoSidebarThemeEnvironment&&) noexcept = default;
MahoSidebarThemeEnvironment& MahoSidebarThemeEnvironment::operator=(
    MahoSidebarThemeEnvironment&&) noexcept = default;
MahoSidebarThemeEnvironment::~MahoSidebarThemeEnvironment() = default;

MahoSidebarPalette::MahoSidebarPalette() = default;
MahoSidebarPalette::MahoSidebarPalette(const MahoSidebarPalette&) = default;
MahoSidebarPalette& MahoSidebarPalette::operator=(
    const MahoSidebarPalette&) = default;
MahoSidebarPalette::MahoSidebarPalette(MahoSidebarPalette&&) noexcept =
    default;
MahoSidebarPalette& MahoSidebarPalette::operator=(
    MahoSidebarPalette&&) noexcept = default;
MahoSidebarPalette::~MahoSidebarPalette() = default;

bool operator==(const MahoSidebarPalette& lhs,
                const MahoSidebarPalette& rhs) {
  return lhs.dark_family == rhs.dark_family &&
         lhs.used_surface_fallback == rhs.used_surface_fallback &&
         lhs.forced_colors == rhs.forced_colors && lhs.opaque == rhs.opaque &&
         lhs.surface_stops == rhs.surface_stops &&
         lhs.opaque_contrast_stops == rhs.opaque_contrast_stops &&
         lhs.content_surface_stops == rhs.content_surface_stops &&
         lhs.row_hover == rhs.row_hover && lhs.row_active == rhs.row_active &&
         lhs.row_selected == rhs.row_selected &&
         lhs.normal_contrast_surfaces == rhs.normal_contrast_surfaces &&
         lhs.hover_contrast_surfaces == rhs.hover_contrast_surfaces &&
         lhs.active_contrast_surfaces == rhs.active_contrast_surfaces &&
         lhs.selected_contrast_surfaces == rhs.selected_contrast_surfaces &&
         lhs.primary_text == rhs.primary_text &&
         lhs.secondary_text == rhs.secondary_text &&
         lhs.tertiary_text == rhs.tertiary_text &&
         lhs.disabled_text == rhs.disabled_text &&
         lhs.neutral_glyph == rhs.neutral_glyph &&
         lhs.outline == rhs.outline && lhs.focus_ring == rhs.focus_ring &&
         lhs.grain == rhs.grain;
}

bool operator!=(const MahoSidebarPalette& lhs,
                const MahoSidebarPalette& rhs) {
  return !(lhs == rhs);
}

MahoSidebarPalette ResolveMahoSidebarPalette(
    const MahoSidebarThemeEnvironment& environment) {
  MahoSidebarPalette palette;
  if (environment.forced_colors) {
    const MahoSidebarForcedColors& forced = *environment.forced_colors;
    palette.dark_family = environment.os_dark;
    palette.forced_colors = true;
    palette.surface_stops = {SkColorSetA(forced.surface, SK_AlphaOPAQUE)};
    palette.opaque_contrast_stops = palette.surface_stops;
    palette.content_surface_stops = palette.surface_stops;
    palette.row_hover = SK_ColorTRANSPARENT;
    palette.row_active = SK_ColorTRANSPARENT;
    palette.row_selected = SK_ColorTRANSPARENT;
    palette.normal_contrast_surfaces = palette.surface_stops;
    palette.hover_contrast_surfaces = palette.surface_stops;
    palette.active_contrast_surfaces = palette.surface_stops;
    palette.selected_contrast_surfaces = palette.surface_stops;
    palette.primary_text = forced.on_surface;
    palette.secondary_text = forced.on_surface;
    palette.tertiary_text = forced.outline;
    palette.disabled_text = forced.outline;
    palette.neutral_glyph = forced.on_surface;
    palette.outline = forced.outline;
    palette.focus_ring = forced.focus;
    palette.grain = 0.0f;
    return palette;
  }

  palette.dark_family = ResolveDarkFamily(environment);
  const SkColor base_surface = palette.dark_family ? kSidebarBackgroundDark
                                                   : kSidebarBackgroundLight;
  const float opacity =
      environment.theme ? std::clamp(environment.theme->opacity, 0.0f, 1.0f)
                        : 0.0f;
  const SkAlpha surface_alpha = static_cast<SkAlpha>(
      kSidebarMinAlpha + (255 - kSidebarMinAlpha) * opacity);
  const bool has_theme_color =
      environment.theme && !environment.theme->achromatic &&
      !environment.theme->stops.empty();
  if (has_theme_color) {
    const size_t stop_count =
        std::min(environment.theme->stops.size(), size_t{3});
    palette.surface_stops.reserve(stop_count);
    for (size_t i = 0; i < stop_count; ++i) {
      palette.surface_stops.push_back(MakeTintedStop(
          environment.theme->stops[i], environment.theme->kind,
          palette.dark_family, base_surface, surface_alpha));
    }
  } else {
    palette.surface_stops.push_back(SkColorSetA(base_surface, surface_alpha));
  }
  for (SkColor stop : palette.surface_stops) {
    palette.opaque_contrast_stops.push_back(
        SkColorSetA(stop, SK_AlphaOPAQUE));
  }
  const SkColor maho_content_base = SkColorSetA(base_surface, SK_AlphaOPAQUE);
  for (SkColor stop : palette.surface_stops) {
    palette.content_surface_stops.push_back(
        color_utils::GetResultingPaintColor(stop, maho_content_base));
  }

  palette.row_hover = palette.dark_family
                          ? SkColorSetARGB(26, 255, 255, 255)
                          : SkColorSetARGB(20, 0, 0, 0);
  palette.row_active = palette.dark_family
                           ? SkColorSetARGB(51, 255, 255, 255)
                           : SkColorSetARGB(38, 0, 0, 0);
  palette.row_selected = palette.dark_family
                             ? SkColorSetARGB(38, 255, 255, 255)
                             : SkColorSetARGB(25, 0, 0, 0);

  const std::vector<SkColor> backdrops = environment.contrast_backdrops.empty()
                                             ? std::vector<SkColor>{base_surface}
                                             : environment.contrast_backdrops;
  BuildContrastSurfaces(palette.surface_stops, backdrops, &palette);
  ResolveRoleColors(&palette);
  if (!PaletteMeetsContrast(palette)) {
    palette.used_surface_fallback = true;
    BuildContrastSurfaces(palette.opaque_contrast_stops,
                          {SK_ColorTRANSPARENT}, &palette);
    ResolveRoleColors(&palette);
  }
  palette.outline = palette.neutral_glyph;
  palette.focus_ring = palette.primary_text;
  palette.grain = environment.theme
                      ? std::clamp(environment.theme->texture, 0.0f, 1.0f)
                      : 0.0f;
  return palette;
}

MahoSidebarThemeEnvironment BuildMahoSidebarThemeEnvironmentForBrowser(
    Browser* browser,
    bool os_dark,
    std::vector<SkColor> contrast_backdrops) {
  MahoSidebarThemeEnvironment environment;
  environment.theme = MahoSpaceThemeState::GetThemeDataForBrowser(browser);
  environment.os_dark = os_dark;
  environment.contrast_backdrops = std::move(contrast_backdrops);
  return environment;
}

MahoSidebarThemeEnvironment BuildMahoSidebarThemeEnvironmentForBrowser(
    Browser* browser,
    const ui::ColorProvider* color_provider,
    const ui::NativeTheme* native_theme) {
  const bool os_dark = native_theme &&
                       native_theme->preferred_color_scheme() ==
                           ui::NativeTheme::PreferredColorScheme::kDark;
  std::vector<SkColor> contrast_backdrops;
  if (color_provider) {
    contrast_backdrops.push_back(
        color_provider->GetColor(ui::kColorSysSurface));
  }
#if BUILDFLAG(IS_WIN)
  // On Windows 11 the translucent sidebar composites over a DWM acrylic
  // backdrop (ApplySidebarWindowVibrancy): a blur of whatever is behind the
  // window, anywhere from near-white to near-black and unknown to us. Resolve
  // text against both extremes so it stays readable over any desktop; when no
  // color can, the palette falls back to its opaque surface.
  contrast_backdrops.push_back(SK_ColorWHITE);
  contrast_backdrops.push_back(SK_ColorBLACK);
#endif
  MahoSidebarThemeEnvironment environment =
      BuildMahoSidebarThemeEnvironmentForBrowser(
          browser, os_dark, std::move(contrast_backdrops));
  if (!color_provider || !native_theme) {
    return environment;
  }

  const bool forced_colors =
      native_theme->forced_colors() !=
          ui::ColorProviderKey::ForcedColors::kNone ||
      native_theme->preferred_contrast() ==
          ui::NativeTheme::PreferredContrast::kMore ||
      native_theme->preferred_contrast() ==
          ui::NativeTheme::PreferredContrast::kCustom;
  if (forced_colors) {
    environment.forced_colors = MahoSidebarForcedColors{
        .surface = color_provider->GetColor(ui::kColorSysSurface),
        .on_surface = color_provider->GetColor(ui::kColorSysOnSurface),
        .outline = color_provider->GetColor(ui::kColorSysOutline),
        .focus = color_provider->GetColor(ui::kColorSysPrimary),
    };
  }
  return environment;
}

MahoSidebarPaletteHost::MahoSidebarPaletteHost() = default;
MahoSidebarPaletteHost::~MahoSidebarPaletteHost() = default;

void MahoSidebarPaletteHost::SetApplyCallback(ApplyCallback callback) {
  apply_callback_ = std::move(callback);
}

bool MahoSidebarPaletteHost::Update(
    MahoSidebarPaletteUpdateReason reason,
    const MahoSidebarThemeEnvironment& environment,
    bool opaque) {
  if (reason == MahoSidebarPaletteUpdateReason::kCosmetic) {
    return false;
  }

  MahoSidebarPalette next_palette = ResolveMahoSidebarPalette(environment);
  next_palette.opaque = opaque;

  // A native-theme notification that resolves to the snapshot already on screen
  // is a no-op: keep the counters and skip the fan-out so no dependent surface
  // (docked rail, library overlay, Spaces overlay) is asked to repaint an
  // identical palette. Structural and preview updates keep their unconditional
  // recompute contract -- their inputs are not fully captured by `environment`.
  if (reason == MahoSidebarPaletteUpdateReason::kNativeTheme &&
      has_palette() && palette_ == next_palette) {
    return false;
  }

  ++compute_count_;
  palette_ = std::move(next_palette);
  opaque_ = opaque;
  ++apply_count_;
  if (apply_callback_) {
    apply_callback_.Run(palette_);
  }
  return true;
}

namespace maho_theme {

SkColor BlendColors(SkColor base, SkColor overlay, float fraction) {
  const float inv = 1.0f - fraction;
  return SkColorSetARGB(
      static_cast<uint8_t>(SkColorGetA(base) * inv +
                           SkColorGetA(overlay) * fraction),
      static_cast<uint8_t>(SkColorGetR(base) * inv +
                           SkColorGetR(overlay) * fraction),
      static_cast<uint8_t>(SkColorGetG(base) * inv +
                           SkColorGetG(overlay) * fraction),
      static_cast<uint8_t>(SkColorGetB(base) * inv +
                           SkColorGetB(overlay) * fraction));
}

}  // namespace maho_theme
