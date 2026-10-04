// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_CHROMIUM_BROWSER_NET_MAHO_SHIELD_SITE_STATE_FACTORY_H_
#define MAHO_CHROMIUM_BROWSER_NET_MAHO_SHIELD_SITE_STATE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace maho {

class MahoShieldSiteState;

class MahoShieldSiteStateFactory : public BrowserContextKeyedServiceFactory {
 public:
  static MahoShieldSiteState* GetForProfile(Profile* profile);
  static MahoShieldSiteState* GetForProfileIfExists(Profile* profile);
  static MahoShieldSiteStateFactory* GetInstance();

  MahoShieldSiteStateFactory(const MahoShieldSiteStateFactory&) = delete;
  MahoShieldSiteStateFactory& operator=(const MahoShieldSiteStateFactory&) =
      delete;

 private:
  friend base::NoDestructor<MahoShieldSiteStateFactory>;

  MahoShieldSiteStateFactory();
  ~MahoShieldSiteStateFactory() override;

  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace maho

#endif  // MAHO_CHROMIUM_BROWSER_NET_MAHO_SHIELD_SITE_STATE_FACTORY_H_
