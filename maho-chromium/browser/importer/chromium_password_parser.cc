// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/importer/chromium_password_parser.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/importer/oscrypt_key_provider.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "content/public/browser/browser_thread.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include <CommonCrypto/CommonCryptor.h>
#endif

namespace maho {

ParsedLogin::ParsedLogin() = default;
ParsedLogin::~ParsedLogin() = default;
ParsedLogin::ParsedLogin(const ParsedLogin&) = default;
ParsedLogin& ParsedLogin::operator=(const ParsedLogin&) = default;
ParsedLogin::ParsedLogin(ParsedLogin&&) = default;
ParsedLogin& ParsedLogin::operator=(ParsedLogin&&) = default;

namespace {

constexpr size_t kAes128KeyLength = 16;
constexpr size_t kV10PrefixLength = 3;
constexpr size_t kIvLength = 16;

// AES-128-CBC IV: 16 space characters (0x20).
constexpr uint8_t kOsCryptIv[kIvLength] = {
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};

struct KeychainEntry {
  const char* display_name;
  ChromiumKeychainInfo info;
};

constexpr KeychainEntry kKeychainEntries[] = {
    {"Google Chrome", {"Chrome Safe Storage", "Chrome"}},
    {"Brave", {"Brave Safe Storage", "Brave"}},
    {"Arc", {"Arc Safe Storage", "Arc"}},
    {"Microsoft Edge", {"Microsoft Edge Safe Storage", "Microsoft Edge"}},
    {"Vivaldi", {"Vivaldi Safe Storage", "Vivaldi"}},
    {"Opera", {"Opera Safe Storage", "Opera"}},
};

}  // namespace

const ChromiumKeychainInfo* GetKeychainInfoForBrowser(
    const std::string& browser_display_name) {
  for (const auto& entry : kKeychainEntries) {
    if (browser_display_name == entry.display_name) {
      return &entry.info;
    }
  }
  return nullptr;
}

std::string DecryptChromiumPassword(const std::vector<uint8_t>& ciphertext,
                                    const uint8_t* key) {
  if (ciphertext.size() <= kV10PrefixLength) {
    return std::string();
  }

  if (ciphertext[0] != 'v' || ciphertext[1] != '1' || ciphertext[2] != '0') {
    LOG(WARNING) << "Unsupported password encryption prefix: "
                 << static_cast<char>(ciphertext[0])
                 << static_cast<char>(ciphertext[1])
                 << static_cast<char>(ciphertext[2]);
    return std::string();
  }

  auto encrypted_span = base::span(ciphertext).subspan(kV10PrefixLength);
  const uint8_t* encrypted_data = encrypted_span.data();
  size_t encrypted_len = encrypted_span.size();

  if (encrypted_len == 0 || encrypted_len % kAes128KeyLength != 0) {
    LOG(WARNING) << "Invalid ciphertext length for AES-128-CBC: "
                 << encrypted_len;
    return std::string();
  }

#if BUILDFLAG(IS_MAC)
  size_t out_len = 0;
  std::vector<uint8_t> plaintext(encrypted_len + kCCBlockSizeAES128);
  SecureBuffer zero_plaintext(plaintext.data(), plaintext.size());

  CCCryptorStatus status = CCCrypt(
      kCCDecrypt, kCCAlgorithmAES128, kCCOptionPKCS7Padding, key,
      kAes128KeyLength, kOsCryptIv, encrypted_data, encrypted_len,
      plaintext.data(), plaintext.size(), &out_len);

  if (status != kCCSuccess) {
    LOG(WARNING) << "AES-128-CBC decrypt failed with status: " << status;
    return std::string();
  }

  if (out_len >= encrypted_len) {
    LOG(WARNING) << "AES-128-CBC decrypt: invalid PKCS7 padding";
    return std::string();
  }

  return std::string(reinterpret_cast<const char*>(plaintext.data()), out_len);
#else
  LOG(WARNING) << "Password decryption not supported on this platform";
  return std::string();
#endif
}

std::vector<ParsedLogin> ParseChromiumPasswords(
    const base::FilePath& profile_path,
    const OSCryptKeyProvider* key_provider,
    PasswordParseStatus* out_status) {
  DCHECK(!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  std::vector<ParsedLogin> results;

  if (!key_provider || !key_provider->IsValid()) {
    *out_status = PasswordParseStatus::kKeyDerivationFailed;
    return results;
  }

  const std::vector<uint8_t>& key = key_provider->GetKey();
  if (key.size() < kAes128KeyLength) {
    *out_status = PasswordParseStatus::kKeyDerivationFailed;
    return results;
  }

  // Copy Login Data to temp to avoid SQLite locking issues.
  base::FilePath login_data_path = profile_path.Append("Login Data");
  if (!base::PathExists(login_data_path)) {
    *out_status = PasswordParseStatus::kDatabaseOpenFailed;
    return results;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    *out_status = PasswordParseStatus::kDatabaseOpenFailed;
    return results;
  }

  base::FilePath temp_db_path = temp_dir.GetPath().AppendASCII("Login Data");
  if (!base::CopyFile(login_data_path, temp_db_path)) {
    *out_status = PasswordParseStatus::kDatabaseOpenFailed;
    return results;
  }

  sql::Database db("MahoImporterChromiumPasswords");
  if (!db.Open(temp_db_path)) {
    *out_status = PasswordParseStatus::kDatabaseOpenFailed;
    return results;
  }

  if (!db.DoesTableExist("logins")) {
    *out_status = PasswordParseStatus::kDatabaseOpenFailed;
    return results;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "SELECT origin_url, action_url, username_value, password_value "
      "FROM logins"));

  if (!stmt.is_valid()) {
    *out_status = PasswordParseStatus::kDatabaseOpenFailed;
    return results;
  }

  while (stmt.Step()) {
    std::string origin = stmt.ColumnString(0);
    std::string action = stmt.ColumnString(1);
    std::u16string username = stmt.ColumnString16(2);

    base::span<const uint8_t> blob = stmt.ColumnBlob(3);

    if (blob.empty()) {
      continue;
    }

    std::vector<uint8_t> ciphertext(blob.begin(), blob.end());
    std::string decrypted = DecryptChromiumPassword(ciphertext, key.data());

    if (decrypted.empty()) {
      LOG(WARNING) << "Skipping row: decrypt failed";
      continue;
    }

    ParsedLogin login;
    login.origin_url = GURL(origin);
    login.action_url = GURL(action);
    login.username = std::move(username);
    login.password = base::UTF8ToUTF16(decrypted);
    results.push_back(std::move(login));
  }

  if (results.empty()) {
    *out_status = PasswordParseStatus::kNoLoginRows;
  } else {
    *out_status = PasswordParseStatus::kSuccess;
  }

  return results;
}

}  // namespace maho
