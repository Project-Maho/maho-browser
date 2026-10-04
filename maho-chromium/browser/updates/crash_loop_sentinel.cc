// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/crash_loop_sentinel.h"

#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "maho/browser/updates/maho_update_pref_names.h"

namespace maho {
namespace updates {

bool CrashLoopSentinel::CheckAndMarkStart(PrefService* local_state, const std::string& current_version) {
  if (!local_state) {
    return false;
  }

  const base::DictValue& dict = local_state->GetDict(prefs::kMahoUpdateCrashSentinel);
  const std::string* version = dict.FindString("version");
  int count = dict.FindInt("count").value_or(0);

  bool is_crash_loop = false;
  
  ScopedDictPrefUpdate update(local_state, prefs::kMahoUpdateCrashSentinel);
  base::DictValue& new_dict = update.Get();

  if (version && *version == current_version) {
    count++;
    if (count >= 3) {
      is_crash_loop = true;
    }
    new_dict.Set("count", count);
  } else {
    new_dict.Set("version", current_version);
    new_dict.Set("count", 1);
    new_dict.Set("started_at", double(base::Time::Now().InMillisecondsSinceUnixEpoch()));
  }

  return is_crash_loop;
}

void CrashLoopSentinel::MarkStable(PrefService* local_state, const std::string& current_version) {
  if (!local_state) {
    return;
  }
  
  const base::DictValue& dict = local_state->GetDict(prefs::kMahoUpdateCrashSentinel);
  const std::string* version = dict.FindString("version");
  if (version && *version == current_version) {
    ScopedDictPrefUpdate update(local_state, prefs::kMahoUpdateCrashSentinel);
    update.Get().Set("count", 0);
  }
}

}  // namespace updates
}  // namespace maho
