// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace maho::ctrl_tab_prefs {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kMruOrder, true);
  registry->RegisterStringPref(kScope, kScopeCurrentSpace);
  registry->RegisterIntegerPref(kMaxVisible, 7);
}

}  // namespace maho::ctrl_tab_prefs
