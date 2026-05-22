// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <string>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

class MahoAiRuntimeRouterTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs_.registry()->RegisterStringPref(
        maho::ai_prefs::kRuntimeCommand, std::string());
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(MahoAiRuntimeRouterTest, DefaultsToAcpAdapter) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  EXPECT_EQ(router.GetActiveAdapterName(), "opencode-acp");
}

TEST_F(MahoAiRuntimeRouterTest, ConstructsAcpAdapterUnconditionally) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  EXPECT_EQ(router.GetActiveAdapterName(), "opencode-acp");
}
