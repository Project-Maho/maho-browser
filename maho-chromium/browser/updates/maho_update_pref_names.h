// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_PREF_NAMES_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_PREF_NAMES_H_

namespace maho {
namespace prefs {

inline constexpr char kMahoUpdateChannel[] = "maho.update.channel";
inline constexpr char kMahoUpdateInstallId[] = "maho.update.install_id";
inline constexpr char kMahoUpdateLastKnownGoodVersion[] = "maho.update.last_known_good_version";
inline constexpr char kMahoUpdateLastSeenVersion[] = "maho.update.last_seen_version";
inline constexpr char kMahoUpdateAutoInstall[] = "maho.update.auto_install";
inline constexpr char kMahoUpdateCrashSentinel[] = "maho.update.crash_sentinel";
inline constexpr char kMahoUpdateRollbackRequested[] = "maho.update.rollback_requested";

// Per-profile override of the update server base URL. Empty string falls
// back to the build-time default (https://relay.mahobrowser.com). The
// MAHO_UPDATE_SERVER_OVERRIDE env var, if set, beats this pref. Used for
// pointing dev builds at a local maho-relay without rebuilding.
inline constexpr char kMahoUpdateServerUrl[] = "maho.update.server_url";

// Master kill-switch. Default false. When false, MahoUpdateManager::Initialize()
// is a no-op. Flip to true only after Phase 6 wires the user-visible channel
// selector and at least one full check-→download-→verify-→apply cycle has been
// validated against a fixture server. See the implementation plan §7.
inline constexpr char kMahoUpdateEnabled[] = "maho.update.enabled";
inline constexpr char kMahoConfigEnabled[] = "maho.config.enabled";

// Last version for which the post-update changelog tab was auto-opened.
// Local state. Default empty string (fresh installs seed current version on
// first run without auto-opening).
inline constexpr char kMahoChangelogLastShownVersion[] =
    "maho.changelog.last_shown_version";

}  // namespace prefs
}  // namespace maho


#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_PREF_NAMES_H_
