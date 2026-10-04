// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/linux_install_kind.h"

#include "base/files/file_path.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace updates {
namespace {

TEST(LinuxInstallKindTest, PackagePrefixesSelectTheSystemPackagePath) {
  const base::FilePath::CharType* kPackageDirs[] = {
      FILE_PATH_LITERAL("/usr/lib/maho"),
      FILE_PATH_LITERAL("/usr/lib/maho/"),
      FILE_PATH_LITERAL("/usr/lib64/maho"),
      FILE_PATH_LITERAL("/opt/maho"),
      FILE_PATH_LITERAL("/usr/lib/maho/lib"),
  };
  for (const auto* dir : kPackageDirs) {
    EXPECT_EQ(DetectLinuxInstallKindForDir(base::FilePath(dir)),
              LinuxInstallKind::kSystemPackage)
        << base::FilePath(dir);
  }
}

TEST(LinuxInstallKindTest, ArbitraryDirectoriesSelectTheTarballPath) {
  const base::FilePath::CharType* kTarballDirs[] = {
      FILE_PATH_LITERAL("/home/alice/maho"),
      FILE_PATH_LITERAL("/home/alice/Downloads/maho-linux-x64"),
      FILE_PATH_LITERAL("/usr/local/maho"),
      FILE_PATH_LITERAL("/tmp/maho"),
  };
  for (const auto* dir : kTarballDirs) {
    EXPECT_EQ(DetectLinuxInstallKindForDir(base::FilePath(dir)),
              LinuxInstallKind::kSelfContained)
        << base::FilePath(dir);
  }
}

TEST(LinuxInstallKindTest, SiblingOfAPackagePrefixIsNotAPackageInstall) {
  const base::FilePath::CharType* kSiblingDirs[] = {
      FILE_PATH_LITERAL("/usr/lib/maho-nightly"),
      FILE_PATH_LITERAL("/opt/maho-browser"),
      FILE_PATH_LITERAL("/usr/lib/mahogany"),
  };
  for (const auto* dir : kSiblingDirs) {
    EXPECT_EQ(DetectLinuxInstallKindForDir(base::FilePath(dir)),
              LinuxInstallKind::kSelfContained)
        << base::FilePath(dir);
  }
}

TEST(LinuxInstallKindTest, EmptyDirectoryFallsBackToTarballPath) {
  EXPECT_EQ(DetectLinuxInstallKindForDir(base::FilePath()),
            LinuxInstallKind::kSelfContained);
}

}  // namespace
}  // namespace updates
}  // namespace maho
