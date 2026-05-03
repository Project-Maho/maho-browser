// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_THEME_HELPER_H_
#define MAHO_BROWSER_UI_THEME_MAHO_THEME_HELPER_H_

#include <cstdint>

#include "third_party/skia/include/core/SkColor.h"

// Utility functions for Maho theme color manipulation.
// Color registration is handled by AddMahoColorMixers (maho_color_mixer.h).
// Used for space theme tinting.

namespace maho_theme {

// Linearly blends two SkColors by the given fraction (0.0 = base, 1.0 = overlay).
// Intended for Phase 3 space theme tinting.
SkColor BlendColors(SkColor base, SkColor overlay, float fraction);

}  // namespace maho_theme

#endif  // MAHO_BROWSER_UI_THEME_MAHO_THEME_HELPER_H_
