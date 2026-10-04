// Copyright 2026 Maho Browser. All rights reserved.
// Use of this source code is governed by the same license as the chromium-src
// directory.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_ACCOUNT_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_ACCOUNT_PREFS_H_

namespace maho::account_prefs {

// Encrypted relay auth tokens (OSCrypt + base64).
inline constexpr char kRelayAccessTokenEncryptedB64[] =
    "maho.auth.relay.access_token.encrypted_b64";
inline constexpr char kRelayRefreshTokenEncryptedB64[] =
    "maho.auth.relay.refresh_token.encrypted_b64";

// Token expiry timestamps (unix seconds, not encrypted).
inline constexpr char kRelayAccessTokenExpiresAt[] =
    "maho.auth.relay.access_token.expires_at";
inline constexpr char kRelayRefreshTokenExpiresAt[] =
    "maho.auth.relay.refresh_token.expires_at";

// User identity (plaintext, displayed in UI).
inline constexpr char kRelayUserEmail[] = "maho.auth.relay.user.email";
inline constexpr char kRelayUserId[] = "maho.auth.relay.user.id";
inline constexpr char kRelayUserDisplayName[] =
    "maho.auth.relay.user.display_name";
inline constexpr char kRelayUserTier[] = "maho.auth.relay.user.tier";
// Optional canonical identity link returned by the relay. Both values are
// stored only when the response supplies a non-empty pair.
inline constexpr char kRelayOAuthProvider[] =
    "maho.auth.relay.user.oauth_provider";
inline constexpr char kRelayOAuthProviderSub[] =
    "maho.auth.relay.user.oauth_provider_sub";

// Stable per-profile device identifier (client-generated UUID). Sent on
// login/signup so the relay reuses this device's row and sync room instead of
// minting a new one each time.
inline constexpr char kRelayDeviceId[] = "maho.auth.relay.device_id";

inline constexpr char kRelaySubscriptionStatus[] =
    "maho.auth.relay.subscription_status";
inline constexpr char kRelaySubscriptionExpiresAt[] =
    "maho.auth.relay.subscription_expires_at";

}  // namespace maho::account_prefs

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ACCOUNT_PREFS_H_
