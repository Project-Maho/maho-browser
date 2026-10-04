// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_badge.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoSidebarMailBadgeTest, FormatsNumericUnreadCounts) {
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 0).text, u"");
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 1).text, u"1");
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 9).text, u"9");
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 99).text, u"99");
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 100).text, u"99+");
}

TEST(MahoSidebarMailBadgeTest, FeatureAndBadgePreferencesHideBadge) {
  EXPECT_FALSE(ResolveMailBadgePresentation(false, true, 9).visible);
  EXPECT_FALSE(ResolveMailBadgePresentation(true, false, 9).visible);
  EXPECT_FALSE(ResolveMailBadgePresentation(true, true, 0).visible);
}

TEST(MahoSidebarMailBadgeTest, AccessibilityNamesMatchVisibleValue) {
  EXPECT_EQ(ResolveMailBadgePresentation(false, true, 9).accessible_name,
            u"Mail");
  EXPECT_EQ(ResolveMailBadgePresentation(true, false, 9).accessible_name,
            u"Mail");
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 1).accessible_name,
            u"Mail, 1 unread message");
  EXPECT_EQ(ResolveMailBadgePresentation(true, true, 100).accessible_name,
            u"Mail, 99+ unread messages");
}


TEST(MahoSidebarMailBadgeTest, PrunesRemovedAccountsAndTheirLateCallbacks) {
  MailUnreadBadgeState state;
  const uint64_t account_a = state.BeginRefresh("a");
  const uint64_t account_b = state.BeginRefresh("b");
  EXPECT_TRUE(state.CompleteRefresh("a", account_a, 4));
  EXPECT_TRUE(state.CompleteRefresh("b", account_b, 7));
  EXPECT_EQ(state.total(), 11);

  EXPECT_TRUE(state.RetainAccounts({"b"}));
  EXPECT_EQ(state.total(), 7);
  EXPECT_FALSE(state.CompleteRefresh("a", account_a, 99));
  EXPECT_EQ(state.total(), 7);
}

TEST(MahoSidebarMailBadgeTest, IgnoresLateCallbackFromOlderRefresh) {
  MailUnreadBadgeState state;
  const uint64_t older = state.BeginRefresh("account");
  const uint64_t newer = state.BeginRefresh("account");

  EXPECT_TRUE(state.CompleteRefresh("account", newer, 2));
  EXPECT_FALSE(state.CompleteRefresh("account", older, 50));
  EXPECT_EQ(state.total(), 2);
}

}  // namespace maho
