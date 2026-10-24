// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders_page_handler.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace {

constexpr net::NetworkTrafficAnnotationTag kGitHubTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_live_folder_github", R"(
      semantics {
        sender: "Maho Live Folders"
        description: "Fetches open pull requests from GitHub for sidebar display."
        trigger: "User configures a GitHub PR live folder."
        data: "GitHub API token (user-provided) and repository name."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "User can remove live folders in sidebar settings."
      })");

constexpr net::NetworkTrafficAnnotationTag kGoogleCalendarTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_live_folder_gcal", R"(
      semantics {
        sender: "Maho Live Folders"
        description: "Fetches upcoming events from Google Calendar for sidebar display."
        trigger: "User configures a Google Calendar live folder."
        data: "Google OAuth token (user-provided) and calendar ID."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "User can remove live folders in sidebar settings."
      })");

constexpr base::TimeDelta kGitHubRefreshInterval = base::Minutes(5);
constexpr base::TimeDelta kGCalRefreshInterval = base::Minutes(2);
constexpr size_t kMaxResponseSize = 1024 * 1024;

}  // namespace

namespace {

// Returns the core folder_id for a folder whose configJson matches
// |config_json| in |space_id|, or empty string if not found.
// Uses maho_core_get_folder_view_models FFI to query the Rust model.
std::string FindCoreFolderIdByConfig(const std::string& space_id,
                                     const std::string& config_json) {
  MahoCore* core = maho::GetCore();
  if (!core || space_id.empty() || config_json.empty()) {
    return std::string();
  }
  std::string space_id_json = "\"" + space_id + "\"";
  char* json_str =
      maho_core_get_folder_view_models(core, space_id_json.c_str());
  if (!json_str) {
    return std::string();
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::string();
  }
  for (const auto& entry : parsed->GetList()) {
    const auto* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* cfg = dict->FindString("configJson");
    if (cfg && *cfg == config_json) {
      const std::string* id = dict->FindString("id");
      return id ? *id : std::string();
    }
  }
  return std::string();
}

}  // namespace

MahoLiveFolderPageHandler::FolderConfig::FolderConfig() = default;
MahoLiveFolderPageHandler::FolderConfig::FolderConfig(const FolderConfig&) =
    default;
MahoLiveFolderPageHandler::FolderConfig&
MahoLiveFolderPageHandler::FolderConfig::operator=(const FolderConfig&) =
    default;
MahoLiveFolderPageHandler::FolderConfig::~FolderConfig() = default;

MahoLiveFolderPageHandler::MahoLiveFolderPageHandler(
    mojo::PendingReceiver<maho_live_folders::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_live_folders::mojom::Page> page,
    Profile* profile)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile),
      url_loader_factory_(profile->GetURLLoaderFactory()) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // R-4 fail-closed: forged Mojo receiver bypasses config-level denial. Do not remove.
  if (!MahoIsWebUIEnabled(profile)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  LoadFolders();
  for (const auto& [id, config] : folders_) {
    ScheduleRefresh(id);
    RefreshFolder(id);
  }
}

MahoLiveFolderPageHandler::~MahoLiveFolderPageHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoLiveFolderPageHandler::GetFolders(GetFoldersCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<maho_live_folders::mojom::LiveFolderPtr> result;
  for (const auto& [id, config] : folders_) {
    auto folder = maho_live_folders::mojom::LiveFolder::New();
    folder->id = config.id;
    folder->name = config.name;
    folder->provider_type = config.provider_type;
    folder->config_json = config.config_json;
    result.push_back(std::move(folder));
  }
  std::move(callback).Run(std::move(result));
}

void MahoLiveFolderPageHandler::GetFolderItems(
    const std::string& folder_id,
    GetFolderItemsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = cached_items_.find(folder_id);
  if (it != cached_items_.end()) {
    std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
    for (const auto& item : it->second) {
      cloned.push_back(item->Clone());
    }
    std::move(callback).Run(std::move(cloned));
    return;
  }
  pending_callbacks_[folder_id] = std::move(callback);
  RefreshFolder(folder_id);
}

void MahoLiveFolderPageHandler::AddFolder(const std::string& provider_type,
                                           const std::string& config_json,
                                           AddFolderCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(false);
    return;
  }

  FolderConfig config;
  config.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  config.provider_type = provider_type;
  config.config_json = config_json;

  const std::string* name = parsed->GetDict().FindString("name");
  if (name) {
    config.name = *name;
  } else if (provider_type == "github_prs") {
    const std::string* repo = parsed->GetDict().FindString("repo");
    config.name = repo ? ("GitHub: " + *repo) : "GitHub PRs";
  } else if (provider_type == "google_calendar") {
    config.name = "Google Calendar";
  } else {
    config.name = "Live Folder";
  }

  // Register in Rust core so the folder appears in the sidebar tree and
  // toggle_folder_expanded can operate on it.
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    const std::string& space_id = bridge->GetActiveSpaceId();
    if (!space_id.empty()) {
      maho::PostCoreClosure(
          FROM_HERE,
          base::BindOnce(
              [](const std::string& space_id, const std::string& name,
                 const std::string& provider_type,
                 const std::string& config_json) {
                std::string existing_core_id =
                    FindCoreFolderIdByConfig(space_id, config_json);
                if (existing_core_id.empty()) {
                  maho::DispatchShellEventEx("create_folder", {
                      maho::ShellEventField("space_id", space_id),
                      maho::ShellEventField("name", name),
                      maho::ShellEventField("is_pinned", false),
                      maho::ShellEventField("provider_type", provider_type),
                      maho::ShellEventField("config_json", config_json)});
                }
              },
              space_id, config.name, provider_type, config_json));
    }
  }

  folders_[config.id] = config;
  SaveFolders();
  ScheduleRefresh(config.id);
  RefreshFolder(config.id);
  std::move(callback).Run(true);
}

void MahoLiveFolderPageHandler::RemoveFolder(const std::string& folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Capture config_json before erasing so we can find the core folder.
  std::string config_json;
  auto it = folders_.find(folder_id);
  if (it != folders_.end()) {
    config_json = it->second.config_json;
  }

  folders_.erase(folder_id);
  cached_items_.erase(folder_id);
  active_loaders_.erase(folder_id);
  refresh_timers_.erase(folder_id);
  pending_callbacks_.erase(folder_id);
  SaveFolders();

  // Also remove the corresponding Folder from Rust core so it doesn't
  // linger as an orphan in the sidebar tree.
  if (!config_json.empty()) {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    if (bridge) {
      const std::string& space_id = bridge->GetActiveSpaceId();
      if (!space_id.empty()) {
        maho::PostCoreClosure(
            FROM_HERE,
            base::BindOnce(
                [](const std::string& space_id,
                   const std::string& config_json) {
                  std::string core_folder_id =
                      FindCoreFolderIdByConfig(space_id, config_json);
                  if (!core_folder_id.empty()) {
                    maho::DispatchShellEvent("delete_folder", {
                        {"space_id", space_id},
                        {"folder_id", core_folder_id}});
                  }
                },
                space_id, config_json));
      }
    }
  }

  // Clear any cached items for this config_json from the item cache.
  if (!config_json.empty()) {
    maho::LiveFolderItemCache::GetInstance().Clear(config_json);
  }
}

void MahoLiveFolderPageHandler::RefreshFolder(const std::string& folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = folders_.find(folder_id);
  if (it == folders_.end()) {
    return;
  }
  const FolderConfig& config = it->second;
  if (config.provider_type == "github_prs") {
    FetchGitHubPRs(config);
  } else if (config.provider_type == "google_calendar") {
    FetchGoogleCalendarEvents(config);
  }
}

void MahoLiveFolderPageHandler::FetchGitHubPRs(const FolderConfig& folder) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::optional<base::Value> config =
      base::JSONReader::Read(folder.config_json, base::JSON_PARSE_RFC);
  if (!config || !config->is_dict()) {
    return;
  }
  const std::string* repo = config->GetDict().FindString("repo");
  if (!repo || repo->empty()) {
    return;
  }

  std::string url =
      "https://api.github.com/repos/" + *repo + "/pulls?state=open&per_page=20";

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url);
  request->method = "GET";
  request->headers.SetHeader("Accept", "application/vnd.github.v3+json");

  const std::string* token = config->GetDict().FindString("token");
  if (token && !token->empty()) {
    request->headers.SetHeader("Authorization", "Bearer " + *token);
  }

  auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                  kGitHubTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(30));
  auto* loader_ptr = loader.get();
  active_loaders_[folder.id] = std::move(loader);

  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&MahoLiveFolderPageHandler::OnGitHubResponse,
                     weak_factory_.GetWeakPtr(), folder.id),
      kMaxResponseSize);
}

void MahoLiveFolderPageHandler::FetchGoogleCalendarEvents(
    const FolderConfig& folder) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::optional<base::Value> config =
      base::JSONReader::Read(folder.config_json, base::JSON_PARSE_RFC);
  if (!config || !config->is_dict()) {
    return;
  }

  const std::string* token = config->GetDict().FindString("token");
  if (!token || token->empty()) {
    ApplyFetchResult(folder.id,
                     maho::LiveFolderGoogleCalendarPlaceholderItems());
    return;
  }

  const std::string* calendar_id =
      config->GetDict().FindString("calendar_id");
  std::string cal = calendar_id ? *calendar_id : "primary";

  std::string url =
      maho::LiveFolderGoogleCalendarEventsUrl(cal, base::Time::Now());

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url);
  request->method = "GET";
  request->headers.SetHeader("Authorization", "Bearer " + *token);

  auto loader = network::SimpleURLLoader::Create(
      std::move(request), kGoogleCalendarTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(30));
  auto* loader_ptr = loader.get();
  active_loaders_[folder.id] = std::move(loader);

  loader_ptr->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&MahoLiveFolderPageHandler::OnGoogleCalendarResponse,
                     weak_factory_.GetWeakPtr(), folder.id),
      kMaxResponseSize);
}

void MahoLiveFolderPageHandler::OnGitHubResponse(
    const std::string& folder_id,
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_loaders_.erase(folder_id);
  ApplyFetchResult(folder_id, body.has_value()
                                  ? maho::ParseLiveFolderGitHubPRs(*body)
                                  : std::nullopt);
}

void MahoLiveFolderPageHandler::OnGoogleCalendarResponse(
    const std::string& folder_id,
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_loaders_.erase(folder_id);
  ApplyFetchResult(folder_id,
                   body.has_value()
                       ? maho::ParseLiveFolderGoogleCalendarEvents(*body)
                       : std::nullopt);
}

void MahoLiveFolderPageHandler::ApplyFetchResult(
    const std::string& folder_id,
    std::optional<std::vector<maho::LiveFolderCachedItem>> fetched) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto folder_it = folders_.find(folder_id);
  if (folder_it == folders_.end()) {
    pending_callbacks_.erase(folder_id);
    return;
  }

  if (fetched.has_value()) {
    std::vector<maho_live_folders::mojom::LiveFolderItemPtr> items;
    for (const auto& cached : *fetched) {
      auto item = maho_live_folders::mojom::LiveFolderItem::New();
      item->id = cached.id;
      item->title = cached.title;
      item->subtitle = cached.subtitle;
      item->url = cached.url;
      item->icon_url = "";
      item->timestamp = 0;
      items.push_back(std::move(item));
    }
    cached_items_[folder_id] = std::move(items);

    // Same items as the cache warmer writes, so the sidebar tree does not
    // change depending on which writer refreshed last.
    maho::LiveFolderItemCache::GetInstance().SetItems(
        folder_it->second.config_json, std::move(*fetched));
    if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
      bridge->NotifyChanged();
    }
  } else {
    // Network/HTTP failure or malformed body: keep the previous items and do
    // not mark the shared cache fresh, so the next tick retries.
    LOG(WARNING) << "maho-live-folders: refresh failed for folder "
                 << folder_id << "; keeping previous items";
  }

  auto cb_it = pending_callbacks_.find(folder_id);
  if (cb_it != pending_callbacks_.end()) {
    GetFolderItemsCallback callback = std::move(cb_it->second);
    pending_callbacks_.erase(cb_it);
    std::move(callback).Run(CloneCachedItems(folder_id));
  }

  if (fetched.has_value()) {
    page_->OnFolderUpdated(folder_id, CloneCachedItems(folder_id));
  }
}

std::vector<maho_live_folders::mojom::LiveFolderItemPtr>
MahoLiveFolderPageHandler::CloneCachedItems(
    const std::string& folder_id) const {
  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
  auto it = cached_items_.find(folder_id);
  if (it != cached_items_.end()) {
    for (const auto& item : it->second) {
      cloned.push_back(item->Clone());
    }
  }
  return cloned;
}

void MahoLiveFolderPageHandler::SaveFolders() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::ListValue folder_list;
  for (const auto& [id, config] : folders_) {
    base::DictValue dict;
    dict.Set("id", config.id);
    dict.Set("name", config.name);
    dict.Set("provider_type", config.provider_type);
    dict.Set("config_json", config.config_json);
    folder_list.Append(std::move(dict));
  }
  profile_->GetPrefs()->SetList(maho::sidebar_prefs::kLiveFolders, std::move(folder_list));
}

void MahoLiveFolderPageHandler::LoadFolders() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::ListValue& folder_list =
      profile_->GetPrefs()->GetList(maho::sidebar_prefs::kLiveFolders);
  for (const auto& entry : folder_list) {
    const auto* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    FolderConfig config;
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    const std::string* provider_type = dict->FindString("provider_type");
    const std::string* config_json = dict->FindString("config_json");
    if (!id || !provider_type) {
      continue;
    }
    config.id = *id;
    config.name = name ? *name : "Live Folder";
    config.provider_type = *provider_type;
    config.config_json = config_json ? *config_json : "{}";
    folders_[config.id] = config;
  }

  // Reconcile: ensure every pref-loaded live folder has a corresponding
  // Folder object in the Rust core model. Existing users who saved live
  // folders before the core integration will have prefs but no core folder.
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return;
  }
  const std::string& space_id = bridge->GetActiveSpaceId();
  if (space_id.empty()) {
    return;
  }

  // Collect folder metadata for core-sequence reconciliation.
  struct FolderMeta {
    std::string name;
    std::string provider_type;
    std::string config_json;
  };
  std::vector<FolderMeta> metas;
  metas.reserve(folders_.size());
  for (const auto& [id, config] : folders_) {
    metas.push_back({config.name, config.provider_type, config.config_json});
  }

  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(
          [](const std::string& space_id, std::vector<FolderMeta> metas) {
            for (const auto& meta : metas) {
              std::string existing_id =
                  FindCoreFolderIdByConfig(space_id, meta.config_json);
              if (!existing_id.empty()) {
                continue;
              }
              maho::DispatchShellEventEx("create_folder", {
                  maho::ShellEventField("space_id", space_id),
                  maho::ShellEventField("name", meta.name),
                  maho::ShellEventField("is_pinned", false),
                  maho::ShellEventField("provider_type", meta.provider_type),
                  maho::ShellEventField("config_json", meta.config_json)});
            }
          },
          space_id, std::move(metas)));
}

void MahoLiveFolderPageHandler::ScheduleRefresh(
    const std::string& folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = folders_.find(folder_id);
  if (it == folders_.end()) {
    return;
  }

  base::TimeDelta interval = kGitHubRefreshInterval;
  if (it->second.provider_type == "google_calendar") {
    interval = kGCalRefreshInterval;
  }

  auto timer = std::make_unique<base::RepeatingTimer>();
  timer->Start(FROM_HERE, interval,
               base::BindRepeating(&MahoLiveFolderPageHandler::RefreshFolder,
                                   weak_factory_.GetWeakPtr(), folder_id));
  refresh_timers_[folder_id] = std::move(timer);
}
