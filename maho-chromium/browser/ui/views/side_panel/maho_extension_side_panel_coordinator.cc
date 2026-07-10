// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h"

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

base::flat_map<Profile*, maho::MahoExtensionSidePanelCoordinator*>& GetInstanceMap() {
  static base::NoDestructor<base::flat_map<Profile*, maho::MahoExtensionSidePanelCoordinator*>> s_map;
  return *s_map;
}

}  // namespace

namespace maho {

// static
MahoExtensionSidePanelCoordinator* MahoExtensionSidePanelCoordinator::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto& map = GetInstanceMap();
  auto it = map.find(profile);
  if (it == map.end()) {
    return nullptr;
  }
  return it->second;
}

MahoExtensionSidePanelCoordinator::MahoExtensionSidePanelCoordinator(Profile* profile)
    : profile_(profile) {
  GetInstanceMap()[profile_] = this;

  auto* service = extensions::SidePanelService::Get(profile_);
  if (service) {
    service_observation_.Observe(service);
  }

  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  if (registry) {
    registry_observation_.Observe(registry);
  }
}

MahoExtensionSidePanelCoordinator::~MahoExtensionSidePanelCoordinator() {
  GetInstanceMap().erase(profile_);
}

void MahoExtensionSidePanelCoordinator::OnPanelOptionsChanged(
    const extensions::ExtensionId& extension_id,
    const extensions::api::side_panel::PanelOptions& updated_options) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  bool is_enabled = updated_options.enabled.value_or(true);
  std::string path = updated_options.path.value_or("");

  if (!is_enabled || path.empty()) {
    maho_core_unregister_side_panel(core, extension_id.c_str());
    return;
  }

  std::string layout = "right";
  int32_t default_width = 320;

  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile_)->GetExtensionById(
          extension_id, extensions::ExtensionRegistry::EVERYTHING);
  if (extension && extension->manifest() && extension->manifest()->value()) {
    const base::DictValue* side_panel_dict =
        extension->manifest()->value()->FindDict("side_panel");
    if (side_panel_dict) {
      const base::DictValue* maho_side_panel_dict =
          side_panel_dict->FindDict("maho_side_panel");
      if (maho_side_panel_dict) {
        const std::string* layout_val = maho_side_panel_dict->FindString("layout");
        if (layout_val) {
          layout = *layout_val;
        }
        std::optional<int> width_val = maho_side_panel_dict->FindInt("default_width");
        if (width_val) {
          default_width = *width_val;
        }
      }
    }
  }

  maho_core_register_side_panel(core, extension_id.c_str(), path.c_str(),
                                layout.c_str(), default_width);
}

void MahoExtensionSidePanelCoordinator::OnSidePanelServiceShutdown() {
  service_observation_.Reset();
}

void MahoExtensionSidePanelCoordinator::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  MahoCore* core = maho::GetCore();
  if (core && extension) {
    maho_core_unregister_side_panel(core, extension->id().c_str());
  }
}

void MahoExtensionSidePanelCoordinator::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UninstallReason reason) {
  MahoCore* core = maho::GetCore();
  if (core && extension) {
    maho_core_unregister_side_panel(core, extension->id().c_str());
  }
}

}  // namespace maho
