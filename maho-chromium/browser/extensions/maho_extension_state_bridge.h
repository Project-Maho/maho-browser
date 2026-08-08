// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_EXTENSIONS_MAHO_EXTENSION_STATE_BRIDGE_H_
#define MAHO_BROWSER_EXTENSIONS_MAHO_EXTENSION_STATE_BRIDGE_H_

#include <string>

#include "components/keyed_service/core/keyed_service.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"

class Profile;

namespace maho {

void EnsureMahoExtensionStateBridgeFactoryBuilt();

class MahoExtensionStateBridge : public KeyedService,
                                 public extensions::ExtensionRegistryObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnInstalledExtensionsChanged() = 0;
   protected:
    ~Observer() override = default;
  };

  explicit MahoExtensionStateBridge(Profile* profile);
  MahoExtensionStateBridge(const MahoExtensionStateBridge&) = delete;
  MahoExtensionStateBridge& operator=(const MahoExtensionStateBridge&) = delete;
  ~MahoExtensionStateBridge() override;

  static MahoExtensionStateBridge* FromProfile(Profile* profile);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // FFI mutation requests (must be called on the UI sequence).
  // Returns true if the operation was dispatched (policy allowed it and the
  // extension was found).  Returns false if policy blocks the action or the
  // extension is not registered.
  bool RequestEnable(const std::string& extension_id);
  bool RequestDisable(const std::string& extension_id);
  // Returns whether policy permits uninstalling the extension. Pure policy
  // check; performs no uninstall itself.
  bool CanUninstall(const std::string& extension_id);

  void HandleIncomingSyncForTesting(const std::string& extension_id,
                                    bool enabled,
                                    bool deleted) {
    HandleIncomingSync(extension_id, enabled, deleted);
  }

  // extensions::ExtensionRegistryObserver implementation:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;

 private:
  void PushFullSnapshot();
  void HandleIncomingSync(const std::string& extension_id,
                          bool enabled,
                          bool deleted);
  static void OnSyncCallback(void* user_data,
                             const char* extension_id,
                             bool enabled,
                             bool deleted);

  // Posts NotifyObservers() to the current sequence if no notification is
  // already pending.  Safe to call from inside an ExtensionRegistry callback
  // because the actual fan-out runs after the callback stack unwinds.
  void ScheduleObserverNotify();

  // Runs on the UI sequence via PostTask; clears notify_pending_ then fans
  // out OnInstalledExtensionsChanged() to all registered observers.
  void NotifyObservers();

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<Profile> profile_;
  const std::string profile_key_;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      observation_{this};
  base::ObserverList<Observer> observers_;

  // Coalesces multiple registry callbacks that fire in the same task into a
  // single posted notification.
  bool notify_pending_ = false;

  // Must be last — invalidates WeakPtrs on destruction before any other member
  // is torn down.
  base::WeakPtrFactory<MahoExtensionStateBridge> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_EXTENSIONS_MAHO_EXTENSION_STATE_BRIDGE_H_
