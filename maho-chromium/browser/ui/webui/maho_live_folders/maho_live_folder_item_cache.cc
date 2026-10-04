// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h"

namespace maho {

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

void LiveFolderItemCache::SetItems(const std::string& config_json,
                                   std::vector<LiveFolderCachedItem> items) {
  items_by_config_[config_json] = std::move(items);
}

const std::vector<LiveFolderCachedItem>* LiveFolderItemCache::GetItems(
    const std::string& config_json) const {
  auto it = items_by_config_.find(config_json);
  if (it == items_by_config_.end()) {
    return nullptr;
  }
  return &it->second;
}

void LiveFolderItemCache::Clear(const std::string& config_json) {
  items_by_config_.erase(config_json);
}

}  // namespace maho
