// Copyright 2026 The Maho Authors. All rights reserved.

#include "maho/browser/mcp/maho_mcp_navigation_throttle.h"

namespace maho {

MahoMcpNavigationThrottle::MahoMcpNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

MahoMcpNavigationThrottle::~MahoMcpNavigationThrottle() = default;

const char* MahoMcpNavigationThrottle::GetNameForLogging() {
  return "MahoMcpNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
MahoMcpNavigationThrottle::WillStartRequest() {
  return PROCEED;
}

content::NavigationThrottle::ThrottleCheckResult
MahoMcpNavigationThrottle::WillRedirectRequest() {
  return PROCEED;
}

}  // namespace maho
