// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_SERVER_URL_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_SERVER_URL_H_

#include <string>

#include "base/component_export.h"

class Profile;

namespace maho {
namespace updates {

inline constexpr char kMahoUpdateServerDefaultUrl[] = "https://relay.mahobrowser.com";

inline constexpr char kMahoUpdateServerOverrideEnvVar[] =
    "MAHO_UPDATE_SERVER_OVERRIDE";

// Static fallback feeds published as GitHub Release assets by the release
// pipeline. Clients read them only when the relay feed fails; their content is
// the same signed bytes, so signature verification stays mandatory.
inline constexpr char kMahoStaticMacAppcastFallbackUrl[] =
    "https://github.com/Project-Maho/maho-browser/releases/latest/download/"
    "appcast.xml";
inline constexpr char kMahoStaticLinuxManifestFallbackUrl[] =
    "https://github.com/Project-Maho/maho-browser/releases/latest/download/"
    "linux-update-manifest.json";

COMPONENT_EXPORT(MAHO_UPDATES)
std::string GetStaticMacAppcastFallbackUrl();

COMPONENT_EXPORT(MAHO_UPDATES)
std::string GetStaticLinuxManifestFallbackUrl();

COMPONENT_EXPORT(MAHO_UPDATES)
std::string ResolveUpdateServerBaseUrl(const Profile* profile);

COMPONENT_EXPORT(MAHO_UPDATES)
bool IsAcceptableUpdateServerUrl(const std::string& url);

COMPONENT_EXPORT(MAHO_UPDATES)
std::string StripTrailingSlash(std::string url);

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_SERVER_URL_H_
