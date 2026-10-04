// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service.h"

namespace maho {

// static
MahoDownloadBridgeServiceFactory* MahoDownloadBridgeServiceFactory::GetInstance() {
  static base::NoDestructor<MahoDownloadBridgeServiceFactory> instance;
  return instance.get();
}

// static
MahoDownloadBridgeService* MahoDownloadBridgeServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<MahoDownloadBridgeService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
MahoDownloadBridgeService* MahoDownloadBridgeServiceFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<MahoDownloadBridgeService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

MahoDownloadBridgeServiceFactory::MahoDownloadBridgeServiceFactory()
    : ProfileKeyedServiceFactory(
          "MahoDownloadBridgeService",
          ProfileSelections::BuildForRegularProfile()) {}

MahoDownloadBridgeServiceFactory::~MahoDownloadBridgeServiceFactory() = default;

std::unique_ptr<KeyedService>
MahoDownloadBridgeServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<MahoDownloadBridgeService>(
      Profile::FromBrowserContext(context));
}

bool MahoDownloadBridgeServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return false;
}

}  // namespace maho
