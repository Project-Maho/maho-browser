// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h"

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

bool IsFailClosed(Profile* profile) {
  return !profile || profile->IsOffTheRecord();
}

}  // namespace

namespace maho {

namespace {

class MahoExtensionSidePanelCoordinatorFactory
    : public ProfileKeyedServiceFactory {
 public:
  static MahoExtensionSidePanelCoordinatorFactory* GetInstance() {
    static base::NoDestructor<MahoExtensionSidePanelCoordinatorFactory> instance;
    return instance.get();
  }

  static MahoExtensionSidePanelCoordinator* GetForProfile(Profile* profile) {
    if (IsFailClosed(profile)) {
      return nullptr;
    }
    return static_cast<MahoExtensionSidePanelCoordinator*>(
        GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
  }

  MahoExtensionSidePanelCoordinatorFactory(
      const MahoExtensionSidePanelCoordinatorFactory&) = delete;
  MahoExtensionSidePanelCoordinatorFactory& operator=(
      const MahoExtensionSidePanelCoordinatorFactory&) = delete;

 private:
  friend base::NoDestructor<MahoExtensionSidePanelCoordinatorFactory>;

  MahoExtensionSidePanelCoordinatorFactory()
      : ProfileKeyedServiceFactory(
            "MahoExtensionSidePanelCoordinator",
            ProfileSelections::BuildForRegularProfile()) {}
  ~MahoExtensionSidePanelCoordinatorFactory() override = default;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    Profile* profile = Profile::FromBrowserContext(context);
    if (IsFailClosed(profile)) {
      return nullptr;
    }
    return std::make_unique<MahoExtensionSidePanelCoordinator>(profile);
  }

  bool ServiceIsCreatedWithBrowserContext() const override { return true; }
};

}  // namespace

// static
MahoExtensionSidePanelCoordinator* MahoExtensionSidePanelCoordinator::GetForProfile(Profile* profile) {
  return MahoExtensionSidePanelCoordinatorFactory::GetForProfile(profile);
}

MahoExtensionSidePanelCoordinator::MahoExtensionSidePanelCoordinator(Profile* profile)
    : profile_(profile),
      profile_key_(MahoSpaceProfileBridge::GetInstance()->GetSpaceIdForProfile(profile)) {
  // Fail closed for every OTR/null profile: register no instance and observe
  // no service/registry so no callback can reach maho::GetCore/FFI.
  if (IsFailClosed(profile_)) {
    return;
  }

  auto* service = extensions::SidePanelService::Get(profile_);
  if (service) {
    service_observation_.Observe(service);
  }

  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  if (registry) {
    registry_observation_.Observe(registry);
  }
}

MahoExtensionSidePanelCoordinator::~MahoExtensionSidePanelCoordinator() = default;

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
    maho_core_unregister_side_panel_for_profile(core, profile_key_.c_str(),
                                                extension_id.c_str());
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
        if (layout_val && (*layout_val == "left" || *layout_val == "right")) {
          layout = *layout_val;
        }
        std::optional<int> width_val = maho_side_panel_dict->FindInt("default_width");
        if (width_val) {
          int w = *width_val;
          default_width = w < 200 ? 200 : (w > 800 ? 800 : w);
        }
      }
    }
  }

  maho_core_register_side_panel_for_profile(core, profile_key_.c_str(),
                                            extension_id.c_str(), path.c_str(),
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
  int32_t out_default_width = 320;
  bool registered = maho_core_get_side_panel_options_for_profile(
      core, profile_key_.c_str(), extension_id.c_str(), &out_path,
      /*out_layout=*/nullptr, &out_default_width);

  std::string path = (registered && out_path) ? out_path : std::string();
  if (out_path) {
    maho_string_free(out_path);
  }

  if (!registered || path.empty()) {
    return;
  }

  maho_core_register_side_panel_for_profile(core, profile_key_.c_str(),
                                            extension_id.c_str(), path.c_str(),
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
    maho_core_unregister_side_panel_for_profile(core, profile_key_.c_str(),
                                                extension->id().c_str());
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
    maho_core_unregister_side_panel_for_profile(core, profile_key_.c_str(),
                                                extension->id().c_str());
  }
}

}  // namespace maho
