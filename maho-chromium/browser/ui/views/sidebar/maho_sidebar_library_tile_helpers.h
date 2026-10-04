// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_TILE_HELPERS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_TILE_HELPERS_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/skia/include/core/SkColor.h"

namespace maho {

enum class MahoSidebarTileAccent {
  kBlue = 0,
  kRed,
  kGreen,
  kPurple,
  kOrange,
  kPink,
  kCyan,
  kAmber,
  kGray,
  kCharcoal,
};

enum class MahoSidebarTilePaletteStyle {
  kSolid,
  kTinted,
};

struct MahoSidebarTilePalette {
  SkColor background = SK_ColorTRANSPARENT;
  SkColor foreground = SK_ColorWHITE;
};

inline constexpr std::array<SkColor, 10> kMahoSidebarTileAccentPalette = {
    SkColorSetRGB(0x4A, 0x7A, 0xEE), SkColorSetRGB(0xE8, 0x54, 0x54),
    SkColorSetRGB(0x4A, 0xB8, 0x77), SkColorSetRGB(0x8B, 0x5C, 0xF6),
    SkColorSetRGB(0xE8, 0x88, 0x3A), SkColorSetRGB(0xD9, 0x4C, 0x8A),
    SkColorSetRGB(0x3A, 0xAC, 0xCC), SkColorSetRGB(0xC4, 0x90, 0x20),
    SkColorSetRGB(0x6B, 0x65, 0x73), SkColorSetRGB(0x3A, 0x35, 0x40),
};

inline uint32_t MahoSidebarTileSeedHash(std::string_view seed) {
  uint32_t hash = 0;
  for (unsigned char character : seed) {
    hash = (hash * 131u) + character;
  }
  return hash;
}

inline MahoSidebarTileAccent MahoSidebarTileAccentForSeed(
    std::string_view seed) {
  if (seed.empty()) {
    return MahoSidebarTileAccent::kGray;
  }
  return static_cast<MahoSidebarTileAccent>(
      MahoSidebarTileSeedHash(seed) % kMahoSidebarTileAccentPalette.size());
}

inline SkColor MahoSidebarTileAccentColor(MahoSidebarTileAccent accent) {
  return kMahoSidebarTileAccentPalette[static_cast<size_t>(accent)];
}

inline uint8_t MahoSidebarBlendColorChannel(uint8_t base,
                                            uint8_t overlay,
                                            float fraction) {
  return static_cast<uint8_t>(base + (overlay - base) * fraction);
}

inline SkColor MahoSidebarBrightenAccent(SkColor accent, float fraction) {
  return SkColorSetARGB(
      SkColorGetA(accent),
      MahoSidebarBlendColorChannel(SkColorGetR(accent), 0xFF, fraction),
      MahoSidebarBlendColorChannel(SkColorGetG(accent), 0xFF, fraction),
      MahoSidebarBlendColorChannel(SkColorGetB(accent), 0xFF, fraction));
}

inline MahoSidebarTilePalette MahoSidebarTilePaletteForAccent(
    MahoSidebarTileAccent accent,
    MahoSidebarTilePaletteStyle style) {
  const SkColor accent_color = MahoSidebarTileAccentColor(accent);
  if (style == MahoSidebarTilePaletteStyle::kTinted) {
    return {SkColorSetA(accent_color, 0x2E),
            MahoSidebarBrightenAccent(accent_color, 0.35f)};
  }

  return {accent_color, SK_ColorWHITE};
}

inline MahoSidebarTilePalette MahoSidebarTilePaletteForSeed(
    std::string_view seed,
    MahoSidebarTilePaletteStyle style = MahoSidebarTilePaletteStyle::kSolid) {
  return MahoSidebarTilePaletteForAccent(MahoSidebarTileAccentForSeed(seed),
                                         style);
}

inline std::u16string MahoSidebarTileLetterForText(
    std::u16string_view source,
    std::u16string_view fallback = std::u16string_view()) {
  for (char16_t character : source) {
    if (base::IsAsciiAlpha(character)) {
      return std::u16string(1, base::ToUpperASCII(character));
    }
    if (base::IsAsciiDigit(character)) {
      return std::u16string(1, character);
    }
  }
  return std::u16string(fallback);
}

inline std::u16string MahoSidebarTileLetterForSeed(
    std::string_view seed,
    std::u16string_view fallback = std::u16string_view()) {
  return MahoSidebarTileLetterForText(base::UTF8ToUTF16(std::string(seed)),
                                      fallback);
}

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_LIBRARY_TILE_HELPERS_H_
