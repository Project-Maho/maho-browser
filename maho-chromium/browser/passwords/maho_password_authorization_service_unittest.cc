// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/passwords/maho_password_authorization_service.h"

#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/device_reauth/mock_device_authenticator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::passwords {
namespace {

class MahoPasswordAuthorizationServiceTest : public testing::Test {
protected:
  void SetUp() override {
    service_ = MahoPasswordAuthorizationService::Get();
    service_->ResetForTesting();
    service_->SetVaultLockedForTesting(false);
    // Pin the platform-dependent default (not required on Linux) so the
    // device-auth tests run identically everywhere.
    service_->SetDeviceReauthRequiredForTesting(true);
  }

  void TearDown() override { service_->ResetForTesting(); }

  MahoPasswordAuthorizationService *service_ = nullptr;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(MahoPasswordAuthorizationServiceTest,
       RequiresExactProfileOriginAndAction) {
  const base::TimeTicks expiry =
      base::TimeTicks::Now() +
      MahoPasswordAuthorizationService::kAuthorizationLifetime;
  service_->GrantForTesting("profile-a", "https://example.test/login",
                            PasswordAuthorizationAction::kFill, expiry);

  EXPECT_TRUE(
      service_->ConsumeAuthorization("profile-a", "https://example.test/path",
                                     PasswordAuthorizationAction::kFill));
  EXPECT_FALSE(
      service_->ConsumeAuthorization("profile-b", "https://example.test/path",
                                     PasswordAuthorizationAction::kFill));
  EXPECT_FALSE(
      service_->ConsumeAuthorization("profile-a", "https://other.test/path",
                                     PasswordAuthorizationAction::kFill));
  EXPECT_FALSE(
      service_->ConsumeAuthorization("profile-a", "https://example.test/path",
                                     PasswordAuthorizationAction::kDelete));
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       SuccessfulDeviceAuthGrantsOnlyRequestedOperation) {
  service_->SetVaultLockedForTesting(false);
  auto authenticator =
      std::make_unique<device_reauth::MockDeviceAuthenticator>();
  auto *authenticator_ptr = authenticator.get();
  EXPECT_CALL(*authenticator_ptr, AuthenticateWithMessage)
      .WillOnce([](const std::u16string &,
                   device_reauth::DeviceAuthenticator::AuthenticateCallback
                       callback) { std::move(callback).Run(true); });

  bool callback_result = false;
  service_->Authorize(
      "profile-a", "https://example.test/path",
      PasswordAuthorizationAction::kFill, std::move(authenticator), u"Fill",
      base::BindOnce([](bool *result, bool success) { *result = success; },
                     &callback_result));

  EXPECT_TRUE(callback_result);
  EXPECT_TRUE(
      service_->ConsumeAuthorization("profile-a", "https://example.test/other",
                                     PasswordAuthorizationAction::kFill));
  EXPECT_FALSE(
      service_->ConsumeAuthorization("profile-a", "https://example.test/other",
                                     PasswordAuthorizationAction::kDelete));
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       CancelledDeviceAuthenticationDoesNotGrant) {
  service_->SetVaultLockedForTesting(false);
  auto authenticator =
      std::make_unique<device_reauth::MockDeviceAuthenticator>();
  auto *authenticator_ptr = authenticator.get();
  EXPECT_CALL(*authenticator_ptr, AuthenticateWithMessage)
      .WillOnce([](const std::u16string &,
                   device_reauth::DeviceAuthenticator::AuthenticateCallback
                       callback) { std::move(callback).Run(false); });

  bool callback_result = true;
  service_->Authorize(
      "profile-a", "https://example.test/path",
      PasswordAuthorizationAction::kFill, std::move(authenticator), u"Fill",
      base::BindOnce([](bool *result, bool success) { *result = success; },
                     &callback_result));

  EXPECT_FALSE(callback_result);
  EXPECT_FALSE(
      service_->ConsumeAuthorization("profile-a", "https://example.test/other",
                                     PasswordAuthorizationAction::kFill));
  EXPECT_EQ(0u, service_->grant_count_for_testing());
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       CancelsPendingAuthenticationWhenProfileIsInvalidated) {
  service_->SetVaultLockedForTesting(false);
  auto authenticator =
      std::make_unique<device_reauth::MockDeviceAuthenticator>();
  auto *authenticator_ptr = authenticator.get();
  EXPECT_CALL(*authenticator_ptr, AuthenticateWithMessage);
  EXPECT_CALL(*authenticator_ptr, Cancel);

  bool callback_result = true;
  service_->Authorize(
      "profile-a", "https://example.test/path",
      PasswordAuthorizationAction::kFill, std::move(authenticator), u"Fill",
      base::BindOnce([](bool *result, bool success) { *result = success; },
                     &callback_result));
  service_->RevokeProfile("profile-a");

  EXPECT_FALSE(callback_result);
  EXPECT_EQ(0u, service_->grant_count_for_testing());
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       VaultLockCancelsPendingAuthentication) {
  service_->SetVaultLockedForTesting(false);
  auto authenticator =
      std::make_unique<device_reauth::MockDeviceAuthenticator>();
  auto *authenticator_ptr = authenticator.get();
  EXPECT_CALL(*authenticator_ptr, AuthenticateWithMessage);
  EXPECT_CALL(*authenticator_ptr, Cancel);

  bool callback_result = true;
  service_->Authorize(
      "profile-a", "https://example.test/path",
      PasswordAuthorizationAction::kFill, std::move(authenticator), u"Fill",
      base::BindOnce([](bool *result, bool success) { *result = success; },
                     &callback_result));
  service_->SetVaultLockedForTesting(true);

  EXPECT_FALSE(callback_result);
  EXPECT_EQ(0u, service_->grant_count_for_testing());
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       RangeDeleteScopeIsActionAndScopeIsolated) {
  const base::TimeTicks expiry =
      base::TimeTicks::Now() +
      MahoPasswordAuthorizationService::kAuthorizationLifetime;
  service_->SetVaultLockedForTesting(false);
  service_->GrantForTesting("profile-a",
                            MahoPasswordAuthorizationService::kRangeDeleteScope,
                            PasswordAuthorizationAction::kRangeDelete, expiry);

  EXPECT_TRUE(service_->ConsumeAuthorization(
      "profile-a", MahoPasswordAuthorizationService::kRangeDeleteScope,
      PasswordAuthorizationAction::kRangeDelete));
  EXPECT_FALSE(service_->ConsumeAuthorization(
      "profile-a", MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kRangeDelete));
  EXPECT_FALSE(service_->ConsumeAuthorization(
      "profile-a", MahoPasswordAuthorizationService::kRangeDeleteScope,
      PasswordAuthorizationAction::kDelete));
}

TEST_F(MahoPasswordAuthorizationServiceTest, RejectsExpiredGrant) {
  const base::TimeTicks expiry =
      base::TimeTicks::Now() +
      MahoPasswordAuthorizationService::kAuthorizationLifetime;
  service_->GrantForTesting("profile-a",
                            MahoPasswordAuthorizationService::kAllOriginsScope,
                            PasswordAuthorizationAction::kAdd, expiry);
  task_environment_.FastForwardBy(
      MahoPasswordAuthorizationService::kAuthorizationLifetime);

  EXPECT_FALSE(service_->ConsumeAuthorization(
      "profile-a", MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kAdd));
}

TEST_F(MahoPasswordAuthorizationServiceTest, LockAndTeardownRevokeGrants) {
  const base::TimeTicks expiry =
      base::TimeTicks::Now() +
      MahoPasswordAuthorizationService::kAuthorizationLifetime;
  service_->GrantForTesting("profile-a",
                            MahoPasswordAuthorizationService::kAllOriginsScope,
                            PasswordAuthorizationAction::kDelete, expiry);
  service_->SetVaultLockedForTesting(true);
  EXPECT_FALSE(service_->ConsumeAuthorization(
      "profile-a", MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kDelete));

  service_->SetVaultLockedForTesting(false);
  service_->GrantForTesting("profile-a",
                            MahoPasswordAuthorizationService::kAllOriginsScope,
                            PasswordAuthorizationAction::kDelete, expiry);
  service_->RevokeProfile("profile-a");
  EXPECT_EQ(0u, service_->grant_count_for_testing());
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       DisabledDeviceReauthAuthorizesUnlockedVaultOperations) {
  service_->SetDeviceReauthRequiredForTesting(false);

  EXPECT_TRUE(service_->ConsumeAuthorization(
      "profile-a", "https://example.test/login",
      PasswordAuthorizationAction::kFill));
  EXPECT_TRUE(service_->ConsumeAuthorization(
      "profile-a", MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kCopy));
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       DisabledDeviceReauthStillFailsClosedWhenVaultIsLocked) {
  service_->SetDeviceReauthRequiredForTesting(false);
  service_->SetVaultLockedForTesting(true);

  EXPECT_FALSE(service_->ConsumeAuthorization(
      "profile-a", "https://example.test/login",
      PasswordAuthorizationAction::kFill));
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       DisabledDeviceReauthAuthorizesWithoutPlatformAuthenticator) {
  // Linux desktop has no DeviceAuthenticator; Vault writes must still work
  // on an unlocked Vault when device reauth is not required.
  service_->SetDeviceReauthRequiredForTesting(false);
  std::optional<bool> result;
  service_->Authorize(
      "profile-a", MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kAdd, nullptr, u"Add",
      base::BindOnce([](std::optional<bool> *out, bool ok) { *out = ok; },
                     &result));
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(*result);
}

TEST_F(MahoPasswordAuthorizationServiceTest,
       RequiredDeviceReauthFailsClosedWithoutPlatformAuthenticator) {
  service_->SetDeviceReauthRequiredForTesting(true);
  std::optional<bool> result;
  service_->Authorize(
      "profile-a", MahoPasswordAuthorizationService::kAllOriginsScope,
      PasswordAuthorizationAction::kAdd, nullptr, u"Add",
      base::BindOnce([](std::optional<bool> *out, bool ok) { *out = ok; },
                     &result));
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(*result);
}

} // namespace
} // namespace maho::passwords
