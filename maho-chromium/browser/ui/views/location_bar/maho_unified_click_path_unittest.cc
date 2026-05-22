// Copyright 2026 Maho Browser. All rights reserved.
//
// The Phase 4 intercept lives in the chromium_src overlay and cannot be
// exercised without a full in-process browser test. These tests cover the
// feature-flag contract (default state, enable/disable overrides, nesting)
// so that interactive tests can override the flag per test case without
// re-testing this machinery themselves.

#include "maho/browser/maho_features.h"

#include "base/feature_list.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoUnifiedClickPathFeatureTest, DisabledByDefault) {
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
}

TEST(MahoUnifiedClickPathFeatureTest, EnabledWhenExplicitlyEnabled) {
  base::test::ScopedFeatureList scoped;
  scoped.InitAndEnableFeature(features::kMahoUnifiedClickPath);

  EXPECT_TRUE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
}

TEST(MahoUnifiedClickPathFeatureTest, DisabledWhenExplicitlyDisabled) {
  base::test::ScopedFeatureList scoped;
  scoped.InitAndDisableFeature(features::kMahoUnifiedClickPath);

  EXPECT_FALSE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
}

TEST(MahoUnifiedClickPathFeatureTest,
     DefaultRestoredAfterScopedEnableGoesOutOfScope) {
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
}

TEST(MahoUnifiedClickPathFeatureTest, ScopedFlagNestingSymmetry) {
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));

  {
    base::test::ScopedFeatureList outer;
    outer.InitAndEnableFeature(features::kMahoUnifiedClickPath);
    EXPECT_TRUE(
        base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));

    {
      base::test::ScopedFeatureList inner;
      inner.InitAndDisableFeature(features::kMahoUnifiedClickPath);
      EXPECT_FALSE(
          base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
    }

    EXPECT_TRUE(
        base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
  }

  EXPECT_FALSE(
      base::FeatureList::IsEnabled(features::kMahoUnifiedClickPath));
}

}  // namespace
}  // namespace maho
