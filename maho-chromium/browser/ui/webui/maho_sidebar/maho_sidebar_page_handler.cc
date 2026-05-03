// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_sidebar/maho_sidebar_page_handler.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/color/color_provider_manager.h"
#include "url/gurl.h"

MahoSidebarPageHandler::MahoSidebarPageHandler(
    mojo::PendingReceiver<maho_sidebar::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_sidebar::mojom::Page> page,
    Browser* browser)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser) {
  browser_->tab_strip_model()->AddObserver(this);
}

MahoSidebarPageHandler::~MahoSidebarPageHandler() {
  browser_->tab_strip_model()->RemoveObserver(this);
}

std::vector<maho_sidebar::mojom::TabInfoPtr>
MahoSidebarPageHandler::BuildTabsList() {
  std::vector<maho_sidebar::mojom::TabInfoPtr> tabs;
  TabStripModel* model = browser_->tab_strip_model();
  int active_index = model->active_index();
  for (int i = 0; i < model->count(); ++i) {
    auto* wc = model->GetWebContentsAt(i);
    auto tab = maho_sidebar::mojom::TabInfo::New();
    tab->tab_id = i;
    tab->title = base::UTF16ToUTF8(wc->GetTitle());
    tab->url = wc->GetVisibleURL().spec();
    tab->active = (i == active_index);
    tab->index = i;
    tab->section = model->IsTabPinned(i) ? "pinned" : "today";
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

void MahoSidebarPageHandler::GetTabs(GetTabsCallback callback) {
  std::move(callback).Run(BuildTabsList());
}

void MahoSidebarPageHandler::ActivateTab(int32_t tab_id) {
  TabStripModel* model = browser_->tab_strip_model();
  if (tab_id >= 0 && tab_id < model->count()) {
    model->ActivateTabAt(tab_id);
  }
}

void MahoSidebarPageHandler::CloseTab(int32_t tab_id) {
  TabStripModel* model = browser_->tab_strip_model();
  if (tab_id >= 0 && tab_id < model->count()) {
    model->CloseWebContentsAt(tab_id, TabCloseTypes::CLOSE_USER_GESTURE);
  }
}

void MahoSidebarPageHandler::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  SendTabsUpdate();
}

void MahoSidebarPageHandler::SendTabsUpdate() {
  page_->TabsChanged(BuildTabsList());
}

// static
std::vector<maho_sidebar::mojom::SpaceInfoPtr>
MahoSidebarPageHandler::BuildSpacesOnPool() {
  std::vector<maho_sidebar::mojom::SpaceInfoPtr> spaces;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return spaces;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return spaces;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::string active_id;
  if (char* active_id_str = maho_core_get_active_space_id(core)) {
    std::optional<base::Value> parsed_active_id =
        base::JSONReader::Read(active_id_str, base::JSON_PARSE_RFC);
    active_id = parsed_active_id && parsed_active_id->is_string()
                    ? parsed_active_id->GetString()
                    : std::string(active_id_str);
    maho_string_free(active_id_str);
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return spaces;
  }

  int position = 0;
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    const std::string* color = dict->FindString("color");
    const std::string* icon = dict->FindString("icon");
    const std::string* profile_id = dict->FindString("profile_id");
    std::optional<int> tab_count = dict->FindInt("tab_count");
    if (!id || !name) {
      continue;
    }

    auto space = maho_sidebar::mojom::SpaceInfo::New();
    space->id = *id;
    space->name = *name;
    space->color = color ? *color : "#888888";
    space->icon = icon ? *icon : "";
    space->active = (*id == active_id);
    space->tab_count = tab_count.value_or(0);
    space->profile_id = profile_id ? *profile_id : "default";
    // last_active_tab_id is tracked on the UI thread by the bridge, not from core.
    // Leave as null here; the UI thread callback will populate it if needed.
    space->last_active_tab_id = std::nullopt;
    space->position = position++;
    spaces.push_back(std::move(space));
  }

  return spaces;
}

void MahoSidebarPageHandler::GetSpaces(GetSpacesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::BuildSpacesOnPool),
      base::BindOnce(&MahoSidebarPageHandler::OnSpacesResult,
                     weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void MahoSidebarPageHandler::OnSpacesResult(
    GetSpacesCallback callback,
    std::vector<maho_sidebar::mojom::SpaceInfoPtr> spaces) {
  std::move(callback).Run(std::move(spaces));
}

// static
void MahoSidebarPageHandler::ActivateSpaceOnPool(
    const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_activate_space(core, space_id.c_str());
}

void MahoSidebarPageHandler::ActivateSpace(const std::string& space_id) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  const std::string& old_space_id = bridge->GetActiveSpaceId();
  if (!old_space_id.empty()) {
    TabStripModel* model = browser_->tab_strip_model();
    int active_index = model->active_index();
    if (active_index >= 0) {
      bridge->SetLastActiveTab(old_space_id, active_index);
    }
  }

  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::ActivateSpaceOnPool, space_id),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceActivated,
                     weak_factory_.GetWeakPtr(), space_id));
}

// static
std::string MahoSidebarPageHandler::CreateSpaceOnPool(
    const std::string& name,
    const std::string& color,
    const std::string& icon,
    const std::string& profile_id,
    int32_t insert_after_index) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }

  base::DictValue color_dict;
  color_dict.Set("color", color);
  if (!icon.empty()) {
    color_dict.Set("icon", icon);
  }
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  const char* pid = profile_id.empty() ? nullptr : profile_id.c_str();
  char* result = maho_core_create_space(core, name.c_str(),
                                        color_json.c_str(), pid);
  std::string new_id;
  if (result) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(result, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const std::string* parsed_id = parsed->GetDict().FindString("id");
      if (parsed_id) {
        new_id = *parsed_id;
        if (insert_after_index >= 0) {
          maho_core_reorder_space(core, parsed_id->c_str(), 0,
                                  insert_after_index + 1);
        }
      }
    }
    maho_string_free(result);
  }
  return new_id;
}

void MahoSidebarPageHandler::CreateSpace(
    const std::string& name,
    const std::string& color,
    const std::string& icon,
    const std::optional<std::string>& profile_id,
    int32_t insert_after_index) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::CreateSpaceOnPool,
                     name, color, icon, profile_id.value_or(""),
                     insert_after_index),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceCreated,
                     weak_factory_.GetWeakPtr()));
}

// static
void MahoSidebarPageHandler::DeleteSpaceOnPool(const std::string& space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_delete_space(core, space_id.c_str());
}

void MahoSidebarPageHandler::DeleteSpace(const std::string& space_id) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::DeleteSpaceOnPool, space_id),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
void MahoSidebarPageHandler::RenameSpaceOnPool(const std::string& space_id,
                                                const std::string& new_name) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_rename_space(core, space_id.c_str(), new_name.c_str());
}

void MahoSidebarPageHandler::RenameSpace(const std::string& space_id,
                                          const std::string& new_name) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::RenameSpaceOnPool,
                     space_id, new_name),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
void MahoSidebarPageHandler::RecolorSpaceOnPool(
    const std::string& space_id,
    const std::string& new_color) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  base::DictValue color_dict;
  color_dict.Set("color", new_color);
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);
  maho_core_recolor_space(core, space_id.c_str(), color_json.c_str());
}

void MahoSidebarPageHandler::RecolorSpace(const std::string& space_id,
                                           const std::string& new_color) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::RecolorSpaceOnPool,
                     space_id, new_color),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
void MahoSidebarPageHandler::UpdateSpaceThemeOnPool(
    const std::string& space_id,
    const std::string& theme_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  std::optional<base::Value> theme_value =
      base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
  if (!theme_value) {
    return;
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

// static
void MahoSidebarPageHandler::ChangeSpaceIconOnPool(
    const std::string& space_id,
    const std::string& new_icon) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("icon", new_icon);
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

void MahoSidebarPageHandler::ChangeSpaceIcon(const std::string& space_id,
                                              const std::string& new_icon) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::ChangeSpaceIconOnPool,
                     space_id, new_icon),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarPageHandler::UpdateSpaceTheme(const std::string& space_id,
                                              const std::string& theme_json) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::UpdateSpaceThemeOnPool,
                     space_id, theme_json),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
void MahoSidebarPageHandler::ReorderSpaceOnPool(const std::string& space_id,
                                                 int32_t new_index) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_reorder_space(core, space_id.c_str(), 0,
                           static_cast<uintptr_t>(new_index));
}

void MahoSidebarPageHandler::ReorderSpace(const std::string& space_id,
                                           int32_t new_index) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::ReorderSpaceOnPool,
                     space_id, new_index),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
std::vector<maho_sidebar::mojom::ProfileInfoPtr>
MahoSidebarPageHandler::BuildProfilesOnPool() {
  std::vector<maho_sidebar::mojom::ProfileInfoPtr> profiles;
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
    std::optional<bool> is_default = dict->FindBool("is_default");
    if (!id || !name) {
      continue;
    }

    auto profile = maho_sidebar::mojom::ProfileInfo::New();
    profile->id = *id;
    profile->name = *name;
    profile->is_default = is_default.value_or(false);
    profiles.push_back(std::move(profile));
  }

  // Populate space_ids for each profile (N:M mapping)
  char* spaces_json_str = maho_core_get_space_view_models(core);
  if (spaces_json_str) {
    std::string spaces_json(spaces_json_str);
    maho_string_free(spaces_json_str);
    std::optional<base::Value> spaces_parsed =
        base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
    if (spaces_parsed && spaces_parsed->is_list()) {
      for (auto& profile : profiles) {
        for (const auto& space_item : spaces_parsed->GetList()) {
          const auto* space_dict = space_item.GetIfDict();
          if (!space_dict) {
            continue;
          }
          const std::string* space_id = space_dict->FindString("id");
          const std::string* space_profile = space_dict->FindString("profile_id");
          if (!space_id) {
            continue;
          }
          std::string effective_profile =
              (space_profile && !space_profile->empty()) ? *space_profile : "default";
          if (effective_profile == profile->id) {
            profile->space_ids.push_back(*space_id);
          }
        }
      }
    }
  }

  return profiles;
}

void MahoSidebarPageHandler::GetProfiles(GetProfilesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::BuildProfilesOnPool),
      base::BindOnce(&MahoSidebarPageHandler::OnProfilesResult,
                     weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void MahoSidebarPageHandler::OnProfilesResult(
    GetProfilesCallback callback,
    std::vector<maho_sidebar::mojom::ProfileInfoPtr> profiles) {
  std::move(callback).Run(std::move(profiles));
}

// static
void MahoSidebarPageHandler::AssignProfileOnPool(
    const std::string& space_id,
    const std::string& profile_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("profileId", profile_id);
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

void MahoSidebarPageHandler::AssignProfileToSpace(
    const std::string& space_id,
    const std::string& profile_id) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::AssignProfileOnPool,
                     space_id, profile_id),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
std::vector<maho_sidebar::mojom::ATCRulePtr>
MahoSidebarPageHandler::BuildATCRulesOnPool() {
  std::vector<maho_sidebar::mojom::ATCRulePtr> rules;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return rules;
  }

  char* json_str = maho_core_get_atc_rules(core);
  if (!json_str) {
    return rules;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* pattern = dict->FindString("url_pattern");
    const std::string* target = dict->FindString("target_space_id");
    std::optional<bool> enabled = dict->FindBool("enabled");
    if (!id || !pattern || !target) {
      continue;
    }

    auto rule = maho_sidebar::mojom::ATCRule::New();
    rule->id = *id;
    rule->url_pattern = *pattern;
    rule->target_space_id = *target;
    rule->enabled = enabled.value_or(true);
    rules.push_back(std::move(rule));
  }

  return rules;
}

void MahoSidebarPageHandler::GetATCRules(GetATCRulesCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::BuildATCRulesOnPool),
      base::BindOnce(&MahoSidebarPageHandler::OnATCRulesResult,
                     weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void MahoSidebarPageHandler::OnATCRulesResult(
    GetATCRulesCallback callback,
    std::vector<maho_sidebar::mojom::ATCRulePtr> rules) {
  std::move(callback).Run(std::move(rules));
}

// static
maho_sidebar::mojom::ATCRulePtr MahoSidebarPageHandler::AddATCRuleOnPool(
    const std::string& url_pattern,
    const std::string& target_space_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return nullptr;
  }

  base::DictValue rule_dict;
  rule_dict.Set("url_pattern", url_pattern);
  rule_dict.Set("target_space_id", target_space_id);
  std::string rule_json;
  base::JSONWriter::Write(rule_dict, &rule_json);

  char* result_str = maho_core_add_atc_rule(core, rule_json.c_str());
  if (!result_str) {
    return nullptr;
  }
  std::string result(result_str);
  maho_string_free(result_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(result, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return nullptr;
  }

  const auto& dict = parsed->GetDict();
  const std::string* id = dict.FindString("id");
  if (!id) {
    return nullptr;
  }

  auto rule = maho_sidebar::mojom::ATCRule::New();
  rule->id = *id;
  rule->url_pattern = url_pattern;
  rule->target_space_id = target_space_id;
  rule->enabled = true;
  return rule;
}

void MahoSidebarPageHandler::AddATCRule(const std::string& url_pattern,
                                         const std::string& target_space_id,
                                         AddATCRuleCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::AddATCRuleOnPool,
                     url_pattern, target_space_id),
      base::BindOnce(&MahoSidebarPageHandler::OnATCRuleAdded,
                     weak_factory_.GetWeakPtr(),
                     std::move(callback)));
}

void MahoSidebarPageHandler::OnATCRuleAdded(
    AddATCRuleCallback callback,
    maho_sidebar::mojom::ATCRulePtr rule) {
  std::move(callback).Run(std::move(rule));
  SendATCRulesUpdate();
}

// static
void MahoSidebarPageHandler::RemoveATCRuleOnPool(
    const std::string& rule_id) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  char* result = maho_core_remove_atc_rule(core, rule_id.c_str());
  if (result) {
    maho_string_free(result);
  }
}

void MahoSidebarPageHandler::RemoveATCRule(const std::string& rule_id) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::RemoveATCRuleOnPool, rule_id),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

// static
void MahoSidebarPageHandler::ToggleATCRuleOnPool(const std::string& rule_id,
                                                   bool enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_toggle_atc_rule(core, rule_id.c_str(), enabled);
}

void MahoSidebarPageHandler::ToggleATCRule(const std::string& rule_id,
                                            bool enabled) {
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::ToggleATCRuleOnPool,
                     rule_id, enabled),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarPageHandler::MoveTabToSpace(int32_t tab_id,
                                             const std::string& target_space_id,
                                             const std::string& section) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  base::DictValue event;
  event.Set("kind", "move_tab");
  event.Set("tab_id", tab_id);
  event.Set("target_space", target_space_id);
  event.Set("position", 0);
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);

  base::ThreadPool::PostTaskAndReply(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(
          [](std::string json) {
            MahoCore* c = maho::GetCore();
            if (!c) {
              return;
            }
            char* result = maho_core_handle_event(c, json.c_str());
            if (result) {
              maho_string_free(result);
            }
          },
          std::move(event_json)),
      base::BindOnce(&MahoSidebarPageHandler::OnSpaceMutated,
                     weak_factory_.GetWeakPtr()));
}

void MahoSidebarPageHandler::FetchThemeStore(FetchThemeStoreCallback callback) {
  if (!cached_theme_store_json_.empty() &&
      (base::TimeTicks::Now() - last_theme_store_fetch_) < base::Minutes(5)) {
    std::move(callback).Run(cached_theme_store_json_);
    return;
  }

  theme_store_loader_.reset();
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("https://themes.maho.app/manifest.json");
  request->method = "GET";
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_theme_store", R"(
        semantics {
          sender: "Maho Theme Store"
          description: "Fetches community theme manifest."
          trigger: "User opens the theme store panel."
          data: "No user data sent."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Not user-configurable."
        })");
  theme_store_loader_ = network::SimpleURLLoader::Create(
      std::move(request), traffic_annotation);
  theme_store_loader_->SetTimeoutDuration(base::Seconds(15));
  auto* profile = browser_->profile();
  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  theme_store_loader_->DownloadToString(
      url_loader_factory.get(),
      base::BindOnce(&MahoSidebarPageHandler::OnThemeStoreFetched,
                     weak_factory_.GetWeakPtr(), std::move(callback)),
      1024 * 1024);
}

void MahoSidebarPageHandler::OnSpaceMutated() {
  SendSpacesUpdate();
  SendTabsUpdate();
  SendProfilesUpdate();
  SendATCRulesUpdate();
  // Update space theme tint state and invalidate color cache.
  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();
}

void MahoSidebarPageHandler::OnThemeStoreFetched(
    FetchThemeStoreCallback callback,
    std::optional<std::string> body) {
  theme_store_loader_.reset();
  if (body) {
    cached_theme_store_json_ = *body;
    last_theme_store_fetch_ = base::TimeTicks::Now();
    std::move(callback).Run(*body);
  } else {
    std::move(callback).Run(cached_theme_store_json_);
  }
}

void MahoSidebarPageHandler::OnSpaceCreated(const std::string& new_space_id) {
  if (!new_space_id.empty()) {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    bridge->RegisterSpace(new_space_id, base::FilePath::FromASCII("Default"));
  }
  OnSpaceMutated();
}

void MahoSidebarPageHandler::OnSpaceActivated(const std::string& space_id) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  int last_tab = bridge->GetLastActiveTab(space_id);
  if (last_tab >= 0) {
    TabStripModel* model = browser_->tab_strip_model();
    if (last_tab < model->count()) {
      model->ActivateTabAt(last_tab);
    }
  }
  OnSpaceMutated();
}

void MahoSidebarPageHandler::SendSpacesUpdate() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::BuildSpacesOnPool),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarPageHandler> handler,
             std::vector<maho_sidebar::mojom::SpaceInfoPtr> spaces) {
            if (handler) {
              handler->page_->SpacesChanged(std::move(spaces));
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarPageHandler::SendProfilesUpdate() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::BuildProfilesOnPool),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarPageHandler> handler,
             std::vector<maho_sidebar::mojom::ProfileInfoPtr> profiles) {
            if (handler) {
              handler->page_->ProfilesChanged(std::move(profiles));
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarPageHandler::SendATCRulesUpdate() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&MahoSidebarPageHandler::BuildATCRulesOnPool),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarPageHandler> handler,
             std::vector<maho_sidebar::mojom::ATCRulePtr> rules) {
            if (handler) {
              handler->page_->ATCRulesChanged(std::move(rules));
            }
          },
          weak_factory_.GetWeakPtr()));
}
