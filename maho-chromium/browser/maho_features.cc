// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_features.h"

#include "base/feature_list.h"

namespace maho::features {

BASE_FEATURE(kMahoUnifiedClickPath,
             "MahoUnifiedClickPath",
             base::FEATURE_ENABLED_BY_DEFAULT);



BASE_FEATURE(kMahoArcToolbarButtonProvider,
             "MahoArcToolbarButtonProvider",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMahoCommandPaletteVibrancy,
             "MahoCommandPaletteVibrancy",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace maho::features
