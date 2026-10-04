// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_atc_navigation_throttle.h"

#include <memory>

#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/mock_navigation_throttle_registry.h"
#include "maho/browser/net/maho_atc_state.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {
namespace {

using ::content::NavigationThrottle;

// Verifies the WillStartRequest() gating: only top-level, http(s),
// user-initiated navigations are eligible for routing; everything else must
// PROCEED untouched. The positive DEFER/route path requires a live Browser and
// space profiles and is exercised by browser tests; the routing decision itself
// is covered by the Rust atc_manager unit tests.
class MahoAtcNavigationThrottleTest : public ChromeRenderViewHostTestHarness {
 protected:
  void TearDown() override {
    MahoAtcState::SetHasEnabledRules(false);
    ChromeRenderViewHostTestHarness::TearDown();
  }

  NavigationThrottle::ThrottleAction RunWillStartRequest(
      content::MockNavigationHandle* handle) {
    content::MockNavigationThrottleRegistry registry(
        handle,
        content::MockNavigationThrottleRegistry::RegistrationMode::kHold);
    MahoAtcNavigationThrottle throttle(registry);
    return throttle.WillStartRequest().action();
  }

  std::unique_ptr<content::MockNavigationHandle> MakeHandle(const GURL& url) {
    auto handle =
        std::make_unique<content::MockNavigationHandle>(url, main_rfh());
    handle->set_is_in_primary_main_frame(true);
    handle->set_is_same_document(false);
    handle->set_is_renderer_initiated(false);
    ON_CALL(*handle, IsPost()).WillByDefault(::testing::Return(false));
    ON_CALL(*handle, HasUserGesture()).WillByDefault(::testing::Return(true));
    return handle;
  }
};

TEST_F(MahoAtcNavigationThrottleTest, NoEnabledRulesProceeds) {
  MahoAtcState::SetHasEnabledRules(false);
  auto handle = MakeHandle(GURL("https://github.com/signup"));
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

TEST_F(MahoAtcNavigationThrottleTest,
       HasEnabledTrafficRuleIgnoresCleanupRules) {
  EXPECT_FALSE(HasEnabledTrafficRuleInJson(
      R"([{"id":"cleanup","name":"old","max_age_hours":24,"enabled":true}])"));
  EXPECT_FALSE(HasEnabledTrafficRuleInJson(
      R"([{"id":"limit","name":"many","max_tabs":5,"enabled":true}])"));
  EXPECT_TRUE(HasEnabledTrafficRuleInJson(
      R"([{"id":"traffic","name":"github.com","space_id":"space","enabled":true}])"));
  EXPECT_FALSE(HasEnabledTrafficRuleInJson(
      R"([{"id":"disabled","name":"github.com","space_id":"space","enabled":false}])"));
}

TEST_F(MahoAtcNavigationThrottleTest, SubframeProceeds) {
  MahoAtcState::SetHasEnabledRules(true);
  auto handle = MakeHandle(GURL("https://github.com/signup"));
  handle->set_is_in_primary_main_frame(false);
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

TEST_F(MahoAtcNavigationThrottleTest, SameDocumentProceeds) {
  MahoAtcState::SetHasEnabledRules(true);
  auto handle = MakeHandle(GURL("https://github.com/signup"));
  handle->set_is_same_document(true);
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

TEST_F(MahoAtcNavigationThrottleTest, NonHttpSchemeProceeds) {
  MahoAtcState::SetHasEnabledRules(true);
  auto handle = MakeHandle(GURL("chrome://settings"));
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

TEST_F(MahoAtcNavigationThrottleTest, RendererInitiatedWithoutGestureProceeds) {
  MahoAtcState::SetHasEnabledRules(true);
  auto handle = MakeHandle(GURL("https://github.com/signup"));
  handle->set_is_renderer_initiated(true);
  ON_CALL(*handle, HasUserGesture()).WillByDefault(::testing::Return(false));
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

TEST_F(MahoAtcNavigationThrottleTest, PostMainFrameNavigationProceeds) {
  MahoAtcState::SetHasEnabledRules(true);
  auto handle = MakeHandle(GURL("https://github.com/login"));
  ON_CALL(*handle, IsPost()).WillByDefault(::testing::Return(true));
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

TEST_F(MahoAtcNavigationThrottleTest,
       EligibleNavigationWithoutBrowserProceeds) {
  MahoAtcState::SetHasEnabledRules(true);
  auto handle = MakeHandle(GURL("https://github.com/signup"));
  // Browser-initiated (user-initiated) top-level http navigation is eligible,
  // but the test WebContents has no owning Browser, so routing cannot resolve a
  // Browser and the throttle proceeds instead of deferring.
  EXPECT_EQ(NavigationThrottle::PROCEED, RunWillStartRequest(handle.get()));
}

}  // namespace
}  // namespace maho
