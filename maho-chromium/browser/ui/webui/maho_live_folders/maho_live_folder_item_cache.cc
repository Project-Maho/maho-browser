// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h"

#include "base/strings/stringprintf.h"

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
