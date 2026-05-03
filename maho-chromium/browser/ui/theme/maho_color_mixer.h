// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_THEME_MAHO_COLOR_MIXER_H_
#define MAHO_BROWSER_UI_THEME_MAHO_COLOR_MIXER_H_

namespace ui {
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

// Adds Maho-specific color recipes to the given ColorProvider.
// Registered via AppendColorProviderInitializer during browser startup.
void AddMahoColorMixers(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key);

#endif  // MAHO_BROWSER_UI_THEME_MAHO_COLOR_MIXER_H_
