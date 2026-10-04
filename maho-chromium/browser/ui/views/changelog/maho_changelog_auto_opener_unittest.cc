// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/changelog/maho_changelog_auto_opener.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

constexpr char kCurrentVersion[] = "1.0.0";
constexpr char kStaleVersion[] = "0.0.0.1";

constexpr bool kRegular = true;
constexpr bool kNotRegular = false;
constexpr bool kNormalWindow = true;
constexpr bool kNotNormalWindow = false;
constexpr bool kGateActive = true;
constexpr bool kGateInactive = false;
constexpr bool kReadyToInstall = true;
constexpr bool kStableUpdateState = false;

TEST(MahoChangelogAutoOpenDecisionTest, Yield1_NonRegularProfile) {
  auto d = MahoChangelogEvaluateAutoOpen(kStaleVersion, kCurrentVersion,
                                         kNotRegular, kNormalWindow,
                                         kGateInactive, kStableUpdateState);
  EXPECT_FALSE(d.open_tab);
  EXPECT_FALSE(d.write_pref);
}

TEST(MahoChangelogAutoOpenDecisionTest, Yield1_NonNormalWindow) {
  auto d = MahoChangelogEvaluateAutoOpen(kStaleVersion, kCurrentVersion,
                                         kRegular, kNotNormalWindow,
                                         kGateInactive, kStableUpdateState);
  EXPECT_FALSE(d.open_tab);
  EXPECT_FALSE(d.write_pref);
}

TEST(MahoChangelogAutoOpenDecisionTest, Yield2_LoginGateActive) {
  auto d = MahoChangelogEvaluateAutoOpen(kStaleVersion, kCurrentVersion,
                                         kRegular, kNormalWindow, kGateActive,
                                         kStableUpdateState);
  EXPECT_FALSE(d.open_tab);
  EXPECT_FALSE(d.write_pref);
}

TEST(MahoChangelogAutoOpenDecisionTest, Yield3_FreshInstall_WritesPrefNoOpen) {
  auto d = MahoChangelogEvaluateAutoOpen(/*last_shown=*/"", kCurrentVersion,
                                         kRegular, kNormalWindow,
                                         kGateInactive, kStableUpdateState);
  EXPECT_FALSE(d.open_tab);
  EXPECT_TRUE(d.write_pref);
}

TEST(MahoChangelogAutoOpenDecisionTest, Yield4_UpdateReadyToInstall) {
  auto d = MahoChangelogEvaluateAutoOpen(kStaleVersion, kCurrentVersion,
                                         kRegular, kNormalWindow,
                                         kGateInactive, kReadyToInstall);
  EXPECT_FALSE(d.open_tab);
  EXPECT_FALSE(d.write_pref);
}

TEST(MahoChangelogAutoOpenDecisionTest, OpenPath_VersionUpgrade) {
  auto d = MahoChangelogEvaluateAutoOpen(kStaleVersion, kCurrentVersion,
                                         kRegular, kNormalWindow,
                                         kGateInactive, kStableUpdateState);
  EXPECT_TRUE(d.open_tab);
  EXPECT_TRUE(d.write_pref);
}

TEST(MahoChangelogAutoOpenDecisionTest, SameVersion_NoOp) {
  auto d = MahoChangelogEvaluateAutoOpen(kCurrentVersion, kCurrentVersion,
                                         kRegular, kNormalWindow,
                                         kGateInactive, kStableUpdateState);
  EXPECT_FALSE(d.open_tab);
  EXPECT_FALSE(d.write_pref);
}

}  // namespace
}  // namespace maho
