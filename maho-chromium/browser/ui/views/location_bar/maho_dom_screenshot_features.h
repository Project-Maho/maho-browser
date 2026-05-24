// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_FEATURES_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_FEATURES_H_

#include "base/feature_list.h"

namespace maho::features {

// Gates the "Edit" row in the post-capture screenshot menu.
// Deferred: requires either a Maho image-editor surface or adoption of
// image_editor::ScreenshotFlow's editor entry point. Default disabled until
// the prerequisite editor surface lands.
BASE_DECLARE_FEATURE(kMahoScreenshotEditAction);

// Gates the "Send via iMessage..." row in the post-capture screenshot menu.
// Deferred: requires a macOS NSSharingService shim that does not exist yet.
// On non-macOS builds the row is hidden at the handler level regardless of
// this flag. Default disabled.
BASE_DECLARE_FEATURE(kMahoScreenshotImessageShareAction);

// Gates the "Save to Library" row in the post-capture screenshot menu.
// Deferred: requires a Maho image-library destination (product question open).
// Default disabled until the library destination is defined and plumbed.
BASE_DECLARE_FEATURE(kMahoScreenshotSaveToLibraryAction);

}  // namespace maho::features

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_FEATURES_H_
