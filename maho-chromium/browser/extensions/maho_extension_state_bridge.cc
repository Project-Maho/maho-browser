// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/maho_extension_state_bridge.h"

#include <string>

#include "base/containers/flat_map.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/ui_util.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/permissions/permission_set.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "content/public/browser/browser_thread.h"

namespace maho {

namespace {

base::flat_map<Profile*, MahoExtensionStateBridge*>& GetInstanceMap() {
  static base::NoDestructor<base::flat_map<Profile*, MahoExtensionStateBridge*>>
      s_map;
  return *s_map;
}

}  // namespace

// static
MahoExtensionStateBridge* MahoExtensionStateBridge::FromProfile(
    Profile* profile) {
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

MahoExtensionStateBridge::MahoExtensionStateBridge(Profile* profile)
    : profile_(profile) {
  GetInstanceMap()[profile_] = this;
  observation_.Observe(extensions::ExtensionRegistry::Get(profile_));
  PushFullSnapshot();

  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_register_extension_sync_callback(core, OnSyncCallback, this);
  }
}

MahoExtensionStateBridge::~MahoExtensionStateBridge() {
  GetInstanceMap().erase(profile_);
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_clear_installed_extensions(core);
    maho_core_register_extension_sync_callback(core, nullptr, nullptr);
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

bool MahoExtensionStateBridge::RequestUninstall(
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
  if (!core) {
    return;
  }

  auto* registry = extensions::ExtensionRegistry::Get(profile_);
  if (!registry) {
    return;
  }

  base::ListValue list;

  // Process Enabled Extensions
  for (const auto& ext : registry->enabled_extensions()) {
    if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*ext)) {
      continue;
    }
    base::DictValue dict;
    dict.Set("id", ext->id());
    dict.Set("name", ext->name());
    dict.Set("version", ext->version().GetString());
    dict.Set("description", ext->description());
    dict.Set("enabled", true);
    dict.Set("manifestVersion", ext->manifest_version());

    base::ListValue permissions_list;
    for (const extensions::APIPermission* permission : ext->permissions_data()->active_permissions().apis()) {
      permissions_list.Append(permission->name());
    }
    dict.Set("permissions", std::move(permissions_list));

    list.Append(std::move(dict));
  }

  // Process Disabled Extensions
  for (const auto& ext : registry->disabled_extensions()) {
    if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*ext)) {
      continue;
    }
    base::DictValue dict;
    dict.Set("id", ext->id());
    dict.Set("name", ext->name());
    dict.Set("version", ext->version().GetString());
    dict.Set("description", ext->description());
    dict.Set("enabled", false);
    dict.Set("manifestVersion", ext->manifest_version());

    base::ListValue permissions_list;
    for (const extensions::APIPermission* permission : ext->permissions_data()->active_permissions().apis()) {
      permissions_list.Append(permission->name());
    }
    dict.Set("permissions", std::move(permissions_list));

    list.Append(std::move(dict));
  }

  base::DictValue envelope;
  envelope.Set("schema_version", 1);
  envelope.Set("extensions", std::move(list));

  std::string json;
  if (base::JSONWriter::Write(envelope, &json)) {
    maho_core_set_installed_extensions(core, json.c_str());
  }

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
