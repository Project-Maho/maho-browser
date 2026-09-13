// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_

#include <string>

class PrefRegistrySimple;

namespace maho::welcome {

inline constexpr char kWelcomeCompleted[] = "maho.welcome.completed";
inline constexpr char kLoginGateActive[] = "maho.auth.login_gate_active";

constexpr bool RequiresPasswordSetup(bool welcome_completed) {
  return !welcome_completed;
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
