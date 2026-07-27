// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_MAHO_URL_SCHEME_H_
#define MAHO_CHROMIUM_BROWSER_MAHO_URL_SCHEME_H_

#include "url/gurl.h"

namespace maho {

// Returns true if `url` is a valid `maho://` public alias URL.
// Requirements:
// 1. `url.is_valid()` must be true.
// 2. `url.scheme_piece()` must equal "maho".
// 3. `url.has_username()` and `url.has_password()` must be false.
// 4. `url.has_port()` must be false.
// 5. The canonical parsed host must exactly match one of the 9 approved public alias hosts.
//    (Trailing dots, lookalike hosts, empty authority, unlisted hosts, or encodings
//    that parse to a different host are rejected).
bool IsValidMahoUrlAlias(const GURL& url);

// Maps a valid `maho://<alias-host>/path?query#fragment` public alias URL to its
// corresponding `chrome://maho-<actual-host>/path?query#fragment` destination.
// Returns true and writes the result to `*out_actual_url` if `url` is a valid alias.
// Returns false and leaves `*out_actual_url` unchanged if `url` is invalid.
bool MapMahoUrlAliasToActualUrl(const GURL& url, GURL* out_actual_url);

// Resolves an approved `chrome://maho-<actual-host>/path?query#fragment` actual URL
// backward to its `maho://<alias-host>/path?query#fragment` public alias URL.
// Returns true and writes the result to `*out_alias_url` if `url` is an approved actual WebUI URL.
// Returns false and leaves `*out_alias_url` unchanged if `url` is not an approved actual WebUI destination.
bool ResolveActualUrlToMahoAlias(const GURL& url, GURL* out_alias_url);

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_MAHO_URL_SCHEME_H_
