// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_COMPONENTS_CONSTANTS_WEBUI_URL_CONSTANTS_H_
#define MAHO_CHROMIUM_COMPONENTS_CONSTANTS_WEBUI_URL_CONSTANTS_H_

namespace maho {

inline constexpr char kMahoAIHost[] = "maho-ai";
inline constexpr char kMahoAIURL[] = "chrome://maho-ai/";

inline constexpr char kMahoSettingsHost[] = "maho-settings";
inline constexpr char kMahoSettingsURL[] = "chrome://maho-settings/";

inline constexpr char kMahoNotesHost[] = "maho-notes";
inline constexpr char kMahoNotesURL[] = "chrome://maho-notes/";

// Legacy trusted host — kept as a compile-time reference but no longer
// registered as a live WebUI.  All runtime use should go through the
// untrusted constants below.
inline constexpr char kMahoBoostHost[] = "maho-boost";
inline constexpr char kMahoBoostURL[] = "chrome://maho-boost/";

// Untrusted Boost editor host (active).
inline constexpr char kMahoBoostUntrustedHost[] = "maho-boost-editor";
inline constexpr char kMahoBoostUntrustedURL[] =
    "chrome-untrusted://maho-boost-editor/";

inline constexpr char kMahoEaselHost[] = "maho-easel";
inline constexpr char kMahoEaselURL[] = "chrome://maho-easel/";

inline constexpr char kMahoTestHost[] = "maho-test";
inline constexpr char kMahoTestURL[] = "chrome://maho-test/";

inline constexpr char kMahoLiveFoldersHost[] = "maho-live-folders";
inline constexpr char kMahoLiveFoldersURL[] = "chrome://maho-live-folders/";

inline constexpr char kMahoSyncHost[] = "maho-sync";
inline constexpr char kMahoSyncURL[] = "chrome://maho-sync/";

inline constexpr char kMahoSidebarHost[] = "maho-sidebar";
inline constexpr char kMahoSidebarURL[] = "chrome://maho-sidebar/";

inline constexpr char kMahoSidebarLegacyHost[] = "maho-sidebar-legacy";
inline constexpr char kMahoSidebarLegacyURL[] = "chrome://maho-sidebar-legacy/";

inline constexpr char kMahoSpaceConfigHost[] = "maho-space-config";
inline constexpr char kMahoSpaceConfigURL[] = "chrome://maho-space-config/";

inline constexpr char kMahoSpaceCreateHost[] = "maho-space-create";
inline constexpr char kMahoSpaceCreateURL[] = "chrome://maho-space-create/";

}  // namespace maho

#endif  // MAHO_CHROMIUM_COMPONENTS_CONSTANTS_WEBUI_URL_CONSTANTS_H_
