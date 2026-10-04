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

// The account-escrow model supersedes the Vault-setup repair gate that used to
// send already-onboarded profiles back through onboarding when their profile
// database held no Vault key material: the Vault key now follows the signed-in
// account and is provisioned/escrowed by the sign-in Sync bootstrap, so a
// diverged profile is repaired by the next sign-in instead of a ceremony.

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
