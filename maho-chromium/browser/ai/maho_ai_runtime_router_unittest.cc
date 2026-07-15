// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

class MahoAiRuntimeRouterTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
  TestingPrefServiceSimple prefs_;
};

TEST_F(MahoAiRuntimeRouterTest, UsesUnifiedAgentByDefault) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  EXPECT_EQ(router.GetActiveAdapterName(), "unified-agent");
  EXPECT_TRUE(router.IsAvailable());
}

TEST_F(MahoAiRuntimeRouterTest, GetActiveAdapterReturnsNonNull) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  EXPECT_NE(router.GetActiveAdapter(), nullptr);
}

TEST_F(MahoAiRuntimeRouterTest, RebuildIsIdempotent) {
  MahoAiRuntimeRouter router(&prefs_, nullptr);
  MahoAiRuntimeAdapter* first = router.GetActiveAdapter();
  MahoAiRuntimeAdapter* second = router.GetActiveAdapter();
  EXPECT_EQ(first, second);
}

// R-9: an allowing kAI gate (regular class) builds the adapter.
TEST_F(MahoAiRuntimeRouterTest, RegularTokenBuildsAdapter) {
  MahoAiRuntimeRouter router(&prefs_, nullptr,
                             base::BindRepeating([]() { return true; }));
  EXPECT_NE(router.GetActiveAdapter(), nullptr);
}

// R-9: a denying kAI gate (every OTR/non-regular class) builds nothing.
TEST_F(MahoAiRuntimeRouterTest, OtrTokenBuildsNothing) {
  MahoAiRuntimeRouter router(&prefs_, nullptr,
                             base::BindRepeating([]() { return false; }));
  EXPECT_EQ(router.GetActiveAdapter(), nullptr);
}

// R-9: a gate that flips to deny (e.g. an async profile/window switch that
// invalidates the token) rejects a subsequent resolution and drops the adapter.
TEST_F(MahoAiRuntimeRouterTest, AsyncRevalidationRejectsProfileSwitch) {
  auto allowed = std::make_shared<bool>(true);
  MahoAiRuntimeRouter router(
      &prefs_, nullptr,
      base::BindRepeating([](std::shared_ptr<bool> a) { return *a; }, allowed));
  EXPECT_NE(router.GetActiveAdapter(), nullptr);

  *allowed = false;
  EXPECT_EQ(router.GetActiveAdapter(), nullptr);
}
