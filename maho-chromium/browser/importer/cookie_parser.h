// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_COOKIE_PARSER_H_
#define MAHO_BROWSER_IMPORTER_COOKIE_PARSER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "net/cookies/canonical_cookie.h"

namespace maho {
class OSCryptKeyProvider;
}

namespace maho::importer {

struct ParsedCookie {
  ParsedCookie();
  ~ParsedCookie();
  ParsedCookie(ParsedCookie&&);
  ParsedCookie& operator=(ParsedCookie&&);

  std::string host_key;
  std::string name;
  std::string value;
  std::string path;
  base::Time expires;
  bool is_secure = false;
  bool is_httponly = false;
  // net::CookieSameSite mapping: -1=UNSPECIFIED, 0=NO_RESTRICTION, 1=LAX, 2=STRICT
  int samesite = -1;
  int source_scheme = 0;
};

std::vector<ParsedCookie> ParseChromiumCookies(
    const base::FilePath& cookies_db_path,
    const OSCryptKeyProvider* key_provider);

std::vector<ParsedCookie> ParseFirefoxCookies(
    const base::FilePath& cookies_db_path);

}  // namespace maho::importer

#endif  // MAHO_BROWSER_IMPORTER_COOKIE_PARSER_H_
