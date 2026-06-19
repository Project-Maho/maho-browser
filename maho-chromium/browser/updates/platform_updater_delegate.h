// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_DELEGATE_H_
#define MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_DELEGATE_H_

#include <string>

namespace maho {

class PlatformUpdaterDelegate {
 public:
  virtual ~PlatformUpdaterDelegate() = default;

  // Initialize the platform updater.
  virtual void Initialize() = 0;

  // Run the update check.
  virtual void Check(bool manual_check) = 0;

  // Update the feed URL / channel configuration.
  virtual void SetChannel(const std::string& channel_name) = 0;

  // Apply update and relaunch (if supported).
  virtual void ApplyUpdateAndRestart() = 0;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_DELEGATE_H_
