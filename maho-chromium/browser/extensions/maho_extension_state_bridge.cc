// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/maho_extension_state_bridge.h"

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/maho_private_context_policy.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/ui_util.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/manifest_handlers/description_info.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/permissions/permission_set.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"

namespace maho {

namespace {

class MahoExtensionStateBridgeFactory : public ProfileKeyedServiceFactory {
 public:
  static MahoExtensionStateBridgeFactory* GetInstance() {
    static base::NoDestructor<MahoExtensionStateBridgeFactory> instance;
    return instance.get();
  }

  static MahoExtensionStateBridge* GetForProfile(Profile* profile) {
    if (!profile) {
      return nullptr;
    }
    return static_cast<MahoExtensionStateBridge*>(
        GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
  }

  MahoExtensionStateBridgeFactory(const MahoExtensionStateBridgeFactory&) = delete;
  MahoExtensionStateBridgeFactory& operator=(const MahoExtensionStateBridgeFactory&) = delete;

 private:
  friend base::NoDestructor<MahoExtensionStateBridgeFactory>;

  MahoExtensionStateBridgeFactory()
      : ProfileKeyedServiceFactory(
            "MahoExtensionStateBridge",
            ProfileSelections::BuildForRegularProfile()) {}
  ~MahoExtensionStateBridgeFactory() override = default;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    Profile* profile = Profile::FromBrowserContext(context);
    if (!MahoIsCapabilityAllowed(profile,
                                 MahoPrivateCapability::kExtensionSnapshot)) {
      return nullptr;
    }
    return std::make_unique<MahoExtensionStateBridge>(profile);
  }

  bool ServiceIsCreatedWithBrowserContext() const override { return true; }
};

}  // namespace

void EnsureMahoExtensionStateBridgeFactoryBuilt() {
  MahoExtensionStateBridgeFactory::GetInstance();
}

// static
MahoExtensionStateBridge* MahoExtensionStateBridge::FromProfile(
    Profile* profile) {
  if (!MahoIsCapabilityAllowed(profile,
                               MahoPrivateCapability::kExtensionSnapshot)) {
    return nullptr;
  }
  return MahoExtensionStateBridgeFactory::GetForProfile(profile);
}

MahoExtensionStateBridge::MahoExtensionStateBridge(Profile* profile)
    : profile_(profile),
      profile_key_(MahoSpaceProfileBridge::GetInstance()->GetSpaceIdForProfile(profile)) {
  observation_.Observe(extensions::ExtensionRegistry::Get(profile_));
  PushFullSnapshot();

  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_register_extension_sync_callback_for_profile(
        core, profile_key_.c_str(), OnSyncCallback, this);
  }
}

MahoExtensionStateBridge::~MahoExtensionStateBridge() {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_clear_installed_extensions_for_profile(core, profile_key_.c_str());
    maho_core_register_extension_sync_callback_for_profile(
        core, profile_key_.c_str(), nullptr, nullptr);
  }
}

void MahoExtensionStateBridge::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void MahoExtensionStateBridge::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool MahoExtensionStateBridge::RequestEnable(const std::string& extension_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  auto* registrar = extensions::ExtensionRegistrar::Get(profile_);
  auto* policy = extensions::ExtensionSystem::Get(profile_)->management_policy();
  if (!registry || !registrar || !policy) {
    return false;
  }
  const extensions::Extension* ext =
      registry->GetExtensionById(extension_id,
                                 extensions::ExtensionRegistry::EVERYTHING);
  if (!ext) {
    return false;
  }
  std::u16string error;
  if (!policy->UserMayModifySettings(ext, &error) ||
      policy->MustRemainDisabled(ext, nullptr)) {
    return false;
  }
  registrar->EnableExtension(extension_id);
  return true;
}

bool MahoExtensionStateBridge::RequestDisable(const std::string& extension_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  auto* registrar = extensions::ExtensionRegistrar::Get(profile_);
  auto* policy = extensions::ExtensionSystem::Get(profile_)->management_policy();
  if (!registry || !registrar || !policy) {
    return false;
  }
  const extensions::Extension* ext =
      registry->GetExtensionById(extension_id,
                                 extensions::ExtensionRegistry::EVERYTHING);
  if (!ext) {
    return false;
  }
  std::u16string error;
  if (!policy->UserMayModifySettings(ext, &error) ||
      policy->MustRemainEnabled(ext, &error)) {
    return false;
  }
  registrar->DisableExtension(extension_id,
                              {extensions::disable_reason::DISABLE_USER_ACTION});
  return true;
}

bool MahoExtensionStateBridge::CanUninstall(
    const std::string& extension_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  auto* policy = extensions::ExtensionSystem::Get(profile_)->management_policy();
  if (!registry || !policy) {
    return false;
  }
  const extensions::Extension* ext =
      registry->GetExtensionById(extension_id,
                                 extensions::ExtensionRegistry::EVERYTHING);
  if (!ext) {
    return false;
  }
  std::u16string error;
  if (!policy->UserMayModifySettings(ext, &error) ||
      policy->MustRemainInstalled(ext, &error)) {
    return false;
  }
  return true;
}

void MahoExtensionStateBridge::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension) {
  PushFullSnapshot();
}

void MahoExtensionStateBridge::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  PushFullSnapshot();
}

void MahoExtensionStateBridge::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    bool is_update) {
  PushFullSnapshot();
}

void MahoExtensionStateBridge::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UninstallReason reason) {
  PushFullSnapshot();
}

void MahoExtensionStateBridge::PushFullSnapshot() {
  MahoCore* core = maho::GetCore();
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  if (core && registry) {
    base::ListValue list;
    auto append_set = [&list](const extensions::ExtensionSet& set, bool enabled) {
      for (const auto& ext : set) {
        if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*ext)) {
          continue;
        }
        base::DictValue dict;
        dict.Set("id", ext->id());
        dict.Set("name", ext->name());
        dict.Set("version", ext->version().GetString());
        dict.Set("description",
                 extensions::DescriptionInfo::GetDescription(*ext));
        dict.Set("enabled", enabled);
        dict.Set("manifestVersion", ext->manifest_version());

        base::ListValue permissions_list;
        for (const extensions::APIPermission* permission :
             ext->permissions_data()->active_permissions().apis()) {
          permissions_list.Append(permission->name());
        }
        dict.Set("permissions", std::move(permissions_list));

        list.Append(std::move(dict));
      }
    };
    append_set(registry->enabled_extensions(), true);
    append_set(registry->disabled_extensions(), false);

    base::DictValue envelope;
    envelope.Set("schema_version", 1);
    envelope.Set("extensions", std::move(list));

    std::string json;
    if (base::JSONWriter::Write(envelope, &json)) {
      maho_core_set_installed_extensions_for_profile(
          core, profile_key_.c_str(), json.c_str());
    }
  }

  // H5: observer notification must fire regardless of core/registry
  // availability, otherwise the location-bar extension grid goes stale whenever
  // core is not yet initialized.
  ScheduleObserverNotify();
}

void MahoExtensionStateBridge::ScheduleObserverNotify() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (notify_pending_) {
    return;
  }
  notify_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoExtensionStateBridge::NotifyObservers,
                     weak_factory_.GetWeakPtr()));
}

void MahoExtensionStateBridge::NotifyObservers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  notify_pending_ = false;
  for (auto& observer : observers_) {
    observer.OnInstalledExtensionsChanged();
  }
}

void MahoExtensionStateBridge::HandleIncomingSync(const std::string& extension_id,
                                                  bool enabled,
                                                  bool deleted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  if (!registry) {
    return;
  }

  if (deleted) {
    // Policy gate: CanUninstall only checks policy (does not uninstall);
    // skipping it here would let sync delete a force-installed extension.
    if (!CanUninstall(extension_id)) {
      return;
    }
    auto* registrar = extensions::ExtensionRegistrar::Get(profile_);
    if (registrar) {
      registrar->UninstallExtension(
          extension_id,
          extensions::UNINSTALL_REASON_SYNC,
          nullptr);
    }
    return;
  }

  bool currently_enabled = registry->enabled_extensions().Contains(extension_id);
  if (enabled && !currently_enabled) {
    RequestEnable(extension_id);
  } else if (!enabled && currently_enabled) {
    RequestDisable(extension_id);
  }
}

// static
void MahoExtensionStateBridge::OnSyncCallback(void* user_data,
                                              const char* extension_id,
                                              bool enabled,
                                              bool deleted) {
  MahoExtensionStateBridge* bridge = static_cast<MahoExtensionStateBridge*>(user_data);
  std::string id(extension_id);
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoExtensionStateBridge::HandleIncomingSync,
                     bridge->weak_factory_.GetWeakPtr(), id, enabled, deleted));
}

}  // namespace maho
