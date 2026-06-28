#include "maho/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h"

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
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/view_utils.h"

namespace {

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

    auto info = maho_space_config::mojom::SpaceInfo::New();
    info->id = *id;

    const std::string* name = dict->FindString("name");
    info->name = name ? *name : std::string();

    const std::string* icon = dict->FindString("icon");
    info->icon = icon ? *icon : std::string();

    const std::string* color = dict->FindString("color");
    info->color = color ? *color : std::string();

    if (const std::string* theme_json = dict->FindString("theme_json")) {
      info->theme_json = *theme_json;
    }

    const std::string* pid = dict->FindString("profile_id");
    if (pid && !pid->empty()) {
      info->profile_id = *pid;
    }

    return info;
  }

  return nullptr;
}

std::vector<maho_space_config::mojom::ProfileInfoPtr> BuildProfilesOnPool() {
  std::vector<maho_space_config::mojom::ProfileInfoPtr> profiles;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return profiles;
  }

  char* json_str = maho_core_list_profiles(core);
  if (!json_str) {
    return profiles;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return profiles;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id || !name) {
      continue;
    }

    auto profile = maho_space_config::mojom::ProfileInfo::New();
    profile->id = *id;
    profile->name = *name;
    profile->is_default = dict->FindBool("is_default").value_or(false);
    profiles.push_back(std::move(profile));
  }

  return profiles;
}

bool RenameSpaceOnPool(const std::string& space_id,
                       const std::string& name) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return false;
  }
  maho_core_rename_space(core, space_id.c_str(), name.c_str());
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
      maho_string_free(result);
    }
  }

  return true;
}

}  // namespace

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
      initial_focus_(initial_focus) {}

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
  maho::PostCoreTask<std::vector<maho_space_config::mojom::ProfileInfoPtr>>(
      FROM_HERE,
      base::BindOnce(&BuildProfilesOnPool),
      std::move(callback));
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
            maho_string_free(result);
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
              MahoSpaceThemeState::UpdateFromCore();
              ui::ColorProviderManager::Get().ResetColorProviderCache();
            }
            std::move(cb).Run(success);
          },
          weak_factory_.GetWeakPtr(), std::move(callback), theme_json_value));
}

void MahoSpaceConfigPageHandler::UpdateProfile(
    const std::optional<std::string>& profile_id,
    UpdateProfileCallback callback) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (bridge && profile_id.has_value()) {
    bridge->RegisterSpace(space_id_,
                          base::FilePath::FromASCII(profile_id.value()));
  }
  std::move(callback).Run(true);
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
