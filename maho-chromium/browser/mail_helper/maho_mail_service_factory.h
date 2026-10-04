// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_FACTORY_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_FACTORY_H_

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

class MahoMailService;

// Singleton that owns the per-profile MahoMailService. Uses the default
// ProfileSelections (regular profiles only), so no service is created for
// off-the-record or otherwise unsupported contexts. The factory never launches
// the helper itself -- that is an explicit EnsureHelperLaunched() call by the
// owner -- and holds no global browser-main state.
class MahoMailServiceFactory : public ProfileKeyedServiceFactory {
 public:
  MahoMailServiceFactory(const MahoMailServiceFactory&) = delete;
  MahoMailServiceFactory& operator=(const MahoMailServiceFactory&) = delete;

  static MahoMailServiceFactory* GetInstance();

  // Returns the service for `profile`, creating it on first use. Returns
  // nullptr for off-the-record / unsupported profiles.
  static MahoMailService* GetForProfile(Profile* profile);

  static MahoMailService* GetForProfileIfExists(Profile* profile);

 private:
  friend base::NoDestructor<MahoMailServiceFactory>;

  MahoMailServiceFactory();
  ~MahoMailServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_SERVICE_FACTORY_H_
