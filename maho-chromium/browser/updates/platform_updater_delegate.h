// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_DELEGATE_H_
#define MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_DELEGATE_H_

#include <string>

#include "base/functional/callback.h"

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

  // Human-readable guidance shown when this install cannot self-update and the
  // user must update some other way (e.g. a package manager or an app store).
  // Empty when there is nothing to say.
  virtual std::string GetUpdateGuidance() const = 0;

  // Handler for the user asking to see the full release notes / version
  // history. A platform updater (Sparkle) normally opens the appcast's
  // releaseNotesLink in the default browser; that URL points at a release
  // asset GitHub serves as a download, so the user got a saved .html file
  // instead of readable notes. The browser layer injects a handler that opens
  // Maho's own changelog surface in-app. Unsupported platforms keep the
  // no-op default.
  virtual void SetVersionHistoryHandler(base::RepeatingClosure handler) {}
};

}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_DELEGATE_H_
