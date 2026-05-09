#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_MAC_SF_SYMBOL_HELPER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_MAC_SF_SYMBOL_HELPER_H_

#include <string_view>

#include "build/build_config.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/image/image_skia.h"

namespace maho {

#if BUILDFLAG(IS_MAC)
ui::ImageModel CreateSidebarTopBarMacSFSymbol(
    std::string_view symbol_name,
    std::string_view accessibility_description,
    int point_size,
    ui::ColorId color_id = ui::kColorSysOnSurface,
    float alpha = 1.0f);
#else
inline ui::ImageModel CreateSidebarTopBarMacSFSymbol(
    std::string_view symbol_name,
    std::string_view accessibility_description,
    int point_size,
    ui::ColorId color_id = ui::kColorSysOnSurface,
    float alpha = 1.0f) {
  (void)symbol_name;
  (void)accessibility_description;
  (void)point_size;
  (void)color_id;
  (void)alpha;
  return ui::ImageModel::FromImageSkia(gfx::ImageSkia());
}
#endif

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_MAC_SF_SYMBOL_HELPER_H_
