// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_ATC_NAVIGATION_THROTTLE_H_
#define MAHO_BROWSER_NET_MAHO_ATC_NAVIGATION_THROTTLE_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/navigation_throttle.h"
#include "maho/browser/ui/views/peek/maho_peek_route.h"
#include "url/gurl.h"

class Browser;
class BrowserWindowInterface;
class Profile;

namespace maho {

struct LinkDestinationResult;

// Routes top-level, user-initiated navigations into an ATC target space when a
// traffic rule matches. This covers the navigation paths that never reach
// Browser::OpenURLFromTab: address-bar (omnibox) typed navigations
// (chrome::OpenCurrentURL -> chrome::Navigate) and plain same-tab
// renderer-initiated link clicks (content BeginNavigation). New-tab /
// new-window dispositions continue to route via the OpenURLFromTab hook (before
// tab creation), and this throttle no-ops on the re-issued same-space tab.
class MahoAtcNavigationThrottle : public content::NavigationThrottle {
 public:
  explicit MahoAtcNavigationThrottle(
      content::NavigationThrottleRegistry& registry);
  ~MahoAtcNavigationThrottle() override;

  MahoAtcNavigationThrottle(const MahoAtcNavigationThrottle&) = delete;
  MahoAtcNavigationThrottle& operator=(const MahoAtcNavigationThrottle&) =
      delete;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult MaybeDeferNavigation(bool allow_peek_routing);
  bool IsPeekEligibleAtStart();
  PeekRoute DecideSameTabPeekRoute(Browser* browser);
  ThrottleCheckResult ApplyPeekRouteWithoutAtc(Browser* browser,
                                               const GURL& url);
  bool ApplyPeekRouteAfterAtc(Browser* browser, const GURL& url);
  void OnLinkDestinationDecided(base::WeakPtr<BrowserWindowInterface> browser,
                                GURL url,
                                std::string current_space_id,
                                bool peek_eligible_at_start,
                                LinkDestinationResult result);
  void OnTargetProfileLoaded(base::WeakPtr<BrowserWindowInterface> source_browser,
                             GURL url,
                             std::string space_id,
                             Profile* target_profile);
  void RouteToTargetProfile(base::WeakPtr<BrowserWindowInterface> source_browser,
                            GURL url,
                            std::string space_id,
                            Profile* target_profile);
  void ResumeDeferredNavigation();
  void CancelDeferredNavigationAndIgnore();
  void OnDecisionTimeout();

  base::OneShotTimer decision_timeout_;
  bool deferred_navigation_finished_ = false;
  base::WeakPtrFactory<MahoAtcNavigationThrottle> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_ATC_NAVIGATION_THROTTLE_H_
