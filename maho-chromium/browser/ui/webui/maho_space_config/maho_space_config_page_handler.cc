#include "maho/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/views/view_utils.h"

// static
maho_space_config::mojom::SpaceInfoPtr
MahoSpaceConfigPageHandler::ParseSpaceInfoFromDictForTesting(
    const base::DictValue& dict) {
  const std::string* id = dict.FindString("id");
  if (!id) {
    return nullptr;
  }

  auto info = maho_space_config::mojom::SpaceInfo::New();
  info->id = *id;

  const std::string* name = dict.FindString("name");
  info->name = name ? *name : std::string();

  const std::string* icon = dict.FindString("icon");
  info->icon = icon ? *icon : std::string();

  if (const base::DictValue* color = dict.FindDict("color")) {
    info->color =
        maho_theme::SpaceColorDictToHex(*color).value_or(std::string());
  } else if (const std::string* legacy_color = dict.FindString("color")) {
    info->color = *legacy_color;
  }

  if (const base::DictValue* theme = dict.FindDict("theme")) {
    info->theme_json = maho_theme::SerializeSpaceThemeJson(*theme);
  }

  const std::string* pid = dict.FindString("profileId");
  if (pid && !pid->empty()) {
    info->profile_id = *pid;
  }

  return info;
}

maho_space_config::mojom::ProfileInfoPtr
MahoSpaceConfigPageHandler::ParseProfileInfoFromDictForTesting(
    const base::DictValue& dict) {
  const std::string* id = dict.FindString("id");
  const std::string* name = dict.FindString("name");
  if (!id || !name) {
    return nullptr;
  }

  auto profile = maho_space_config::mojom::ProfileInfo::New();
  profile->id = *id;
  profile->name = *name;
  const base::Value* data_store_id = dict.Find("dataStoreId");
  profile->is_default = !data_store_id || data_store_id->is_none();
  return profile;
}

namespace {

struct ValidatedProfileUpdate {
  base::FilePath profile_basename;
  std::string profile_id;
  std::string event_json;
};

std::optional<ValidatedProfileUpdate> BuildValidatedProfileUpdate(
    const std::string& space_id,
    const std::optional<std::string>& profile_id) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  const uint64_t revision = bridge->GetProfileRegistryRevision();
  maho::ProfileRegistryPathResult resolved = bridge->ResolveProfilePath(
      profile_id.value_or("default"), revision);
  std::optional<std::string> canonical_id =
      bridge->CanonicalizeProfileId(profile_id.value_or("default"));
  if (resolved.error != maho::ProfileRegistryLookupError::kNone ||
      !resolved.path || !canonical_id) {
    return std::nullopt;
  }

  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("profileId", *canonical_id);
  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));
  std::string event_json;
  if (!base::JSONWriter::Write(event, &event_json)) {
    return std::nullopt;
  }
  return ValidatedProfileUpdate{.profile_basename = *resolved.path,
                                .profile_id = std::move(*canonical_id),
                                .event_json = std::move(event_json)};
}

bool CoreAcceptedSpaceConfigUpdate(const std::string& result_json,
                                   const std::string& expected_space_id,
                                   const std::string& expected_profile_id) {
  std::optional<base::Value> result =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  if (!result || !result->is_list()) {
    return false;
  }
  for (const base::Value& update : result->GetList()) {
    const base::DictValue* dict = update.GetIfDict();
    const std::string* kind = dict ? dict->FindString("kind") : nullptr;
    const base::DictValue* changes = dict ? dict->FindDict("changes") : nullptr;
    const std::string* space_id =
        changes ? changes->FindString("spaceId") : nullptr;
    const std::string* profile_id =
        changes ? changes->FindString("profileId") : nullptr;
    if (kind && *kind == "space_config_updated" && space_id &&
        *space_id == expected_space_id && profile_id &&
        *profile_id == expected_profile_id) {
      return true;
    }
  }
  return false;
}

std::optional<base::FilePath> UpdateProfileOnPool(
    const std::string& space_id,
    ValidatedProfileUpdate update) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::nullopt;
  }

  char* result = maho_core_handle_event(core, update.event_json.c_str());
  if (!result) {
    return std::nullopt;
  }
  const std::string result_json(result);
  maho_string_free(result);
  maho::InvalidateSidebarCoreCacheForUpdatesJson(result_json);
  if (!CoreAcceptedSpaceConfigUpdate(result_json, space_id,
                                     update.profile_id) ||
      maho_core_save_state(core) == 0) {
    return std::nullopt;
  }
  return update.profile_basename;
}

maho_space_config::mojom::SpaceInfoPtr LoadSpaceOnPool(
    const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return nullptr;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return nullptr;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return nullptr;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (!id || *id != space_id) {
      continue;
    }

    return MahoSpaceConfigPageHandler::ParseSpaceInfoFromDictForTesting(*dict);
  }

  return nullptr;
}

bool RenameSpaceOnPool(const std::string& space_id,
                       const std::string& name) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  maho_core_rename_space(core, space_id.c_str(), name.c_str());
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kFooter;
  maho::InvalidateSidebarCoreCache(invalidation);
  return true;
}

bool RecolorSpaceOnPool(const std::string& space_id,
                        const std::string& color_hex,
                        const std::string& theme_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }

  base::DictValue color_dict;
  color_dict.Set("color", color_hex);
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  maho_core_recolor_space(core, space_id.c_str(), color_json.c_str());
  maho::SidebarCacheInvalidation invalidation;
  invalidation.fragments = maho::SidebarCoreFragment::kFooter;
  maho::InvalidateSidebarCoreCache(invalidation);

  if (!theme_json.empty()) {
    std::optional<base::Value> theme_value =
        base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
    if (!theme_value) {
      return false;
    }

    base::DictValue changes;
    changes.Set("spaceId", space_id);
    changes.Set("theme", std::move(*theme_value));
    base::DictValue event;
    event.Set("kind", "update_space_config");
    event.Set("changes", std::move(changes));
    std::string event_json;
    base::JSONWriter::Write(event, &event_json);
    char* result = maho_core_handle_event(core, event_json.c_str());
    if (result) {
      std::string updates(result);
      maho_string_free(result);
      maho::InvalidateSidebarCoreCacheForUpdatesJson(updates);
    }
  }

  return true;
}

}  // namespace

std::optional<std::string>
MahoSpaceConfigPageHandler::BuildProfileUpdateEventJsonForTesting(
    const std::string& space_id,
    const std::optional<std::string>& profile_id) {
  std::optional<ValidatedProfileUpdate> update =
      BuildValidatedProfileUpdate(space_id, profile_id);
  return update ? std::optional<std::string>(std::move(update->event_json))
                : std::nullopt;
}

bool MahoSpaceConfigPageHandler::CoreAcceptedSpaceConfigUpdateForTesting(
    const std::string& result_json,
    const std::string& expected_space_id,
    const std::string& expected_profile_id) {
  return CoreAcceptedSpaceConfigUpdate(result_json, expected_space_id,
                                       expected_profile_id);
}

MahoSpaceConfigPageHandler::MahoSpaceConfigPageHandler(
    mojo::PendingReceiver<maho_space_config::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_space_config::mojom::Page> page,
    Browser* browser,
    content::WebContents* web_contents,
    const std::string& space_id,
    maho_space_config::mojom::InitialFocus initial_focus)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      web_contents_(web_contents),
      space_id_(space_id),
      initial_focus_(initial_focus) {
  // R-4 fail-closed: forged Mojo receiver bypasses config-level denial. Do not remove.
  if (web_contents &&
      !MahoIsWebUIEnabled(web_contents->GetBrowserContext())) {
    receiver_.reset();
    page_.reset();
  }
}

MahoSpaceConfigPageHandler::~MahoSpaceConfigPageHandler() = default;

void MahoSpaceConfigPageHandler::GetSpaceInfo(GetSpaceInfoCallback callback) {
  auto focus = initial_focus_;
  maho::PostCoreTask<maho_space_config::mojom::SpaceInfoPtr>(
      FROM_HERE,
      base::BindOnce(&LoadSpaceOnPool, space_id_),
      base::BindOnce(
          [](base::WeakPtr<MahoSpaceConfigPageHandler> weak_self,
             maho_space_config::mojom::InitialFocus focus,
             GetSpaceInfoCallback cb,
             maho_space_config::mojom::SpaceInfoPtr info) {
            if (weak_self && weak_self->cached_theme_json_.has_value() && info) {
              info->theme_json = weak_self->cached_theme_json_;
            }
            std::move(cb).Run(std::move(info), focus);
          },
          weak_factory_.GetWeakPtr(), focus, std::move(callback)));
}

void MahoSpaceConfigPageHandler::GetProfiles(GetProfilesCallback callback) {
  std::vector<maho_space_config::mojom::ProfileInfoPtr> profiles;
  const maho::ProfileCatalogResult& catalog =
      maho::MahoSpaceProfileBridge::GetInstance()->GetProfileCatalog();
  if (catalog.error != maho::ProfileCatalogError::kNone) {
    std::move(callback).Run(std::move(profiles));
    return;
  }
  for (const maho::ProfileRegistryRecord& record : catalog.records) {
    if (record.lifecycle != maho::ProfileLifecycleState::kReady) {
      continue;
    }
    auto profile = maho_space_config::mojom::ProfileInfo::New();
    profile->id = record.maho_id;
    profile->name = record.name;
    profile->is_default = record.is_default;
    profiles.push_back(std::move(profile));
  }
  std::move(callback).Run(std::move(profiles));
}

void MahoSpaceConfigPageHandler::UpdateName(
    const std::string& name,
    UpdateNameCallback callback) {
  maho::PostCoreTask<bool>(
      FROM_HERE,
      base::BindOnce(&RenameSpaceOnPool, space_id_, name),
      base::BindOnce(
          [](base::WeakPtr<MahoSpaceConfigPageHandler> weak_self,
             UpdateNameCallback cb, bool success) {
            if (weak_self && success) {
              weak_self->RefreshSidebar();
            }
            std::move(cb).Run(success);
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSpaceConfigPageHandler::UpdateIcon(
    const std::string& icon,
    UpdateIconCallback callback) {
  maho::PostCoreTask<bool>(
      FROM_HERE,
      base::BindOnce(
          [](const std::string& space_id, const std::string& icon) -> bool {
            MahoCore* core = maho::GetCore();
            if (!core) {
              return false;
            }

            base::DictValue changes;
            changes.Set("spaceId", space_id);
            changes.Set("icon", icon);
            base::DictValue event;
            event.Set("kind", "update_space_config");
            event.Set("changes", std::move(changes));

            std::string event_json;
            if (!base::JSONWriter::Write(event, &event_json)) {
              return false;
            }
            char* result = maho_core_handle_event(core, event_json.c_str());
            if (!result) {
              return false;
            }
            std::string updates(result);
            maho_string_free(result);
            maho::InvalidateSidebarCoreCacheForUpdatesJson(updates);
            maho_core_save_state(core);
            return true;
          },
          space_id_, icon),
      base::BindOnce(
          [](base::WeakPtr<MahoSpaceConfigPageHandler> weak_self,
             UpdateIconCallback cb, bool success) {
            if (weak_self && success) {
              weak_self->RefreshSidebar();
            }
            std::move(cb).Run(success);
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSpaceConfigPageHandler::UpdateColor(
    const std::string& color_hex,
    const std::optional<std::string>& theme_json,
    UpdateColorCallback callback) {
  const std::string theme_json_value = theme_json.value_or(std::string());
  maho::PostCoreTask<bool>(
      FROM_HERE,
      base::BindOnce(&RecolorSpaceOnPool, space_id_, color_hex,
                     theme_json_value),
      base::BindOnce(
          [](base::WeakPtr<MahoSpaceConfigPageHandler> weak_self,
             UpdateColorCallback cb, std::string theme_json_value,
             bool success) {
            if (weak_self && success) {
              weak_self->cached_theme_json_ = std::move(theme_json_value);
              MahoSpaceThemeState::ChangedSpaceIds changed_spaces =
                  MahoSpaceThemeState::UpdateFromCore();
              if (changed_spaces.empty()) {
                changed_spaces.push_back(weak_self->space_id_);
              }
              for (Browser* affected_browser :
                   maho::MahoSpaceProfileBridge::GetInstance()
                       ->GetBrowsersForSpaces(changed_spaces)) {
                maho::MahoSidebarView::RefreshSpaceThemeForBrowser(
                    affected_browser);
              }
            }
            std::move(cb).Run(success);
          },
          weak_factory_.GetWeakPtr(), std::move(callback), theme_json_value));
}

void MahoSpaceConfigPageHandler::UpdateProfile(
    const std::optional<std::string>& profile_id,
    UpdateProfileCallback callback) {
  const std::string space_id = space_id_;
  std::optional<ValidatedProfileUpdate> update =
      BuildValidatedProfileUpdate(space_id, profile_id);
  if (!update) {
    std::move(callback).Run(false);
    return;
  }
  maho::PostCoreTask<std::optional<base::FilePath>>(
      FROM_HERE,
      base::BindOnce(&UpdateProfileOnPool, space_id, std::move(*update)),
      base::BindOnce(
          [](const std::string& space_id,
             base::WeakPtr<MahoSpaceConfigPageHandler> weak_self,
             UpdateProfileCallback cb,
             std::optional<base::FilePath> profile_basename) {
            const bool success = profile_basename.has_value();
            if (success) {
              maho::MahoSpaceProfileBridge::GetInstance()->RegisterSpace(
                  space_id, *profile_basename);
              if (weak_self) {
                weak_self->RefreshSidebar();
              }
            }
            std::move(cb).Run(success);
          },
          space_id, weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSpaceConfigPageHandler::CloseDialog() {
  MahoSpaceConfigDialog::CloseActiveDialog();
}

void MahoSpaceConfigPageHandler::RefreshSidebar() {
  if (!browser_) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  views::View* container_raw = browser_view->maho_sidebar_container();
  if (!container_raw) {
    return;
  }
  auto* container = views::AsViewClass<maho::MahoSidebarContainerView>(container_raw);
  if (!container) {
    return;
  }
  views::View* sidebar_view_raw = container->sidebar_view();
  if (!sidebar_view_raw) {
    return;
  }
  auto* sidebar_view = views::AsViewClass<maho::MahoSidebarView>(sidebar_view_raw);
  if (!sidebar_view) {
    return;
  }
  sidebar_view->ScheduleRefreshAll();
}
