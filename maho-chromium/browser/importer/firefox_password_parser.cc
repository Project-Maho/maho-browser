// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/firefox_password_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/importer/nss_decrypt_helper.h"
#include "url/gurl.h"

namespace maho {

std::vector<ParsedLogin> ParseFirefoxPasswords(
    const base::FilePath& profile_path,
    FirefoxPasswordStatus* out_status) {
  // Must be called on a thread pool with base::MayBlock(). File I/O and NSS
  // decryption are blocking operations that must not run on the UI thread.
  DCHECK(!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  std::vector<ParsedLogin> results;

  base::FilePath key4_path = profile_path.Append("key4.db");
  base::FilePath key3_path = profile_path.Append("key3.db");
  if (!base::PathExists(key4_path)) {
    if (base::PathExists(key3_path)) {
      *out_status = FirefoxPasswordStatus::kKey3DbUnsupported;
      LOG(WARNING) << "Firefox key3.db is not supported. Please upgrade Firefox to a version using key4.db.";
    } else {
      *out_status = FirefoxPasswordStatus::kKey4DbNotFound;
    }
    return results;
  }

  base::FilePath logins_path = profile_path.Append("logins.json");
  if (!base::PathExists(logins_path)) {
    *out_status = FirefoxPasswordStatus::kLoginsJsonNotFound;
    return results;
  }

  NssDecryptStatus nss_status = NssDecryptStatus::kSuccess;
  NssGlobalKey global_key = ExtractNssGlobalKey(key4_path, &nss_status);

  if (!global_key.valid) {
    if (nss_status == NssDecryptStatus::kMasterPasswordSet) {
      *out_status = FirefoxPasswordStatus::kMasterPasswordSet;
    } else {
      LOG(WARNING) << "NSS key extraction failed with status: "
                   << static_cast<int>(nss_status);
      *out_status = FirefoxPasswordStatus::kKeyDerivationFailed;
    }
    return results;
  }

  std::string logins_content;
  if (!base::ReadFileToString(logins_path, &logins_content)) {
    *out_status = FirefoxPasswordStatus::kLoginsJsonNotFound;
    return results;
  }

  auto parsed =
      base::JSONReader::Read(logins_content, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    *out_status = FirefoxPasswordStatus::kLoginsJsonParseFailed;
    return results;
  }

  const base::ListValue* logins = parsed->GetDict().FindList("logins");
  if (!logins) {
    *out_status = FirefoxPasswordStatus::kLoginsJsonParseFailed;
    return results;
  }

  for (const auto& entry : *logins) {
    if (!entry.is_dict()) {
      continue;
    }

    const base::DictValue& login_dict = entry.GetDict();

    const std::string* hostname = login_dict.FindString("hostname");
    const std::string* encrypted_username =
        login_dict.FindString("encryptedUsername");
    const std::string* encrypted_password =
        login_dict.FindString("encryptedPassword");

    if (!hostname || !encrypted_username || !encrypted_password) {
      continue;
    }

    NssDecryptStatus field_status = NssDecryptStatus::kSuccess;

    std::string username =
        NssDecryptField(*encrypted_username, global_key, &field_status);
    if (field_status != NssDecryptStatus::kSuccess) {
      LOG(WARNING) << "Skipping login: username decrypt failed (status "
                   << static_cast<int>(field_status) << ")";
      continue;
    }

    std::string password =
        NssDecryptField(*encrypted_password, global_key, &field_status);
    if (field_status != NssDecryptStatus::kSuccess) {
      LOG(WARNING) << "Skipping login: password decrypt failed (status "
                   << static_cast<int>(field_status) << ")";
      continue;
    }

    const std::string* form_submit_url =
        login_dict.FindString("formSubmitURL");

    ParsedLogin login;
    login.origin_url = GURL(*hostname);
    login.action_url =
        form_submit_url ? GURL(*form_submit_url) : GURL(*hostname);
    login.username = base::UTF8ToUTF16(username);
    login.password = base::UTF8ToUTF16(password);
    results.push_back(std::move(login));
  }

  if (results.empty()) {
    *out_status = FirefoxPasswordStatus::kNoLogins;
  } else {
    *out_status = FirefoxPasswordStatus::kSuccess;
  }

  return results;
}

}  // namespace maho
