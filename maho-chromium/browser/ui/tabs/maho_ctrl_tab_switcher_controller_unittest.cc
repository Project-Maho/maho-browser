// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

// Pref-only tests here.  The full controller behavioral tests (fast-flip,
// blank/kNoTab early-return, space-scope fallthrough, cursor wraparound
// including the "+N more" affordance slot, Ctrl-release commit, Escape
// cancel, snapshot invalidation on tab close) require a live BrowserView
// with a real Widget, TabStripModel and MahoCore; they belong under
// browser_tests / interactive_ui_tests rather than pref-only unit_tests.
// See BrowserView-based coverage in
// maho/browser/ui/views/tab_switcher/... (Phase 6 follow-up).

TEST(MahoCtrlTabPrefsTest, RegistersProfilePrefsWithDefaults) {
  TestingPrefServiceSimple prefs;
  ctrl_tab_prefs::RegisterProfilePrefs(prefs.registry());

  EXPECT_TRUE(prefs.GetBoolean(ctrl_tab_prefs::kMruOrder));
  EXPECT_EQ(std::string(ctrl_tab_prefs::kScopeCurrentSpace),
            prefs.GetString(ctrl_tab_prefs::kScope));
  EXPECT_EQ(7, prefs.GetInteger(ctrl_tab_prefs::kMaxVisible));
}

TEST(MahoCtrlTabPrefsTest, PrefsAreMutable) {
  TestingPrefServiceSimple prefs;
  ctrl_tab_prefs::RegisterProfilePrefs(prefs.registry());

  prefs.SetBoolean(ctrl_tab_prefs::kMruOrder, false);
  prefs.SetString(ctrl_tab_prefs::kScope, ctrl_tab_prefs::kScopeGlobal);
  prefs.SetInteger(ctrl_tab_prefs::kMaxVisible, 5);

  EXPECT_FALSE(prefs.GetBoolean(ctrl_tab_prefs::kMruOrder));
  EXPECT_EQ(std::string(ctrl_tab_prefs::kScopeGlobal),
            prefs.GetString(ctrl_tab_prefs::kScope));
  EXPECT_EQ(5, prefs.GetInteger(ctrl_tab_prefs::kMaxVisible));
}

}  // namespace maho
