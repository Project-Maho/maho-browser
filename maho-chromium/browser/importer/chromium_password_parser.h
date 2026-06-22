// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_CHROMIUM_PASSWORD_PARSER_H_
#define MAHO_BROWSER_IMPORTER_CHROMIUM_PASSWORD_PARSER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "url/gurl.h"

namespace maho {

class OSCryptKeyProvider;

struct ParsedLogin {
  ParsedLogin();
  ~ParsedLogin();
  ParsedLogin(const ParsedLogin&);
  ParsedLogin& operator=(const ParsedLogin&);
  ParsedLogin(ParsedLogin&&);
  ParsedLogin& operator=(ParsedLogin&&);

  GURL origin_url;
  GURL action_url;
  std::u16string username;
  std::u16string password;
};

enum class PasswordParseStatus {
  kSuccess = 0,
  kDatabaseOpenFailed = 1,
  kKeychainAccessDenied = 2,
  kKeyDerivationFailed = 3,
  kNoLoginRows = 4,
};

// Per-browser Keychain service names for the OSCrypt safe-storage key.
struct ChromiumKeychainInfo {
  const char* service_name;   // e.g. "Chrome Safe Storage"
  const char* account_name;   // e.g. "Chrome"
};

// Returns Keychain info for the given browser type.
// Returns nullptr if the browser type is not a Chromium-based browser.
const ChromiumKeychainInfo* GetKeychainInfoForBrowser(
    const std::string& browser_display_name);

// Decrypts a single Chromium v10-prefixed password_value blob.
// |key| must be 16 bytes (AES-128 key derived from PBKDF2).
// Returns empty string on any decryption error.
std::string DecryptChromiumPassword(const std::vector<uint8_t>& ciphertext,
                                    const uint8_t* key);

// |key_provider| is used to retrieve the derived OSCrypt key.
// |out_status| receives the parse status.
// MUST be called on a thread pool with base::MayBlock(). File I/O and SQLite
// operations are blocking and must not run on the UI thread.
std::vector<ParsedLogin> ParseChromiumPasswords(
    const base::FilePath& profile_path,
    const OSCryptKeyProvider* key_provider,
    PasswordParseStatus* out_status);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_CHROMIUM_PASSWORD_PARSER_H_
