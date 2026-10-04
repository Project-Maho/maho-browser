// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/components/constants/webui_url_constants.h"

#include <set>
#include <string>
#include <string_view>

#include "maho/components/constants/url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoUrlConstantsTest, ProductionArrayCountAndUniqueness) {
  EXPECT_EQ(kMahoUIScheme, std::string_view("maho"));
  EXPECT_EQ(kMahoUrlAliases.size(), 10u);

  std::set<std::string> alias_hosts;
  std::set<std::string> public_urls;
  std::set<std::string> actual_hosts;
  std::set<std::string> actual_urls;

  for (const auto& record : kMahoUrlAliases) {
    EXPECT_NE(record.alias_host, nullptr);
    EXPECT_NE(record.public_url, nullptr);
    EXPECT_NE(record.actual_host, nullptr);
    EXPECT_NE(record.actual_url, nullptr);

    EXPECT_FALSE(std::string_view(record.alias_host).empty());
    EXPECT_FALSE(std::string_view(record.public_url).empty());
    EXPECT_FALSE(std::string_view(record.actual_host).empty());
    EXPECT_FALSE(std::string_view(record.actual_url).empty());

    // Public URL must start with "maho://"
    EXPECT_EQ(std::string_view(record.public_url).rfind("maho://", 0), 0u);

    // Actual URL must start with "chrome://" or "chrome-untrusted://"
    EXPECT_TRUE(std::string_view(record.actual_url).rfind("chrome://", 0) == 0 ||
                std::string_view(record.actual_url).rfind("chrome-untrusted://", 0) == 0);

    alias_hosts.insert(record.alias_host);
    public_urls.insert(record.public_url);
    actual_hosts.insert(record.actual_host);
    actual_urls.insert(record.actual_url);
  }

  EXPECT_EQ(alias_hosts.size(), 10u);
  EXPECT_EQ(public_urls.size(), 10u);
  EXPECT_EQ(actual_hosts.size(), 10u);
  EXPECT_EQ(actual_urls.size(), 10u);
}

TEST(MahoUrlConstantsTest, ActualConstantsRetainChromeSchemes) {
  const char* const actual_urls[] = {
      kMahoAIURL,
      kMahoSettingsURL,
      kMahoBoostURL,
      kMahoBoostUntrustedURL,
      kMahoTestURL,
      kMahoLiveFoldersURL,
      kMahoSyncURL,
      kMahoSidebarURL,
      kMahoWelcomeURL,
      kMahoSidebarLegacyURL,
      kMahoSpaceConfigURL,
      kMahoSpaceCreateURL,
      kMahoRoutinesURL,
      kMahoMailURL,
      kMahoChangelogURL,
  };

  for (const char* url : actual_urls) {
    const std::string_view actual_url(url);
    EXPECT_TRUE(actual_url.starts_with("chrome://") ||
                actual_url.starts_with("chrome-untrusted://"));
  }
}

TEST(MahoUrlConstantsTest, ExcludedHostsHaveNoPublicAlias) {
  const char* const excluded_hosts[] = {
      "test",
      "boost",
      "boost-editor",
      "sidebar",
      "sidebar-legacy",
  };

  for (const char* excluded : excluded_hosts) {
    for (const auto& record : kMahoUrlAliases) {
      EXPECT_NE(std::string_view(record.alias_host), std::string_view(excluded));
    }
  }
}

}  // namespace maho
