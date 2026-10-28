#include "maho/browser/maho_ntp_preload_policy.h"

bool ShouldWarmNtp(MahoNtpProfileType profile_type,
                   MahoNtpMemoryPressure memory_pressure,
                   const GURL& ntp_url) {
  if (profile_type != MahoNtpProfileType::kRegular ||
      memory_pressure != MahoNtpMemoryPressure::kNone) {
    return false;
  }

  return ntp_url.is_valid() && ntp_url.SchemeIs("chrome") &&
         ntp_url.host() == "newtab" && ntp_url.path() == "/" &&
         ntp_url.query().empty() && ntp_url.ref().empty();
}
