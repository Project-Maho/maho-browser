// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_DELEGATE_LINUX_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_DELEGATE_LINUX_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/component_export.h"
#include "maho/browser/updates/linux_install_kind.h"
#include "maho/browser/updates/platform_updater_delegate.h"

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace maho {

// Shared Linux updater. Fetches the signed manifest envelope, verifies the
// ed25519 signature, and compares the manifest version against this build.
// Subclasses differ only in what the user is told once a newer version exists:
// a distro-package install defers to apt/dnf, while a self-contained tarball
// install has no package manager and must be pointed at a fresh download.
class COMPONENT_EXPORT(MAHO_UPDATES) LinuxManifestUpdaterDelegate
    : public PlatformUpdaterDelegate {
 public:
  LinuxManifestUpdaterDelegate(const LinuxManifestUpdaterDelegate&) = delete;
  LinuxManifestUpdaterDelegate& operator=(const LinuxManifestUpdaterDelegate&) =
      delete;
  ~LinuxManifestUpdaterDelegate() override;

  // PlatformUpdaterDelegate:
  void Initialize() override;
  void Check(bool manual_check) override;
  void SetChannel(const std::string& channel_name) override;
  void ApplyUpdateAndRestart() override;

  // Applies an already signature-verified manifest payload and drives the
  // resulting MahoUpdateManager transition. Public so unit tests can exercise
  // the version-compare decision without a network round trip; signature
  // verification itself is covered by maho_update_signature_unittest.cc.
  void ApplyVerifiedManifest(std::string_view payload_json);

  std::string GetUpdateGuidance() const override = 0;

 protected:
  LinuxManifestUpdaterDelegate();

 private:
  void OnManifestFetched(std::optional<std::string> body);

  std::string channel_ = "stable";
  std::unique_ptr<network::SimpleURLLoader> loader_;
};

// Builds the delegate for |kind|. Split from the platform factory so tests can
// construct either branch directly instead of depending on the host's layout.
COMPONENT_EXPORT(MAHO_UPDATES)
std::unique_ptr<LinuxManifestUpdaterDelegate> CreateLinuxUpdaterDelegateForKind(
    updates::LinuxInstallKind kind);

}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_DELEGATE_LINUX_H_
