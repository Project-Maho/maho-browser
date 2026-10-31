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

// Returns the sync relay base URL. Honors MAHO_SYNC_RELAY_URL, then falls back
// to GetRelayBaseUrl() (MAHO_RELAY_URL, else the default relay host) so local
// dev relays are reachable for sync, matching the auth path.
std::string GetSyncRelayUrl();

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
  std::string oauth_provider;
  std::string oauth_provider_sub;
};
bool StoreRelayTokens(PrefService* prefs,
                      const os_crypt_async::Encryptor& encryptor,
                      const StoreTokensParams& params);

// Copies the currently persisted optional identity pair into token-refresh
// parameters so rotating relay tokens does not unlink the signed-in account.
void PreserveRelayIdentityMetadata(PrefService* prefs,
                                   StoreTokensParams* params);

// Clear all auth-related prefs (tokens + expiry + user info).
void ClearRelayTokens(PrefService* prefs);

// Returns Mail OAuth identity-binding options for a linked Google relay
// session, or an empty JSON object for password/non-Google/invalid metadata.
// Values are read from plaintext identity prefs; relay tokens are never
// included.
std::string BuildMailOAuthStartOptionsJson(PrefService* prefs);

// Returns true if there is a valid relay session (an unexpired access token
// or a valid refresh token to obtain one).
bool HasValidRelaySession(PrefService* prefs);

// Returns the relay base URL (MAHO_RELAY_URL env var override, otherwise
// default "https://relay.mahobrowser.com").
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
// Persists a /auth/refresh result while keeping the stored user identity. The
// relay rotates the refresh token on every refresh and invalidates the one it
// consumed, so every refresh caller must persist the result or the profile is
// left holding a dead refresh token.
StoreTokensParams BuildRefreshedRelayTokenParams(PrefService* prefs,
                                                const RefreshedTokens& tokens);
bool StoreRefreshedRelayTokens(PrefService* prefs,
                               const os_crypt_async::Encryptor& encryptor,
                               const RefreshedTokens& tokens);
void RefreshAccessToken(
    const std::string& current_refresh_token,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    RefreshCallback callback);

using LoginResultCallback =
    base::OnceCallback<void(bool ok, const std::string& error_message)>;

namespace testing {

// Enters the same create-when-missing bootstrap path used after a new account
// login, without requiring a relay account whose bootstrap can be cleared.
void FetchAccountSyncBootstrapForTesting(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const std::string& access_token,
    LoginResultCallback callback);

}  // namespace testing

void MahoRelayLogin(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor& encryptor,
    const std::string& email,
    const std::string& password,
    LoginResultCallback callback);

using SignupResultCallback = LoginResultCallback;

// Exchanges a Google id_token (obtained by the maho-core PKCE flow) for a relay
// session, storing the returned tokens exactly as MahoRelayLogin does.
void MahoRelayGoogleLogin(
    base::RepeatingCallback<PrefService*()> prefs_provider,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor& encryptor,
    const std::string& id_token,
    const std::string& nonce,
    LoginResultCallback callback);

void MahoRelaySignup(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor& encryptor,
    const std::string& email,
    const std::string& password,
    const std::string& display_name,
    SignupResultCallback callback);

}  // namespace maho::auth

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AUTH_UTILS_H_
