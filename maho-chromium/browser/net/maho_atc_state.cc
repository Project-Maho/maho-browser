// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_atc_state.h"

#include "base/json/json_reader.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"

namespace maho {
namespace {

bool& GetHasEnabledRulesInternal() {
  static bool has_enabled_rules = false;
  return has_enabled_rules;
}

}  // namespace

bool HasEnabledTrafficRuleInJson(std::string_view rules_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(rules_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict || !dict->FindString("space_id") || dict->Find("url_pattern") ||
        dict->Find("max_age_hours") || dict->Find("max_tabs")) {
      continue;
    }
    if (dict->FindBool("enabled").value_or(true)) {
      return true;
    }
  }
  return false;
}

bool MahoAtcState::HasEnabledRules() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return GetHasEnabledRulesInternal();
}

void MahoAtcState::SetHasEnabledRules(bool has_rules) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  GetHasEnabledRulesInternal() = has_rules;
}

}  // namespace maho
