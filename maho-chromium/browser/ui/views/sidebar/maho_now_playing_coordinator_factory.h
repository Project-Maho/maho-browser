// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_COORDINATOR_FACTORY_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_COORDINATOR_FACTORY_H_

#include <memory>

#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

class Profile;

namespace maho {

class MahoNowPlayingCoordinator;

class MahoNowPlayingCoordinatorFactory : public ProfileKeyedServiceFactory {
 public:
  MahoNowPlayingCoordinatorFactory(const MahoNowPlayingCoordinatorFactory&) = delete;
  MahoNowPlayingCoordinatorFactory& operator=(const MahoNowPlayingCoordinatorFactory&) = delete;

  static MahoNowPlayingCoordinatorFactory* GetInstance();
  static MahoNowPlayingCoordinator* GetForProfile(Profile* profile);
  static MahoNowPlayingCoordinator* GetForProfileIfExists(Profile* profile);

 private:
  friend base::NoDestructor<MahoNowPlayingCoordinatorFactory>;

  MahoNowPlayingCoordinatorFactory();
  ~MahoNowPlayingCoordinatorFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_COORDINATOR_FACTORY_H_
