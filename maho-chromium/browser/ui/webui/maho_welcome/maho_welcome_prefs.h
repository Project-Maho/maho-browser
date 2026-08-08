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

constexpr bool IsLoginGateActive(bool login_gate_bypassed,
                                 bool has_valid_relay_session) {
  return !login_gate_bypassed && !has_valid_relay_session;
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
