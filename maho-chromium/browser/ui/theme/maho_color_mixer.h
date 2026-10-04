// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_COLOR_MIXER_H_
#define MAHO_BROWSER_UI_THEME_MAHO_COLOR_MIXER_H_

#include "ui/color/color_provider_key.h"

namespace ui {
class ColorProvider;
}  // namespace ui

// Adds Maho-specific color recipes to the given ColorProvider.
// Registered via AppendColorProviderInitializer during browser startup.
void AddMahoColorMixers(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key);

// Returns a sentinel InitializerSupplier used to signal OTR/Incognito windows.
ui::ColorProviderKey::InitializerSupplier* GetMahoOtrSentinel();

#endif  // MAHO_BROWSER_UI_THEME_MAHO_COLOR_MIXER_H_
