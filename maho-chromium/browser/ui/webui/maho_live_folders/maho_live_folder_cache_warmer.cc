// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_cache_warmer.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr size_t kMaxResponseSize = 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kWarmGitHubAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_live_folder_warm_github", R"(
      semantics {
        sender: "Maho Live Folders Cache Warmer"
        description: "Pre-fetches open pull requests from GitHub at startup for sidebar display."
        trigger: "Browser startup with configured GitHub PR live folders."
        data: "GitHub API token (user-provided) and repository name."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: NO
        setting: "User can remove live folders in sidebar settings."
      })");

constexpr net::NetworkTrafficAnnotationTag kWarmGCalAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_live_folder_warm_gcal", R"(
      semantics {
        sender: "Maho Live Folders Cache Warmer"
        description: "Pre-fetches calendar events from Google Calendar at startup for sidebar display."
        trigger: "Browser startup with configured Google Calendar live folders."
        data: "Google OAuth token (user-provided) and calendar ID."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "User can remove live folders in sidebar settings."
      })");

// Self-owned instance that fires fetches and self-destructs when done.
class CacheWarmerImpl : public base::RefCounted<CacheWarmerImpl> {
 public:
  CacheWarmerImpl(scoped_refptr<network::SharedURLLoaderFactory> factory)
      : url_loader_factory_(std::move(factory)) {}

  void Start(const base::ListValue& folder_list) {
    for (const auto& entry : folder_list) {
      const auto* dict = entry.GetIfDict();
      if (!dict) {
        continue;
      }
      const std::string* provider_type = dict->FindString("provider_type");
      const std::string* config_json = dict->FindString("config_json");
      if (!provider_type || !config_json) {
        continue;
      }

      // Skip if cache already has items for this config.
      if (LiveFolderItemCache::GetInstance().GetItems(*config_json)) {
        continue;
      }

      if (*provider_type == "github_prs") {
        FetchGitHub(*config_json);
      } else if (*provider_type == "google_calendar") {
        FetchGoogleCalendar(*config_json);
      }
    }
  }

 private:
  friend class base::RefCounted<CacheWarmerImpl>;
  ~CacheWarmerImpl() = default;

  void FetchGitHub(const std::string& config_json) {
    std::optional<base::Value> config =
        base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
    if (!config || !config->is_dict()) {
      return;
    }
    const std::string* repo = config->GetDict().FindString("repo");
    if (!repo || repo->empty()) {
      return;
    }

    std::string url =
        "https://api.github.com/repos/" + *repo +
        "/pulls?state=open&per_page=20";

    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(url);
    request->method = "GET";
    request->headers.SetHeader("Accept", "application/vnd.github.v3+json");

    const std::string* token = config->GetDict().FindString("token");
    if (token && !token->empty()) {
      request->headers.SetHeader("Authorization", "Bearer " + *token);
    }

    auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                   kWarmGitHubAnnotation);
    loader->SetTimeoutDuration(base::Seconds(30));
    auto* loader_ptr = loader.get();
    active_loaders_.push_back(std::move(loader));

    loader_ptr->DownloadToString(
        url_loader_factory_.get(),
        base::BindOnce(&CacheWarmerImpl::OnGitHubResponse, this, config_json),
        kMaxResponseSize);
  }

  void FetchGoogleCalendar(const std::string& config_json) {
    std::optional<base::Value> config =
        base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
    if (!config || !config->is_dict()) {
      return;
    }

    const std::string* token = config->GetDict().FindString("token");
    if (!token || token->empty()) {
      // No token — insert a placeholder item like the page handler does.
      std::vector<LiveFolderCachedItem> items;
      LiveFolderCachedItem ci;
      ci.id = "gcal_open";
      ci.title = "Open Google Calendar";
      ci.subtitle = "View your events";
      ci.url = "https://calendar.google.com";
      items.push_back(std::move(ci));
      LiveFolderItemCache::GetInstance().SetItems(config_json, std::move(items));
      NotifySidebar();
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

    auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                   kWarmGCalAnnotation);
    loader->SetTimeoutDuration(base::Seconds(30));
    auto* loader_ptr = loader.get();
    active_loaders_.push_back(std::move(loader));

    loader_ptr->DownloadToString(
        url_loader_factory_.get(),
        base::BindOnce(&CacheWarmerImpl::OnGoogleCalendarResponse, this,
                       config_json),
        kMaxResponseSize);
  }

  void OnGitHubResponse(const std::string& config_json,
                        std::optional<std::string> body) {
    if (!body.has_value()) {
      return;
    }

    std::optional<base::Value> parsed =
        base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_list()) {
      return;
    }

    std::vector<LiveFolderCachedItem> items;
    for (const auto& entry : parsed->GetList()) {
      const auto* dict = entry.GetIfDict();
      if (!dict) {
        continue;
      }
      LiveFolderCachedItem ci;
      const std::string* title = dict->FindString("title");
      ci.title = title ? *title : "Untitled PR";

      const auto* user_dict = dict->FindDict("user");
      if (user_dict) {
        const std::string* login = user_dict->FindString("login");
        ci.subtitle = login ? *login : "";
      }

      const std::string* html_url = dict->FindString("html_url");
      ci.url = html_url ? *html_url : "";

      std::optional<int> number = dict->FindInt("number");
      ci.id = number ? "pr_" + base::NumberToString(*number) : "";

      items.push_back(std::move(ci));
    }

    LiveFolderItemCache::GetInstance().SetItems(config_json, std::move(items));
    NotifySidebar();
  }

  void OnGoogleCalendarResponse(const std::string& config_json,
                                std::optional<std::string> body) {
    if (!body.has_value()) {
      return;
    }

    std::optional<base::Value> parsed =
        base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return;
    }

    const auto* items_list = parsed->GetDict().FindList("items");
    if (!items_list) {
      return;
    }

    std::vector<LiveFolderCachedItem> items;
    for (const auto& entry : *items_list) {
      const auto* dict = entry.GetIfDict();
      if (!dict) {
        continue;
      }
      LiveFolderCachedItem ci;
      const std::string* summary = dict->FindString("summary");
      ci.title = summary ? *summary : "Untitled Event";

      const auto* start_dict = dict->FindDict("start");
      if (start_dict) {
        const std::string* dt = start_dict->FindString("dateTime");
        if (!dt) {
          dt = start_dict->FindString("date");
        }
        ci.subtitle = dt ? *dt : "";
      }

      const std::string* html_link = dict->FindString("htmlLink");
      ci.url = html_link ? *html_link : "";

      const std::string* id = dict->FindString("id");
      ci.id = id ? *id : "";

      items.push_back(std::move(ci));
    }

    LiveFolderItemCache::GetInstance().SetItems(config_json, std::move(items));
    NotifySidebar();
  }

  void NotifySidebar() {
    if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
      bridge->NotifyChanged();
    }
  }

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> active_loaders_;
};

}  // namespace

// static
void LiveFolderCacheWarmer::WarmCache(Profile* profile) {
  if (!profile) {
    return;
  }

  const base::ListValue& folder_list =
      profile->GetPrefs()->GetList(maho::sidebar_prefs::kLiveFolders);
  if (folder_list.empty()) {
    return;
  }

  auto warmer = base::MakeRefCounted<CacheWarmerImpl>(
      profile->GetURLLoaderFactory());
  warmer->Start(folder_list);
}

}  // namespace maho
