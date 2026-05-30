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
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

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
}

MahoExtensionStateBridge::~MahoExtensionStateBridge() {
  GetInstanceMap().erase(profile_);
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_clear_installed_extensions(core);
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
    list.Append(std::move(dict));
  }

  std::string json;
  if (base::JSONWriter::Write(list, &json)) {
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

}  // namespace maho
