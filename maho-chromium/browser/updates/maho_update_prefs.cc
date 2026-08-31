// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_prefs.h"

#include "components/prefs/pref_registry_simple.h"
#include "maho/browser/updates/maho_update_pref_names.h"

namespace maho {
namespace updates {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kMahoUpdateChannel, "stable");
  registry->RegisterStringPref(prefs::kMahoUpdateServerUrl, std::string());
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(prefs::kMahoUpdateInstallId, std::string());
  registry->RegisterStringPref(prefs::kMahoUpdateLastKnownGoodVersion, std::string());
  registry->RegisterStringPref(prefs::kMahoUpdateLastSeenVersion, std::string());
  registry->RegisterStringPref(prefs::kMahoChangelogLastShownVersion, std::string());
  registry->RegisterBooleanPref(prefs::kMahoUpdateAutoInstall, false);
  registry->RegisterDictionaryPref(prefs::kMahoUpdateCrashSentinel);
  registry->RegisterBooleanPref(prefs::kMahoUpdateRollbackRequested, false);
  // Enabled only after the release feed was live and canary verification passed.
  registry->RegisterBooleanPref(prefs::kMahoUpdateEnabled, true);
  registry->RegisterBooleanPref(prefs::kMahoConfigEnabled, true);
}

}  // namespace updates
}  // namespace maho
