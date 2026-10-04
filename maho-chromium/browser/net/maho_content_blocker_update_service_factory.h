// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_NET_MAHO_CONTENT_BLOCKER_UPDATE_SERVICE_FACTORY_H_
#define MAHO_CHROMIUM_BROWSER_NET_MAHO_CONTENT_BLOCKER_UPDATE_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace maho {

class MahoContentBlockerUpdateService;

class MahoContentBlockerUpdateServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static MahoContentBlockerUpdateService* GetForProfile(Profile* profile);
  // Returns the existing service for |profile| without constructing one; used
  // during shutdown so teardown never creates a service.
  static MahoContentBlockerUpdateService* GetForProfileIfExists(
      Profile* profile);
  static MahoContentBlockerUpdateServiceFactory* GetInstance();

  MahoContentBlockerUpdateServiceFactory(
      const MahoContentBlockerUpdateServiceFactory&) = delete;
  MahoContentBlockerUpdateServiceFactory& operator=(
      const MahoContentBlockerUpdateServiceFactory&) = delete;

 private:
  friend base::NoDestructor<MahoContentBlockerUpdateServiceFactory>;

  MahoContentBlockerUpdateServiceFactory();
  ~MahoContentBlockerUpdateServiceFactory() override;

  // BrowserContextKeyedServiceFactory implementation:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_NET_MAHO_CONTENT_BLOCKER_UPDATE_SERVICE_FACTORY_H_
