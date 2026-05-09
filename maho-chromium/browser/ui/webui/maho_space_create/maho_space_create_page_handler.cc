#include "maho/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"

namespace {

struct CreateResult {
  bool success = false;
  std::string space_id;
};

std::vector<maho_space_create::mojom::ProfileInfoPtr> BuildProfilesOnPool() {
  std::vector<maho_space_create::mojom::ProfileInfoPtr> profiles;
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

    auto profile = maho_space_create::mojom::ProfileInfo::New();
    profile->id = *id;
    profile->name = *name;
    profile->is_default = dict->FindBool("is_default").value_or(false);
    profiles.push_back(std::move(profile));
  }

  return profiles;
}

int32_t CountSpacesOnPool() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return 0;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return 0;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return 0;
  }
  return static_cast<int32_t>(parsed->GetList().size());
}

CreateResult CreateSpaceOnPool(const std::string& name,
                               const std::string& color,
                               const std::string& icon,
                               const std::string& profile_id,
                               const std::string& theme_json) {
  CreateResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return result;
  }

  base::DictValue color_dict;
  color_dict.Set("color", color);
  if (!icon.empty()) {
    color_dict.Set("icon", icon);
  }
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  const std::string resolved_profile_id =
      profile_id.empty() ? std::string("Default") : profile_id;
  char* ffi_result = maho_core_create_space(core, name.c_str(),
                                            color_json.c_str(),
                                            resolved_profile_id.c_str());
  if (ffi_result) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(ffi_result, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const std::string* parsed_id = parsed->GetDict().FindString("id");
      if (parsed_id) {
        result.space_id = *parsed_id;
        result.success = true;
        maho_core_activate_space(core, parsed_id->c_str());

        if (!theme_json.empty()) {
          std::optional<base::Value> theme_value =
              base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
          if (!theme_value) {
            maho_string_free(ffi_result);
            return result;
          }

          base::DictValue changes;
          changes.Set("spaceId", *parsed_id);
          changes.Set("theme", std::move(*theme_value));
          base::DictValue event;
          event.Set("kind", "update_space_config");
          event.Set("changes", std::move(changes));
          std::string event_json;
          base::JSONWriter::Write(event, &event_json);
          char* event_result = maho_core_handle_event(core, event_json.c_str());
          if (event_result) {
            maho_string_free(event_result);
          }
        }
      }
    }
    maho_string_free(ffi_result);
  }
  return result;
}

}  // namespace

MahoSpaceCreatePageHandler::MahoSpaceCreatePageHandler(
    mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_space_create::mojom::Page> page,
    Browser* browser,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      web_contents_(web_contents) {}

MahoSpaceCreatePageHandler::~MahoSpaceCreatePageHandler() = default;

void MahoSpaceCreatePageHandler::GetProfiles(GetProfilesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildProfilesOnPool),
      std::move(callback));
}

void MahoSpaceCreatePageHandler::GetSpaceCount(
    GetSpaceCountCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&CountSpacesOnPool),
      std::move(callback));
}

void MahoSpaceCreatePageHandler::CreateSpace(
    const std::string& name,
    const std::string& color,
    const std::string& icon,
    const std::optional<std::string>& profile_id,
    const std::optional<std::string>& theme_json,
    CreateSpaceCallback callback) {
  const std::string resolved_profile_id =
      profile_id.value_or(std::string("Default"));
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&CreateSpaceOnPool, name, color, icon,
                     resolved_profile_id, theme_json.value_or(std::string())),
      base::BindOnce(
          [](base::WeakPtr<MahoSpaceCreatePageHandler> weak_self,
             CreateSpaceCallback cb, std::string profile_id,
             CreateResult result) {
            if (weak_self && result.success) {
              auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
              bridge->RegisterSpace(result.space_id,
                                    base::FilePath::FromASCII(profile_id));
              MahoSpaceThemeState::UpdateFromCore();
              ui::ColorProviderManager::Get().ResetColorProviderCache();
            }
            std::move(cb).Run(result.success);
          },
          weak_factory_.GetWeakPtr(), std::move(callback),
          resolved_profile_id));
}

void MahoSpaceCreatePageHandler::CloseDialog() {
  if (web_contents_) {
    web_contents_->ClosePage();
  }
}
