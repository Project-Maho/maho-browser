// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/nss_decrypt_helper.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_MAC)
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#endif

namespace maho {
namespace {

#if BUILDFLAG(IS_MAC)

// Test NssDecryptField with a known 3DES-CBC ciphertext.
// ASN.1 structure: SEQUENCE { SEQUENCE { OID(3DES-CBC), OCTET STRING(IV) },
//                             OCTET STRING(ciphertext) }
//
// Plaintext: "hunter2"
// Key: 24 bytes of 0x41 ('A')
// IV: 8 bytes of 0x20 (space)
// Algorithm: 3DES-CBC with PKCS#7 padding
//
// Generated via:
//   echo -n "hunter2" | openssl enc -des-ede3-cbc -K $(python3 -c
//     "print('41'*24)") -iv $(python3 -c "print('20'*8)") | xxd -i
TEST(NssDecryptHelperTest, Decrypt3DesKnownVector) {
  // 3DES key: 24 bytes of 0x41.
  std::vector<uint8_t> key_bytes(24, 0x41);

  NssGlobalKey global_key;
  global_key.key_bytes = key_bytes;
  global_key.valid = true;

  // Build ASN.1 manually:
  // SEQUENCE {
  //   SEQUENCE {
  //     OID 1.2.840.113549.3.7 (DES-EDE3-CBC)
  //     OCTET STRING (IV: 8 bytes of 0x20)
  //   }
  //   OCTET STRING (ciphertext from 3DES-CBC encrypt of "hunter2")
  // }
  //
  // Ciphertext of "hunter2" + PKCS#7(1 byte pad) under the above key/IV:
  // openssl enc -des-ede3-cbc -K 414141414141414141414141414141414141414141414141
  //   -iv 2020202020202020 -in <(echo -n "hunter2") | xxd -i
  // = 0xd2, 0x19, 0x8d, 0x7d, 0x7c, 0xf1, 0x56, 0x58
  const uint8_t ciphertext[] = {0xd2, 0x19, 0x8d, 0x7d,
                                0x7c, 0xf1, 0x56, 0x58};

  // OID for DES-EDE3-CBC: 1.2.840.113549.3.7
  const uint8_t oid_3des[] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0x86,
                              0xf7, 0x0d, 0x03, 0x07};
  const uint8_t iv_asn1[] = {0x04, 0x08, 0x20, 0x20, 0x20, 0x20,
                             0x20, 0x20, 0x20, 0x20};

  // Inner SEQUENCE: OID + IV
  std::vector<uint8_t> inner_seq;
  inner_seq.insert(inner_seq.end(), oid_3des, oid_3des + sizeof(oid_3des));
  inner_seq.insert(inner_seq.end(), iv_asn1, iv_asn1 + sizeof(iv_asn1));

  // Ciphertext OCTET STRING
  std::vector<uint8_t> ct_octet;
  ct_octet.push_back(0x04);
  ct_octet.push_back(static_cast<uint8_t>(sizeof(ciphertext)));
  ct_octet.insert(ct_octet.end(), ciphertext,
                  ciphertext + sizeof(ciphertext));

  // Wrap inner_seq in SEQUENCE tag
  std::vector<uint8_t> algo_seq;
  algo_seq.push_back(0x30);
  algo_seq.push_back(static_cast<uint8_t>(inner_seq.size()));
  algo_seq.insert(algo_seq.end(), inner_seq.begin(), inner_seq.end());

  // Outer SEQUENCE
  std::vector<uint8_t> outer;
  outer.push_back(0x30);
  outer.push_back(static_cast<uint8_t>(algo_seq.size() + ct_octet.size()));
  outer.insert(outer.end(), algo_seq.begin(), algo_seq.end());
  outer.insert(outer.end(), ct_octet.begin(), ct_octet.end());

  std::string b64 = base::Base64Encode(outer);

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  std::string result = NssDecryptField(b64, global_key, &status);

  EXPECT_EQ(status, NssDecryptStatus::kSuccess);
  EXPECT_EQ(result, "hunter2");
}

// Test NssDecryptField with AES-256-CBC known vector.
// Plaintext: "s3cret!"
// Key: 32 bytes of 0x42 ('B')
// IV: 16 bytes of 0x10
// Algorithm: AES-256-CBC with PKCS#7 padding
TEST(NssDecryptHelperTest, DecryptAes256CbcKnownVector) {
  std::vector<uint8_t> key_bytes(32, 0x42);

  NssGlobalKey global_key;
  global_key.key_bytes = key_bytes;
  global_key.valid = true;

  // openssl enc -aes-256-cbc -K $(python3 -c "print('42'*32)")
  //   -iv $(python3 -c "print('10'*16)") -in <(echo -n "s3cret!") | xxd -i
  // = 0xd2, 0x84, 0xc6, 0x33, 0xdd, 0x88, 0x42, 0xa6,
  //   0x05, 0xe8, 0xda, 0x5a, 0x33, 0xbf, 0x3f, 0x93
  const uint8_t ciphertext[] = {0xd2, 0x84, 0xc6, 0x33, 0xdd, 0x88,
                                0x42, 0xa6, 0x05, 0xe8, 0xda, 0x5a,
                                0x33, 0xbf, 0x3f, 0x93};
  const uint8_t iv[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                        0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10};

  // OID for AES-256-CBC: 2.16.840.1.101.3.4.1.42
  const uint8_t oid_aes256[] = {0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                0x65, 0x03, 0x04, 0x01, 0x2a};
  // IV as OCTET STRING
  std::vector<uint8_t> iv_asn1;
  iv_asn1.push_back(0x04);
  iv_asn1.push_back(0x10);
  iv_asn1.insert(iv_asn1.end(), iv, iv + 16);

  // Inner SEQUENCE: OID + IV
  std::vector<uint8_t> inner_seq;
  inner_seq.insert(inner_seq.end(), oid_aes256,
                   oid_aes256 + sizeof(oid_aes256));
  inner_seq.insert(inner_seq.end(), iv_asn1.begin(), iv_asn1.end());

  // Ciphertext OCTET STRING
  std::vector<uint8_t> ct_octet;
  ct_octet.push_back(0x04);
  ct_octet.push_back(static_cast<uint8_t>(sizeof(ciphertext)));
  ct_octet.insert(ct_octet.end(), ciphertext,
                  ciphertext + sizeof(ciphertext));

  // Wrap in SEQUENCE
  std::vector<uint8_t> algo_seq;
  algo_seq.push_back(0x30);
  algo_seq.push_back(static_cast<uint8_t>(inner_seq.size()));
  algo_seq.insert(algo_seq.end(), inner_seq.begin(), inner_seq.end());

  // Outer SEQUENCE
  std::vector<uint8_t> outer;
  outer.push_back(0x30);
  outer.push_back(static_cast<uint8_t>(algo_seq.size() + ct_octet.size()));
  outer.insert(outer.end(), algo_seq.begin(), algo_seq.end());
  outer.insert(outer.end(), ct_octet.begin(), ct_octet.end());

  std::string b64 = base::Base64Encode(outer);

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  std::string result = NssDecryptField(b64, global_key, &status);

  EXPECT_EQ(status, NssDecryptStatus::kSuccess);
  EXPECT_EQ(result, "s3cret!");
}

// Test that invalid Base64 returns kInvalidAsn1.
TEST(NssDecryptHelperTest, InvalidBase64ReturnsError) {
  NssGlobalKey global_key;
  global_key.key_bytes = std::vector<uint8_t>(24, 0x41);
  global_key.valid = true;

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  std::string result = NssDecryptField("not!valid!base64!", global_key, &status);

  EXPECT_EQ(status, NssDecryptStatus::kInvalidAsn1);
  EXPECT_TRUE(result.empty());
}

// Test that invalid global key returns error.
TEST(NssDecryptHelperTest, InvalidKeyReturnsError) {
  NssGlobalKey global_key;
  global_key.valid = false;

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  std::string result = NssDecryptField("AAAA", global_key, &status);

  EXPECT_EQ(status, NssDecryptStatus::kKeyDerivationFailed);
  EXPECT_TRUE(result.empty());
}

// Test that truncated ASN.1 returns kInvalidAsn1.
TEST(NssDecryptHelperTest, TruncatedAsn1ReturnsError) {
  NssGlobalKey global_key;
  global_key.key_bytes = std::vector<uint8_t>(24, 0x41);
  global_key.valid = true;

  // Valid base64 but truncated ASN.1 (just a SEQUENCE tag with wrong length).
  std::vector<uint8_t> bad_asn1 = {0x30, 0x50, 0x30, 0x03};
  std::string b64 = base::Base64Encode(bad_asn1);

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  std::string result = NssDecryptField(b64, global_key, &status);

  EXPECT_EQ(status, NssDecryptStatus::kInvalidAsn1);
  EXPECT_TRUE(result.empty());
}

// Test decryption with key too short for 3DES (should fail).
TEST(NssDecryptHelperTest, KeyTooShortFor3DesReturnsError) {
  // Only 8 bytes — too short for 3DES (needs 24).
  std::vector<uint8_t> short_key(8, 0x41);

  NssGlobalKey global_key;
  global_key.key_bytes = short_key;
  global_key.valid = true;

  // Build a valid 3DES ASN.1 structure with dummy ciphertext.
  const uint8_t dummy_ct[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  const uint8_t oid_3des[] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0x86,
                              0xf7, 0x0d, 0x03, 0x07};
  const uint8_t iv_asn1[] = {0x04, 0x08, 0x20, 0x20, 0x20, 0x20,
                             0x20, 0x20, 0x20, 0x20};

  std::vector<uint8_t> inner_seq;
  inner_seq.insert(inner_seq.end(), oid_3des, oid_3des + sizeof(oid_3des));
  inner_seq.insert(inner_seq.end(), iv_asn1, iv_asn1 + sizeof(iv_asn1));

  std::vector<uint8_t> ct_octet;
  ct_octet.push_back(0x04);
  ct_octet.push_back(static_cast<uint8_t>(sizeof(dummy_ct)));
  ct_octet.insert(ct_octet.end(), dummy_ct, dummy_ct + sizeof(dummy_ct));

  std::vector<uint8_t> algo_seq;
  algo_seq.push_back(0x30);
  algo_seq.push_back(static_cast<uint8_t>(inner_seq.size()));
  algo_seq.insert(algo_seq.end(), inner_seq.begin(), inner_seq.end());

  std::vector<uint8_t> outer;
  outer.push_back(0x30);
  outer.push_back(static_cast<uint8_t>(algo_seq.size() + ct_octet.size()));
  outer.insert(outer.end(), algo_seq.begin(), algo_seq.end());
  outer.insert(outer.end(), ct_octet.begin(), ct_octet.end());

  std::string b64 = base::Base64Encode(outer);

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  std::string result = NssDecryptField(b64, global_key, &status);

  EXPECT_EQ(status, NssDecryptStatus::kDecryptionFailed);
  EXPECT_TRUE(result.empty());
}

TEST(NssDecryptHelperTest, EmptyInputReturnsError) {
  NssGlobalKey global_key;
  global_key.key_bytes = std::vector<uint8_t>(24, 0x41);
  global_key.valid = true;

  NssDecryptStatus status = NssDecryptStatus::kSuccess;
  EXPECT_DCHECK_DEATH(NssDecryptField("", global_key, &status));
}

class NssDecryptHelperParamTest : public testing::TestWithParam<const char*> {};

TEST_P(NssDecryptHelperParamTest, LoadFixture) {
  base::FilePath src_dir;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_dir));
  base::FilePath fixture_path = src_dir
      .AppendASCII("maho")
      .AppendASCII("browser")
      .AppendASCII("importer")
      .AppendASCII("test_data")
      .AppendASCII("synthetic")
      .AppendASCII(GetParam());

  std::string content;
  EXPECT_TRUE(base::ReadFileToString(fixture_path, &content));
  EXPECT_FALSE(content.empty());
}

INSTANTIATE_TEST_SUITE_P(
    NssDecryptHelperFixtures,
    NssDecryptHelperParamTest,
    testing::Values("nss_3des_cbc.bin",
                    "nss_aes256_cbc_sha256.bin",
                    "nss_aes256_cbc_sha384.bin",
                    "nss_aes256_gcm.bin"));

#endif  // BUILDFLAG(IS_MAC)

}  // namespace
}  // namespace maho
