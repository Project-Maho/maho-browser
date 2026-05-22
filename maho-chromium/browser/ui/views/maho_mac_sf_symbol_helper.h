#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_MAC_SF_SYMBOL_HELPER_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_MAC_SF_SYMBOL_HELPER_H_

#include <string_view>

#include "build/buildflag.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/image/image_skia.h"

namespace maho {

enum class MahoMacSFSymbolScale {
  kSmall,
  kMedium,
  kLarge,
};

#if BUILDFLAG(IS_MAC)
ui::ImageModel CreateMahoMacSFSymbol(
    std::string_view symbol_name,
    std::string_view accessibility_description,
    int point_size,
    ui::ColorId color_id = ui::kColorSysOnSurface,
    float alpha = 1.0f,
    float vertical_offset = 0.0f,
    MahoMacSFSymbolScale scale = MahoMacSFSymbolScale::kMedium);
#else
inline ui::ImageModel CreateMahoMacSFSymbol(
    std::string_view symbol_name,
    std::string_view accessibility_description,
    int point_size,
    ui::ColorId color_id = ui::kColorSysOnSurface,
    float alpha = 1.0f,
    float vertical_offset = 0.0f,
    MahoMacSFSymbolScale scale = MahoMacSFSymbolScale::kMedium) {
  (void)symbol_name;
  (void)accessibility_description;
  (void)point_size;
  (void)color_id;
  (void)alpha;
  (void)vertical_offset;
  (void)scale;
  return ui::ImageModel::FromImageSkia(gfx::ImageSkia());
}
#endif

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_MAC_SF_SYMBOL_HELPER_H_
