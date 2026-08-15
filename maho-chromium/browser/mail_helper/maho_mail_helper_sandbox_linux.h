// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_SANDBOX_LINUX_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_SANDBOX_LINUX_H_

#include "base/files/file_path.h"

namespace maho::mail_helper {

bool CloseLinuxMailHelperInheritedFds(int mojo_fd,
                                      int oauth_loopback_listener_fd);

// Engages the Linux syscall-broker and seccomp layers. This must run while the
// helper is single-threaded and before any Mail FFI call. Sandboxing is
// irreversible; failure is fail-closed.
bool InitializeLinuxMailHelperSandbox(const base::FilePath& mail_root,
                                      int oauth_loopback_listener_fd = -1);

bool IsLinuxMailHelperSandboxEngagedForTesting();
void SetLinuxMailHelperSandboxFailureForTesting(bool fail);

}  // namespace maho::mail_helper

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_SANDBOX_LINUX_H_
