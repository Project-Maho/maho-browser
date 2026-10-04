// Copyright 2026 The Maho Authors. All rights reserved.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_THROTTLE_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace maho {

class MahoMcpNavigationThrottle : public content::NavigationThrottle {
 public:
  explicit MahoMcpNavigationThrottle(
      content::NavigationThrottleRegistry& registry);
  ~MahoMcpNavigationThrottle() override;

  MahoMcpNavigationThrottle(const MahoMcpNavigationThrottle&) = delete;
  MahoMcpNavigationThrottle& operator=(const MahoMcpNavigationThrottle&) =
      delete;

  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_NAVIGATION_THROTTLE_H_
