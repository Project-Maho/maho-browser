#ifndef MAHO_BROWSER_MAHO_NTP_PRELOAD_POLICY_H_
#define MAHO_BROWSER_MAHO_NTP_PRELOAD_POLICY_H_

#include "url/gurl.h"

enum class MahoNtpProfileType {
  kRegular,
  kIncognito,
  kGuest,
  kSystem,
};

enum class MahoNtpMemoryPressure {
  kNone,
  kModerate,
  kCritical,
};

// Decides whether warming Maho's own NTP is allowed for the current context.
// This is a policy only; it does not start prerendering or inspect browser state.
bool ShouldWarmNtp(MahoNtpProfileType profile_type,
                   MahoNtpMemoryPressure memory_pressure,
                   const GURL& ntp_url);

#endif  // MAHO_BROWSER_MAHO_NTP_PRELOAD_POLICY_H_
