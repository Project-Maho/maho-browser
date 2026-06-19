// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/maho_update_manager.h"

#include "base/logging.h"

// AppImage backend.
//
// The original plan §5.3 called for AppImageUpdate / zsync delta downloads
// gated behind `maho_appimage_delta = true`. That work is intentionally
// DEFERRED: it requires linking libappimageupdate (or vendoring zsync) and
// parsing the AppImage's `update_information` ELF section, neither of which
// is wired up yet. Until then this delegate stays a banner-only no-op so
// AppImage builds at least link cleanly. Tracked as a follow-up under the
// implementation plan §5.3.
//
// TODO(maho): wire libappimageupdate behind maho_appimage_delta.

namespace maho {

class PlatformUpdaterDelegateLinuxAppImage : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateLinuxAppImage() = default;
  ~PlatformUpdaterDelegateLinuxAppImage() override = default;

  void Initialize() override {
    LOG(INFO) << "Maho: AppImage update delegate initialized (deferred; "
                 "no-op until libappimageupdate is wired).";
  }

  void Check(bool manual_check) override {
    MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kUpToDate);
  }

  void SetChannel(const std::string& channel_name) override {}
  void ApplyUpdateAndRestart() override {}
};

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateLinuxAppImage>();
}

}  // namespace maho
