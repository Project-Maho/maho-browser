// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_mini/maho_mini_state_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoMiniStatePolicyTest, ToggleChangesState) {
  EXPECT_EQ(TransitionMahoMiniState(MahoMiniState::kCompact,
                                    MahoMiniStateEvent::kToggle),
            MahoMiniState::kExpanded);
  EXPECT_EQ(TransitionMahoMiniState(MahoMiniState::kExpanded,
                                    MahoMiniStateEvent::kToggle),
            MahoMiniState::kCompact);
}

TEST(MahoMiniStatePolicyTest, NavigationExpandsCompactState) {
  EXPECT_EQ(TransitionMahoMiniState(MahoMiniState::kCompact,
                                    MahoMiniStateEvent::kNavigation),
            MahoMiniState::kExpanded);
  EXPECT_EQ(TransitionMahoMiniState(MahoMiniState::kExpanded,
                                    MahoMiniStateEvent::kNavigation),
            MahoMiniState::kExpanded);
}

TEST(MahoMiniStatePolicyTest, PromotionExpandsCompactState) {
  EXPECT_EQ(TransitionMahoMiniState(MahoMiniState::kCompact,
                                    MahoMiniStateEvent::kPromote),
            MahoMiniState::kExpanded);
  EXPECT_EQ(TransitionMahoMiniState(MahoMiniState::kExpanded,
                                    MahoMiniStateEvent::kPromote),
            MahoMiniState::kExpanded);
}

}  // namespace
}  // namespace maho
