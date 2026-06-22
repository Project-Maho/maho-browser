// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_FIREFOX_PASSWORD_PARSER_H_
#define MAHO_BROWSER_IMPORTER_FIREFOX_PASSWORD_PARSER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "maho/browser/importer/chromium_password_parser.h"
#include "maho/browser/importer/nss_decrypt_helper.h"

namespace maho {

enum class FirefoxPasswordStatus {
  kSuccess = 0,
  kLoginsJsonNotFound = 1,
  kLoginsJsonParseFailed = 2,
  kKey4DbNotFound = 3,
  kMasterPasswordSet = 4,
  kKeyDerivationFailed = 5,
  kNoLogins = 6,
  kKey3DbUnsupported = 7,
};

// Parses Firefox logins.json and decrypts all passwords using key4.db.
// |profile_path| is the Firefox profile directory containing key4.db and
// logins.json.
// MUST be called on a thread pool with base::MayBlock().
std::vector<ParsedLogin> ParseFirefoxPasswords(
    const base::FilePath& profile_path,
    FirefoxPasswordStatus* out_status);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_FIREFOX_PASSWORD_PARSER_H_
