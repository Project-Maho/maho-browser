// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_COMPONENTS_CONSTANTS_WEBUI_URL_CONSTANTS_H_
#define MAHO_CHROMIUM_COMPONENTS_CONSTANTS_WEBUI_URL_CONSTANTS_H_

#include <array>

namespace maho {

inline constexpr char kMahoAIHost[] = "maho-ai";
inline constexpr char kMahoAIURL[] = "chrome://maho-ai/";

inline constexpr char kMahoSettingsHost[] = "maho-settings";
inline constexpr char kMahoSettingsURL[] = "chrome://maho-settings/";

// Legacy trusted host — kept as a compile-time reference but no longer
// registered as a live WebUI.  All runtime use should go through the
// untrusted constants below.
inline constexpr char kMahoBoostHost[] = "maho-boost";
inline constexpr char kMahoBoostURL[] = "chrome://maho-boost/";

// Untrusted Boost editor host (active).
inline constexpr char kMahoBoostUntrustedHost[] = "maho-boost-editor";
inline constexpr char kMahoBoostUntrustedURL[] =
    "chrome-untrusted://maho-boost-editor/";

// Untrusted artifact preview host: serves sanitized static HTML for the
// chrome://maho-ai side panel's sandboxed preview iframe (capability-gated).
inline constexpr char kMahoArtifactPreviewUntrustedHost[] =
    "maho-ai-artifact-preview";
inline constexpr char kMahoArtifactPreviewUntrustedURL[] =
    "chrome-untrusted://maho-ai-artifact-preview/";

// Untrusted artifact export host: streams raw artifact bytes for drag-out
// (capability-gated, application/octet-stream, never a trusted origin).
inline constexpr char kMahoArtifactExportUntrustedHost[] =
    "maho-ai-artifact-export";
inline constexpr char kMahoArtifactExportUntrustedURL[] =
    "chrome-untrusted://maho-ai-artifact-export/";

inline constexpr char kMahoTestHost[] = "maho-test";
inline constexpr char kMahoTestURL[] = "chrome://maho-test/";

inline constexpr char kMahoLiveFoldersHost[] = "maho-live-folders";
inline constexpr char kMahoLiveFoldersURL[] = "chrome://maho-live-folders/";

inline constexpr char kMahoSyncHost[] = "maho-sync";
inline constexpr char kMahoSyncURL[] = "chrome://maho-sync/";

inline constexpr char kMahoSidebarHost[] = "maho-sidebar";
inline constexpr char kMahoSidebarURL[] = "chrome://maho-sidebar/";

inline constexpr char kMahoWelcomeHost[] = "maho-welcome";
inline constexpr char kMahoWelcomeURL[] = "chrome://maho-welcome/";

inline constexpr char kMahoSidebarLegacyHost[] = "maho-sidebar-legacy";
inline constexpr char kMahoSidebarLegacyURL[] = "chrome://maho-sidebar-legacy/";

inline constexpr char kMahoSpaceConfigHost[] = "maho-space-config";
inline constexpr char kMahoSpaceConfigURL[] = "chrome://maho-space-config/";

inline constexpr char kMahoSpaceCreateHost[] = "maho-space-create";
inline constexpr char kMahoSpaceCreateURL[] = "chrome://maho-space-create/";

inline constexpr char kMahoRoutinesHost[] = "maho-routines";
inline constexpr char kMahoRoutinesURL[] = "chrome://maho-routines/";

inline constexpr char kMahoMailHost[] = "maho-mail";
inline constexpr char kMahoMailURL[] = "chrome://maho-mail/";

inline constexpr char kMahoChangelogHost[] = "maho-changelog";
inline constexpr char kMahoChangelogURL[] = "chrome://maho-changelog/";

// Public alias constants (maho:// scheme).
inline constexpr char kMahoSettingsPublicURL[] = "maho://settings/";
inline constexpr char kMahoAIPublicURL[] = "maho://ai/";
inline constexpr char kMahoSpaceConfigPublicURL[] = "maho://space-config/";
inline constexpr char kMahoSpaceCreatePublicURL[] = "maho://space-create/";
inline constexpr char kMahoSyncPublicURL[] = "maho://sync/";
inline constexpr char kMahoLiveFoldersPublicURL[] = "maho://live-folders/";
inline constexpr char kMahoWelcomePublicURL[] = "maho://welcome/";
inline constexpr char kMahoRoutinesPublicURL[] = "maho://routines/";
inline constexpr char kMahoMailPublicURL[] = "maho://mail/";
inline constexpr char kMahoChangelogPublicURL[] = "maho://changelog/";

struct MahoUrlAliasRecord {
  const char* alias_host;
  const char* public_url;
  const char* actual_host;
  const char* actual_url;
};

// Exactly ten production alias records.
inline constexpr std::array<MahoUrlAliasRecord, 10> kMahoUrlAliases = {{
    { "settings", kMahoSettingsPublicURL, kMahoSettingsHost, kMahoSettingsURL },
    { "ai", kMahoAIPublicURL, kMahoAIHost, kMahoAIURL },
    { "space-config", kMahoSpaceConfigPublicURL, kMahoSpaceConfigHost, kMahoSpaceConfigURL },
    { "space-create", kMahoSpaceCreatePublicURL, kMahoSpaceCreateHost, kMahoSpaceCreateURL },
    { "sync", kMahoSyncPublicURL, kMahoSyncHost, kMahoSyncURL },
    { "live-folders", kMahoLiveFoldersPublicURL, kMahoLiveFoldersHost, kMahoLiveFoldersURL },
    { "welcome", kMahoWelcomePublicURL, kMahoWelcomeHost, kMahoWelcomeURL },
    { "routines", kMahoRoutinesPublicURL, kMahoRoutinesHost, kMahoRoutinesURL },
    { "mail", kMahoMailPublicURL, kMahoMailHost, kMahoMailURL },
    { "changelog", kMahoChangelogPublicURL, kMahoChangelogHost, kMahoChangelogURL },
}};

}  // namespace maho

#endif  // MAHO_CHROMIUM_COMPONENTS_CONSTANTS_WEBUI_URL_CONSTANTS_H_
