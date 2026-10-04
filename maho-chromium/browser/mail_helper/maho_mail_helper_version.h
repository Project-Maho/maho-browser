// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_VERSION_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_VERSION_H_

namespace maho::mail_helper {

// Compiled-in version token for the mail helper protocol/shell. The host
// (MahoMailHelperLauncher) passes this value as the expected version and
// compares it against the version the helper reports via GetVersion(). Because
// both the browser and the helper executable compile this same constant, an
// in-build pair always matches; a stale helper binary left on disk from an
// older build carries an older constant and is rejected (kill + relaunch).
//
// Bump this whenever the helper Mojo contract or handshake changes in a way
// that makes an older helper binary incompatible with a newer browser.
inline constexpr char kMahoMailHelperVersion[] = "3";

}  // namespace maho::mail_helper

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_VERSION_H_
