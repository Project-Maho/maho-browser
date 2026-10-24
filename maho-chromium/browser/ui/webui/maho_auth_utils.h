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

// Reports the account Sync key custody GET /sync/bootstrap found:
// ("client_held", room_id) when the relay holds no seed, ("escrow", room_id)
// otherwise. Login records it through RecordSyncKeyMode.
using SyncKeyModeCallback =
    base::RepeatingCallback<void(const std::string& key_mode,
                                 const std::string& room_id)>;

// Persists the key custody mode (and the client-held room id) so welcome and
// Settings know a client-held account must be joined with its recovery phrase.
void RecordSyncKeyMode(PrefService* prefs,
                       const std::string& key_mode,
                       const std::string& room_id);

// True once GET /sync/bootstrap reported key_mode == "client_held".
bool IsClientHeldSyncAccount(PrefService* prefs);

// For a client-held account, a recovery phrase must derive the account's
// recorded room; any other phrase would silently start a new, empty room.
// Returns an empty string when the join may proceed (escrow account, or the
// phrase matches), otherwise a user-facing error.
std::string CheckClientHeldRecoveryPhrase(PrefService* prefs,
                                          const std::string& recovery_phrase);

// After a successful recovery-phrase join, clears the retired-room banner.
void ClearRetiredSyncRoom(PrefService* prefs);

// Marks `room_id` retired (relay answered room_retired): the account moved to
// client-held keys on another device. Also records key_mode = client_held so
// recovery-phrase joins are validated (fail closed until the new room is
// known via RefreshSyncKeyModeFromRelay).
void MarkSyncRoomRetired(PrefService* prefs, const std::string& room_id);

// True when a retired room is recorded but the account's client-held room id
// is still unknown, i.e. GET /sync/bootstrap must be refetched before any
// recovery phrase can be accepted.
bool NeedsClientHeldRoomRefresh(PrefService* prefs);

// GETs /sync/bootstrap and, for a client-held account, records the mode and
// its room id. `callback` receives true when a client-held room was recorded.
using SyncKeyModeRefreshCallback = base::OnceCallback<void(bool recorded)>;
void RefreshSyncKeyModeFromRelay(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const std::string& access_token,
    SyncKeyModeRefreshCallback callback);

// POSTs /sync/bootstrap/client-held {room_id}: the relay drops its seed and
// Vault escrow and keeps only `room_id`. `error` is "room_retired" on 410.
using ClientHeldSwitchCallback = base::OnceCallback<
    void(bool ok, int http_status, const std::string& error)>;
void PostClientHeldSyncBootstrap(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const std::string& access_token,
    const std::string& room_id,
    ClientHeldSwitchCallback callback);

namespace testing {

// Enters the same create-when-missing bootstrap path used after a new account
// login, without requiring a relay account whose bootstrap can be cleared.
void FetchAccountSyncBootstrapForTesting(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const std::string& access_token,
    LoginResultCallback callback,
    SyncKeyModeCallback on_key_mode = SyncKeyModeCallback());

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
