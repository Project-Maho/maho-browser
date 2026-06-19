// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "base/logging.h"

namespace maho {

class PlatformUpdaterDelegateLinuxNoop : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateLinuxNoop() = default;
  ~PlatformUpdaterDelegateLinuxNoop() override = default;

  void Initialize() override {
    LOG(INFO) << "Linux Noop updater delegate initialized.";
  }

  void Check(bool manual_check) override {
    MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kUpToDate);
  }

  void SetChannel(const std::string& channel_name) override {}
  void ApplyUpdateAndRestart() override {}
};

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateLinuxNoop>();
}

}  // namespace maho
