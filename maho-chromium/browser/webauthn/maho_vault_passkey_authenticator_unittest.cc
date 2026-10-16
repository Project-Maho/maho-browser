// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/webauthn/maho_vault_passkey_authenticator.h"

#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "components/cbor/reader.h"
#include "crypto/hash.h"
#include "device/fido/authenticator_data.h"
#include "device/fido/ctap_get_assertion_request.h"
#include "device/fido/ctap_make_credential_request.h"
#include "device/fido/public/fido_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class VaultPasskeyAuthenticatorTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(VaultPasskeyAuthenticatorTest, MapsNotFoundToUnrecognizedCredential) {
  const auto response = base::JSONReader::Read(
      R"({"ok":false,"error":{"code":"not_found","message":"missing"}})",
         base::JSON_PARSE_RFC);
  ASSERT_TRUE(response);
  EXPECT_EQ(device::GetAssertionStatus::kUserConsentButCredentialNotRecognized,
            MapGetErrorForTesting(*response));
}

TEST_F(VaultPasskeyAuthenticatorTest, MapsRpMismatchToUnrecognizedCredential) {
  const auto response = base::JSONReader::Read(
      R"({"ok":false,"error":{"code":"rp_mismatch","message":"wrong RP"}})",
         base::JSON_PARSE_RFC);
  ASSERT_TRUE(response);
  EXPECT_EQ(device::GetAssertionStatus::kUserConsentButCredentialNotRecognized,
            MapGetErrorForTesting(*response));
}

TEST_F(VaultPasskeyAuthenticatorTest, MapsUvRequiredToMissingVerification) {
  const auto response = base::JSONReader::Read(
      R"({"ok":false,"error":{"code":"uv_required","message":"UV required"}})",
         base::JSON_PARSE_RFC);
  ASSERT_TRUE(response);
  EXPECT_EQ(device::MakeCredentialStatus::kAuthenticatorMissingUserVerification,
            MapCreateErrorForTesting(*response));
  EXPECT_EQ(device::GetAssertionStatus::kAuthenticatorMissingUserVerification,
            MapGetErrorForTesting(*response));
}

TEST_F(VaultPasskeyAuthenticatorTest, DecodesBase64UrlBinaryFields) {
  const auto response = base::JSONReader::Read(
      R"({"ok":true,"data":{"credentialId":"AQID","publicKeyCose":"pQECAyYgASFYIA","itemId":"item"}})",
         base::JSON_PARSE_RFC);
  ASSERT_TRUE(response);
  const auto credential_id = DecodeBase64UrlForTesting(*response,
                                                        "data.credentialId");
  ASSERT_TRUE(credential_id);
  EXPECT_EQ((std::vector<uint8_t>{1, 2, 3}), *credential_id);
}

}  // namespace
}  // namespace maho
