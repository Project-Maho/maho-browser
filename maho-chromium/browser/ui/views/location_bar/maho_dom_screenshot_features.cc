// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_features.h"

#include "base/feature_list.h"

namespace maho::features {

BASE_FEATURE(kMahoScreenshotEditAction,
             "MahoScreenshotEditAction",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kMahoScreenshotImessageShareAction,
             "MahoScreenshotImessageShareAction",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kMahoScreenshotSaveToLibraryAction,
             "MahoScreenshotSaveToLibraryAction",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace maho::features
