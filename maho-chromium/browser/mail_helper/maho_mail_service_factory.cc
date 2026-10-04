// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_service_factory.h"

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "maho/browser/mail_helper/maho_mail_service.h"

namespace maho {

// static
MahoMailServiceFactory* MahoMailServiceFactory::GetInstance() {
  static base::NoDestructor<MahoMailServiceFactory> instance;
  return instance.get();
}

// static
MahoMailService* MahoMailServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<MahoMailService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

MahoMailService* MahoMailServiceFactory::GetForProfileIfExists(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<MahoMailService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/false));
}

MahoMailServiceFactory::MahoMailServiceFactory()
    : ProfileKeyedServiceFactory(
          "MahoMailService",
          ProfileSelections::BuildForRegularProfile()) {}

MahoMailServiceFactory::~MahoMailServiceFactory() = default;

std::unique_ptr<KeyedService>
MahoMailServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<MahoMailService>(
      Profile::FromBrowserContext(context),
      g_browser_process->os_crypt_async());
}

}  // namespace maho
