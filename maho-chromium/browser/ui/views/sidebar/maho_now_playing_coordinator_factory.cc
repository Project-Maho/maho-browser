// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service_factory.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"

namespace maho {

// static
MahoNowPlayingCoordinatorFactory* MahoNowPlayingCoordinatorFactory::GetInstance() {
  static base::NoDestructor<MahoNowPlayingCoordinatorFactory> instance;
  return instance.get();
}

// static
MahoNowPlayingCoordinator* MahoNowPlayingCoordinatorFactory::GetForProfile(Profile* profile) {
  return static_cast<MahoNowPlayingCoordinator*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
MahoNowPlayingCoordinator* MahoNowPlayingCoordinatorFactory::GetForProfileIfExists(
    Profile* profile) {
  return static_cast<MahoNowPlayingCoordinator*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

MahoNowPlayingCoordinatorFactory::MahoNowPlayingCoordinatorFactory()
    : ProfileKeyedServiceFactory(
          "MahoNowPlayingCoordinator",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kOwnInstance)
              .WithAshInternals(ProfileSelection::kOwnInstance)
              .Build()) {
  DependsOn(MediaNotificationServiceFactory::GetInstance());
}

MahoNowPlayingCoordinatorFactory::~MahoNowPlayingCoordinatorFactory() = default;

std::unique_ptr<KeyedService>
MahoNowPlayingCoordinatorFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  auto* service = MediaNotificationServiceFactory::GetForProfile(profile);
  auto* manager = service ? service->media_item_manager() : nullptr;
  return std::make_unique<MahoNowPlayingCoordinator>(profile, manager);
}

bool MahoNowPlayingCoordinatorFactory::ServiceIsCreatedWithBrowserContext() const {
  return false;
}

}  // namespace maho
