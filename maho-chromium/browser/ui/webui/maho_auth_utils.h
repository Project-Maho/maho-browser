// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AUTH_UTILS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AUTH_UTILS_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"

class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace maho::auth {

// Returns decrypted access token, or empty string if not stored or decrypt
// failed. Synchronous — only safe to call after the Encryptor is ready.
std::string GetRelayAccessToken(PrefService* prefs,
                                const os_crypt_async::Encryptor& encryptor);

// Returns decrypted refresh token, or empty string if not stored.
std::string GetRelayRefreshToken(PrefService* prefs,
                                 const os_crypt_async::Encryptor& encryptor);

// Encrypt + store both tokens + their expiry timestamps + user info.
// Returns true on success. Pass 0 for expiry timestamps if unknown.
struct StoreTokensParams {
  StoreTokensParams();
  ~StoreTokensParams();
  StoreTokensParams(const StoreTokensParams&);
  StoreTokensParams& operator=(const StoreTokensParams&);
  StoreTokensParams(StoreTokensParams&&) noexcept;
  StoreTokensParams& operator=(StoreTokensParams&&) noexcept;

  std::string access_token;
  std::string refresh_token;
  int64_t access_expires_at = 0;
  int64_t refresh_expires_at = 0;
  std::string user_email;
  std::string user_id;
  std::string user_display_name;
  std::string user_tier;
};
bool StoreRelayTokens(PrefService* prefs,
                      const os_crypt_async::Encryptor& encryptor,
                      const StoreTokensParams& params);

// Clear all auth-related prefs (tokens + expiry + user info).
void ClearRelayTokens(PrefService* prefs);

// Returns the relay base URL (MAHO_RELAY_URL env var override, otherwise
// default "https://relay.maho.co").
std::string GetRelayBaseUrl();

// POSTs /auth/refresh with the supplied (already-decrypted) refresh token.
// Callers must re-encrypt and persist the returned tokens via StoreRelayTokens.
// Split this way because os_crypt_async::Encryptor::Clone is protected and
// cannot cross an async callback boundary.
struct RefreshedTokens {
  std::string access_token;
  std::string refresh_token;
  int64_t access_expires_at = 0;
  int64_t refresh_expires_at = 0;
};
using RefreshCallback =
    base::OnceCallback<void(bool success,
                            int http_status,
                            std::optional<RefreshedTokens> tokens)>;
void RefreshAccessToken(
    const std::string& current_refresh_token,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    RefreshCallback callback);

}  // namespace maho::auth

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AUTH_UTILS_H_
