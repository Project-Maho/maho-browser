// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/linux_install_kind.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"

namespace maho {
namespace updates {

namespace {

// Prefixes the .deb / .rpm packages install the runtime under. See
// maho-chromium/build/scripts/build_maho_linux_packages.py: the deb ships
// usr/lib/maho/ with a usr/bin/maho symlink, and the rpm spec installs the
// same layout (usr/lib64/maho on multilib distros).
constexpr const base::FilePath::CharType* kSystemPackagePrefixes[] = {
    FILE_PATH_LITERAL("/usr/lib/maho"),
    FILE_PATH_LITERAL("/usr/lib64/maho"),
    FILE_PATH_LITERAL("/opt/maho"),
};

}  // namespace

LinuxInstallKind DetectLinuxInstallKindForDir(const base::FilePath& exe_dir) {
  if (exe_dir.empty()) {
    return LinuxInstallKind::kSelfContained;
  }

  const base::FilePath dir = exe_dir.StripTrailingSeparators();
  for (const base::FilePath::CharType* prefix : kSystemPackagePrefixes) {
    const base::FilePath package_root =
        base::FilePath(prefix).StripTrailingSeparators();
    // Component-wise comparison: IsParent() walks path components, so
    // "/usr/lib/maho-nightly" does not match the "/usr/lib/maho" prefix the way
    // a raw string compare would.
    if (dir == package_root || package_root.IsParent(dir)) {
      return LinuxInstallKind::kSystemPackage;
    }
  }

  return LinuxInstallKind::kSelfContained;
}

LinuxInstallKind DetectLinuxInstallKind() {
  base::FilePath exe_dir;
  if (!base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    return LinuxInstallKind::kSelfContained;
  }
  return DetectLinuxInstallKindForDir(exe_dir);
}

}  // namespace updates
}  // namespace maho
