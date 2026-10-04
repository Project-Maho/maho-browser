// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_LINUX_INSTALL_KIND_H_
#define MAHO_BROWSER_UPDATES_LINUX_INSTALL_KIND_H_

#include "base/component_export.h"
#include "base/files/file_path.h"

namespace maho {
namespace updates {

// How this Linux build was installed. Determines which updater delegate the
// factory hands back: a system-package install cannot self-update (the package
// manager owns the payload), while a self-contained install must point the user
// at a fresh download.
enum class LinuxInstallKind {
  // Installed from the .deb / .rpm. The runtime lives under a distro-owned
  // prefix and `apt`/`dnf` applies upgrades.
  kSystemPackage,
  // Unpacked from the .tar.gz into an arbitrary user-chosen directory.
  kSelfContained,
};

// Classifies |exe_dir| (the directory holding the browser executable). Pure and
// filesystem-free so tests can drive both branches. Matches on path components,
// so "/usr/lib/maho-nightly" is NOT treated as living under "/usr/lib/maho".
COMPONENT_EXPORT(MAHO_UPDATES)
LinuxInstallKind DetectLinuxInstallKindForDir(const base::FilePath& exe_dir);

// Production wrapper: resolves the real executable directory and classifies it.
// Falls back to kSelfContained when the directory cannot be resolved, because
// prompting for a download is safe while claiming "up to date" is not.
COMPONENT_EXPORT(MAHO_UPDATES)
LinuxInstallKind DetectLinuxInstallKind();

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_LINUX_INSTALL_KIND_H_
