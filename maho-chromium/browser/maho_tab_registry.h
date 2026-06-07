// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_TAB_REGISTRY_H_
#define MAHO_BROWSER_MAHO_TAB_REGISTRY_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "base/containers/flat_map.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;
class TabStripModel;

namespace maho {

struct LastDispatchedTabState {
  std::string url;
  std::string title;
};

// Architectural invariant: the canonical tab UUID is the one issued by
// `MahoTabIdHelper` on each `WebContents`. The Rust maho-core never invents
// its own UUID; it only receives the helper's UUID via `create_tab` shell
// events dispatched by this registry. Every Chromium TabStripModel mutation
// in a normal Browser flows through here, so any Navigate() or strip insert
// in the system reconciles automatically — there is no per-call-site
// `DispatchShellEvent("create_tab")` requirement.
//
// Bootstrap dispatches `create_tab` for every tab already on the strip when a
// Browser becomes observed. The Rust side must treat `create_tab_with_id`
// with a known TabId as a no-op for this to be safe under session restore.
class MahoTabRegistry : public BrowserCollectionObserver {
 public:
  static MahoTabRegistry* Get();

  MahoTabRegistry(const MahoTabRegistry&) = delete;
  MahoTabRegistry& operator=(const MahoTabRegistry&) = delete;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  static std::set<std::string> GetLiveTabIdsForProfile(Profile* profile);

  LastDispatchedTabState& GetOrCreateDispatchCache(const std::string& tab_id);
  void EraseDispatchCache(const std::string& tab_id);
  void MigrateDispatchCache(const std::string& from_tab_id,
                            const std::string& to_tab_id);

 private:
  friend class base::NoDestructor<MahoTabRegistry>;
  class StripObserver;
  class NavObserver;

  MahoTabRegistry();
  ~MahoTabRegistry() override;

  void AttachStripObserverFor(BrowserWindowInterface* browser);
  void DetachStripObserverFor(BrowserWindowInterface* browser);

  base::flat_map<BrowserWindowInterface*, std::unique_ptr<StripObserver>> strip_observers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  std::unordered_map<std::string, LastDispatchedTabState> dispatch_cache_;

  SEQUENCE_CHECKER(sequence_checker_);
};

class ShellEventObserver;
void SetTabRegistryShellEventObserverForTesting(ShellEventObserver* observer);

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_TAB_REGISTRY_H_
