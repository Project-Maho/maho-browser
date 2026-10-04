// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_ERROR_MAPPING_H_
#define MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_ERROR_MAPPING_H_

#include "maho/browser/updates/maho_update_manager.h"

namespace maho {
namespace updates {

// Kept free of Sparkle types so the mapping is unit-testable on hosts that
// cannot compile Objective-C++ (Linux, Windows). The macOS delegate is the only
// caller.
enum class UpdateCycleOutcome {
  kFailure,
  kNoUpdateFound,
  kUserCanceled,
};

// Values from Sparkle's SUError enum (third_party/sparkle .../SUErrors.h),
// repeated as integers because that header is Objective-C. Part of Sparkle's
// public API; keep in sync with the vendored header.
inline constexpr int kSparkleNoPublicDSAFoundError = 1;
inline constexpr int kSparkleInsufficientSigningError = 2;
inline constexpr int kSparkleInsecureFeedURLError = 3;
inline constexpr int kSparkleInvalidFeedURLError = 4;
inline constexpr int kSparkleInvalidUpdaterError = 5;
inline constexpr int kSparkleInvalidHostBundleIdentifierError = 6;
inline constexpr int kSparkleInvalidHostVersionError = 7;
inline constexpr int kSparkleAppcastParseError = 1000;
inline constexpr int kSparkleNoUpdateError = 1001;
inline constexpr int kSparkleAppcastError = 1002;
inline constexpr int kSparkleResumeAppcastError = 1004;
inline constexpr int kSparkleWebKitTerminationError = 1006;
inline constexpr int kSparkleReleaseNotesError = 1007;
inline constexpr int kSparkleTemporaryDirectoryError = 2000;
inline constexpr int kSparkleDownloadError = 2001;
inline constexpr int kSparkleUnarchivingError = 3000;
inline constexpr int kSparkleSignatureError = 3001;
inline constexpr int kSparkleValidationError = 3002;
inline constexpr int kSparkleInstallationCanceledError = 4007;

// Sparkle documents SUNoUpdateError ("No new update was found") and
// SUInstallationCanceledError ("The user canceled installing") as values of the
// error parameter of -updater:didFinishUpdateCycleForUpdateCheck:error:. Both
// are normal outcomes; treating them as failures made an up-to-date app show
// "Update check failed" (updaterDidNotFindUpdate: sets kUpToDate, then this
// method's SUNoUpdateError overwrote it).
inline UpdateCycleOutcome ClassifyUpdateCycleError(bool is_sparkle_domain,
                                                   int code) {
  if (!is_sparkle_domain) {
    return UpdateCycleOutcome::kFailure;
  }
  if (code == kSparkleNoUpdateError) {
    return UpdateCycleOutcome::kNoUpdateFound;
  }
  if (code == kSparkleInstallationCanceledError) {
    return UpdateCycleOutcome::kUserCanceled;
  }
  return UpdateCycleOutcome::kFailure;
}

inline UpdateError UpdateErrorFromUpdaterCode(int code) {
  switch (code) {
    case kSparkleNoUpdateError:
    case kSparkleInstallationCanceledError:
      return UpdateError::kNone;

    case kSparkleInsufficientSigningError:
    case kSparkleSignatureError:
    case kSparkleValidationError:
      return UpdateError::kSignatureMismatch;

    case kSparkleTemporaryDirectoryError:
    case kSparkleDownloadError:
    case kSparkleUnarchivingError:
      return UpdateError::kDownloadFailed;

    case kSparkleNoPublicDSAFoundError:
    case kSparkleInsecureFeedURLError:
    case kSparkleInvalidFeedURLError:
    case kSparkleInvalidUpdaterError:
    case kSparkleInvalidHostBundleIdentifierError:
    case kSparkleInvalidHostVersionError:
    case kSparkleAppcastParseError:
    case kSparkleAppcastError:
    case kSparkleResumeAppcastError:
    case kSparkleWebKitTerminationError:
    case kSparkleReleaseNotesError:
      return UpdateError::kConnectionFailed;

    default:
      // Install-phase codes (4000-4012) and the disk-image / translocated
      // refusals (1003, 1005). No taxonomy entry fits; guessing would
      // misreport the cause.
      return UpdateError::kUnknown;
  }
}

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_PLATFORM_UPDATER_ERROR_MAPPING_H_
