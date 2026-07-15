// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_

class PrefRegistrySimple;

namespace maho::welcome {

inline constexpr char kWelcomeCompleted[] = "maho.welcome.completed";
inline constexpr char kLoginGateActive[] = "maho.auth.login_gate_active";

void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace maho::welcome

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_PREFS_H_
