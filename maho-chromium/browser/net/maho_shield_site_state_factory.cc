// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_shield_site_state_factory.h"

#include <memory>

#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_context.h"
#include "maho/browser/net/maho_shield_site_state.h"

namespace maho {

// static
MahoShieldSiteState* MahoShieldSiteStateFactory::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<MahoShieldSiteState*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
MahoShieldSiteState* MahoShieldSiteStateFactory::GetForProfileIfExists(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<MahoShieldSiteState*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

// static
MahoShieldSiteStateFactory* MahoShieldSiteStateFactory::GetInstance() {
  static base::NoDestructor<MahoShieldSiteStateFactory> instance;
  return instance.get();
}

MahoShieldSiteStateFactory::MahoShieldSiteStateFactory()
    : BrowserContextKeyedServiceFactory(
          "MahoShieldSiteState",
          BrowserContextDependencyManager::GetInstance()) {}

MahoShieldSiteStateFactory::~MahoShieldSiteStateFactory() = default;

content::BrowserContext* MahoShieldSiteStateFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  if (!context) {
    return nullptr;
  }
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || (!profile->IsRegularProfile() &&
                   !profile->IsIncognitoProfile())) {
    return nullptr;
  }
  return GetBrowserContextOwnInstanceInIncognito(context);
}

std::unique_ptr<KeyedService>
MahoShieldSiteStateFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || (!profile->IsRegularProfile() &&
                   !profile->IsIncognitoProfile())) {
    return nullptr;
  }
  return std::make_unique<MahoShieldSiteState>(profile);
}

}  // namespace maho
