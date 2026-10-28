// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_DELEGATE_LINUX_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_DELEGATE_LINUX_H_

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/component_export.h"
#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "maho/browser/updates/linux_install_kind.h"
#include "maho/browser/updates/platform_updater_delegate.h"

class GURL;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace maho {

// Shared Linux updater. Fetches the signed manifest envelope, verifies the
// ed25519 signature, and compares the manifest version against this build.
// When the relay is unreachable (network error or HTTP 5xx) the check retries
// once against the static GitHub Release copy of the same signed envelope;
// signature verification applies to both sources.
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

  // Test seams. Production uses g_browser_process's factory, the manager's
  // resolved relay URL, and the compiled-in manifest public key.
  void SetURLLoaderFactoryForTesting(
      scoped_refptr<network::SharedURLLoaderFactory> factory);
  void SetRelayBaseUrlForTesting(const std::string& base_url);
  void SetManifestPublicKeyForTesting(base::span<const uint8_t, 32> key);

 protected:
  LinuxManifestUpdaterDelegate();

 private:
  void StartFetch(const GURL& url, bool is_static_fallback);
  void OnManifestFetched(bool is_static_fallback,
                         std::optional<std::string> body);
  void VerifyAndApplyEnvelope(const std::string& body);

  std::string channel_ = "stable";
  std::unique_ptr<network::SimpleURLLoader> loader_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_for_testing_;
  std::optional<std::string> relay_base_url_for_testing_;
  std::array<uint8_t, 32> manifest_public_key_;
};

// Builds the delegate for |kind|. Split from the platform factory so tests can
// construct either branch directly instead of depending on the host's layout.
COMPONENT_EXPORT(MAHO_UPDATES)
std::unique_ptr<LinuxManifestUpdaterDelegate> CreateLinuxUpdaterDelegateForKind(
    updates::LinuxInstallKind kind);

}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_DELEGATE_LINUX_H_
