// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_signature.h"

#include <array>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace maho {
namespace updates {
namespace {

class SignatureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ED25519_keypair(public_key_.data(), private_key_.data());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  std::vector<uint8_t> Sign(std::string_view payload) {
    std::vector<uint8_t> sig(64);
    EXPECT_EQ(1, ED25519_sign(sig.data(),
                              reinterpret_cast<const uint8_t*>(payload.data()),
                              payload.size(), private_key_.data()));
    return sig;
  }

  base::FilePath WriteFile(const std::string& contents) {
    base::FilePath path = temp_dir_.GetPath().AppendASCII("payload.bin");
    EXPECT_TRUE(base::WriteFile(path, contents));
    return path;
  }

  std::array<uint8_t, 32> public_key_{};
  std::array<uint8_t, 64> private_key_{};
  base::ScopedTempDir temp_dir_;
};

TEST_F(SignatureTest, IsZeroKey_AllZerosIsTrue) {
  std::array<uint8_t, 32> key{};
  EXPECT_TRUE(IsZeroKey(key));
}

TEST_F(SignatureTest, IsZeroKey_SingleByteIsFalse) {
  std::array<uint8_t, 32> key{};
  key[15] = 0x01;
  EXPECT_FALSE(IsZeroKey(key));
}

TEST_F(SignatureTest, ManifestSignature_PositiveAcceptsRealSignature) {
  std::string payload = R"({"version":"1.2.3","installer_url":"x"})";
  std::vector<uint8_t> sig = Sign(payload);
  EXPECT_TRUE(VerifyManifestSignature(payload, sig, public_key_));
}

TEST_F(SignatureTest, ManifestSignature_RejectsZeroPublicKey) {
  std::string payload = "{}";
  std::vector<uint8_t> sig = Sign(payload);
  std::array<uint8_t, 32> zero_key{};
  EXPECT_FALSE(VerifyManifestSignature(payload, sig, zero_key));
}

TEST_F(SignatureTest, ManifestSignature_RejectsTamperedPayload) {
  std::string payload = R"({"version":"1.2.3"})";
  std::vector<uint8_t> sig = Sign(payload);
  std::string tampered = R"({"version":"9.9.9"})";
  EXPECT_FALSE(VerifyManifestSignature(tampered, sig, public_key_));
}

TEST_F(SignatureTest, ManifestSignature_RejectsTamperedSignature) {
  std::string payload = R"({"version":"1.2.3"})";
  std::vector<uint8_t> sig = Sign(payload);
  sig[0] ^= 0x01;
  EXPECT_FALSE(VerifyManifestSignature(payload, sig, public_key_));
}

TEST_F(SignatureTest, ManifestSignature_RejectsWrongLengthSignature) {
  std::string payload = "{}";
  std::vector<uint8_t> short_sig(32, 0);
  EXPECT_FALSE(VerifyManifestSignature(payload, short_sig, public_key_));
}

TEST_F(SignatureTest, ManifestSignature_RejectsWrongLengthPublicKey) {
  std::string payload = "{}";
  std::vector<uint8_t> sig = Sign(payload);
  std::array<uint8_t, 16> short_key{};
  EXPECT_FALSE(VerifyManifestSignature(payload, sig, short_key));
}

TEST_F(SignatureTest, Sha256_PositiveMatchesKnownVector) {
  // Empty file SHA-256 is well-known: e3b0...b855.
  base::FilePath path = WriteFile(std::string());
  EXPECT_TRUE(VerifySha256(
      path,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST_F(SignatureTest, Sha256_RejectsHashMismatch) {
  base::FilePath path = WriteFile("hello");
  EXPECT_FALSE(VerifySha256(
      path,
      "0000000000000000000000000000000000000000000000000000000000000000"));
}

TEST_F(SignatureTest, Sha256_AcceptsCaseInsensitiveHex) {
  base::FilePath path = WriteFile("hello");
  std::string upper =
      "2CF24DBA5FB0A30E26E83B2AC5B9E29E1B161E5C1FA7425E73043362938B9824";
  EXPECT_TRUE(VerifySha256(path, upper));
}

TEST_F(SignatureTest, Sha256_RejectsMissingFile) {
  base::FilePath path = temp_dir_.GetPath().AppendASCII("does_not_exist.bin");
  EXPECT_FALSE(VerifySha256(
      path,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

}  // namespace
}  // namespace updates
}  // namespace maho
