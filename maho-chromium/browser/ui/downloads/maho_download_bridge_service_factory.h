// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_BRIDGE_SERVICE_FACTORY_H_
#define MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_BRIDGE_SERVICE_FACTORY_H_

#include <memory>

#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace content {
class BrowserContext;
}  // namespace content

class KeyedService;
class Profile;

namespace maho {

class MahoDownloadBridgeService;

class MahoDownloadBridgeServiceFactory : public ProfileKeyedServiceFactory {
 public:
  MahoDownloadBridgeServiceFactory(const MahoDownloadBridgeServiceFactory&) = delete;
  MahoDownloadBridgeServiceFactory& operator=(const MahoDownloadBridgeServiceFactory&) = delete;

  static MahoDownloadBridgeServiceFactory* GetInstance();
  static MahoDownloadBridgeService* GetForProfile(Profile* profile);
  static MahoDownloadBridgeService* GetForProfileIfExists(Profile* profile);

 private:
  friend base::NoDestructor<MahoDownloadBridgeServiceFactory>;

  MahoDownloadBridgeServiceFactory();
  ~MahoDownloadBridgeServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_BRIDGE_SERVICE_FACTORY_H_
