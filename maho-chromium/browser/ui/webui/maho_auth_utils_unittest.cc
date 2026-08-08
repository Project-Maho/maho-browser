// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_auth_utils.h"

#include <string>

#include "base/time/time.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::auth {
namespace {

class HasValidRelaySessionTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayAccessTokenEncryptedB64, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayRefreshTokenEncryptedB64, "");
    prefs_.registry()->RegisterInt64Pref(
        account_prefs::kRelayAccessTokenExpiresAt, 0);
    prefs_.registry()->RegisterInt64Pref(
        account_prefs::kRelayRefreshTokenExpiresAt, 0);
    prefs_.registry()->RegisterStringPref(account_prefs::kRelayUserEmail, "");
    prefs_.registry()->RegisterStringPref(account_prefs::kRelayUserId, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayUserDisplayName, "");
    prefs_.registry()->RegisterStringPref(account_prefs::kRelayUserTier, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayOAuthProvider, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelayOAuthProviderSub, "");
    prefs_.registry()->RegisterStringPref(
        account_prefs::kRelaySubscriptionStatus, "");
    prefs_.registry()->RegisterInt64Pref(
        account_prefs::kRelaySubscriptionExpiresAt, 0);
  }

  int64_t NowSec() const { return base::Time::Now().ToTimeT(); }
  int64_t FutureSec() const { return NowSec() + 3600; }
  int64_t PastSec() const { return NowSec() - 3600; }

  TestingPrefServiceSimple prefs_;
};

TEST_F(HasValidRelaySessionTest, ReturnsFalseWhenTokensMissing) {
  EXPECT_FALSE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest, ReturnsTrueWhenAccessTokenValid) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, FutureSec());
  EXPECT_TRUE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest,
       ReturnsTrueWhenAccessExpiredButRefreshValid) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, PastSec());
  prefs_.SetString(account_prefs::kRelayRefreshTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayRefreshTokenExpiresAt, FutureSec());
  EXPECT_TRUE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest, ReturnsFalseWhenBothExpired) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, PastSec());
  prefs_.SetString(account_prefs::kRelayRefreshTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayRefreshTokenExpiresAt, PastSec());
  EXPECT_FALSE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest,
       ReturnsFalseWhenAccessExpiredAndRefreshMissing) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, PastSec());
  EXPECT_FALSE(HasValidRelaySession(&prefs_));
}

TEST_F(HasValidRelaySessionTest, OldSessionWithoutOAuthMetadataRemainsValid) {
  prefs_.SetString(account_prefs::kRelayAccessTokenEncryptedB64, "opaque");
  prefs_.SetInt64(account_prefs::kRelayAccessTokenExpiresAt, FutureSec());

  EXPECT_TRUE(HasValidRelaySession(&prefs_));
  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));
}

TEST_F(HasValidRelaySessionTest, GoogleMetadataBuildsExactMailOptions) {
  prefs_.SetString(account_prefs::kRelayUserEmail, " user@example.com ");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, " google ");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, " subject-123 ");

  EXPECT_EQ(
      R"({"expected_google_sub":"subject-123","login_hint":"user@example.com"})",
      BuildMailOAuthStartOptionsJson(&prefs_));
}

TEST_F(HasValidRelaySessionTest, NonGoogleOrMalformedMetadataIsIndependent) {
  prefs_.SetString(account_prefs::kRelayUserEmail, "user@example.com");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, "github");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "subject-123");
  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));

  prefs_.SetString(account_prefs::kRelayOAuthProvider, " google ");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "   ");
  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));
}

TEST_F(HasValidRelaySessionTest, RefreshPreservesOAuthMetadataPair) {
  prefs_.SetString(account_prefs::kRelayOAuthProvider, " google ");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, " subject-123 ");
  StoreTokensParams params;

  PreserveRelayIdentityMetadata(&prefs_, &params);

  EXPECT_EQ(" google ", params.oauth_provider);
  EXPECT_EQ(" subject-123 ", params.oauth_provider_sub);
}

TEST_F(HasValidRelaySessionTest, LogoutClearsOAuthMetadataRepeatedly) {
  prefs_.SetString(account_prefs::kRelayUserEmail, "user@example.com");
  prefs_.SetString(account_prefs::kRelayOAuthProvider, "google");
  prefs_.SetString(account_prefs::kRelayOAuthProviderSub, "subject-123");

  ClearRelayTokens(&prefs_);
  ClearRelayTokens(&prefs_);

  EXPECT_EQ("{}", BuildMailOAuthStartOptionsJson(&prefs_));
  EXPECT_TRUE(prefs_.GetString(account_prefs::kRelayOAuthProvider).empty());
  EXPECT_TRUE(prefs_.GetString(account_prefs::kRelayOAuthProviderSub).empty());
}

}  // namespace
}  // namespace maho::auth
