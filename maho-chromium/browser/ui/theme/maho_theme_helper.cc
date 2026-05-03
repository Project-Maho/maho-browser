// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_theme_helper.h"

#include "third_party/skia/include/core/SkColor.h"

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
