// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/chromium_password_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "maho/browser/importer/oscrypt_key_provider.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

// Known AES-128-CBC test vector:
// Key: 16 bytes all 0x42 ("BBBBBBBBBBBBBBBB")
// IV: 16 spaces (0x20)
// Plaintext: "hunter2" (7 bytes, padded to 16 with PKCS7 = 9 bytes of 0x09)
// Ciphertext (v10 prefixed): "v10" + encrypted_block
//
// Generated via:
//   echo -n "hunter2" | openssl enc -aes-128-cbc -K 42424242424242424242424242424242
//     -iv 20202020202020202020202020202020 -nopad (with manual PKCS7)
//
// The encrypted block for "hunter2\x09\x09\x09\x09\x09\x09\x09\x09\x09":
constexpr uint8_t kTestKey[16] = {
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
};

// v10 + AES-128-CBC(key=0x42*16, iv=0x20*16, plaintext="hunter2" with PKCS7)
// Pre-computed ciphertext bytes:
constexpr uint8_t kTestCiphertext[] = {
    'v',  '1',  '0',
    0x9c, 0x31, 0x37, 0xcc, 0x50, 0xf8, 0xd5, 0x93,
    0x0e, 0xe3, 0x7a, 0x1e, 0x63, 0x0c, 0xce, 0x75,
};

class TestOSCryptKeyProvider : public OSCryptKeyProvider {
 public:
  explicit TestOSCryptKeyProvider(std::vector<uint8_t> key);
  ~TestOSCryptKeyProvider() override;
  const std::vector<uint8_t>& GetKey() const override;
  bool IsValid() const override;
 private:
  std::vector<uint8_t> key_;
};

TestOSCryptKeyProvider::TestOSCryptKeyProvider(std::vector<uint8_t> key)
    : key_(std::move(key)) {}
TestOSCryptKeyProvider::~TestOSCryptKeyProvider() = default;
const std::vector<uint8_t>& TestOSCryptKeyProvider::GetKey() const {
  return key_;
}
bool TestOSCryptKeyProvider::IsValid() const {
  return !key_.empty();
}

TEST(ChromiumPasswordParserTest, DecryptV10EmptyCiphertext) {
  std::vector<uint8_t> empty;
  std::string result = DecryptChromiumPassword(empty, kTestKey);
  EXPECT_TRUE(result.empty());
}

TEST(ChromiumPasswordParserTest, DecryptV10TooShort) {
  std::vector<uint8_t> short_blob = {'v', '1', '0'};
  std::string result = DecryptChromiumPassword(short_blob, kTestKey);
  EXPECT_TRUE(result.empty());
}

TEST(ChromiumPasswordParserTest, DecryptV10WrongPrefix) {
  std::vector<uint8_t> bad_prefix = {'v', '2', '0', 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00};
  std::string result = DecryptChromiumPassword(bad_prefix, kTestKey);
  EXPECT_TRUE(result.empty());
}

TEST(ChromiumPasswordParserTest, DecryptV10InvalidLength) {
  std::vector<uint8_t> bad_len = {'v', '1', '0', 0x01, 0x02, 0x03, 0x04,
                                   0x05, 0x06, 0x07};
  std::string result = DecryptChromiumPassword(bad_len, kTestKey);
  EXPECT_TRUE(result.empty());
}

TEST(ChromiumPasswordParserTest, DecryptV10KnownVector) {
  std::vector<uint8_t> ciphertext(kTestCiphertext,
                                   kTestCiphertext + sizeof(kTestCiphertext));
  std::string result = DecryptChromiumPassword(ciphertext, kTestKey);
  EXPECT_EQ(result, "hunter2");
}

TEST(ChromiumPasswordParserTest, GetKeychainInfoChrome) {
  const auto* info = GetKeychainInfoForBrowser("Google Chrome");
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->service_name, "Chrome Safe Storage");
  EXPECT_STREQ(info->account_name, "Chrome");
}

TEST(ChromiumPasswordParserTest, GetKeychainInfoBrave) {
  const auto* info = GetKeychainInfoForBrowser("Brave");
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->service_name, "Brave Safe Storage");
  EXPECT_STREQ(info->account_name, "Brave");
}

TEST(ChromiumPasswordParserTest, GetKeychainInfoEdge) {
  const auto* info = GetKeychainInfoForBrowser("Microsoft Edge");
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->service_name, "Microsoft Edge Safe Storage");
}

TEST(ChromiumPasswordParserTest, GetKeychainInfoArc) {
  const auto* info = GetKeychainInfoForBrowser("Arc");
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->service_name, "Arc Safe Storage");
}

TEST(ChromiumPasswordParserTest, GetKeychainInfoUnknownBrowser) {
  const auto* info = GetKeychainInfoForBrowser("FooBar Browser");
  EXPECT_EQ(info, nullptr);
}

// Helper to create test Login Data database.
bool CreateTestLoginsDb(const base::FilePath& db_path,
                        const std::vector<std::vector<uint8_t>>& password_values) {
  sql::Database db("Passwords");
  if (!db.Open(db_path)) {
    return false;
  }
  if (!db.Execute("CREATE TABLE logins ("
                  "origin_url TEXT NOT NULL,"
                  "action_url TEXT NOT NULL,"
                  "username_value TEXT NOT NULL,"
                  "password_value BLOB NOT NULL)")) {
    return false;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "INSERT INTO logins (origin_url, action_url, username_value, password_value) "
      "VALUES (?, ?, ?, ?)"));

  for (size_t i = 0; i < password_values.size(); i++) {
    stmt.Reset(true);
    stmt.BindString(0, "https://example.com");
    stmt.BindString(1, "https://example.com/login");
    stmt.BindString(2, "user_" + std::to_string(i));
    stmt.BindBlob(3, password_values[i]);
    if (!stmt.Run()) {
      return false;
    }
  }
  return true;
}

TEST(ChromiumPasswordParserTest, ParseV10Valid) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Login Data");

  std::vector<uint8_t> v10_blob(kTestCiphertext, kTestCiphertext + sizeof(kTestCiphertext));
  ASSERT_TRUE(CreateTestLoginsDb(db_path, {v10_blob}));

  std::vector<uint8_t> key_bytes(kTestKey, kTestKey + 16);
  TestOSCryptKeyProvider key_provider(key_bytes);

  PasswordParseStatus status;
  std::vector<ParsedLogin> logins = ParseChromiumPasswords(temp_dir.GetPath(), &key_provider, &status);

  EXPECT_EQ(status, PasswordParseStatus::kSuccess);
  ASSERT_EQ(logins.size(), 1u);
  EXPECT_EQ(logins[0].origin_url, GURL("https://example.com"));
  EXPECT_EQ(logins[0].username, u"user_0");
  EXPECT_EQ(logins[0].password, u"hunter2");
}

TEST(ChromiumPasswordParserTest, ParseWrongKeyError) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Login Data");

  std::vector<uint8_t> v10_blob(kTestCiphertext, kTestCiphertext + sizeof(kTestCiphertext));
  ASSERT_TRUE(CreateTestLoginsDb(db_path, {v10_blob}));

  std::vector<uint8_t> wrong_key(16, 0x43);
  TestOSCryptKeyProvider key_provider(wrong_key);

  PasswordParseStatus status;
  std::vector<ParsedLogin> logins = ParseChromiumPasswords(temp_dir.GetPath(), &key_provider, &status);

  EXPECT_EQ(status, PasswordParseStatus::kNoLoginRows);
  EXPECT_EQ(logins.size(), 0u);
}

TEST(ChromiumPasswordParserTest, ParseEmptyDbReturnsEmptyVec) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Login Data");

  ASSERT_TRUE(CreateTestLoginsDb(db_path, {}));

  std::vector<uint8_t> key_bytes(kTestKey, kTestKey + 16);
  TestOSCryptKeyProvider key_provider(key_bytes);

  PasswordParseStatus status;
  std::vector<ParsedLogin> logins = ParseChromiumPasswords(temp_dir.GetPath(), &key_provider, &status);

  EXPECT_EQ(status, PasswordParseStatus::kNoLoginRows);
  EXPECT_EQ(logins.size(), 0u);
}

}  // namespace
}  // namespace maho
