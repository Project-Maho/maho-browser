// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_content_blocker_update_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/net/maho_content_blocker_update_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace maho {

// static
MahoContentBlockerUpdateService*
MahoContentBlockerUpdateServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<MahoContentBlockerUpdateService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
MahoContentBlockerUpdateService*
MahoContentBlockerUpdateServiceFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<MahoContentBlockerUpdateService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

// static
MahoContentBlockerUpdateServiceFactory*
MahoContentBlockerUpdateServiceFactory::GetInstance() {
  static base::NoDestructor<MahoContentBlockerUpdateServiceFactory> instance;
  return instance.get();
}

MahoContentBlockerUpdateServiceFactory::MahoContentBlockerUpdateServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "MahoContentBlockerUpdateService",
          BrowserContextDependencyManager::GetInstance()) {}

MahoContentBlockerUpdateServiceFactory::
    ~MahoContentBlockerUpdateServiceFactory() = default;

std::unique_ptr<KeyedService>
MahoContentBlockerUpdateServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  return std::make_unique<MahoContentBlockerUpdateService>(
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess());
}

}  // namespace maho
