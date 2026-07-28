// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_EXTENSION_SIDE_PANEL_COORDINATOR_H_
#define MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_EXTENSION_SIDE_PANEL_COORDINATOR_H_

#include <string>

#include "base/scoped_observation.h"
#include "chrome/browser/extensions/api/side_panel/side_panel_service.h"
#include "components/keyed_service/core/keyed_service.h"

#include "extensions/browser/extension_registry_observer.h"

#include "base/memory/raw_ptr.h"

class Profile;

namespace maho {

class MahoExtensionSidePanelCoordinator : public KeyedService,
                                          public extensions::SidePanelService::Observer,
                                          public extensions::ExtensionRegistryObserver {
 public:
  static MahoExtensionSidePanelCoordinator* GetForProfile(Profile* profile);

  explicit MahoExtensionSidePanelCoordinator(Profile* profile);
  MahoExtensionSidePanelCoordinator(const MahoExtensionSidePanelCoordinator&) = delete;
  MahoExtensionSidePanelCoordinator& operator=(const MahoExtensionSidePanelCoordinator&) = delete;
  ~MahoExtensionSidePanelCoordinator() override;

  // Updates the docking side ("left" or "right") of the given extension's Maho
  // side panel at runtime, overriding the static
  // side_panel.maho_side_panel.layout manifest value. No-op when the extension
  // has not registered a side panel with maho-core yet.
  void SetLayout(const extensions::ExtensionId& extension_id,
                 const std::string& layout);

  // extensions::SidePanelService::Observer:
  void OnPanelOptionsChanged(
      const extensions::ExtensionId& extension_id,
      const extensions::api::side_panel::PanelOptions& updated_options) override;
  void OnSidePanelServiceShutdown() override;

  // extensions::ExtensionRegistryObserver:
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;

 private:
  const raw_ptr<Profile> profile_;
  const std::string profile_key_;
  base::ScopedObservation<extensions::SidePanelService,
                          extensions::SidePanelService::Observer>
      service_observation_{this};
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_EXTENSION_SIDE_PANEL_COORDINATOR_H_
