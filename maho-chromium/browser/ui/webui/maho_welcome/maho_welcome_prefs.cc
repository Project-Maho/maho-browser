// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace maho::welcome {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kWelcomeCompleted, false);
}

}  // namespace maho::welcome
