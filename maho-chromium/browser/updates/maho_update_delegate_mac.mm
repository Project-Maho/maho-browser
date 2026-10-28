// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_update_server_url.h"
#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/platform_updater_error_mapping.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

#include <utility>

#include "base/functional/callback.h"
#include "base/logging.h"

// macOS Sparkle 2 integration.
//
// EdDSA verification REQUIRES the app bundle's Info.plist to carry
// `SUPublicEDKey` (base64 of the 32-byte ed25519 public key). Until that key
// is injected at build time (see apply_chromium_src_overrides.py
// "SUPublicEDKey" patch + maho-chromium/branding/), Sparkle will REFUSE every
// update with a signature-verification error. Treat this file as wired but
// non-functional for end-to-end updates.

namespace maho {
class PlatformUpdaterDelegateMac;
}  // namespace maho

@interface MahoSparkleDelegate
    : NSObject <SPUUpdaterDelegate, SPUStandardUserDriverDelegate>
@property(nonatomic, strong) NSURL* testFeedURL;
@property(nonatomic, copy) NSString* channelName;
// Set for exactly one update cycle after the relay appcast fetch failed, so
// feedURLStringForUpdater: hands Sparkle the static GitHub Release appcast.
// The static appcast carries the same sparkle:edSignature values, and Sparkle
// still verifies them against SUPublicEDKey, so the fallback does not weaken
// update authentication.
@property(nonatomic, assign) BOOL useStaticFallbackFeed;
// Non-owning back-pointer: the C++ delegate owns this Objective-C object and
// outlives it.
@property(nonatomic, assign) maho::PlatformUpdaterDelegateMac* owner;
@end

namespace maho {

class PlatformUpdaterDelegateMac : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateMac() = default;
  ~PlatformUpdaterDelegateMac() override = default;

  void Initialize() override {
    delegate_ = [[MahoSparkleDelegate alloc] init];
    delegate_.owner = this;
    // channelName intentionally left nil; the manager's SetChannel() runs
    // immediately after Initialize(). If nil, feedURLStringForUpdater:
    // falls back to "stable".
    //
    // The user driver delegate is set so Sparkle asks Maho to show the full
    // release notes ("Version History") instead of opening the appcast's
    // releaseNotesLink in the system browser.
    controller_ =
        [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                      updaterDelegate:delegate_
                                                   userDriverDelegate:delegate_];
  }

  void Check(bool manual_check) override {
    MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kChecking);
    if (!StartUpdaterIfNeeded()) {
      return;
    }
    if (manual_check) {
      [controller_ checkForUpdates:nil];
    } else {
      [controller_.updater checkForUpdatesInBackground];
    }
  }

  // macOS updates run through Sparkle, which drives its own UI, so there is no
  // extra guidance to surface.
  std::string GetUpdateGuidance() const override { return std::string(); }

  void SetChannel(const std::string& channel_name) override {
    if (delegate_) {
      delegate_.channelName =
          [NSString stringWithUTF8String:channel_name.c_str()];
    }
  }

  void ApplyUpdateAndRestart() override {
    // SPUStandardUpdaterController handles installation and relaunch automatically.
  }

  void SetVersionHistoryHandler(base::RepeatingClosure handler) override {
    version_history_handler_ = std::move(handler);
  }

  // Runs the browser-layer handler that opens Maho's own changelog surface.
  // Returns false when no handler was injected, so the caller can fall back to
  // the appcast link rather than doing nothing.
  bool ShowVersionHistory() const {
    if (!version_history_handler_) {
      return false;
    }
    version_history_handler_.Run();
    return true;
  }

  void SetTestFeedURL(const std::string& url_str) {
    if (delegate_) {
      delegate_.testFeedURL =
          [NSURL URLWithString:[NSString stringWithUTF8String:url_str.c_str()]];
    }
  }

  // Re-runs the check once against the static fallback appcast. Called after a
  // relay-feed failure; the retry is posted so Sparkle finishes its current
  // update cycle first.
  void RetryWithStaticFallbackFeed(bool manual_check) {
    delegate_.useStaticFallbackFeed = YES;
    SPUStandardUpdaterController* controller = controller_;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (manual_check) {
        [controller checkForUpdates:nil];
      } else {
        [controller.updater checkForUpdatesInBackground];
      }
    });
  }

 private:
  bool StartUpdaterIfNeeded() {
    if (updater_started_) {
      return true;
    }

    NSError* error = nil;
    if (![controller_.updater startUpdater:&error]) {
      LOG(ERROR) << "Maho update updater failed to start: "
                 << error.localizedDescription.UTF8String;
      MahoUpdateManager::GetInstance()->TransitionToError(
          UpdateError::kUnknown);
      return false;
    }
    updater_started_ = true;
    return true;
  }

  SPUStandardUpdaterController* controller_ = nil;
  MahoSparkleDelegate* delegate_ = nil;
  base::RepeatingClosure version_history_handler_;
  bool updater_started_ = false;
};

// Factory implementation
std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateMac>();
}

}  // namespace maho

@implementation MahoSparkleDelegate

@synthesize testFeedURL;
@synthesize channelName;
@synthesize useStaticFallbackFeed;
@synthesize owner;

// Sparkle's standard user driver offers a "Version History" button when a
// manual check finds no update. Without this delegate method Sparkle hands the
// appcast's releaseNotesLink to the system browser; that URL is a GitHub
// release asset served as an attachment, so the user only got a downloaded
// .html file. Show the bundled changelog surface instead and keep the appcast
// link as the fallback when no in-app handler was injected.
- (void)standardUserDriverShowVersionHistoryForAppcastItem:
    (SUAppcastItem*)item {
  maho::PlatformUpdaterDelegateMac* delegate = self.owner;
  if (delegate && delegate->ShowVersionHistory()) {
    return;
  }
  NSURL* url = item.fullReleaseNotesURL ?: item.releaseNotesURL;
  if (url) {
    [[NSWorkspace sharedWorkspace] openURL:url];
  }
}

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater {
  if (self.testFeedURL) {
    return [self.testFeedURL absoluteString];
  }
  if (self.useStaticFallbackFeed) {
    return [NSString
        stringWithUTF8String:maho::updates::GetStaticMacAppcastFallbackUrl()
                                 .c_str()];
  }

  int bucket = maho::MahoUpdateManager::GetInstance()->GetRolloutBucket();
  NSString* channel =
      self.channelName.length > 0 ? self.channelName : @"stable";
  const std::string& base_url =
      maho::MahoUpdateManager::GetInstance()->GetServerBaseUrl();
  NSString* base_ns =
      [NSString stringWithUTF8String:base_url.c_str()];
  NSString* urlString = [NSString
      stringWithFormat:@"%@/updates/macos/appcast?channel=%@&b=%d",
                       base_ns, channel, bucket];
  return urlString;
}

- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item {
  maho::MahoUpdateManager::GetInstance()->TransitionToState(
      maho::UpdateState::kUpdateAvailable);
}

- (void)updaterDidNotFindUpdate:(SPUUpdater *)updater {
  maho::MahoUpdateManager::GetInstance()->TransitionToState(
      maho::UpdateState::kUpToDate);
}

- (void)updater:(SPUUpdater *)updater didDownloadUpdate:(SUAppcastItem *)item {
  maho::MahoUpdateManager::GetInstance()->TransitionToState(
      maho::UpdateState::kReadyToInstall);
}

- (void)updater:(SPUUpdater *)updater
    failedToDownloadUpdate:(SUAppcastItem *)item
                     error:(NSError *)error {
  // Download-phase failures are never benign, so kNone falls back to
  // kDownloadFailed.
  maho::UpdateError err = maho::UpdateError::kDownloadFailed;
  if ([error.domain isEqualToString:SUSparkleErrorDomain]) {
    const maho::UpdateError classified =
        maho::updates::UpdateErrorFromUpdaterCode((int)error.code);
    if (classified != maho::UpdateError::kNone) {
      err = classified;
    }
  }
  maho::MahoUpdateManager::GetInstance()->TransitionToError(err);
}

- (void)updater:(SPUUpdater *)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
                                 error:(NSError *)error {
  const BOOL was_static_fallback = self.useStaticFallbackFeed;
  self.useStaticFallbackFeed = NO;
  if (!error) {
    return;
  }
  const bool is_sparkle = [error.domain isEqualToString:SUSparkleErrorDomain];
  // SUAppcastError means the appcast itself could not be fetched (relay down,
  // DNS/TLS failure, HTTP 5xx). Retry once against the static release copy;
  // parse and signature errors are not outages and are reported as-is.
  if (is_sparkle && error.code == SUAppcastError && !was_static_fallback &&
      !self.testFeedURL && self.owner) {
    LOG(WARNING) << "Maho: relay appcast unreachable; retrying the static "
                    "fallback appcast.";
    self.owner->RetryWithStaticFallbackFeed(updateCheck ==
                                            SPUUpdateCheckUpdates);
    return;
  }
  switch (maho::updates::ClassifyUpdateCycleError(is_sparkle,
                                                  (int)error.code)) {
    case maho::updates::UpdateCycleOutcome::kNoUpdateFound:
      maho::MahoUpdateManager::GetInstance()->TransitionToState(
          maho::UpdateState::kUpToDate);
      return;
    case maho::updates::UpdateCycleOutcome::kUserCanceled:
      // The user declined the authorization prompt; nothing failed and the
      // existing state still describes the downloaded update.
      return;
    case maho::updates::UpdateCycleOutcome::kFailure:
      maho::MahoUpdateManager::GetInstance()->TransitionToError(
          is_sparkle
              ? maho::updates::UpdateErrorFromUpdaterCode((int)error.code)
              : maho::UpdateError::kConnectionFailed);
      return;
  }
}

@end
