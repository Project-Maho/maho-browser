// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders_page_handler.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace {

constexpr char kPrefLiveFolders[] = "maho.live_folders";

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

  folders_[config.id] = config;
  SaveFolders();
  ScheduleRefresh(config.id);
  RefreshFolder(config.id);
  std::move(callback).Run(true);
}

void MahoLiveFolderPageHandler::RemoveFolder(const std::string& folder_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  folders_.erase(folder_id);
  cached_items_.erase(folder_id);
  active_loaders_.erase(folder_id);
  refresh_timers_.erase(folder_id);
  pending_callbacks_.erase(folder_id);
  SaveFolders();
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
    auto item = maho_live_folders::mojom::LiveFolderItem::New();
    item->id = "gcal_open";
    item->title = "Open Google Calendar";
    item->subtitle = "View your events";
    item->url = "https://calendar.google.com";
    item->icon_url = "";
    item->timestamp = 0;
    std::vector<maho_live_folders::mojom::LiveFolderItemPtr> items;
    items.push_back(std::move(item));
    cached_items_[folder.id] = std::move(items);

    std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
    for (const auto& ci : cached_items_[folder.id]) {
      cloned.push_back(ci->Clone());
    }
    page_->OnFolderUpdated(folder.id, std::move(cloned));
    return;
  }

  const std::string* calendar_id =
      config->GetDict().FindString("calendar_id");
  std::string cal = calendar_id ? *calendar_id : "primary";

  std::string url =
      "https://www.googleapis.com/calendar/v3/calendars/" + cal +
      "/events?maxResults=20&orderBy=startTime&singleEvents=true"
      "&timeMin=2026-01-01T00:00:00Z";

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

  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> items;
  if (body.has_value()) {
    items = ParseGitHubPRs(*body);
  }
  cached_items_[folder_id] = std::move(items);

  auto cb_it = pending_callbacks_.find(folder_id);
  if (cb_it != pending_callbacks_.end()) {
    std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
    for (const auto& item : cached_items_[folder_id]) {
      cloned.push_back(item->Clone());
    }
    std::move(cb_it->second).Run(std::move(cloned));
    pending_callbacks_.erase(cb_it);
  }

  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
  for (const auto& item : cached_items_[folder_id]) {
    cloned.push_back(item->Clone());
  }
  page_->OnFolderUpdated(folder_id, std::move(cloned));
}

void MahoLiveFolderPageHandler::OnGoogleCalendarResponse(
    const std::string& folder_id,
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_loaders_.erase(folder_id);

  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> items;
  if (body.has_value()) {
    items = ParseGoogleCalendarEvents(*body);
  }
  cached_items_[folder_id] = std::move(items);

  auto cb_it = pending_callbacks_.find(folder_id);
  if (cb_it != pending_callbacks_.end()) {
    std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
    for (const auto& item : cached_items_[folder_id]) {
      cloned.push_back(item->Clone());
    }
    std::move(cb_it->second).Run(std::move(cloned));
    pending_callbacks_.erase(cb_it);
  }

  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> cloned;
  for (const auto& item : cached_items_[folder_id]) {
    cloned.push_back(item->Clone());
  }
  page_->OnFolderUpdated(folder_id, std::move(cloned));
}

// static
std::vector<maho_live_folders::mojom::LiveFolderItemPtr>
MahoLiveFolderPageHandler::ParseGitHubPRs(const std::string& json) {
  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> items;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return items;
  }

  for (const auto& entry : parsed->GetList()) {
    const auto* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* title = dict->FindString("title");
    const std::string* html_url = dict->FindString("html_url");
    std::optional<int> number = dict->FindInt("number");
    if (!title || !html_url || !number) {
      continue;
    }

    std::string subtitle;
    const auto* user = dict->FindDict("user");
    if (user) {
      const std::string* login = user->FindString("login");
      if (login) {
        subtitle = *login;
      }
    }

    auto item = maho_live_folders::mojom::LiveFolderItem::New();
    item->id = "pr_" + base::NumberToString(*number);
    item->title = "#" + base::NumberToString(*number) + " " + *title;
    item->subtitle = subtitle;
    item->url = *html_url;
    item->icon_url = "";
    item->timestamp = 0;
    items.push_back(std::move(item));
  }
  return items;
}

// static
std::vector<maho_live_folders::mojom::LiveFolderItemPtr>
MahoLiveFolderPageHandler::ParseGoogleCalendarEvents(
    const std::string& json) {
  std::vector<maho_live_folders::mojom::LiveFolderItemPtr> items;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return items;
  }

  const auto* events_list = parsed->GetDict().FindList("items");
  if (!events_list) {
    return items;
  }

  int index = 0;
  for (const auto& entry : *events_list) {
    const auto* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* summary = dict->FindString("summary");
    const std::string* html_link = dict->FindString("htmlLink");
    if (!summary) {
      continue;
    }

    std::string subtitle;
    const auto* start = dict->FindDict("start");
    if (start) {
      const std::string* date_time = start->FindString("dateTime");
      const std::string* date = start->FindString("date");
      if (date_time) {
        subtitle = *date_time;
      } else if (date) {
        subtitle = *date;
      }
    }

    auto item = maho_live_folders::mojom::LiveFolderItem::New();
    item->id = "gcal_" + base::NumberToString(index++);
    item->title = *summary;
    item->subtitle = subtitle;
    item->url = html_link ? *html_link : "https://calendar.google.com";
    item->icon_url = "";
    item->timestamp = 0;
    items.push_back(std::move(item));
  }
  return items;
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
  profile_->GetPrefs()->SetList(kPrefLiveFolders, std::move(folder_list));
}

void MahoLiveFolderPageHandler::LoadFolders() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::ListValue& folder_list =
      profile_->GetPrefs()->GetList(kPrefLiveFolders);
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
