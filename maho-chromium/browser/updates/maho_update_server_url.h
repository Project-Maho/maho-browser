// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_SERVER_URL_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_SERVER_URL_H_

#include <string>

class Profile;

namespace maho {
namespace updates {

inline constexpr char kMahoUpdateServerDefaultUrl[] = "https://update.maho.dev";

inline constexpr char kMahoUpdateServerOverrideEnvVar[] =
    "MAHO_UPDATE_SERVER_OVERRIDE";

std::string ResolveUpdateServerBaseUrl(const Profile* profile);

bool IsAcceptableUpdateServerUrl(const std::string& url);

std::string StripTrailingSlash(std::string url);

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_SERVER_URL_H_
