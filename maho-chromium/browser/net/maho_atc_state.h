// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_ATC_STATE_H_
#define MAHO_BROWSER_NET_MAHO_ATC_STATE_H_

#include <string_view>

namespace maho {

bool HasEnabledTrafficRuleInJson(std::string_view rules_json);

class MahoAtcState {
 public:
  static bool HasEnabledRules();
  static void SetHasEnabledRules(bool has_rules);
};

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_ATC_STATE_H_
