// Copyright 2026 Maho Browser. All rights reserved.

#pragma allow_unsafe_buffers

#include "maho/browser/importer/cookie_parser.h"

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

namespace maho::importer {
namespace {

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
  TestOSCryptKeyProvider();
  ~TestOSCryptKeyProvider() override;
  const std::vector<uint8_t>& GetKey() const override;
  bool IsValid() const override;
 private:
  std::vector<uint8_t> key_;
};

TestOSCryptKeyProvider::TestOSCryptKeyProvider()
    : key_(kTestKey, kTestKey + 16) {}
TestOSCryptKeyProvider::~TestOSCryptKeyProvider() = default;
const std::vector<uint8_t>& TestOSCryptKeyProvider::GetKey() const {
  return key_;
}
bool TestOSCryptKeyProvider::IsValid() const {
  return true;
}

bool CreateTestCookiesDb(const base::FilePath& db_path,
                         const std::vector<std::vector<uint8_t>>& encrypted_values,
                         const std::vector<std::string>& plaintext_values,
                         bool insert_null_encrypted = false) {
  sql::Database db("Cookie");
  if (!db.Open(db_path)) {
    return false;
  }
  if (!db.Execute("CREATE TABLE cookies ("
                  "host_key TEXT NOT NULL,"
                  "name TEXT NOT NULL,"
                  "value TEXT NOT NULL,"
                  "encrypted_value BLOB,"
                  "path TEXT NOT NULL,"
                  "expires_utc INTEGER NOT NULL,"
                  "is_secure INTEGER NOT NULL,"
                  "is_httponly INTEGER NOT NULL,"
                  "samesite INTEGER NOT NULL,"
                  "source_scheme INTEGER NOT NULL)")) {
    return false;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "INSERT INTO cookies (host_key, name, value, encrypted_value, path, "
      "expires_utc, is_secure, is_httponly, samesite, source_scheme) "
      "VALUES (?, ?, ?, ?, ?, 100000, 0, 0, 0, 0)"));

  for (size_t i = 0; i < encrypted_values.size(); i++) {
    stmt.Reset(true);
    stmt.BindString(0, "example.com");
    stmt.BindString(1, "cookie_" + std::to_string(i));
    stmt.BindString(2, plaintext_values[i]);
    if (insert_null_encrypted && i == encrypted_values.size() - 1) {
      stmt.BindNull(3);
    } else {
      stmt.BindBlob(3, encrypted_values[i]);
    }
    stmt.BindString(4, "/");
    if (!stmt.Run()) {
      return false;
    }
  }
  return true;
}

TEST(CookieParserTest, DecryptV10Valid) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Cookies");

  std::vector<uint8_t> v10_blob(kTestCiphertext, kTestCiphertext + sizeof(kTestCiphertext));
  ASSERT_TRUE(CreateTestCookiesDb(db_path, {v10_blob}, {""}));

  TestOSCryptKeyProvider key_provider;
  std::vector<ParsedCookie> cookies = ParseChromiumCookies(db_path, &key_provider);

  ASSERT_EQ(cookies.size(), 1u);
  EXPECT_EQ(cookies[0].host_key, "example.com");
  EXPECT_EQ(cookies[0].name, "cookie_0");
  EXPECT_EQ(cookies[0].value, "hunter2");
}

TEST(CookieParserTest, SkipV20WithWarning) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Cookies");

  std::vector<uint8_t> v20_blob = {'v', '2', '0', 0x01, 0x02, 0x03, 0x04};
  ASSERT_TRUE(CreateTestCookiesDb(db_path, {v20_blob}, {""}));

  TestOSCryptKeyProvider key_provider;
  std::vector<ParsedCookie> cookies = ParseChromiumCookies(db_path, &key_provider);

  // v20 App-Bound cookie should be skipped, returning 0 cookies.
  EXPECT_EQ(cookies.size(), 0u);
}

TEST(CookieParserTest, CorruptBlobError) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Cookies");

  std::vector<uint8_t> corrupt_blob = {'v', '1', '0', 0x01, 0x02, 0x03};
  ASSERT_TRUE(CreateTestCookiesDb(db_path, {corrupt_blob}, {""}));

  TestOSCryptKeyProvider key_provider;
  std::vector<ParsedCookie> cookies = ParseChromiumCookies(db_path, &key_provider);

  // Decryption of corrupt blob should fail, returning empty result (error).
  EXPECT_EQ(cookies.size(), 0u);
}

TEST(CookieParserTest, NullColumnHandling) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath db_path = temp_dir.GetPath().AppendASCII("Cookies");

  // Create two cookies:
  // 1. Plaintext value "plain_val", encrypted_value is NULL.
  // 2. Plaintext value is NULL, encrypted_value is NULL.
  std::vector<std::vector<uint8_t>> encrypted = {{}, {}};
  std::vector<std::string> plaintext = {"plain_val", ""};
  ASSERT_TRUE(CreateTestCookiesDb(db_path, encrypted, plaintext, /*insert_null_encrypted=*/true));

  TestOSCryptKeyProvider key_provider;
  std::vector<ParsedCookie> cookies = ParseChromiumCookies(db_path, &key_provider);

  ASSERT_EQ(cookies.size(), 2u);
  EXPECT_EQ(cookies[0].value, "plain_val");
  EXPECT_EQ(cookies[1].value, "");
}

}  // namespace
}  // namespace maho::importer
