// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_auth_utils.h"

#include <string>

#include "base/time/time.h"
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

}  // namespace
}  // namespace maho::auth
