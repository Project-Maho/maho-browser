// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/cookie_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "maho/browser/importer/chromium_password_parser.h"
#include "maho/browser/importer/oscrypt_key_provider.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace maho::importer {

ParsedCookie::ParsedCookie() = default;
ParsedCookie::~ParsedCookie() = default;
ParsedCookie::ParsedCookie(ParsedCookie&&) = default;
ParsedCookie& ParsedCookie::operator=(ParsedCookie&&) = default;

namespace {

// Chromium stores time as microseconds since Windows epoch (1601-01-01).
base::Time ChromiumTimeToBaseTime(int64_t chromium_time) {
  if (chromium_time == 0) {
    return base::Time();
  }
  return base::Time::FromInternalValue(chromium_time);
}

// Firefox stores time as seconds since Unix epoch.
base::Time FirefoxTimeToBaseTime(int64_t firefox_time) {
  if (firefox_time == 0) {
    return base::Time();
  }
  return base::Time::UnixEpoch() + base::Seconds(firefox_time);
}

}  // namespace

std::vector<ParsedCookie> ParseChromiumCookies(
    const base::FilePath& cookies_db_path,
    const OSCryptKeyProvider* key_provider) {
  std::vector<ParsedCookie> result;

  if (!base::PathExists(cookies_db_path)) {
    return result;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return result;
  }

  base::FilePath temp_db_path = temp_dir.GetPath().AppendASCII("Cookies");
  if (!base::CopyFile(cookies_db_path, temp_db_path)) {
    return result;
  }

  sql::Database db("MahoImporterChromiumCookies");
  if (!db.Open(temp_db_path)) {
    return result;
  }

  if (!db.DoesTableExist("cookies")) {
    return result;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "SELECT host_key, name, value, encrypted_value, path, "
      "expires_utc, is_secure, is_httponly, samesite, source_scheme "
      "FROM cookies WHERE expires_utc > 0"));
  if (!stmt.is_valid()) {
    return result;
  }

  while (stmt.Step()) {
    ParsedCookie cookie;
    cookie.host_key = stmt.ColumnString(0);
    cookie.name = stmt.ColumnString(1);
    cookie.path = stmt.ColumnString(4);
    cookie.expires = ChromiumTimeToBaseTime(stmt.ColumnInt64(5));
    cookie.is_secure = stmt.ColumnBool(6);
    cookie.is_httponly = stmt.ColumnBool(7);
    cookie.samesite = stmt.ColumnInt(8);
    cookie.source_scheme = stmt.ColumnInt(9);

    std::vector<uint8_t> encrypted;
    if (stmt.GetColumnType(3) != sql::ColumnType::kNull) {
      encrypted = stmt.ColumnBlobAsVector(3);
    }

    if (!encrypted.empty()) {
      // Check prefix (v10/v20) per INV-5
      if (encrypted.size() >= 3) {
        if (encrypted[0] == 'v' && encrypted[1] == '1' && encrypted[2] == '0') {
          if (key_provider && key_provider->IsValid()) {
            std::string decrypted = DecryptChromiumPassword(encrypted, key_provider->GetKey().data());
            if (decrypted.empty()) {
              LOG(WARNING) << "Cookie decryption failed, skipping row.";
              continue;
            }
            cookie.value = std::move(decrypted);
          } else {
            LOG(WARNING) << "Missing or invalid key provider for cookie decryption, skipping row.";
            continue;
          }
        } else if (encrypted[0] == 'v' && encrypted[1] == '2' && encrypted[2] == '0') {
          LOG(WARNING) << "App-Bound encrypted cookie (v20) detected, skipping row.";
          continue;
        } else {
          LOG(WARNING) << "Unknown cookie encryption prefix, skipping row.";
          continue;
        }
      } else {
        LOG(WARNING) << "Encrypted blob too small, skipping row.";
        continue;
      }
    } else {
      cookie.value = (stmt.GetColumnType(2) != sql::ColumnType::kNull) ? stmt.ColumnString(2) : "";
    }

    result.push_back(std::move(cookie));
  }

  return result;
}

std::vector<ParsedCookie> ParseFirefoxCookies(
    const base::FilePath& cookies_db_path) {
  std::vector<ParsedCookie> result;

  if (!base::PathExists(cookies_db_path)) {
    return result;
  }

  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    return result;
  }

  base::FilePath temp_db_path = temp_dir.GetPath().AppendASCII("cookies.sqlite");
  if (!base::CopyFile(cookies_db_path, temp_db_path)) {
    return result;
  }

  sql::Database db("MahoImporterFirefoxCookies");
  if (!db.Open(temp_db_path)) {
    return result;
  }

  if (!db.DoesTableExist("moz_cookies")) {
    return result;
  }

  sql::Statement stmt(db.GetUniqueStatement(
      "SELECT host, name, value, path, expiry, isSecure, isHttpOnly, sameSite "
      "FROM moz_cookies WHERE expiry > 0"));
  if (!stmt.is_valid()) {
    return result;
  }

  while (stmt.Step()) {
    ParsedCookie cookie;
    cookie.host_key = stmt.ColumnString(0);
    cookie.name = stmt.ColumnString(1);
    cookie.value = (stmt.GetColumnType(2) != sql::ColumnType::kNull) ? stmt.ColumnString(2) : "";
    cookie.path = stmt.ColumnString(3);
    cookie.expires = FirefoxTimeToBaseTime(stmt.ColumnInt64(4));
    cookie.is_secure = stmt.ColumnBool(5);
    cookie.is_httponly = stmt.ColumnBool(6);
    cookie.samesite = stmt.ColumnInt(7);
    result.push_back(std::move(cookie));
  }

  return result;
}

}  // namespace maho::importer
