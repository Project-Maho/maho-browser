// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_browser_detector.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/test/scoped_path_override.h"
#include "testing/gtest/include/gtest/gtest.h"

#include "maho/components/maho_importer/maho_importer_constants.mojom.h"

namespace maho {
namespace {

bool WriteFileHelper(const base::FilePath& path, const std::string& data) {
  return base::WriteFile(path, data);
}

base::FilePath ResolveSymlink(const base::FilePath& path) {
  base::FilePath resolved;
  if (base::NormalizeFilePath(path, &resolved)) {
    return resolved;
  }
  return path;
}

class MahoBrowserDetectorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    app_data_override_ = std::make_unique<base::ScopedPathOverride>(
        base::DIR_APP_DATA, temp_dir_.GetPath());
    home_override_ = std::make_unique<base::ScopedPathOverride>(
        base::DIR_HOME, temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedPathOverride> app_data_override_;
  std::unique_ptr<base::ScopedPathOverride> home_override_;
};

TEST_F(MahoBrowserDetectorTest, DetectNoBrowsers) {
  auto browsers = DetectInstalledBrowsers();
  for (const auto& browser : browsers) {
    EXPECT_NE(browser.type, BrowserType::kChrome);
    EXPECT_NE(browser.type, BrowserType::kFirefox);
    EXPECT_NE(browser.type, BrowserType::kZen);
    EXPECT_NE(browser.type, BrowserType::kArc);
  }
}

TEST_F(MahoBrowserDetectorTest, DetectChromeSingleProfile) {
  base::FilePath chrome_dir = temp_dir_.GetPath().AppendASCII("Google").AppendASCII("Chrome");
  ASSERT_TRUE(base::CreateDirectory(chrome_dir));
  base::FilePath default_profile = chrome_dir.AppendASCII("Default");
  ASSERT_TRUE(base::CreateDirectory(default_profile));

  ASSERT_TRUE(WriteFileHelper(default_profile.AppendASCII("Bookmarks"), "{}"));
  ASSERT_TRUE(WriteFileHelper(default_profile.AppendASCII("Login Data"), "passwords"));

  auto browsers = DetectInstalledBrowsers();
  bool found_chrome = false;
  for (const auto& b : browsers) {
    if (b.type == BrowserType::kChrome) {
      found_chrome = true;
      EXPECT_EQ(b.display_name, "Google Chrome");
      EXPECT_EQ(ResolveSymlink(b.profile_path), ResolveSymlink(default_profile.AppendASCII("Bookmarks")));
      EXPECT_TRUE(b.services_supported & maho::mojom::kImportBookmarks);
      EXPECT_TRUE(b.services_supported & maho::mojom::kImportPasswords);
    }
  }
  EXPECT_TRUE(found_chrome);
}

TEST_F(MahoBrowserDetectorTest, DetectChromeMultiProfile) {
  base::FilePath chrome_dir = temp_dir_.GetPath().AppendASCII("Google").AppendASCII("Chrome");
  ASSERT_TRUE(base::CreateDirectory(chrome_dir));

  base::FilePath default_profile = chrome_dir.AppendASCII("Default");
  base::FilePath profile1 = chrome_dir.AppendASCII("Profile 1");
  base::FilePath profile2 = chrome_dir.AppendASCII("Profile 2");

  ASSERT_TRUE(base::CreateDirectory(default_profile));
  ASSERT_TRUE(base::CreateDirectory(profile1));
  ASSERT_TRUE(base::CreateDirectory(profile2));

  ASSERT_TRUE(WriteFileHelper(default_profile.AppendASCII("Bookmarks"), "{}"));
  ASSERT_TRUE(WriteFileHelper(profile1.AppendASCII("Bookmarks"), "{}"));
  ASSERT_TRUE(WriteFileHelper(profile2.AppendASCII("Bookmarks"), "{}"));

  std::string prefs_json = "{\"profile\":{\"name\":\"Work Profile\"}}";
  ASSERT_TRUE(WriteFileHelper(profile1.AppendASCII("Preferences"), prefs_json));
  ASSERT_TRUE(WriteFileHelper(profile2.AppendASCII("Preferences"), "{}"));

  auto browsers = DetectInstalledBrowsers();
  int chrome_count = 0;
  bool found_default = false;
  bool found_profile1 = false;
  bool found_profile2 = false;

  for (const auto& b : browsers) {
    if (b.type == BrowserType::kChrome) {
      chrome_count++;
      if (b.display_name == "Google Chrome") {
        found_default = true;
        EXPECT_EQ(ResolveSymlink(b.profile_path), ResolveSymlink(default_profile.AppendASCII("Bookmarks")));
      } else if (b.display_name == "Google Chrome - Work Profile") {
        found_profile1 = true;
        EXPECT_EQ(ResolveSymlink(b.profile_path), ResolveSymlink(profile1.AppendASCII("Bookmarks")));
      } else if (b.display_name == "Google Chrome - Profile 2") {
        found_profile2 = true;
        EXPECT_EQ(ResolveSymlink(b.profile_path), ResolveSymlink(profile2.AppendASCII("Bookmarks")));
      }
    }
  }

  EXPECT_EQ(chrome_count, 3);
  EXPECT_TRUE(found_default);
  EXPECT_TRUE(found_profile1);
  EXPECT_TRUE(found_profile2);
}

TEST_F(MahoBrowserDetectorTest, DetectGeckoBrowsers) {
  base::FilePath zen_dir = temp_dir_.GetPath().AppendASCII("zen");
  ASSERT_TRUE(base::CreateDirectory(zen_dir));

  std::string profiles_ini =
      "[General]\n"
      "StartWithLastProfile=1\n"
      "\n"
      "[Profile0]\n"
      "Name=default\n"
      "IsRelative=1\n"
      "Path=Profiles/default\n"
      "Default=1\n";
  ASSERT_TRUE(WriteFileHelper(zen_dir.AppendASCII("profiles.ini"), profiles_ini));

  base::FilePath profile_path = zen_dir.AppendASCII("Profiles").AppendASCII("default");
  ASSERT_TRUE(base::CreateDirectory(profile_path));
  ASSERT_TRUE(WriteFileHelper(profile_path.AppendASCII("places.sqlite"), "places"));
  ASSERT_TRUE(WriteFileHelper(profile_path.AppendASCII("logins.json"), "logins"));

  std::string compatibility_ini =
      "[Compatibility]\n"
      "LastVersion=1.0.0-a.39_20260601\n"
      "LastAppDir=/Applications/Zen.app\n";
  ASSERT_TRUE(WriteFileHelper(profile_path.AppendASCII("compatibility.ini"), compatibility_ini));

  auto browsers = DetectInstalledBrowsers();
  bool found_zen = false;
  for (const auto& b : browsers) {
    if (b.type == BrowserType::kZen) {
      found_zen = true;
      EXPECT_EQ(b.display_name, "Zen");
      EXPECT_EQ(ResolveSymlink(b.profile_path), ResolveSymlink(profile_path.AppendASCII("places.sqlite")));
      EXPECT_TRUE(b.services_supported & maho::mojom::kImportBookmarks);
      EXPECT_TRUE(b.services_supported & maho::mojom::kImportHistory);
      EXPECT_TRUE(b.services_supported & maho::mojom::kImportPasswords);
      EXPECT_TRUE(b.services_supported & maho::mojom::kImportWorkspaces);
    }
  }
  EXPECT_TRUE(found_zen);
}

}  // namespace
}  // namespace maho
