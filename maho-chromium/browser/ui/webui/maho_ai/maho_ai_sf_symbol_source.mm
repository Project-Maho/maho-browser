// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_sf_symbol_source.h"

#include <stdint.h>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted_memory.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"

#import <AppKit/AppKit.h>

namespace maho_ai {

namespace {

struct SFSymbolEntry {
  const char* sf_name;
};

const base::flat_map<std::string_view, SFSymbolEntry>& GetAllowlist() {
  static const base::NoDestructor<base::flat_map<std::string_view, SFSymbolEntry>>
      kAllowlist({
          {"arrowUpRight",   {"arrow.up.right"}},
          {"composerSpark",  {"sparkles"}},
          {"copy",           {"doc.on.doc"}},
          {"edit",           {"pencil"}},
          {"ellipsis",       {"ellipsis"}},
          {"globe",          {"globe"}},
          {"panelSpark",     {"sparkles.rectangle.stack"}},
          {"plus",           {"plus"}},
          {"retry",          {"arrow.clockwise"}},
          {"stop",           {"stop.fill"}},
          {"thumbDown",      {"hand.thumbsdown"}},
          {"thumbUp",        {"hand.thumbsup"}},
          {"trash",          {"trash"}},
      });
  return *kAllowlist;
}

constexpr int kIconPointSize = 20;

scoped_refptr<base::RefCountedMemory> MakeEmptyPNG() {
  static const uint8_t kTransparent1x1[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x62, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x01, 0xe2, 0x21, 0xbc, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
      0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  return base::MakeRefCounted<base::RefCountedStaticMemory>(kTransparent1x1);
}

std::string_view ExtractIconName(const std::string& path) {
  if (!base::StartsWith(path, kSFSymbolPathPrefix)) {
    return {};
  }
  return std::string_view(path).substr(sizeof(kSFSymbolPathPrefix) - 1);
}

scoped_refptr<base::RefCountedMemory> RenderSFSymbolToPNG(
    std::string_view sf_name) {
  NSString* name_ns = base::SysUTF8ToNSString(std::string(sf_name));
  NSImage* ns_image = [NSImage imageWithSystemSymbolName:name_ns
                               accessibilityDescription:nil];
  if (!ns_image) {
    return MakeEmptyPNG();
  }

  NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration
      configurationWithPointSize:kIconPointSize
                          weight:NSFontWeightMedium
                           scale:NSImageSymbolScaleMedium];
  NSImage* configured = [ns_image imageWithSymbolConfiguration:config];
  if (configured) {
    ns_image = configured;
  }

  NSSize size = ns_image.size;
  if (size.width <= 0 || size.height <= 0) {
    return MakeEmptyPNG();
  }

  NSBitmapImageRep* bitmap_rep = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nil
                    pixelsWide:(NSInteger)size.width
                    pixelsHigh:(NSInteger)size.height
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSCalibratedRGBColorSpace
                   bytesPerRow:0
                  bitsPerPixel:0];
  if (!bitmap_rep) {
    return MakeEmptyPNG();
  }

  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext* ctx =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap_rep];
  [NSGraphicsContext setCurrentContext:ctx];
  [ns_image drawInRect:NSMakeRect(0, 0, size.width, size.height)
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];

  NSData* png_data =
      [bitmap_rep representationUsingType:NSBitmapImageFileTypePNG
                               properties:@{}];
  if (!png_data || png_data.length == 0) {
    return MakeEmptyPNG();
  }

  std::vector<uint8_t> bytes(png_data.length);
  [png_data getBytes:bytes.data() length:png_data.length];
  return base::MakeRefCounted<base::RefCountedBytes>(std::move(bytes));
}

}  // namespace

bool ShouldHandleSFSymbolRequest(const std::string& path) {
  std::string_view icon_name = ExtractIconName(path);
  if (icon_name.empty()) {
    return false;
  }
  return GetAllowlist().contains(icon_name);
}

void HandleSFSymbolRequest(
    const std::string& path,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback) {
  std::string_view icon_name = ExtractIconName(path);
  auto it = GetAllowlist().find(icon_name);
  if (it == GetAllowlist().end()) {
    std::move(callback).Run(MakeEmptyPNG());
    return;
  }
  std::move(callback).Run(RenderSFSymbolToPNG(it->second.sf_name));
}

}  // namespace maho_ai
