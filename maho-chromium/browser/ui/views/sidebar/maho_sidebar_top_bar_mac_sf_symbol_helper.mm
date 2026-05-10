#include "maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_mac_sf_symbol_helper.h"

#include <algorithm>
#include <string>

#include "base/strings/sys_string_conversions.h"
#include "ui/color/color_id.h"
#include "ui/gfx/image/image_skia_util_mac.h"

#import <AppKit/AppKit.h>

namespace maho {

namespace {

constexpr float kTopBarDarkEnabledMinimumAlpha = 0.92f;
constexpr CGFloat kTopBarSymbolVerticalOffsetPx = -5.0;
constexpr NSImageSymbolScale kTopBarSymbolScale = NSImageSymbolScaleMedium;

NSColor* CreateTopBarSymbolTint(ui::ColorId color_id, float alpha) {
  const CGFloat resolved_alpha = std::clamp(alpha, 0.0f, 1.0f);
  return [NSColor colorWithName:nil
                dynamicProvider:^NSColor*(NSAppearance* appearance) {
                  const bool is_dark =
                      [appearance bestMatchFromAppearancesWithNames:@[
                        NSAppearanceNameDarkAqua, NSAppearanceNameAqua
                      ]] == NSAppearanceNameDarkAqua;
                  const bool is_subtle_color =
                      color_id == ui::kColorSysOnSurfaceSubtle;
                  const CGFloat effective_alpha =
                      is_dark && !is_subtle_color
                          ? std::max<CGFloat>(resolved_alpha,
                                              kTopBarDarkEnabledMinimumAlpha)
                          : resolved_alpha;

                  if (is_subtle_color) {
                    return is_dark
                               ? [NSColor colorWithSRGBRed:0.953
                                                     green:0.953
                                                      blue:0.969
                                                     alpha:resolved_alpha]
                               : [NSColor colorWithSRGBRed:0.176
                                                     green:0.200
                                                      blue:0.290
                                                     alpha:resolved_alpha];
                  }

                  return is_dark
                             ? [NSColor colorWithSRGBRed:0.953
                                                   green:0.953
                                                    blue:0.969
                                                   alpha:effective_alpha]
                             : [NSColor colorWithSRGBRed:0.176
                                                   green:0.200
                                                    blue:0.290
                                                   alpha:effective_alpha];
                }];
}

NSImage* OffsetTopBarSymbolImage(NSImage* image) {
  if (!image) {
    return nil;
  }

  return [NSImage
      imageWithSize:image.size
            flipped:NO
     drawingHandler:^BOOL(NSRect dst_rect) {
       [image drawInRect:NSOffsetRect(dst_rect, 0, kTopBarSymbolVerticalOffsetPx)
                fromRect:NSZeroRect
               operation:NSCompositingOperationSourceOver
                fraction:1.0];
       return YES;
     }];
}

}  // namespace

ui::ImageModel CreateSidebarTopBarMacSFSymbol(
    std::string_view symbol_name,
    std::string_view accessibility_description,
    int point_size,
    ui::ColorId color_id,
    float alpha) {
  NSString* symbol_name_ns =
      base::SysUTF8ToNSString(std::string(symbol_name));
  NSString* accessibility_description_ns =
      accessibility_description.empty()
          ? nil
          : base::SysUTF8ToNSString(std::string(accessibility_description));

  NSImage* image = [NSImage imageWithSystemSymbolName:symbol_name_ns
                           accessibilityDescription:accessibility_description_ns];
  if (!image) {
    return ui::ImageModel::FromImageSkia(gfx::ImageSkia());
  }

  NSImageSymbolConfiguration* configuration =
      [NSImageSymbolConfiguration
          configurationWithPointSize:point_size
                             weight:NSFontWeightMedium
                              scale:kTopBarSymbolScale];
  NSColor* tint = CreateTopBarSymbolTint(color_id, alpha);
  if (configuration && tint) {
    configuration = [configuration
        configurationByApplyingConfiguration:
            [NSImageSymbolConfiguration configurationWithPaletteColors:@[ tint ]]];
  }
  if (configuration) {
    NSImage* configured_image = [image imageWithSymbolConfiguration:configuration];
    if (configured_image) {
      image = configured_image;
    }
  }

  image = OffsetTopBarSymbolImage(image);

  return ui::ImageModel::FromImageSkia(gfx::ImageSkiaFromNSImage(image));
}

}  // namespace maho
