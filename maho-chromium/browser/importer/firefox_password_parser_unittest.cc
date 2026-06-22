// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/firefox_password_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_MAC)
#include <CommonCrypto/CommonCryptor.h>
#endif

namespace maho {
namespace {

#if BUILDFLAG(IS_MAC)


// Helper to convert hex string to byte vector
std::vector<uint8_t> HexToBytes(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
    bytes.push_back(byte);
  }
  return bytes;
}

// ASN.1 construction helpers
std::vector<uint8_t> BuildAsn1Sequence(const std::vector<uint8_t>& content) {
  std::vector<uint8_t> seq;
  seq.push_back(0x30);
  if (content.size() >= 128) {
    size_t len = content.size();
    std::vector<uint8_t> len_bytes;
    while (len > 0) {
      len_bytes.push_back(len & 0xff);
      len = len >> 8;
    }
    seq.push_back(0x80 | len_bytes.size());
    for (auto it = len_bytes.rbegin(); it != len_bytes.rend(); ++it) {
      seq.push_back(*it);
    }
  } else {
    seq.push_back(content.size());
  }
  seq.insert(seq.end(), content.begin(), content.end());
  return seq;
}

std::vector<uint8_t> BuildAsn1OctetString(const std::vector<uint8_t>& content) {
  std::vector<uint8_t> oct;
  oct.push_back(0x04);
  if (content.size() >= 128) {
    size_t len = content.size();
    std::vector<uint8_t> len_bytes;
    while (len > 0) {
      len_bytes.push_back(len & 0xff);
      len = len >> 8;
    }
    oct.push_back(0x80 | len_bytes.size());
    for (auto it = len_bytes.rbegin(); it != len_bytes.rend(); ++it) {
      oct.push_back(*it);
    }
  } else {
    oct.push_back(content.size());
  }
  oct.insert(oct.end(), content.begin(), content.end());
  return oct;
}

// Encrypt helpers
std::vector<uint8_t> Encrypt3Des(const uint8_t* key, size_t key_len,
                                  const uint8_t* iv, const uint8_t* data,
                                  size_t data_len) {
  std::vector<uint8_t> output(data_len + kCCBlockSize3DES);
  size_t out_len = 0;
  CCCryptorStatus status = CCCrypt(kCCEncrypt, kCCAlgorithm3DES,
                                    kCCOptionPKCS7Padding, key, key_len, iv,
                                    data, data_len, output.data(),
                                    output.size(), &out_len);
  if (status != kCCSuccess) {
    return {};
  }
  output.resize(out_len);
  return output;
}

std::vector<uint8_t> EncryptAes256Cbc(const uint8_t* key, const uint8_t* iv,
                                       const uint8_t* data, size_t data_len) {
  std::vector<uint8_t> output(data_len + kCCBlockSizeAES128);
  size_t out_len = 0;
  CCCryptorStatus status =
      CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, key,
              32, iv, data, data_len, output.data(),
              output.size(), &out_len);
  if (status != kCCSuccess) {
    return {};
  }
  output.resize(out_len);
  return output;
}

// Dynamic ASN.1 encrypted field builder
std::string BuildEncryptedField(const std::string& plaintext,
                                const std::vector<uint8_t>& key,
                                bool use_3des) {
  if (use_3des) {
    std::vector<uint8_t> iv(8, 0x10);
    std::vector<uint8_t> encrypted = Encrypt3Des(
        key.data(), key.size(), iv.data(),
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
    
    const uint8_t oid_3des[] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0x86,
                                0xf7, 0x0d, 0x03, 0x07};
    std::vector<uint8_t> inner_seq;
    inner_seq.insert(inner_seq.end(), oid_3des, oid_3des + sizeof(oid_3des));
    auto iv_asn1 = BuildAsn1OctetString(iv);
    inner_seq.insert(inner_seq.end(), iv_asn1.begin(), iv_asn1.end());
    auto algo_seq = BuildAsn1Sequence(inner_seq);

    std::vector<uint8_t> outer;
    outer.insert(outer.end(), algo_seq.begin(), algo_seq.end());
    auto enc_asn1 = BuildAsn1OctetString(encrypted);
    outer.insert(outer.end(), enc_asn1.begin(), enc_asn1.end());
    auto outer_seq = BuildAsn1Sequence(outer);
    return base::Base64Encode(outer_seq);
  } else {
    std::vector<uint8_t> iv(16, 0x10);
    std::vector<uint8_t> encrypted = EncryptAes256Cbc(
        key.data(), iv.data(),
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
    
    const uint8_t oid_aes256[] = {0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                  0x65, 0x03, 0x04, 0x01, 0x2a};
    std::vector<uint8_t> inner_seq;
    inner_seq.insert(inner_seq.end(), oid_aes256, oid_aes256 + sizeof(oid_aes256));
    auto iv_asn1 = BuildAsn1OctetString(iv);
    inner_seq.insert(inner_seq.end(), iv_asn1.begin(), iv_asn1.end());
    auto algo_seq = BuildAsn1Sequence(inner_seq);

    std::vector<uint8_t> outer;
    outer.insert(outer.end(), algo_seq.begin(), algo_seq.end());
    auto enc_asn1 = BuildAsn1OctetString(encrypted);
    outer.insert(outer.end(), enc_asn1.begin(), enc_asn1.end());
    auto outer_seq = BuildAsn1Sequence(outer);
    return base::Base64Encode(outer_seq);
  }
}

// Writes logins.json to the profile path
bool WriteLoginsJson(const base::FilePath& profile_path,
                     const std::string& hostname,
                     const std::string& enc_user,
                     const std::string& enc_pass) {
  std::string json = "{\n"
                     "  \"nextId\": 2,\n"
                     "  \"logins\": [\n"
                     "    {\n"
                     "      \"id\": 1,\n"
                     "      \"hostname\": \"" + hostname + "\",\n"
                     "      \"httpRealm\": null,\n"
                     "      \"formSubmitURL\": \"" + hostname + "/login\",\n"
                     "      \"usernameField\": \"user\",\n"
                     "      \"passwordField\": \"pass\",\n"
                     "      \"encryptedUsername\": \"" + enc_user + "\",\n"
                     "      \"encryptedPassword\": \"" + enc_pass + "\",\n"
                     "      \"guid\": \"{guid-123}\"\n"
                     "    }\n"
                     "  ]\n"
                     "}";
  return base::WriteFile(profile_path.Append("logins.json"), json);
}

// Writes key4.db SQLite database
bool WriteKey4Db(const base::FilePath& profile_path,
                 const std::vector<uint8_t>& item1,
                 const std::vector<uint8_t>& item2,
                 const std::vector<uint8_t>& a11) {
  base::FilePath db_path = profile_path.Append("key4.db");
  sql::Database db("FirefoxImporter");
  if (!db.Open(db_path)) {
    return false;
  }
  if (!db.Execute("CREATE TABLE metadata (id TEXT, item1 BLOB, item2 BLOB)")) {
    return false;
  }
  if (!db.Execute("CREATE TABLE nssPrivate (a11 BLOB, a102 BLOB)")) {
    return false;
  }

  sql::Statement stmt_meta(db.GetUniqueStatement(
      "INSERT INTO metadata (id, item1, item2) VALUES ('password', ?, ?)"));
  stmt_meta.BindBlob(0, item1);
  stmt_meta.BindBlob(1, item2);
  if (!stmt_meta.Run()) {
    return false;
  }

  sql::Statement stmt_priv(db.GetUniqueStatement(
      "INSERT INTO nssPrivate (a11, a102) VALUES (?, NULL)"));
  stmt_priv.BindBlob(0, a11);
  if (!stmt_priv.Run()) {
    return false;
  }

  return true;
}

TEST(FirefoxPasswordParserTest, ParseVariantAValid) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Pre-computed Variant A values from generate_blobs
  std::vector<uint8_t> item1 = HexToBytes("0101010101010101010101010101010101010101");
  std::vector<uint8_t> item2 = HexToBytes("3028301406082a864886f70d0307040820202020202020200410e434d67f29a9e2f8c95244cd3133dd18");
  std::vector<uint8_t> a11 = HexToBytes("304c3028060b2a864886f70d010c05010330190414010101010101010101010101010101010101010102010104206b04050186e266204c7e706a29de53e293d4c7213ec06fefb6282b46d60e5ce8");

  ASSERT_TRUE(WriteKey4Db(temp_dir.GetPath(), item1, item2, a11));

  // Global key for Variant A is 24 bytes of 0x41 ('A')
  std::vector<uint8_t> global_key(24, 0x41);
  std::string enc_user = BuildEncryptedField("my_username", global_key, /*use_3des=*/true);
  std::string enc_pass = BuildEncryptedField("my_password", global_key, /*use_3des=*/true);

  ASSERT_TRUE(WriteLoginsJson(temp_dir.GetPath(), "https://firefox.com", enc_user, enc_pass));

  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kSuccess);
  ASSERT_EQ(logins.size(), 1u);
  EXPECT_EQ(logins[0].origin_url, GURL("https://firefox.com"));
  EXPECT_EQ(logins[0].action_url, GURL("https://firefox.com/login"));
  EXPECT_EQ(logins[0].username, u"my_username");
  EXPECT_EQ(logins[0].password, u"my_password");
}

TEST(FirefoxPasswordParserTest, ParseVariantBValid) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Pre-computed Variant B values from generate_blobs
  std::vector<uint8_t> item1 = HexToBytes("02020202020202020202020202020202");
  std::vector<uint8_t> item2 = HexToBytes("3031301d060960864801650304012a0410202020202020202020202020202020200410e61d22c1d27cf3389300e888fa387927");
  std::vector<uint8_t> a11 = HexToBytes("3075304106092a864886f70d01050c3015041003030303030303030303030303030303020101301d060960864801650304012a0410040404040404040404040404040404040430336997d1ac27385fe7845455c65e04d6f99f862bdb0bc51762bdd6356e25613555b6aff2094fd501bda4a3a52e4f92d4");

  ASSERT_TRUE(WriteKey4Db(temp_dir.GetPath(), item1, item2, a11));

  // Global key for Variant B is 32 bytes of 0x42 ('B')
  std::vector<uint8_t> global_key(32, 0x42);
  std::string enc_user = BuildEncryptedField("my_username", global_key, /*use_3des=*/false);
  std::string enc_pass = BuildEncryptedField("my_password", global_key, /*use_3des=*/false);

  ASSERT_TRUE(WriteLoginsJson(temp_dir.GetPath(), "https://firefox.com", enc_user, enc_pass));

  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kSuccess);
  ASSERT_EQ(logins.size(), 1u);
  EXPECT_EQ(logins[0].username, u"my_username");
  EXPECT_EQ(logins[0].password, u"my_password");
}

TEST(FirefoxPasswordParserTest, ParseVariantCValid) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Pre-computed Variant C values from generate_blobs
  std::vector<uint8_t> item1 = HexToBytes("020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202");
  std::vector<uint8_t> item2 = HexToBytes("3031301d060960864801650304012a0410202020202020202020202020202020200410e61d22c1d27cf3389300e888fa387927");
  std::vector<uint8_t> a11 = HexToBytes("3075304106092a864886f70d01050c3015041003030303030303030303030303030303020101301d060960864801650304012a04100404040404040404040404040404040404308e40c49787976578f0f83007ff59560159ddb03673d3e2a31150ee2b6fd7617200e5fdcbbf304cbe4323fb0be2b9e4be");

  ASSERT_TRUE(WriteKey4Db(temp_dir.GetPath(), item1, item2, a11));

  // Global key for Variant C is 32 bytes of 0x42 ('B')
  std::vector<uint8_t> global_key(32, 0x42);
  std::string enc_user = BuildEncryptedField("my_username", global_key, /*use_3des=*/false);
  std::string enc_pass = BuildEncryptedField("my_password", global_key, /*use_3des=*/false);

  ASSERT_TRUE(WriteLoginsJson(temp_dir.GetPath(), "https://firefox.com", enc_user, enc_pass));

  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kSuccess);
  ASSERT_EQ(logins.size(), 1u);
  EXPECT_EQ(logins[0].username, u"my_username");
  EXPECT_EQ(logins[0].password, u"my_password");
}

TEST(FirefoxPasswordParserTest, ParseKey3DbUnsupported) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Create key3.db instead of key4.db
  base::FilePath key3_path = temp_dir.GetPath().Append("key3.db");
  ASSERT_TRUE(base::WriteFile(key3_path, "dummy key3.db contents"));

  // logins.json must exist so it does not fail on that first
  ASSERT_TRUE(WriteLoginsJson(temp_dir.GetPath(), "https://firefox.com", "user", "pass"));

  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kKey3DbUnsupported);
  EXPECT_TRUE(logins.empty());
}

TEST(FirefoxPasswordParserTest, ParseKey4DbNotFound) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Neither key4.db nor key3.db exists
  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kKey4DbNotFound);
  EXPECT_TRUE(logins.empty());
}

TEST(FirefoxPasswordParserTest, ParseLoginsJsonNotFound) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Create key4.db
  base::FilePath key4_path = temp_dir.GetPath().Append("key4.db");
  ASSERT_TRUE(base::WriteFile(key4_path, "dummy key4.db"));

  // logins.json is missing
  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kLoginsJsonNotFound);
  EXPECT_TRUE(logins.empty());
}

TEST(FirefoxPasswordParserTest, ParseLoginsJsonParseFailed) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Write a valid key4.db
  std::vector<uint8_t> item1 = HexToBytes("02020202020202020202020202020202");
  std::vector<uint8_t> item2 = HexToBytes("3031301d060960864801650304012a0410202020202020202020202020202020200410e61d22c1d27cf3389300e888fa387927");
  std::vector<uint8_t> a11 = HexToBytes("3075304106092a864886f70d01050c3015041003030303030303030303030303030303020101301d060960864801650304012a0410040404040404040404040404040404040430336997d1ac27385fe7845455c65e04d6f99f862bdb0bc51762bdd6356e25613555b6aff2094fd501bda4a3a52e4f92d4");
  ASSERT_TRUE(WriteKey4Db(temp_dir.GetPath(), item1, item2, a11));

  // Write invalid JSON to logins.json
  base::FilePath logins_path = temp_dir.GetPath().Append("logins.json");
  ASSERT_TRUE(base::WriteFile(logins_path, "{invalid_json"));

  FirefoxPasswordStatus status;
  std::vector<ParsedLogin> logins = ParseFirefoxPasswords(temp_dir.GetPath(), &status);

  EXPECT_EQ(status, FirefoxPasswordStatus::kLoginsJsonParseFailed);
  EXPECT_TRUE(logins.empty());
}

#endif  // BUILDFLAG(IS_MAC)

}  // namespace
}  // namespace maho
