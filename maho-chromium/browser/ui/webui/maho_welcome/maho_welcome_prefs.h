// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_

#include <string>

class PrefRegistrySimple;

namespace maho::welcome {

inline constexpr char kWelcomeCompleted[] = "maho.welcome.completed";
inline constexpr char kLoginGateActive[] = "maho.auth.login_gate_active";

// Durable account credentials identify a returning profile even when its
// completion pref was reset. Run before Vault repair, which may clear completion.
constexpr bool ReconcileWelcomeCompleted(bool welcome_completed,
                                         bool has_valid_relay_session,
                                         bool has_stored_session) {
  return welcome_completed || has_valid_relay_session || has_stored_session;
}

constexpr bool RequiresPasswordSetup(bool welcome_completed) {
  return !welcome_completed;
}

// Whether a profile that already finished onboarding must be sent back through
// Vault setup because its profile database holds no Vault key material.
//
// `kWelcomeCompleted` lives in profile Preferences while the Vault's key
// material lives in the profile database, so the two can diverge (a reset,
// replaced, or newly created database under an already-onboarded profile).
// Onboarding is the only path that initializes the Vault, so without this
// reconciliation such a profile keeps an uninitialized Vault forever: every
// native password write is refused for the life of the profile.
//
// Only an affirmatively observed uninitialized Vault re-opens setup. A locked
// Vault is initialized, and an unknown/unreadable state must never be treated
// as "no key material" because re-running setup would overwrite key material
// that exists but could not be read.
constexpr bool RequiresVaultSetupRepair(bool welcome_completed,
                                        bool vault_is_uninitialized) {
  return welcome_completed && vault_is_uninitialized;
}

// Returning-profile repair additionally respects the selected password
// provider. bitwarden/onepassword/disabled profiles legitimately hold no
// native Vault, so only a profile that affirmatively selected Maho Native may
// be re-opened through setup; an unknown/unreadable provider token is never
// native and is therefore never repair.
constexpr bool RequiresVaultSetupRepair(bool welcome_completed,
                                        bool vault_is_uninitialized,
                                        bool selected_provider_is_native) {
  return welcome_completed && vault_is_uninitialized &&
         selected_provider_is_native;
}

constexpr bool IsLoginGateActive(bool login_gate_bypassed,
                                 bool has_valid_relay_session,
                                 bool welcome_completed) {
  return !login_gate_bypassed &&
         (!has_valid_relay_session || !welcome_completed);
}

struct GeneratedSyncKeyResult {
  std::string sync_key;
  std::string room_id;
  std::string recovery_phrase;
};

struct StartSyncResult {
  bool ok = false;
  std::string error_message;
};

GeneratedSyncKeyResult ParseGenerateSyncKeyJsonForTesting(const std::string& json);
StartSyncResult ParseStartSyncJsonForTesting(const std::string& json);

void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace maho::welcome

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_
