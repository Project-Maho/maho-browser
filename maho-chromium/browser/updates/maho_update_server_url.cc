// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_server_url.h"

#include "base/environment.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/updates/maho_update_pref_names.h"

namespace maho {
namespace updates {

bool IsAcceptableUpdateServerUrl(const std::string& url) {
  if (url.empty()) {
    return false;
  }
  return base::StartsWith(url, "http://", base::CompareCase::INSENSITIVE_ASCII) ||
         base::StartsWith(url, "https://", base::CompareCase::INSENSITIVE_ASCII);
}

std::string StripTrailingSlash(std::string url) {
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url;
}

std::string ResolveUpdateServerBaseUrl(const Profile* profile) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  if (env) {
    std::optional<std::string> from_env =
        env->GetVar(kMahoUpdateServerOverrideEnvVar);
    if (from_env && !from_env->empty()) {
      if (IsAcceptableUpdateServerUrl(*from_env)) {
        return StripTrailingSlash(std::move(*from_env));
      }
      LOG(ERROR) << "Maho: " << kMahoUpdateServerOverrideEnvVar
                 << " has unsupported scheme; falling back to pref/default";
    }
  }

  if (profile && profile->GetPrefs()) {
    std::string from_pref =
        profile->GetPrefs()->GetString(prefs::kMahoUpdateServerUrl);
    if (!from_pref.empty()) {
      if (IsAcceptableUpdateServerUrl(from_pref)) {
        return StripTrailingSlash(std::move(from_pref));
      }
      LOG(ERROR) << "Maho: " << prefs::kMahoUpdateServerUrl
                 << " has unsupported scheme; falling back to default";
    }
  }

  return std::string(kMahoUpdateServerDefaultUrl);
}

}  // namespace updates
}  // namespace maho
