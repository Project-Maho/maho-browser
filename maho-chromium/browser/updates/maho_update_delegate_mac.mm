// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/platform_updater_delegate.h"

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

#include "base/logging.h"

// macOS Sparkle 2 integration.
//
// EdDSA verification REQUIRES the app bundle's Info.plist to carry
// `SUPublicEDKey` (base64 of the 32-byte ed25519 public key). Until that key
// is injected at build time (see apply_chromium_src_overrides.py
// "SUPublicEDKey" patch + maho-chromium/branding/), Sparkle will REFUSE every
// update with a signature-verification error. Treat this file as wired but
// non-functional for end-to-end updates.

@interface MahoSparkleDelegate : NSObject <SPUUpdaterDelegate>
@property(nonatomic, strong) NSURL* testFeedURL;
@property(nonatomic, copy) NSString* channelName;
@end

namespace maho {

class PlatformUpdaterDelegateMac : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateMac() = default;
  ~PlatformUpdaterDelegateMac() override = default;

  void Initialize() override {
    delegate_ = [[MahoSparkleDelegate alloc] init];
    // channelName intentionally left nil; the manager's SetChannel() runs
    // immediately after Initialize(). If nil, feedURLStringForUpdater:
    // falls back to "stable".
    controller_ =
        [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                      updaterDelegate:delegate_
                                                   userDriverDelegate:nil];
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

  void SetChannel(const std::string& channel_name) override {
    if (delegate_) {
      delegate_.channelName =
          [NSString stringWithUTF8String:channel_name.c_str()];
    }
  }

  void ApplyUpdateAndRestart() override {
    // SPUStandardUpdaterController handles installation and relaunch automatically.
  }

  void SetTestFeedURL(const std::string& url_str) {
    if (delegate_) {
      delegate_.testFeedURL =
          [NSURL URLWithString:[NSString stringWithUTF8String:url_str.c_str()]];
    }
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

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater {
  if (self.testFeedURL) {
    return [self.testFeedURL absoluteString];
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
  maho::UpdateError err = maho::UpdateError::kDownloadFailed;
  if ([error.domain isEqualToString:@"SUSparkleErrorDomain"]) {
    // Sparkle error codes related to signature verification (e.g. 2001, 2002)
    if (error.code == 2001 || error.code == 2002) {
      err = maho::UpdateError::kSignatureMismatch;
    }
  }
  maho::MahoUpdateManager::GetInstance()->TransitionToError(err);
}

- (void)updater:(SPUUpdater *)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
                                 error:(NSError *)error {
  if (error) {
    maho::UpdateError err = maho::UpdateError::kConnectionFailed;
    if ([error.domain isEqualToString:@"SUSparkleErrorDomain"]) {
      if (error.code == 2001 || error.code == 2002) {
        err = maho::UpdateError::kSignatureMismatch;
      }
    }
    maho::MahoUpdateManager::GetInstance()->TransitionToError(err);
  }
}

@end
