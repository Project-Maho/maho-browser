// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_theme_helper.h"

#include "maho/browser/ui/theme/maho_color_id.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/test_browser_window.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace {

using ColorScheme = MahoSpaceThemeState::ColorScheme;
using SpaceHSV = MahoSpaceThemeState::SpaceHSV;
using ThemeData = MahoSpaceThemeState::ThemeData;

class MahoSidebarAuxiliaryPaletteTest : public views::ViewsTestBase {};
class MahoSidebarLibraryPaletteTest : public views::ViewsTestBase {};
using ThemeKind = MahoSpaceThemeState::ThemeKind;

SpaceHSV HsvForColor(SkColor color) {
  SkScalar hsv[3];
  SkColorToHSV(color, hsv);
  return {.hue = hsv[0], .saturation = hsv[1], .value = hsv[2]};
}

ThemeData MakeTheme(ThemeKind kind,
                    ColorScheme scheme,
                    std::initializer_list<SkColor> colors,
                    float opacity = 0.5f,
                    bool achromatic = false) {
  std::vector<SpaceHSV> stops;
  for (SkColor color : colors) {
    stops.push_back(HsvForColor(color));
  }
  const SpaceHSV fallback = HsvForColor(SK_ColorGRAY);
  const SpaceHSV primary = stops.empty() ? fallback : stops.front();
  ThemeData theme;
  theme.kind = kind;
  theme.primary_hsv = primary;
  theme.secondary_hsv = stops.size() > 1
                            ? std::optional<SpaceHSV>(stops[1])
                            : std::nullopt;
  theme.stops = std::move(stops);
  theme.opacity = opacity;
  theme.texture = 0.0f;
  theme.grain = 0.0f;
  theme.color_scheme = scheme;
  theme.achromatic = achromatic;
  return theme;
}

ThemeData MakeLegacyDefaultThemeFallback(SkColor color) {
  const SpaceHSV hsv = HsvForColor(color);
  ThemeData theme;
  theme.kind = ThemeKind::kSolid;
  theme.primary_hsv = hsv;
  theme.secondary_hsv = std::nullopt;
  theme.stops = {hsv};
  theme.opacity = 0.5f;
  theme.texture = 0.0f;
  theme.grain = 0.0f;
  theme.color_scheme = ColorScheme::kAuto;
  theme.achromatic = false;
  return theme;
}

MahoSidebarThemeEnvironment Environment(
    std::optional<ThemeData> theme,
    bool os_dark,
    std::vector<SkColor> backdrops = {}) {
  MahoSidebarThemeEnvironment environment;
  environment.theme = std::move(theme);
  environment.os_dark = os_dark;
  environment.contrast_backdrops = std::move(backdrops);
  return environment;
}

void ExpectContrastAtLeast(const MahoSidebarPalette& palette,
                           SkColor foreground,
                           float minimum) {
  const std::vector<const std::vector<SkColor>*> states = {
      &palette.normal_contrast_surfaces,
      &palette.hover_contrast_surfaces,
      &palette.active_contrast_surfaces,
      &palette.selected_contrast_surfaces,
  };
  for (const std::vector<SkColor>* surfaces : states) {
    ASSERT_FALSE(surfaces->empty());
    for (SkColor surface : *surfaces) {
      EXPECT_GE(color_utils::GetContrastRatio(foreground, surface), minimum)
          << "foreground=" << foreground << " surface=" << surface;
    }
  }
}

void ExpectPaletteRolesAtSurface(const MahoSidebarPalette& palette,
                                  SkColor surface) {
  // Light-family subtle roles are held to the readable minimum (4.5:1);
  // dark-family keeps the visible minimum (3.0:1).
  const float subtle_minimum = palette.dark_family ? 3.0f : 4.5f;
  EXPECT_GE(color_utils::GetContrastRatio(palette.primary_text, surface), 4.5f);
  EXPECT_GE(color_utils::GetContrastRatio(palette.secondary_text, surface),
            4.5f);
  EXPECT_GE(color_utils::GetContrastRatio(palette.tertiary_text, surface),
            subtle_minimum);
  EXPECT_GE(color_utils::GetContrastRatio(palette.disabled_text, surface),
            subtle_minimum);
  EXPECT_GE(color_utils::GetContrastRatio(palette.neutral_glyph, surface),
            subtle_minimum);
}

void ExpectSamePaletteSnapshot(const MahoSidebarPalette& expected,
                               const MahoSidebarPalette& actual) {
  EXPECT_EQ(expected.dark_family, actual.dark_family);
  EXPECT_EQ(expected.surface_stops, actual.surface_stops);
  EXPECT_EQ(expected.primary_text, actual.primary_text);
  EXPECT_EQ(expected.secondary_text, actual.secondary_text);
  EXPECT_EQ(expected.tertiary_text, actual.tertiary_text);
  EXPECT_EQ(expected.neutral_glyph, actual.neutral_glyph);
  EXPECT_EQ(expected.outline, actual.outline);
  EXPECT_FLOAT_EQ(expected.grain, actual.grain);
}

void ExpectReadableRailForegrounds(views::View* view,
                                   SkColor surface,
                                   const ui::ColorProvider* color_provider,
                                   int* label_count,
                                   int* glyph_count) {
  if (auto* label = views::AsViewClass<views::Label>(view)) {
    ++*label_count;
    EXPECT_GE(color_utils::GetContrastRatio(label->GetEnabledColor(), surface),
              color_utils::kMinimumReadableContrastRatio)
        << "label=" << label->GetText();
  }

  if (auto* image_view = views::AsViewClass<views::ImageView>(view)) {
    ++*glyph_count;
    const ui::ImageModel image = image_view->GetImageModel();
    ASSERT_TRUE(image.IsVectorIcon());
    const SkColor glyph_color =
        image.GetVectorIcon().color().ResolveToSkColor(color_provider);
    EXPECT_GE(color_utils::GetContrastRatio(glyph_color, surface),
              color_utils::kMinimumReadableContrastRatio);
  }

  for (views::View* child : view->children()) {
    ExpectReadableRailForegrounds(child, surface, color_provider, label_count,
                                  glyph_count);
  }
}

TEST(MahoSidebarPaletteTest, AutoUsesCanonicalPrimaryForBrightAndDarkThemes) {
  const ThemeData bright = MakeTheme(
      ThemeKind::kGradient, ColorScheme::kAuto,
      {SkColorSetRGB(0xF2, 0xD5, 0x65), SkColorSetRGB(0x12, 0x18, 0x24)});
  const ThemeData dark = MakeTheme(
      ThemeKind::kGradient, ColorScheme::kAuto,
      {SkColorSetRGB(0x12, 0x18, 0x24), SkColorSetRGB(0xF2, 0xD5, 0x65)});

  const MahoSidebarPalette bright_palette =
      ResolveMahoSidebarPalette(Environment(bright, true));
  const MahoSidebarPalette dark_palette =
      ResolveMahoSidebarPalette(Environment(dark, false));

  EXPECT_FALSE(bright_palette.dark_family);
  EXPECT_TRUE(dark_palette.dark_family);
}

TEST(MahoSidebarPaletteTest, AutoFallsBackToOsForNoColorAndAchromaticThemes) {
  const ThemeData achromatic = MakeTheme(ThemeKind::kGradient,
                                          ColorScheme::kAuto, {}, 0.5f, true);

  EXPECT_TRUE(ResolveMahoSidebarPalette(Environment(std::nullopt, true))
                  .dark_family);
  EXPECT_FALSE(ResolveMahoSidebarPalette(Environment(std::nullopt, false))
                   .dark_family);
  EXPECT_TRUE(ResolveMahoSidebarPalette(Environment(achromatic, true))
                  .dark_family);
  EXPECT_FALSE(ResolveMahoSidebarPalette(Environment(achromatic, false))
                   .dark_family);
}

TEST(MahoSidebarPaletteTest, SunAndMoonForceTheirPreferredFamilies) {
  const ThemeData sun = MakeTheme(ThemeKind::kSolid, ColorScheme::kLight,
                                  {SkColorSetRGB(0x08, 0x0A, 0x12)});
  const ThemeData moon = MakeTheme(ThemeKind::kSolid, ColorScheme::kDark,
                                   {SkColorSetRGB(0xFA, 0xF0, 0xC0)});

  EXPECT_FALSE(ResolveMahoSidebarPalette(Environment(sun, true)).dark_family);
  EXPECT_TRUE(ResolveMahoSidebarPalette(Environment(moon, false)).dark_family);
}

TEST(MahoSidebarPaletteTest, PreservesSolidOneTwoThreeStopAndZenShapes) {
  const std::vector<ThemeData> themes = {
      MakeTheme(ThemeKind::kSolid, ColorScheme::kAuto,
                {SkColorSetRGB(0x45, 0x67, 0x89)}),
      MakeTheme(ThemeKind::kGradient, ColorScheme::kAuto,
                {SkColorSetRGB(0x45, 0x67, 0x89)}),
      MakeTheme(ThemeKind::kGradient, ColorScheme::kAuto,
                {SkColorSetRGB(0x45, 0x67, 0x89),
                 SkColorSetRGB(0x98, 0x76, 0x54)}),
      MakeTheme(ThemeKind::kGradient, ColorScheme::kAuto,
                {SkColorSetRGB(0x45, 0x67, 0x89),
                 SkColorSetRGB(0x98, 0x76, 0x54),
                 SkColorSetRGB(0x25, 0xA0, 0x80)}),
      MakeTheme(ThemeKind::kZen, ColorScheme::kAuto,
                {SkColorSetRGB(0x45, 0x67, 0x89),
                 SkColorSetRGB(0x98, 0x76, 0x54)}),
  };
  const std::array<size_t, 5> expected_stop_counts = {1, 1, 2, 3, 2};

  for (size_t i = 0; i < themes.size(); ++i) {
    const MahoSidebarPalette palette =
        ResolveMahoSidebarPalette(Environment(themes[i], false));
    EXPECT_EQ(palette.surface_stops.size(), expected_stop_counts[i]);
    EXPECT_EQ(palette.opaque_contrast_stops.size(), expected_stop_counts[i]);
  }
}

TEST(MahoSidebarPaletteTest, MapsOpacityEndpointsToActualSurfaceAlpha) {
  const ThemeData transparent = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kAuto,
      {SkColorSetRGB(0x45, 0x67, 0x89)}, 0.0f);
  const ThemeData near_opaque = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kAuto,
      {SkColorSetRGB(0x45, 0x67, 0x89)}, 0.9f);

  const MahoSidebarPalette transparent_palette =
      ResolveMahoSidebarPalette(Environment(transparent, false));
  const MahoSidebarPalette near_opaque_palette =
      ResolveMahoSidebarPalette(Environment(near_opaque, false));

  ASSERT_EQ(transparent_palette.surface_stops.size(), 1u);
  ASSERT_EQ(near_opaque_palette.surface_stops.size(), 1u);
  EXPECT_EQ(SkColorGetA(transparent_palette.surface_stops[0]), 24u);
  EXPECT_EQ(SkColorGetA(near_opaque_palette.surface_stops[0]), 231u);
  EXPECT_EQ(SkColorGetA(transparent_palette.opaque_contrast_stops[0]), 255u);
}

TEST(MahoSidebarPaletteTest,
     LegacyDefaultThemeFallbackUsesActualSolidSingleStopContract) {
  const SkColor legacy_color = SkColorSetRGB(0x42, 0x73, 0xA5);
  const ThemeData legacy_theme = MakeLegacyDefaultThemeFallback(legacy_color);

  const MahoSidebarPalette palette =
      ResolveMahoSidebarPalette(Environment(legacy_theme, false));

  EXPECT_EQ(legacy_theme.kind, ThemeKind::kSolid);
  EXPECT_EQ(legacy_theme.stops.size(), 1u);
  EXPECT_FALSE(legacy_theme.secondary_hsv.has_value());
  EXPECT_FLOAT_EQ(legacy_theme.opacity, 0.5f);
  EXPECT_FLOAT_EQ(legacy_theme.texture, 0.0f);
  EXPECT_FLOAT_EQ(legacy_theme.grain, 0.0f);
  EXPECT_EQ(legacy_theme.color_scheme, ColorScheme::kAuto);
  EXPECT_EQ(palette.dark_family, color_utils::IsDark(legacy_color));
  ASSERT_EQ(palette.surface_stops.size(), 1u);
  EXPECT_EQ(SkColorGetA(palette.surface_stops[0]), 139u);
  EXPECT_FALSE(palette.used_surface_fallback);
  const float subtle_minimum = palette.dark_family ? 3.0f : 4.5f;
  ExpectContrastAtLeast(palette, palette.primary_text, 4.5f);
  ExpectContrastAtLeast(palette, palette.secondary_text, 4.5f);
  ExpectContrastAtLeast(palette, palette.tertiary_text, subtle_minimum);
  ExpectContrastAtLeast(palette, palette.disabled_text, subtle_minimum);
  ExpectContrastAtLeast(palette, palette.neutral_glyph, subtle_minimum);
}

TEST(MahoSidebarPaletteTest, SamplesContinuousGradientAndEveryRowState) {
  const SkColor backdrop = SkColorSetRGB(0x10, 0x12, 0x18);
  const ThemeData theme = MakeTheme(
      ThemeKind::kGradient, ColorScheme::kAuto,
      {SkColorSetRGB(0x18, 0x2A, 0x40), SkColorSetRGB(0x5A, 0x31, 0x78),
       SkColorSetRGB(0x15, 0x66, 0x63)},
      0.9f);

  const MahoSidebarPalette palette =
      ResolveMahoSidebarPalette(Environment(theme, true, {backdrop}));

  ASSERT_EQ(palette.surface_stops.size(), 3u);
  EXPECT_EQ(palette.normal_contrast_surfaces.size(), 17u);
  EXPECT_EQ(palette.hover_contrast_surfaces.size(), 17u);
  EXPECT_EQ(palette.active_contrast_surfaces.size(), 17u);
  EXPECT_EQ(palette.selected_contrast_surfaces.size(), 17u);
  EXPECT_NE(palette.normal_contrast_surfaces.front(),
            palette.normal_contrast_surfaces.back());

  constexpr size_t kMidpointIndex = 8;
  const SkColor normal_midpoint = color_utils::GetResultingPaintColor(
      palette.surface_stops[1], backdrop);
  const SkColor hover_midpoint = color_utils::GetResultingPaintColor(
      palette.row_hover, normal_midpoint);
  const SkColor active_midpoint = color_utils::GetResultingPaintColor(
      palette.row_active, normal_midpoint);
  const SkColor selected_midpoint = color_utils::GetResultingPaintColor(
      palette.row_selected, normal_midpoint);

  EXPECT_EQ(palette.normal_contrast_surfaces[kMidpointIndex], normal_midpoint);
  EXPECT_EQ(palette.hover_contrast_surfaces[kMidpointIndex], hover_midpoint);
  EXPECT_EQ(palette.active_contrast_surfaces[kMidpointIndex], active_midpoint);
  EXPECT_EQ(palette.selected_contrast_surfaces[kMidpointIndex],
            selected_midpoint);
  EXPECT_NE(normal_midpoint, hover_midpoint);
  EXPECT_NE(normal_midpoint, active_midpoint);
  EXPECT_NE(normal_midpoint, selected_midpoint);
  EXPECT_NE(hover_midpoint, active_midpoint);
  EXPECT_NE(active_midpoint, selected_midpoint);
  ExpectPaletteRolesAtSurface(palette, normal_midpoint);
  ExpectPaletteRolesAtSurface(palette, hover_midpoint);
  ExpectPaletteRolesAtSurface(palette, active_midpoint);
  ExpectPaletteRolesAtSurface(palette, selected_midpoint);
}

TEST(MahoSidebarPaletteTest, ReadableRolesAndNeutralGlyphMeetThresholds) {
  const std::vector<ThemeData> themes = {
      MakeTheme(ThemeKind::kSolid, ColorScheme::kAuto,
                {SkColorSetRGB(0xF1, 0xD1, 0x78)}, 0.0f),
      MakeTheme(ThemeKind::kGradient, ColorScheme::kAuto,
                {SkColorSetRGB(0x13, 0x22, 0x35),
                 SkColorSetRGB(0x76, 0x2F, 0x5E)},
                0.9f),
      MakeTheme(ThemeKind::kZen, ColorScheme::kAuto,
                {SkColorSetRGB(0xE5, 0xC4, 0x8A),
                 SkColorSetRGB(0x65, 0x8A, 0xA8),
                 SkColorSetRGB(0x8D, 0x65, 0x9D)},
                0.5f),
      MakeTheme(ThemeKind::kSolid, ColorScheme::kAuto,
                {SkColorSetRGB(0x42, 0x73, 0xA5)}, 0.5f),
  };

  for (const ThemeData& theme : themes) {
    const MahoSidebarPalette palette =
        ResolveMahoSidebarPalette(Environment(theme, false));
    ExpectContrastAtLeast(palette, palette.primary_text, 4.5f);
    ExpectContrastAtLeast(palette, palette.secondary_text, 4.5f);
    EXPECT_NE(palette.tertiary_text, SK_ColorTRANSPARENT);
    EXPECT_EQ(palette.disabled_text, palette.tertiary_text);
    const float subtle_minimum = palette.dark_family ? 3.0f : 4.5f;
    ExpectContrastAtLeast(palette, palette.tertiary_text, subtle_minimum);
    ExpectContrastAtLeast(palette, palette.disabled_text, subtle_minimum);
    ExpectContrastAtLeast(palette, palette.neutral_glyph, subtle_minimum);
  }
}

TEST(MahoSidebarPaletteTest, AdjustsLowContrastSecondaryCandidate) {
  const ThemeData theme = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kLight,
      {SkColorSetRGB(0xE5, 0xE5, 0xE8)}, 0.9f);
  const SkColor unadjusted_secondary = SkColorSetRGB(128, 136, 166);

  const MahoSidebarPalette palette =
      ResolveMahoSidebarPalette(Environment(theme, false));

  EXPECT_NE(palette.secondary_text, unadjusted_secondary);
  ExpectContrastAtLeast(palette, palette.secondary_text, 4.5f);
}

TEST(MahoSidebarPaletteTest, ImpossibleMixedBackdropNormalizesSafely) {
  const ThemeData theme = MakeTheme(
      ThemeKind::kGradient, ColorScheme::kAuto,
      {SkColorSetRGB(0x78, 0x78, 0x78), SkColorSetRGB(0x88, 0x88, 0x88)},
      0.0f);

  const MahoSidebarPalette palette = ResolveMahoSidebarPalette(
      Environment(theme, false, {SK_ColorBLACK, SK_ColorWHITE}));

  EXPECT_TRUE(palette.used_surface_fallback);
  ASSERT_EQ(palette.surface_stops.size(), theme.stops.size());
  for (SkColor stop : palette.surface_stops) {
    EXPECT_EQ(SkColorGetA(stop), 24u);
  }
  EXPECT_NE(palette.surface_stops.front(), palette.surface_stops.back());
  ExpectContrastAtLeast(palette, palette.primary_text, 4.5f);
  ExpectContrastAtLeast(palette, palette.secondary_text, 4.5f);
  ExpectContrastAtLeast(palette, palette.neutral_glyph, 3.0f);
}

TEST(MahoSidebarCompositeContrastTest,
     RolesStayReadableOverAnyAcrylicBackdropExtreme) {
  // Windows composites the translucent sidebar over a DWM acrylic blur of an
  // arbitrary desktop. With white and black backdrops in the contrast set,
  // every role must hold its threshold at both extremes, which bounds every
  // backdrop in between.
  const ThemeData dark_theme = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kDark, {SkColorSetRGB(0x60, 0x50, 0x90)},
      0.3f);
  const ThemeData light_theme = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kLight, {SkColorSetRGB(0xC8, 0xB8, 0xD0)},
      0.3f);
  for (const ThemeData& theme : {dark_theme, light_theme}) {
    const MahoSidebarPalette palette = ResolveMahoSidebarPalette(Environment(
        theme, theme.color_scheme == ColorScheme::kDark,
        {SkColorSetRGB(0x20, 0x20, 0x20), SK_ColorWHITE, SK_ColorBLACK}));
    ExpectContrastAtLeast(palette, palette.primary_text, 4.5f);
    ExpectContrastAtLeast(palette, palette.secondary_text, 4.5f);
  }
}

TEST(MahoSidebarCompositeContrastTest,
     RolesStayReadableOnOpaquePaintedSurfaces) {
  // The sidebar paints `opaque_contrast_stops` on Linux and while an overlay
  // is open, so every role must meet its threshold on those stops (and every
  // row state over them), not only on the translucent composite.
  const std::vector<std::pair<ThemeData, SkColor>> fixtures = {
      {MakeTheme(ThemeKind::kSolid, ColorScheme::kAuto,
                 {SkColorSetRGB(0xF1, 0xD1, 0x78)}, 0.3f),
       SkColorSetRGB(0xEE, 0xEC, 0xE8)},
      {MakeTheme(ThemeKind::kGradient, ColorScheme::kAuto,
                 {SkColorSetRGB(0x42, 0x73, 0xA5),
                  SkColorSetRGB(0xE5, 0xC4, 0x8A)},
                 0.4f),
       SkColorSetRGB(0xF5, 0xF5, 0xF7)},
      {MakeTheme(ThemeKind::kGradient, ColorScheme::kDark,
                 {SkColorSetRGB(0x30, 0x20, 0x50),
                  SkColorSetRGB(0x10, 0x40, 0x50)},
                 0.2f),
       SkColorSetRGB(0x05, 0x05, 0x08)},
      {MakeTheme(ThemeKind::kZen, ColorScheme::kLight,
                 {SkColorSetRGB(0xE5, 0xC4, 0x8A),
                  SkColorSetRGB(0x65, 0x8A, 0xA8),
                  SkColorSetRGB(0x8D, 0x65, 0x9D)},
                 0.5f),
       SkColorSetRGB(0xFF, 0xFF, 0xFF)},
  };

  for (const auto& [theme, backdrop] : fixtures) {
    const MahoSidebarPalette palette = ResolveMahoSidebarPalette(
        Environment(theme, theme.color_scheme == ColorScheme::kDark,
                    {backdrop}));
    ASSERT_FALSE(palette.opaque_contrast_stops.empty());
    for (SkColor stop : palette.opaque_contrast_stops) {
      ASSERT_EQ(SkColorGetA(stop), SK_AlphaOPAQUE);
      ExpectPaletteRolesAtSurface(palette, stop);
      for (SkColor row_state :
           {palette.row_hover, palette.row_active, palette.row_selected}) {
        ExpectPaletteRolesAtSurface(
            palette, color_utils::GetResultingPaintColor(row_state, stop));
      }
    }
  }
}

class MahoSidebarPaletteHostTest : public BrowserWithTestWindowTest {
 protected:
  Browser* CreateAdditionalBrowser(Profile* profile) {
    BrowserWindowCreateParams params(profile, /*user_gesture=*/true);
    params.window = new TestBrowserWindow();
    std::unique_ptr<Browser> browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser_ptr = browser.get();
    additional_browsers_.push_back(std::move(browser));
    return browser_ptr;
  }

  maho::MahoSidebarView* CreateSidebar(Browser* browser) {
    auto widget = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.context = GetContext();
    widget->Init(std::move(params));
    widget->SetBounds(gfx::Rect(0, 0, 240, 800));
    auto* sidebar = widget->SetContentsView(
        std::make_unique<maho::MahoSidebarView>(browser));
    sidebar->SetBoundsRect(gfx::Rect(0, 0, 240, 800));
    sidebar->DeprecatedLayoutImmediately();
    widget->Show();
    widgets_.push_back(std::move(widget));
    return sidebar;
  }

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    MahoSpaceThemeState::Clear();
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    bridge->EnsureBrowserCollectionObservationForTesting();
    bridge->SetActiveSpaceId("");
    browser_a_ = browser();
    browser_b_ = CreateAdditionalBrowser(profile());
    // SetActiveSpaceId(browser, id) only binds spaces registered to the
    // browser's profile, so register the fixture spaces first.
    bridge->RegisterSpace("space-a", profile()->GetPath().BaseName());
    bridge->RegisterSpace("space-b", profile()->GetPath().BaseName());
    bridge->SetActiveSpaceId(browser_a_, "space-a");
    bridge->SetActiveSpaceId(browser_b_, "space-b");
    ASSERT_EQ(MahoSpaceThemeState::UpdateFromSnapshotForTesting(
                  R"([{"id":"space-a","theme":{"type":"gradient","gradientColors":[{"hue":25.0,"saturation":0.8,"brightness":0.8,"isPrimary":true,"isCustom":false},{"hue":55.0,"saturation":0.7,"brightness":0.9,"isPrimary":false,"isCustom":false}],"opacity":0.25,"texture":0.3}},{"id":"space-b","theme":{"type":"gradient","gradientColors":[{"hue":205.0,"saturation":0.8,"brightness":0.7,"isPrimary":true,"isCustom":false},{"hue":260.0,"saturation":0.6,"brightness":0.6,"isPrimary":false,"isCustom":false},{"hue":300.0,"saturation":0.5,"brightness":0.7,"isPrimary":false,"isCustom":false}],"opacity":0.9,"texture":0.1}}])"),
              (std::vector<std::string>{"space-a", "space-b"}));
  }

  void TearDown() override {
    widgets_.clear();
    additional_browsers_.clear();
    task_environment()->RunUntilIdle();
    browser_a_ = nullptr;
    browser_b_ = nullptr;
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    bridge->SetActiveSpaceId("");
    MahoSpaceThemeState::Clear();
    bridge->ResetBrowserCollectionObservationForTesting();
    BrowserWithTestWindowTest::TearDown();
  }

  std::vector<std::unique_ptr<Browser>> additional_browsers_;
  std::vector<std::unique_ptr<views::Widget>> widgets_;
  raw_ptr<Browser> browser_a_ = nullptr;
  raw_ptr<Browser> browser_b_ = nullptr;
};

TEST_F(MahoSidebarPaletteHostTest,
       ConstructionPublishesOneAtomicSnapshotBeforeConsumerCallback) {
  MahoSidebarPaletteHost host;
  bool callback_ran = false;
  host.SetApplyCallback(base::BindRepeating(
      [](MahoSidebarPaletteHost* host, bool* callback_ran,
         const MahoSidebarPalette& palette) {
        *callback_ran = true;
        EXPECT_EQ(host->compute_count_for_testing(), 1);
        EXPECT_EQ(host->apply_count_for_testing(), 1);
        EXPECT_EQ(host->palette_for_testing().surface_stops,
                  palette.surface_stops);
        EXPECT_FLOAT_EQ(host->palette_for_testing().grain, palette.grain);
      },
      &host, &callback_ran));

  EXPECT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kConstruction,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_,
                                                  /*os_dark=*/false),
      /*opaque=*/false));

  EXPECT_TRUE(callback_ran);
  EXPECT_EQ(host.compute_count_for_testing(), 1);
  EXPECT_EQ(host.apply_count_for_testing(), 1);
  EXPECT_FALSE(host.opaque_for_testing());
}

TEST_F(MahoSidebarPaletteHostTest,
       CosmeticDoesNotComputeWhileStructuralAndPreviewComputeExactlyOnce) {
  MahoSidebarPaletteHost host;
  ASSERT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kConstruction,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));
  const int baseline = host.compute_count_for_testing();

  EXPECT_FALSE(host.Update(
      MahoSidebarPaletteUpdateReason::kCosmetic,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));
  EXPECT_EQ(host.compute_count_for_testing(), baseline);
  EXPECT_EQ(host.apply_count_for_testing(), baseline);

  EXPECT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kStructuralBridge,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));
  EXPECT_EQ(host.compute_count_for_testing(), baseline + 1);
  EXPECT_EQ(host.apply_count_for_testing(), baseline + 1);

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  EXPECT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kPreview,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));
  EXPECT_EQ(host.compute_count_for_testing(), baseline + 2);
  EXPECT_EQ(host.apply_count_for_testing(), baseline + 2);
}

TEST_F(MahoSidebarPaletteHostTest,
       BrowserOwnersKeepOpacityGradientAndGrainSnapshotsIsolated) {
  MahoSidebarPaletteHost host_a;
  MahoSidebarPaletteHost host_b;
  ASSERT_TRUE(host_a.Update(
      MahoSidebarPaletteUpdateReason::kConstruction,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));
  ASSERT_TRUE(host_b.Update(
      MahoSidebarPaletteUpdateReason::kConstruction,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_b_, false), false));

  const MahoSidebarPalette palette_a = host_a.palette_for_testing();
  const MahoSidebarPalette palette_b = host_b.palette_for_testing();
  ASSERT_EQ(palette_a.surface_stops.size(), 2u);
  ASSERT_EQ(palette_b.surface_stops.size(), 3u);
  EXPECT_LT(SkColorGetA(palette_a.surface_stops.front()),
            SkColorGetA(palette_b.surface_stops.front()));
  EXPECT_FLOAT_EQ(palette_a.grain, 0.3f);
  EXPECT_FLOAT_EQ(palette_b.grain, 0.1f);
  EXPECT_NE(palette_a.surface_stops, palette_b.surface_stops);

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  ASSERT_TRUE(host_a.Update(
      MahoSidebarPaletteUpdateReason::kPreview,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));

  EXPECT_EQ(host_b.compute_count_for_testing(), 1);
  EXPECT_EQ(host_b.palette_for_testing().surface_stops,
            palette_b.surface_stops);
  EXPECT_FLOAT_EQ(host_b.palette_for_testing().grain, 0.1f);
  EXPECT_EQ(host_a.palette_for_testing().surface_stops.size(), 1u);
  EXPECT_FLOAT_EQ(host_a.palette_for_testing().grain, 0.4f);
}

TEST_F(MahoSidebarPaletteHostTest,
       OpaqueModeRecomputesOnceWithoutChangingOwningBrowserColors) {
  MahoSidebarPaletteHost host;
  ASSERT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kConstruction,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), false));
  const std::vector<SkColor> initial_stops =
      host.palette_for_testing().surface_stops;

  EXPECT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kOpaqueMode,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false), true));

  EXPECT_EQ(host.compute_count_for_testing(), 2);
  EXPECT_EQ(host.apply_count_for_testing(), 2);
  EXPECT_TRUE(host.opaque_for_testing());
  EXPECT_EQ(host.palette_for_testing().surface_stops, initial_stops);
}

// The snapshot is the single source of truth every sidebar-family surface
// paints from (docked rail, Downloads/Archive library overlay, Spaces overlay).
// Two surfaces of one Space theme used to disagree because each decided its own
// paint mode and gradient span; the snapshot now carries the paint mode, so
// equality has to cover it -- otherwise a notification that only flipped
// opacity would be mistaken for a no-op.
TEST_F(MahoSidebarPaletteHostTest, SnapshotEqualityCoversTheOpaquePaintMode) {
  MahoSidebarPalette a;
  MahoSidebarPalette b;
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);

  b.opaque = true;
  EXPECT_FALSE(a == b) << "a paint-mode change must not compare equal";
  EXPECT_TRUE(a != b);

  a.opaque = true;
  EXPECT_TRUE(a == b);

  const MahoSidebarPalette resolved = ResolveMahoSidebarPalette(
      Environment(MakeTheme(ThemeKind::kGradient, ColorScheme::kDark,
                            {SkColorSetRGB(0x20, 0x10, 0x40),
                             SkColorSetRGB(0x10, 0x30, 0x50)},
                            0.8f),
                  /*os_dark=*/true));
  EXPECT_TRUE(resolved == resolved);
  MahoSidebarPalette mutated = resolved;
  mutated.primary_text = SK_ColorMAGENTA;
  EXPECT_FALSE(mutated == resolved)
      << "a resolved role color change must not compare equal";
}

TEST_F(MahoSidebarPaletteHostTest,
       OpaquePaintModeTravelsWithThePublishedSnapshot) {
  MahoSidebarPaletteHost host;
  const MahoSidebarThemeEnvironment environment =
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false);

  ASSERT_TRUE(host.Update(MahoSidebarPaletteUpdateReason::kConstruction,
                          environment, /*opaque=*/false));
  EXPECT_FALSE(host.palette_for_testing().opaque);
  EXPECT_FALSE(host.opaque_for_testing());

  // The overlay surfaces read the paint mode off the palette instead of
  // hardcoding opaque, so the published snapshot must carry it.
  ASSERT_TRUE(host.Update(MahoSidebarPaletteUpdateReason::kOpaqueMode,
                          environment, /*opaque=*/true));
  EXPECT_TRUE(host.palette_for_testing().opaque);
  EXPECT_TRUE(host.opaque_for_testing());
}

TEST_F(MahoSidebarPaletteHostTest, NativeThemeNoOpDoesNotFanOutToConsumers) {
  MahoSidebarPaletteHost host;
  int callback_count = 0;
  host.SetApplyCallback(base::BindRepeating(
      [](int* count, const MahoSidebarPalette&) { ++*count; }, &callback_count));
  const MahoSidebarThemeEnvironment environment =
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, false);

  ASSERT_TRUE(host.Update(MahoSidebarPaletteUpdateReason::kNativeTheme,
                          environment, /*opaque=*/false));
  EXPECT_EQ(callback_count, 1);
  const int compute = host.compute_count_for_testing();
  const int apply = host.apply_count_for_testing();

  // Second identical native-theme notification: same inputs, same snapshot.
  EXPECT_FALSE(host.Update(MahoSidebarPaletteUpdateReason::kNativeTheme,
                           environment, /*opaque=*/false));
  EXPECT_EQ(callback_count, 1)
      << "an unchanged snapshot must not repaint every consumer surface";
  EXPECT_EQ(host.compute_count_for_testing(), compute);
  EXPECT_EQ(host.apply_count_for_testing(), apply);

  // Flipping only the paint mode is a real change and must still publish.
  EXPECT_TRUE(host.Update(MahoSidebarPaletteUpdateReason::kNativeTheme,
                          environment, /*opaque=*/true));
  EXPECT_EQ(callback_count, 2);
  EXPECT_TRUE(host.palette_for_testing().opaque);

  // A genuine theme change must still publish. This fixture's default theme
  // resolves the same dark family for both OS modes, so drive the change with an
  // explicit preview override (the real space-theme path) instead of os_dark.
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  EXPECT_TRUE(host.Update(
      MahoSidebarPaletteUpdateReason::kNativeTheme,
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_a_, /*os_dark=*/false),
      /*opaque=*/true));
  EXPECT_EQ(callback_count, 3);
  EXPECT_FLOAT_EQ(host.palette_for_testing().grain, 0.4f)
      << "the new theme must actually reach the published snapshot";
}

TEST_F(MahoSidebarPaletteHostTest,
        ForcedColorsStripsGradientTranslucencyAndGrain) {
  MahoSidebarThemeEnvironment environment =
      BuildMahoSidebarThemeEnvironmentForBrowser(browser_b_, true);
  environment.forced_colors = MahoSidebarForcedColors{
      .surface = SkColorSetRGB(0x10, 0x20, 0x30),
      .on_surface = SkColorSetRGB(0xF0, 0xE0, 0xD0),
      .outline = SkColorSetRGB(0xAA, 0xBB, 0xCC),
      .focus = SkColorSetRGB(0xFF, 0xCC, 0x00),
  };
  MahoSidebarPaletteHost host;

  ASSERT_TRUE(host.Update(MahoSidebarPaletteUpdateReason::kNativeTheme,
                          environment, /*opaque=*/false));
  const MahoSidebarPalette& palette = host.palette_for_testing();

  EXPECT_TRUE(palette.forced_colors);
  ASSERT_EQ(palette.surface_stops.size(), 1u);
  ASSERT_EQ(palette.opaque_contrast_stops.size(), 1u);
  EXPECT_EQ(palette.surface_stops.front(), environment.forced_colors->surface);
  EXPECT_EQ(SkColorGetA(palette.surface_stops.front()), SK_AlphaOPAQUE);
  EXPECT_FLOAT_EQ(palette.grain, 0.0f);
  EXPECT_EQ(palette.primary_text, environment.forced_colors->on_surface);
  EXPECT_EQ(palette.outline, environment.forced_colors->outline);
  EXPECT_EQ(palette.focus_ring, environment.forced_colors->focus);
}

TEST(MahoSidebarAccessibilityPaletteTest,
     ForcedColorsUseOpaqueSystemRolesWithoutCustomEffects) {
  const MahoSidebarForcedColors forced_colors{
      .surface = SkColorSetRGB(0x10, 0x20, 0x30),
      .on_surface = SkColorSetRGB(0xF0, 0xE0, 0xD0),
      .outline = SkColorSetRGB(0xAA, 0xBB, 0xCC),
      .focus = SkColorSetRGB(0xFF, 0xCC, 0x00),
  };
  const ThemeData themed = MakeTheme(
      ThemeKind::kGradient, ColorScheme::kDark,
      {SkColorSetRGB(0x20, 0x10, 0x40), SkColorSetRGB(0x40, 0x20, 0x60)},
      0.9f);
  MahoSidebarThemeEnvironment environment = Environment(themed, true);
  environment.forced_colors = forced_colors;

  const MahoSidebarPalette palette = ResolveMahoSidebarPalette(environment);

  EXPECT_TRUE(palette.forced_colors);
  EXPECT_EQ(palette.surface_stops,
            std::vector<SkColor>{forced_colors.surface});
  EXPECT_EQ(palette.opaque_contrast_stops, palette.surface_stops);
  EXPECT_EQ(palette.row_hover, SK_ColorTRANSPARENT);
  EXPECT_EQ(palette.row_active, SK_ColorTRANSPARENT);
  EXPECT_EQ(palette.row_selected, SK_ColorTRANSPARENT);
  EXPECT_FLOAT_EQ(palette.grain, 0.0f);
  EXPECT_EQ(palette.primary_text, forced_colors.on_surface);
  EXPECT_EQ(palette.secondary_text, forced_colors.on_surface);
  EXPECT_EQ(palette.outline, forced_colors.outline);
  EXPECT_EQ(palette.focus_ring, forced_colors.focus);
  EXPECT_GE(color_utils::GetContrastRatio(palette.focus_ring,
                                          forced_colors.surface),
            3.0f);
}

TEST(MahoSidebarPaletteTest, DeterministicNormalModeCompositeFixturesAtOpacityLimitsAndGradientRowStates) {
  const SkColor backdrop = SkColorSetRGB(0x14, 0x16, 0x20);

  const ThemeData min_opacity_theme = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kDark,
      {SkColorSetRGB(0x30, 0x40, 0x50)}, 0.0f);
  const MahoSidebarPalette min_opacity_palette =
      ResolveMahoSidebarPalette(Environment(min_opacity_theme, true, {backdrop}));
  ASSERT_EQ(min_opacity_palette.surface_stops.size(), 1u);
  EXPECT_EQ(SkColorGetA(min_opacity_palette.surface_stops[0]), 24u);
  EXPECT_EQ(SkColorGetA(min_opacity_palette.opaque_contrast_stops[0]), 255u);
  EXPECT_EQ(min_opacity_palette.normal_contrast_surfaces.size(), 17u);
  ExpectContrastAtLeast(min_opacity_palette, min_opacity_palette.primary_text, 4.5f);

  const ThemeData max_opacity_theme = MakeTheme(
      ThemeKind::kSolid, ColorScheme::kDark,
      {SkColorSetRGB(0x30, 0x40, 0x50)}, 1.0f);
  const MahoSidebarPalette max_opacity_palette =
      ResolveMahoSidebarPalette(Environment(max_opacity_theme, true, {backdrop}));
  ASSERT_EQ(max_opacity_palette.surface_stops.size(), 1u);
  EXPECT_EQ(SkColorGetA(max_opacity_palette.surface_stops[0]), 255u);
  EXPECT_EQ(max_opacity_palette.opaque_contrast_stops[0], max_opacity_palette.surface_stops[0]);
  EXPECT_EQ(max_opacity_palette.normal_contrast_surfaces.size(), 17u);
  ExpectContrastAtLeast(max_opacity_palette, max_opacity_palette.primary_text, 4.5f);

  const ThemeData gradient_theme = MakeTheme(
      ThemeKind::kGradient, ColorScheme::kDark,
      {SkColorSetRGB(0x20, 0x10, 0x40), SkColorSetRGB(0x40, 0x20, 0x60), SkColorSetRGB(0x10, 0x30, 0x50)},
      0.8f);
  const MahoSidebarPalette gradient_palette =
      ResolveMahoSidebarPalette(Environment(gradient_theme, true, {backdrop}));
  ASSERT_EQ(gradient_palette.surface_stops.size(), 3u);
  EXPECT_EQ(gradient_palette.normal_contrast_surfaces.size(), 17u);
  EXPECT_EQ(gradient_palette.hover_contrast_surfaces.size(), 17u);
  EXPECT_EQ(gradient_palette.active_contrast_surfaces.size(), 17u);
  EXPECT_EQ(gradient_palette.selected_contrast_surfaces.size(), 17u);

  for (size_t i = 0; i < 17; ++i) {
    const SkColor norm = gradient_palette.normal_contrast_surfaces[i];
    const SkColor hov = gradient_palette.hover_contrast_surfaces[i];
    const SkColor act = gradient_palette.active_contrast_surfaces[i];
    const SkColor sel = gradient_palette.selected_contrast_surfaces[i];
    EXPECT_NE(norm, hov);
    EXPECT_NE(norm, act);
    EXPECT_NE(norm, sel);
    EXPECT_NE(hov, act);
    EXPECT_NE(act, sel);
  }
}

TEST(MahoSidebarCompositeContrastTest,
     FixtureBoundedBackdropsPreserveReadableRolesAtOpacityLimits) {
  const std::vector<std::pair<ThemeData, SkColor>> fixtures = {
      {MakeTheme(ThemeKind::kSolid, ColorScheme::kLight,
                 {SkColorSetRGB(0xD0, 0xB8, 0x70)}, 0.0f),
       SkColorSetRGB(0xEE, 0xEC, 0xE8)},
      {MakeTheme(ThemeKind::kGradient, ColorScheme::kDark,
                 {SkColorSetRGB(0x20, 0x10, 0x40),
                  SkColorSetRGB(0x10, 0x30, 0x50)},
                 0.9f),
       SkColorSetRGB(0x14, 0x16, 0x20)},
      {MakeTheme(ThemeKind::kGradient, ColorScheme::kDark,
                 {SkColorSetRGB(0x30, 0x20, 0x50),
                  SkColorSetRGB(0x10, 0x40, 0x50)},
                 0.0f),
       SkColorSetRGB(0x2A, 0x1C, 0x3C)},
  };

  for (const auto& [theme, backdrop] : fixtures) {
    const MahoSidebarPalette palette = ResolveMahoSidebarPalette(
        Environment(theme, theme.color_scheme == ColorScheme::kDark,
                    {backdrop}));
    ASSERT_EQ(palette.normal_contrast_surfaces.size(), 17u);
    ExpectContrastAtLeast(palette, palette.primary_text, 4.5f);
    ExpectContrastAtLeast(palette, palette.secondary_text, 4.5f);
    ExpectContrastAtLeast(palette, palette.neutral_glyph, 3.0f);
  }
}

TEST_F(MahoSidebarPaletteHostTest,
       ViewRoutesCosmeticStructuralAndPreviewBeforeOneRepaint) {
  maho::MahoSidebarView* sidebar = CreateSidebar(browser_a_);
  ASSERT_TRUE(sidebar);
  const int initial_compute = sidebar->palette_compute_count_for_testing();
  const int initial_apply = sidebar->palette_apply_count_for_testing();
  const int initial_repaint = sidebar->palette_repaint_count_for_testing();
  ASSERT_GT(initial_compute, 0);
  ASSERT_EQ(initial_compute, initial_apply);
  ASSERT_EQ(initial_compute, initial_repaint);

  int callback_count = 0;
  int repaint_before_callback = initial_repaint;
  auto subscription = sidebar->AddSidebarPaletteChangedCallback(
      base::BindRepeating(
          [](maho::MahoSidebarView* sidebar, int* callback_count,
             int* repaint_before_callback,
             const MahoSidebarPalette& palette) {
            ++*callback_count;
            EXPECT_EQ(sidebar->sidebar_palette_for_testing().surface_stops,
                      palette.surface_stops);
            EXPECT_FLOAT_EQ(sidebar->grain_texture_for_testing(),
                            palette.grain);
            EXPECT_EQ(sidebar->palette_repaint_count_for_testing(),
                      *repaint_before_callback);
          },
          sidebar, &callback_count, &repaint_before_callback));

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->NotifyChanged(/*is_structural=*/false);
  EXPECT_EQ(sidebar->palette_compute_count_for_testing(), initial_compute);
  EXPECT_EQ(sidebar->palette_apply_count_for_testing(), initial_apply);
  EXPECT_EQ(sidebar->palette_repaint_count_for_testing(), initial_repaint);
  EXPECT_EQ(callback_count, 0);

  bridge->NotifyChanged(/*is_structural=*/true);
  EXPECT_EQ(sidebar->palette_compute_count_for_testing(), initial_compute + 1);
  EXPECT_EQ(sidebar->palette_apply_count_for_testing(), initial_apply + 1);
  EXPECT_EQ(sidebar->palette_repaint_count_for_testing(), initial_repaint + 1);
  EXPECT_EQ(callback_count, 1);

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":145.0,"saturation":0.8,"brightness":0.6,"grain":0.5}})"));
  repaint_before_callback = initial_repaint + 1;
  sidebar->OnThemeChanged();
  EXPECT_EQ(sidebar->palette_compute_count_for_testing(), initial_compute + 2);
  EXPECT_EQ(sidebar->palette_apply_count_for_testing(), initial_apply + 2);
  EXPECT_EQ(sidebar->palette_repaint_count_for_testing(), initial_repaint + 2);
  EXPECT_EQ(callback_count, 2);
  EXPECT_FLOAT_EQ(sidebar->sidebar_palette_for_testing().grain, 0.5f);
}

TEST_F(MahoSidebarPaletteHostTest, ViewSnapshotsNeverCrossBrowserOwners) {
  maho::MahoSidebarView* sidebar_a = CreateSidebar(browser_a_);
  maho::MahoSidebarView* sidebar_b = CreateSidebar(browser_b_);
  ASSERT_TRUE(sidebar_a);
  ASSERT_TRUE(sidebar_b);
  const MahoSidebarPalette palette_b =
      sidebar_b->sidebar_palette_for_testing();
  const int compute_b = sidebar_b->palette_compute_count_for_testing();

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":100.0,"saturation":0.9,"brightness":0.7,"grain":0.45}})"));
  sidebar_a->OnThemeChanged();

  EXPECT_EQ(sidebar_b->palette_compute_count_for_testing(), compute_b);
  EXPECT_EQ(sidebar_b->sidebar_palette_for_testing().surface_stops,
            palette_b.surface_stops);
  EXPECT_FLOAT_EQ(sidebar_b->sidebar_palette_for_testing().grain,
                  palette_b.grain);
  EXPECT_NE(sidebar_a->sidebar_palette_for_testing().surface_stops,
            sidebar_b->sidebar_palette_for_testing().surface_stops);
}

TEST_F(MahoSidebarPaletteHostTest,
       TabAndHeaderConsumersReceiveTheOwningBrowserPaletteSnapshot) {
  maho::MahoSidebarView* sidebar = CreateSidebar(browser_a_);
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(sidebar->tab_list_view_for_testing());
  ASSERT_TRUE(sidebar->space_header_view_for_testing());

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":145.0,"saturation":0.8,"brightness":0.6,"grain":0.5}})"));
  sidebar->OnThemeChanged();

  const MahoSidebarPalette& palette = sidebar->sidebar_palette_for_testing();
  ExpectSamePaletteSnapshot(
      palette,
      sidebar->tab_list_view_for_testing()->sidebar_palette_for_testing());
  ExpectSamePaletteSnapshot(
      palette, maho::GetSidebarSpaceHeaderRowPaletteForTesting(
                   sidebar->space_header_view_for_testing()));
}

TEST_F(MahoSidebarPaletteHostTest,
       PersistentShellConsumersReceiveAtomicOwningBrowserPaletteSnapshot) {
  maho::MahoSidebarView* sidebar = CreateSidebar(browser_a_);
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(sidebar->top_bar_view_for_testing());
  ASSERT_TRUE(sidebar->favorites_view_for_testing());
  ASSERT_TRUE(sidebar->footer_view_for_testing());

  const MahoSidebarPalette& initial = sidebar->sidebar_palette_for_testing();
  ExpectSamePaletteSnapshot(
      initial, sidebar->top_bar_view_for_testing()->sidebar_palette_for_testing());
  ExpectSamePaletteSnapshot(
      initial,
      sidebar->favorites_view_for_testing()->sidebar_palette_for_testing());
  ExpectSamePaletteSnapshot(
      initial, sidebar->footer_view_for_testing()->sidebar_palette_for_testing());

  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":300.0,"saturation":0.7,"brightness":0.5,"grain":0.2}})"));
  sidebar->OnThemeChanged();
  const MahoSidebarPalette& preview = sidebar->sidebar_palette_for_testing();
  ExpectSamePaletteSnapshot(
      preview, sidebar->top_bar_view_for_testing()->sidebar_palette_for_testing());
  ExpectSamePaletteSnapshot(
      preview,
      sidebar->favorites_view_for_testing()->sidebar_palette_for_testing());
  ExpectSamePaletteSnapshot(
      preview, sidebar->footer_view_for_testing()->sidebar_palette_for_testing());
}

TEST_F(MahoSidebarAuxiliaryPaletteTest,
       AuxiliaryFeedbackConsumersPreserveIndependentPaletteSnapshots) {
  const MahoSidebarPalette palette_a = ResolveMahoSidebarPalette(Environment(
      MakeTheme(ThemeKind::kSolid, ColorScheme::kLight,
                {SkColorSetRGB(0xD0, 0xB8, 0x70)}),
      false));
  const MahoSidebarPalette palette_b = ResolveMahoSidebarPalette(Environment(
      MakeTheme(ThemeKind::kSolid, ColorScheme::kDark,
                {SkColorSetRGB(0x20, 0x30, 0x60)}),
      true));
  maho::MahoSidebarUpdateNotificationView notification;
  maho::MahoSidebarProcessingPlaceholderView processing;

  notification.SetSidebarPalette(palette_a);
  processing.SetSidebarPalette(palette_a);
  notification.SetSidebarPalette(palette_b);

  ExpectSamePaletteSnapshot(
      palette_b, notification.sidebar_palette_for_testing());
  ExpectSamePaletteSnapshot(palette_a, processing.sidebar_palette_for_testing());
}

TEST_F(MahoSidebarLibraryPaletteTest,
       LightPaletteKeepsRailReadableWithDarkColorProvider) {
  constexpr SkColor kLightPinkSurface = SkColorSetRGB(229, 216, 227);
  MahoSidebarPalette palette;
  palette.dark_family = false;
  palette.surface_stops = {kLightPinkSurface};
  palette.opaque_contrast_stops = palette.surface_stops;
  palette.primary_text = SkColorSetRGB(45, 51, 74);
  palette.secondary_text = SkColorSetRGB(45, 51, 74);
  palette.neutral_glyph = palette.secondary_text;

  ASSERT_GE(color_utils::GetContrastRatio(palette.primary_text,
                                           kLightPinkSurface),
            color_utils::kMinimumReadableContrastRatio);
  ASSERT_GE(color_utils::GetContrastRatio(palette.secondary_text,
                                           kLightPinkSurface),
            color_utils::kMinimumReadableContrastRatio);

  // Pinned value of the dark color provider's rail item foreground id that
  // Phase 6a-c deleted, kept here as the regression control.
  constexpr SkColor kOldDarkRailItemText = SkColorSetARGB(219, 122, 114, 128);
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                 views::Widget::InitParams::TYPE_POPUP);
  widget->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kDark);
  auto* rail = widget->SetContentsView(
      std::make_unique<maho::MahoSidebarLibraryRailView>());
  ASSERT_TRUE(rail->GetColorProvider());
  EXPECT_EQ(kOldDarkRailItemText, SkColorSetARGB(219, 122, 114, 128));

  rail->OnSidebarPaletteChanged(palette);

  int label_count = 0;
  int glyph_count = 0;
  ExpectReadableRailForegrounds(rail, palette.surface_stops.back(),
                                rail->GetColorProvider(), &label_count,
                                &glyph_count);
  EXPECT_EQ(label_count, 4);
  EXPECT_EQ(glyph_count, 4);

  // Failing-first control for the migrated bug: the old dark-provider rail
  // foreground (ARGB 219,122,114,128) composites to RGB(137,128,142) on this
  // light Space surface, far below the readable threshold. Before the rail
  // consumed `palette_`, the tree walk above observed this dark-family color
  // and failed under exactly this light-palette + dark-provider combination.
  constexpr SkColor kOldDarkProviderRailForeground =
      SkColorSetARGB(219, 122, 114, 128);
  const SkColor old_composited = color_utils::GetResultingPaintColor(
      kOldDarkProviderRailForeground, kLightPinkSurface);
  EXPECT_EQ(old_composited, SkColorSetRGB(137, 128, 142));
  EXPECT_LT(color_utils::GetContrastRatio(old_composited, kLightPinkSurface),
            color_utils::kMinimumReadableContrastRatio);
}

TEST_F(MahoSidebarLibraryPaletteTest,
       OpaqueLibraryPaneUsesTheProvidedSidebarPaletteWithoutChangingContentText) {
  const MahoSidebarPalette palette = ResolveMahoSidebarPalette(Environment(
      MakeTheme(ThemeKind::kSolid, ColorScheme::kDark,
                {SkColorSetRGB(0x20, 0x30, 0x60)}),
      true));
  maho::MahoSidebarLibraryActionPaneView pane(
      u"Library", u"Available actions", {},
      base::BindRepeating([](size_t) {}));

  pane.SetSidebarPalette(palette);

  ExpectSamePaletteSnapshot(palette, pane.sidebar_palette_for_testing());
  ASSERT_TRUE(pane.title_label_for_testing());
  ASSERT_TRUE(pane.description_label_for_testing());
  EXPECT_EQ(pane.title_label_for_testing()->GetEnabledColor(),
            palette.primary_text);
  EXPECT_EQ(pane.description_label_for_testing()->GetEnabledColor(),
            palette.secondary_text);
}

TEST_F(MahoSidebarPaletteHostTest,
       ThemePickerCancelAndCloseClearOnlyTheirOwnerPreviewOverrides) {
  bool a_cancelled = false;
  bool b_cancelled = false;
  // The picker parents its dialog widget to the browser's native window; test
  // windows have none by default, which leaves Aura without a root window.
  for (Browser* b : {browser_a_.get(), browser_b_.get()}) {
    static_cast<TestBrowserWindow*>(b->GetWindow())
        ->SetNativeWindow(GetContext());
  }
  views::Widget* picker_a = MahoSpaceThemePickerDialog::Open(
      browser_a_, "{}",
      base::BindOnce(
          [](bool* cancelled, const std::string&, bool result_cancelled) {
            *cancelled = result_cancelled;
          },
          &a_cancelled));
  views::Widget* picker_b = MahoSpaceThemePickerDialog::Open(
      browser_b_, "{}",
      base::BindOnce(
          [](bool* cancelled, const std::string&, bool result_cancelled) {
            *cancelled = result_cancelled;
          },
          &b_cancelled));
  ASSERT_TRUE(picker_a);
  ASSERT_TRUE(picker_b);
  ASSERT_NE(picker_a, picker_b);
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_a_,
      R"({"type":"solid","color":{"hue":120.0,"saturation":0.9,"brightness":0.7,"grain":0.4}})"));
  ASSERT_TRUE(MahoSpaceThemeState::SetPreviewOverride(
      browser_b_,
      R"({"type":"solid","color":{"hue":280.0,"saturation":0.5,"brightness":0.6,"grain":0.1}})"));

  MahoSpaceThemePickerDialog::DispatchCancelToActive(browser_a_);
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(a_cancelled);
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(browser_a_));
  EXPECT_TRUE(MahoSpaceThemeState::HasPreviewOverride(browser_b_));

  picker_b->Close();
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(b_cancelled);
  EXPECT_FALSE(MahoSpaceThemeState::HasPreviewOverride(browser_b_));
}

}  // namespace
