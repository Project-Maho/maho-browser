// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

namespace maho {

base::TimeDelta LiveFolderRefreshInterval(const std::string& provider_type) {
  if (provider_type == "google_calendar") {
    return base::Minutes(2);
  }
  return base::Minutes(5);
}

std::string LiveFolderGoogleCalendarEventsUrl(const std::string& calendar_id,
                                              base::Time now) {
  base::Time::Exploded utc;
  now.UTCExplode(&utc);
  return base::StringPrintf(
      "https://www.googleapis.com/calendar/v3/calendars/%s/events"
      "?maxResults=20&orderBy=startTime&singleEvents=true"
      "&timeMin=%04d-%02d-%02dT%02d:%02d:%02dZ",
      calendar_id.c_str(), utc.year, utc.month, utc.day_of_month, utc.hour,
      utc.minute, utc.second);
}

LiveFolderCachedItem::LiveFolderCachedItem() = default;
LiveFolderCachedItem::~LiveFolderCachedItem() = default;
LiveFolderCachedItem::LiveFolderCachedItem(const LiveFolderCachedItem&) =
    default;
LiveFolderCachedItem& LiveFolderCachedItem::operator=(
    const LiveFolderCachedItem&) = default;
LiveFolderCachedItem::LiveFolderCachedItem(LiveFolderCachedItem&&) = default;
LiveFolderCachedItem& LiveFolderCachedItem::operator=(LiveFolderCachedItem&&) =
    default;

std::optional<std::vector<LiveFolderCachedItem>> ParseLiveFolderGitHubPRs(
    const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  std::vector<LiveFolderCachedItem> items;
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

    LiveFolderCachedItem item;
    item.id = "pr_" + base::NumberToString(*number);
    item.title = "#" + base::NumberToString(*number) + " " + *title;
    if (const auto* user = dict->FindDict("user")) {
      if (const std::string* login = user->FindString("login")) {
        item.subtitle = *login;
      }
    }
    item.url = *html_url;
    items.push_back(std::move(item));
  }
  return items;
}

std::optional<std::vector<LiveFolderCachedItem>>
ParseLiveFolderGoogleCalendarEvents(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::ListValue* events = parsed->GetDict().FindList("items");
  if (!events) {
    return std::nullopt;
  }

  std::vector<LiveFolderCachedItem> items;
  for (const auto& entry : *events) {
    const auto* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* summary = dict->FindString("summary");
    const std::string* event_id = dict->FindString("id");
    if (!event_id || event_id->empty()) {
      continue;
    }

    LiveFolderCachedItem item;
    item.id = *event_id;
    // Google omits `summary` for untitled events and free/busy-only calendars;
    // keep them so busy time never silently disappears from the sidebar.
    item.title = (summary && !summary->empty()) ? *summary : "Untitled Event";
    if (const auto* start = dict->FindDict("start")) {
      const std::string* when = start->FindString("dateTime");
      if (!when) {
        when = start->FindString("date");
      }
      if (when) {
        item.subtitle = *when;
      }
    }
    const std::string* html_link = dict->FindString("htmlLink");
    item.url = html_link ? *html_link : "https://calendar.google.com";
    items.push_back(std::move(item));
  }
  return items;
}

std::vector<LiveFolderCachedItem> LiveFolderGoogleCalendarPlaceholderItems() {
  LiveFolderCachedItem item;
  item.id = "gcal_open";
  item.title = "Open Google Calendar";
  item.subtitle = "View your events";
  item.url = "https://calendar.google.com";
  std::vector<LiveFolderCachedItem> items;
  items.push_back(std::move(item));
  return items;
}

// static
LiveFolderItemCache& LiveFolderItemCache::GetInstance() {
  static base::NoDestructor<LiveFolderItemCache> instance;
  return *instance;
}

LiveFolderItemCache::LiveFolderItemCache() = default;
LiveFolderItemCache::~LiveFolderItemCache() = default;

LiveFolderItemCache::Entry::Entry() = default;
LiveFolderItemCache::Entry::~Entry() = default;
LiveFolderItemCache::Entry::Entry(Entry&&) = default;
LiveFolderItemCache::Entry& LiveFolderItemCache::Entry::operator=(Entry&&) =
    default;

void LiveFolderItemCache::SetItems(const std::string& config_json,
                                   std::vector<LiveFolderCachedItem> items) {
  Entry& entry = items_by_config_[config_json];
  entry.items = std::move(items);
  entry.updated = base::Time::Now();
}

const std::vector<LiveFolderCachedItem>* LiveFolderItemCache::GetItems(
    const std::string& config_json) const {
  auto it = items_by_config_.find(config_json);
  if (it == items_by_config_.end()) {
    return nullptr;
  }
  return &it->second.items;
}

base::Time LiveFolderItemCache::GetUpdateTime(
    const std::string& config_json) const {
  auto it = items_by_config_.find(config_json);
  return it == items_by_config_.end() ? base::Time() : it->second.updated;
}

void LiveFolderItemCache::Clear(const std::string& config_json) {
  items_by_config_.erase(config_json);
}

}  // namespace maho
