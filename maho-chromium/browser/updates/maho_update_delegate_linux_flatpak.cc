// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "base/logging.h"

namespace maho {

class PlatformUpdaterDelegateLinuxFlatpak : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateLinuxFlatpak() = default;
  ~PlatformUpdaterDelegateLinuxFlatpak() override = default;

  void Initialize() override {
    LOG(INFO) << "Linux Flatpak updater delegate initialized.";
  }

  void Check(bool manual_check) override {
    MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kUpToDate);
  }

  void SetChannel(const std::string& channel_name) override {}
  void ApplyUpdateAndRestart() override {}
};

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateLinuxFlatpak>();
}

}  // namespace maho
