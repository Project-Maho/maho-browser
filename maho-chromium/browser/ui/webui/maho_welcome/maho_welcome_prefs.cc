// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"

#include "base/json/json_reader.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"

namespace maho::welcome {

GeneratedSyncKeyResult ParseGenerateSyncKeyJsonForTesting(const std::string& json) {
  GeneratedSyncKeyResult result;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return result;
  }
  const auto& dict = parsed->GetDict();
  const std::string* sk = dict.FindString("syncKey");
  const std::string* ri = dict.FindString("roomId");
  const std::string* rp = dict.FindString("recoveryPhrase");
  if (sk) result.sync_key = *sk;
  if (ri) result.room_id = *ri;
  if (rp) result.recovery_phrase = *rp;
  return result;
}

StartSyncResult ParseStartSyncJsonForTesting(const std::string& json) {
  StartSyncResult result;
  if (json.empty()) {
    result.ok = true;
    return result;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    result.ok = false;
    result.error_message = "Invalid JSON response";
    return result;
  }
  const auto& dict = parsed->GetDict();
  std::optional<bool> success = dict.FindBool("success");
  if (success && !*success) {
    result.ok = false;
    const std::string* error = dict.FindString("error");
    result.error_message = error ? *error : "Sync operation failed";
  } else {
    result.ok = true;
  }
  return result;
}

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kWelcomeCompleted, false);
  registry->RegisterBooleanPref(kLoginGateActive, false);
}

}  // namespace maho::welcome
