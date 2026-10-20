#include "maho/browser/maho_ntp_preload_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

TEST(MahoNtpPreloadPolicyTest, RegularProfileWithoutPressureWarmsOwnNtp) {
  EXPECT_TRUE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                            MahoNtpMemoryPressure::kNone,
                            GURL("chrome://newtab/")));
}

TEST(MahoNtpPreloadPolicyTest, IncognitoProfileDoesNotWarm) {
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kIncognito,
                             MahoNtpMemoryPressure::kNone,
                             GURL("chrome://newtab/")));
}

TEST(MahoNtpPreloadPolicyTest, GuestAndSystemProfilesDoNotWarm) {
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kGuest,
                             MahoNtpMemoryPressure::kNone,
                             GURL("chrome://newtab/")));
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kSystem,
                             MahoNtpMemoryPressure::kNone,
                             GURL("chrome://newtab/")));
}

TEST(MahoNtpPreloadPolicyTest, MemoryPressurePreventsWarming) {
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kModerate,
                             GURL("chrome://newtab/")));
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kCritical,
                             GURL("chrome://newtab/")));
}

TEST(MahoNtpPreloadPolicyTest, RejectsArbitraryOrDecoratedUrls) {
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kNone,
                             GURL("https://example.com/")));
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kNone,
                             GURL("chrome://settings/")));
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kNone,
                             GURL("chrome://newtab/?source=test")));
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kNone,
                             GURL("chrome://newtab/#section")));
  EXPECT_FALSE(ShouldWarmNtp(MahoNtpProfileType::kRegular,
                             MahoNtpMemoryPressure::kNone, GURL()));
}
