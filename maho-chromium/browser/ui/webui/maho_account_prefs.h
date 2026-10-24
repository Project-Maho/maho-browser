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

// First-run "Continue without an account" choice (registered by the welcome
// prefs). Any successful sign-in clears it.
inline constexpr char kLocalOnly[] = "maho.auth.local_only";

inline constexpr char kRelaySubscriptionStatus[] =
    "maho.auth.relay.subscription_status";
inline constexpr char kRelaySubscriptionExpiresAt[] =
    "maho.auth.relay.subscription_expires_at";

// Account Sync key custody reported by GET /sync/bootstrap: "client_held"
// when the account holds its own keys (the relay has no seed), empty or
// "escrow" otherwise. A client-held device must join with the recovery phrase.
inline constexpr char kRelaySyncKeyMode[] = "maho.auth.relay.sync_key_mode";
inline constexpr char kSyncKeyModeClientHeld[] = "client_held";
// The client-held account's current room id; a recovery phrase must derive
// this room or it belongs to another account.
inline constexpr char kRelaySyncClientHeldRoomId[] =
    "maho.auth.relay.sync_client_held_room_id";
// Room the relay answered 410 room_retired for (the account moved to
// client-held keys on another device). Sync is stopped for that room until
// the user re-joins with the recovery phrase.
inline constexpr char kRelaySyncRetiredRoomId[] =
    "maho.auth.relay.sync_retired_room_id";
// Room id this device switched to client-held keys locally but could not yet
// report to the relay (POST /sync/bootstrap/client-held failed). Settings
// offers a retry that re-POSTs only this room id.
inline constexpr char kRelaySyncClientHeldRelayPendingRoomId[] =
    "maho.auth.relay.sync_client_held_relay_pending_room_id";
inline constexpr char kSyncRoomRetiredMessage[] =
    "This account now uses your own keys - enter your recovery phrase to "
    "continue";

}  // namespace maho::account_prefs

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ACCOUNT_PREFS_H_
