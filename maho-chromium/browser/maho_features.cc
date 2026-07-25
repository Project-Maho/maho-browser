// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_features.h"

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

BASE_FEATURE(kMahoSpaceSwitchActivatesTab,
             "MahoSpaceSwitchActivatesTab",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMahoSidebarVibrancy,
             "MahoSidebarVibrancy",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMahoSidebarActionPlaceholder,
             "MahoSidebarActionPlaceholder",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kMahoAgentBrowserActions,
             "MahoAgentBrowserActions",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kMahoShieldLauncherPanel,
             "MahoShieldLauncherPanel",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace maho::features
