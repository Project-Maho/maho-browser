// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_BOOSTS_DATA_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_BOOSTS_DATA_H_

#include <optional>
#include <string>
#include <vector>

namespace maho {

struct BoostItem {
  BoostItem();
  BoostItem(const BoostItem&);
  BoostItem(BoostItem&&);
  BoostItem& operator=(const BoostItem&);
  BoostItem& operator=(BoostItem&&);
  ~BoostItem();

  std::string id;
  std::string url_pattern;
  std::optional<std::string> custom_css;
  std::optional<std::string> custom_js;
  bool enabled = false;
};

std::vector<BoostItem> ParseBoosts();
std::u16string BoostTitleFromUrlPattern(const std::string& url_pattern);
std::u16string BoostSubtitleFromUrlPattern(const std::string& url_pattern);
std::u16string BoostBadgeTextFromUrlPattern(const std::string& url_pattern);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_BOOSTS_DATA_H_
