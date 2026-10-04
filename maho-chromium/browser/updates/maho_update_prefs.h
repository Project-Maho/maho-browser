// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_PREFS_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_PREFS_H_

class PrefRegistrySimple;

namespace maho {
namespace updates {

void RegisterProfilePrefs(PrefRegistrySimple* registry);
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_PREFS_H_
