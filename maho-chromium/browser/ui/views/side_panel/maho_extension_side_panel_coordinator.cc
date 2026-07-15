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

namespace {

// Fail-closed predicate: Maho extension side-panel metadata is regular-profile
// only. A null or off-the-record profile must never reach maho::GetCore/FFI, so
// a stale or forged coordinator cannot mutate Maho side-panel state for OTR.
bool IsFailClosed(Profile* profile) {
  return !profile || profile->IsOffTheRecord();
}

}  // namespace

// static
MahoExtensionSidePanelCoordinator* MahoExtensionSidePanelCoordinator::GetForProfile(Profile* profile) {
  if (IsFailClosed(profile)) {
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
  // Fail closed for every OTR/null profile: register no instance and observe
  // no service/registry so no callback can reach maho::GetCore/FFI.
  if (IsFailClosed(profile_)) {
    return;
  }

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
  if (IsFailClosed(profile_)) {
    return;
  }
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

void MahoExtensionSidePanelCoordinator::SetLayout(
    const extensions::ExtensionId& extension_id,
    const std::string& layout) {
  if (IsFailClosed(profile_)) {
    return;
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  char* out_path = nullptr;
  char* out_layout = nullptr;
  int32_t out_default_width = 320;
  bool registered = maho_core_get_side_panel_options(
      core, extension_id.c_str(), &out_path, &out_layout, &out_default_width);

  std::string path = (registered && out_path) ? out_path : std::string();
  if (out_path) {
    maho_string_free(out_path);
  }
  if (out_layout) {
    maho_string_free(out_layout);
  }

  if (!registered || path.empty()) {
    return;
  }

  maho_core_register_side_panel(core, extension_id.c_str(), path.c_str(),
                                layout.c_str(), out_default_width);
}

void MahoExtensionSidePanelCoordinator::OnSidePanelServiceShutdown() {
  service_observation_.Reset();
}

void MahoExtensionSidePanelCoordinator::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  if (IsFailClosed(profile_)) {
    return;
  }
  MahoCore* core = maho::GetCore();
  if (core && extension) {
    maho_core_unregister_side_panel(core, extension->id().c_str());
  }
}

void MahoExtensionSidePanelCoordinator::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UninstallReason reason) {
  if (IsFailClosed(profile_)) {
    return;
  }
  MahoCore* core = maho::GetCore();
  if (core && extension) {
    maho_core_unregister_side_panel(core, extension->id().c_str());
  }
}

}  // namespace maho
