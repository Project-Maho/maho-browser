// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_atc_navigation_throttle.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/net/maho_atc_state.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "ui/base/page_transition_types.h"

namespace maho {

namespace {

constexpr base::TimeDelta kAtcDecisionTimeout = base::Seconds(5);

std::set<Browser*>& AtcThrottleBypassedBrowsers() {
  static auto* bypassed_browsers = new std::set<Browser*>();
  return *bypassed_browsers;
}

class ScopedAtcThrottleBypass {
 public:
  explicit ScopedAtcThrottleBypass(Browser* browser) : browser_(browser) {
    if (browser_) {
      AtcThrottleBypassedBrowsers().insert(browser_);
    }
  }

  ScopedAtcThrottleBypass(const ScopedAtcThrottleBypass&) = delete;
  ScopedAtcThrottleBypass& operator=(const ScopedAtcThrottleBypass&) = delete;

  ~ScopedAtcThrottleBypass() {
    if (browser_) {
      AtcThrottleBypassedBrowsers().erase(browser_);
    }
  }

  static bool IsBypassed(Browser* browser) {
    return browser && AtcThrottleBypassedBrowsers().contains(browser);
  }

 private:
  raw_ptr<Browser> browser_ = nullptr;
};

Profile* GetLoadedProfileForSpace(const std::string& space_id) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (!bridge || !g_browser_process || !g_browser_process->profile_manager()) {
    return nullptr;
  }
  const base::FilePath relative_path = bridge->GetProfilePathForSpace(space_id);
  if (relative_path.empty()) {
    return nullptr;
  }
  const base::FilePath absolute_path =
      g_browser_process->profile_manager()->user_data_dir().Append(
          relative_path);
  return g_browser_process->profile_manager()->GetProfileByPath(absolute_path);
}

base::FilePath GetAbsoluteProfilePathForSpace(const std::string& space_id) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (!bridge || !g_browser_process || !g_browser_process->profile_manager()) {
    return base::FilePath();
  }
  const base::FilePath relative_path = bridge->GetProfilePathForSpace(space_id);
  if (relative_path.empty()) {
    return base::FilePath();
  }
  return g_browser_process->profile_manager()->user_data_dir().Append(
      relative_path);
}

}  // namespace

MahoAtcNavigationThrottle::MahoAtcNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

MahoAtcNavigationThrottle::~MahoAtcNavigationThrottle() = default;

const char* MahoAtcNavigationThrottle::GetNameForLogging() {
  return "MahoAtcNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
MahoAtcNavigationThrottle::WillStartRequest() {
  return MaybeDeferNavigation(IsPeekEligibleAtStart());
}

content::NavigationThrottle::ThrottleCheckResult
MahoAtcNavigationThrottle::WillRedirectRequest() {
  return MaybeDeferNavigation(/*allow_peek_routing=*/false);
}

bool MahoAtcNavigationThrottle::IsPeekEligibleAtStart() {
  content::NavigationHandle* handle = navigation_handle();
  return handle->IsInPrimaryMainFrame() &&
         handle->IsRendererInitiated() && handle->HasUserGesture() &&
         handle->GetURL().SchemeIsHTTPOrHTTPS() && !handle->IsPost() &&
         !handle->IsSameDocument() &&
         ui::PageTransitionCoreTypeIs(handle->GetPageTransition(),
                                      ui::PAGE_TRANSITION_LINK);
}

PeekRoute MahoAtcNavigationThrottle::DecideSameTabPeekRoute(
    Browser* browser) {
  content::WebContents* source = navigation_handle()->GetWebContents();
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  MahoPeekController* peek_controller =
      browser_view ? browser_view->GetOrCreateMahoPeekController() : nullptr;
  return DecidePeekRoute({
      .master_enabled =
          prefs && prefs->GetBoolean(sidebar_prefs::kPeekEnabled),
      .popup_routing_enabled = false,
      .link_routing_enabled =
          prefs && prefs->GetBoolean(sidebar_prefs::kPeekLinkRoutingEnabled),
      .seam = PeekSeam::kSameTabThrottle,
      .disposition = WindowOpenDisposition::CURRENT_TAB,
      .source_role =
          MahoSidebarContainerView::GetPeekSourceRole(browser, source),
      .is_user_initiated = true,
      // Command-clicks are promoted by Blink to NEW_BACKGROUND_TAB and route
      // through Browser::OpenURLFromTab, so a navigation reaching this
      // CURRENT_TAB-only seam was not force-tabbed.
      .force_tab = false,
      .is_maho_mini_gesture = false,
      .atc_has_cross_space_target = false,
      .peek_slot_state =
          peek_controller && peek_controller->SlotBusy()
              ? PeekSlotState::kBusy
              : PeekSlotState::kClosed,
  });
}

content::NavigationThrottle::ThrottleCheckResult
MahoAtcNavigationThrottle::ApplyPeekRouteWithoutAtc(Browser* browser,
                                                     const GURL& url) {
  const PeekRoute route = DecideSameTabPeekRoute(browser);
  content::WebContents* source = navigation_handle()->GetWebContents();
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  MahoPeekController* peek_controller =
      browser_view ? browser_view->GetOrCreateMahoPeekController() : nullptr;
  if (route == PeekRoute::kOpenInPeek && peek_controller &&
      peek_controller->ShowUrl(source, url)) {
    return CANCEL_AND_IGNORE;
  }
  if (route == PeekRoute::kOpenInForegroundTab && browser) {
    ScopedAtcThrottleBypass bypass(browser);
    chrome::AddSelectedTabWithURL(browser, url, ui::PAGE_TRANSITION_LINK);
    return CANCEL_AND_IGNORE;
  }
  return PROCEED;
}

bool MahoAtcNavigationThrottle::ApplyPeekRouteAfterAtc(Browser* browser,
                                                        const GURL& url) {
  const ThrottleCheckResult result = ApplyPeekRouteWithoutAtc(browser, url);
  if (result.action() == content::NavigationThrottle::CANCEL_AND_IGNORE) {
    CancelDeferredNavigationAndIgnore();
    return true;
  }
  return false;
}

content::NavigationThrottle::ThrottleCheckResult
MahoAtcNavigationThrottle::MaybeDeferNavigation(bool allow_peek_routing) {
  content::NavigationHandle* handle = navigation_handle();
  content::WebContents* web_contents = handle->GetWebContents();
  Browser* browser =
      web_contents ? static_cast<Browser*>(
                         GlobalBrowserCollection::GetInstance()
                             ->FindBrowserWithTab(web_contents))
                   : nullptr;
  if (allow_peek_routing && !maho::MahoAtcState::HasEnabledRules()) {
    return ApplyPeekRouteWithoutAtc(browser, handle->GetURL());
  }
  if (!maho::MahoAtcState::HasEnabledRules()) {
    return PROCEED;
  }

  if (!handle->IsInPrimaryMainFrame() || handle->IsSameDocument()) {
    return PROCEED;
  }
  if (handle->IsPost()) {
    return PROCEED;
  }
  const GURL& url = handle->GetURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }
  if (handle->IsRendererInitiated() && !handle->HasUserGesture()) {
    return PROCEED;
  }

  if (!web_contents || !browser) {
    return PROCEED;
  }
  if (ScopedAtcThrottleBypass::IsBypassed(browser)) {
    return PROCEED;
  }

  // DecideLinkDestination runs synchronously (before this returns DEFER) when
  // the core is absent; guarding here keeps the decision asynchronous so
  // Resume()/CancelDeferredNavigation() are only ever called on a deferred
  // navigation.
  if (!maho::GetCore()) {
    return PROCEED;
  }

  std::string current_space_id;
  if (maho::MahoSpaceProfileBridge* bridge =
          maho::MahoSpaceProfileBridge::GetInstance()) {
    current_space_id = bridge->GetActiveSpaceId(browser);
  }

  deferred_navigation_finished_ = false;
  decision_timeout_.Start(
      FROM_HERE, kAtcDecisionTimeout,
      base::BindOnce(&MahoAtcNavigationThrottle::OnDecisionTimeout,
                     weak_factory_.GetWeakPtr()));
  maho::DecideLinkDestination(
      url, /*is_external=*/false,
      base::BindOnce(&MahoAtcNavigationThrottle::OnLinkDestinationDecided,
                     weak_factory_.GetWeakPtr(), browser->GetWeakPtr(), url,
                     std::move(current_space_id), allow_peek_routing));
  return DEFER;
}

void MahoAtcNavigationThrottle::OnLinkDestinationDecided(
    base::WeakPtr<BrowserWindowInterface> browser,
    GURL url,
    std::string current_space_id,
    bool peek_eligible_at_start,
    LinkDestinationResult result) {
  if (deferred_navigation_finished_) {
    return;
  }
  decision_timeout_.Stop();
  if (browser && result.type == maho::LinkDestinationType::kSpace &&
      !result.space_id.empty() && result.space_id != current_space_id) {
    Profile* target_profile = GetLoadedProfileForSpace(result.space_id);
    if (target_profile) {
      RouteToTargetProfile(browser, url, result.space_id, target_profile);
      return;
    }

    const base::FilePath target_path =
        GetAbsoluteProfilePathForSpace(result.space_id);
    if (target_path.empty() || !g_browser_process ||
        !g_browser_process->profile_manager() ||
        !g_browser_process->profile_manager()->LoadProfileByPath(
            target_path, false,
            base::BindOnce(&MahoAtcNavigationThrottle::OnTargetProfileLoaded,
                           weak_factory_.GetWeakPtr(), browser, url,
                           result.space_id))) {
      ResumeDeferredNavigation();
    }
    return;
  }
  if (peek_eligible_at_start && browser &&
      ApplyPeekRouteAfterAtc(static_cast<Browser*>(browser.get()), url)) {
    return;
  }
  ResumeDeferredNavigation();
}

void MahoAtcNavigationThrottle::OnTargetProfileLoaded(
    base::WeakPtr<BrowserWindowInterface> source_browser,
    GURL url,
    std::string space_id,
    Profile* target_profile) {
  if (deferred_navigation_finished_) {
    return;
  }
  RouteToTargetProfile(source_browser, url, std::move(space_id),
                       target_profile);
}

void MahoAtcNavigationThrottle::RouteToTargetProfile(
    base::WeakPtr<BrowserWindowInterface> source_browser,
    GURL url,
    std::string space_id,
    Profile* target_profile) {
  if (deferred_navigation_finished_) {
    return;
  }
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (!source_browser || !target_profile || !bridge) {
    ResumeDeferredNavigation();
    return;
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(target_profile);
  BrowserWindowInterface* target_bwi =
      collection ? collection->FindTabbedBrowser(/*match_original_profiles=*/false)
                 : nullptr;
  Browser* target_browser = static_cast<Browser*>(target_bwi);
  if (!target_browser) {
    target_browser =
        static_cast<Browser*>(CreateBrowserWindow(
            BrowserWindowCreateParams(target_profile, true)));
  }
  if (!target_browser || !bridge->SwitchToSpace(target_browser, space_id)) {
    ResumeDeferredNavigation();
    return;
  }

  ScopedAtcThrottleBypass bypass(target_browser);
  chrome::AddSelectedTabWithURL(target_browser, url, ui::PAGE_TRANSITION_LINK);
  if (target_browser->GetWindow()) {
    target_browser->GetWindow()->Show();
    target_browser->GetWindow()->Activate();
  }
  CancelDeferredNavigationAndIgnore();
}

void MahoAtcNavigationThrottle::ResumeDeferredNavigation() {
  if (deferred_navigation_finished_) {
    return;
  }
  deferred_navigation_finished_ = true;
  decision_timeout_.Stop();
  Resume();
}

void MahoAtcNavigationThrottle::CancelDeferredNavigationAndIgnore() {
  if (deferred_navigation_finished_) {
    return;
  }
  deferred_navigation_finished_ = true;
  decision_timeout_.Stop();
  CancelDeferredNavigation(content::NavigationThrottle::CANCEL_AND_IGNORE);
}

void MahoAtcNavigationThrottle::OnDecisionTimeout() {
  ResumeDeferredNavigation();
}

}  // namespace maho
