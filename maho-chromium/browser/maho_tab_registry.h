// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_TAB_REGISTRY_H_
#define MAHO_BROWSER_MAHO_TAB_REGISTRY_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/no_destructor.h"
#include "base/callback_list.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"

class Browser;
class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;
struct MahoCore;
class TabStripModel;

namespace maho {

struct LastDispatchedTabState {
  std::string url;
  std::string title;
  std::string favicon_b64;
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

  void ArchiveTabsAndRemoveFromStrip(Browser* browser,
                                     const std::vector<std::string>& tab_ids);
  bool ConsumeArchiveRemovalSuppression(const std::string& tab_id);
  void EraseArchiveRemovalSuppression(const std::string& tab_id);

  // Retries create_tab announcement for every observed tab still carrying
  // has_been_announced()==false (inserted before the bridge resolved the
  // active space, which makes DispatchCreateTabForContents no-op with no retry
  // trigger). Invoked by the bridge once the active space is known. Idempotent:
  // DispatchCreateTabForContents self-guards on announced/empty space_id.
  void ReannounceUnannouncedTabs();

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
  // Called from StripObserver::OnTabStripModelDestroyed(). Unlike
  // DetachStripObserverFor() it must not touch the browser's TabStripModel,
  // which is already being torn down.
  void DetachStripObserverForDestroyedModel(BrowserWindowInterface* browser);

  base::flat_map<BrowserWindowInterface*, std::unique_ptr<StripObserver>> strip_observers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::CallbackListSubscription core_ready_subscription_;

  std::unordered_map<std::string, LastDispatchedTabState> dispatch_cache_;
  base::flat_set<std::string> archive_removal_suppressions_;

  SEQUENCE_CHECKER(sequence_checker_);
};

class ShellEventObserver;
void SetTabRegistryShellEventObserverForTesting(ShellEventObserver* observer);
void DispatchTabRegistryShellEventForTesting(MahoCore* core,
                                             const std::string& kind,
                                             base::DictValue event);

bool IsCorePinnedTab(const std::string& tab_id);
bool IsCoreCloseProtectedTab(const std::string& tab_id);

// Scopes a suspended-tab wake (WakeSuspendedTab's Navigate call). While alive,
// the first WebContents announced through DispatchCreateTabForContents adopts
// |tab_id| in place of the fresh UUID minted by TabHelpers, so the create_tab
// event reaches maho-core under the ORIGINAL identity and the existing
// suspended tab is restored instead of a brand-new Normal tab being created.
// The kInserted announcement fires synchronously inside Navigate — before the
// caller gets a chance to rebind the id on the returned WebContents — which is
// why the adoption must happen here and not after Navigate returns. Unlike the
// old SetPendingRestoredTabId FIFO, the id cannot outlive the wake call and
// pollute an unrelated tab creation.
class ScopedMahoTabWakeId {
 public:
  explicit ScopedMahoTabWakeId(const std::string& tab_id);
  ~ScopedMahoTabWakeId();
  ScopedMahoTabWakeId(const ScopedMahoTabWakeId&) = delete;
  ScopedMahoTabWakeId& operator=(const ScopedMahoTabWakeId&) = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_TAB_REGISTRY_H_
