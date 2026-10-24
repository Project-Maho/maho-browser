// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_

#include <string>

#include "maho/browser/ui/webui/maho_account_prefs.h"

class PrefRegistrySimple;

namespace maho::welcome {

inline constexpr char kWelcomeCompleted[] = "maho.welcome.completed";
inline constexpr char kLoginGateActive[] = "maho.auth.login_gate_active";
// True after the user chose "Continue without an account" on first run.
// Cleared whenever relay tokens are stored (any sign-in). Declared with the
// account prefs so maho_auth_utils can clear it without depending on welcome.
using ::maho::account_prefs::kLocalOnly;

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

// Only an unfinished first run gates the browser. A completed profile whose
// relay session expired keeps browsing signed out (sync paused, account-escrowed
// Vault locked until the next sign-in) instead of being re-gated. A local-only
// profile (Continue without an account) is never gated by sign-in.
constexpr bool IsLoginGateActive(bool login_gate_bypassed,
                                 bool welcome_completed,
                                 bool local_only) {
  return !login_gate_bypassed && !welcome_completed && !local_only;
}

// FinishOnboarding requires either a signed-in relay session or the explicit
// local-only choice; local-only never needs a relay session.
constexpr bool CanFinishOnboarding(bool has_valid_relay_session,
                                   bool local_only) {
  return has_valid_relay_session || local_only;
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
